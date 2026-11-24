// Lantern GPU hardware test screen (milestone 2). Shown at power-on on the custom core; START continues to the game.
// Every tile tests one GPU feature; the emulator renders the same screen (build/selftest_preview.png) to compare.
//
//   tile        what it tests                      should look like
//   BLIT        plain image                        round badge with a white arrow, black corners
//   KEY         transparency (value 0)             same badge, no corners: the checkerboard shows through
//   FLIP        mirrored                           arrow points left
//   SCALE 2X    8.8 step = 0.5                     badge twice as big, blocky
//   OUTLINE     value 1 darkens what is under it   dark ring around the badge
//   TINT        solid colour silhouette            red disc
//   LUT         light tables                       three badges, dark to bright
//   GLOW        additive light from a mask         soft orange light pool
//   SEE         see-through cut-out                grey wall with an oval hole, dithered edge
// Also: all the text (font = TINT blits), FPS at the bottom (should be 60).

#define ST_N 24
static u16 g_st_img[ST_N * ST_N];

static void st_make_image(void) {
    for (int y = 0; y < ST_N; y++) for (int x = 0; x < ST_N; x++) {
        int dx = 2 * x - (ST_N - 1), dy = 2 * y - (ST_N - 1), d2 = dx * dx + dy * dy;   // doubled coordinates
        u16 c;
        if (d2 > 23 * 23) c = 0;                          // outside: transparent with KEY
        else if (d2 > 19 * 19) c = 1;                     // ring: the outline marker
        else {
            c = RGB(40 + x * 8, 60 + y * 7, 200 - x * 4);  // gradient
            int ax = x - 6, ay = y - 12; if (ay < 0) ay = -ay;
            if ((ax >= 0 && ax < 12 && ay <= 1) || (ax >= 7 && ax < 12 && ay <= 11 - ax)) c = RGB(255, 255, 255);   // arrow pointing right
        }
        g_st_img[y * ST_N + x] = c;
    }
}

static void st_checker(int x, int y, int w, int h) {
    for (int j = 0; j < h; j += 6) for (int i = 0; i < w; i += 6)
        gpu_fill(x + i, y + j, MIN(6, w - i), MIN(6, h - j), ((i + j) / 6) & 1 ? RGB(90, 90, 110) : RGB(150, 150, 170));
}

static void gpu_selftest(void) {
    st_make_image();
    u32 a = vram_image(g_st_img, ST_N * ST_N);
    u32 t = 0, f0 = frame_counter(), c0 = cycles(), sub = frame_counter(); int fps = 0;
    for (;;) {
        u32 keys = CONT1_KEY & 0xFFFF;
        if (keys & BTN_START) break;
        if (GPU_STATUS & (3u << 18)) break;   // sleep / wake request: the game loop answers it
        t++;
        int bob = (int)((t / 4) % 8); bob = bob < 4 ? bob : 8 - bob;   // gentle up/down: shows smooth 60 fps

        gpu_clip(0, 0, SCR_W, SCR_H);
        gpu_fill(0, 0, SCR_W, SCR_H, RGB(24, 22, 34));
        static const char *names[9] = {"BLIT", "KEY", "FLIP", "SCALE 2X", "OUTLINE", "TINT", "LUT", "GLOW", "SEE"};
        for (int i = 0; i < 9; i++) {
            int tx = 4 + (i % 3) * 87, ty = 4 + (i / 3) * 72, tw = 84, th = 68;
            if (i == 7) gpu_fill(tx, ty, tw, th, RGB(10, 10, 14)); else st_checker(tx, ty, tw, th);
            int cx = tx + tw / 2 - ST_N / 2, cy = ty + 30 - ST_N / 2 + bob;
            gpu_clip(tx, ty, tx + tw, ty + th);
            switch (i) {
            case 0: gpu_blit(a, ST_N, cx, cy, ST_N, ST_N, 0, LUT_ID(0, 7), 0, 256, 256); break;
            case 1: gpu_blit(a, ST_N, cx, cy, ST_N, ST_N, GB_KEY, LUT_ID(0, 7), 0, 256, 256); break;
            case 2: gpu_blit(a, ST_N, cx, cy, ST_N, ST_N, GB_KEY | GB_FLIPX, LUT_ID(0, 7), 0, 256, 256); break;
            case 3: gpu_blit(a, ST_N, cx - ST_N / 2, cy - ST_N / 2 + 4, ST_N * 2, ST_N * 2, GB_KEY, LUT_ID(0, 7), 0, 128, 128); break;
            case 4: gpu_blit(a, ST_N, cx, cy, ST_N, ST_N, GB_KEY | GB_OUTLINE, LUT_ID(0, 7), 0, 256, 256); break;
            case 5: gpu_blit(a, ST_N, cx, cy, ST_N, ST_N, GB_KEY | GB_TINT, LUT_ID(0, 7), RGB(230, 40, 40), 256, 256); break;
            case 6: for (int k = 0; k < 3; k++) gpu_blit(a, ST_N, tx + 2 + k * 27, cy, ST_N, ST_N, GB_KEY | GB_LUT, LUT_ID(0, 1 + k * 3), 0, 256, 256); break;
            case 7: glow(tx + tw / 2, ty + 32 + bob, 30, 22, 26, 14, 2); break;
            case 8: {
                gpu_see_ellipse(tx + tw / 2, ty + 30 + bob, 18, 14);
                gpu_blit(a, ST_N, tx + 10, ty + 8, tw - 20, 46, GB_TINT | GB_SEE, LUT_ID(0, 7), RGB(120, 120, 120), 64, 128);
                break; }
            }
            gpu_clip(0, 0, SCR_W, SCR_H);
            text_c(tx + tw / 2, ty + th - 10, names[i], RGB(255, 230, 150));
        }
        char buf[40]; int n = 0; const char *s = "LANTERN GPU TEST  FPS ";
        while (*s) buf[n++] = *s++;
        if (fps >= 10) buf[n++] = (char)('0' + fps / 10);
        buf[n++] = (char)('0' + fps % 10);
        s = "  START: PLAY"; while (*s) buf[n++] = *s++; buf[n] = 0;
        text_shadow(6, 224, buf, fps >= 58 ? RGB(120, 255, 120) : RGB(255, 200, 80));
        gpu_present(64, 0);
        sub++;
        while (sub - frame_counter() >= 2) { }
        u32 now = cycles();
        if (now - c0 >= 57120000u) { u32 f = frame_counter(); fps = (int)(f - f0); f0 = f; c0 = now; }
        // keep the audio FIFO fed with silence
        while (AUDIO_FILL < 800) AUDIO_OUT = 0;
    }
    while ((CONT1_KEY & BTN_START) && !(GPU_STATUS & (3u << 18))) { }   // don't pass the press on to the title screen
}
