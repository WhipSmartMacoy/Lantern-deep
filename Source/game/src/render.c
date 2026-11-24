// Rendering: isometric world with torch lighting, town, effects, HUD.

#define VIEW 15
static u8 g_light[2 * VIEW + 1][2 * VIEW + 1];
static int g_camx, g_camy;
static int g_hide_player, g_no_amb, g_door_bg_build;
static void draw_amb(void);   // hero-select chamber: draw the world without the hero, with a softer, smaller light
static u8 g_remap_champ[NPAL], g_remap_rare[NPAL], g_remap_mpot[NPAL], g_remap_gem[G_TYPES][NPAL], g_remap_elix[EL_TYPES][NPAL];
static int g_remap_ready;

static void band_remaps_init(void);
static void remap_init(void) {
    for (int i = 0; i < NPAL; i++) {
        int l = (g_pal_rgb[i][0] * 3 + g_pal_rgb[i][1] * 6 + g_pal_rgb[i][2]) / 10;
        g_remap_champ[i] = (u8)(i <= 1 ? i : l < 60 ? 20 : l < 120 ? 21 : l < 190 ? 22 : 5);
        g_remap_rare[i] = (u8)(i <= 1 ? i : l < 60 ? 9 : l < 120 ? 15 : l < 190 ? 30 : 16);
        g_remap_mpot[i] = (u8)(i == 14 ? 21 : i == 13 ? 20 : i);
        for (int g = 0; g < G_TYPES; g++) g_remap_gem[g][i] = (u8)(i == 22 ? GEM_REMAP[g][0] : i == 20 ? GEM_REMAP[g][1] : i);
        // elixirs: the red of the health potion becomes orange (wrath), gold (precision) or steel blue (ironskin)
        int r = g_pal_rgb[i][0], g = g_pal_rgb[i][1], b = g_pal_rgb[i][2];
        for (int e = 0; e < EL_TYPES; e++) {
            g_remap_elix[e][i] = (u8)i;
            if (i < 2 || r < 60 || r < g * 3 / 2 || r < b * 3 / 2) continue;   // only the red liquid
            int lum = (r * 3 + g * 6 + b) / 10 + r / 3, tr, tg, tb;
            if (e == EL_WRATH) { tr = r; tg = r * 50 / 100; tb = r / 10; }
            else if (e == EL_PREC) { tr = r; tg = r * 85 / 100; tb = r / 5; }
            else { tr = lum * 70 / 100; tg = lum * 85 / 100; tb = MIN(255, lum * 115 / 100); }
            int best = i, bd = 1 << 30;
            for (int k = 2; k < NPAL; k++) {
                int dr = g_pal_rgb[k][0] - tr, dg = g_pal_rgb[k][1] - tg, db = g_pal_rgb[k][2] - tb, d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                if (k >= 32 + EXTRA_PAL_N) break;
                if (d < bd) { bd = d; best = k; }
            }
            g_remap_elix[e][i] = (u8)best;
        }
    }
    band_remaps_init();
    g_remap_ready = 1;
}

// top-down 3/4 camera: world x -> screen x, world y -> screen y (foreshortened)
static inline int iso_x(i32 x, i32 y) { (void)y; return (x * TW >> 8) - g_camx; }
static inline int iso_y(i32 x, i32 y) { (void)x; return (y * TH >> 8) - g_camy; }
static const HiSprite *const PROP_SPR[PR_COUNT] = {0, &HC_p_barrel, &HC_p_crate, &HC_p_urn, &HC_p_pillar, &HC_p_lamp, &HC_p_well, &HC_p_cart, &HC_p_fence,
    &HC_p_torch, &HC_p_brazier, &HC_p_hedge, &HC_p_flowerbed, &HC_p_bench, &HC_p_log, &HC_p_stump,
    &HC_p_bones, &HC_p_rubble, &HC_p_mushroom, &HC_p_candles, &HC_p_bush, &HC_p_flowers, &HC_p_rock, &HC_p_tallgrass, &HC_p_fern};
static const HiSprite *const TREE_HC[3] = {&HC_tree, &HC_tree_1, &HC_tree_2};
static const HiSprite *const DIRT_HC[3] = {&HC_t_dirt_0, &HC_t_dirt_1, &HC_t_dirt_2};
static inline int earthy(int x, int y) {   // dungeon: walls and dirt patches spill loose earth over neighbouring flagstones
    if (!in_map(x, y)) return 0; int t = g_map[y][x];
    return t == T_WALL || (t == T_FLOOR && g_dirt[y][x]) || (t == T_PROP && g_dirt[y][x]);
}
static inline int grassy(int x, int y) { if (!in_map(x, y)) return 0; int t = g_map[y][x]; return t == T_GRASS || t == T_TREE || (t == T_PROP && g_prop_floor[y][x] == T_GRASS); }
static void flame(int fx, int fy, int seed) {   // small animated fire
    int f = (G.ticks / 4 + seed) & 3;
    pset(fx, fy, pal(16)); pset(fx - 1, fy, pal(15)); pset(fx + 1, fy, pal(15));
    pset(fx, fy - 1, pal(15)); pset(fx + (f == 1) - (f == 3), fy - 2, pal(14)); pset(fx - (f == 2), fy - 1, pal(16));
    if (f & 1) pset(fx + (f == 1 ? 1 : -1), fy - 3, pal(14));
    if (!(f & 2)) pset(fx, fy - 3, pal(15));
}
static const HiSprite *const BLD_SPR[4] = {&HC_b_merchant, &HC_b_smith, &HC_b_jeweler, &HC_b_trainer};
static const HiSprite *const FLOOR_HC[4] = {&HC_t_floor_0, &HC_t_floor_1, &HC_t_floor_2, &HC_t_floor_3};
static const HiSprite *const WALL_HC[4] = {&HC_t_wall_0, &HC_t_wall_1, &HC_t_wall_2, &HC_t_wall_3};
static const HiSprite *const GRASS_HC[3] = {&HC_t_grass_0, &HC_t_grass_1, &HC_t_grass_2};
static const HiSprite *const COBBLE_HC[3] = {&HC_t_cobble_0, &HC_t_cobble_1, &HC_t_cobble_2};
static inline int light_at(int tx, int ty) {
    int lx = tx - G.ptx + VIEW, ly = ty - G.pty + VIEW;
    if (lx < 0 || ly < 0 || lx > 2 * VIEW || ly > 2 * VIEW) return 0;
    return g_light[ly][lx];
}

static void compute_light(void) {
    Player *p = &G.p;
    if (G.in_town) {   // dusk: soft ambient light, torches and braziers glow
        for (int ly = 0; ly <= 2 * VIEW; ly++) for (int lx = 0; lx <= 2 * VIEW; lx++) {
            int tx = G.ptx + lx - VIEW, ty = G.pty + ly - VIEW;
            g_light[ly][lx] = (u8)(in_map(tx, ty) && g_map[ty][tx] != T_ROCK ? 5 : 0);
        }
        return;   // town torches glow as soft light pools instead (see glow())
    }
    int R = g_hide_player ? 4 * 16 : (p->powers & P_LIGHT) ? 10 * 16 : 7 * 16;
    static const i8 FL[16] = {0, 2, 3, 1, -1, 2, 4, 3, 0, -2, 1, 3, 2, 0, -1, 1};
    if (!g_hide_player) R += FL[(G.ticks / 4) & 15];
    i32 R2 = R * R;
    for (int ly = 0; ly <= 2 * VIEW; ly++) for (int lx = 0; lx <= 2 * VIEW; lx++) {
        int tx = G.ptx + lx - VIEW, ty = G.pty + ly - VIEW; u8 l = 0;
        if (in_map(tx, ty) && g_map[ty][tx] != T_ROCK) {
            l = g_hide_player ? 2 : 3;   // the whole floor is visible in dim ambient light; the hero's lantern brightens around them
            if (tile_visible(tx, ty)) {
                i32 dx = ((tx * FX + 128) - p->x) >> 4, dy = ((ty * FX + 128) - p->y) >> 4;
                i32 d2 = dx * dx + dy * dy;
                if (d2 < R2) { int v = g_hide_player ? 2 + (int)(6 * (R2 - d2) / R2) : 3 + (int)(9 * (R2 - d2) / R2); l = (u8)MIN(7, MAX(g_hide_player ? 2 : 3, v)); }   // bright plateau, short falloff
            }
        }
        g_light[ly][lx] = l;
    }
    for (int ly = 0; ly <= 2 * VIEW; ly++) for (int lx = 0; lx <= 2 * VIEW; lx++) {
        int tx = G.ptx + lx - VIEW, ty = G.pty + ly - VIEW;
        if (!in_map(tx, ty) || !g_torch[ty][tx]) continue;
        for (int oy = -3; oy <= 3; oy++) for (int ox = -3; ox <= 3; ox++) {
            int ax = lx + ox, ay = ly + oy; if (ax < 0 || ay < 0 || ax > 2 * VIEW || ay > 2 * VIEW) continue;
            int tx2 = tx + ox, ty2 = ty + oy; if (!in_map(tx2, ty2)) continue;
            int d2 = ox * ox + oy * oy; if (d2 > (G.in_town ? 1 : 5)) continue;
            u8 v = (u8)(7 - d2 / 2); if (v > g_light[ay][ax]) g_light[ay][ax] = v;
        }
    }
    if (g_gfx_effects) for (int i = 0; i < MAX_PROJ; i++) {   // spells light up the floor and walls as they fly past
        Proj *q = &G.proj[i]; if (!q->alive) continue;
        int pk = q->kind; if (pk != PK_BOLT && pk != PK_LIGHT && pk != PK_ORB && pk != PK_FIRE && pk != PK_EXPLO && pk != PK_BIG) continue;
        int qx = q->x >> 8, qy = q->y >> 8;
        for (int oy = -2; oy <= 2; oy++) for (int ox = -2; ox <= 2; ox++) {
            int tx = qx + ox, ty = qy + oy, lx = tx - G.ptx + VIEW, ly = ty - G.pty + VIEW;
            if (lx < 0 || ly < 0 || lx > 2 * VIEW || ly > 2 * VIEW || !in_map(tx, ty) || g_map[ty][tx] == T_ROCK) continue;
            int d2 = ox * ox + oy * oy; if (d2 > 5) continue;
            u8 v = (u8)(7 - d2); if (v > g_light[ly][lx]) g_light[ly][lx] = v;
        }
    }
}

