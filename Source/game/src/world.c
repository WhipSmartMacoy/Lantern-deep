// Dungeon: seeded floor generation, tiles, line of sight, flow field, tile art.
static int g_gfx_fast;   // simpler lighting (flat per tile, no vignette): the Pocket default
#include "common.h"

#define MAP 64
enum { T_ROCK, T_FLOOR, T_WALL, T_STAIRS, T_DEEP, T_GRASS, T_COBBLE, T_HOUSE, T_TREE, T_PROP };
// props: 3D-baked objects standing on a tile. Solid ones turn the tile into T_PROP (floor drawn underneath).
enum { PR_NONE, PR_BARREL, PR_CRATE, PR_URN, PR_PILLAR, PR_LAMP, PR_WELL, PR_CART, PR_FENCE,   // solid
       PR_TORCH, PR_BRAZIER, PR_HEDGE, PR_FLOWERBED, PR_BENCH, PR_LOG, PR_STUMP,                 // solid
       PR_BONES, PR_RUBBLE, PR_MUSH, PR_CANDLES, PR_BUSH, PR_FLOWERS, PR_ROCK, PR_TALLGRASS, PR_FERN, PR_COUNT };
#define PR_LAST_SOLID PR_STUMP
static u8 g_prop[MAP][MAP];
static u8 g_dirt[MAP][MAP];   // loose-earth patches on dungeon floors (visual only)
static u8 g_prop_floor[MAP][MAP];   // floor type under a solid prop
static struct { u8 x, y; } g_bld[4];   // town shop footprints (5x4 tiles), index = NPC kind
static u8 g_map[MAP][MAP];
static u8 g_var[MAP][MAP];        // floor/wall art variant
static u8 g_seen[MAP][MAP];       // explored
static u8 g_vis[MAP][MAP];        // currently visible (== g_vis_stamp)
static u8 g_vis_stamp = 1;
static u8 g_torch[MAP][MAP];      // 0 none, 1 left face, 2 right face
static u16 g_flow[MAP][MAP];      // BFS distance to player

typedef struct { int x, y, w, h; } Room;
static Room g_rooms[24]; static int g_nrooms;
static void dungeon_props(void);
static int g_start_x, g_start_y;
static int g_stairs_tx = -1, g_stairs_ty = -1, g_deep_tx = -1, g_deep_ty = -1;   // where the way down is

static inline int in_map(int x, int y) { return x >= 0 && y >= 0 && x < MAP && y < MAP; }
static inline int tile_at(int x, int y) { return in_map(x, y) ? g_map[y][x] : T_ROCK; }
static inline int is_solid(int x, int y) { int t = tile_at(x, y); return t == T_ROCK || t == T_WALL || t == T_HOUSE || t == T_TREE || t == T_PROP; }
static inline int is_opaque(int x, int y) { int t = tile_at(x, y); return t == T_ROCK || t == T_WALL || t == T_HOUSE || t == T_TREE; }
static inline int is_floorish(int t) { return t == T_FLOOR || t == T_STAIRS || t == T_DEEP || t == T_GRASS || t == T_COBBLE; }

static void carve(int x0, int y0, int x1, int y1) {
    for (int y = MIN(y0, y1); y <= MAX(y0, y1); y++) for (int x = MIN(x0, x1); x <= MAX(x0, x1); x++)
        if (x > 0 && y > 0 && x < MAP - 1 && y < MAP - 1 && g_map[y][x] == T_ROCK) g_map[y][x] = T_FLOOR;
}
static void carve1(int x, int y) { if (x > 0 && y > 0 && x < MAP - 1 && y < MAP - 1 && g_map[y][x] == T_ROCK) g_map[y][x] = T_FLOOR; }
// winding tunnel: steps toward the target, drifting sideways now and then, 1-2 tiles wide
static void corridor(int ax, int ay, int bx, int by, int wide) {
    int x = ax, y = ay, guard = 0, w = wide;
    while ((x != bx || y != by) && guard++ < 400) {
        carve1(x, y); if (w) { carve1(x + 1, y); carve1(x, y + 1); }
        if (rn(12) == 0) w = !w;
        int dx = bx - x, dy = by - y, r = rn(ABS(dx) + ABS(dy) + 2);
        if (rn(7) == 0) { if (rn(2)) y += rn(2) ? 1 : -1; else x += rn(2) ? 1 : -1; }   // wander
        else if (r < ABS(dx)) x += dx > 0 ? 1 : -1; else if (dy) y += dy > 0 ? 1 : -1; else x += dx > 0 ? 1 : -1;
        x = MAX(1, MIN(MAP - 2, x)); y = MAX(1, MIN(MAP - 2, y));
    }
    carve1(bx, by);
}
// cave chamber: a noisy blob inside the room rectangle, with a solid core so the centre is always open
static void carve_blob(const Room *r) {
    int cx = r->x * 2 + r->w - 1, cy = r->y * 2 + r->h - 1;   // doubled coordinates
    u32 sd = (u32)rn(1 << 30);
    for (int y = r->y; y < r->y + r->h; y++) for (int x = r->x; x < r->x + r->w; x++) {
        int dx = (x * 2 - cx) * 100 / r->w, dy = (y * 2 - cy) * 100 / r->h;
        int d = dx * dx + dy * dy, wob = (int)(hash32(sd + (u32)(x * 37 + y * 101)) % 4200);
        if (d < 10000 - 1800 + wob) carve1(x, y);
    }
    for (int y = r->y + r->h / 2 - 1; y <= r->y + r->h / 2 + 1; y++) for (int x = r->x + r->w / 2 - 1; x <= r->x + r->w / 2 + 1; x++) carve1(x, y);
}

