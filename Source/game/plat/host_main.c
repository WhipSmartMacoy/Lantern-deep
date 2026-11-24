// Host test harness: scripted screens + combat soak test, frame dumps (PPM).
#define HOST_BUILD 1
#include "../src/game.c"
#include <stdio.h>
#include <stdlib.h>
static u16 fb[SCR_W * SCR_H];
static void dump(const char *name) {
    char fn[128]; sprintf(fn, "build/s_%s.ppm", name);
    game_render(fb);
    FILE *f = fopen(fn, "wb"); fprintf(f, "P6 %d %d 255\n", SCR_W, SCR_H);
    for (int i = 0; i < SCR_W * SCR_H; i++) { u16 c = fb[i]; unsigned char px[3] = {(c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3}; fwrite(px, 1, 3, f); }
    fclose(f);
}
static void tap(u32 b) { game_tick(b); game_tick(0); }
static void run(u32 btn, int n) { for (int i = 0; i < n; i++) game_tick(btn); }
// simple bot: fight nearest, else walk flow toward stairs
static void bot(int ticks, int use_skills) {
    static u16 sd[MAP][MAP]; int built = -1;
    for (int t = 0; t < ticks && (G.state == ST_PLAY || G.state == ST_FADE); t++) {
        if (G.state == ST_FADE) { game_tick(0); continue; }
        if (G.in_town) return;
        u32 b = 0;
        Mob *tg = find_target(10 * FX, 0);
        if (built != G.floor) { Room *e = &g_rooms[g_stairs_room]; bfs_from(e->x + e->w / 2, e->y + e->h / 2, sd); built = G.floor; }
        int tx = G.ptx, ty = G.pty, best = sd[ty][tx], bx = 0, by = 0;
        if (tg && dist_fx(tg->x, tg->y, G.p.x, G.p.y) < (G.p.wtype == B_SWORD ? 300 : 1400)) {
            i32 dx = tg->x - G.p.x, dy = tg->y - G.p.y, l = dist_fx(dx, dy, 0, 0); if (l) { G.p.dirx = dx * 256 / l; G.p.diry = dy * 256 / l; }
            b |= BTN_A;
            if (use_skills && (t % 7) == 0) b |= (t / 7) % 2 ? BTN_X : BTN_Y;
        } else {
            u16 (*fl)[MAP] = tg ? g_flow : sd;   // flow toward player isn't useful; head to stairs, mobs come to us
            (void)fl;
            static const int ox[4] = {1, -1, 0, 0}, oy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) if (in_map(tx+ox[k],ty+oy[k]) && sd[ty+oy[k]][tx+ox[k]] < best) { best = sd[ty+oy[k]][tx+ox[k]]; bx = ox[k]; by = oy[k]; }
            i32 wx = (tx + bx) * FX + 128 - G.p.x, wy = (ty + by) * FX + 128 - G.p.y, sx = wx, sy = wy;
            if (sx > 20) b |= BTN_RIGHT; if (sx < -20) b |= BTN_LEFT; if (sy > 20) b |= BTN_DOWN; if (sy < -20) b |= BTN_UP;
            if (tile_at(tx, ty) == T_STAIRS && (t & 1)) b = BTN_A;
        }
        if (G.p.hp < (G.p.hpmax << 8) / 3 && (t % 10) == 0) b |= BTN_L;
        game_tick(b);
    }
}
// hunter bot: walks to the nearest living monster anywhere on the floor and fights it (melee, no skills, no rolling)
static void hunter(void) {
    static u16 hd[MAP][MAP]; static int htx = -1, hty = -1, age = 0;
    Mob *best = 0; i32 bd = 1 << 30;
    for (int i = 0; i < MAX_MOBS; i++) { Mob *m = &G.mobs[i]; if (!m->alive) continue; i32 d = dist_fx(m->x, m->y, G.p.x, G.p.y); if (d < bd) { bd = d; best = m; } }
    u32 b = 0;
    if (best) {
        if (bd < 300) {
            i32 dx = best->x - G.p.x, dy = best->y - G.p.y, l = dist_fx(dx, dy, 0, 0); if (l) { G.p.dirx = dx * 256 / l; G.p.diry = dy * 256 / l; }
            if (tile_at(G.ptx, G.pty) != T_STAIRS && tile_at(G.ptx, G.pty) != T_DEEP) b |= BTN_A;
            if (bd > 200) { if (dx > 40) b |= BTN_RIGHT; if (dx < -40) b |= BTN_LEFT; if (dy > 40) b |= BTN_DOWN; if (dy < -40) b |= BTN_UP; }
        } else {
            int mtx = best->x >> 8, mty = best->y >> 8;
            if (mtx != htx || mty != hty || ++age > 40) { bfs_from(mtx, mty, hd); htx = mtx; hty = mty; age = 0; }
            int tx = G.ptx, ty = G.pty, bs = hd[ty][tx], bx = 0, by = 0;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if ((dx || dy) && in_map(tx + dx, ty + dy) && hd[ty + dy][tx + dx] < bs) { bs = hd[ty + dy][tx + dx]; bx = dx; by = dy; }
            i32 wx = (tx + bx) * FX + 128 - G.p.x, wy = (ty + by) * FX + 128 - G.p.y;
            if (wx > 20) b |= BTN_RIGHT; if (wx < -20) b |= BTN_LEFT; if (wy > 20) b |= BTN_DOWN; if (wy < -20) b |= BTN_UP;
        }
    }
    game_tick(b);
}
int main(int argc, char **argv) {
    if (getenv("FAST")) g_gfx_fast = 1;
    if (argc > 1 && argv[1][0] == 's') {   // skill spam sim: hold each skill's button for 10s from full mana, no basic attacks
        static const int LV[3] = {1, 12, 30}, RK[3] = {1, 5, 10};
        printf("%-16s", "skill"); for (int c = 0; c < 3; c++) printf("  L%-2d R%-2d cost cd   10s  20s", LV[c], RK[c]); printf("\n");
        for (int id = 0; id < NSK; id++) {
            if (SKILLS[id].passive) continue;
            printf("%-16s", SKILLS[id].name);
            for (int c = 0; c < 3; c++) {
                game_init(5); tap(BTN_START); tap(BTN_A); tap(BTN_A); run(0, 45); enter_floor(1);
                for (int i = 0; i < MAX_MOBS; i++) G.mobs[i].alive = 0;
                memset(G.equip, 0, sizeof G.equip);
                int t = SKILLS[id].tree; item_make(&G.equip[S_MAIN], t == 0 ? B_SWORD : t == 1 ? B_BOW : B_STAFF, R_COMMON, 1);
                G.p.cls = t; G.p.level = LV[c]; G.p.attr[t == 0 ? 0 : t == 1 ? 1 : 2] = (LV[c] - 1) * 3; memset(G.p.sk, 0, sizeof G.p.sk); G.p.sk[id] = (u8)RK[c];
                G.p.slot[0] = (i8)id; player_recalc(); G.p.mp = G.p.mpmax << 8; G.p.hp = G.p.hpmax << 8;
                g_casts = 0; int c10 = 0;
                for (int k = 0; k < 52 * 20; k++) { G.p.hp = G.p.hpmax << 8; G.p.whirl_t = 0; G.p.charge_t = 0; game_tick(BTN_X); if (k == 52 * 10 - 1) c10 = g_casts; if (G.state != ST_PLAY) { G.state = ST_PLAY; } }
                printf("  pool%3d %4d %3.1f %4d %4d", G.p.mpmax, skill_cost(id), skill_cd(id) / 52.0, c10, g_casts);
            }
            printf("\n");
        }
        return 0;
    }
    if (argc > 1 && argv[1][0] == 'c') {   // class change at the trainer + skills tab
        game_init(9); tap(BTN_START); tap(BTN_A); tap(BTN_A); run(0, 45);
        G.p.gold = 100000; G.p.level = 6; G.p.unspent_skill = 3; player_recalc();
        G.state = ST_NPC; G.npc = NPC_TRAINER; G.sub = 0; tap(0); dump("trainer");
        tap(BTN_X); dump("trainer_pick"); tap(BTN_RIGHT); tap(BTN_A); run(0, 2);
        printf("class=%d gold=%u weapon=%d bag0=%d unspent=%d\n", G.p.cls, G.p.gold, G.equip[S_MAIN].base, G.bag[0].used ? G.bag[0].base : -1, G.p.unspent_skill);
        dump("trainer_done"); tap(BTN_B);
        G.state = ST_PLAY; tap(BTN_START); tap(BTN_R); run(0, 2); dump("skills_tab");
        tap(BTN_A); printf("learned %d\n", G.p.sk[G.p.cls * 7]);
        tap(BTN_LEFT); tap(BTN_A); dump("skills_other");
        // old v2 save import: a ranger-skilled hero saved in slot 0 should move to slot 1 as a ranger
        G.p.cls = CL_RANGER; memset(G.p.sk, 0, sizeof G.p.sk); G.p.sk[SK_MULTI] = 3; G.slot_cur = 0; game_save();
        g_saves[0][0] = g_saves[G.slot_cur][G.char_cur]; g_saves[0][0].version = 2; g_saves[0][0].cls = 0; memset(&g_saves[0][1], 0, sizeof(SaveData));
        import_v2_save(); printf("import: hero0 valid=%d hero1 valid=%d cls=%d multi=%d\n", hero_valid(0, 0), hero_valid(0, 1), g_saves[0][1].cls, g_saves[0][1].sk[SK_MULTI]);
        G.state = ST_TITLE; tap(BTN_START); run(0, 3); dump("load_slots"); tap(BTN_A); run(0, 3); dump("door_fade"); run(0, 30); dump("door"); tap(BTN_RIGHT); run(0, 4); dump("door_turning"); run(0, 20); dump("door_turned");
        return 0;
    }
    if (argc > 1 && argv[1][0] == 'l') {   // loot sim: clear floors 1..10 with the hunter (kept alive), count item drops by rarity
        int per[12][8]; memset(per, 0, sizeof per); static int lvl[12], gld[12]; memset(lvl, 0, sizeof lvl); memset(gld, 0, sizeof gld);
        for (int seed = 1; seed <= 4; seed++) {
            game_init((u32)seed * 104729u); tap(BTN_START); tap(BTN_A); tap(BTN_A); run(0, 45);
            G.interact_kind = IK_NPC; G.interact_idx = NPC_ENTRANCE; do_interact(); tap(BTN_A); run(0, 20);
            for (int f = 1; f <= 10; f++) {
                while (G.state == ST_FADE) game_tick(0);
                memset(g_dropstat, 0, sizeof g_dropstat);
                for (int t = 0; t < 12000; t++) {
                    if (G.state == ST_FADE) { game_tick(0); continue; }
                    if (G.state != ST_PLAY) G.state = ST_PLAY;
                    G.p.hp = G.p.hpmax << 8;
                    hunter();
                    int left = 0; for (int i = 0; i < MAX_MOBS; i++) left += G.mobs[i].alive; if (!left) break;
                }
                for (int r = 0; r < 6; r++) per[f][r] += g_dropstat[r];
                lvl[f] += G.p.level; gld[f] += (int)G.p.gold;
                enter_floor(G.floor + 1);   // next floor directly
            }
        }
        printf("item drops per floor (avg of 4 runs): common rare ultra epic legend majestic\n");
        for (int f = 1; f <= 10; f++) printf("floor %2d: %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f   level %4.1f  gold %6d\n", f, per[f][0] / 4.0, per[f][1] / 4.0, per[f][2] / 4.0, per[f][3] / 4.0, per[f][4] / 4.0, per[f][5] / 4.0, lvl[f] / 4.0, gld[f] / 4);
        return 0;
    }
    if (argc > 1 && argv[1][0] == 'f') {   // floor-1 difficulty sim: level-1 hero, no level ups, bot clears the floor
        double tot = 0;
        for (int seed = 1; seed <= 8; seed++) {
            game_init((u32)seed * 7919u); tap(BTN_START); tap(BTN_A); tap(BTN_A); run(0, 45);
            G.interact_kind = IK_NPC; G.interact_idx = NPC_ENTRANCE; do_interact(); tap(BTN_A); run(0, 20);
            i32 lost = 0, prev = G.p.hp; int ticks = 0, refills = 0, start_mobs = 0;
            for (int i = 0; i < MAX_MOBS; i++) start_mobs += G.mobs[i].alive;
            while (ticks < 9000) {
                G.p.xp = 0;
                if (G.state == ST_FADE) { game_tick(0); continue; }
                if (G.floor != 1 || G.in_town) break;
                hunter(); ticks++;
                if (G.p.hp < prev) lost += prev - G.p.hp;
                if (G.p.hp <= (G.p.hpmax << 8) / 4 || G.state == ST_DEAD) { G.state = ST_PLAY; G.p.hp = G.p.hpmax << 8; refills++; }   // "drink a potion" at 25%
                prev = G.p.hp;
                int left = 0; for (int i = 0; i < MAX_MOBS; i++) left += G.mobs[i].alive;
                if (!left) break;
            }
            int left = 0; for (int i = 0; i < MAX_MOBS; i++) left += G.mobs[i].alive;
            printf("seed %d: mobs %d left %d ticks %d  damage taken %.2f x maxhp (%d)  potions %d  lvl %d floor %d\n", seed, start_mobs, left, ticks, (lost >> 8) / (double)G.p.hpmax, G.p.hpmax, refills, G.p.level, G.floor);
            tot += (lost >> 8) / (double)G.p.hpmax;
        }
        printf("average damage taken: %.2f x max HP\n", tot / 8);
        return 0;
    }
    if (argc > 1 && argv[1][0] == 'w') { game_init(argc > 2 ? 0xA5A5F00Du : 1); tap(BTN_START); tap(BTN_A); tap(BTN_A); run(0, 45); for (int i = 0; i < 170; i++) { game_tick(BTN_UP); if (i % 20 == 0) printf("%d %d.%d %d.%d\n", i, G.p.x >> 8, G.p.x & 255, G.p.y >> 8, G.p.y & 255); } return 0; }
    u32 seed = argc > 1 ? (u32)atoi(argv[1]) : 777;
    game_init(seed);
    dump("title");
    tap(BTN_START); run(0, 4); dump("select");
    tap(BTN_A); run(0, 4);
    tap(BTN_A); run(0, 10); dump("door_out"); run(0, 26); dump("world_fade"); run(0, 80);
    dump("town");
    { i32 ox = G.p.x, oy = G.p.y;   /* behind_shop_real: stand just north of the merchant's shop, let the cut fade in */
      G.p.x = (g_bld[0].x + 2) * FX + 128; G.p.y = (g_bld[0].y - 1) * FX + 200; G.camx = G.p.x; G.camy = G.p.y;
      for (int k = 0; k < 10; k++) { run(0, 1); game_render(fb); }
      dump("behind_shop_real"); G.p.x = ox; G.p.y = oy; G.camx = ox; G.camy = oy; run(0, 2); }
    { i32 ox = G.p.x, oy = G.p.y;   // scroll past the jeweler's left edge: the shop must not jump
      for (int k = 0; k < 3; k++) { G.p.x = (26 + k * 2) * FX + 128; G.p.y = 36 * FX + 128; G.camx = G.p.x; G.camy = G.p.y; run(0, 1);
        char nm[32]; snprintf(nm, sizeof nm, "jeweler_scroll%d", k); dump(nm); }
      G.p.x = ox; G.p.y = oy; G.camx = ox; G.camy = oy; run(0, 2); }
    { i32 ox = G.p.x, oy = G.p.y;
      G.p.x = 32 * FX + 128; G.p.y = 28 * FX + 128; G.camx = G.p.x; G.camy = G.p.y; run(0, 2); dump("town_plaza");
      G.p.x = 40 * FX + 128; G.p.y = 26 * FX + 30; G.camx = G.p.x; G.camy = G.p.y; run(0, 2); dump("town_smith");
      G.p.x = 30 * FX + 128; G.p.y = 37 * FX + 128; G.camx = G.p.x; G.camy = G.p.y; run(0, 2); dump("town_behind_shop");
      G.p.x = ox; G.p.y = oy; G.camx = ox; G.camy = oy; run(0, 2); }
    // walk to the merchant
    G.p.x = g_npcs[NPC_MERCHANT].tx * FX + 128; G.p.y = (g_npcs[NPC_MERCHANT].ty + 1) * FX + 128; G.camx = G.p.x; G.camy = G.p.y; run(0, 2);
    dump("town_merchant_prompt");
    tap(BTN_A); dump("merchant");
    tap(BTN_DOWN); tap(BTN_DOWN); dump("merchant_bag");
    tap(BTN_B);
    G.p.gold = 50000; G.gems[2] = 4; G.gems[7] = 3; G.gems[25] = 1;
    G.interact_kind = IK_NPC; G.interact_idx = NPC_SMITH; do_interact(); G.cur_region = 0; G.cur_x = 0; G.cur_y = 1; dump("smith");
    tap(BTN_X); tap(BTN_X); tap(BTN_A); dump("smith_after");
    tap(BTN_B);
    G.interact_kind = IK_NPC; G.interact_idx = NPC_JEWELER; do_interact(); G.cur_region = 0; G.cur_x = 0; G.cur_y = 1;
    tap(BTN_A); tap(BTN_Y); dump("jeweler_pick"); tap(BTN_A); dump("jeweler");
    tap(BTN_R); dump("jeweler_gems"); tap(BTN_B);
    G.interact_kind = IK_NPC; G.interact_idx = NPC_TRAINER; do_interact(); dump("trainer"); tap(BTN_B);
    G.interact_kind = IK_NPC; G.interact_idx = NPC_STASH; do_interact(); dump("stash"); tap(BTN_B);
    G.interact_kind = IK_NPC; G.interact_idx = NPC_ENTRANCE; do_interact(); dump("waypoint"); tap(BTN_B);
    // menus
    tap(BTN_START); dump("menu_bag");
    tap(BTN_R); dump("menu_skills");
    tap(BTN_A); tap(BTN_DOWN); tap(BTN_DOWN); dump("menu_skills2");
    tap(BTN_R); dump("menu_hero");
    tap(BTN_R); dump("menu_system");
    tap(BTN_B);
    G.show_map = 1; dump("map"); G.show_map = 0;
    // into the dungeon
    G.interact_kind = IK_NPC; G.interact_idx = NPC_ENTRANCE; do_interact(); tap(BTN_A); run(0, 40);
    dump("dungeon1");
    { /* smash_test: stand south of a crate/barrel/pot, face it and swing */
      int found = 0, bx = 0, by = 0;
      for (int y = 1; y < MAP - 2 && !found; y++) for (int x = 1; x < MAP - 1; x++) if (breakable_at(x, y) && is_floorish(g_map[y + 1][x])) { bx = x; by = y; found = 1; break; }
      if (found) {
        i32 sx0 = G.p.x, sy0 = G.p.y; int w0 = G.p.wtype;
        G.p.x = bx * FX + 128; G.p.y = (by + 1) * FX + 128; G.p.dirx = 0; G.p.diry = -256; G.camx = G.p.x; G.camy = G.p.y;
        G.p.wtype = B_SWORD; melee_arc(100, 340, 40, 0);
        printf("smash_test: prop at %d,%d %s\n", bx, by, breakable_at(bx, by) ? "STILL THERE" : "broken");
        run(0, 6); dump("smash");
        G.p.wtype = w0; G.p.x = sx0; G.p.y = sy0; G.camx = sx0; G.camy = sy0;
      } else printf("smash_test: no breakable prop found\n");
    }
    { i32 sx0 = G.p.x, sy0 = G.p.y;
      G.p.x = g_stairs_tx * FX + 128; G.p.y = (g_stairs_ty + 2) * FX + 128; G.camx = G.p.x; G.camy = G.p.y; G.ptx = G.p.x >> 8; G.pty = G.p.y >> 8; refresh_vis(); run(0, 2); dump("near_stairs");
      G.show_map = 1; dump("dungeon_map"); G.show_map = 0;
      G.p.x = sx0; G.p.y = sy0; G.camx = sx0; G.camy = sy0; G.ptx = G.p.x >> 8; G.pty = G.p.y >> 8; refresh_vis(); run(0, 2); }
    { i32 sx0 = G.p.x, sy0 = G.p.y;   // stand just north of a wall to check the see-through
      for (int y = 1; y < MAP - 2; y++) for (int x = 1; x < MAP - 1; x++)
        if (is_floorish(g_map[y][x]) && g_map[y + 1][x] == T_WALL && g_map[y + 1][x - 1] == T_WALL && g_map[y + 1][x + 1] == T_WALL && g_map[y + 2][x] == T_WALL) { G.p.x = x * FX + 128; G.p.y = y * FX + 200; G.camx = G.p.x; G.camy = G.p.y; y = MAP; break; }
      G.ptx = G.p.x >> 8; G.pty = G.p.y >> 8; refresh_vis(); run(0, 2); dump("behind_wall"); G.p.x = sx0; G.p.y = sy0; run(0, 2); }
    // give a level-10ish character with skills for a combat soak test
    G.p.level = 12; G.p.attr[0] = 30; G.p.attr[3] = 25; G.p.sk[SK_CLEAVE] = 5; G.p.sk[SK_WHIRL] = 3; G.p.sk[SK_QUAKE] = 0; G.p.sk[SK_WARCRY] = 2;
    G.p.slot[0] = SK_CLEAVE; G.p.slot[1] = SK_WHIRL; player_recalc(); G.p.hp = G.p.hpmax << 8; G.p.mp = G.p.mpmax << 8;
    bot(60 * 90, 1);
    printf("melee: state=%d floor=%d kills=%d hp=%d/%d gold=%u lvl=%d\n", G.state, G.floor, G.p.kills, G.p.hp >> 8, G.p.hpmax, G.p.gold, G.p.level);
    dump("melee_fight");
    // ranged + magic quick checks
    if (G.state == ST_DEAD) { tap(BTN_START); }
    G.state = ST_PLAY; enter_floor(4);
    G.p.cls = CL_RANGER; item_make(&G.equip[S_MAIN], B_BOW, R_EPIC, 8); G.equip[S_OFF].used = 0; G.p.sk[SK_MULTI] = 4; G.p.sk[SK_RAIN] = 3; G.p.sk[SK_EXPLO] = 3;
    G.p.slot[0] = SK_MULTI; G.p.slot[1] = SK_RAIN; G.p.slot[2] = SK_EXPLO; player_recalc(); G.p.hp = G.p.hpmax << 8;
    bot(60 * 40, 1); printf("range: state=%d floor=%d kills=%d\n", G.state, G.floor, G.p.kills); dump("range_fight");
    G.state = ST_PLAY; enter_floor(6);
    G.p.cls = CL_MAGE; item_make(&G.equip[S_MAIN], B_STAFF, R_EPIC, 8); G.p.sk[SK_FIRE] = 4; G.p.sk[SK_METEOR] = 3; G.p.sk[SK_NOVA] = 3; G.p.sk[SK_CHAIN] = 3; G.p.sk[SK_SHIELD] = 2;
    G.p.slot[0] = SK_FIRE; G.p.slot[1] = SK_METEOR; G.p.slot[2] = SK_CHAIN; G.p.slot[3] = SK_NOVA; player_recalc(); G.p.hp = G.p.hpmax << 8; G.p.mp = G.p.mpmax << 8;
    bot(60 * 40, 1); printf("magic: state=%d floor=%d kills=%d\n", G.state, G.floor, G.p.kills); dump("magic_fight");
    // town portal
    G.state = ST_PLAY; tap(BTN_START); G.tab = 3; G.sel = 0; tap(BTN_A); run(0, 40); printf("after portal: in_town=%d portal=%d\n", G.in_town, G.portal_floor); dump("town_portal");
    // save/load round trip
    game_save(); int lvl = G.p.level; G.p.level = 1; printf("load=%d level %d->%d\n", game_load(G.slot_cur, G.char_cur), lvl, G.p.level);
    return 0;
}
