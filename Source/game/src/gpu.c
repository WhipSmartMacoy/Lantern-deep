// Lantern GPU: the drawing-command interface between the game and the graphics hardware.
//
// The game never writes pixels itself. It sends short commands (fill, shade, blit, glow, ...) as 32-bit words.
// On the Pocket these words go to the GPU built into our custom FPGA core. Everywhere else (browser, tests, and the
// Pocket until that core exists) they are executed by the reference model below, which defines exactly what the
// hardware must produce.
//
// VRAM: a flat array of 16-bit words holding every image the GPU can draw from (tiles, walls, props, characters,
// item icons, font glyphs, glow masks). Images are uploaded once, on first use, and then referred to by address.
#include "common.h"

// ---------------- command encoding ----------------
enum {
    GOP_CLIP = 1,      // [op] [x0 | y0<<16] [x1 | y1<<16]                     clip rectangle for everything after it
    GOP_FILL = 2,      // [op] [x | y<<16] [w | h<<16] [colour]
    GOP_SHADE = 3,     // [op | k<<8] [x | y<<16] [w | h<<16]                   dst = dst * k / 64 (darken)
    GOP_BLIT = 4,      // [op | flags<<8 | lut<<24] [src] [stride | tint<<16] [x | y<<16] [w | h<<16] [stepx | stepy<<16]
    GOP_GLOW = 5,      // [op] [mask] [x | y<<16] [w | h<<16] [r | g<<8 | b<<16]  dst += colour * mask/16 (saturating)
    GOP_SEE = 6,       // [op | n<<8] [cx | top<<16] then n half-widths (u16, 2 per word): cut-out for GB_SEE blits
    GOP_PRESENT = 7,   // [op | bright<<8 | vignette<<16]                      frame done: brightness 0..64, vignette 0/1
};
enum {   // blit flags
    GB_KEY = 1,        // source value 0 is transparent
    GB_FLIPX = 2,      // mirror horizontally
    GB_OUTLINE = 4,    // source value 1 darkens the destination to 3/4 instead of drawing
    GB_TINT = 8,       // every drawn pixel becomes the tint colour (text, hit-flash silhouettes)
    GB_SEE = 16,       // skip pixels inside the current SEE cut-out (dithered rim)
    GB_LUT = 32,       // shade the source through light table `lut` (tint * 8 + level)
    GB_GRAD = 64,      // smooth light: 4 more words A, B, C, D (16.16 light levels); the level of output pixel (x, y)
                       // relative to the blit is A + B*x + C*y + D*x*y, 2x2-dithered, through table (lut & ~7) + level
};
static int g_gpu_grad = 1;   // GRAD available (the Pocket checks the core's ID register; the C model always has it)
#define LUT_N 24
#define LUT_ID(tint, lvl) ((tint) * 8 + (lvl))

// ---------------- hardware interface (custom Pocket core, built with -DGPU_HW) ----------------
// Registers in the core's uncached window at 0x8000_0000:
//   +0x00 CMD     (W)  command FIFO: command words in order
//   +0x04 STATUS  (R)  [15:0] free FIFO words, [16] busy, [31:24] frames shown (increments at each swap)
//   +0x08 VADDR   (W)  VRAM upload address (16-bit word address)
//   +0x0C VDATA   (W)  two pixels: [15:0] at VADDR, [31:16] at VADDR+1; VADDR += 2
//   +0x10 LUT     (W)  [31:24] table (0..23), [23:22] channel (0 R, 1 G, 2 B), [21:16] entry, [15:0] value
#ifdef GPU_HW
#define GPU_REG(o) (*(volatile u32 *)(0x80000000u + (o)))
#define GPU_CMD GPU_REG(0x00)
#define GPU_STATUS GPU_REG(0x04)
#define GPU_VADDR GPU_REG(0x08)
#define GPU_VDATA GPU_REG(0x0C)
#define GPU_LUT GPU_REG(0x10)
#endif

// ---------------- VRAM ----------------
#ifndef VRAM_WORDS
#ifdef GPU_HW
#define VRAM_WORDS (128u * 1024)        // the Pocket's SRAM (256 KB)
#else
#define VRAM_WORDS (3u * 1024 * 1024)   // 6 MB
#endif
#endif
static u16 g_vram[VRAM_WORDS];
// Two regions: images that stay for the whole run (fonts, wall/floor tiles) grow up from the bottom; images
// cached on first use (sprites, glow masks) grow down from the top. When the two meet, the cached ones are
// dropped and re-uploaded as they are used again (the working set of a frame is far smaller than VRAM).
static u32 g_vram_top = 16;            // address 0..15 reserved
static u32 g_vram_dyn = VRAM_WORDS;    // lowest cached image
static u32 g_vram_recycles;            // statistics
static u16 g_lut_r[LUT_N][32], g_lut_g[LUT_N][64], g_lut_b[LUT_N][32];   // light tables (also loaded into the GPU)

