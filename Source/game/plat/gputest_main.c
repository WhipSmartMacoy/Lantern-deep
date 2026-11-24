// Lantern GPU hardware test (milestone 1): runs on the custom Lantern Deep core, exercises FILL / SHADE / CLIP /
// PRESENT and shows the frame rate as big digits. Everything on screen is drawn by the GPU from commands.
//
//   A       vignette on / off
//   B       brightness ramp (fade out and back in)
//   X       stress test: add full-screen SHADE passes (UP / DOWN change how many)
//   Y       freeze the animation
//
// What you should see: a smooth colour gradient, eight colour bars, bouncing squares, a darkened panel,
// a clipped checkerboard window, and "60" in the corner with a full green bar.
typedef unsigned int u32; typedef unsigned short u16; typedef int i32;

#define CSR(off) (*(volatile u32 *)(0xF0000000u + (off)))
#define CONT1_KEY        CSR(0x1800)
#define FB_DMA_ENABLE    CSR(0x6008)
#define UPTIME_LATCH     CSR(0x5020)
#define UPTIME_LO        CSR(0x5028)
#define AUDIO_OUT        CSR(0x0000)
#define AUDIO_PLAY_EN    CSR(0x0004)
#define AUDIO_FILL       CSR(0x000C)

#define GPU_REG(o) (*(volatile u32 *)(0x80000000u + (o)))
#define GPU_CMD    GPU_REG(0x00)
#define GPU_STATUS GPU_REG(0x04)
#define GPU_ID     GPU_REG(0x14)

enum { GOP_CLIP = 1, GOP_FILL = 2, GOP_SHADE = 3, GOP_PRESENT = 7 };
#define W 266
#define H 240
#define RGB(r, g, b) (u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

static u32 credit;
static void emit(const u32 *w, int n) {
    if (credit < (u32)n) { do credit = GPU_STATUS & 0xFFFF; while (credit < (u32)n); }
    for (int i = 0; i < n; i++) GPU_CMD = w[i];
    credit -= (u32)n;
}
static void clip(int x0, int y0, int x1, int y1) { u32 c[3] = {GOP_CLIP, (u32)(u16)x0 | (u32)(u16)y0 << 16, (u32)(u16)x1 | (u32)(u16)y1 << 16}; emit(c, 3); }
static void fill(int x, int y, int w, int h, u16 col) { u32 c[4] = {GOP_FILL, (u32)(u16)x | (u32)(u16)y << 16, (u32)(u16)w | (u32)(u16)h << 16, col}; emit(c, 4); }
static void shade(int x, int y, int w, int h, int k) { u32 c[3] = {GOP_SHADE | (u32)k << 8, (u32)(u16)x | (u32)(u16)y << 16, (u32)(u16)w | (u32)(u16)h << 16}; emit(c, 3); }
static void present(int bright, int vig) { u32 c[1] = {GOP_PRESENT | (u32)bright << 8 | (u32)(vig ? 1 : 0) << 16}; emit(c, 1); }
static u32 frames_shown(void) { return GPU_STATUS >> 24; }
static u32 cycles(void) { UPTIME_LATCH = 1; return UPTIME_LO; }

// seven-segment digits made of FILL rectangles
static void digit(int x, int y, int d, int s, u16 col) {
    static const unsigned char seg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    int m = seg[d % 10], L = s * 4, T = s;
    if (m & 0x01) fill(x + T, y, L, T, col);                 // a top
    if (m & 0x02) fill(x + T + L, y + T, T, L, col);         // b top right
    if (m & 0x04) fill(x + T + L, y + 2 * T + L, T, L, col); // c bottom right
    if (m & 0x08) fill(x + T, y + 2 * (T + L), L, T, col);   // d bottom
    if (m & 0x10) fill(x, y + 2 * T + L, T, L, col);         // e bottom left
    if (m & 0x20) fill(x, y + T, T, L, col);                 // f top left
    if (m & 0x40) fill(x + T, y + T + L, L, T, col);         // g middle
}
static void number(int x, int y, int v, int s, u16 col) {
    int w = s * 7;
    if (v >= 100) { digit(x, y, v / 100, s, col); x += w; }
    if (v >= 10) { digit(x, y, (v / 10) % 10, s, col); x += w; }
    digit(x, y, v % 10, s, col);
}