static void bfs_from(int sx, int sy, u16 out[MAP][MAP]) {
    static u16 qx[MAP * MAP], qy[MAP * MAP];
    for (int y = 0; y < MAP; y++) for (int x = 0; x < MAP; x++) out[y][x] = 0xFFFF;
    int h = 0, t = 0;
    out[sy][sx] = 0; qx[t] = (u16)sx; qy[t] = (u16)sy; t++;
    while (h < t) {
        int x = qx[h], y = qy[h]; h++;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (!in_map(nx, ny) || is_solid(nx, ny) || out[ny][nx] != 0xFFFF) continue;
            out[ny][nx] = out[y][x] + 1; qx[t] = (u16)nx; qy[t] = (u16)ny; t++;
        }
    }
}

// returns room index of farthest room from start (excluding 'exclude')
static int farthest_room(u16 dist[MAP][MAP], int exclude) {
    int best = 1, bd = -1;
    for (int i = 1; i < g_nrooms; i++) {
        if (i == exclude) continue;
        int cx = g_rooms[i].x + g_rooms[i].w / 2, cy = g_rooms[i].y + g_rooms[i].h / 2;
        int d = dist[cy][cx]; if (d != 0xFFFF && d > bd) { bd = d; best = i; }
    }
    return best;
}

static int g_stairs_room, g_deep_room;

static void world_generate(u32 seed, int floor) {
    g_rng = hash32(seed ^ (u32)floor * 0x9E3779B9u) | 1;
    memset(g_map, T_ROCK, sizeof g_map); memset(g_seen, 0, sizeof g_seen); memset(g_vis, 0, sizeof g_vis);
    memset(g_torch, 0, sizeof g_torch);
    g_nrooms = 0;
    int target = 9 + rn(5) + MIN(floor / 4, 6);
    for (int tries = 0; tries < 400 && g_nrooms < target && g_nrooms < 24; tries++) {
        Room r; r.w = rr(4, 9); r.h = rr(4, 9);
        if (floor % 5 == 0 && g_nrooms == 1) { r.w = 11; r.h = 11; }   // boss arena candidate
        r.x = rr(2, MAP - 3 - r.w); r.y = rr(2, MAP - 3 - r.h);
        int ok = 1;
        for (int i = 0; i < g_nrooms && ok; i++) {
            Room *o = &g_rooms[i];
            if (r.x < o->x + o->w + 2 && r.x + r.w + 2 > o->x && r.y < o->y + o->h + 2 && r.y + r.h + 2 > o->y) ok = 0;
        }
        if (!ok) continue;
        g_rooms[g_nrooms++] = r;
        if (floor % 5 == 0 && g_nrooms == 2) carve(r.x + 1, r.y + 1, r.x + r.w - 2, r.y + r.h - 2);   // boss arena stays roomy
        carve_blob(&r);
        // rock outcrops in bigger caves
        if (r.w >= 7 && r.h >= 7 && rn(2) == 0) {
            int px = r.x + 2 + rn(r.w - 4), py = r.y + 2 + rn(r.h - 4);
            if (ABS(px - (r.x + r.w / 2)) > 1 || ABS(py - (r.y + r.h / 2)) > 1) { g_map[py][px] = T_ROCK; if (rn(2)) g_map[py][px + 1] = T_ROCK; }
        }
    }
    // connect in sequence, plus a few loops (branching paths)
    for (int i = 1; i < g_nrooms; i++) {
        Room *a = &g_rooms[i - 1], *b = &g_rooms[i];
        corridor(a->x + a->w / 2, a->y + a->h / 2, b->x + b->w / 2, b->y + b->h / 2, rn(3) == 0);
    }
    for (int k = 0; k < 2 + rn(3); k++) {
        int i = rn(g_nrooms), j = rn(g_nrooms); if (i == j) continue;
        Room *a = &g_rooms[i], *b = &g_rooms[j];
        corridor(a->x + a->w / 2, a->y + a->h / 2, b->x + b->w / 2, b->y + b->h / 2, 0);
    }
    // one cellular smoothing step: fill notches so cave edges flow
    for (int y = 2; y < MAP - 2; y++) for (int x = 2; x < MAP - 2; x++) {
        if (g_map[y][x] != T_ROCK) continue;
        int n = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) n += g_map[y + dy][x + dx] == T_FLOOR;
        if (n >= 6) g_map[y][x] = T_FLOOR;
    }
    // loose earth in soft patches (map-scale value noise)
    memset(g_dirt, 0, sizeof g_dirt);
    { u32 sd = (u32)rn(1 << 30);
      for (int y = 1; y < MAP - 1; y++) for (int x = 1; x < MAP - 1; x++) {
        if (g_map[y][x] != T_FLOOR) continue;
        int gx = x / 4, gy = y / 4, fx = (x % 4) * 64, fy = (y % 4) * 64;
        #define HN(i, j) ((int)(hash32(sd + (u32)((i) * 7919 + (j) * 104729)) & 255))
        int a = HN(gx, gy), b = HN(gx + 1, gy), c = HN(gx, gy + 1), d = HN(gx + 1, gy + 1);
        #undef HN
        int v = (a * (256 - fx) + b * fx) * (256 - fy) / 65536 + (c * (256 - fx) + d * fx) * fy / 65536;
        if (v > 175) g_dirt[y][x] = 1;
      } }
    // walls around floors
    for (int y = 0; y < MAP; y++) for (int x = 0; x < MAP; x++) {
        if (g_map[y][x] != T_ROCK) continue;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
            if (in_map(x + dx, y + dy) && is_floorish(g_map[y + dy][x + dx])) { g_map[y][x] = T_WALL; goto next; }
    next:;
    }
    for (int y = 0; y < MAP; y++) for (int x = 0; x < MAP; x++) g_var[y][x] = (u8)(hash32(seed + (u32)(x * 131 + y * 977 + floor * 7)) & 0xFF);
    // torches on walls facing floor
    for (int y = 0; y < MAP - 1; y++) for (int x = 0; x < MAP - 1; x++) {
        if (g_map[y][x] != T_WALL || rn(100) >= 7) continue;
        if (is_floorish(g_map[y + 1][x])) g_torch[y][x] = 1;   // only walls whose face we can see
    }
    Room *s = &g_rooms[0];
    g_start_x = s->x + s->w / 2; g_start_y = s->y + s->h / 2;
    static u16 dist[MAP][MAP];
    bfs_from(g_start_x, g_start_y, dist);
    g_stairs_room = (floor % 5 == 0) ? 1 : farthest_room(dist, -1);
    Room *e = &g_rooms[g_stairs_room];
    g_map[e->y + e->h / 2][e->x + e->w / 2] = T_STAIRS;
    g_stairs_tx = e->x + e->w / 2; g_stairs_ty = e->y + e->h / 2; g_deep_tx = g_deep_ty = -1;
    g_deep_room = -1;
    if (floor >= 2 && g_nrooms > 3 && rn(100) < 10) {   // a perilous way down: about one level in ten
        g_deep_room = farthest_room(dist, g_stairs_room);
        if (g_deep_room != g_stairs_room && g_deep_room != 0) {
            Room *d = &g_rooms[g_deep_room];
            g_map[d->y + 1][d->x + 1] = T_DEEP; g_deep_tx = d->x + 1; g_deep_ty = d->y + 1;
        } else g_deep_room = -1;
    }
    dungeon_props();
    g_torch[g_stairs_ty][g_stairs_tx] = 2; if (g_deep_tx >= 0) g_torch[g_deep_ty][g_deep_tx] = 2;   // the way down glows
}


