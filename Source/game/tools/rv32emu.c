// Minimal RV32IM emulator to smoke-test boot.bin with the Pocket core's memory map.
// Measures instructions per frame and dumps the framebuffer.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define RAM_BASE 0x40000000u
#define RAM_SIZE 0x04000000u
static uint32_t x[32], pc;
static uint64_t instret;
static uint32_t fb_base = 0x40C00000u;
static uint32_t keys;
#define IPF 952000ull   // instructions per 60Hz frame at 57.12MHz, 1 IPC

static uint64_t wait_start, work_total, frames_measured, work_max;
static int in_wait;

// fake APF bridge: slot 1 is backed by build/emu_sd.sav; EMU_SHIFT=k makes writes land k words late (like a lagging bus)
static uint32_t br_slot, br_off, br_len, br_ram, br_status, br_size_set;
static uint8_t *ram;
static long sd_file_size(void) { FILE *f = fopen("build/emu_sd.sav", "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); return n; }
static void br_read(void) {
    FILE *f = fopen("build/emu_sd.sav", "rb");
    if (f && br_slot == 1) { fseek(f, br_off, SEEK_SET); size_t n = fread(ram + (br_ram - 0x40000000u), 1, br_len, f); fprintf(stderr, "bridge: read %zu bytes\n", n); }
    if (f) fclose(f); br_status = 1;
}
static void br_write(void) {
    if (br_slot != 1) return;
    int k = getenv("EMU_SHIFT") ? atoi(getenv("EMU_SHIFT")) : 0;
    uint32_t *src = (uint32_t *)(ram + (br_ram - 0x40000000u)), nw = br_len / 4;
    uint32_t *out = calloc(nw, 4);
    for (uint32_t i = 0; i < nw; i++) { int j = (int)i - k; out[i] = (j >= 0 && j < (int)nw) ? src[j] : 0xDEADBEEF; }
    FILE *f = fopen("build/emu_sd.sav", "wb"); fwrite(out, 4, nw, f); fclose(f); free(out);
    fprintf(stderr, "bridge: wrote %u bytes (shift %d)\n", br_len, k); br_status = 1;
}
// ---- emulated Lantern GPU (the reference model from the game sources), driven through its register window ----
#define HOST_BUILD 1
#include "../src/gpu.c"
static uint16_t gpu_fb[2][SCR_W * SCR_H]; static int gpu_back = 0, gpu_frames = 0, gpu_used = 0;
static uint32_t gpu_cbuf[300]; static int gpu_cn = 0; static uint32_t gpu_vaddr = 0, gpu_vmax = 0; static uint8_t *gpu_touch; static uint64_t gpu_ws, gpu_ws_max, gpu_ws_tot; static uint64_t gpu_bpx, gpu_gpx, gpu_fpx, gpu_bpx_max, gpu_bpx_tot, gpu_gpx_tot, gpu_fpx_tot, gpu_nf; static uint64_t gpu_words_total = 0;
static int gpu_cmd_len(const uint32_t *c, int have) {
    switch (c[0] & 0xFF) { case GOP_CLIP: return 3; case GOP_FILL: return 4; case GOP_SHADE: return 3; case GOP_BLIT: return (c[0] >> 8) & GB_GRAD ? 10 : 6; case GOP_GLOW: return 5;
    case GOP_SEE: { int n = (c[0] >> 8) & 0xFF; return 2 + (n + 1) / 2; } case GOP_PRESENT: return 1; }
    (void)have; return 1;
}
static FILE *gpu_trace;   // GPU_TRACE=file: every register write as (u8 reg, u32 value) — test vectors for the hardware
static void gpu_port_write(uint32_t a, uint32_t v) {
    gpu_used = 1;
    if (gpu_trace) { uint8_t r = (uint8_t)(a & 0xFF); fwrite(&r, 1, 1, gpu_trace); fwrite(&v, 4, 1, gpu_trace); }
    switch (a & 0xFF) {
    case 0x00:
        gpu_cbuf[gpu_cn++] = v; gpu_words_total++;
        if (gpu_cn >= gpu_cmd_len(gpu_cbuf, gpu_cn)) {
            { int op = gpu_cbuf[0] & 0xFF; if (op == GOP_BLIT || op == GOP_GLOW || op == GOP_FILL || op == GOP_SHADE) { uint32_t xy = gpu_cbuf[op == GOP_BLIT ? 3 : op == GOP_GLOW ? 2 : 1], wh = gpu_cbuf[op == GOP_BLIT ? 4 : op == GOP_GLOW ? 3 : 2];
              int x = (int16_t)(xy & 0xFFFF), y = (int16_t)(xy >> 16), w = (int16_t)(wh & 0xFFFF), h = (int16_t)(wh >> 16);
              int x0 = MAX(x, g_gcx0), y0 = MAX(y, g_gcy0), x1 = MIN(x + w, g_gcx1), y1 = MIN(y + h, g_gcy1); long a = (x1 > x0 && y1 > y0) ? (long)(x1 - x0) * (y1 - y0) : 0;
              if (!gpu_touch) gpu_touch = calloc(1, VRAM_WORDS);
              if (a && op == GOP_BLIT) { uint32_t src = gpu_cbuf[1], st = gpu_cbuf[2] & 0xFFFF, sy = gpu_cbuf[5] >> 16; uint32_t rows = (uint32_t)(((h - 1) * (int)sy) >> 8) + 1; for (uint32_t i = 0; i < rows * st && src + i < VRAM_WORDS; i++) if (!gpu_touch[src + i]) { gpu_touch[src + i] = 1; gpu_ws++; } }
              if (a && op == GOP_GLOW) { uint32_t m = gpu_cbuf[1]; for (uint32_t i = 0; i < (uint32_t)(w * h) && m + i < VRAM_WORDS; i++) if (!gpu_touch[m + i]) { gpu_touch[m + i] = 1; gpu_ws++; } }
              if (op == GOP_BLIT) gpu_bpx += a; else if (op == GOP_GLOW) gpu_gpx += a; else gpu_fpx += a; }
              if (op == GOP_PRESENT) { gpu_nf++; gpu_bpx_tot += gpu_bpx; gpu_gpx_tot += gpu_gpx; gpu_fpx_tot += gpu_fpx; if (gpu_bpx > gpu_bpx_max) gpu_bpx_max = gpu_bpx; gpu_bpx = gpu_gpx = gpu_fpx = 0; gpu_ws_tot += gpu_ws; if (gpu_ws > gpu_ws_max) gpu_ws_max = gpu_ws; gpu_ws = 0; if (gpu_touch) memset(gpu_touch, 0, VRAM_WORDS); } }
            g_gfb = gpu_fb[gpu_back]; gpu_exec(gpu_cbuf);
            if ((gpu_cbuf[0] & 0xFF) == GOP_PRESENT) { gpu_back ^= 1; gpu_frames++; }
            gpu_cn = 0;
        }
        break;
    case 0x08: gpu_vaddr = v; break;
    case 0x0C: if (gpu_vaddr + 1 < VRAM_WORDS) { g_vram[gpu_vaddr] = (uint16_t)v; g_vram[gpu_vaddr + 1] = (uint16_t)(v >> 16); } gpu_vaddr += 2; if (gpu_vaddr > gpu_vmax) gpu_vmax = gpu_vaddr; break;
    case 0x10: { int t = v >> 24, ch = (v >> 22) & 3, e = (v >> 16) & 63; if (t < LUT_N) { if (ch == 0) g_lut_r[t][e & 31] = (uint16_t)v; else if (ch == 1) g_lut_g[t][e] = (uint16_t)v; else g_lut_b[t][e & 31] = (uint16_t)v; } break; }
    }
}
static uint32_t mmio_read(uint32_t a) {
    if ((a >> 24) == 0x80) return (a & 0xFF) == 0x04 ? (0xFFFFu | (uint32_t)(gpu_frames & 0xFF) << 24) : (a & 0xFF) == 0x14 ? 0x4C475033u : 0;
    switch (a) {
    case 0xF0000820: return br_slot == 1 ? (uint32_t)sd_file_size() : 0;
    case 0xF0000824: { uint32_t v = br_status; br_status = 0; return v; }
    case 0xF000000C: return 800;                    // audio fill
    case 0xF0001800: return keys;
    case 0xF0002800: return 1700000000u;
    case 0xF0003000: {
        if (!in_wait) { in_wait = 1; uint64_t w = instret - wait_start; if (frames_measured++ > 2) { static FILE *wf; if (!wf) wf = fopen("build/work.txt","w"); fprintf(wf, "%lu\n", (unsigned long long)w); work_total += w; if (w > work_max) work_max = w; } }
        return (uint32_t)((instret / IPF) << 2);
    }
    case 0xF0006000: return fb_base;
    case 0xF0005028: return (uint32_t)(instret);   // uptime cycles (low word) ~ instructions
    }
    return 0;
}
static uint64_t audio_samples; static uint32_t *hist;
static void mmio_write(uint32_t a, uint32_t v) {
    if (a == 0xF0000000) audio_samples++;
    switch (a) {
    case 0xF0000800: br_read(); break;
    case 0xF0000804: br_write(); break;
    case 0xF0000810: br_slot = v; break;
    case 0xF0000814: br_off = v; break;
    case 0xF0000818: br_len = v; break;
    case 0xF000081C: br_ram = v; break;
    case 0xF0000820: br_size_set = v; break;
    case 0xF0006000: fb_base = v; break;
    }
}

static inline uint32_t ld(uint32_t a, int sz) {
    if (a >= 0xF0000000u || (a >> 24) == 0x80) return mmio_read(a);
    if (a - RAM_BASE >= RAM_SIZE) { fprintf(stderr, "bad load %08x pc=%08x\n", a, pc); exit(1); }
    uint8_t *p = ram + (a - RAM_BASE);
    return sz == 1 ? p[0] : sz == 2 ? (uint32_t)(p[0] | p[1] << 8) : (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
}
static inline void st(uint32_t a, uint32_t v, int sz) {
    if (a >= 0xF0000000u) { mmio_write(a, v); return; }
    if ((a >> 24) == 0x80) { gpu_port_write(a, v); return; }
    if (a - RAM_BASE >= RAM_SIZE) { fprintf(stderr, "bad store %08x pc=%08x\n", a, pc); exit(1); }
    uint8_t *p = ram + (a - RAM_BASE);
    p[0] = (uint8_t)v; if (sz > 1) p[1] = (uint8_t)(v >> 8); if (sz > 2) { p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
}

static void dump(const char *fn) {
    FILE *f = fopen(fn, "wb"); fprintf(f, "P6 266 240 255\n");
    for (int i = 0; i < 266 * 240; i++) { uint16_t c = (uint16_t)(gpu_used ? gpu_fb[gpu_back ^ 1][i] : ld(fb_base + i * 2, 2)); uint8_t px[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)}; fwrite(px, 1, 3, f); }
    fclose(f);
}

int main(int argc, char **argv) {
    if (getenv("GPU_TRACE")) gpu_trace = fopen(getenv("GPU_TRACE"), "wb");
    ram = calloc(1, RAM_SIZE); hist = calloc(4, 0x100000);
    FILE *f = fopen(argv[1], "rb"); size_t n = fread(ram, 1, RAM_SIZE, f); fclose(f);
    fprintf(stderr, "loaded %zu bytes\n", n);
    pc = RAM_BASE;
    int frames = argc > 2 ? atoi(argv[2]) : 600;
    uint64_t limit = (uint64_t)frames * IPF;
    int dumped = 0;
    while (instret < limit) {
        uint64_t fr = instret / IPF;
        // scripted input: START at frame 30-33, then wander + attack
        keys = 0;
        if (fr >= 30 && fr < 34) keys = 1 << 15;
        else if (fr > 60 && fr < 230) keys = 1;                         // walk north to the dungeon entrance
        else if ((fr >= 240 && fr < 243) || (fr >= 260 && fr < 263) || (fr >= 280 && fr < 283) || (fr >= 300 && fr < 303)) keys = 1 << 4;   // A, A: enter depth 1
        else if (fr > 320) { int ph = (int)(fr / 90) % 4; keys = (ph == 0 ? 8 : ph == 1 ? 2 : ph == 2 ? 4 : 1) | ((fr / 20) % 2 ? 1 << 4 : 0) | ((fr / 50) % 3 == 0 ? 1 << 6 : 0); }
        if (getenv("NOKEYS")) keys = 0;
        if (fr == (uint64_t)(getenv("TITLE_FR") ? atoi(getenv("TITLE_FR")) : 20) && dumped == 0) { dump("build/emu_title.ppm"); dumped = -1; }
        if (fr == 200 && dumped == -1) { dumped = 0; }
        if (fr == 200 && dumped == 0) { dump("build/emu_town.ppm"); dumped = 1; }
        if (fr == (uint64_t)frames - 5 && dumped == 1) { dump("build/emu_play.ppm"); dumped = 2; }

        uint32_t in = ld(pc, 4), op = in & 0x7f, rd = (in >> 7) & 31, f3 = (in >> 12) & 7, rs1 = (in >> 15) & 31, rs2 = (in >> 20) & 31, f7 = in >> 25;
        int32_t immi = (int32_t)in >> 20;
        int32_t imms = ((int32_t)(in & 0xfe000000) >> 20) | ((in >> 7) & 31);
        int32_t immb = ((int32_t)(in & 0x80000000) >> 19) | ((in & 0x80) << 4) | ((in >> 20) & 0x7e0) | ((in >> 7) & 0x1e);
        int32_t immj = ((int32_t)(in & 0x80000000) >> 11) | (in & 0xff000) | ((in >> 9) & 0x800) | ((in >> 20) & 0x7fe);
        uint32_t a = x[rs1], b = x[rs2], r = 0, npc = pc + 4; int wr = 1;
        if (in_wait && op != 0x03) { /* still in poll loop */ }
        switch (op) {
        case 0x37: r = in & 0xfffff000; break;
        case 0x17: r = pc + (in & 0xfffff000); break;
        case 0x6f: r = pc + 4; npc = pc + immj; break;
        case 0x67: r = pc + 4; npc = (a + immi) & ~1u; break;
        case 0x63: wr = 0; {
            int t = f3 == 0 ? a == b : f3 == 1 ? a != b : f3 == 4 ? (int32_t)a < (int32_t)b : f3 == 5 ? (int32_t)a >= (int32_t)b : f3 == 6 ? a < b : a >= b;
            if (t) npc = pc + immb; } break;
        case 0x03: {
            uint32_t ad = a + immi;
            if (in_wait && ad != 0xF0003000u) { in_wait = 0; wait_start = instret; }
            switch (f3) { case 0: r = (uint32_t)(int8_t)ld(ad, 1); break; case 1: r = (uint32_t)(int16_t)ld(ad, 2); break; case 2: r = ld(ad, 4); break; case 4: r = ld(ad, 1); break; case 5: r = ld(ad, 2); break; default: goto bad; }
        } break;
        case 0x23: wr = 0; if (in_wait) { in_wait = 0; wait_start = instret; } st(a + imms, b, f3 == 0 ? 1 : f3 == 1 ? 2 : 4); break;
        case 0x13: switch (f3) {
            case 0: r = a + immi; break; case 2: r = (int32_t)a < immi; break; case 3: r = a < (uint32_t)immi; break;
            case 4: r = a ^ immi; break; case 6: r = a | immi; break; case 7: r = a & immi; break;
            case 1: r = a << (immi & 31); break; case 5: r = (in >> 30) & 1 ? (uint32_t)((int32_t)a >> (immi & 31)) : a >> (immi & 31); break;
        } break;
        case 0x33:
            if (f7 == 1) switch (f3) {
                case 0: r = a * b; break;
                case 1: r = (uint32_t)(((int64_t)(int32_t)a * (int64_t)(int32_t)b) >> 32); break;
                case 2: r = (uint32_t)(((int64_t)(int32_t)a * (uint64_t)b) >> 32); break;
                case 3: r = (uint32_t)(((uint64_t)a * b) >> 32); break;
                case 4: r = b == 0 ? ~0u : ((int32_t)a == INT32_MIN && (int32_t)b == -1) ? a : (uint32_t)((int32_t)a / (int32_t)b); break;
                case 5: r = b == 0 ? ~0u : a / b; break;
                case 6: r = b == 0 ? a : ((int32_t)a == INT32_MIN && (int32_t)b == -1) ? 0 : (uint32_t)((int32_t)a % (int32_t)b); break;
                case 7: r = b == 0 ? a : a % b; break;
            } else switch (f3) {
                case 0: r = f7 ? a - b : a + b; break; case 1: r = a << (b & 31); break; case 2: r = (int32_t)a < (int32_t)b; break; case 3: r = a < b; break;
                case 4: r = a ^ b; break; case 5: r = f7 ? (uint32_t)((int32_t)a >> (b & 31)) : a >> (b & 31); break; case 6: r = a | b; break; case 7: r = a & b; break;
            } break;
        case 0x0f: wr = 0; break;   // fence
        case 0x73: wr = 0; break;   // csr/ecall: ignore
        default: bad: fprintf(stderr, "illegal instr %08x at %08x\n", in, pc); return 1;
        }
        if (wr && rd) x[rd] = r;
        if (!in_wait && pc - RAM_BASE < 0x400000) hist[(pc - RAM_BASE) >> 2]++; pc = npc; instret++;
    }
    { FILE *h = fopen("build/hist.txt","w"); for (int i = 0; i < 0x100000; i++) if (hist[i]) fprintf(h, "%x %u\n", RAM_BASE + i * 4, hist[i]); fclose(h); }
    fprintf(stderr, "vram high-water %u words; per frame: blit px avg %lu max %lu, glow px avg %lu, fill/shade px avg %lu; working set avg %lu max %lu words\n", gpu_vmax, gpu_nf ? gpu_bpx_tot / gpu_nf : 0, gpu_bpx_max, gpu_nf ? gpu_gpx_tot / gpu_nf : 0, gpu_nf ? gpu_fpx_tot / gpu_nf : 0, gpu_nf ? gpu_ws_tot / gpu_nf : 0, gpu_ws_max);
    fprintf(stderr, "ran %d frames; avg work %lu instr/frame (max %lu) = %.1f%% / %.1f%% of a 60Hz frame at 1 IPC; audio samples %lu\n",
            frames, (unsigned long long)(work_total / (frames_measured > 3 ? frames_measured - 3 : 1)), (unsigned long long)work_max,
            100.0 * work_total / (frames_measured > 3 ? frames_measured - 3 : 1) / IPF, 100.0 * work_max / IPF, (unsigned long long)audio_samples);
    return 0;
}