static void vram_recycle(void);
static u32 vram_alloc_persist(u32 n) {
    if (g_vram_dyn - g_vram_top < n) return 0;   // out of VRAM: draws from address 0 (blank)
    u32 a = g_vram_top; g_vram_top += n; return a;
}
static u32 vram_alloc(u32 n) {
    if (g_vram_dyn - g_vram_top < n) { vram_recycle(); if (g_vram_dyn - g_vram_top < n) return 0; }
    g_vram_dyn -= n; return g_vram_dyn;
}
// copy a range of the CPU-side VRAM image to the GPU (no-op without the hardware)
static void vram_sync(u32 addr, u32 n) {
#ifdef GPU_HW
    if (!addr || !n) return;
    u32 a = addr & ~1u, e = addr + n; GPU_VADDR = a;
    for (; a < e; a += 2) GPU_VDATA = g_vram[a] | (u32)g_vram[a + 1] << 16;
#else
    (void)addr; (void)n;
#endif
}
static void vram_write(u32 addr, const u16 *src, u32 n) { if (addr) { memcpy(&g_vram[addr], src, n * 2); vram_sync(addr, n); } }
static void gpu_luts_upload(void) {
#ifdef GPU_HW
    for (u32 t = 0; t < LUT_N; t++) {
        for (u32 e = 0; e < 32; e++) GPU_LUT = t << 24 | 0u << 22 | e << 16 | g_lut_r[t][e];
        for (u32 e = 0; e < 64; e++) GPU_LUT = t << 24 | 1u << 22 | e << 16 | g_lut_g[t][e];
        for (u32 e = 0; e < 32; e++) GPU_LUT = t << 24 | 2u << 22 | e << 16 | g_lut_b[t][e];
    }
#endif
}

// upload cache: image pointer (+ variant) -> VRAM address
#define VCACHE_N 8192
static struct { const void *p; u32 v; u32 addr; } g_vcache[VCACHE_N];
static u32 vcache_find(const void *p, u32 v) {
    u32 h = ((u32)(uintptr_t)p * 2654435761u ^ v * 40503u) & (VCACHE_N - 1);
    for (int i = 0; i < VCACHE_N; i++, h = (h + 1) & (VCACHE_N - 1)) {
        if (!g_vcache[h].p) return 0;
        if (g_vcache[h].p == p && g_vcache[h].v == v) return g_vcache[h].addr;
    }
    return 0;
}
static void vcache_put(const void *p, u32 v, u32 addr) {
    u32 h = ((u32)(uintptr_t)p * 2654435761u ^ v * 40503u) & (VCACHE_N - 1);
    for (int i = 0; i < VCACHE_N; i++, h = (h + 1) & (VCACHE_N - 1))
        if (!g_vcache[h].p) { g_vcache[h].p = p; g_vcache[h].v = v; g_vcache[h].addr = addr; return; }
}
// drop every cached image (the persistent ones stay)
static void vram_recycle(void) {
#ifdef GPU_HW
    while (GPU_STATUS & (1u << 16)) { }   // queued commands may still read the old images
#endif
    static struct { const void *p; u32 v; u32 addr; } keep[VCACHE_N]; int nk = 0;
    for (int i = 0; i < VCACHE_N; i++) if (g_vcache[i].p && g_vcache[i].addr && g_vcache[i].addr < g_vram_top) { keep[nk].p = g_vcache[i].p; keep[nk].v = g_vcache[i].v; keep[nk].addr = g_vcache[i].addr; nk++; }
    memset(g_vcache, 0, sizeof g_vcache);
    for (int i = 0; i < nk; i++) vcache_put(keep[i].p, keep[i].v, keep[i].addr);
    g_vram_dyn = VRAM_WORDS; g_vram_recycles++;
}
// VRAM address of a 16-bit image (uploaded the first time it is used)
static u32 vram_image(const u16 *px, int n) {
    u32 a = vcache_find(px, 0);
    if (!a) { a = vram_alloc((u32)n); vram_write(a, px, (u32)n); vcache_put(px, 0, a); }
    return a;
}
static u32 vram_image_persist(const u16 *px, int n) {   // same, kept for the whole run
    u32 a = vcache_find(px, 0);
    if (!a) { a = vram_alloc_persist((u32)n); vram_write(a, px, (u32)n); vcache_put(px, 0, a); }
    return a;
}