static const Sprite *item_icon(int base) {
    static const Sprite *ICONS[B_COUNT] = {&SPR_i_sword, &SPR_i_bow, &SPR_i_staff, &SPR_i_shield, &SPR_i_tome, &SPR_i_helm, &SPR_i_amulet,
                                           &SPR_i_chest, &SPR_i_legs, &SPR_i_boots, &SPR_i_gloves, &SPR_i_ring, &SPR_i_cape, &SPR_i_quiver};
    return ICONS[base];
}
// ---- item looks: Majestic uniques have their own art; everything else changes material every 10 item levels ----
static const Sprite *majestic_icon(const Item *it) {
    if (!it->uniq || it->rarity != R_MAJESTIC) return 0;
    switch (UNIQUES[it->uniq - 1].power) {
    case P_UNDYING: return &SPR_i_maj_crown;
    case P_TWIN | P_CLEAVE: return &SPR_i_maj_twinfang;
    case P_FIVE: return &SPR_i_maj_heavensplit;
    case P_STARFALL: return &SPR_i_maj_starfall;
    case P_DEEPHEART: return &SPR_i_maj_heart;
    case P_MIDAS: return &SPR_i_maj_midas;
    case P_SHROUD: return &SPR_i_maj_shroud;
    }
    return 0;
}
// material bands by item level: 1-9 rusted iron, 10-19 steel (the base art), 20-29 blued steel,
// 30-39 bright silver with crimson leather, 40+ obsidian with ember-red accents and a faint glow
#define IBANDS 5
static u8 g_remap_band[IBANDS][NPAL];
static const char *BAND_NAME[IBANDS] = {"RUSTED", "STEEL", "BLUED", "SILVERED", "OBSIDIAN"};
static int item_band(const Item *it) { return MIN(IBANDS - 1, it->ilvl / 10); }
static int pal_nearest(int tr, int tg, int tb) {
    int best = 2, bd = 1 << 30;
    for (int k = 2; k < 32 + EXTRA_PAL_N && k < NPAL; k++) {
        int dr = g_pal_rgb[k][0] - tr, dg = g_pal_rgb[k][1] - tg, db = g_pal_rgb[k][2] - tb, d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}
static void band_remaps_init(void) {
    for (int b = 0; b < IBANDS; b++) for (int i = 0; i < NPAL; i++) {
        g_remap_band[b][i] = (u8)i;
        if (i < 2 || b == 1 || i >= 32 + EXTRA_PAL_N) continue;
        int r = g_pal_rgb[i][0], g = g_pal_rgb[i][1], bl = g_pal_rgb[i][2];
        int mx = MAX(r, MAX(g, bl)), mn = MIN(r, MIN(g, bl)), sat = mx ? (mx - mn) * 100 / mx : 0, l = (r * 3 + g * 6 + bl) / 10;
        if (l < 34) continue;   // outlines and deep shadow stay
        int metal = sat < 30, warm = !metal && r > bl + 30 && r >= g, gold = warm && g > r * 55 / 100 && sat > 45 && l > 110;   // greys carry a warm grade, so 'metal' allows a little colour
        int tr = r, tg = g, tb = bl;
        if (metal) {
            if (b == 0) { tr = l * 92 / 100 + 10; tg = l * 76 / 100; tb = l * 62 / 100; }        // rust brown
            else if (b == 2) { tr = l * 72 / 100; tg = l * 84 / 100; tb = MIN(255, l * 112 / 100); }   // blue steel
            else if (b == 3) { tr = MIN(255, l * 120 / 100 + 10); tg = MIN(255, l * 120 / 100 + 10); tb = MIN(255, l * 116 / 100 + 10); }   // bright silver
            else { tr = l * 40 / 100; tg = l * 32 / 100; tb = l * 46 / 100; }                     // obsidian
        } else if (gold) {
            if (b == 0) { tr = r * 70 / 100; tg = g * 60 / 100; tb = bl * 60 / 100; }              // tarnished brass
            else if (b == 4) { tr = MIN(255, r * 105 / 100); tg = g * 45 / 100; tb = bl / 4; }     // ember red
            else continue;
        } else if (warm) {   // wood and leather
            if (b == 0) { tr = r * 85 / 100; tg = g * 80 / 100; tb = bl * 80 / 100; }
            else if (b == 3) { tr = MIN(255, r * 110 / 100); tg = g * 45 / 100; tb = bl * 55 / 100; }   // crimson leather
            else if (b == 4) { tr = r * 45 / 100; tg = g * 35 / 100; tb = bl * 50 / 100 + 10; }
            else continue;
        } else continue;
        g_remap_band[b][i] = (u8)pal_nearest(CLAMP(tr, 0, 255), CLAMP(tg, 0, 255), CLAMP(tb, 0, 255));
    }
}
// draw an item's picture (inventory icon or ground sprite) in its look
static void draw_item_sprite(const Item *it, const Sprite *sp, int x, int y, const u16 *shade, int flip, int sc) {
    const Sprite *mj = majestic_icon(it);
    if (mj) sp = mj;
    const u8 *rm = mj ? 0 : g_remap_band[item_band(it)];
    if (!mj && item_band(it) == 4) {   // obsidian: a faint ember outline that breathes
        int on = ((G.ticks >> 4) & 3) != 0;
        if (on) for (int yy = 0; yy < sp->h; yy++) for (int xx = 0; xx < sp->w; xx++) {
            if (sp->px[yy * sp->w + xx]) continue;
            int n = (xx > 0 && sp->px[yy * sp->w + xx - 1]) || (xx < sp->w - 1 && sp->px[yy * sp->w + xx + 1]) ||
                    (yy > 0 && sp->px[(yy - 1) * sp->w + xx]) || (yy < sp->h - 1 && sp->px[(yy + 1) * sp->w + xx]);
            if (n) { int px = flip ? sp->w - 1 - xx : xx; fill_rect(x + px * sc, y + yy * sc, sc, sc, RGB(120, 30, 10)); }
        }
    }
    blit_ex(sp, x, y, shade, rm, flip, sc);
}
#ifdef HAVE_SPR_i_mpotion
#define MPOT_SPR (&SPR_i_mpotion)
#define MPOT_REMAP ((const u8 *)0)
#else
#define MPOT_SPR (&SPR_i_potion)
#define MPOT_REMAP g_remap_mpot
#endif
static void draw_potion(int mana, int x, int y, const u16 *pal16) { blit_ex(mana ? MPOT_SPR : &SPR_i_potion, x, y, pal16, mana ? MPOT_REMAP : (const u8 *)0, 0, 1); }
static void blit_origin(const Sprite *s, int sx, int sy, const u16 *pal16, const u8 *rm, int flip, int scale) {
    int ox = flip ? (s->w - 1 - s->ox) : s->ox;
    blit_ex(s, sx - ox * scale, sy - s->oy * scale, pal16, rm, flip, scale);
}
static void flash_origin(const Sprite *s, int sx, int sy, u16 col, int flip, int scale) {
    int ox = flip ? (s->w - 1 - s->ox) : s->ox;
    blit_flash(s, sx - ox * scale, sy - s->oy * scale, col, flip, scale);
}
// screen dir8 (0=E..7=NE) -> baked direction (0=S,1=SE,2=E,3=NE,4=N) + mirror flag
static const u8 DIR_BAKE[8] = {2, 1, 0, 1, 2, 3, 4, 3}, DIR_FLIP[8] = {0, 0, 0, 1, 1, 1, 0, 0};
static const u8 DIR_BAKE2[8] = {0, 0, 0, 0, 0, 1, 1, 1}, DIR_FLIP2[8] = {0, 0, 0, 1, 1, 1, 0, 0};   // front/back sets
static void draw_gem_icon(int id, int x, int y) { blit_ex(&SPR_gem, x, y, g_shade[SHADES - 1], g_remap_gem[id / G_TIERS], 0, 1); }
static void draw_ring(int cx, int cy, int r, u16 c) {   // isometric ellipse (2:1)
    // simple parametric ellipse with integer steps
    int px = cx + r, py = cy;
    for (int t = 1; t <= 48; t++) {
        // approximate cos/sin via rotation of (r,0) by 7.5 degrees each step
        static i32 cosv[49], sinv[49]; static int init;
        if (!init) { i32 x = 256, y = 0; for (int i = 0; i <= 48; i++) { cosv[i] = x; sinv[i] = y; i32 nx = x - (y * 33 >> 8), ny = y + (nx * 33 >> 8); x = nx; y = ny; } init = 1; }
        int nx = cx + (r * cosv[t] >> 8), ny = cy + ((r * TH / TW) * sinv[t] >> 8);
        line(px, py, nx, ny, c); px = nx; py = ny;
    }
}

// soft contact shadow: darkens an ellipse of the floor under a character
static void shadow(int cx, int cy, int rx) {   // soft contact shadow: light outer band, slightly darker core
    int ry = MAX(2, rx * TH / TW / 2);
    for (int dy = -ry; dy <= ry; dy++) {
        int w = rx * isqrt(256 * 256 - (dy * 256 / (ry + 1)) * (dy * 256 / (ry + 1))) / 256, wi = w * 3 / 5;
        if (ABS(dy) < ry && wi > 0) { gpu_shade(cx - w, cy + dy, w - wi + 1, 1, 56); gpu_shade(cx - wi + 1, cy + dy, 2 * wi - 1, 1, 49); gpu_shade(cx + wi, cy + dy, w - wi + 1, 1, 56); }
        else gpu_shade(cx - w, cy + dy, 2 * w + 1, 1, 56);
    }
}
// ---------- entity drawing ----------
enum { EK_PLAYER, EK_MOB, EK_DROP, EK_CHEST, EK_PROJ, EK_NPC, EK_CORPSE };
#define MAX_ENT (1 + MAX_MOBS + MAX_DROPS + MAX_CHESTS + MAX_PROJ + NPC_COUNT + MAX_CORPSES)
static u16 g_ent_id[MAX_ENT]; static i32 g_ent_key[MAX_ENT]; static i16 g_ent_next[MAX_ENT];
static i16 g_bucket[4 * VIEW + 4];

// ---- full-color characters: shaded per pixel at draw time (light level + optional elite tint) ----
static u16 g_chr[3][8][32], g_chg[3][8][64], g_chb[3][8][32];   // [tint][level][channel value] -> pre-shifted RGB565 part
static void char_luts_init(void) {
    for (int m = 0; m < 3; m++) for (int l = 0; l < 8; l++) {
        int f = l * 256 / 7;
        for (int v = 0; v < 64; v++) {
            int r = v < 32 ? (v << 3) : 0, g = v << 2, b = v < 32 ? (v << 3) : 0;
            r = r * f >> 8; g = (g * f >> 8) * (10 + l) / 17; b = (b * f >> 8) * (6 + l) / 13;
            if (m == 1) { r = r * 55 / 100; g = g * 80 / 100; b = MIN(255, b * 115 / 100 + 50 * f / 256); }        // champion: cold blue
            if (m == 2) { r = MIN(255, r + 45 * f / 256); g = MIN(255, g * 95 / 100 + 25 * f / 256); b = b * 45 / 100; }   // rare: gilded
            if (v < 32) { g_chr[m][l][v] = (u16)((r >> 3) << 11); g_chb[m][l][v] = (u16)(b >> 3); g_lut_r[LUT_ID(m, l)][v] = g_chr[m][l][v]; g_lut_b[LUT_ID(m, l)][v] = g_chb[m][l][v]; }
            g_chg[m][l][v] = (u16)((g >> 2) << 5); g_lut_g[LUT_ID(m, l)][v] = g_chg[m][l][v];
        }
    }
    gpu_luts_upload();
}
// draw with the top-left of the *original* frame at (x, y); flash != 0 draws a solid silhouette in that colour
static void char_draw(const CharSprite *s, int x, int y, int lvl, int tint, int flip, int scale, u16 flash) {
    if (!s) return;
    u32 a = vram_image(s->px, s->w * s->h);
    int ox = flip ? x : x + s->cx * scale, oy = y + s->cy * scale;
    gpu_blit(a, s->w, ox, oy, s->w * scale, s->h * scale, GB_KEY | GB_OUTLINE | (flip ? GB_FLIPX : 0) | (flash ? GB_TINT : GB_LUT),
             LUT_ID(tint, lvl), flash, 256 / scale, 256 / scale);
}
// draw with the frame's anchor (feet) at screen point (sx, sy)
static void char_origin(const CharSprite *s, int sx, int sy, int lvl, int tint, int flip, int scale, u16 flash) {
    if (!s) return;
    int x = flip ? sx - (s->w - 1 - s->ox) * scale : sx - s->ox * scale;
    int y = sy - s->oy * scale;
    char_draw(s, flip ? x : x - s->cx * scale, y - s->cy * scale, lvl, tint, flip, scale, flash);
}
// draw scaled by q/4 (fractional sizes for the hero-select chamber), feet at (sx, sy)
static void char_origin_q(const CharSprite *s, int sx, int sy, int lvl, int flip, int q) {
    if (!s) return;
    u32 a = vram_image(s->px, s->w * s->h);
    int fw = s->w * q / 4, fh = s->h * q / 4;
    int ox = flip ? sx - (s->w - 1 - s->ox) * q / 4 : sx - s->ox * q / 4, oy = sy - s->oy * q / 4;
    gpu_blit(a, s->w, ox, oy, fw, fh, GB_KEY | GB_OUTLINE | GB_LUT | (flip ? GB_FLIPX : 0), LUT_ID(0, lvl), 0, 1024 / q, 1024 / q);
}
static void draw_player(void) {
    Player *p = &G.p;
    int sx = iso_x(p->x, p->y), sy = iso_y(p->x, p->y);
    int lvl = MAX(4, light_at(G.ptx, G.pty));
    shadow(sx, sy, 8);
    if (p->shield > 0) draw_ring(sx, sy - 14, 14 + (G.ticks / 4 & 1), pal(24));
    if (p->warcry_t > 0 && (G.ticks & 8)) draw_ring(sx, sy, 11, pal(15));
#ifdef HAVE_HERO_ANIM
    int w = p->cls;   // each class has its own look (and its own weapon)
    int clip = p->anim, d8 = dir8_of(p->dirx, p->diry);
    if (p->whirl_t > 0) { clip = HA_ATTACK; d8 = (G.ticks / 2) & 7; }
    int n = HERO_ANIM_N[w][clip], f;
    switch (clip) {
    case HA_IDLE: f = (G.ticks / 10) % n; break;
    case HA_WALK: f = ((u32)p->adist * n / 416) % n; break;
    case HA_ROLL: f = MIN(n - 1, (16 - p->roll_t) * n / 16); break;
    case HA_DEATH: f = MIN(n - 1, p->anim_t * n / 36); break;
    default: f = MIN(n - 1, p->anim_t * n / MAX(1, p->anim_len));
    }
    if (p->whirl_t > 0) f = 4;
    const CharSprite *s = HERO_ANIM[w][clip][DIR_BAKE[d8]][f];
    int flip = DIR_FLIP[d8];
    char_origin(s, sx, sy, lvl, 0, flip, 1, (p->hurt > 0 && (p->hurt & 2)) ? RGB(255, 70, 60) : 0);
    if (p->whirl_t > 0) draw_ring(sx, sy - 6, 22, pal(5));
#else
    const Sprite *s = ((p->walk / 6) & 1) ? &SPR_hero_b : &SPR_hero_a;
    blit_ex(s, sx - s->w / 2, sy - s->h + 3, g_shade[lvl], 0, p->face, 1);
#endif
}
static void draw_bar_small(int x, int y, int w, i32 v, i32 max, u16 col) {
    fill_rect(x, y, w, 2, RGB(20, 0, 0));
    if (max > 0) fill_rect(x, y, (int)(w * MAX(0, v) / max), 2, col);
}
static void draw_mob(Mob *m) {
    int tx = m->x >> 8, ty = m->y >> 8;
    int scale = m->boss ? 2 : 1;
    int sx = iso_x(m->x, m->y), sy = iso_y(m->x, m->y);
    int lvl = MAX(3, light_at(tx, ty));
    shadow(sx, sy, (m->type == MT_RAT ? 5 : m->type == MT_BRUTE ? 10 : 7) * scale);
    const u8 *rm = m->elite == 1 ? g_remap_champ : m->elite == 2 ? g_remap_rare : 0;
#ifdef HAVE_MOB_ANIM
    int atk = m->atk_t > 0, n = MOB_ANIM_N[m->type][atk];
    int f = atk ? MIN(n - 1, m->atk_t * n / MOB_ATK_LEN) : (m->kx || m->ky || m->stun) ? 0 : ((u32)m->wdist * n / 176) % n;
    const CharSprite *s = MOB_ANIM[m->type][atk][DIR_BAKE2[m->dir8]][f];
    int flip = DIR_FLIP2[m->dir8];
    int y = sy - s->oy * scale;
    u16 fc = m->flash > 0 ? RGB(255, 255, 255) : (atk && m->atk_t < MOB_ATK_HIT && m->atk_t > MOB_ATK_HIT - 6 && (G.ticks & 2)) ? RGB(255, 200, 120) : 0;   // hit flash / telegraph
    char_origin(s, sx, sy, lvl, m->elite == 1 ? 1 : m->elite == 2 ? 2 : 0, flip, scale, fc);
    (void)rm;
#else
    const Sprite *s = MOBDEF[m->type].spr;
    int x = sx - s->w * scale / 2, y = sy - s->h * scale + 3;
    if (m->flash > 0) blit_flash(s, x, y, RGB(255, 255, 255), m->face, scale);
    else blit_ex(s, x, y, g_shade[lvl], rm, m->face, scale);
#endif
    if (m->stun > 0) { int k = (G.ticks / 4) & 3; pset(sx - 4 + k * 2, y - 3, pal(22)); pset(sx + 3 - k, y - 4, pal(5)); }
    if (m->burn > 0 && (G.ticks & 4)) { pset(sx - 2, y - 1, pal(15)); pset(sx + 1, y - 2, pal(16)); }
    if (!m->boss && (m->hp < m->hpmax || m->elite)) draw_bar_small(sx - 8, y - 4, 16, m->hp, m->hpmax, m->elite == 2 ? pal(30) : m->elite ? pal(21) : pal(14));
    if (m->elite == 2 && dist_fx(m->x, m->y, G.p.x, G.p.y) < 6 * FX) {
        Str s2; s_reset(&s2); s_add(&s2, ELITE_FIRST[m->name1]); s_add(&s2, ELITE_LAST[m->name2]); s_add(&s2, " THE "); s_add(&s2, ELITE_AFFIX[m->eaff]);
        text_c(sx, y - 11, s2.b, RGB(255, 200, 60));
    }
}
static void draw_corpse(Corpse *c) {
    int tx = c->x >> 8, ty = c->y >> 8;
    int sx = iso_x(c->x, c->y), sy = iso_y(c->x, c->y), scale = c->boss ? 2 : 1;
    int lvl = MAX(1, light_at(tx, ty) * c->t / 40);
#ifdef HAVE_MOB_ANIM
    int n = MOB_ANIM_N[c->type][1];
    const CharSprite *s = MOB_ANIM[c->type][1][DIR_BAKE2[c->dir8]][n - 1];
    int sink = (40 - c->t) / 5;
    gfx_clip(0, 0, SCR_W, sy + 2);
    char_origin(s, sx, sy + sink * scale, MIN(7, lvl), c->elite == 2 ? 2 : c->elite ? 1 : 0, DIR_FLIP2[c->dir8], scale, 0);
    gfx_noclip();
#else
    (void)sx; (void)sy; (void)lvl; (void)scale;
#endif
}
static void draw_drop(Drop *d) {
    int tx = d->x >> 8, ty = d->y >> 8;
    int sx = iso_x(d->x, d->y), sy = iso_y(d->x, d->y);
    int hop = d->t < 16 ? (d->t * (16 - d->t)) / 8 : 0;
    int lvl = MAX(4, light_at(tx, ty));
    if (d->kind == DK_GOLD) { blit_origin(&SPR_g_gold, sx, sy + 2 - hop, g_shade[lvl], 0, 0, 1); return; }
    if (d->kind == DK_HPOT) { blit_origin(&SPR_g_potion, sx, sy + 1 - hop, g_shade[lvl], 0, 0, 1); return; }
    if (d->kind == DK_MPOT) { blit_origin(&SPR_g_mpotion, sx, sy + 1 - hop, g_shade[lvl], 0, 0, 1); return; }
    if (d->kind == DK_ELIX) { blit_origin(&SPR_g_potion, sx, sy + 1 - hop, g_shade[lvl], g_remap_elix[d->amount / EL_TIERS], 0, 1); if (G.ticks & 16) pset(sx, sy - 9 - hop, pal(5)); return; }
    if (d->kind == DK_GEM) { draw_gem_icon(d->amount, sx - 3, sy - 8 - hop); if (G.ticks & 16) pset(sx - 1, sy - 7 - hop, pal(5)); return; }
    int r = d->it.rarity; u16 c = rarity_col(r);
    if (r >= R_LEGEND) {
        for (int k = 0; k < 40; k++) if (((k + G.ticks / 2) % 5) != 0) pset(sx, sy - 6 - k - hop, c);
        for (int k = 0; k < 20; k++) if ((k + G.ticks / 3) % 3 == 0) { pset(sx - 1, sy - 6 - k, c); pset(sx + 1, sy - 6 - k, c); }
    }
    if (r >= R_RARE) for (int i = -4; i <= 4; i++) pset(sx + i, sy, c);
    { static const Sprite *const GS[B_COUNT] = {&SPR_g_sword, &SPR_g_bow, &SPR_g_staff, &SPR_g_shield, &SPR_g_tome, &SPR_g_helm, &SPR_g_amulet,
                                                &SPR_g_chest, &SPR_g_legs, &SPR_g_boots, &SPR_g_gloves, &SPR_g_ring, &SPR_g_cape, &SPR_g_quiver};
      const Sprite *gs = majestic_icon(&d->it); if (!gs) gs = GS[d->it.base];
      draw_item_sprite(&d->it, gs, sx - gs->w / 2, sy + 2 - hop - (gs->h - 1), g_shade[lvl], d->x & 256 ? 1 : 0, 1); }
    if (r >= R_EPIC || dist_fx(d->x, d->y, G.p.x, G.p.y) < 3 * FX) {
        Str s; item_name(&d->it, &s);
        int w = text_w(s.b);
        dim_rect(sx - w / 2 - 1, sy - 19 - hop, w + 2, 7);
        text(sx - w / 2, sy - 18 - hop, s.b, c);
    }
}
static void draw_chest(Chest *c) {
    int tx = c->x >> 8, ty = c->y >> 8;
    if (!g_seen[ty][tx]) return;
    int sx = iso_x(c->x, c->y), sy = iso_y(c->x, c->y);
    int lvl = MAX(2, light_at(tx, ty));
    blit_origin(&SPR_i_chestbox, sx, sy + 2, g_shade[c->opened ? lvl / 2 + 1 : lvl], 0, 0, 1);
}
static const char *NPC_NAME[NPC_COUNT] = {"MERCHANT", "BLACKSMITH", "JEWELER", "TRAINER", "STASH", "DUNGEON", "PORTAL"};
static void draw_npc(int k) {
    NpcPos *n = &g_npcs[k];
    i32 wx = n->tx * FX + 128, wy = n->ty * FX + 128;
    int sx = iso_x(wx, wy), sy = iso_y(wx, wy);
    if (k == NPC_ENTRANCE) return;
    if (k == NPC_PORTAL) {
        if (!G.portal_floor) return;
        for (int r = 9; r > 2; r -= 2) draw_ring(sx, sy - 18, r + ((G.ticks / 6 + r) & 1), r > 6 ? pal(21) : pal(22));
        fill_rect(sx - 1, sy - 22, 2, 18, pal(22));
    } else if (k == NPC_STASH) blit_origin(&SPR_i_chestbox, sx, sy + 2, g_shade[7], g_remap_rare, 0, 1);
    else {
        shadow(sx, sy, 8);
#ifdef HAVE_NPC_ANIM
        const CharSprite *s = NPC_ANIM[k][0][0][(G.ticks / 10 + k * 3) % NPC_ANIM_N[k][0]];
        char_origin(s, sx, sy, 7, 0, 0, 1, 0);
#else
        static const Sprite *NS[4] = {&SPR_npc_merchant, &SPR_npc_smith, &SPR_npc_jeweler, &SPR_npc_trainer};
        const Sprite *s = NS[k];
        blit_ex(s, sx - s->w / 2, sy - s->h + 3, g_shade[7], 0, 0, 1);
#endif
    }
    if (dist_fx(G.p.x, G.p.y, wx, wy) < 4 * FX) text_c(sx, sy - 48, NPC_NAME[k], RGB(255, 220, 150));
}
// Projectiles: dark outline + ground shadow so they read against any floor, short trails, and enemy shots
// in hot "danger" colours that pulse, so they are easy to dodge.
static void proj_dot(int x, int y, int r, u16 outer, u16 inner, u16 core) {
    disc(x, y, r + 1, RGB(10, 6, 8)); disc(x, y, r, outer); if (r > 1) disc(x, y, r - 1, inner); pset(x, y, core);
}
static void proj_line(int x0, int y0, int x1, int y1, u16 c, u16 head) {
    int ox = ABS(x1 - x0) >= ABS(y1 - y0) ? 0 : 1, oy = 1 - ox;   // thicken across the direction of flight
    line(x0 - ox, y0 - oy, x1 - ox, y1 - oy, RGB(10, 6, 8)); line(x0 + 2 * ox, y0 + 2 * oy, x1 + 2 * ox, y1 + 2 * oy, RGB(10, 6, 8));   // dark edges
    line(x0, y0, x1, y1, c); line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, c);   // 2 px thick
    fill_rect(x0 - 1, y0 - 1, 3, 3, head);
}
static void draw_proj(Proj *q) {
    int gx = iso_x(q->x, q->y), gy = iso_y(q->x, q->y);
    int sx = gx, sy = gy - 8;
    int tx = (q->vx * TW / 256), ty = (q->vy * TH / 256);   // travel per tick, screen pixels x256
    int enemy = q->kind == PK_MOBARROW || q->kind == PK_ORB, pulse = (G.ticks >> 2) & 1;
    gpu_shade(gx - 3, gy - 1, 7, 2, 30);   // shadow on the ground under it
    switch (q->kind) {
    case PK_ARROW: case PK_MOBARROW: case PK_BIG: case PK_EXPLO: {
        int L = isqrt(tx * tx + ty * ty), len = q->kind == PK_BIG ? 13 : 10;   // fixed length, whatever the speed
        int ex = L ? sx - tx * len / L : sx - len, ey = L ? sy - ty * len / L : sy;
        u16 c = q->kind == PK_MOBARROW ? RGB(255, 90, 40) : q->kind == PK_EXPLO ? pal(15) : RGB(240, 230, 200);
        u16 h = q->kind == PK_MOBARROW ? (pulse ? RGB(255, 255, 255) : RGB(255, 200, 80)) : RGB(255, 255, 255);
        proj_line(sx, sy, ex, ey, c, h);
        if (q->kind == PK_BIG) proj_line(sx, sy + 2, ex, ey + 2, c, h);
        if (q->kind == PK_EXPLO && (G.ticks & 2)) disc(sx, sy, 2, pal(16));
        break; }
    case PK_BOLT:
        for (int k = 2; k >= 1; k--) disc(sx - tx * k / 12, sy - ty * k / 12, 3 - k, pal(21));   // trail
        proj_dot(sx, sy, 3, pal(22), RGB(200, 240, 255), RGB(255, 255, 255));
        if (G.ticks & 1) pset(sx + rr(-4, 4), sy + rr(-4, 4), pal(21));
        break;
    case PK_FIRE:
        for (int k = 3; k >= 1; k--) disc(sx - tx * k / 10, sy - ty * k / 10, 4 - k, k > 1 ? pal(13) : pal(14));
        proj_dot(sx, sy, 4, pal(14), pal(15), pal(16));
        break;
    case PK_LIGHT: { int px = sx, py = sy; for (int k = 0; k < 5; k++) { int nx = px - tx / 10 + rr(-2, 2), ny = py - ty / 10 + rr(-2, 2); line(px, py, nx, ny, k & 1 ? pal(22) : pal(5)); line(px + 1, py, nx + 1, ny, pal(22)); px = nx; py = ny; } proj_dot(sx, sy, 2, pal(22), pal(5), pal(5)); break; }
    default: {   // enemy orb: big, pulsing, with a warning ring
        for (int k = 2; k >= 1; k--) disc(sx - tx * k / 10, sy - ty * k / 10, 3 - k, RGB(150, 40, 170));
        proj_dot(sx, sy, pulse ? 4 : 3, RGB(220, 60, 230), RGB(255, 150, 255), RGB(255, 255, 255));
        draw_ring(sx, sy, 6 + pulse, RGB(255, 80, 200));
        break; }
    }
    (void)enemy;
}
static void draw_efx(void) {
    for (int i = 0; i < MAX_EFX; i++) {
        Effect *e = &G.efx[i]; if (!e->alive) continue;
        int sx = iso_x(e->x, e->y), sy = iso_y(e->x, e->y);
        int rpx = e->radius * TW / 256;
        switch (e->kind) {
        case FX_RAIN: if (G.ticks & 2) draw_ring(sx, sy, rpx, pal(26)); break;
        case FX_METEOR: {
            draw_ring(sx, sy, rpx * (40 - e->t) / 40 + 2, pal(14));
            int h = e->t * 5; disc(sx + h / 3, sy - h, 4, pal(15)); disc(sx + h / 3, sy - h, 2, pal(16));
            for (int k = 1; k < 5; k++) pset(sx + h / 3 + k * 2, sy - h - k * 4, pal(14));
            break; }
        case FX_TRAP: draw_ring(sx, sy, 5, pal(28)); pset(sx - 2, sy - 2, pal(5)); pset(sx + 2, sy - 2, pal(5)); pset(sx, sy - 3, pal(5)); break;
        case FX_QUAKE: draw_ring(sx, sy, ((e->t0 - e->t) * e->radius / e->t0) * TW / 256, pal(11)); break;
        case FX_RING: draw_ring(sx, sy, rpx * (e->t0 - e->t) / e->t0, e->col); break;
        case FX_BOOM: { int r = rpx * (e->t0 - e->t) / e->t0; draw_ring(sx, sy, r, e->col); if (e->t > e->t0 / 2) disc(sx, sy - 4, r / 3, pal(16)); break; }
        }
    }
}