static void set_prop(int x, int y, int k) {
    g_prop[y][x] = (u8)k;
    if (k <= PR_LAST_SOLID) { g_prop_floor[y][x] = g_map[y][x]; g_map[y][x] = T_PROP; }
}
static int fl(int x, int y) { return in_map(x, y) && g_map[y][x] == T_FLOOR; }
static void dungeon_props(void) {
    memset(g_prop, 0, sizeof g_prop);
    for (int y = 2; y < MAP - 3; y++) for (int x = 2; x < MAP - 2; x++) {
        if (!fl(x, y)) continue;
        int dx = x - g_start_x, dy = y - g_start_y; if (dx * dx + dy * dy < 5) continue;
        int against_wall = g_map[y - 1][x] == T_WALL && g_map[y - 1][x - 1] == T_WALL && g_map[y - 1][x + 1] == T_WALL;
        int roomy = fl(x - 1, y) && fl(x + 1, y) && fl(x, y + 1) && fl(x - 1, y + 1) && fl(x + 1, y + 1) && fl(x, y + 2);
        if (against_wall && roomy && rn(100) < 22) { static const u8 K[6] = {PR_BARREL, PR_BARREL, PR_CRATE, PR_URN, PR_PILLAR, PR_CRATE}; set_prop(x, y, K[rn(6)]); continue; }
        int r = rn(1000);
        int at_wall = g_map[y - 1][x] == T_WALL || g_map[y][x - 1] == T_WALL || g_map[y][x + 1] == T_WALL;
        if (at_wall && r < 110) { set_prop(x, y, PR_RUBBLE); continue; }
        if (r < 22) set_prop(x, y, PR_BONES);
        else if (r < 42) set_prop(x, y, PR_RUBBLE);
        else if (r < 52) set_prop(x, y, PR_MUSH);
        else if (r < 60 && g_map[y - 1][x] == T_WALL) { set_prop(x, y, PR_CANDLES); g_torch[y][x] = 2; }   // candles light their corner
    }
}

