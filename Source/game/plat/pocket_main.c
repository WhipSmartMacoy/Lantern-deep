// Analogue Pocket backend for agg23's openFPGA RISC-V core (VexRiscv @ 57.12 MHz, 266x240 RGB565).
#include "../src/game.c"

#define CSR(off) (*(volatile u32 *)(0xF0000000u + (off)))
#define AUDIO_OUT        CSR(0x0000)
#define AUDIO_PLAY_EN    CSR(0x0004)
#define AUDIO_FILL       CSR(0x000C)
#define CONT1_KEY        CSR(0x1800)
#define RTC_UNIX         CSR(0x2800)
#define VIDEO_STATUS     CSR(0x3000)   // bit0 vblank, bit1 vblank triggered, bits 2.. frame counter
#define FB_DMA_BASE      CSR(0x6000)
#define UPTIME_LATCH     CSR(0x5020)
#define UPTIME_LO        CSR(0x5028)
static u32 cycles(void) { UPTIME_LATCH = 1; return UPTIME_LO; }
#define CYC_PER_MS 57120u

// APF bridge (file access through the Pocket's SD card)
#define BR_REQ_READ      CSR(0x0800)
#define BR_REQ_WRITE     CSR(0x0804)
#define BR_SLOT_ID       CSR(0x0810)
#define BR_DATA_OFFSET   CSR(0x0814)
#define BR_LENGTH        CSR(0x0818)
#define BR_RAM_ADDR      CSR(0x081C)
#define BR_FILE_SIZE     CSR(0x0820)
#define BR_STATUS        CSR(0x0824)   // reads 1 once the last request finished (clears on read)

static u16 backbuf[SCR_W * SCR_H] __attribute__((aligned(64)));

// ---------- SD save (data slot 1, "<program>.sav", 64 KB) ----------
// The file holds two copies of all three hero slots (current + previous save), each behind a findable header with
// a checksum. The header is searched for anywhere in the file, so a transfer that lands a few words early or late
// still loads, and a copy that comes back damaged is simply skipped in favour of the other one.
#define SD_SLOT    1
#define SD_COPY_W  8192                  // words per copy (32 KB)
#define SD_FILE_W  (SD_COPY_W * 2)       // 64 KB
#define SD_MAGIC   0x4C445344u           // "LDSD"
#define SD_SYNC    0x5AFE0F0Fu
#define SD_HDR     8                     // header starts 8 words into a copy (zero padding before it)
#define SD_PAY     (SD_HDR + 8)          // payload starts here
static u32 sd_buf[SD_FILE_W] __attribute__((aligned(64)));
static u32 sd_seq, sd_next_copy, sd_busy_frames;
_Static_assert(SD_PAY * 4 + sizeof g_persist + 64 <= SD_COPY_W * 4, "save copy too small");

static u32 sd_sum(const u32 *w, u32 n) { u32 h = 0x811C9DC5u; for (u32 i = 0; i < n; i++) { h ^= w[i]; h *= 16777619u; h ^= h >> 15; } return h; }
static int sd_wait(u32 max_spins) { for (u32 i = 0; i < max_spins; i++) if (BR_STATUS == 1) return 1; return 0; }