static inline int light_c(int tx, int ty) {   // light at a tile corner (top-left of tx,ty), 8.8 fixed point; unlit rock is ignored
    static const u16 INV[5] = {0, 256, 128, 85, 64};
    int a = light_at(tx - 1, ty - 1), b = light_at(tx, ty - 1), c = light_at(tx - 1, ty), d = light_at(tx, ty);
    int n = (a > 0) + (b > 0) + (c > 0) + (d > 0);
    return (a + b + c + d) * INV[n];
}
static inline int flat_light(const int lc[4]) {   // nearly uniform corners: plain blit at the rounded level, else -1
    int mn = MIN(MIN(lc[0], lc[1]), MIN(lc[2], lc[3])), mx = MAX(MAX(lc[0], lc[1]), MAX(lc[2], lc[3]));
    return (mx - mn < 80 || g_gfx_fast) ? MIN(7, (lc[0] + lc[1] + lc[2] + lc[3] + 512) >> 10) : -1;
}
static void tile_corners(int tx, int ty, int lc[4]) { lc[0] = light_c(tx, ty); lc[1] = light_c(tx + 1, ty); lc[2] = light_c(tx, ty + 1); lc[3] = light_c(tx + 1, ty + 1); }
// the way down: pulsing rings, a column of light and rising sparks, visible from across the room
static void beacon(int cx, int cy, int deep) {
    u16 c1 = deep ? RGB(255, 70, 50) : RGB(255, 220, 90), c2 = deep ? RGB(255, 160, 120) : RGB(255, 250, 200);
    int ph = G.ticks % 48;
    draw_ring(cx, cy, 6 + ph / 4, c1);
    draw_ring(cx, cy, 6 + ((ph + 24) % 48) / 4, c2);
    for (int k = 0; k < 90; k++) {   // light column
        if (((k + G.ticks / 2) % 6) < 2) continue;
        int y = cy - 4 - k; u16 c = k < 50 ? c2 : c1;
        pset(cx, y, c); if (k < 60 && (k & 1)) { pset(cx - 1, y, c1); pset(cx + 1, y, c1); }
    }
    for (int k = 0; k < 6; k++) {   // sparks
        int t = (G.ticks + k * 13) % 60, sx2 = cx + ((k * 37) % 17) - 8 + ((t / 8) & 1), sy2 = cy - t;
        pset(sx2, sy2, t < 30 ? c2 : c1);
    }
}
// edge-of-screen arrow to the stairs (and the red stairs) when they're off screen
static void tri_fill(int x0, int y0, int x1, int y1, int x2, int y2, u16 c) {
    int mnx = MIN(x0, MIN(x1, x2)), mxx = MAX(x0, MAX(x1, x2)), mny = MIN(y0, MIN(y1, y2)), mxy = MAX(y0, MAX(y1, y2));
    int area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0); if (!area) return;
    for (int y = mny; y <= mxy; y++) for (int x = mnx; x <= mxx; x++) {
        int w0 = (x1 - x) * (y2 - y) - (x2 - x) * (y1 - y), w1 = (x2 - x) * (y0 - y) - (x0 - x) * (y2 - y), w2 = (x0 - x) * (y1 - y) - (x1 - x) * (y0 - y);
        if (area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0)) pset(x, y, c);
    }
}
static void arrow_tri(int x, int y, int dx, int dy, u16 c) {   // tip at (x,y), pointing along (dx,dy) (|d| ~ 256), dark outline
    for (int o = 1; o >= 0; o--) {
        int L = 13 + o * 2, W = 7 + o * 2, tx = x + dx * o * 2 / 256, ty = y + dy * o * 2 / 256;
        int bx = tx - dx * L / 256, by = ty - dy * L / 256;
        tri_fill(tx, ty, bx - dy * W / 256, by + dx * W / 256, bx + dy * W / 256, by - dx * W / 256, o ? RGB(20, 12, 4) : c);
    }
}
static void stairs_pointer(int tx, int ty, int deep) {
    if (tx < 0) return;
    int sx = tx * TW + TW / 2 - g_camx, sy = ty * TH + TH / 2 - g_camy;
    int x0 = 12, x1 = SCR_W - 12, y0 = 22, y1 = SCR_H - 30;
    u16 c = deep ? RGB(255, 80, 60) : RGB(255, 220, 90);
    if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) {   // on screen: bobbing marker above the stairs
        int b = (G.ticks / 6) & 3; b = b == 3 ? 1 : b;
        arrow_tri(sx, sy - 18 + b, 0, 256, c);
        text_shadow(sx - text_w(deep ? "DEEPER" : "STAIRS") / 2, sy - 36 + b, deep ? "DEEPER" : "STAIRS", c);
        return;
    }
    int hx = SCR_W / 2, hy = SCR_H / 2 + 4, dx = sx - hx, dy = sy - hy;
    // clamp the ray from the hero to the screen margin
    int kx = dx ? ((dx > 0 ? x1 - hx : x0 - hx) * 256 / dx) : 1 << 20, ky = dy ? ((dy > 0 ? y1 - hy : y0 - hy) * 256 / dy) : 1 << 20;
    int k = MIN(kx, ky), ax = hx + dx * k / 256, ay = hy + dy * k / 256;
    int len = isqrt(dx * dx + dy * dy); if (!len) return;
    int ux = dx * 256 / len, uy = dy * 256 / len;
    if ((G.ticks / 10) & 1) { ax -= ux * 2 / 256; ay -= uy * 2 / 256; }
    arrow_tri(ax, ay, ux, uy, c);
    int dist = dist_fx(tx * FX + 128, ty * FX + 128, G.p.x, G.p.y) >> 8;
    Str s2; s_reset(&s2); s_add(&s2, deep ? "DEEP " : "STAIRS "); s_int(&s2, dist);
    int lx = ax - ux * 20 / 256 - text_w(s2.b) / 2, ly = ay - uy * 16 / 256 - 3;
    lx = MAX(2, MIN(SCR_W - 2 - text_w(s2.b), lx)); ly = MAX(14, MIN(SCR_H - 30, ly));
    text_shadow(lx, ly, s2.b, c);
}
static void draw_way_down(void) {
    if (G.in_town || G.show_map) return;
    int left = 0; for (int i = 0; i < MAX_MOBS; i++) left += G.mobs[i].alive;
    if (left) {   // the arrow appears once every monster on the floor is dead; until then show how many remain
        Str s2; s_reset(&s2); s_add(&s2, "FOES LEFT "); s_int(&s2, left);
        text_shadow(SCR_W - 3 - text_w(s2.b), 3 + LH, s2.b, RGB(220, 170, 150));
        return;
    }
    stairs_pointer(g_stairs_tx, g_stairs_ty, 0);
    if (g_deep_tx >= 0 && g_seen[g_deep_ty][g_deep_tx]) stairs_pointer(g_deep_tx, g_deep_ty, 1);   // the perilous way down only once it has been found
}
static void draw_tile_floor(int t, int tx, int ty, int sx, int sy, int l) {
    const HiSprite *s;
    switch (t) {
    case T_STAIRS: s = &HC_t_stairs_0; break;
    case T_DEEP: s = &HC_t_stairs_1; break;
    case T_GRASS: s = GRASS_HC[g_var[ty][tx] % 3]; break;
    case T_COBBLE: s = COBBLE_HC[g_var[ty][tx] % 3]; break;
    default: s = FLOOR_HC[g_var[ty][tx] & 3];
    }
    if (t == T_FLOOR && !G.in_town) {
        if (g_dirt[ty][tx]) s = DIRT_HC[g_var[ty][tx] % 3];
        else if (!g_gfx_fast) {   // earth spilling over the flagstones (skipped in fast mode)
            int c = earthy(tx, ty - 1) * E_N | earthy(tx, ty + 1) * E_S | earthy(tx + 1, ty) * E_E | earthy(tx - 1, ty) * E_W |
                    earthy(tx + 1, ty - 1) * E_NE | earthy(tx - 1, ty - 1) * E_NW | earthy(tx + 1, ty + 1) * E_SE | earthy(tx - 1, ty + 1) * E_SW;
            if ((c &= 15)) { int lc[4]; tile_corners(tx, ty, lc); int fl = flat_light(lc); const HiSprite *o = DIRT_HC[g_var[ty][tx] % 3];
                     if (fl >= 0) hc_blend(s, o, fl, sx, sy, c & 15); else hc_smooth(s, o, g_edge[c & 15], sx, sy, lc); goto shade; }
        }
    }
    if ((t == T_COBBLE || t == T_STAIRS) && !g_gfx_fast) {   // organic edges where grass meets the lanes (fast mode: plain edges)
        int c = grassy(tx, ty - 1) * E_N | grassy(tx, ty + 1) * E_S | grassy(tx + 1, ty) * E_E | grassy(tx - 1, ty) * E_W |
                grassy(tx + 1, ty - 1) * E_NE | grassy(tx - 1, ty - 1) * E_NW | grassy(tx + 1, ty + 1) * E_SE | grassy(tx - 1, ty + 1) * E_SW;
        if ((c &= 15)) { int lc[4]; tile_corners(tx, ty, lc); int fl = flat_light(lc);
                 if (fl >= 0) hc_blend(s, GRASS_HC[g_var[ty][tx] % 3], fl, sx, sy, c & 15); else hc_smooth(s, GRASS_HC[g_var[ty][tx] % 3], g_edge[c & 15], sx, sy, lc); goto shade; }
    }
    { int lc[4]; tile_corners(tx, ty, lc); int fl = flat_light(lc);
      if (fl >= 0) hc_rect(s, fl, sx, sy); else hc_smooth(s, 0, 0, sx, sy, lc); }
shade:;
    if (ty > 0 && (g_map[ty - 1][tx] == T_WALL || g_map[ty - 1][tx] == T_HOUSE)) {   // contact shadow at the foot of a wall
        gpu_shade(sx, sy, TW, 2, 32); gpu_shade(sx, sy + 2, TW, 3, 48);
    }
}