// line of sight from tile (px,py) — marks g_vis/g_seen
// The hero-select chamber: a small torch-lit cave hall around (cx, cy), open to the south (below the screen).
static void world_chamber(int cx, int cy) {
    memset(g_map, T_ROCK, sizeof g_map); memset(g_torch, 0, sizeof g_torch); memset(g_prop, 0, sizeof g_prop);
    memset(g_dirt, 0, sizeof g_dirt); memset(g_seen, 1, sizeof g_seen);
    for (int y = cy - 6; y <= cy + 9; y++) for (int x = cx - 9; x <= cx + 9; x++) {
        int dx = x - cx, dy = y - cy;
        int hw = dy < -3 ? -1 : dy == -3 ? 2 : dy <= 2 ? 3 : 3 + (dy - 2) / 2;   // a narrow hall that opens up toward the viewer
        if (dy >= -2 && dy <= 2 && (hash32((u32)(x * 31 + y * 17) ^ 0x5EEDu) % 3) == 0) hw += 1;   // ragged side walls
        if (ABS(dx) <= hw) g_map[y][x] = T_FLOOR;
    }
    for (int y = 0; y < MAP; y++) for (int x = 0; x < MAP; x++) {
        if (g_map[y][x] != T_ROCK) continue;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
            if (in_map(x + dx, y + dy) && is_floorish(g_map[y + dy][x + dx])) { g_map[y][x] = T_WALL; goto next_w; }
    next_w:;
    }
    for (int y = 0; y < MAP; y++) for (int x = 0; x < MAP; x++) {
        g_var[y][x] = (u8)(hash32((u32)(x * 131 + y * 977) ^ 0xC4A3u) & 0xFF);
        if (g_map[y][x] == T_FLOOR && (hash32((u32)(x * 7 + y * 13)) & 7) < 3 && ABS(x - cx) > 1) g_dirt[y][x] = 1;
    }
    // sconces on the back wall and the side walls, braziers flanking the heroes, clutter along the edges
    for (int y = cy - 6; y < cy + 3; y++) for (int x = cx - 6; x <= cx + 6; x++)
        if (g_map[y][x] == T_WALL && is_floorish(g_map[y + 1][x]) && (x == cx - 2 || x == cx + 2)) g_torch[y][x] = 1;
    set_prop(cx - 3, cy + 1, PR_BRAZIER); g_torch[cy + 1][cx - 3] = 2;
    set_prop(cx + 3, cy + 1, PR_BRAZIER); g_torch[cy + 1][cx + 3] = 2;
    set_prop(cx - 3, cy - 2, PR_PILLAR); set_prop(cx + 3, cy - 2, PR_PILLAR);
    set_prop(cx - 1, cy - 3, PR_CANDLES); g_torch[cy - 3][cx - 1] = 2;
    set_prop(cx + 1, cy - 3, PR_BONES);
    set_prop(cx - 4, cy + 4, PR_BARREL); set_prop(cx + 4, cy + 4, PR_URN); set_prop(cx + 5, cy + 5, PR_CRATE);
    set_prop(cx - 3, cy + 3, PR_RUBBLE); set_prop(cx + 3, cy + 3, PR_RUBBLE); set_prop(cx - 2, cy + 5, PR_BONES); set_prop(cx + 2, cy - 1, PR_MUSH);
}
static void world_update_vis(int px, int py, int radius) {
    g_vis_stamp++; if (g_vis_stamp == 0) { memset(g_vis, 0, sizeof g_vis); g_vis_stamp = 1; }
    for (int dy = -radius; dy <= radius; dy++) for (int dx = -radius; dx <= radius; dx++) {
        if (ABS(dx) != radius && ABS(dy) != radius) continue;
        int x0 = px, y0 = py, x1 = px + dx, y1 = py + dy;
        int ddx = ABS(x1 - x0), sx = x0 < x1 ? 1 : -1, ddy = -ABS(y1 - y0), sy = y0 < y1 ? 1 : -1, e = ddx + ddy;
        for (;;) {
            if (!in_map(x0, y0)) break;
            int ex = x0 - px, ey = y0 - py; if (ex * ex + ey * ey > radius * radius + radius) break;
            g_vis[y0][x0] = g_vis_stamp; g_seen[y0][x0] = 1;
            if (is_opaque(x0, y0) && !(x0 == px && y0 == py)) break;
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * e; if (e2 >= ddy) { e += ddy; x0 += sx; } if (e2 <= ddx) { e += ddx; y0 += sy; }
        }
    }
    // reveal walls bordering visible floor (prevents gaps in corners)
    for (int y = MAX(1, py - radius - 1); y < MIN(MAP - 1, py + radius + 2); y++)
        for (int x = MAX(1, px - radius - 1); x < MIN(MAP - 1, px + radius + 2); x++) {
            if (g_vis[y][x] != g_vis_stamp || is_solid(x, y)) continue;
            for (int d = 0; d < 8; d++) {
                static const int ox[8] = {1, -1, 0, 0, 1, 1, -1, -1}, oy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
                int nx = x + ox[d], ny = y + oy[d];
                if (is_solid(nx, ny)) { g_vis[ny][nx] = g_vis_stamp; g_seen[ny][nx] = 1; }
            }
        }
}
static inline int tile_visible(int x, int y) { return in_map(x, y) && g_vis[y][x] == g_vis_stamp; }