static void sd_load(void) {
    BR_SLOT_ID = SD_SLOT; (void)BR_SLOT_ID;            // the size lookup takes a few cycles
    u32 size = BR_FILE_SIZE;
    if (size < 64) { g_sd_load_msg = "NO SAVE FILE YET"; return; }
    if (size > sizeof sd_buf) size = sizeof sd_buf;
    (void)BR_STATUS;                                    // clear any completion left over from loading the program
    BR_DATA_OFFSET = 0; BR_LENGTH = size; BR_RAM_ADDR = (u32)(uintptr_t)sd_buf; BR_SLOT_ID = SD_SLOT;
    BR_REQ_READ = 1;
    if (!sd_wait(200000000u)) { g_sd_load_msg = "SAVE FILE DID NOT LOAD"; return; }
    u32 nw = size / 4, best = 0xFFFFFFFFu, best_seq = 0, best_bytes = 0;
    for (u32 i = 0; i + 6 <= nw; i++) {
        if (sd_buf[i] != SD_MAGIC || sd_buf[i + 1] != SD_SYNC || sd_buf[i + 5] != ~SD_MAGIC) continue;
        u32 seq = sd_buf[i + 2], bytes = sd_buf[i + 3], sum = sd_buf[i + 4], pay = i + 8;
        if ((bytes != sizeof g_persist && bytes != offsetof(struct Persist, ext) && bytes != sizeof g_persist.saves && bytes != 3 * sizeof(SaveData)) || pay + bytes / 4 > nw) continue;   // older files: no settings / 3 heroes
        if (sd_sum(&sd_buf[pay], bytes / 4) != sum) continue;   // damaged copy: try the other one
        if (best == 0xFFFFFFFFu || seq > best_seq) { best = pay; best_seq = seq; best_bytes = bytes; }
    }
    if (best == 0xFFFFFFFFu) { g_sd_load_msg = size < sizeof sd_buf ? "OLD SAVE FILE: COPY THE NEW BOOT.SAV" : "NO HEROES SAVED YET"; memset(sd_buf, 0, sizeof sd_buf); return; }
    memcpy(&g_persist, &sd_buf[best], best_bytes);   // an old 3-hero file fills slot 1; files without settings get defaults
    settings_apply();
    sd_seq = best_seq;
    // rebuild a clean image (both copies = what we loaded) so later saves start from good data
    memset(sd_buf, 0, sizeof sd_buf);
    for (int c = 0; c < 2; c++) {
        u32 *b = &sd_buf[c * SD_COPY_W];
        memcpy(&b[SD_PAY], &g_persist, sizeof g_persist);
        b[SD_HDR] = SD_MAGIC; b[SD_HDR + 1] = SD_SYNC; b[SD_HDR + 2] = sd_seq; b[SD_HDR + 3] = sizeof g_persist;
        b[SD_HDR + 4] = sd_sum(&b[SD_PAY], sizeof g_persist / 4); b[SD_HDR + 5] = ~SD_MAGIC;
    }
    G.has_save = any_save();
    g_sd_load_msg = size < sizeof sd_buf ? "OLD SAVE FILE: COPY THE NEW BOOT.SAV" : G.has_save ? "HEROES LOADED FROM SD CARD" : "NO HEROES SAVED YET";
}
// Start writing the current slots to the SD card (finishes in the background; polled once per frame).
static void sd_save_start(void) {
    u32 *b = &sd_buf[sd_next_copy * SD_COPY_W];   // overwrite the older copy, keep the other as a fallback
    memset(b, 0, SD_COPY_W * 4);
    memcpy(&b[SD_PAY], &g_persist, sizeof g_persist);
    sd_seq++;
    b[SD_HDR] = SD_MAGIC; b[SD_HDR + 1] = SD_SYNC; b[SD_HDR + 2] = sd_seq; b[SD_HDR + 3] = sizeof g_persist;
    b[SD_HDR + 4] = sd_sum(&b[SD_PAY], sizeof g_persist / 4); b[SD_HDR + 5] = ~SD_MAGIC;
    sd_next_copy ^= 1;
    __asm__ volatile("fence" ::: "memory");
    // whole file from the start of the buffer, so the bridge stays pointed at it (the Pocket flushes this slot on exit too)
    BR_SLOT_ID = SD_SLOT; BR_DATA_OFFSET = 0; BR_LENGTH = sizeof sd_buf; BR_RAM_ADDR = (u32)(uintptr_t)sd_buf;
    BR_FILE_SIZE = sizeof sd_buf;
    (void)BR_STATUS;
    BR_REQ_WRITE = 1;
    g_sd_state = SD_BUSY; sd_busy_frames = 0;
}
static void sd_poll(void) {
    if (g_sd_state == SD_BUSY) {
        if (BR_STATUS == 1) g_sd_state = SD_IDLE;
        else if (++sd_busy_frames > 30 * 20) g_sd_state = SD_FAIL;   // nothing after 20 s
    }
    if (G.save_dirty && g_sd_state != SD_BUSY) { G.save_dirty = 0; sd_save_start(); }
}
static i16 abuf[2400];

#ifdef GPU_HW
// our own core: the GPU drives the screen; frames shown = its 8-bit counter, extended here
#define FB_DMA_ENABLE CSR(0x6008)
static u32 frame_counter(void) { static u32 acc, prev; u32 f = GPU_STATUS >> 24; acc += (f - prev) & 0xFF; prev = f; return acc; }
// Sleep / Memories: the core asks for a save state (STATUS bit 18) or hands one back (bit 19). The buffer is
// fixed at 0x4300_0000; the host reads / writes it through the bridge while we keep running.
#define GPU_SSDONE GPU_REG(0x18)
#define SS_BUF ((u8 *)0x43000000u)
#define SS_CAP (256u * 1024)
static u8 g_ss_evict[16384];
static int ss_poll(void) {   // returns 1 when a saved state was just restored
    u32 st = GPU_STATUS, restored = 0;
    if (st & (1u << 18)) { ss_save(SS_BUF, SS_CAP); __asm__ volatile("fence" ::: "memory"); GPU_SSDONE = 1; }
    if (st & (1u << 19)) {
        volatile u8 *e = g_ss_evict; u32 x = 0; for (u32 i = 0; i < sizeof g_ss_evict; i += 32) x += e[i];   // push stale lines out of the data cache
        (void)x; __asm__ volatile("fence" ::: "memory");
        restored = (u32)ss_load(SS_BUF);
        GPU_SSDONE = 2;
    }
    return (int)restored;
}
#include "gpu_selftest.c"
#else
static u32 frame_counter(void) { return (VIDEO_STATUS >> 2) & 0x3FFFFFFF; }
#endif