// see-through cut-outs fade in and out over a few frames instead of popping
static struct { i16 x, y; u8 v, touched; } g_sf[24];
static int see_fade(int tx, int ty, int want) {
    int fr = -1;
    for (int i = 0; i < 24; i++) {
        if (g_sf[i].v && g_sf[i].x == tx && g_sf[i].y == ty) {
            g_sf[i].touched = 1;
            if (want) { if (g_sf[i].v < 8) g_sf[i].v++; } else g_sf[i].v--;
            return g_sf[i].v;
        }
        if (!g_sf[i].v && fr < 0) fr = i;
    }
    if (!want || fr < 0) return 0;
    g_sf[fr].x = (i16)tx; g_sf[fr].y = (i16)ty; g_sf[fr].v = 1; g_sf[fr].touched = 1;
    return 1;
}
// warm, round light pool on the ground: additive, squared falloff, gently flickering
// warm, round light pool on the ground: additive, squared falloff. Intensity maps are cached per size.
#define GLOW_MAXW 72
#define GLOW_MAXH 54
static struct { u8 rx, ry; u8 k[GLOW_MAXH * GLOW_MAXW]; } g_glowmap[8];
static int g_nglow;
static const u8 *glow_map(int rx, int ry) {
    for (int i = 0; i < g_nglow; i++) if (g_glowmap[i].rx == rx && g_glowmap[i].ry == ry) return g_glowmap[i].k;
    int i = g_nglow < 8 ? g_nglow++ : 7;
    g_glowmap[i].rx = (u8)rx; g_glowmap[i].ry = (u8)ry;
    i32 rx2 = rx * rx, ry2 = ry * ry, lim = rx2 * ry2;
    for (int y = -ry; y <= ry; y++) for (int x = -rx; x <= rx; x++) {
        i32 dd = (i32)x * x * ry2 + (i32)y * y * rx2; int k = 0;
        if (dd < lim) { k = (int)((lim - dd) / (lim >> 4 | 1)); k = MIN(16, k * k >> 4); }
        g_glowmap[i].k[(y + ry) * GLOW_MAXW + x + rx] = (u8)k;
    }
    return g_glowmap[i].k;
}
static void glow(int cx, int cy, int rx, int ry, int ar, int ag, int ab) {
    if (g_gfx_fast && !g_door_bg_build) { rx = rx * 3 / 4; ry = ry * 3 / 4; ar += ar / 4; ag += ag / 4; ab += ab / 4; }   // fast: smaller, slightly brighter pools
    rx = MIN(rx, GLOW_MAXW / 2 - 1); ry = MIN(ry, GLOW_MAXH / 2 - 1);   // the mask buffer's size
    if (cx + rx < 0 || cx - rx >= SCR_W || cy + ry < 0 || cy - ry >= SCR_H || rx <= 0 || ry <= 0) return;
    int w = 2 * rx + 1, h = 2 * ry + 1;
    u32 a = vcache_find(g_glowmap, (u32)(rx | ry << 8));
    if (!a) {   // mask for this size: upload once
        const u8 *km = glow_map(rx, ry);
        a = vram_alloc((u32)(w * h));
        if (a) { for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) g_vram[a + y * w + x] = km[y * GLOW_MAXW + x]; vram_sync(a, (u32)(w * h)); }
        vcache_put(g_glowmap, (u32)(rx | ry << 8), a);
    }
    gpu_glow(a, cx - rx, cy - ry, w, h, ar, ag, ab);
}
static void draw_glows(void) {
    int f = (G.ticks / 5) & 3, fl = (f == 1) - (f == 3);
    int tx0 = g_camx / TW - 2, tx1 = (g_camx + SCR_W) / TW + 2, ty0 = g_camy / TH - 2, ty1 = (g_camy + SCR_H) / TH + 3;
    for (int ty = MAX(0, ty0); ty <= MIN(MAP - 1, ty1); ty++) for (int tx = MAX(0, tx0); tx <= MIN(MAP - 1, tx1); tx++) {
        int t = g_torch[ty][tx]; if (!t) continue;
        int cx = tx * TW + TW / 2 - g_camx, cy = ty * TH + TH / 2 - g_camy;
        if (t == 1) { if (g_map[ty][tx] != T_WALL) continue; glow(cx, cy + TH / 2 + 4, 26 + fl, 15 + fl, 5, 7, 1); }   // wall sconce: pool at its foot
        else if (G.in_town) glow(cx, cy + 3, 28 + fl, 20 + fl, 8, 11, 2);
        else glow(cx, cy + 2, 22 + fl, 16 + fl, 5, 7, 1);
    }
    if (!g_hide_player && (any_buff() || G.p.warcry_t > 0 || G.p.shield_t > 0)) {   // an elixir or spell at work: a pool of its colour under the hero
        Player *p = &G.p; int br = (int)((G.ticks / 9) % 6); br = br < 3 ? br : 6 - br;
        int r = 0, g = 0, b = 0;
        if (p->warcry_t > 0) { r += 8; g += 2; }
        if (p->shield_t > 0) { r += 4; g += 2; b += 9; }
        if (G.buff_t[EL_WRATH] > 0) { r += 9; g += 3; }
        if (G.buff_t[EL_PREC] > 0) { r += 7; g += 7; b += 1; }
        if (G.buff_t[EL_IRON] > 0) { r += 4; g += 5; b += 7; }
        glow(iso_x(p->x, p->y), iso_y(p->x, p->y) + 3, 26 + br, 19 + br, MIN(r, 12), MIN(g, 12), MIN(b, 12));
    }
    if (g_gfx_effects) {   // hot embers light the ground under them
        int n = 0;
        for (int i = 0; i < MAX_AMB && n < 12; i++) {
            Amb *a = &g_amb[i]; if (!a->alive || a->kind != AM_EMBER || a->t * 3 < a->t0) continue;
            glow(iso_x(a->x, a->y), iso_y(a->x, a->y), 5, 4, 8, 3, 0); n++;
        }
    }
}
// drifting fog banks in the deeper levels (Effects: high)
static void draw_fog(void) {
    if (!g_gfx_effects || G.in_town || G.floor < 3 || g_hide_player) return;
    int n = MIN(5, 1 + G.floor / 4);
    for (int i = 0; i < n; i++) {
        u32 h = hash32((u32)G.floor * 977u + (u32)i * 131u);
        int wx = (int)(h % 400) + (int)(G.ticks / (u32)(6 + i)), wy = (int)((h >> 9) % 300) + (int)(G.ticks / (u32)(17 + 3 * i));
        int sx = ((wx - g_camx) % 400 + 400) % 400 - 67, sy = ((wy - g_camy) % 300 + 300) % 300 - 30;
        int k = 3 + (int)((h >> 20) & 1);
        glow(sx, sy, 35, 24, k, k + 2, k + 3);
    }
}
// light cast by spells and explosions onto the ground
static void draw_spell_light(void) {
    if (g_gfx_effects) for (int i = 0; i < MAX_IMPACT; i++) if (g_impact[i].t) {   // flashes where blows land
        int k = g_impact[i].t, big = g_impact[i].crit;
        glow(iso_x(g_impact[i].x, g_impact[i].y), iso_y(g_impact[i].x, g_impact[i].y) - 6, big ? 16 : 10, big ? 12 : 8, k * (big ? 2 : 1), k * (big ? 2 : 1), k / 2);
    }
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *q = &G.proj[i]; if (!q->alive) continue;
        int sx = iso_x(q->x, q->y), sy = iso_y(q->x, q->y) + 4;
        switch (q->kind) {
        case PK_BOLT: glow(sx, sy, 18, 13, 2, 8, 12); break;
        case PK_LIGHT: glow(sx, sy, 20, 15, 6, 12, 14); break;
        case PK_ORB: glow(sx, sy, 16, 12, 9, 3, 12); break;
        case PK_MOBARROW: glow(sx, sy, 10, 7, 8, 4, 1); break;
        case PK_ARROW: glow(sx, sy, 8, 6, 4, 4, 3); break;
        case PK_FIRE: case PK_EXPLO: case PK_BIG: glow(sx, sy, 22, 16, 10, 9, 1); break;
        default: break;
        }
    }
    for (int i = 0; i < MAX_EFX; i++) {
        Effect *e = &G.efx[i]; if (!e->alive) continue;
        if (e->kind == FX_BOOM || e->kind == FX_METEOR) {
            int k = e->t * 16 / MAX(1, e->t0), rx = MIN(GLOW_MAXW / 2 - 1, (int)(e->radius * TW / 256) + 6), ry = MIN(GLOW_MAXH / 2 - 1, rx * 3 / 4);
            glow(iso_x(e->x, e->y), iso_y(e->x, e->y), rx & ~1, ry & ~1, 12 * k / 16, 10 * k / 16, 3 * k / 16);
        } else if (e->kind == FX_RING) {
            int k = e->t * 16 / MAX(1, e->t0);
            glow(iso_x(e->x, e->y), iso_y(e->x, e->y), 30, 22, 2 * k / 16, 8 * k / 16, 14 * k / 16);
        }
    }
}
// darken the screen edges a little: draws the eye to the hero and adds depth
static u8 g_vig[SCR_H][3];   // per row: half-width (from centre) beyond which levels 1, 2, 3 apply
static int g_vig_ready;
static int g_present_bright = 64, g_present_vig;   // applied by the GPU when the frame is shown
static void vignette(void) { }   // dark screen edges: removed (v0.33)
static void draw_world(void) {
    for (int i = 0; i < 24; i++) g_sf[i].touched = 0;
    Player *p = &G.p;
    g_camx = (G.camx * TW >> 8) - SCR_W / 2;
    g_camy = (G.camy * TH >> 8) - SCR_H / 2 - 12;
    if (G.shake) { g_camx += rr(-2, 2); g_camy += rr(-2, 2); }
    compute_light();
    // visible tile window (camera may lead/lag the hero slightly)
    int tx0 = (g_camx >= 0 ? g_camx / TW : -1) - 1, tx1 = (g_camx + SCR_W) / TW + 1;
    int ty0 = (g_camy >= 0 ? g_camy / TH : -1) - 1, ty1 = (g_camy + SCR_H + WALLH + 40) / TH + 1;
    if (G.in_town) ty1 += 8;   // shop buildings reach far above their front row: start drawing them before that row is on screen
    tx0 = MAX(tx0, G.ptx - VIEW); tx1 = MIN(tx1, G.ptx + VIEW); ty0 = MAX(ty0, G.pty - VIEW); ty1 = MIN(ty1, G.pty + VIEW);
    for (int ty = ty0; ty <= ty1; ty++) for (int tx = tx0; tx <= tx1; tx++) {
        if (!in_map(tx, ty)) continue;
        int t = g_map[ty][tx]; if (!is_floorish(t) && t != T_TREE && t != T_PROP && t != T_HOUSE) continue;   // grass under shops: seen through the see-through cut
        int l = light_at(tx, ty); if (!l) continue;
        int sx = tx * TW - g_camx, sy = ty * TH - g_camy;
        if (sy > SCR_H || sy < -TH) continue;
        draw_tile_floor(t == T_TREE || t == T_HOUSE ? T_GRASS : t == T_PROP ? g_prop_floor[ty][tx] : t, tx, ty, sx, sy, l);
    }
    draw_glows();
    draw_spell_light();
    draw_efx();
    int nb = ty1 - ty0 + 1, ne = 0;
    if (nb > (int)(sizeof g_bucket / sizeof g_bucket[0])) nb = (int)(sizeof g_bucket / sizeof g_bucket[0]);
    for (int i = 0; i < nb; i++) g_bucket[i] = -1;
#define ADD_ENT(kind, idx, x, y) do { int dd = ((y) >> 8) - ty0; if (dd >= 0 && dd < nb && ne < MAX_ENT) { \
        g_ent_id[ne] = (u16)((kind) << 8 | (idx)); g_ent_key[ne] = (y); g_ent_next[ne] = g_bucket[dd]; g_bucket[dd] = (i16)ne; ne++; } } while (0)
    if (!g_hide_player) ADD_ENT(EK_PLAYER, 0, p->x, p->y);
    for (int i = 0; i < MAX_CORPSES; i++) if (G.corpses[i].alive) ADD_ENT(EK_CORPSE, i, G.corpses[i].x, G.corpses[i].y - 64);
    for (int i = 0; i < MAX_MOBS; i++) if (G.mobs[i].alive) ADD_ENT(EK_MOB, i, G.mobs[i].x, G.mobs[i].y);
    for (int i = 0; i < MAX_DROPS; i++) if (G.drops[i].alive) ADD_ENT(EK_DROP, i, G.drops[i].x, G.drops[i].y);
    for (int i = 0; i < MAX_CHESTS; i++) if (G.chests[i].alive) ADD_ENT(EK_CHEST, i, G.chests[i].x, G.chests[i].y);
    for (int i = 0; i < MAX_PROJ; i++) if (G.proj[i].alive) ADD_ENT(EK_PROJ, i, G.proj[i].x, G.proj[i].y);
    if (G.in_town) for (int i = 0; i < NPC_COUNT; i++) ADD_ENT(EK_NPC, i, g_npcs[i].tx * FX + 128, g_npcs[i].ty * FX + 128);
    // see-through zone around the hero: anything drawn in front of (south of) the hero fades out there
    int hcx = iso_x(p->x, p->y), hcy = iso_y(p->x, p->y) - 20, hrx = 19, hry = 27, hrow = g_hide_player ? -1000 : p->y >> 8;
    for (int ty = ty0; ty < ty0 + nb; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            if (!in_map(tx, ty)) continue;
            int t = g_map[ty][tx];
            if (g_prop[ty][tx] && (t == T_PROP || is_floorish(t))) {
                int l = light_at(tx, ty); if (!l) continue;
                const HiSprite *ps = PROP_SPR[g_prop[ty][tx]];
                int jx = (int)((g_var[ty][tx] >> 2) & 7) - 3, jy = (int)((g_var[ty][tx] >> 5) & 3) - 1;
                int ox = tx * TW - g_camx + TW / 2 + jx * (t == T_PROP ? 1 : 3), oy = ty * TH - g_camy + TH * 3 / 4 + jy * (t == T_PROP ? 1 : 2);
                if (oy - ps->h > SCR_H || oy < 0) continue;
                int want = t == T_PROP && ty > hrow && ty <= hrow + 2 && ps->h > 20 && ABS(ox - hcx) < hrx + ps->w / 2 && oy - ps->h < hcy + hry;
                int see = ps->h > 20 ? see_fade(tx, ty, want) : 0;
                if (see) hc_see(ps, l, ox - ps->ox, oy - ps->oy, hcx, hcy, hrx * see / 8, hry * see / 8);
                else hc_origin(ps, l, ox, oy, (g_var[ty][tx] >> 4) & (t != T_PROP));
                if (g_prop[ty][tx] == PR_TORCH) flame(ox, oy - ps->h + 3, tx + ty);
                else if (g_prop[ty][tx] == PR_BRAZIER) { flame(ox - 3, oy - 11, tx); flame(ox + 2, oy - 12, ty); flame(ox, oy - 13, tx + 5); }
                continue;
            }
            if (t == T_STAIRS || t == T_DEEP) { beacon(tx * TW - g_camx + TW / 2, ty * TH - g_camy + TH / 2, t == T_DEEP); continue; }
            if (t != T_WALL && t != T_HOUSE && t != T_TREE) continue;
            int l = light_at(tx, ty); if (!l) continue;
            int sx = tx * TW - g_camx, sy = ty * TH - g_camy;   // top-left of the tile's ground rect
            if (t == T_HOUSE) {   // whole shop buildings are drawn once, at their front row
                for (int b = 0; b < 4; b++) if (g_bld[b].y + 3 == ty && tx == MAX(g_bld[b].x, MAX(tx0, 0))) {
                    const HiSprite *bs = BLD_SPR[b];
                    int cx = (g_bld[b].x * TW + TW * 5 / 2) - g_camx, cy = (ty - 1) * TH - g_camy;   // footprint center (from the shop, not the column we happen to draw it at)
                    int bl = light_at(g_bld[b].x + 2, g_bld[b].y + 1); if (!bl) bl = l;              // one steady light level for the whole shop
                    int bx = cx - bs->ox, by = cy - bs->oy;
                    if (by > SCR_H || by + bs->h < 0 || bx > SCR_W || bx + bs->w < 0) continue;
                    int see = see_fade(-1 - b, 0, hrow < ty + 1 && hcx > bx && hcx < bx + bs->w && hcy + hry > by && hcy - hry < by + bs->h);
                    if (see) hc_see(bs, bl, bx, by, hcx, hcy, (hrx + 4) * see / 8, (hry + 4) * see / 8);
                    else hc_key(bs, bl, bx, by, 0);
                }
                continue;
            }
            if (sy - WALLH > SCR_H || sy + TH < 0) continue;
            int see = see_fade(tx, ty, ty > hrow && ty <= hrow + 3 && sx < hcx + 8 && sx + TW > hcx - 8 && sy - WALLH < hcy + 14 && sy + TH > hcy - 14);   // only things that actually hide the hero's body
            if (t == T_TREE) {
                const HiSprite *ts = TREE_HC[g_var[ty][tx] % 3];
                int jx = (int)((g_var[ty][tx] >> 2) & 7) - 3, jy = (int)((g_var[ty][tx] >> 5) & 7) - 3;   // break up the grid
                int ox = sx + TW / 2 + jx * 2, oy = sy + TH / 2 + 2 + jy;
                if (see) hc_see(ts, l, ox - ts->ox, oy - ts->oy, hcx, hcy, (hrx + 6) * see / 8, (hry + 4) * see / 8);
                else if (g_gfx_effects && G.in_town) {   // the wind moves the crowns a little
                    int ph = (int)((G.ticks / 7 + tx * 5 + ty * 3) % 24); int sw = ph < 6 ? 0 : ph < 12 ? 1 : ph < 18 ? 2 : 1;
                    int fl2 = g_var[ty][tx] & 1;
                    hc_key_sway(ts, l, ox - (fl2 ? ts->w - 1 - ts->ox : ts->ox), oy - ts->oy, fl2, sw);
                } else hc_origin(ts, l, ox, oy, g_var[ty][tx] & 1);
                continue;
            }
            const HiSprite *ws = WALL_HC[g_var[ty][tx] & 3];
            if (see) hc_rect(DIRT_HC[g_var[ty][tx] % 3], MIN(7, l), sx, sy);   // ground under a wall we can see through
            { int lc[4]; tile_corners(tx, ty, lc);   // walls: smooth light, ragged where open, optional see-through cut — one path so nothing pops
                   int open_n = ty > 0 && g_map[ty - 1][tx] != T_WALL, open_w = tx > 0 && is_floorish(g_map[ty][tx - 1]), open_e = tx < MAP - 1 && is_floorish(g_map[ty][tx + 1]);
                   int fl = flat_light(lc), below = ty < MAP - 1 ? g_map[ty + 1][tx] : T_ROCK, face = below == T_ROCK ? 0 : below == T_WALL ? 2 : 1;
                   int fside = 0;
                   if (face == 1 && ty < MAP - 1) {
                       if (tx > 0 && g_map[ty][tx - 1] == T_WALL && (g_map[ty + 1][tx - 1] == T_WALL || g_map[ty + 1][tx - 1] == T_ROCK)) fside |= 1;
                       if (tx < MAP - 1 && g_map[ty][tx + 1] == T_WALL && (g_map[ty + 1][tx + 1] == T_WALL || g_map[ty + 1][tx + 1] == T_ROCK)) fside |= 2;
                   }
                   int rock_w = tx > 0 && g_map[ty][tx - 1] == T_ROCK, rock_e = tx < MAP - 1 && g_map[ty][tx + 1] == T_ROCK;
                   int tl = open_n && (open_w || (rock_w && ty > 0 && g_map[ty - 1][tx - 1] != T_WALL)), tr = open_n && (open_e || (rock_e && ty > 0 && g_map[ty - 1][tx + 1] != T_WALL));
                   { if (fl >= 0) lc[0] = lc[1] = lc[2] = lc[3] = fl << 8;
                          hc_wall(ws, lc, sx, sy - WALLH, tx * TW, ty, open_n, open_w, open_e, tl, tr, face, fside, hcx, hcy, see ? hrx * see / 8 : 0, hry * see / 8); } }
            if (g_torch[ty][tx] && !see) {   // sconce on the wall's front face
                int fx = sx + TW / 2, fy = sy + TH - WALLH / 2 - 2;
                int f = (G.ticks / 6 + tx) & 3;
                vline(fx, fy + 1, 5, pal(9)); pset(fx - 1, fy + 1, pal(9)); pset(fx + 1, fy + 1, pal(9));
                pset(fx, fy, pal(16)); pset(fx, fy - 1, pal(15)); pset(fx - 1, fy - 1, pal(14)); pset(fx + 1, fy - 1, pal(14));
                pset(fx + (f == 1) - (f == 3), fy - 2, pal(15));
                if (f & 1) pset(fx, fy - 3, pal(14));
            }
        }
        int ids[48], n = 0;
        for (int e = g_bucket[ty - ty0]; e >= 0 && n < 48; e = g_ent_next[e]) ids[n++] = e;
        for (int i = 1; i < n; i++) { int v = ids[i], j = i - 1; while (j >= 0 && g_ent_key[ids[j]] > g_ent_key[v]) { ids[j + 1] = ids[j]; j--; } ids[j + 1] = v; }
        for (int i = 0; i < n; i++) {
            int id = g_ent_id[ids[i]], k = id >> 8, idx = id & 0xFF;
            if (k == EK_PLAYER) draw_player();
            else if (k == EK_MOB) draw_mob(&G.mobs[idx]);
            else if (k == EK_DROP) draw_drop(&G.drops[idx]);
            else if (k == EK_CHEST) draw_chest(&G.chests[idx]);
            else if (k == EK_NPC) draw_npc(idx);
            else if (k == EK_CORPSE) draw_corpse(&G.corpses[idx]);
            else draw_proj(&G.proj[idx]);
        }
    }
    for (int i = 0; i < 24; i++) if (g_sf[i].v && !g_sf[i].touched) g_sf[i].v--;
    for (int i = 0; i < MAX_PART; i++) {
        Part *q = &G.part[i]; if (!q->alive) continue;
        int sx = iso_x(q->x, q->y), sy = iso_y(q->x, q->y) - (q->z >> 8);
        pset(sx, sy, q->col); if (q->vz < -100) pset(sx, sy - 1, q->col);
    }
    if (!g_no_amb) draw_amb();
    draw_fog();
    for (int i = 0; i < MAX_FLOAT; i++) {
        FloatText *f = &G.ft[i]; if (!f->alive || !f->s[0]) continue;
        int sx = iso_x(f->x, f->y), sy = iso_y(f->x, f->y) - (40 - f->t) / 2;
        text_c(sx, sy, f->s, f->col);
    }
    // interaction prompt
    const char *pr = 0; Str ps; s_reset(&ps); s_add(&ps, act_key(ACT_ATTACK)); s_add(&ps, ": ");
    switch (G.interact_kind) {
    case IK_STAIRS: s_add(&ps, "DESCEND"); pr = ps.b; break;
    case IK_DEEP: s_add(&ps, "PERILOUS DESCENT (+2 DEPTH)"); pr = ps.b; break;
    case IK_CHEST: s_add(&ps, "OPEN CHEST"); pr = ps.b; break;
    case IK_NPC:
        s_add(&ps, G.interact_idx == NPC_ENTRANCE ? "ENTER THE DEEP" : G.interact_idx == NPC_PORTAL ? "RETURN THROUGH PORTAL" : G.interact_idx == NPC_STASH ? "OPEN STASH" : "TALK TO ");
        if (G.interact_idx < 4) s_add(&ps, NPC_NAME[G.interact_idx]);
        pr = ps.b; break;
    }
    if (pr) text_c(SCR_W / 2, 160, pr, RGB(255, 230, 160));
}