// ---------------- command stream ----------------
static u32 g_cmd[64]; static int g_ncmd;
static void gpu_exec(const u32 *c);
#ifdef GPU_STATS
static long g_st_blit, g_st_px, g_st_tile, g_st_key, g_st_other, g_st_trans;
#endif
static u32 g_gpu_words, g_gpu_cmds;   // per-frame statistics
static void gpu_emit(const u32 *w, int n) {
    g_gpu_words += (u32)n; g_gpu_cmds++;
#ifdef GPU_HW
    static u32 credit;   // FIFO words known to be free
    if (credit < (u32)n) { do credit = GPU_STATUS & 0xFFFF; while (credit < (u32)n); }
    for (int i = 0; i < n; i++) GPU_CMD = w[i];
    credit -= (u32)n;
#else
    gpu_exec(w); (void)n;
#endif
}

static void gpu_clip(int x0, int y0, int x1, int y1) {
    u32 c[3] = {GOP_CLIP, (u32)(u16)x0 | (u32)(u16)y0 << 16, (u32)(u16)x1 | (u32)(u16)y1 << 16}; gpu_emit(c, 3);
}
static void gpu_fill(int x, int y, int w, int h, u16 col) {
    if (w <= 0 || h <= 0) return;
    u32 c[4] = {GOP_FILL, (u32)(u16)x | (u32)(u16)y << 16, (u32)(u16)w | (u32)(u16)h << 16, col}; gpu_emit(c, 4);
}
static void gpu_shade(int x, int y, int w, int h, int k) {
    if (w <= 0 || h <= 0) return;
    u32 c[3] = {GOP_SHADE | (u32)k << 8, (u32)(u16)x | (u32)(u16)y << 16, (u32)(u16)w | (u32)(u16)h << 16}; gpu_emit(c, 3);
}
// draw a w*h (destination size) image from VRAM `src` (row length `stride`); step = source pixels per output pixel (8.8)
static void gpu_blit(u32 src, int stride, int x, int y, int w, int h, int flags, int lut, u16 tint, int stepx, int stepy) {
    if (w <= 0 || h <= 0) return;
    u32 c[6] = {GOP_BLIT | (u32)flags << 8 | (u32)lut << 24, src, (u32)(u16)stride | (u32)tint << 16,
                (u32)(u16)x | (u32)(u16)y << 16, (u32)(u16)w | (u32)(u16)h << 16, (u32)(u16)stepx | (u32)(u16)stepy << 16};
    gpu_emit(c, 6);
}
static void gpu_blit_grad(u32 src, int stride, int x, int y, int w, int h, int flags, int tint, i32 A, i32 B, i32 C, i32 D) {
    if (w <= 0 || h <= 0) return;
    u32 c[10] = {GOP_BLIT | (u32)(flags | GB_GRAD) << 8 | (u32)(tint * 8) << 24, src, (u32)(u16)stride,
                 (u32)(u16)x | (u32)(u16)y << 16, (u32)(u16)w | (u32)(u16)h << 16, 256u | 256u << 16, (u32)A, (u32)B, (u32)C, (u32)D};
    gpu_emit(c, 10);
}
static void gpu_glow(u32 mask, int x, int y, int w, int h, int r, int g, int b) {
    u32 c[5] = {GOP_GLOW, mask, (u32)(u16)x | (u32)(u16)y << 16, (u32)(u16)w | (u32)(u16)h << 16, (u32)r | (u32)g << 8 | (u32)b << 16};
    gpu_emit(c, 5);
}
// cut-out for GB_SEE blits: rows top..top+n-1, half-width hw[i] around cx (0 = no cut on that row)
static void gpu_see(int cx, int top, const u16 *hw, int n) {
    u32 c[2 + 128]; if (n > 254) n = 254;
    c[0] = GOP_SEE | (u32)n << 8; c[1] = (u32)(u16)cx | (u32)(u16)top << 16;
    for (int i = 0; i < n; i += 2) c[2 + i / 2] = hw[i] | (u32)(i + 1 < n ? hw[i + 1] : 0) << 16;
    gpu_emit(c, 2 + (n + 1) / 2);
}
static void gpu_present(int bright, int vignette) { u32 c[1] = {GOP_PRESENT | (u32)CLAMP(bright, 0, 64) << 8 | (u32)(vignette != 0) << 16}; gpu_emit(c, 1); }