int main(void) {
    FB_DMA_ENABLE = 0;      // LiteX's own framebuffer is not on screen any more: stop it reading SDRAM
    AUDIO_PLAY_EN = 1;
    u32 ok = GPU_ID == 0x4C475031u;

    int bx[6], by[6], vx[6], vy[6];
    for (int i = 0; i < 6; i++) { bx[i] = 20 + i * 37; by[i] = 60 + i * 23; vx[i] = (i & 1) ? 2 : -3; vy[i] = (i & 2) ? 3 : -2; }
    int vig = 0, fade = 0, stress = 0, nstress = 4, freeze = 0, fps = 0, t = 0;
    u32 prev_keys = 0, sub = frames_shown(), last_f = sub, t0 = cycles(), cpu_ms10 = 0;

    for (;;) {
        u32 c0 = cycles();
        u32 keys = CONT1_KEY & 0xFFFF, hit = keys & ~prev_keys; prev_keys = keys;
        if (hit & (1u << 4)) vig ^= 1;
        if (hit & (1u << 5)) fade = 1;
        if (hit & (1u << 6)) stress ^= 1;
        if (hit & (1u << 7)) freeze ^= 1;
        if ((hit & (1u << 0)) && nstress < 16) nstress++;
        if ((hit & (1u << 1)) && nstress > 1) nstress--;
        if (!freeze) t++;

        clip(0, 0, W, H);
        // background: one FILL per row, slowly scrolling colours
        for (int y = 0; y < H; y++) {
            int a = (y + t) & 255, b = (y * 2 - t) & 255;
            fill(0, y, W, 1, RGB(a, b / 2, 255 - a));
        }
        // colour bars: red, green, blue, white, cyan, magenta, yellow, grey
        static const u16 bars[8] = {RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 255, 255),
                                    RGB(0, 255, 255), RGB(255, 0, 255), RGB(255, 255, 0), RGB(128, 128, 128)};
        for (int i = 0; i < 8; i++) fill(5 + i * 32, 4, 30, 14, bars[i]);
        // bouncing squares (some partly off screen: tests clipping at the edges)
        for (int i = 0; i < 6; i++) {
            if (!freeze) {
                bx[i] += vx[i]; by[i] += vy[i];
                if (bx[i] < -10 || bx[i] > W - 20) vx[i] = -vx[i];
                if (by[i] < 20 || by[i] > H - 20) vy[i] = -vy[i];
            }
            fill(bx[i], by[i], 30, 30, bars[i]);
            fill(bx[i] + 8, by[i] + 8, 14, 14, RGB(0, 0, 0));
        }
        // darkened glass panel across the middle (SHADE)
        shade(30, 110, 206, 40, 28);
        // clipped window: a big checkerboard drawn through a small clip rectangle
        clip(170, 160, 250, 220);
        for (int y = 0; y < 8; y++) for (int x = 0; x < 12; x++)
            fill(150 + x * 12 - (t % 24), 150 + y * 12, 12, 12, ((x + y) & 1) ? RGB(250, 250, 250) : RGB(200, 40, 40));
        clip(0, 0, W, H);
        // stress: full-screen SHADE passes (k = 63 barely darkens; each pass reads and writes every pixel)
        if (stress) for (int i = 0; i < nstress; i++) shade(0, 0, W, H, 63);
        // frame-rate readout: big digits, plus a bar that is full at 60
        fill(4, 198, 92, 40, RGB(0, 0, 0));
        number(8, 202, fps, 3, fps >= 58 ? RGB(80, 255, 80) : fps >= 30 ? RGB(255, 220, 60) : RGB(255, 60, 60));
        fill(100, 226, fps * 2, 8, fps >= 58 ? RGB(80, 255, 80) : RGB(255, 200, 60));
        fill(100 + 120, 222, 2, 16, RGB(255, 255, 255));   // the 60 mark
        // CPU time per frame (tenths of a ms) under the bar
        number(100, 202, (int)cpu_ms10, 2, RGB(200, 200, 255));
        if (stress) number(170, 202, nstress, 2, RGB(255, 160, 60));
        if (!ok) fill(0, 0, W, 3, RGB(255, 0, 0));   // ID register did not answer as expected

        int bright = 64;
        if (fade) { int f = fade - 1; bright = f < 64 ? 64 - f : f - 64; if (bright > 64) bright = 64; if (++fade > 129) fade = 0; }
        u32 c1 = cycles();
        cpu_ms10 = (c1 - c0) / 5712u;
        present(bright, vig);

        // keep the audio FIFO fed with silence so the core is happy
        while (AUDIO_FILL < 800) AUDIO_OUT = 0;

        // stay at most one frame ahead of the screen (PRESENT stalls the GPU, not the CPU)
        sub++;
        while (((sub - frames_shown()) & 0xFF) >= 2) { }
        // frames actually shown per second
        u32 now = cycles();
        if (now - t0 >= 57120000u) { u32 f = frames_shown(); fps = (int)((f - last_f) & 0xFF); last_f = f; t0 = now; }
    }
}