static void draw_bigmap(void) {
    gpu_shade(0, 0, SCR_W, SCR_H - 16, 34);   // the world stays visible behind the map
    int ox = (SCR_W - MAP * 3) / 2, oy = 16;
    for (int ty = 0; ty < MAP; ty++) for (int tx = 0; tx < MAP; tx++) {
        if (!g_seen[ty][tx]) continue;
        int t = g_map[ty][tx]; u16 c;
        switch (t) {
        case T_WALL: c = RGB(130, 120, 130); break; case T_STAIRS: c = RGB(255, 230, 80); break; case T_DEEP: c = RGB(255, 60, 60); break;
        case T_FLOOR: c = g_dirt[ty][tx] ? RGB(58, 50, 44) : RGB(50, 46, 58); break; case T_GRASS: c = RGB(40, 90, 40); break; case T_COBBLE: c = RGB(90, 86, 90); break;
        case T_HOUSE: c = RGB(160, 70, 40); break; case T_TREE: c = RGB(20, 60, 20); break; case T_PROP: c = RGB(110, 90, 70); break; default: continue;
        }
        if (t == T_TREE || t == T_PROP) { if (((tx ^ ty) & 1) == 0) pset(ox + tx * 3 + 1, oy + ty * 3 + 1, t == T_TREE ? RGB(40, 110, 40) : RGB(150, 120, 90)); continue; }
        if (t == T_FLOOR || t == T_GRASS || t == T_COBBLE) { pset(ox + tx * 3 + 1, oy + ty * 3 + 1, t == T_GRASS ? RGB(80, 150, 80) : t == T_COBBLE ? RGB(160, 155, 160) : RGB(120, 110, 130)); continue; }   // open ground: a dot, so you see through it
        fill_rect(ox + tx * 3, oy + ty * 3, 3, 3, c);
    }
    if (!G.in_town) for (int k = 0; k < 2; k++) {   // big blinking markers on the way down
        int tx = k ? g_deep_tx : g_stairs_tx, ty = k ? g_deep_ty : g_stairs_ty; if (tx < 0) continue;
        int cleared = 1; for (int i = 0; i < MAX_MOBS; i++) if (G.mobs[i].alive) { cleared = 0; break; }
        if (!g_seen[ty][tx] && !cleared) continue;   // only once found (or the floor is cleared)
        int mx = ox + tx * 3 + 1, my = oy + ty * 3 + 1; u16 c = k ? RGB(255, 70, 50) : RGB(255, 220, 90);
        rect_outline(mx - 4 - ((G.ticks / 8) & 1), my - 4 - ((G.ticks / 8) & 1), 9 + 2 * ((G.ticks / 8) & 1), 9 + 2 * ((G.ticks / 8) & 1), c);
        fill_rect(mx - 1, my - 1, 3, 3, c);
        text_shadow(mx + 7, my - 2, k ? "DEEP" : "STAIRS", c);
    }
    int px = ox + G.ptx * 3 + 1, py = oy + G.pty * 3 + 1;
    if (G.ticks & 8) fill_rect(px - 1, py - 1, 3, 3, RGB(255, 255, 255));
    text_c(SCR_W / 2, 4, G.in_town ? "EMBERHOLD" : "MAP", RGB(255, 220, 150));
}