// ---------- environment art: full-color (RGB565) sprites, pre-shaded per light level at boot ----------
// Camera: looking north, tilted ~51 degrees down (Diablo 4 style). One tile = TW x TH pixels on the ground;
// a wall block shows its top (TH rows) above a WALLH-pixel front face.
#define TW 28
#define TH 22
#define WALLH 25
#define BLKH (TH + WALLH)
// VRAM address of a full-colour environment image (uploaded on first use; may be recycled, so never stored)
static u32 hc_addr(const HiSprite *h) { return vram_image(h->px, h->w * h->h); }
static const u16 *g_hc[HC_COUNT][SHADES];
static u8 g_hcspan[HC_TOTAL_ROWS][2];   // per sprite row: first and last+1 opaque column
static u32 g_hcrow0[HC_COUNT];                           // index of each sprite's first row in g_hcspan
// organic boundaries: masks of where a neighbouring grass tile spills over a path tile (index = 8 neighbour bits)
static u8 g_edge[256][TW * TH];
enum { E_N = 1, E_S = 2, E_E = 4, E_W = 8, E_NE = 16, E_NW = 32, E_SE = 64, E_SW = 128 };
static int edge_noise(int x, int y) {   // tileable value noise, 0..255 (period = one tile, so neighbours line up)
    int cx = x * 4 / TW, cy = y * 4 / TH, fx = (x * 4 % TW) * 256 / TW, fy = (y * 4 % TH) * 256 / TH;
    #define NV(i, j) ((int)(hash32((u32)(((i) & 3) * 7 + ((j) & 3) * 131 + 99)) & 255))
    int a = NV(cx, cy), b = NV(cx + 1, cy), c = NV(cx, cy + 1), d = NV(cx + 1, cy + 1);
    #undef NV
    int top = a + ((b - a) * fx >> 8), bot = c + ((d - c) * fx >> 8);
    return top + ((bot - top) * fy >> 8);
}
static void edges_init(void) {
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) {
        int dn = y * TW / TH, ds = (TH - 1 - y) * TW / TH, dw = x, de = TW - 1 - x;
        int dist[8] = {dn, ds, de, dw, MAX(dn, de) + MIN(dn, de) / 2, MAX(dn, dw) + MIN(dn, dw) / 2, MAX(ds, de) + MIN(ds, de) / 2, MAX(ds, dw) + MIN(ds, dw) / 2};
        int n = edge_noise(x, y), lim = 2 + n * 9 / 255;   // 2..11 px of grass creeping in
        for (int c = 0; c < 256; c++) {
            u8 v = 0;
            for (int k = 0; k < 8 && !v; k++) if ((c >> k) & 1 && dist[k] < lim) v = 1;
            g_edge[c][y * TW + x] = v;
        }
    }
}
static u8 g_edgerow[256][TH];   // per edge mask row: 0 = no grass, 1 = all grass, 2 = mixed
static void rag_init(void);
static void char_luts_init(void);
static void tiles_init(void) {
    edges_init(); rag_init(); char_luts_init();
    for (int c = 0; c < 256; c++) for (int y = 0; y < TH; y++) {
        int n = 0; for (int x = 0; x < TW; x++) n += g_edge[c][y * TW + x];
        g_edgerow[c][y] = (u8)(n == 0 ? 0 : n == TW ? 1 : 2);
    }
    { u32 r = 0;
      for (int i = 0; i < HC_COUNT; i++) {
        const HiSprite *h = HC_ALL[i]; g_hcrow0[i] = r;
        for (int y = 0; y < h->h; y++, r++) {
            int l = h->w, rr = 0;
            for (int x = 0; x < h->w; x++) if (h->px[y * h->w + x]) { if (x < l) l = x; rr = x + 1; }
            if (rr <= l) { l = 0; rr = 0; }
            g_hcspan[r][0] = (u8)l; g_hcspan[r][1] = (u8)rr;
        }
      } }
}
static int isqrt_w(i32 v) { if (v <= 0) return 0; i32 r = 0, bb = 1 << 30; while (bb > v) bb >>= 2; while (bb) { if (v >= r + bb) { v -= r + bb; r = (r >> 1) + bb; } else r >>= 1; bb >>= 2; } return r; }
// wall block with ragged silhouette: top edge (when nothing stands behind it) and sides (when open floor is beside it)
static u8 rag_n(int v) { return (u8)(hash32((u32)v * 2654435761u) & 255); }
static int rag_smooth(int p, int span, int amp, int salt) {   // smooth 1D noise (smoothstep between knots), 0..amp
    int i = p / span, f = (p % span) * 256 / span;
    f = f * f * (768 - 2 * f) >> 16;
    int a = rag_n(i * 31 + salt), b = rag_n((i + 1) * 31 + salt);
    return (a + ((b - a) * f >> 8)) * amp >> 8;
}
// rounded, irregular wall block. top/left/right: open sides get a soft wavy silhouette; tl/tr: round the corner;
// face: draw the front face (with a wavy edge where the dark top meets it); erx > 0: see-through cut at (ecx, ecy)
#define WALL_R 13
// precomputed silhouettes (indexed by world pixel column / row) so the per-pixel tests are just table reads
static u8 g_rag_top[MAP * TW], g_rag_bot[MAP * TW], g_rag_seam[MAP * TW], g_rag_l[MAP * BLKH], g_rag_r[MAP * BLKH];
static void rag_init(void) {
    for (int i = 0; i < MAP * TW; i++) { g_rag_top[i] = (u8)rag_smooth(i, 11, 8, 5); g_rag_bot[i] = (u8)rag_smooth(i, 10, 5, 77); g_rag_seam[i] = (u8)rag_smooth(i, 7, 7, 41); }
    for (int i = 0; i < MAP * BLKH; i++) { g_rag_l[i] = (u8)rag_smooth(i, 9, 6, 11); g_rag_r[i] = (u8)rag_smooth(i, 9, 6, 23); }
}
// --- environment blits (all through the GPU) ---
static inline int lvl_avg(const int lc[4]) { return MIN(7, (lc[0] + lc[1] + lc[2] + lc[3] + 512) >> 10); }
static inline void hc_blit_rows(const HiSprite *s, int l, int x, int y, int r0, int r1, int a, int b, int flags) {   // rows r0..r1-1, columns a..b-1 (screen)
    if (r1 <= r0 || b <= a) return;
    gpu_blit(hc_addr(s) + (u32)(r0 * s->w + (a - x)), s->w, a, y + r0, b - a, r1 - r0, flags | (l < 7 ? GB_LUT : 0), LUT_ID(0, l), 0, 256, 256);
}
// smooth light: corner levels lc (8.8: TL, TR, BL, BR) of a w x h image -> A + B*x + C*y + D*x*y (16.16, pixel centres)
static void grad_coefs(const int lc[4], int w, int h, i32 *A, i32 *B, i32 *C, i32 *D) {
    i32 a = lc[0], b = lc[1], c = lc[2], d = lc[3], e = a - b - c + d, wh = (i32)w * h;
    *A = (a << 8) + ((b - a) << 8) / (2 * w) + ((c - a) << 8) / (2 * h) + (e << 8) / (4 * wh);
    *B = ((b - a) << 8) / w + (e << 8) / (2 * wh);
    *C = ((c - a) << 8) / h + (e << 8) / (2 * wh);
    *D = (e << 8) / wh;
}
// rows r0..r1-1, screen columns a..b-1 of an image at (x, y), lit smoothly from its corner levels
static void grad_rows(u32 addr, int w, int h, const int lc[4], int x, int y, int r0, int r1, int a, int b, int flags) {
    if (r1 <= r0 || b <= a || !addr) return;
    if (!g_gpu_grad) { int l = lvl_avg(lc); gpu_blit(addr + (u32)(r0 * w + (a - x)), w, a, y + r0, b - a, r1 - r0, flags | (l < 7 ? GB_LUT : 0), LUT_ID(0, l), 0, 256, 256); return; }
    i32 A, B, C, D; grad_coefs(lc, w, h, &A, &B, &C, &D);
    i32 ox = a - x, oy = r0;   // the part drawn starts here inside the image
    gpu_blit_grad(addr + (u32)(r0 * w + ox), w, a, y + r0, b - a, r1 - r0, flags, 0, A + B * ox + C * oy + D * ox * oy, B + D * oy, C + D * ox, D);
}
// ground tile with an edge of another ground blended in (dirt spilling over flagstones, grass along lanes):
// built once per combination on the CPU and kept in VRAM
#define BAKE_N 256
static struct { const HiSprite *b, *o; const u8 *m; u32 gen; } g_bk[BAKE_N];
static u16 g_bkpx[BAKE_N][TW * TH];
static u32 g_bkgen, g_bk_miss, g_bk_up;
static u32 bake_addr(const HiSprite *base, const HiSprite *over, const u8 *m) {
    u32 h = hash32((u32)(uintptr_t)base * 31u ^ (u32)(uintptr_t)over * 17u ^ (u32)(uintptr_t)m * 2654435761u) % BAKE_N, slot = h;
    for (int k = 0; k < 8; k++) {
        u32 i = (h + (u32)k) % BAKE_N;
        if (g_bk[i].b == base && g_bk[i].o == over && g_bk[i].m == m) {
            u32 a = vcache_find(g_bkpx[i], g_bk[i].gen);
            if (!a) { g_bk_up++; a = vram_alloc(TW * TH); vram_write(a, g_bkpx[i], TW * TH); vcache_put(g_bkpx[i], g_bk[i].gen, a); }
            return a;
        }
        if (!g_bk[i].b) { slot = i; break; }
        if (g_bk[i].gen < g_bk[slot].gen) slot = i;   // else replace the oldest in the window
    }
    g_bk_miss++; g_bk[slot].b = base; g_bk[slot].o = over; g_bk[slot].m = m; g_bk[slot].gen = ++g_bkgen;
    for (int k = 0; k < TW * TH; k++) g_bkpx[slot][k] = m[k] ? over->px[k] : base->px[k];
    u32 a = vram_alloc(TW * TH); vram_write(a, g_bkpx[slot], TW * TH); vcache_put(g_bkpx[slot], g_bk[slot].gen, a);
    return a;
}
// SEE cut-out for an ellipse (cx, cy, rx, ry): row half-widths for the GPU
static void gpu_see_ellipse(int cx, int cy, int rx, int ry) {
    u16 hw[256]; int n = 0, top = cy - ry + 1;
    for (int dy = -ry + 1; dy < ry && n < 254; dy++) { i32 q = 256 * 256 - (dy * 256 / ry) * (dy * 256 / ry); hw[n++] = (u16)MAX(1, rx * isqrt_w(q) >> 8); }
    gpu_see(cx, top, hw, n);
}
// wall block: straight silhouette with rounded top corners; rows grouped into as few blits as possible
static void hc_wall(const HiSprite *s, const int lc[4], int x, int y, int gx, int gy, int top, int left, int right, int tl, int tr, int face, int fside, int ecx, int ecy, int erx, int ery) {
    (void)gx; (void)gy; (void)left; (void)right; (void)fside;
    int w = s->w, h = s->h, flags = 0;
    int r0 = top ? 3 : 0, r1 = face == 1 ? h : h - WALLH;
    int sl = (lc[0] + lc[2]) / 2, sr = (lc[1] + lc[3]) / 2, wl[4] = {sl, sr, sl, sr};   // walls: light changes along the wall only
    u32 addr = hc_addr(s);
    if (face == 0) r1 = MIN(r1, TH - 3);
    if (erx > 0 && ecy + ery > y + r0 && ecy - ery < y + r1 && ecx + erx > x && ecx - erx < x + w) { gpu_see_ellipse(ecx, ecy, erx, ery); flags |= GB_SEE; }
    int ga = -1, gb = -1, gr = r0;
    for (int r = r0; r <= r1; r++) {
        int a = x, b = x + w;
        if (r < r1 && (tl || tr) && r < WALL_R) { int cut = WALL_R - r; cut = cut * cut / WALL_R; if (tl) a += cut; if (tr) b -= cut; }
        if (r == r1 || a != ga || b != gb) { grad_rows(addr, w, h, wl, x, y, gr, r, ga, gb, flags); ga = a; gb = b; gr = r; }
    }
}
// solid rectangle (floors, walls)
static void hc_rect(const HiSprite *s, int l, int x, int y) { hc_blit_rows(s, l, x, y, 0, s->h, x, x + s->w, 0); }
// sprite with transparency (props, trees, shops): drawn in bands of rows trimmed to their opaque columns, so the
// GPU does not read the empty space around the shape. sway: top bands lean sideways by up to `sway` pixels (trees in the wind).
static void hc_key_sway(const HiSprite *s, int l, int x, int y, int flip, int sway) {
    u32 addr = hc_addr(s); int w = s->w, h = s->h, fl = GB_KEY | (flip ? GB_FLIPX : 0) | (l < 7 ? GB_LUT : 0);
    const u8 (*sp)[2] = &g_hcspan[g_hcrow0[s->id]];
    for (int r0 = 0; r0 < h;) {
        int a = 255, b = 0, r1 = r0;
        while (r1 < h && r1 - r0 < 8) {   // grow the band while it stays tight
            int na = sp[r1][1] > sp[r1][0] ? MIN(a, sp[r1][0]) : a, nb = MAX(b, sp[r1][1]);
            if (r1 > r0 && b > a && (nb - na) - (b - a) > 6) break;
            a = na; b = nb; r1++;
        }
        if (b > a) {
            int da = flip ? w - b : a, db = flip ? w - a : b;   // destination columns of the opaque part
            int dx = sway ? sway * (h - r0) / h : 0;
            u32 src = addr + (u32)(r0 * w) + (u32)(flip ? w - db : da);
            gpu_blit(src, w, x + da + dx, y + r0, db - da, r1 - r0, fl, LUT_ID(0, l), 0, 256, 256);
        }
        r0 = r1;
    }
}
static void hc_key(const HiSprite *s, int l, int x, int y, int flip) { hc_key_sway(s, l, x, y, flip, 0); }
static inline void hc_origin(const HiSprite *s, int l, int sx, int sy, int flip) { hc_key(s, l, sx - (flip ? s->w - 1 - s->ox : s->ox), sy - s->oy, flip); }
// per-corner light, interpolated by the GPU across the tile; optional blended edge from another ground (mask mk)
static void hc_smooth(const HiSprite *base, const HiSprite *over, const u8 *mk, int x, int y, const int lc[4]) {
    u32 a = over ? bake_addr(base, over, mk) : hc_addr(base);
    grad_rows(a, base->w, base->h, lc, x, y, 0, base->h, x, x + base->w, 0);
}
static void hc_blend(const HiSprite *base, const HiSprite *over, int l, int x, int y, int combo) {
    u32 a = bake_addr(base, over, g_edge[combo]);
    gpu_blit(a, base->w, x, y, base->w, base->h, l < 7 ? GB_LUT : 0, LUT_ID(0, l), 0, 256, 256);
}
// pixels inside an ellipse (cx, cy, rx, ry) are cut away (dithered rim) so the hero shows through
static void hc_see(const HiSprite *s, int l, int x, int y, int cx, int cy, int rx, int ry) {
    gpu_see_ellipse(cx, cy, rx, ry);
    gpu_blit(hc_addr(s), s->w, x, y, s->w, s->h, GB_KEY | GB_SEE | (l < 7 ? GB_LUT : 0), LUT_ID(0, l), 0, 256, 256);
}

