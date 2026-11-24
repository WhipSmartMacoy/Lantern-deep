// Lantern GPU - drawing engine for Lantern Deep (milestone 3)
//
// The CPU sends drawing commands through a small register window (LiteX "example slave" at 0x8000_0000).
// The GPU draws them into the back framebuffer while the scan-out shows the front one; PRESENT swaps the two
// at the start of vertical blanking, so the picture never tears.
//
// Milestone 1: register window + command FIFO, two RGB555 framebuffers in block RAM, our own video timing,
//              scan-out with brightness and vignette, CLIP / FILL / SHADE / PRESENT.
// Milestone 2: VRAM in the Pocket's 128K x 16 SRAM (uploads through VADDR/VDATA), light tables (LUT),
//              BLIT (key, flip, outline, tint, see-through, light table, scaling), GLOW and SEE.
// Milestone 3: pipelined blitter (one SRAM read every SRAM_RD_CYCLES, processing overlapped with the next read),
//              GRAD flag: smooth lighting, the light level interpolated across the image (bilinear) and dithered.
//
// Registers (byte offsets from 0x8000_0000):
//   +0x00 CMD     W  command FIFO (the CPU stalls while it is full)
//   +0x04 STATUS  R  [15:0] free FIFO words, [16] busy, [17] upload pending,
//                    [18] save state wanted (sleep), [19] save state to restore (wake), [31:24] frames shown
//   +0x18 SSDONE  W  [0] save state written to its buffer, [1] save state restored
//   +0x08 VADDR   W  VRAM upload address (16-bit word address)
//   +0x0C VDATA   W  two pixels: [15:0] at VADDR, [31:16] at VADDR+1; VADDR += 2
//   +0x10 LUT     W  [31:24] table 0..23, [23:22] channel (0 R, 1 G, 2 B), [21:16] entry, [15:0] value
//   +0x14 ID      R  0x4C475033 "LGP3"
//
// Command reference: src/gpu.c in the game sources (the C model there defines the exact behaviour).
// VRAM ordering rule (kept by the game's driver): the CPU never overwrites VRAM that queued commands still use;
// when it recycles VRAM it first waits until STATUS.busy is 0.