static void draw_hud(void) {
    Player *p = &G.p; Str s;
    int Y = SCR_H - 16;
    fill_rect(0, Y, SCR_W, 16, RGB(14, 11, 16)); hline(0, Y, SCR_W, RGB(90, 70, 50));
    // life / mana bars
    fill_rect(3, Y + 3, 64, 9, RGB(50, 8, 10));
    fill_rect(3, Y + 3, (int)(64 * MAX(0, p->hp >> 8) / MAX(1, p->hpmax)), 9, RGB(200, 30, 36));
    if (p->shield > 0) fill_rect(3, Y + 3, (int)MIN(64, 64 * (p->shield >> 8) / MAX(1, p->hpmax)), 2, RGB(170, 120, 255));
    s_reset(&s); s_int(&s, MAX(0, p->hp >> 8)); text_c(35, Y + 5, s.b, RGB(255, 255, 255));
    fill_rect(SCR_W - 67, Y + 3, 64, 9, RGB(8, 12, 50));
    fill_rect(SCR_W - 67, Y + 3, (int)(64 * MAX(0, p->mp >> 8) / MAX(1, p->mpmax)), 9, RGB(40, 80, 220));
    s_reset(&s); s_int(&s, p->mp >> 8); text_c(SCR_W - 35, Y + 5, s.b, RGB(255, 255, 255));
    // skill slots X Y L R
    for (int i = 0; i < 4; i++) {
        int x = 72 + i * 31, sl = p->slot[i];
        fill_rect(x, Y + 2, 29, 12, RGB(26, 22, 30)); rect_outline(x, Y + 2, 29, 12, RGB(70, 60, 70));
        { const char *k = act_key_short(ACT_SK1 + i); if (k[1]) text_small(x + 1, Y + 6, k, RGB(255, 210, 120)); else text(x + 2, Y + 5, k, RGB(255, 210, 120)); }
        if (sl >= 0 && sl < NSK) {
            int cd = p->skcd[sl], mx = skill_cd(sl);
            int ok = p->mp >= skill_cost(sl) << 8 && skill_usable(sl, 0);
            text(x + 9, Y + 5, SKILLS[sl].abbr, ok ? RGB(230, 230, 230) : RGB(110, 100, 110));
            if (cd > 0 && mx > 0) fill_rect(x + 1, Y + 12, 27 * cd / mx, 1, RGB(255, 120, 60));
        } else if (sl == SLOT_HPOT || sl == SLOT_MPOT) {   // small flask beside the key letter, count after it
            int n = sl == SLOT_HPOT ? p->hpot : p->mpot;
            blit_ex(sl == SLOT_HPOT ? &SPR_g_potion : &SPR_g_mpotion, x + 9, Y + 3, g_shade[n ? 7 : 3], 0, 0, 1);
            s_reset(&s); s_int(&s, n); text(x + 19, Y + 5, s.b, n ? RGB(230, 230, 230) : RGB(110, 100, 110));
        } else text(x + 10, Y + 5, "-", RGB(80, 70, 80));
    }
    // XP bar
    u32 step = p->level > 1 ? XP_STEP(p->level) : XP_FIRST;   // XP needed for this level (same rule as gain_xp)
    u32 prev = p->xpnext > step ? p->xpnext - step : 0, span = MAX(1u, p->xpnext - prev);
    u32 got = p->xp > prev ? MIN(p->xp - prev, span) : 0;
    fill_rect(0, SCR_H - 2, SCR_W, 2, RGB(40, 34, 14));
    fill_rect(0, SCR_H - 2, (int)(SCR_W * got / span), 2, RGB(230, 190, 50));
    // active elixirs: name, seconds left and a draining bar, along the top left (short names when three are running)
    {   int n = 0, w[EL_TYPES]; Str t[EL_TYPES];
        for (int pass = 0; pass < 2; pass++) {
            n = 0;
            for (int e = 0; e < EL_TYPES; e++) if (G.buff_t[e] > 0) {
                s_reset(&t[e]); s_add(&t[e], pass ? EL_ABBR[e] : EL_NAME[e]); s_chr(&t[e], ' '); s_int(&t[e], (G.buff_t[e] + TICKS_PER_SEC - 1) / TICKS_PER_SEC);
                w[e] = text_w(t[e].b) + 10; n += w[e] + 6;
            }
            if (n < SCR_W - 90) break;
        }
        int bx = 3;
        for (int e = 0; e < EL_TYPES; e++) if (G.buff_t[e] > 0) {
            static const u8 COL[EL_TYPES] = {15, 16, 29};
            int total = EL_SECS[G.buff_tier[e]] * TICKS_PER_SEC, blink = G.buff_t[e] < 5 * TICKS_PER_SEC && (G.ticks & 8);
            blit_ex(&SPR_g_potion, bx, 2, g_shade[7], g_remap_elix[e], 0, 1);
            if (!blink) text_shadow(bx + 10, 3, t[e].b, pal(COL[e]));
            fill_rect(bx + 10, 11, w[e] - 10, 1, RGB(40, 34, 40));
            fill_rect(bx + 10, 11, (w[e] - 10) * G.buff_t[e] / MAX(1, total), 1, pal(COL[e]));
            bx += w[e] + 6;
        }
    }
    // top-right: depth & level
    s_reset(&s); if (G.in_town) s_add(&s, "TOWN"); else { s_add(&s, "DEPTH "); s_int(&s, G.floor); }
    s_add(&s, "  LV "); s_int(&s, p->level);
    text_shadow(SCR_W - 3 - text_w(s.b), 3, s.b, RGB(255, 220, 150));
    if ((p->unspent_attr || p->unspent_skill) && (G.ticks & 32)) text_shadow(SCR_W - 3 - text_w("POINTS! (START)"), 3 + 2 * LH, "POINTS! (START)", RGB(120, 255, 120));
    { int my = any_buff() ? 3 + 13 : 3;   // messages move down while the elixir timers use the top line
      for (int i = 0; i < 3; i++) if (G.msg_t[i] > 0) text_shadow(3, my + i * LH, G.msg[i], G.msg_col[i]); }
    if (G.boss_idx >= 0 && G.mobs[G.boss_idx].alive && G.mobs[G.boss_idx].awake) {
        Mob *m = &G.mobs[G.boss_idx];
        fill_rect(53, 33, 160, 5, RGB(40, 0, 0)); fill_rect(53, 33, (int)(160 * m->hp / m->hpmax), 5, RGB(220, 40, 40));
        rect_outline(52, 32, 162, 7, RGB(150, 110, 60));
        text_c(SCR_W / 2, 42, BOSS_NAMES[m->boss - 1], RGB(255, 140, 30));
    }
    if (G.show_map) draw_bigmap();
}