int main(void) {
    // page flipping: draw into one buffer while the other is shown (no 128 KB copy per frame)
    u16 *fbs[2] = {(u16 *)(uintptr_t)FB_DMA_BASE, backbuf}; int shown = 0;
    (void)fbs; (void)shown;
#ifdef GPU_HW
    g_plat_fancy = 1; g_plat_effects = 1;   // our own core draws in hardware: full detail by default
#else
    g_plat_fancy = 0; g_plat_effects = 0;   // the plain RISC-V core: cheaper lighting by default
#endif
    u32 fps_frames = 0, fps_t0 = 0, acc_l = 0, acc_r = 0, acc_a = 0, acc_n = 0;
    game_init(RTC_UNIX ^ 0xA5A5F00Du);
#ifndef BENCH
    g_sd_state = SD_IDLE;
    sd_load();
#endif
#ifdef BENCH   // deterministic benchmark for the emulator: 1 = town, 2 = dungeon
    game_tick(BTN_START); game_tick(0); game_tick(BTN_A); game_tick(0); G.ticks = 0; game_tick(BTN_A); for (int i = 0; i < 120; i++) game_tick(0);   // ticks reset: same hero seed as the old single-slot bench
    if (BENCH == 3) { open_door(0, 0); }
    if (BENCH == 2) { G.interact_kind = IK_NPC; G.interact_idx = NPC_ENTRANCE; do_interact(); game_tick(BTN_A); game_tick(0); for (int i = 0; i < 40; i++) game_tick(0); G.p.hp = 9999 << 8; }
    u32 bench_t = 0;
#endif
    AUDIO_PLAY_EN = 1;
#ifdef GPU_HW
    FB_DMA_ENABLE = 0;          // LiteX's framebuffer is not on screen: stop it reading SDRAM
    g_gpu_grad = GPU_REG(0x14) >= 0x4C475033u;   // smooth light needs milestone 3 of the core
#ifndef BENCH
    gpu_selftest();             // hardware test screen; START goes on to the game
#endif
    u32 submitted = frame_counter();
#endif
    u32 last = frame_counter();
    fps_t0 = last;
    for (;;) {
        // locked 30 fps: one new picture every 2 display refreshes (twice the render budget of 60 fps)
#ifdef GPU_HW
        u32 now, step = 1;   // our own core draws in hardware: 60 fps
#else
        u32 now, step = 2;
#endif
#ifdef GPU_HW
        (void)step;
        while (submitted - frame_counter() >= 2) { }   // at most one finished frame waiting for the screen
        now = frame_counter();
        u32 elapsed = now - last; if (!elapsed) elapsed = 1; last = now;
#else
        while ((now = frame_counter()) - last < step) { }
        u32 elapsed = now - last; last = now;
#endif
#ifndef GPU_HW
        FB_DMA_BASE = (u32)(uintptr_t)fbs[shown];   // show the picture finished last time
#endif
        u32 c0 = cycles();
        if (elapsed > 4) elapsed = 4;              // don't spiral if a frame took very long
        u32 keys = CONT1_KEY & 0xFFFF;
#if defined(GPU_HW) && !defined(BENCH)
        if (ss_poll()) last = frame_counter();   // woke from sleep: carry on where we were
#endif
#ifdef BENCH
        { static const u32 K[8] = {BTN_RIGHT, BTN_RIGHT | BTN_DOWN, BTN_DOWN, BTN_DOWN | BTN_LEFT, BTN_LEFT, BTN_LEFT | BTN_UP, BTN_UP, BTN_UP | BTN_RIGHT};
          bench_t++; keys = BENCH == 3 ? 0 : K[(bench_t / 40) & 7] | ((bench_t / 15) % 3 == 0 ? BTN_A : 0); if (BENCH == 2) G.p.hp = G.p.hpmax << 8; }
#endif
        game_frame(keys, elapsed);
        u32 c1 = cycles();
#ifndef BENCH
        sd_poll();
#endif   // 90% game speed: 54 logic ticks per second, rendering interpolated
#ifdef GPU_HW
        game_render(0);   // commands go to the GPU; it swaps at the next vblank
        submitted++;
#else
        game_render(fbs[shown ^ 1]);
        shown ^= 1;
#endif
        u32 c2 = cycles();
        // audio: keep ~1.5 frames (at 30 fps) buffered (48kHz)
        u32 fill = AUDIO_FILL;
        int need = 2400 - (int)fill;
        if (need > 0) {
            if (need > 2400) need = 2400;
            game_audio(abuf, need);
            for (int i = 0; i < need; i++) { u32 v = (u16)abuf[i]; AUDIO_OUT = v | (v << 16); }
        }
        u32 c3 = cycles();
        // frame-rate readout: averaged over each second
        acc_l += c1 - c0; acc_r += c2 - c1; acc_a += c3 - c2; acc_n++; fps_frames++;
        if (now - fps_t0 >= 60) {
            g_perf_fps = (int)(fps_frames * 60 / (now - fps_t0));
            g_perf_ms = (int)((acc_l + acc_r + acc_a) / acc_n / CYC_PER_MS);
            g_perf_split[0] = (int)(acc_l / acc_n / CYC_PER_MS) + 0; g_perf_split[1] = (int)(acc_r / acc_n / CYC_PER_MS); g_perf_split[2] = (int)(acc_a / acc_n / CYC_PER_MS);
            fps_t0 = now; fps_frames = 0; acc_l = acc_r = acc_a = acc_n = 0;
        }
    }
}