// ---------- town ----------
enum { NPC_MERCHANT, NPC_SMITH, NPC_JEWELER, NPC_TRAINER, NPC_STASH, NPC_ENTRANCE, NPC_PORTAL, NPC_COUNT };
typedef struct { int kind, tx, ty; } NpcPos;
static NpcPos g_npcs[NPC_COUNT];
static int g_town_spawn_x, g_town_spawn_y;

static void fill(int x0, int y0, int x1, int y1, int t) {
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (in_map(x, y)) g_map[y][x] = (u8)t;
}
// organic shapes for the town: noisy discs and curved paths stamped onto the grid
static void stamp(int cx16, int cy16, int r16, int t) {   // 1/16-tile units
    int r = r16 / 16 + 1;
    for (int y = cy16 / 16 - r; y <= cy16 / 16 + r; y++) for (int x = cx16 / 16 - r; x <= cx16 / 16 + r; x++) {
        if (!in_map(x, y)) continue;
        int dx = x * 16 + 8 - cx16, dy = y * 16 + 8 - cy16;
        if (dx * dx + dy * dy <= r16 * r16 && (g_map[y][x] == T_GRASS || g_map[y][x] == T_TREE)) g_map[y][x] = (u8)t;
    }
}
static void path(int x0, int y0, int x1, int y1, int bend) {   // quadratic curve with a sideways bend
    int mx = (x0 + x1) * 8, my = (y0 + y1) * 8, dx = x1 - x0, dy = y1 - y0;
    int cx = mx - dy * bend * 16 / 8, cy = my + dx * bend * 16 / 8;   // control point (1/16 units)
    int len = ABS(dx) + ABS(dy), n = len * 4 + 4;
    for (int i = 0; i <= n; i++) {
        int t = i * 256 / n, u = 256 - t;
        int px = (u * u * (x0 * 16 + 8) + 2 * u * t * cx + t * t * (x1 * 16 + 8)) >> 16;
        int py = (u * u * (y0 * 16 + 8) + 2 * u * t * cy + t * t * (y1 * 16 + 8)) >> 16;
        stamp(px, py, 11 + (int)(hash32((u32)i * 77u + (u32)x0) % 7), T_COBBLE);
    }
}
static int near_type(int x, int y, int t, int r) {
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) if (in_map(x + dx, y + dy) && g_map[y + dy][x + dx] == t) return 1;
    return 0;
}
static void world_town(u32 seed) {
    g_rng = hash32(seed ^ 0x70777Eu) | 1;
    memset(g_map, T_ROCK, sizeof g_map); memset(g_torch, 0, sizeof g_torch); memset(g_prop, 0, sizeof g_prop);
    // a clearing in the forest: noisy ellipse of grass, trees all around
    for (int y = 0; y < MAP; y++) for (int x = 0; x < MAP; x++) {
        int dx = (x - 32) * 100 / 15, dy = (y - 27) * 100 / 13;
        int d = dx * dx + dy * dy, wob = (int)(hash32((u32)(x * 7 + y * 131) ^ 0xBEEF) % 2600);
        g_map[y][x] = d < 10000 - 1300 + wob ? T_GRASS : d < 52000 ? T_TREE : T_ROCK;
    }
    // dungeon entrance: stone arch with stairs, north of the square
    fill(29, 16, 35, 17, T_WALL); fill(29, 18, 30, 19, T_WALL); fill(34, 18, 35, 19, T_WALL); fill(31, 18, 33, 19, T_COBBLE);
    g_map[19][32] = T_STAIRS; g_torch[17][30] = 1; g_torch[17][34] = 1; g_stairs_tx = 32; g_stairs_ty = 19; g_deep_tx = g_deep_ty = -1;
    // the square
    for (int y = 22; y <= 34; y++) for (int x = 25; x <= 39; x++) {
        int dx = (x - 32) * 100 / 4, dy = (y - 28) * 100 / 3;
        if (dx * dx + dy * dy < 10000 + (int)(hash32((u32)(x * 31 + y * 17)) % 3500) - 1500) g_map[y][x] = T_COBBLE;
    }
    // shops close around the square (NPC stands at the front, south side)
    static const int H[4][2] = {{20, 20}, {40, 20}, {20, 30}, {40, 30}};
    for (int i = 0; i < 4; i++) { g_bld[i].x = (u8)H[i][0]; g_bld[i].y = (u8)H[i][1]; }
    for (int i = 0; i < 4; i++) {
        fill(H[i][0] - 1, H[i][1] - 1, H[i][0] + 5, H[i][1] + 5, T_GRASS);   // clear trees around the shop
        fill(H[i][0], H[i][1], H[i][0] + 4, H[i][1] + 3, T_HOUSE);
        g_npcs[i].kind = i; g_npcs[i].tx = H[i][0] + 2; g_npcs[i].ty = H[i][1] + 5;
        g_torch[H[i][1] + 3][H[i][0]] = 1; g_torch[H[i][1] + 3][H[i][0] + 4] = 1;
    }
    for (int i = 0; i < 4; i++) {   // winding lanes from the square to each shop front
        int nx = g_npcs[i].tx, ny = g_npcs[i].ty;
        path(32, 28, nx, ny, (i & 1) ? 2 : -2);
        stamp(nx * 16 + 8, ny * 16 + 8, 22, T_COBBLE); stamp(nx * 16 + 8, (ny - 1) * 16 + 8, 18, T_COBBLE);
    }
    path(32, 26, 32, 20, 1);    // north to the dungeon
    path(32, 31, 31, 44, -2);   // south lane into the woods
    path(26, 28, 17, 27, 2); path(38, 28, 47, 29, -2);   // trails west and east
    g_npcs[NPC_STASH] = (NpcPos){NPC_STASH, 29, 26};
    g_npcs[NPC_ENTRANCE] = (NpcPos){NPC_ENTRANCE, 32, 19};
    g_npcs[NPC_PORTAL] = (NpcPos){NPC_PORTAL, 35, 26};
    // braziers on the square, torches along the lanes
    set_prop(28, 30, PR_BRAZIER); set_prop(36, 30, PR_BRAZIER);
    g_torch[30][28] = 2; g_torch[30][36] = 2;
    set_prop(35, 31, PR_WELL); set_prop(25, 24, PR_CART);
    int tc = 0;
    for (int y = 14; y < 46; y++) for (int x = 14; x < 52; x++) {
        if (g_map[y][x] != T_GRASS || g_prop[y][x]) continue;
        if (!near_type(x, y, T_COBBLE, 1) || near_type(x, y, T_HOUSE, 1) || near_type(x, y, T_PROP, 2)) continue;
        if ((hash32((u32)(x * 53 + y * 97)) % 100) < 22 && ++tc) { set_prop(x, y, PR_TORCH); g_torch[y][x] = 2; }
    }
    // shop dressing: flower beds and barrels at the fronts, hedges along the sides, benches by the square
    for (int i = 0; i < 4; i++) {
        int x0 = H[i][0], y0 = H[i][1];
        if (g_map[y0 + 4][x0] == T_GRASS) set_prop(x0, y0 + 4, PR_FLOWERBED);
        if (g_map[y0 + 4][x0 + 4] == T_GRASS) set_prop(x0 + 4, y0 + 4, PR_BARREL);
        for (int y = y0; y < y0 + 3; y++) { if (g_map[y][x0 - 1] == T_GRASS) set_prop(x0 - 1, y, PR_HEDGE); if (g_map[y][x0 + 5] == T_GRASS) set_prop(x0 + 5, y, PR_HEDGE); }
        if (g_map[y0 + 3][x0 + 5] == T_GRASS) set_prop(x0 + 5, y0 + 3, PR_CRATE);
    }
    if (g_map[24][31] == T_GRASS) set_prop(31, 24, PR_BENCH);
    // foliage everywhere else: trees, tall grass, ferns, bushes, flowers, rocks, logs
    for (int y = 12; y < 50; y++) for (int x = 12; x < 54; x++) {
        if (g_map[y][x] != T_GRASS || g_prop[y][x]) continue;
        int nearpath = near_type(x, y, T_COBBLE, 1) || near_type(x, y, T_HOUSE, 1) || near_type(x, y, T_STAIRS, 2);
        int r = rn(1000);
        if (!nearpath && r < 90) { g_map[y][x] = T_TREE; continue; }
        r = rn(1000);
        if (r < 120) set_prop(x, y, PR_TALLGRASS);
        else if (r < 180) set_prop(x, y, PR_FERN);
        else if (r < 250) set_prop(x, y, PR_FLOWERS);
        else if (r < 300 && !nearpath) set_prop(x, y, PR_BUSH);
        else if (r < 315) set_prop(x, y, PR_ROCK);
        else if (r < 323 && !nearpath) set_prop(x, y, PR_LOG);
        else if (r < 331 && !nearpath) set_prop(x, y, PR_STUMP);
    }
    for (int y = 0; y < MAP; y++) for (int x = 0; x < MAP; x++) g_var[y][x] = (u8)(hash32(seed + (u32)(x * 131 + y * 977)) & 0xFF);
    memset(g_seen, 1, sizeof g_seen);
    g_town_spawn_x = 32; g_town_spawn_y = 29;
    g_nrooms = 0;
}