// ---------- shared UI pieces ----------
static int item_lines(const Item *it, int x, int y, int maxy) {
    Str s; item_name(it, &s);
    u16 rc = rarity_col(it->rarity);
    text_shadow(x, y, s.b, rc); y += LH;
    s_reset(&s); s_add(&s, RARITY_NAME[it->rarity]); s_chr(&s, ' '); if (!majestic_icon(it)) { s_add(&s, BAND_NAME[item_band(it)]); s_chr(&s, ' '); } s_add(&s, BASE_KIND[it->base]); s_add(&s, "  ILVL "); s_int(&s, it->ilvl);
    text(x, y, s.b, RGB(150, 140, 130)); y += LH;
    if (it->dmax) { s_reset(&s); s_add(&s, "DAMAGE "); s_int(&s, it->dmin); s_chr(&s, '-'); s_int(&s, it->dmax); text(x, y, s.b, RGB(255, 255, 255)); y += LH; }
    if (it->armor) { s_reset(&s); s_add(&s, "ARMOR "); s_int(&s, it->armor); text(x, y, s.b, RGB(255, 255, 255)); y += LH; }
    for (int a = 0; a < it->naff && y < maxy; a++) {
        s_reset(&s); s_chr(&s, '+'); s_int(&s, it->av[a]); if (AFFIX_FMT[it->at[a]][0] != '%') s_chr(&s, ' '); s_add(&s, AFFIX_FMT[it->at[a]]);
        text(x, y, s.b, RGB(120, 160, 255)); y += LH;
    }
    for (int g = 0; g < it->sockets && y < maxy; g++) {
        if (it->gem[g]) {
            int id = it->gem[g] - 1; draw_gem_icon(id, x, y - 1);
            s_reset(&s); s_chr(&s, '+'); s_int(&s, GEM_VAL[id / G_TIERS][id % G_TIERS]); if (GEM_STAT[id / G_TIERS][0] != '%') s_chr(&s, ' '); s_add(&s, GEM_STAT[id / G_TIERS]);
            text(x + 9, y, s.b, RGB(200, 240, 255));
        } else { rect_outline(x + 1, y - 1, 6, 6, RGB(90, 86, 90)); text(x + 9, y, "EMPTY SOCKET", RGB(100, 96, 100)); }
        y += LH;
    }
    if (it->uniq && y < maxy) { text(x, y, UNIQUES[it->uniq - 1].desc, rc); y += LH; }
    return y;
}
static void draw_slot_box(int x, int y, int w, const Item *it, int sel, int slot_label) {
    fill_rect(x, y, w, w, RGB(22, 18, 26));
    u16 bc = it && it->used ? rarity_col(it->rarity) : RGB(60, 54, 64);
    if (it && it->used && it->rarity == R_COMMON) bc = RGB(90, 86, 90);
    rect_outline(x, y, w, w, bc);
    if (it && it->used) {
        const Sprite *ic = majestic_icon(it); if (!ic) ic = item_icon(it->base);
        int sc = w >= ic->w * 2 + 2 ? 2 : 1;
        draw_item_sprite(it, ic, x + (w - ic->w * sc) / 2, y + (w - ic->h * sc) / 2, g_shade[SHADES - 1], 0, sc);
        for (int g = 0; g < it->sockets; g++) pset(x + 2 + g * 2, y + w - 3, it->gem[g] ? pal(22) : pal(3));
    } else if (slot_label >= 0) {
        const char *n = SLOT_NAME[slot_label]; char t[4] = {n[0], n[1], 0, 0};
        text(x + w / 2 - 4, y + w / 2 - 2, t, RGB(70, 64, 74));
    }
    if (sel) rect_outline(x - 1, y - 1, w + 2, w + 2, (G.ticks & 16) ? RGB(255, 255, 255) : RGB(255, 220, 120));
}