module lantern_gpu #(
    parameter [2:0] SRAM_RD_CYCLES = 3'd4  // SRAM read: address to data capture, in 57.12 MHz cycles (4 = 70 ns; the AS6C2016 needs 55 ns)
) (
    input wire clk_sys,   // 57.12 MHz
    input wire reset,

    // Wishbone slave (classic cycles from the VexRiscv, uncached)
    input  wire [29:0] wb_adr,
    input  wire [31:0] wb_dat_w,
    output reg  [31:0] wb_dat_r = 0,
    input  wire [ 3:0] wb_sel,
    input  wire        wb_cyc,
    input  wire        wb_stb,
    input  wire        wb_we,
    output reg         wb_ack = 0,
    output wire        wb_err,

    // VRAM: asynchronous SRAM, 128K x 16 (AS6C2016-55)
    output reg  [16:0] sram_a = 0,
    input  wire [15:0] sram_dq_in,
    output reg  [15:0] sram_dq_out = 0,
    output reg         sram_dq_oe = 0,
    output reg         sram_oe_n = 1,
    output reg         sram_we_n = 1,

    // Save states (sleep / wake): requests arrive as toggles from the bridge (clk_74a), answers leave as toggles
    input  wire        ss_start_tgl,
    input  wire        ss_load_tgl,
    output reg         ss_save_done_tgl = 0,
    output reg         ss_load_done_tgl = 0,

    // Video (5.712 MHz, same PLL as clk_sys: exactly clk_sys / 10)
    input  wire        clk_vid,
    output reg  [23:0] vid_rgb = 0,
    output reg         vid_de = 0,
    output reg         vid_hs = 0,
    output reg         vid_vs = 0
);
  assign wb_err = 1'b0;

  localparam [15:0] SCR_W = 16'd266;

  // ------------------------------------------------------------------------------------------------
  // Command FIFO: 1024 x 32 in block RAM, registered read
  // ------------------------------------------------------------------------------------------------
  localparam FIFO_DEPTH = 1024;

  (* ramstyle = "M10K, no_rw_check" *) reg [31:0] fifo_mem[0:FIFO_DEPTH-1];
  reg  [ 9:0] fifo_wp = 0;
  reg  [ 9:0] fifo_rp = 0;
  reg  [10:0] fifo_cnt = 0;
  reg  [31:0] fifo_q = 0;
  wire        fifo_full = fifo_cnt == FIFO_DEPTH;
  wire        fifo_empty = fifo_cnt == 0;
  wire        fifo_wr;
  wire        fifo_rd;

  always @(posedge clk_sys) begin
    if (fifo_wr) fifo_mem[fifo_wp] <= wb_dat_w;
    if (fifo_rd) fifo_q <= fifo_mem[fifo_rp];
  end

  always @(posedge clk_sys) begin
    if (reset) begin
      fifo_wp  <= 0;
      fifo_rp  <= 0;
      fifo_cnt <= 0;
    end else begin
      if (fifo_wr) fifo_wp <= fifo_wp + 10'd1;
      if (fifo_rd) fifo_rp <= fifo_rp + 10'd1;
      fifo_cnt <= fifo_cnt + (fifo_wr ? 11'd1 : 11'd0) - (fifo_rd ? 11'd1 : 11'd0);
    end
  end

  // ------------------------------------------------------------------------------------------------
  // Light tables: 24 tables, per channel. Written by the CPU, read by BLIT with the LUT flag (1 cycle).
  // Stored as the channel bits only: R 5 bits, G 6 bits, B 5 bits.
  // ------------------------------------------------------------------------------------------------
  (* ramstyle = "MLAB, no_rw_check" *) reg [4:0] lut_r[0:767];
  (* ramstyle = "MLAB, no_rw_check" *) reg [5:0] lut_g[0:1535];
  (* ramstyle = "MLAB, no_rw_check" *) reg [4:0] lut_b[0:767];
  reg  [4:0] lut_rq, lut_bq;
  reg  [5:0] lut_gq;
  wire       lut_wr;
  wire [4:0] lut_wt = wb_dat_w[28:24];  // table (0..23)
  wire [1:0] lut_wc = wb_dat_w[23:22];
  wire [5:0] lut_we = wb_dat_w[21:16];
  reg  [4:0] b_lut;  // table of the current BLIT (without GRAD)
  reg  [4:0] done_lut;  // table of the pixel whose VRAM word just arrived
  wire [15:0] lut_src;  // source pixel being looked up (the SRAM word)

  always @(posedge clk_sys) begin
    if (lut_wr && lut_wc == 2'd0) lut_r[{lut_wt, lut_we[4:0]}] <= wb_dat_w[15:11];
    if (lut_wr && lut_wc == 2'd1) lut_g[{lut_wt, lut_we}] <= wb_dat_w[10:5];
    if (lut_wr && lut_wc == 2'd2) lut_b[{lut_wt, lut_we[4:0]}] <= wb_dat_w[4:0];
    lut_rq <= lut_r[{done_lut, lut_src[15:11]}];
    lut_gq <= lut_g[{done_lut, lut_src[10:5]}];
    lut_bq <= lut_b[{done_lut, lut_src[4:0]}];
  end

  // ------------------------------------------------------------------------------------------------
  // Wishbone register window
  // ------------------------------------------------------------------------------------------------
  wire [2:0] reg_idx = wb_adr[2:0];
  wire       wb_req = wb_cyc && wb_stb && !wb_ack;

  // VRAM upload: one VDATA word (two pixels) waits here until the SRAM controller has written it
  reg        up_pend = 0;
  reg [16:0] up_addr = 0;
  reg [31:0] up_data = 0;
  reg [16:0] vaddr = 0;
  wire       up_done;  // pulse from the SRAM controller: both pixels written

  // a CMD write waits while the FIFO is full; VADDR/VDATA wait while an upload is still being written
  wire wr_cmd = wb_req && wb_we && reg_idx == 3'd0;
  wire wr_vaddr = wb_req && wb_we && reg_idx == 3'd2;
  wire wr_vdata = wb_req && wb_we && reg_idx == 3'd3;
  assign fifo_wr = wr_cmd && !fifo_full && !reset;
  assign lut_wr = wb_req && wb_we && reg_idx == 3'd4;

  reg  [ 7:0] frames = 0;
  wire        busy;
  reg  [ 2:0] ss_start_s = 0, ss_load_s = 0;
  always @(posedge clk_sys) begin
    ss_start_s <= {ss_start_s[1:0], ss_start_tgl};
    ss_load_s  <= {ss_load_s[1:0], ss_load_tgl};
  end
  wire ss_save_req = ss_start_s[2] != ss_save_done_tgl;
  wire ss_load_req = ss_load_s[2] != ss_load_done_tgl;
  wire [15:0] fifo_free = 16'd1024 - {5'd0, fifo_cnt};

  always @(posedge clk_sys) begin
    wb_ack <= 1'b0;
    if (up_done) up_pend <= 1'b0;
    if (wb_req) begin
      if (wb_we) begin
        if (wr_cmd) begin
          if (!fifo_full || reset) wb_ack <= 1'b1;
        end else if (wr_vaddr) begin
          if (!up_pend) begin
            vaddr  <= wb_dat_w[16:0];
            wb_ack <= 1'b1;
          end
        end else if (wr_vdata) begin
          if (!up_pend) begin
            up_addr <= vaddr;
            up_data <= wb_dat_w;
            up_pend <= 1'b1;
            vaddr   <= vaddr + 17'd2;
            wb_ack  <= 1'b1;
          end
        end else begin
          wb_ack <= 1'b1;  // LUT (written above), SSDONE and anything else
          if (reg_idx == 3'd6) begin
            if (wb_dat_w[0]) ss_save_done_tgl <= ss_start_s[2];
            if (wb_dat_w[1]) ss_load_done_tgl <= ss_load_s[2];
          end
        end
      end else begin
        wb_ack <= 1'b1;
        case (reg_idx)
          3'd1: wb_dat_r <= {frames, 4'd0, ss_load_req, ss_save_req, up_pend, busy, fifo_free};
          3'd5: wb_dat_r <= 32'h4C475033;
          default: wb_dat_r <= 32'd0;
        endcase
      end
    end
  end

  // ------------------------------------------------------------------------------------------------
  // SRAM controller: one 16-bit read or write at a time. Uploads go first, then the blitter's reads.
  //   read : address out, OE low; data captured SRAM_RD_CYCLES cycles later
  //   write: address + data out, WE low for 4 cycles (70 ns; 45 ns needed), data held one more cycle
  // ------------------------------------------------------------------------------------------------
  localparam [1:0] M_IDLE = 2'd0, M_RD = 2'd1, M_WR = 2'd2;
  reg  [ 1:0] m_state = M_IDLE;
  reg  [ 2:0] m_cnt = 0;
  reg  [15:0] m_q = 0;
  reg         m_done = 0;  // pulse: m_q holds the word just read
  reg         up_half = 0;  // which pixel of the pending upload
  reg         up_done_r = 0;
  assign up_done = up_done_r;
  wire        m_idle = m_state == M_IDLE;
  wire        m_last = m_state == M_RD && m_cnt == 3'd0;  // a read completes at the end of this cycle
  wire        rd_go;  // the blitter starts a read this cycle (when the SRAM is free or just finishing, no upload waiting)
  wire [16:0] rd_addr;
  // the pixel each read belongs to: "pend" = read in flight, "done" = word in m_q (processed while m_done)
  wire [15:0] iss_faddr;
  wire [ 4:0] iss_lut;
  wire        iss_lit;  // GRAD: this pixel goes through a light table (level < 7)
  reg  [15:0] pend_faddr = 0, done_faddr = 0;
  reg  [ 4:0] pend_lut = 0;
  reg         pend_lit = 0, done_lit = 0;

  always @(posedge clk_sys) begin
    m_done    <= 1'b0;
    up_done_r <= 1'b0;
    if (reset) begin
      m_state    <= M_IDLE;
      sram_oe_n  <= 1'b1;
      sram_we_n  <= 1'b1;
      sram_dq_oe <= 1'b0;
      up_half    <= 1'b0;
    end else begin
      case (m_state)
        M_IDLE: begin
          if (up_pend && !up_done_r) begin
            sram_a      <= up_addr + {16'd0, up_half};
            sram_dq_out <= up_half ? up_data[31:16] : up_data[15:0];
            sram_dq_oe  <= 1'b1;
            sram_oe_n   <= 1'b1;
            m_cnt       <= 3'd0;
            m_state     <= M_WR;
          end else if (rd_go) begin
            sram_a     <= rd_addr;
            sram_oe_n  <= 1'b0;
            m_cnt      <= SRAM_RD_CYCLES - 3'd1;
            m_state    <= M_RD;
            pend_faddr <= iss_faddr;
            pend_lut   <= iss_lut;
            pend_lit   <= iss_lit;
          end
        end
        M_RD: begin
          if (m_cnt == 3'd0) begin
            m_q        <= sram_dq_in;
            m_done     <= 1'b1;
            done_faddr <= pend_faddr;
            done_lut   <= pend_lut;
            done_lit   <= pend_lit;
            if (rd_go) begin  // next read straight away: one read every SRAM_RD_CYCLES
              sram_a     <= rd_addr;
              m_cnt      <= SRAM_RD_CYCLES - 3'd1;
              pend_faddr <= iss_faddr;
              pend_lut   <= iss_lut;
              pend_lit   <= iss_lit;
            end else begin
              sram_oe_n <= 1'b1;
              m_state   <= M_IDLE;
            end
          end else begin
            m_cnt <= m_cnt - 3'd1;
          end
        end
        M_WR: begin
          m_cnt <= m_cnt + 3'd1;
          case (m_cnt)
            3'd0: sram_we_n <= 1'b0;
            3'd1, 3'd2, 3'd3: ;
            3'd4: sram_we_n <= 1'b1;
            default: begin  // data held one cycle after WE went high
              sram_dq_oe <= 1'b0;
              m_state    <= M_IDLE;
              up_half    <= ~up_half;
              if (up_half) up_done_r <= 1'b1;
            end
          endcase
        end
        default: m_state <= M_IDLE;
      endcase
    end
  end

  // ------------------------------------------------------------------------------------------------
  // Framebuffers: two 266x240 RGB555 buffers, port A = GPU (clk_sys), port B = scan-out (clk_vid)
  // ------------------------------------------------------------------------------------------------
  reg  [15:0] fa_addr;
  reg  [14:0] fa_data;
  reg         fa_we;
  wire [14:0] fa_q0, fa_q1;
  reg  [15:0] fb_addr = 0;  // scan-out address (clk_vid)
  wire [14:0] fb_q0, fb_q1;

  wire back;  // buffer the GPU draws into: the one the scan-out is not showing

  lantern_fb fb0 (
      .clk_a (clk_sys),
      .we_a  (fa_we && back == 1'b0),
      .addr_a(fa_addr),
      .d_a   (fa_data),
      .q_a   (fa_q0),
      .clk_b (clk_vid),
      .addr_b(fb_addr),
      .q_b   (fb_q0)
  );
  lantern_fb fb1 (
      .clk_a (clk_sys),
      .we_a  (fa_we && back == 1'b1),
      .addr_a(fa_addr),
      .d_a   (fa_data),
      .q_a   (fa_q1),
      .clk_b (clk_vid),
      .addr_b(fb_addr),
      .q_b   (fb_q1)
  );
  wire [14:0] fa_q = back ? fa_q1 : fa_q0;

  // ------------------------------------------------------------------------------------------------
  // Command processor
  // ------------------------------------------------------------------------------------------------
  localparam [4:0]
    S_FETCH = 5'd0,   // wait for a word
    S_CAP   = 5'd1,   // FIFO output valid: store it
    S_EXEC  = 5'd2,   // all words of the command are here
    S_RECT  = 5'd3,   // clip the rectangle
    S_ROW   = 5'd4,   // start of a row: row address
    S_FILL  = 5'd5,   // FILL: one pixel per cycle
    S_SRD   = 5'd6,   // SHADE: read
    S_SWR   = 5'd7,   // SHADE: write
    S_PRES  = 5'd8,   // PRESENT: latch brightness / vignette
    S_PREQ  = 5'd9,   // PRESENT: request the swap
    S_PWAIT = 5'd10,  // PRESENT: wait for the swap at vblank
    S_ROW2  = 5'd11,  // BLIT/GLOW row setup: source row offset
    S_ROW3  = 5'd12,  // BLIT/GLOW row setup: source row address, see-through widths
    S_PIX   = 5'd13,  // BLIT/GLOW: walk the pixels, start each one's VRAM read
    S_DRAIN = 5'd14,  // BLIT/GLOW: all reads started; wait until the last pixel is written
    S_GRAD1 = 5'd15,  // GRAD setup: products
    S_GRAD2 = 5'd16,  // GRAD setup: second products
    S_RECT2 = 5'd17,  // BLIT/GLOW setup: first source column (timing: split from S_RECT)
    S_RECT3 = 5'd18,  // BLIT/GLOW setup: source column x step
    S_GRAD3 = 5'd19;  // GRAD setup: light at the first pixel and the steps

  localparam [7:0] OP_CLIP = 8'd1, OP_FILL = 8'd2, OP_SHADE = 8'd3, OP_BLIT = 8'd4,
                   OP_GLOW = 8'd5, OP_SEE = 8'd6, OP_PRESENT = 8'd7;

  // BLIT flags
  localparam F_KEY = 0, F_FLIPX = 1, F_OUTLINE = 2, F_TINT = 3, F_SEE = 4, F_LUT = 5, F_GRAD = 6;

  reg [4:0] state = S_FETCH;
  assign fifo_rd = state == S_FETCH && !fifo_empty && !reset;
  assign busy = state != S_FETCH || !fifo_empty;

  reg [31:0] w0, w1, w2, w3, w4, w5, w6, w7, w8, w9;  // command words (SEE widths go to their own RAM)
  reg [7:0] widx;  // index of the next word to store
  reg [7:0] need;  // words in the current command
  wire [7:0] op = w0[7:0];

  // clip rectangle (pixels x0 <= x < x1, y0 <= y < y1)
  reg [8:0] clip_x0 = 0, clip_y0 = 0, clip_x1 = 266, clip_y1 = 240;

  // rectangle walk
  reg [8:0] rx0, rx1, ry1;  // clipped column range and row end
  reg [8:0] cx, cy;  // current pixel
  reg [15:0] row_addr;  // cy * 266
  reg [14:0] fill_col;  // FILL colour / BLIT tint (RGB555)
  reg [7:0] shade_k;

  // BLIT / GLOW
  reg [7:0] b_flags;
  reg [16:0] b_src;  // image (BLIT) or mask (GLOW) in VRAM
  reg [15:0] b_stride, b_sx, b_sy;
  reg signed [17:0] b_x, b_y, b_w;
  reg [7:0] g_r, g_g, g_b;  // GLOW colour
  reg [25:0] acc, acc0;  // source column, 8.8 fixed point
  reg [25:0] row_mul;  // per-row product
  reg [16:0] rowsrc;  // VRAM address of the source row (GLOW: of column rx0)
  reg [15:0] v;  // VRAM word being processed
  reg dst_glow;  // P_DST: GLOW blend (else outline darkening)
  reg [4:0] dst_k;  // GLOW strength of the pixel in P_DST
  // processing of the word that arrived: most pixels are written in the m_done cycle; these take one more
  localparam [1:0] P_IDLE = 2'd0, P_DST = 2'd1, P_LUT = 2'd2;
  reg [1:0] post = P_IDLE;
  // GRAD: light level in 16.16 fixed point, L(x, y) = A + B*x + C*y + D*x*y over the blit's output pixels
  reg signed [31:0] g_row, g_rowstep, g_bx, g_cur, g_t1, g_t2, g_t3;
  reg [8:0] g_relx, g_rely;
  reg signed [17:0] f_col;  // first visible source column (FLIPX counts from the right)
  reg signed [31:0] g_t4, g_t5, g_ab;

  // SEE: cut-out half-widths for consecutive rows, two per word
  (* ramstyle = "M10K, no_rw_check" *) reg [31:0] see_ram[0:127];
  reg [31:0] see_q;
  reg [6:0] see_rd;
  reg signed [17:0] see_cx, see_top;
  reg [7:0] see_n;
  reg see_act;  // this row has a cut-out
  reg [15:0] see_w, see_i;  // outer / inner half-width for this row
  reg see_half;
  wire [7:0] see_wi = widx - 8'd2;
  always @(posedge clk_sys) begin
    if (state == S_CAP && widx >= 8'd2 && op == OP_SEE) see_ram[see_wi[6:0]] <= fifo_q;
    see_q <= see_ram[see_rd];
  end

  // PRESENT -> scan-out handshake (toggle request / toggle acknowledge)
  reg        swap_req = 0;
  reg  [6:0] pres_bright = 64;
  reg        pres_vig = 0;
  wire       swap_ack_s;  // swap_ack synchronised into clk_sys

  // words needed by an opcode, from its first word
  function automatic [7:0] cmd_words(input [31:0] c);
    case (c[7:0])
      OP_CLIP: cmd_words = 8'd3;
      OP_FILL: cmd_words = 8'd4;
      OP_SHADE: cmd_words = 8'd3;
      OP_BLIT: cmd_words = c[8+6] ? 8'd10 : 8'd6;  // GRAD adds A, B, C, D
      OP_GLOW: cmd_words = 8'd5;
      OP_SEE: cmd_words = 8'd2 + {1'b0, c[15:9]} + {7'd0, c[8]};  // 2 + ceil(n / 2)
      default: cmd_words = 8'd1;  // PRESENT and unknown opcodes
    endcase
  endfunction

  // signed 16-bit helpers
  function automatic signed [17:0] s16(input [15:0] x);
    s16 = {{2{x[15]}}, x};
  endfunction
  function automatic [8:0] clamp_w(input signed [17:0] x);  // to 0..266
    clamp_w = x < 0 ? 9'd0 : x > 18'sd266 ? 9'd266 : x[8:0];
  endfunction
  function automatic [8:0] clamp_h(input signed [17:0] x);  // to 0..240
    clamp_h = x < 0 ? 9'd0 : x > 18'sd240 ? 9'd240 : x[8:0];
  endfunction

  // per-channel scale by k/64 (k <= 64 in practice; larger values saturate)
  function automatic [4:0] scale5(input [4:0] c, input [7:0] k);
    reg [12:0] p;
    begin
      p = c * k;
      scale5 = p[12:6] > 7'd31 ? 5'd31 : p[10:6];
    end
  endfunction
  function automatic [14:0] shade555(input [14:0] c, input [7:0] k);
    shade555 = {scale5(c[14:10], k), scale5(c[9:5], k), scale5(c[4:0], k)};
  endfunction
  function automatic [14:0] to555(input [15:0] c);
    to555 = {c[15:11], c[10:6], c[4:0]};
  endfunction

  // GLOW: dst += colour * k / 16 per channel, saturating (green on its 6-bit scale)
  function automatic [14:0] glow555(input [14:0] o, input [4:0] k, input [7:0] ar, input [7:0] ag, input [7:0] ab);
    reg [12:0] pr, pg, pb;
    reg [9:0] r, g, b;
    begin
      pr = ar * k;
      pg = ag * k;
      pb = ab * k;
      r = {5'd0, o[14:10]} + {1'b0, pr[12:4]};
      g = {4'd0, o[9:5], 1'b0} + {1'b0, pg[12:4]};
      b = {5'd0, o[4:0]} + {1'b0, pb[12:4]};
      glow555 = {r > 10'd31 ? 5'd31 : r[4:0], g > 10'd63 ? 5'd31 : g[5:1], b > 10'd31 ? 5'd31 : b[4:0]};
    end
  endfunction

  // the command's rectangle intersected with the clip rectangle
  wire [31:0] xy_word = op == OP_BLIT ? w3 : op == OP_GLOW ? w2 : w1;
  wire [31:0] wh_word = op == OP_BLIT ? w4 : op == OP_GLOW ? w3 : w2;
  wire signed [17:0] r_x = s16(xy_word[15:0]);
  wire signed [17:0] r_y = s16(xy_word[31:16]);
  wire signed [17:0] r_xe = r_x + s16(wh_word[15:0]);
  wire signed [17:0] r_ye = r_y + s16(wh_word[31:16]);
  wire [8:0] r_x0 = clamp_w(r_x) > clip_x0 ? clamp_w(r_x) : clip_x0;
  wire [8:0] r_y0 = clamp_h(r_y) > clip_y0 ? clamp_h(r_y) : clip_y0;
  wire [8:0] r_x1 = clamp_w(r_xe) < clip_x1 ? clamp_w(r_xe) : clip_x1;
  wire [8:0] r_y1 = clamp_h(r_ye) < clip_y1 ? clamp_h(r_ye) : clip_y1;

  // BLIT: source column of the first visible pixel (rel = r_x0 - x; FLIPX counts from the right)
  wire signed [17:0] first_rel = $signed({9'd0, rx0}) - b_x;   // registered inputs (S_RECT2)
  wire [33:0] first_acc = f_col[17:0] * b_sx;

  // BLIT / GLOW pixel decisions
  wire signed [17:0] see_dx_s = $signed({9'd0, cx}) - see_cx;
  wire [17:0] see_dx = see_dx_s < 0 ? -see_dx_s : see_dx_s;
  wire see_skip = see_act && (see_dx < {2'd0, see_i} || (see_dx < {2'd0, see_w} && (cx[0] ^ cy[0])));
  wire pix_skip = op == OP_BLIT && see_skip;
  assign rd_addr = rowsrc + (op == OP_GLOW ? {8'd0, cx - rx0} : acc[24:8]);
  assign rd_go = state == S_PIX && !pix_skip && (m_idle || m_last) && !up_pend;
  assign lut_src = m_q;
  assign iss_faddr = row_addr + {7'd0, cx};
  // GRAD: light level of the pixel being issued, ordered 2x2 dither between levels
  wire [1:0] bayer = {cx[0] ^ cy[0], cy[0]};
  wire signed [31:0] g_d = g_cur + $signed({16'd0, bayer, 14'h2000});
  wire [2:0] g_lvl = g_d < 0 ? 3'd0 : g_d[31:16] > 16'sd7 ? 3'd7 : g_d[18:16];
  assign iss_lut = b_flags[F_GRAD] ? {b_lut[4:3], 3'd0} + {2'd0, g_lvl} : b_lut;
  assign iss_lit = b_flags[F_GRAD] ? g_lvl != 3'd7 : b_flags[F_LUT];

  wire bl_key = b_flags[F_KEY] && m_q == 16'd0;
  wire bl_out = b_flags[F_OUTLINE] && m_q == 16'd1;
  wire bl_lut = done_lit && !b_flags[F_TINT];
  // the word just read is written straight away (no framebuffer read, no table)
  wire bl_direct = op == OP_BLIT && !bl_key && !bl_out && !bl_lut;
  wire blitting = op == OP_BLIT || op == OP_GLOW;

  always @(*) begin
    fa_we   = 1'b0;
    fa_addr = blitting ? done_faddr : row_addr + {7'd0, cx};
    fa_data = fill_col;
    if (state == S_FILL) fa_we = 1'b1;
    else if (state == S_SWR) begin
      fa_we   = 1'b1;
      fa_data = shade555(fa_q, shade_k);
    end else if (post == P_DST) begin
      fa_we   = 1'b1;
      fa_data = dst_glow ? glow555(fa_q, dst_k, g_r, g_g, g_b) : shade555(fa_q, 8'd48);
    end else if (post == P_LUT) begin
      fa_we   = 1'b1;
      fa_data = {lut_rq, lut_gq[5:1], lut_bq};
    end else if (m_done && bl_direct) begin
      fa_we   = 1'b1;
      fa_data = b_flags[F_TINT] ? fill_col : to555(m_q);
    end
  end

  // processing of each arriving word (runs beside the pixel walker; at most 2 cycles, a read takes 4+)
  always @(posedge clk_sys) begin
    if (reset) post <= P_IDLE;
    else begin
      post <= P_IDLE;
      if (m_done && blitting) begin
        if (op == OP_GLOW) begin
          if (m_q[4:0] != 5'd0) begin
            dst_glow <= 1'b1;
            dst_k    <= m_q[4:0];
            post     <= P_DST;  // the framebuffer pixel is read this cycle (fa_addr = done_faddr)
          end
        end else if (bl_out && !b_flags[F_TINT]) begin
          dst_glow <= 1'b0;
          post     <= P_DST;
        end else if (!bl_key && !bl_out && bl_lut) begin
          post <= P_LUT;  // the light tables are read this cycle (address from m_q)
        end
      end
    end
  end

  // FILL / SHADE: advance to the next pixel; returns to S_FETCH after the last one
  task next_pixel(input [4:0] same_row_state);
    begin
      if (cx + 9'd1 < rx1) begin
        cx <= cx + 9'd1;
        state <= same_row_state;
      end else if (cy + 9'd1 < ry1) begin
        cx <= rx0;
        cy <= cy + 9'd1;
        row_addr <= row_addr + SCR_W;
        state <= same_row_state;
      end else begin
        state <= S_FETCH;
      end
    end
  endtask

  // BLIT / GLOW: advance; a new row goes through the row setup again
  task blit_next;
    begin
      if (cx + 9'd1 < rx1) begin
        cx <= cx + 9'd1;
        acc <= b_flags[F_FLIPX] ? acc - {10'd0, b_sx} : acc + {10'd0, b_sx};
        g_cur <= g_cur + g_bx;
        state <= S_PIX;
      end else if (cy + 9'd1 < ry1) begin
        cx <= rx0;
        cy <= cy + 9'd1;
        acc <= acc0;
        g_row <= g_row + g_rowstep;
        g_bx <= g_bx + $signed(w9);
        state <= S_ROW;
      end else begin
        state <= S_DRAIN;  // the last reads are still on their way
      end
    end
  endtask

  wire signed [17:0] rel_y = $signed({9'd0, cy}) - b_y;
  wire signed [17:0] see_row = $signed({9'd0, cy}) - see_top;

  always @(posedge clk_sys) begin
    if (reset) begin
      state   <= S_FETCH;
      widx    <= 0;
      clip_x0 <= 0;
      clip_y0 <= 0;
      clip_x1 <= 266;
      clip_y1 <= 240;
      see_n   <= 0;
    end else begin
      case (state)
        S_FETCH: if (!fifo_empty) state <= S_CAP;

        S_CAP: begin
          case (widx)
            8'd0: begin
              w0   <= fifo_q;
              need <= cmd_words(fifo_q);
            end
            8'd1: w1 <= fifo_q;
            8'd2: w2 <= fifo_q;
            8'd3: w3 <= fifo_q;
            8'd4: w4 <= fifo_q;
            8'd5: w5 <= fifo_q;
            8'd6: w6 <= fifo_q;
            8'd7: w7 <= fifo_q;
            8'd8: w8 <= fifo_q;
            8'd9: w9 <= fifo_q;
            default: ;
          endcase
          if ((widx == 8'd0 ? cmd_words(fifo_q) : need) == widx + 8'd1) begin
            widx  <= 0;
            state <= S_EXEC;
          end else begin
            widx  <= widx + 8'd1;
            state <= S_FETCH;
          end
        end

        S_EXEC: begin
          state <= S_FETCH;
          case (op)
            OP_CLIP: begin
              clip_x0 <= clamp_w(s16(w1[15:0]));
              clip_y0 <= clamp_h(s16(w1[31:16]));
              clip_x1 <= clamp_w(s16(w2[15:0]));
              clip_y1 <= clamp_h(s16(w2[31:16]));
            end
            OP_FILL: begin
              fill_col <= to555(w3[15:0]);
              state <= S_RECT;
            end
            OP_SHADE: begin
              shade_k <= w0[15:8];
              state   <= S_RECT;
            end
            OP_BLIT: begin
              b_flags  <= w0[15:8];
              b_lut    <= w0[31:24] > 8'd23 ? 5'd23 : w0[28:24];
              b_src    <= w1[16:0];
              b_stride <= w2[15:0];
              fill_col <= to555(w2[31:16]);  // tint
              b_x      <= s16(w3[15:0]);
              b_y      <= s16(w3[31:16]);
              b_w      <= s16(w4[15:0]);
              b_sx     <= w5[15:0];
              b_sy     <= w5[31:16];
              state    <= S_RECT;
            end
            OP_GLOW: begin
              b_flags <= 8'd0;
              b_src   <= w1[16:0];
              b_x     <= s16(w2[15:0]);
              b_y     <= s16(w2[31:16]);
              b_w     <= s16(w3[15:0]);
              g_r     <= w4[7:0];
              g_g     <= w4[15:8];
              g_b     <= w4[23:16];
              state   <= S_RECT;
            end
            OP_SEE: begin
              see_cx  <= s16(w1[15:0]);
              see_top <= s16(w1[31:16]);
              see_n   <= w0[15:8];
            end
            OP_PRESENT: state <= S_PRES;
            default: ;
          endcase
        end

        S_RECT: begin
          if (r_x0 < r_x1 && r_y0 < r_y1) begin
            rx0   <= r_x0;
            rx1   <= r_x1;
            ry1   <= r_y1;
            cx    <= r_x0;
            cy    <= r_y0;
            state <= blitting ? S_RECT2 : S_ROW;
          end else begin
            state <= S_FETCH;
          end
        end

        S_RECT2: begin
          f_col  <= b_flags[F_FLIPX] ? b_w - 18'sd1 - first_rel : first_rel;
          g_relx <= rx0 - b_x[8:0];
          g_rely <= cy - b_y[8:0];
          state  <= S_RECT3;
        end

        S_RECT3: begin
          acc   <= first_acc[25:0];
          acc0  <= first_acc[25:0];
          state <= op == OP_BLIT && b_flags[F_GRAD] ? S_GRAD1 : S_ROW;
        end

        S_ROW: begin  // row_addr = cy * 266 = cy*256 + cy*8 + cy*2
          row_addr <= {cy[7:0], 8'd0} + {4'd0, cy, 3'd0} + {6'd0, cy, 1'd0};
          see_rd <= see_row[7:1];
          see_half <= see_row[0];
          see_act <= 1'b0;
          if (op == OP_BLIT) begin
            row_mul <= rel_y[15:0] * b_sy;  // source row, 8.8
            state   <= S_ROW2;
          end else if (op == OP_GLOW) begin
            row_mul <= rel_y[15:0] * b_w[15:0];  // mask rows are w words long
            state   <= S_ROW2;
          end else begin
            state <= op == OP_FILL ? S_FILL : S_SRD;
          end
        end

        S_ROW2: begin
          if (op == OP_BLIT) row_mul <= row_mul[25:8] * b_stride;
          else row_mul <= row_mul + {17'd0, rx0} - {{8{b_x[17]}}, b_x};  // + (rx0 - x); x may be negative
          // see-through widths for this row (the SEE RAM word was read in S_ROW)
          see_act <= b_flags[F_SEE] && see_row >= 0 && see_row < $signed({10'd0, see_n});
          state <= S_ROW3;
        end

        S_GRAD1: begin  // B*relx, C*rely, D*relx
          g_t1  <= $signed(w7) * $signed({1'b0, g_relx});
          g_t2  <= $signed(w8) * $signed({1'b0, g_rely});
          g_t3  <= $signed(w9) * $signed({1'b0, g_relx});
          state <= S_GRAD2;
        end

        S_GRAD2: begin
          g_t4      <= g_t3 * $signed({1'b0, g_rely});
          g_t5      <= $signed(w9) * $signed({1'b0, g_rely});
          g_ab      <= $signed(w6) + g_t1 + g_t2;
          g_rowstep <= $signed(w8) + g_t3;
          state     <= S_GRAD3;
        end

        S_GRAD3: begin  // light at the first visible pixel, and how it changes per column
          g_row <= g_ab + g_t4;
          g_bx  <= $signed(w7) + g_t5;
          state <= S_ROW;
        end

        S_ROW3: begin
          g_cur  <= g_row;
          rowsrc <= b_src + row_mul[16:0];
          see_w  <= see_half ? see_q[31:16] : see_q[15:0];
          see_i  <= ((see_half ? see_q[31:16] : see_q[15:0]) * 16'd3) >> 2;
          if ((see_half ? see_q[31:16] : see_q[15:0]) == 16'd0) see_act <= 1'b0;
          state <= S_PIX;
        end

        S_PIX: begin  // skipped pixels take one cycle; others wait for the SRAM (the read starts as we move on)
          if (pix_skip || rd_go) blit_next();
        end

        S_DRAIN: begin
          if (m_idle && !m_done && post == P_IDLE) state <= S_FETCH;
        end

        S_FILL: next_pixel(S_FILL);
        S_SRD:  state <= S_SWR;
        S_SWR:  next_pixel(S_SRD);

        S_PRES: begin
          pres_bright <= w0[15:8] > 8'd64 ? 7'd64 : w0[14:8];
          pres_vig <= w0[16];
          state <= S_PREQ;
        end

        S_PREQ: begin  // one cycle after the values above settled
          swap_req <= ~swap_req;
          state <= S_PWAIT;
        end

        S_PWAIT: begin  // the scan-out swaps at the start of the next vblank (at most one frame away)
          if (swap_req == swap_ack_s) begin
            frames <= frames + 8'd1;
            state  <= S_FETCH;
          end
        end

        default: state <= S_FETCH;
      endcase
    end
  end

  // ------------------------------------------------------------------------------------------------
  // Scan-out (clk_vid)
  // ------------------------------------------------------------------------------------------------
  // ---- vignette table: generated by hw/gen_vig.py (begin) ----
  // generated by hw/gen_vig.py - vignette band edges per row: {edge2, edge1, edge0} (distance from centre)
  function automatic [23:0] vig_row(input [7:0] y);
    case (y)
      8'd0: vig_row = {8'd80, 8'd57, 8'd0};
      8'd1: vig_row = {8'd82, 8'd59, 8'd0};
      8'd2: vig_row = {8'd83, 8'd61, 8'd8};
      8'd3: vig_row = {8'd85, 8'd63, 8'd17};
      8'd4: vig_row = {8'd86, 8'd64, 8'd22};
      8'd5: vig_row = {8'd87, 8'd66, 8'd27};
      8'd6: vig_row = {8'd88, 8'd68, 8'd30};
      8'd7: vig_row = {8'd90, 8'd69, 8'd34};
      8'd8: vig_row = {8'd91, 8'd71, 8'd37};
      8'd9: vig_row = {8'd92, 8'd72, 8'd40};
      8'd10: vig_row = {8'd93, 8'd74, 8'd42};
      8'd11: vig_row = {8'd94, 8'd75, 8'd45};
      8'd12: vig_row = {8'd95, 8'd76, 8'd47};
      8'd13: vig_row = {8'd96, 8'd78, 8'd49};
      8'd14: vig_row = {8'd97, 8'd79, 8'd51};
      8'd15: vig_row = {8'd99, 8'd80, 8'd53};
      8'd16: vig_row = {8'd100, 8'd82, 8'd55};
      8'd17: vig_row = {8'd101, 8'd83, 8'd57};
      8'd18: vig_row = {8'd101, 8'd84, 8'd58};
      8'd19: vig_row = {8'd102, 8'd85, 8'd60};
      8'd20: vig_row = {8'd103, 8'd86, 8'd61};
      8'd21: vig_row = {8'd104, 8'd87, 8'd63};
      8'd22: vig_row = {8'd105, 8'd88, 8'd65};
      8'd23: vig_row = {8'd106, 8'd89, 8'd66};
      8'd24: vig_row = {8'd107, 8'd90, 8'd67};
      8'd25: vig_row = {8'd108, 8'd91, 8'd69};
      8'd26: vig_row = {8'd109, 8'd92, 8'd70};
      8'd27: vig_row = {8'd109, 8'd93, 8'd71};
      8'd28: vig_row = {8'd110, 8'd94, 8'd72};
      8'd29: vig_row = {8'd111, 8'd95, 8'd74};
      8'd30: vig_row = {8'd112, 8'd96, 8'd75};
      8'd31: vig_row = {8'd113, 8'd97, 8'd76};
      8'd32: vig_row = {8'd113, 8'd98, 8'd77};
      8'd33: vig_row = {8'd114, 8'd99, 8'd78};
      8'd34: vig_row = {8'd115, 8'd99, 8'd79};
      8'd35: vig_row = {8'd115, 8'd100, 8'd80};
      8'd36: vig_row = {8'd116, 8'd101, 8'd81};
      8'd37: vig_row = {8'd117, 8'd102, 8'd82};
      8'd38: vig_row = {8'd117, 8'd103, 8'd83};
      8'd39: vig_row = {8'd118, 8'd103, 8'd84};
      8'd40: vig_row = {8'd119, 8'd104, 8'd85};
      8'd41: vig_row = {8'd119, 8'd105, 8'd86};
      8'd42: vig_row = {8'd120, 8'd105, 8'd86};
      8'd43: vig_row = {8'd121, 8'd106, 8'd87};
      8'd44: vig_row = {8'd121, 8'd107, 8'd88};
      8'd45: vig_row = {8'd122, 8'd107, 8'd89};
      8'd46: vig_row = {8'd122, 8'd108, 8'd90};
      8'd47: vig_row = {8'd123, 8'd109, 8'd90};
      8'd48: vig_row = {8'd123, 8'd109, 8'd91};
      8'd49: vig_row = {8'd124, 8'd110, 8'd92};
      8'd50: vig_row = {8'd124, 8'd111, 8'd93};
      8'd51: vig_row = {8'd125, 8'd111, 8'd93};
      8'd52: vig_row = {8'd125, 8'd112, 8'd94};
      8'd53: vig_row = {8'd126, 8'd112, 8'd95};
      8'd54: vig_row = {8'd126, 8'd113, 8'd95};
      8'd55: vig_row = {8'd127, 8'd113, 8'd96};
      8'd56: vig_row = {8'd127, 8'd114, 8'd96};
      8'd57: vig_row = {8'd128, 8'd114, 8'd97};
      8'd58: vig_row = {8'd128, 8'd115, 8'd98};
      8'd59: vig_row = {8'd129, 8'd115, 8'd98};
      8'd60: vig_row = {8'd129, 8'd116, 8'd99};
      8'd61: vig_row = {8'd129, 8'd116, 8'd99};
      8'd62: vig_row = {8'd130, 8'd117, 8'd100};
      8'd63: vig_row = {8'd130, 8'd117, 8'd100};
      8'd64: vig_row = {8'd131, 8'd118, 8'd101};
      8'd65: vig_row = {8'd131, 8'd118, 8'd101};
      8'd66: vig_row = {8'd131, 8'd118, 8'd102};
      8'd67: vig_row = {8'd132, 8'd119, 8'd102};
      8'd68: vig_row = {8'd132, 8'd119, 8'd103};
      8'd69: vig_row = {8'd132, 8'd120, 8'd103};
      8'd70: vig_row = {8'd133, 8'd120, 8'd104};
      8'd71: vig_row = {8'd133, 8'd120, 8'd104};
      8'd72: vig_row = {8'd133, 8'd121, 8'd104};
      8'd73: vig_row = {8'd133, 8'd121, 8'd105};
      8'd74: vig_row = {8'd133, 8'd121, 8'd105};
      8'd75: vig_row = {8'd133, 8'd122, 8'd105};
      8'd76: vig_row = {8'd133, 8'd122, 8'd106};
      8'd77: vig_row = {8'd133, 8'd122, 8'd106};
      8'd78: vig_row = {8'd133, 8'd123, 8'd107};
      8'd79: vig_row = {8'd133, 8'd123, 8'd107};
      8'd80: vig_row = {8'd133, 8'd123, 8'd107};
      8'd81: vig_row = {8'd133, 8'd123, 8'd107};
      8'd82: vig_row = {8'd133, 8'd124, 8'd108};
      8'd83: vig_row = {8'd133, 8'd124, 8'd108};
      8'd84: vig_row = {8'd133, 8'd124, 8'd108};
      8'd85: vig_row = {8'd133, 8'd124, 8'd109};
      8'd86: vig_row = {8'd133, 8'd125, 8'd109};
      8'd87: vig_row = {8'd133, 8'd125, 8'd109};
      8'd88: vig_row = {8'd133, 8'd125, 8'd109};
      8'd89: vig_row = {8'd133, 8'd125, 8'd110};
      8'd90: vig_row = {8'd133, 8'd125, 8'd110};
      8'd91: vig_row = {8'd133, 8'd125, 8'd110};
      8'd92: vig_row = {8'd133, 8'd126, 8'd110};
      8'd93: vig_row = {8'd133, 8'd126, 8'd110};
      8'd94: vig_row = {8'd133, 8'd126, 8'd110};
      8'd95: vig_row = {8'd133, 8'd126, 8'd111};
      8'd96: vig_row = {8'd133, 8'd126, 8'd111};
      8'd97: vig_row = {8'd133, 8'd126, 8'd111};
      8'd98: vig_row = {8'd133, 8'd126, 8'd111};
      8'd99: vig_row = {8'd133, 8'd127, 8'd111};
      8'd100: vig_row = {8'd133, 8'd127, 8'd111};
      8'd101: vig_row = {8'd133, 8'd127, 8'd111};
      8'd102: vig_row = {8'd133, 8'd127, 8'd112};
      8'd103: vig_row = {8'd133, 8'd127, 8'd112};
      8'd104: vig_row = {8'd133, 8'd127, 8'd112};
      8'd105: vig_row = {8'd133, 8'd127, 8'd112};
      8'd106: vig_row = {8'd133, 8'd127, 8'd112};
      8'd107: vig_row = {8'd133, 8'd127, 8'd112};
      8'd108: vig_row = {8'd133, 8'd127, 8'd112};
      8'd109: vig_row = {8'd133, 8'd127, 8'd112};
      8'd110: vig_row = {8'd133, 8'd127, 8'd112};
      8'd111: vig_row = {8'd133, 8'd127, 8'd112};
      8'd112: vig_row = {8'd133, 8'd127, 8'd112};
      8'd113: vig_row = {8'd133, 8'd127, 8'd112};
      8'd114: vig_row = {8'd133, 8'd127, 8'd112};
      8'd115: vig_row = {8'd133, 8'd127, 8'd112};
      8'd116: vig_row = {8'd133, 8'd127, 8'd112};
      8'd117: vig_row = {8'd133, 8'd127, 8'd112};
      8'd118: vig_row = {8'd133, 8'd127, 8'd112};
      8'd119: vig_row = {8'd133, 8'd127, 8'd112};
      8'd120: vig_row = {8'd133, 8'd127, 8'd112};
      8'd121: vig_row = {8'd133, 8'd127, 8'd112};
      8'd122: vig_row = {8'd133, 8'd127, 8'd112};
      8'd123: vig_row = {8'd133, 8'd127, 8'd111};
      8'd124: vig_row = {8'd133, 8'd127, 8'd111};
      8'd125: vig_row = {8'd133, 8'd127, 8'd111};
      8'd126: vig_row = {8'd133, 8'd126, 8'd111};
      8'd127: vig_row = {8'd133, 8'd126, 8'd111};
      8'd128: vig_row = {8'd133, 8'd126, 8'd111};
      8'd129: vig_row = {8'd133, 8'd126, 8'd111};
      8'd130: vig_row = {8'd133, 8'd126, 8'd110};
      8'd131: vig_row = {8'd133, 8'd126, 8'd110};
      8'd132: vig_row = {8'd133, 8'd126, 8'd110};
      8'd133: vig_row = {8'd133, 8'd125, 8'd110};
      8'd134: vig_row = {8'd133, 8'd125, 8'd110};
      8'd135: vig_row = {8'd133, 8'd125, 8'd110};
      8'd136: vig_row = {8'd133, 8'd125, 8'd109};
      8'd137: vig_row = {8'd133, 8'd125, 8'd109};
      8'd138: vig_row = {8'd133, 8'd125, 8'd109};
      8'd139: vig_row = {8'd133, 8'd124, 8'd109};
      8'd140: vig_row = {8'd133, 8'd124, 8'd108};
      8'd141: vig_row = {8'd133, 8'd124, 8'd108};
      8'd142: vig_row = {8'd133, 8'd124, 8'd108};
      8'd143: vig_row = {8'd133, 8'd123, 8'd107};
      8'd144: vig_row = {8'd133, 8'd123, 8'd107};
      8'd145: vig_row = {8'd133, 8'd123, 8'd107};
      8'd146: vig_row = {8'd133, 8'd123, 8'd107};
      8'd147: vig_row = {8'd133, 8'd122, 8'd106};
      8'd148: vig_row = {8'd133, 8'd122, 8'd106};
      8'd149: vig_row = {8'd133, 8'd122, 8'd105};
      8'd150: vig_row = {8'd133, 8'd121, 8'd105};
      8'd151: vig_row = {8'd133, 8'd121, 8'd105};
      8'd152: vig_row = {8'd133, 8'd121, 8'd104};
      8'd153: vig_row = {8'd133, 8'd120, 8'd104};
      8'd154: vig_row = {8'd133, 8'd120, 8'd104};
      8'd155: vig_row = {8'd132, 8'd120, 8'd103};
      8'd156: vig_row = {8'd132, 8'd119, 8'd103};
      8'd157: vig_row = {8'd132, 8'd119, 8'd102};
      8'd158: vig_row = {8'd131, 8'd118, 8'd102};
      8'd159: vig_row = {8'd131, 8'd118, 8'd101};
      8'd160: vig_row = {8'd131, 8'd118, 8'd101};
      8'd161: vig_row = {8'd130, 8'd117, 8'd100};
      8'd162: vig_row = {8'd130, 8'd117, 8'd100};
      8'd163: vig_row = {8'd129, 8'd116, 8'd99};
      8'd164: vig_row = {8'd129, 8'd116, 8'd99};
      8'd165: vig_row = {8'd129, 8'd115, 8'd98};
      8'd166: vig_row = {8'd128, 8'd115, 8'd98};
      8'd167: vig_row = {8'd128, 8'd114, 8'd97};
      8'd168: vig_row = {8'd127, 8'd114, 8'd96};
      8'd169: vig_row = {8'd127, 8'd113, 8'd96};
      8'd170: vig_row = {8'd126, 8'd113, 8'd95};
      8'd171: vig_row = {8'd126, 8'd112, 8'd95};
      8'd172: vig_row = {8'd125, 8'd112, 8'd94};
      8'd173: vig_row = {8'd125, 8'd111, 8'd93};
      8'd174: vig_row = {8'd124, 8'd111, 8'd93};
      8'd175: vig_row = {8'd124, 8'd110, 8'd92};
      8'd176: vig_row = {8'd123, 8'd109, 8'd91};
      8'd177: vig_row = {8'd123, 8'd109, 8'd90};
      8'd178: vig_row = {8'd122, 8'd108, 8'd90};
      8'd179: vig_row = {8'd122, 8'd107, 8'd89};
      8'd180: vig_row = {8'd121, 8'd107, 8'd88};
      8'd181: vig_row = {8'd121, 8'd106, 8'd87};
      8'd182: vig_row = {8'd120, 8'd105, 8'd86};
      8'd183: vig_row = {8'd119, 8'd105, 8'd86};
      8'd184: vig_row = {8'd119, 8'd104, 8'd85};
      8'd185: vig_row = {8'd118, 8'd103, 8'd84};
      8'd186: vig_row = {8'd117, 8'd103, 8'd83};
      8'd187: vig_row = {8'd117, 8'd102, 8'd82};
      8'd188: vig_row = {8'd116, 8'd101, 8'd81};
      8'd189: vig_row = {8'd115, 8'd100, 8'd80};
      8'd190: vig_row = {8'd115, 8'd99, 8'd79};
      8'd191: vig_row = {8'd114, 8'd99, 8'd78};
      8'd192: vig_row = {8'd113, 8'd98, 8'd77};
      8'd193: vig_row = {8'd113, 8'd97, 8'd76};
      8'd194: vig_row = {8'd112, 8'd96, 8'd75};
      8'd195: vig_row = {8'd111, 8'd95, 8'd74};
      8'd196: vig_row = {8'd110, 8'd94, 8'd72};
      8'd197: vig_row = {8'd109, 8'd93, 8'd71};
      8'd198: vig_row = {8'd109, 8'd92, 8'd70};
      8'd199: vig_row = {8'd108, 8'd91, 8'd69};
      8'd200: vig_row = {8'd107, 8'd90, 8'd67};
      8'd201: vig_row = {8'd106, 8'd89, 8'd66};
      8'd202: vig_row = {8'd105, 8'd88, 8'd65};
      8'd203: vig_row = {8'd104, 8'd87, 8'd63};
      8'd204: vig_row = {8'd103, 8'd86, 8'd61};
      8'd205: vig_row = {8'd102, 8'd85, 8'd60};
      8'd206: vig_row = {8'd101, 8'd84, 8'd58};
      8'd207: vig_row = {8'd101, 8'd83, 8'd57};
      8'd208: vig_row = {8'd100, 8'd82, 8'd55};
      8'd209: vig_row = {8'd99, 8'd80, 8'd53};
      8'd210: vig_row = {8'd97, 8'd79, 8'd51};
      8'd211: vig_row = {8'd96, 8'd78, 8'd49};
      8'd212: vig_row = {8'd95, 8'd76, 8'd47};
      8'd213: vig_row = {8'd94, 8'd75, 8'd45};
      8'd214: vig_row = {8'd93, 8'd74, 8'd42};
      8'd215: vig_row = {8'd92, 8'd72, 8'd40};
      8'd216: vig_row = {8'd91, 8'd71, 8'd37};
      8'd217: vig_row = {8'd90, 8'd69, 8'd34};
      8'd218: vig_row = {8'd88, 8'd68, 8'd30};
      8'd219: vig_row = {8'd87, 8'd66, 8'd27};
      8'd220: vig_row = {8'd86, 8'd64, 8'd22};
      8'd221: vig_row = {8'd85, 8'd63, 8'd17};
      8'd222: vig_row = {8'd83, 8'd61, 8'd8};
      8'd223: vig_row = {8'd82, 8'd59, 8'd0};
      8'd224: vig_row = {8'd80, 8'd57, 8'd0};
      8'd225: vig_row = {8'd79, 8'd55, 8'd0};
      8'd226: vig_row = {8'd77, 8'd52, 8'd0};
      8'd227: vig_row = {8'd76, 8'd50, 8'd0};
      8'd228: vig_row = {8'd74, 8'd48, 8'd0};
      8'd229: vig_row = {8'd73, 8'd45, 8'd0};
      8'd230: vig_row = {8'd71, 8'd43, 8'd0};
      8'd231: vig_row = {8'd69, 8'd39, 8'd0};
      8'd232: vig_row = {8'd67, 8'd36, 8'd0};
      8'd233: vig_row = {8'd66, 8'd33, 8'd0};
      8'd234: vig_row = {8'd64, 8'd29, 8'd0};
      8'd235: vig_row = {8'd62, 8'd24, 8'd0};
      8'd236: vig_row = {8'd60, 8'd18, 8'd0};
      8'd237: vig_row = {8'd57, 8'd9, 8'd0};
      8'd238: vig_row = {8'd55, 8'd0, 8'd0};
      8'd239: vig_row = {8'd52, 8'd0, 8'd0};
      default: vig_row = {8'd255, 8'd255, 8'd255};
    endcase
  endfunction
  // ---- vignette table (end) ----

  // timing: same waveform as LiteX's VideoTimingGenerator with the core's settings
  // (h: 266 active, sync 275..306, total 340; v: 240 active, sync lines 242..249, total 280)
  reg [8:0] hc = 0, vc = 0;
  reg hact = 0, vact = 0, hs0 = 0, vs0 = 0;
  wire de0 = hact & vact;

  always @(posedge clk_vid) begin
    hc <= hc == 9'd339 ? 9'd0 : hc + 9'd1;
    if (hc == 9'd0) hact <= 1'b1;
    if (hc == 9'd266) hact <= 1'b0;
    if (hc == 9'd274) hs0 <= 1'b1;
    if (hc == 9'd306) hs0 <= 1'b0;
    if (hc == 9'd274) begin
      vc <= vc == 9'd279 ? 9'd0 : vc + 9'd1;
      if (vc == 9'd0) vact <= 1'b1;
      if (vc == 9'd240) vact <= 1'b0;
      if (vc == 9'd241) vs0 <= 1'b1;
      if (vc == 9'd249) vs0 <= 1'b0;
    end
  end

  // swap at the start of vertical blanking (the hc == 274 event that ends line 239)
  reg [2:0] req_sync = 0;
  reg swap_ack = 0;
  reg front = 1'b0;
  reg [6:0] v_bright = 64;
  reg v_vig = 0;
  always @(posedge clk_vid) begin
    req_sync <= {req_sync[1:0], swap_req};
    if (hc == 9'd274 && vc == 9'd240 && req_sync[2] != swap_ack) begin
      front    <= ~front;
      v_bright <= pres_bright;  // stable: only changes while no request is pending
      v_vig    <= pres_vig;
      swap_ack <= req_sync[2];
    end
  end

  // back to clk_sys: the acknowledge and the buffer on screen travel together (same cycle in, same cycle out)
  reg [2:0] ack_sync = 0, front_sync = 0;
  always @(posedge clk_sys) begin
    ack_sync   <= {ack_sync[1:0], swap_ack};
    front_sync <= {front_sync[1:0], front};
  end
  assign swap_ack_s = ack_sync[2];
  assign back = ~front_sync[2];

  // stage 0: address of the pixel being shown
  always @(posedge clk_vid) begin
    if (vc == 9'd0) fb_addr <= 0;
    else if (de0) fb_addr <= fb_addr + 16'd1;
  end

  // stage 1: pixel from RAM, its position and its row's vignette edges
  reg de1 = 0, hs1 = 0, vs1 = 0;
  reg [8:0] x1;
  reg [7:0] y1;
  reg [23:0] vrow;
  always @(posedge clk_vid) begin
    de1  <= de0;
    hs1  <= hs0;
    vs1  <= vs0;
    x1   <= hc - 9'd1;
    y1   <= vc[7:0] - 8'd1;
    vrow <= vig_row(vc[7:0] - 8'd1);
  end
  wire [14:0] px1 = front ? fb_q1 : fb_q0;

  // vignette: three bands toward the left/right edges, not over the bottom 16 rows (HUD)
  wire [8:0] edist = x1 < 9'd133 ? 9'd133 - x1 : x1 - 9'd132;
  wire [1:0] band = (!v_vig || y1 >= 8'd224) ? 2'd0 :
                    edist > {1'b0, vrow[23:16]} ? 2'd3 :
                    edist > {1'b0, vrow[15:8]}  ? 2'd2 :
                    edist > {1'b0, vrow[7:0]}   ? 2'd1 : 2'd0;
  wire [7:0] vig_k = band == 2'd1 ? 8'd56 : band == 2'd2 ? 8'd48 : band == 2'd3 ? 8'd42 : 8'd64;
  wire [14:0] px_v = shade555(px1, vig_k);
  wire [14:0] px_b = shade555(px_v, {1'b0, v_bright});

  // stage 2: output
  always @(posedge clk_vid) begin
    vid_de  <= de1;
    vid_hs  <= hs1;
    vid_vs  <= vs1;
    vid_rgb <= de1 ? {px_b[14:10], px_b[14:12], px_b[9:5], px_b[9:7], px_b[4:0], px_b[4:2]} : 24'd0;
  end
endmodule

// One 266x240 RGB555 framebuffer. Port A: read/write (GPU), port B: read (scan-out). 1-cycle read latency.
// max_depth = 2048 makes Quartus use the 2K x 5 shape of the M10K blocks (no wasted bits): 96 blocks per buffer.
module lantern_fb (
    input  wire        clk_a,
    input  wire        we_a,
    input  wire [15:0] addr_a,
    input  wire [14:0] d_a,
    output reg  [14:0] q_a,
    input  wire        clk_b,
    input  wire [15:0] addr_b,
    output reg  [14:0] q_b
);
  (* ramstyle = "M10K, no_rw_check", max_depth = 2048 *) reg [14:0] mem[0:63839];

  always @(posedge clk_a) begin
    if (we_a) mem[addr_a] <= d_a;
    q_a <= mem[addr_a];
  end

  always @(posedge clk_b) begin
    q_b <= mem[addr_b];
  end
endmodule