// ---------------- reference model ----------------
static u16 *g_gfb;                                   // the frame being drawn
static int g_gcx0, g_gcy0, g_gcx1 = SCR_W, g_gcy1 = SCR_H;
static int g_see_cx, g_see_top, g_see_n; static u16 g_see_hw[256];
static inline u16 shade_px(u16 c, int k) {   // per-channel scale by k/64
    return (u16)((((c & 0xF81Fu) * (u32)k >> 6) & 0xF81Fu) | (((c & 0x07E0u) * (u32)k >> 6) & 0x07E0u));
}
static void gpu_exec(const u32 *c) {
    int op = c[0] & 0xFF;
    switch (op) {
    case GOP_CLIP:
        g_gcx0 = CLAMP((i16)(c[1] & 0xFFFF), 0, SCR_W); g_gcy0 = CLAMP((i16)(c[1] >> 16), 0, SCR_H);
        g_gcx1 = CLAMP((i16)(c[2] & 0xFFFF), 0, SCR_W); g_gcy1 = CLAMP((i16)(c[2] >> 16), 0, SCR_H);
        break;
    case GOP_FILL: case GOP_SHADE: {
        int x = (i16)(c[1] & 0xFFFF), y = (i16)(c[1] >> 16), w = (i16)(c[2] & 0xFFFF), h = (i16)(c[2] >> 16);
        int x0 = MAX(x, g_gcx0), y0 = MAX(y, g_gcy0), x1 = MIN(x + w, g_gcx1), y1 = MIN(y + h, g_gcy1);
        if (op == GOP_FILL) { u16 col = (u16)c[3]; for (int py = y0; py < y1; py++) { u16 *d = g_gfb + py * SCR_W; for (int px = x0; px < x1; px++) d[px] = col; } }
        else { int k = (c[0] >> 8) & 0xFF; for (int py = y0; py < y1; py++) { u16 *d = g_gfb + py * SCR_W; for (int px = x0; px < x1; px++) d[px] = shade_px(d[px], k); } }
        break; }
    case GOP_BLIT: {
        int flags = (c[0] >> 8) & 0xFF, lut = (c[0] >> 24) & 0xFF;
        u32 src = c[1]; int stride = (i16)(c[2] & 0xFFFF); u16 tint = (u16)(c[2] >> 16);
        int x = (i16)(c[3] & 0xFFFF), y = (i16)(c[3] >> 16), w = (i16)(c[4] & 0xFFFF), h = (i16)(c[4] >> 16);
        int sx = (int)(c[5] & 0xFFFF), sy = (int)(c[5] >> 16);
        if (lut >= LUT_N) lut = LUT_N - 1;
        const u16 *LR = g_lut_r[lut], *LG = g_lut_g[lut], *LB = g_lut_b[lut];
        int x0 = MAX(x, g_gcx0), y0 = MAX(y, g_gcy0), x1 = MIN(x + w, g_gcx1), y1 = MIN(y + h, g_gcy1);
#ifdef GPU_STATS
        if (x1 > x0 && y1 > y0) { long ar = (long)(x1 - x0) * (y1 - y0); g_st_blit++; g_st_px += ar; if (stride == 28) g_st_tile += ar; else if (flags & GB_KEY) g_st_key += ar; else g_st_other += ar;
            if (flags & GB_KEY) for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) { int u = ((px - x) * sx) >> 8; if (flags & GB_FLIPX) u = ((w - 1 - (px - x)) * sx) >> 8; if (!g_vram[src + (u32)(((py - y) * sy) >> 8) * (u32)stride + u]) g_st_trans++; } }
#endif
        for (int py = y0; py < y1; py++) {
            const u16 *row = &g_vram[src + (u32)(((py - y) * sy) >> 8) * (u32)stride];
            u16 *d = g_gfb + py * SCR_W;
            int seew = -1, seei = -1;
            if ((flags & GB_SEE) && py >= g_see_top && py < g_see_top + g_see_n) { seew = g_see_hw[py - g_see_top]; seei = seew * 3 / 4; if (!seew) seew = seei = -1; }
            for (int px = x0; px < x1; px++) {
                int u = ((px - x) * sx) >> 8; if (flags & GB_FLIPX) u = ((w - 1 - (px - x)) * sx) >> 8;
                u16 v = row[u];
                if ((flags & GB_KEY) && !v) continue;
                if (seew >= 0) { int dx = ABS(px - g_see_cx); if (dx < seei || (dx < seew && ((px ^ py) & 1))) continue; }
                if ((flags & GB_OUTLINE) && v == 1) { if (!(flags & GB_TINT)) d[px] = shade_px(d[px], 48); continue; }   // soft edge (skipped in silhouettes)
                if (flags & GB_TINT) { d[px] = tint; continue; }
                if (flags & GB_GRAD) {   // same arithmetic as the hardware (32-bit wrap-around)
                    u32 rx = (u32)(px - x), ry = (u32)(py - y);
                    i32 L = (i32)((u32)c[6] + (u32)c[7] * rx + (u32)c[8] * ry + (u32)c[9] * rx * ry);
                    i32 d = (i32)((u32)L + (u32)((((px ^ py) & 1) << 1 | (py & 1)) * 0x4000 + 0x2000));
                    int lvl = d < 0 ? 0 : (d >> 16) > 7 ? 7 : (d >> 16);
                    if (lvl < 7) { int t = (lut & ~7) + lvl; v = (u16)(g_lut_r[t][v >> 11] | g_lut_g[t][(v >> 5) & 63] | g_lut_b[t][v & 31]); }
                } else if (flags & GB_LUT) v = (u16)(LR[v >> 11] | LG[(v >> 5) & 63] | LB[v & 31]);
                d[px] = v;
            }
        }
        break; }
    case GOP_GLOW: {
        u32 mask = c[1]; int x = (i16)(c[2] & 0xFFFF), y = (i16)(c[2] >> 16), w = (i16)(c[3] & 0xFFFF), h = (i16)(c[3] >> 16);
        int ar = c[4] & 0xFF, ag = (c[4] >> 8) & 0xFF, ab = (c[4] >> 16) & 0xFF;
        int x0 = MAX(x, g_gcx0), y0 = MAX(y, g_gcy0), x1 = MIN(x + w, g_gcx1), y1 = MIN(y + h, g_gcy1);
        for (int py = y0; py < y1; py++) {
            const u16 *km = &g_vram[mask + (u32)(py - y) * (u32)w - (u32)x]; u16 *d = g_gfb + py * SCR_W;
            for (int px = x0; px < x1; px++) {
                int k = km[px]; if (!k) continue;
                u16 o = d[px];
                int r = (o >> 11) + (ar * k >> 4), g = ((o >> 5) & 63) + (ag * k >> 4), b = (o & 31) + (ab * k >> 4);
                d[px] = (u16)((MIN(r, 31) << 11) | (MIN(g, 63) << 5) | MIN(b, 31));
            }
        }
        break; }
    case GOP_SEE: {
        int n = (c[0] >> 8) & 0xFF; g_see_cx = (i16)(c[1] & 0xFFFF); g_see_top = (i16)(c[1] >> 16); g_see_n = n;
        for (int i = 0; i < n; i++) g_see_hw[i] = (u16)(c[2 + i / 2] >> ((i & 1) * 16));
        break; }
    case GOP_PRESENT: {   // in hardware this happens during scan-out, for free
        int k = (c[0] >> 8) & 0xFF, vig = (c[0] >> 16) & 1;
        if (vig) {   // three soft bands toward the edges (row table, as the scan-out hardware will use)
            static u8 vt[SCR_H][3]; static int ready;
            if (!ready) {
                for (int y = 0; y < SCR_H; y++) for (int b = 0; b < 3; b++) {
                    int thr = b == 0 ? 620 : b == 1 ? 800 : 960, dy = (y - SCR_H / 2 + 8) * 1000 / (SCR_H / 2 + 20); i32 rem = thr * 1000 - dy * dy;
                    i32 r = 0; if (rem > 0) { i32 v = rem, bb = 1 << 30; while (bb > v) bb >>= 2; while (bb) { if (v >= r + bb) { v -= r + bb; r = (r >> 1) + bb; } else r >>= 1; bb >>= 2; } }
                    vt[y][b] = (u8)MIN(SCR_W / 2, rem <= 0 ? 0 : (int)(r * (SCR_W / 2 + 10) / 1000));
                }
                ready = 1;
            }
            for (int py = 0; py < SCR_H - 16; py++) {
                u16 *d = g_gfb + py * SCR_W;
                for (int px = 0; px < SCR_W; px++) {
                    int e = px < SCR_W / 2 ? SCR_W / 2 - px : px - SCR_W / 2 + 1;   // distance from the centre line
                    int band = e > vt[py][2] ? 3 : e > vt[py][1] ? 2 : e > vt[py][0] ? 1 : 0;
                    if (band) d[px] = shade_px(d[px], band == 1 ? 56 : band == 2 ? 48 : 42);
                }
            }
        }
        if (k < 64) for (int i = 0; i < SCR_W * SCR_H; i++) g_gfb[i] = shade_px(g_gfb[i], k);
        break; }
    }
}
