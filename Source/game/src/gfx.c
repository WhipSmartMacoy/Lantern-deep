// Graphics: palette, light shading, indexed blits, primitives, text.
#include "common.h"
#include "art_data.h"
#include "gpu.c"
static u32 g_font4, g_font3;   // VRAM: 64 glyphs of 4x7, then 64 of 3x5

#ifndef HOST_BUILD
__attribute__((no_builtin("memset"))) void *memset(void *d, int c, size_t n) {
    u8 *p = d; while (n--) *p++ = (u8)c; return d;
}
__attribute__((no_builtin("memcpy"))) void *memcpy(void *d, const void *s, size_t n) {
    u8 *p = d; const u8 *q = s;
    if ((((uintptr_t)p | (uintptr_t)q | n) & 3) == 0) {
        u32 *pw = d; const u32 *qw = s; n >>= 2; while (n--) *pw++ = *qw++; return d;
    }
    while (n--) *p++ = *q++; return d;
}
__attribute__((no_builtin("memmove"))) void *memmove(void *d, const void *s, size_t n) {
    u8 *p = d; const u8 *q = s;
    if (p < q) { while (n--) *p++ = *q++; }
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
#endif

#define RGB(r,g,b) ((u16)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))

static const u8 PALETTE_RGB[32][3] = {
    {0,0,0},{12,10,14},{40,36,44},{80,74,82},{130,124,130},{232,228,220},
    {44,40,50},{66,60,70},{92,84,94},
    {64,40,26},{116,74,42},{176,134,82},{222,172,132},
    {112,22,26},{204,44,40},{244,136,40},{252,222,84},
    {30,84,42},{64,176,74},{156,232,96},
    {30,40,112},{64,114,224},{96,204,232},
    {72,30,104},{156,74,214},{244,112,204},
    {222,212,182},{150,140,120},{164,174,194},{92,98,116},{236,184,52},{156,104,84},
};

#define SHADES 8
#define NPAL 256
static u8 g_pal_rgb[NPAL][3];         // 0..31 built-in, 32.. imported art colors
static u16 g_shade[SHADES][NPAL];     // light level -> palette index -> RGB565
static u16 *g_fb;
static int g_clip_x0 = 0, g_clip_y0 = 0, g_clip_x1 = SCR_W, g_clip_y1 = SCR_H;

static void font_upload(void);
static void gfx_init(void) {
    if (!g_font4) font_upload();
    for (int i = 0; i < NPAL; i++) for (int k = 0; k < 3; k++)
        g_pal_rgb[i][k] = i < 32 ? PALETTE_RGB[i][k] : (i - 32 < EXTRA_PAL_N ? EXTRA_PAL[i - 32][k] : 255);
    for (int l = 0; l < SHADES; l++) {
        // light level 7 = full; slight warm tint as light drops (torchlight feel)
        int f = (l * 256) / (SHADES - 1);
        for (int i = 0; i < NPAL; i++) {
            int r = g_pal_rgb[i][0] * f >> 8, g = g_pal_rgb[i][1] * f >> 8, b = g_pal_rgb[i][2] * f >> 8;
            if (l < SHADES - 1) { b = b * (6 + l) / 13; g = g * (10 + l) / 17; }
            g_shade[l][i] = RGB(r, g, b);
        }
    }
}

static inline u16 pal(int idx) { return g_shade[SHADES - 1][idx]; }

static void gfx_clip(int x0, int y0, int x1, int y1) {
    gpu_clip(x0, y0, x1, y1);
    g_clip_x0 = MAX(0, x0); g_clip_y0 = MAX(0, y0); g_clip_x1 = MIN(SCR_W, x1); g_clip_y1 = MIN(SCR_H, y1);
}
static void gfx_noclip(void) { gfx_clip(0, 0, SCR_W, SCR_H); }

// Row helpers that store two pixels per 32-bit write: on the Pocket every store goes straight out to SDRAM,
// so halving the number of stores matters far more than the extra loads.
static inline void row_copy(u16 *d, const u16 *src, int n) {
    if (n <= 0) return;
    if ((uintptr_t)d & 2) { *d++ = *src++; n--; }
    u32 *dw = (u32 *)d;
    while (n >= 8) { dw[0] = src[0] | (u32)src[1] << 16; dw[1] = src[2] | (u32)src[3] << 16; dw[2] = src[4] | (u32)src[5] << 16; dw[3] = src[6] | (u32)src[7] << 16; dw += 4; src += 8; n -= 8; }
    while (n >= 2) { *dw++ = src[0] | (u32)src[1] << 16; src += 2; n -= 2; }
    if (n) *(u16 *)dw = *src;
}
static inline void row_fill(u16 *d, u16 c, int n) {
    if (n <= 0) return;
    if ((uintptr_t)d & 2) { *d++ = c; n--; }
    u32 v = c | (u32)c << 16, *dw = (u32 *)d;
    while (n >= 8) { dw[0] = v; dw[1] = v; dw[2] = v; dw[3] = v; dw += 4; n -= 8; }
    while (n >= 2) { *dw++ = v; n -= 2; }
    if (n) *(u16 *)dw = c;
}
// ---- primitives: all drawing goes through the GPU command stream (gpu.c) ----
static void fill_rect(int x, int y, int w, int h, u16 c) { gpu_fill(x, y, w, h, c); }
static void dim_rect(int x, int y, int w, int h) { gpu_shade(x, y, w, h, 16); }   // quarter brightness
static inline void pset(int x, int y, u16 c) { gpu_fill(x, y, 1, 1, c); }
static void hline(int x, int y, int w, u16 c) { fill_rect(x, y, w, 1, c); }
static void vline(int x, int y, int h, u16 c) { fill_rect(x, y, 1, h, c); }
static void rect_outline(int x, int y, int w, int h, u16 c) {
    hline(x, y, w, c); hline(x, y + h - 1, w, c); vline(x, y + 1, h - 2, c); vline(x + w - 1, y + 1, h - 2, c);
}
static void line(int x0, int y0, int x1, int y1, u16 c) {
    int dx = ABS(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -ABS(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void disc(int cx, int cy, int r, u16 c) {   // one span per row
    for (int y = -r; y <= r; y++) { int w = 0; while ((w + 1) * (w + 1) + y * y <= r * r + r) w++; if (w * w + y * y <= r * r + r) gpu_fill(cx - w, cy + y, 2 * w + 1, 1, c); }
}

// Indexed (palette) sprites are converted to 16-bit colour once and kept in VRAM; light levels use the GPU's tables.
static u32 pal_image(const Sprite *s, const u16 *pal16, const u8 *remap) {
    u32 v = (u32)(uintptr_t)remap ^ ((u32)(uintptr_t)pal16 * 31u) ^ 0x5A17u;
    u32 a = vcache_find(s, v);
    if (a) return a;
    int n = s->w * s->h; a = vram_alloc((u32)n);
    if (!a) return 0;
    for (int k = 0; k < n; k++) {
        u8 c = s->px[k];
        if (!c) { g_vram[a + k] = 0; continue; }
        if (remap) c = remap[c];
        u16 o = pal16[c]; g_vram[a + k] = o ? o : 0x0020;   // keep black pixels opaque
    }
    vram_sync(a, (u32)n); vcache_put(s, v, a);
    return a;
}
static void blit_ex(const Sprite *s, int x, int y, const u16 *pal16, const u8 *remap, int flip, int scale) {
    int lvl = -1;
    for (int l = 0; l < SHADES; l++) if (pal16 == g_shade[l]) lvl = l;
    u32 a = pal_image(s, lvl >= 0 ? g_shade[SHADES - 1] : pal16, remap);
    gpu_blit(a, s->w, x, y, s->w * scale, s->h * scale, GB_KEY | (flip ? GB_FLIPX : 0) | (lvl >= 0 && lvl < 7 ? GB_LUT : 0),
             LUT_ID(0, lvl >= 0 ? lvl : 7), 0, 256 / scale, 256 / scale);
}
static void blit(const Sprite *s, int x, int y) { blit_ex(s, x, y, g_shade[SHADES - 1], 0, 0, 1); }
// solid-color silhouette (hit flash)
static void blit_flash(const Sprite *s, int x, int y, u16 col, int flip, int scale) {
    u32 a = pal_image(s, g_shade[SHADES - 1], 0);
    gpu_blit(a, s->w, x, y, s->w * scale, s->h * scale, GB_KEY | GB_TINT | (flip ? GB_FLIPX : 0), 7, col, 256 / scale, 256 / scale);
}

// ---- text (4x7 sans-serif font, 5px advance; v0.20) — glyphs live in VRAM as 1/0 masks, drawn tinted ----
#define CH_W 5     // advance per character
#define CH_H 7     // glyph height
#define LH 9       // line height for stacked text
static void font_upload(void) {
    g_font4 = vram_alloc_persist(64 * 28); g_font3 = vram_alloc_persist(64 * 15);
    for (int ch = 0; ch < 64; ch++) {
        u32 g = FONT4x7[ch]; for (int k = 0; k < 28; k++) g_vram[g_font4 + ch * 28 + k] = (g >> k) & 1;
        u16 h = FONT3x5[ch]; for (int k = 0; k < 15; k++) g_vram[g_font3 + ch * 15 + k] = (h >> k) & 1;
    }
    vram_sync(g_font4, 64 * 28); vram_sync(g_font3, 64 * 15);
}
static inline int glyph_index(char ch) { if (ch >= 'a' && ch <= 'z') ch -= 32; if (ch < 32 || ch > 95) ch = '?'; return ch - 32; }
static int text_w(const char *s) { int n = 0; while (*s++) n++; return n ? n * CH_W - 1 : 0; }
static void draw_char(int x, int y, char ch, u16 col) {
    if (ch == ' ') return;
    gpu_blit(g_font4 + glyph_index(ch) * 28, 4, x, y, 4, 7, GB_KEY | GB_TINT, 7, col, 256, 256);
}
static void draw_char2(int x, int y, char ch, u16 col) {
    if (ch == ' ') return;
    gpu_blit(g_font4 + glyph_index(ch) * 28, 4, x, y, 8, 14, GB_KEY | GB_TINT, 7, col, 128, 128);
}
static void text(int x, int y, const char *s, u16 col) { while (*s) { draw_char(x, y, *s++, col); x += CH_W; } }
static void text_shadow(int x, int y, const char *s, u16 col) { text(x + 1, y + 1, s, RGB(0, 0, 0)); text(x, y, s, col); }
static void text_c(int cx, int y, const char *s, u16 col) { text_shadow(cx - text_w(s) / 2, y, s, col); }
static int text_small_w(const char *s) { int n = 0; while (*s++) n++; return n * 4; }
static void text_small(int x, int y, const char *s, u16 col) {   // 3x5 fine print (badges, hints)
    for (; *s; s++, x += 4) if (*s != ' ') gpu_blit(g_font3 + glyph_index(*s) * 15, 3, x, y, 3, 5, GB_KEY | GB_TINT, 7, col, 256, 256);
}
static void text2(int x, int y, const char *s, u16 col) { while (*s) { draw_char2(x, y, *s++, col); x += CH_W * 2; } }
static void text2_c(int cx, int y, const char *s, u16 col) {
    int w = text_w(s) * 2; text2(cx - w / 2 + 1, y + 1, s, RGB(0, 0, 0)); text2(cx - w / 2, y, s, col);
}

// ---- tiny string builder ----
typedef struct { char b[96]; int n; } Str;
static void s_reset(Str *s) { s->n = 0; s->b[0] = 0; }
static void s_add(Str *s, const char *t) { while (*t && s->n < 95) s->b[s->n++] = *t++; s->b[s->n] = 0; }
static void s_int(Str *s, i32 v) {
    char tmp[12]; int n = 0; u32 u = v < 0 ? (u32)(-v) : (u32)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) tmp[n++] = '-';
    while (n && s->n < 95) s->b[s->n++] = tmp[--n];
    s->b[s->n] = 0;
}
static void s_chr(Str *s, char c) { if (s->n < 95) { s->b[s->n++] = c; s->b[s->n] = 0; } }