static void draw_title(void) {
    for (int y = 0; y < SCR_H; y++) fill_rect(0, y, SCR_W, 1, RGB(y / 12, y / 16, y / 10 + 4));
    int cx = SCR_W / 2, cy = 116, f = (G.ticks / 5) & 3;
    for (int r = 46 + f; r > 0; r -= 4) disc(cx, cy, r, RGB(40 + (46 - r) * 4, 20 + (46 - r) * 3, 8 + (46 - r)));
    vline(cx, cy - 22, 8, RGB(60, 50, 40));
    fill_rect(cx - 8, cy - 14, 16, 3, RGB(90, 70, 40)); fill_rect(cx - 7, cy - 11, 14, 20, RGB(70, 50, 30));
    fill_rect(cx - 5, cy - 9, 10, 16, RGB(255, 200, 90)); fill_rect(cx - 2, cy - 4 - f, 4, 8, RGB(255, 255, 200));
    fill_rect(cx - 8, cy + 9, 16, 3, RGB(90, 70, 40));
    text2_c(cx, 40, "LANTERN DEEP", RGB(255, 200, 110));
    text_c(cx, 60, "AN ENDLESS DESCENT", RGB(180, 150, 120));
    if (G.quit_t) {   // came here through SAVE & QUIT
        if (g_sd_state == SD_BUSY || G.save_dirty) text_c(cx, 168, "SAVING...", RGB(255, 220, 120));
        else if (g_sd_state == SD_FAIL) text_c(cx, 168, "SAVE TO SD CARD FAILED", RGB(255, 90, 80));
        else text_c(cx, 168, g_sd_state == SD_IDLE ? "SAVED TO SD CARD - SAFE TO TURN OFF" : "GAME SAVED", RGB(120, 255, 120));
    } else if (g_sd_load_msg) text_c(cx, 168, g_sd_load_msg, RGB(160, 150, 140));
    if (G.ticks & 32) text_c(cx, 184, "PRESS START", RGB(255, 255, 255));
    text_c(cx, SCR_H - 10, "V0.40", RGB(90, 80, 80));
}
static void draw_dead(void) {
    dim_rect(0, 0, SCR_W, SCR_H);
    text2_c(SCR_W / 2, 70, "YOU HAVE FALLEN", RGB(220, 40, 40));
    Str s; s_reset(&s); s_add(&s, "DEPTH "); s_int(&s, G.floor); s_add(&s, "   LEVEL "); s_int(&s, G.p.level);
    text_c(SCR_W / 2, 100, s.b, RGB(230, 220, 200));
    s_reset(&s); s_add(&s, "YOU WILL LOSE "); s_int(&s, G.death_gold); s_add(&s, " GOLD"); text_c(SCR_W / 2, 110, s.b, pal(30));
    if (G.dead_t < DEATH_HOLD) { int w = 120 * G.dead_t / DEATH_HOLD; fill_rect(SCR_W / 2 - 60, 142, 120, 2, RGB(50, 20, 20)); fill_rect(SCR_W / 2 - 60, 142, w, 2, RGB(170, 40, 40)); }
    else if (G.ticks & 32) text_c(SCR_W / 2, 140, "PRESS START TO RETURN TO TOWN", RGB(255, 255, 255));
}
// scale the whole screen's brightness by k/64 (0 = black, 64 = unchanged); two fields per multiply, no per-channel splits
static void fade64(int k) { if (k < 64) g_present_bright = g_present_bright * MAX(0, k) / 64; }
// eased fade level for step t of n (smoothstep), 0..64
static int fade_ease(int t, int n) { if (t >= n) return 64; if (t <= 0) return 0; int x = t * 256 / n; return (x * x * (768 - 2 * x) >> 16) * 64 / 256; }
static void apply_fade(int f) { fade64(64 - fade_ease(f, 32)); }   // stairs / portal fade, f 0..32

static void draw_amb(void) {
    for (int i = 0; i < MAX_AMB; i++) {   // ambient particles
        Amb *a = &g_amb[i]; if (!a->alive) continue;
        int sx = iso_x(a->x, a->y), sy = iso_y(a->x, a->y) - (a->z * 5 >> 11);
        if (sx < 0 || sx >= SCR_W || sy < 0 || sy >= SCR_H - 16) continue;
        int fade = a->t < 20 ? a->t : a->t0 - a->t < 20 ? a->t0 - a->t : 20;
        switch (a->kind) {
        case AM_MOTE: { int v = 60 + fade * 4; if ((i + G.ticks / 8) & 3) pset(sx, sy, RGB(v + 30, v + 20, v)); break; }
        case AM_EMBER: { int k = a->t * 255 / a->t0; pset(sx, sy, RGB(255, 80 + k * 140 / 255, 20 + k / 4)); if (k > 160) pset(sx, sy - 1, RGB(255, 210, 120)); break; }
        case AM_FLY: { int on = ((G.ticks + i * 13) % 50) < 30; if (!on) break;
                       pset(sx, sy, RGB(230, 255, 140)); pset(sx + 1, sy, RGB(150, 200, 70)); pset(sx - 1, sy, RGB(150, 200, 70)); pset(sx, sy - 1, RGB(150, 200, 70)); pset(sx, sy + 1, RGB(150, 200, 70)); break; }
        case AM_LEAF: { u16 c = (i & 1) ? RGB(200, 170, 60) : RGB(110, 160, 60); pset(sx, sy, c); pset(sx + ((G.ticks / 6 + i) & 1), sy + 1, c); break; }
        }
    }
}
