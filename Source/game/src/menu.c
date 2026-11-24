// Menus: Start menu (bag / skills / hero / system), town NPC screens, waypoints. Also game_render.

static const i8 DOLL_GRID[4][3] = {{S_NECK, S_HEAD, S_BACK}, {S_MAIN, S_BODY, S_OFF}, {S_GLOVES, S_LEGS, S_RING1}, {-1, S_BOOTS, S_RING2}};
#define BAG_COLS 8
#define BAG_ROWS 3
enum { TAB_BAG, TAB_GEMS, TAB_SKILLS, TAB_HERO, TAB_SYS, TAB_N };
static const char *TAB_NAME[TAB_N] = {"BAG", "GEMS", "SKILLS", "HERO", "SYSTEM"};
#define SYS_MAIN_N 6
#define SYS_GFX_N 4
#define SYS_CTL_N (ACT_N + 2)
static void footer(const char *s);
static void msg_line(int y);
static void sys_row(int i, int y, int x0, int w, const char *label, const char *value) {
    int sel = G.sel == i;
    fill_rect(x0, y, w, 11, sel ? RGB(60, 44, 30) : RGB(22, 18, 26)); rect_outline(x0, y, w, 11, sel ? RGB(255, 220, 120) : RGB(50, 44, 54));
    if (!value) { text_c(x0 + w / 2, y + 2, label, RGB(255, 255, 255)); return; }
    text(x0 + 6, y + 2, label, RGB(230, 225, 215));
    text(x0 + w - 6 - text_w(value), y + 2, value, sel ? RGB(255, 220, 120) : RGB(170, 200, 255));
}
static void draw_system_page(void) {
    if (g_sys_page == 0) {
        const char *opt[SYS_MAIN_N] = {G.in_town ? "TOWN PORTAL (IN TOWN)" : "TOWN PORTAL", "SAVE & SWITCH HERO", "SAVE & QUIT", "GRAPHICS", "CONTROLS", "RESUME"};
        for (int i = 0; i < SYS_MAIN_N; i++) sys_row(i, 18 + i * 13, 60, 146, opt[i], 0);
        int y = 104;
        text(10, y, "TOWN PORTAL OPENS A WAY HOME. STEP BACK", RGB(150, 140, 130)); y += LH;
        text(10, y, "THROUGH IT TO RETURN TO THE SAME DEPTH.", RGB(150, 140, 130)); y += 12;
        text(10, y, "THE GAME SAVES ON EVERY NEW DEPTH, IN TOWN", RGB(150, 140, 130)); y += LH;
        text(10, y, "AND WHEN YOU CLOSE A MENU.", RGB(150, 140, 130));
        msg_line(200);
        footer("A SELECT   B CLOSE   L/R TABS");
    } else if (g_sys_page == 1) {
        text_c(SCR_W / 2, 18, "GRAPHICS", RGB(255, 210, 140));
        sys_row(0, 30, 30, 206, "LIGHTING", g_gfx_fast ? "SIMPLE" : "SMOOTH");
        sys_row(1, 43, 30, 206, "EFFECTS", g_gfx_effects ? "HIGH" : "LOW");
        sys_row(2, 56, 30, 206, "SHOW FPS", g_show_perf ? "ON" : "OFF");
        sys_row(3, 69, 30, 206, "BACK", 0);
        static const char *HELP[4][2] = {
            {"SMOOTH: LIGHT FADES ACROSS EVERY TILE AND", "THE GROUND BLENDS AT ITS EDGES."},
            {"HIGH: THE HERO CARRIES A LANTERN, EMBERS", "GLOW, MORE DUST DRIFTS IN THE LIGHT."},
            {"FRAMES PER SECOND AND TIME PER FRAME.", ""},
            {"", ""}};
        text(30, 89, HELP[G.sel][0], RGB(150, 140, 130)); text(30, 89 + LH, HELP[G.sel][1], RGB(150, 140, 130));
        msg_line(200);
        footer("A CHANGE   B BACK");
    } else {
        text_c(SCR_W / 2, 16, "CONTROLS", RGB(255, 210, 140));
        for (int a = 0; a < ACT_N; a++) {
            const char *v = g_rebind == a ? (((G.ticks / 15) & 1) ? "PRESS A BUTTON" : "") : act_key(a);
            sys_row(a, 26 + a * 12, 30, 206, ACT_NAME[a], v);
        }
        sys_row(ACT_N, 26 + ACT_N * 12, 30, 206, "RESET TO DEFAULT", 0);
        sys_row(ACT_N + 1, 26 + (ACT_N + 1) * 12, 30, 206, "BACK", 0);
        text(30, 154, "D-PAD ALWAYS MOVES. IN MENUS A, B, L, R", RGB(150, 140, 130));
        text(30, 154 + LH, "AND START WORK AS PRINTED.", RGB(150, 140, 130));
        msg_line(200);
        footer(g_rebind >= 0 ? "PRESS THE NEW BUTTON" : "A CHANGE   B BACK");
    }
}
static const char *ATTR_NAME[4] = {"STRENGTH", "DEXTERITY", "INTELLECT", "VITALITY"};
static const char *ATTR_DESC[4] = {"+1% MELEE DMG, ARMOR", "+1% BOW DMG, CRIT", "+1% SPELL DMG, MANA", "+4 LIFE PER POINT"};

static void npc_portrait(int k, int x, int y, int scale) {
#ifdef HAVE_NPC_ANIM
    const CharSprite *s = NPC_ANIM[k][0][0][(G.ticks / 10) % NPC_ANIM_N[k][0]];
    char_draw(s, x, y, 7, 0, 0, scale, 0);
#else
    static const Sprite *NS[4] = {&SPR_npc_merchant, &SPR_npc_smith, &SPR_npc_jeweler, &SPR_npc_trainer};
    blit_ex(NS[k], x, y, g_shade[7], 0, 0, scale);
#endif
}
// ---------- costs ----------
static int enchant_cost(const Item *it) { return (it->ilvl * 20 + 50) * (it->naff + 1); }
static int reforge_cost(const Item *it) { return (it->ilvl * 15 + 40) * (it->rarity + 1); }
static int upgrade_cost(const Item *it) { return (it->ilvl * 40 + 100) * (it->rarity + 1) * (it->rarity + 1); }
static int socket_cost(const Item *it) { return 100 * (it->sockets + 1) * (10 + it->ilvl) / 10; }
static int unsocket_cost(const Item *it) { return 50 + it->ilvl * 10; }
static int combine_cost(int tier) { return 20 * (tier + 1) * (tier + 1); }
static int respec_cost(void) { return 100 * G.p.level; }
static int class_change_cost(void) { return 1500 + 40 * G.p.level * G.p.level; }   // deliberately steep
static int potion_price(void) { return 15 + 2 * G.p.level; }
static int buy_price(const Item *it) { return item_value(it) * 4; }

static int spend(int cost) {
    if ((int)G.p.gold < cost) { msg("NOT ENOUGH GOLD", RGB(255, 120, 120)); snd_play(150, 120, 0, 1500, 0); return 0; }
    G.p.gold -= (u32)cost; sfx_gold(); return 1;
}

// ---------- cursors ----------
static Item *cursor_item(int *is_equip, int *slot) {
    if (G.cur_region == 0) { int s = DOLL_GRID[G.cur_y][G.cur_x]; *is_equip = 1; *slot = s; return s >= 0 ? &G.equip[s] : 0; }
    if (G.cur_region == 2) { *is_equip = 0; *slot = -1; return 0; }   // potion pouch
    *is_equip = 0; *slot = G.cur_y * BAG_COLS + G.cur_x; return &G.bag[*slot];
}
static void nav_doll_bag(void) {
    int mx = pressed(BTN_RIGHT) - pressed(BTN_LEFT), my = pressed(BTN_DOWN) - pressed(BTN_UP);
    if (mx || my) sfx_menu();
    if (G.cur_region == 0) {
        G.cur_y = CLAMP(G.cur_y + my, 0, 3); G.cur_x += mx;
        if (G.cur_x > 2) { G.cur_region = 1; G.cur_x = 0; G.cur_y = MIN(G.cur_y, BAG_ROWS - 1); }
        if (G.cur_x < 0) G.cur_x = 0;
    } else {
        G.cur_y = CLAMP(G.cur_y + my, 0, BAG_ROWS - 1); G.cur_x += mx;
        if (G.cur_x < 0) { G.cur_region = 0; G.cur_x = 2; }
        if (G.cur_x >= BAG_COLS) G.cur_x = BAG_COLS - 1;
    }
}
static void nav_two_grid(int top_rows) {   // region 0 = top grid (8 x top_rows), region 1 = bag (8 x 3)
    int mx = pressed(BTN_RIGHT) - pressed(BTN_LEFT), my = pressed(BTN_DOWN) - pressed(BTN_UP);
    if (mx || my) sfx_menu();
    G.cur_x = CLAMP(G.cur_x + mx, 0, BAG_COLS - 1);
    G.cur_y += my;
    if (G.cur_region == 0 && G.cur_y >= top_rows) { G.cur_region = 1; G.cur_y = 0; }
    if (G.cur_region == 1 && G.cur_y < 0) { G.cur_region = 0; G.cur_y = top_rows - 1; }
    G.cur_y = CLAMP(G.cur_y, 0, G.cur_region ? BAG_ROWS - 1 : top_rows - 1);
}
static int equip_target_slot(const Item *it) {
    int s = BASE_SLOT[it->base];
    if (s == S_RING1 && G.equip[S_RING1].used && !G.equip[S_RING2].used) s = S_RING2;
    return s;
}
static void inv_action_equip(void) {
    int eq, slot; Item *it = cursor_item(&eq, &slot);
    if (!it || !it->used) return;
    if (eq) { int f = bag_free(); if (f < 0) { msg("BAG FULL", RGB(255, 120, 120)); return; } G.bag[f] = *it; it->used = 0; }
    else {
        int ic = item_class(it->base);
        if (ic >= 0 && ic != G.p.cls) { Str m; s_reset(&m); s_add(&m, "ONLY A "); s_add(&m, CLASS_NAME[ic]); s_add(&m, " CAN USE THIS"); msg(m.b, RGB(255, 140, 120)); return; }
        int s = equip_target_slot(it);
        if (s == S_OFF && G.equip[S_MAIN].used && G.equip[S_MAIN].base == B_BOW) { msg("BOWS NEED BOTH HANDS", RGB(200, 200, 200)); return; }
        Item tmp = G.equip[s]; G.equip[s] = *it; *it = tmp;
        if (s == S_MAIN && G.equip[S_MAIN].base == B_BOW && G.equip[S_OFF].used) {
            int f = bag_free(); if (f >= 0) { G.bag[f] = G.equip[S_OFF]; G.equip[S_OFF].used = 0; msg("BOWS NEED BOTH HANDS", RGB(200, 200, 200)); }
        }
    }
    sfx_equip(); player_recalc();
}
static int g_salv_ask;   // the "salvage this?" question is open
// Points are spent provisionally (stats update live) and only kept after a confirmation; until then they can be
// taken back one at a time, but never below what the hero had when the menu was opened.
static int g_pt_on, g_pt_ask;   // provisional spending in progress; the "keep these changes?" question is open
static Player g_pt_base;        // the hero before any provisional points
static int pt_changed(void) { return g_pt_on && (G.p.unspent_attr != g_pt_base.unspent_attr || G.p.unspent_skill != g_pt_base.unspent_skill); }
static void pt_begin(void) { if (!g_pt_on) { g_pt_base = G.p; g_pt_on = 1; } }
static void pt_keep(void) { g_pt_on = 0; g_pt_ask = 0; game_save(); sfx_levelup(); }
static void pt_undo(void) { G.p = g_pt_base; player_recalc(); g_pt_on = 0; g_pt_ask = 0; sfx_menu(); }
static void inv_action_salvage(void) {
    int eq, slot; Item *it = cursor_item(&eq, &slot);
    if (!it || !it->used || eq) return;
    g_salv_ask = 0;
    for (int g = 0; g < it->sockets; g++) if (it->gem[g]) G.gems[it->gem[g] - 1]++;
    int v = item_value(it); G.p.gold += (u32)v; it->used = 0;
    Str s; s_reset(&s); s_add(&s, "SALVAGED FOR "); s_int(&s, v); s_add(&s, " GOLD"); msg(s.b, pal(30)); sfx_gold();
}

// ---------- start menu ----------
static void change_class(int cls);
static void respec(void) {
    Player *p = &G.p;
    for (int i = 0; i < 4; i++) { p->unspent_attr += p->attr[i]; p->attr[i] = 0; }
    for (int i = 0; i < NSK; i++) { p->unspent_skill += p->sk[i]; p->sk[i] = 0; }
    for (int i = 0; i < 4; i++) if (p->slot[i] >= 0 && p->slot[i] < NSK) p->slot[i] = -1;
    player_recalc();
}
// Trainer: become another class. Skills reset (points refunded), class gear goes to the bag, a plain weapon of the new class is handed over.
static void change_class(int cls) {
    Player *p = &G.p;
    for (int i = 0; i < NSK; i++) { p->unspent_skill += p->sk[i]; p->sk[i] = 0; }
    for (int k = 0; k < 4; k++) if (p->slot[k] >= 0 && p->slot[k] < NSK) p->slot[k] = -1;
    p->cls = cls;
    for (int sl = 0; sl < S_COUNT; sl++) {
        Item *e = &G.equip[sl]; if (!e->used) continue;
        int ic = item_class(e->base); if (ic < 0 || ic == cls) continue;
        int f = bag_free();
        if (f >= 0) G.bag[f] = *e;
        else for (int i = 0; i < STASH_N; i++) if (!G.stash[i].used) { G.stash[i] = *e; break; }
        e->used = 0;
    }
    if (!G.equip[S_MAIN].used) item_make(&G.equip[S_MAIN], class_weapon(cls), R_COMMON, MAX(1, G.deepest));
    player_recalc(); p->hp = p->hpmax << 8; p->mp = p->mpmax << 8;
    Str m; s_reset(&m); s_add(&m, "NOW A "); s_add(&m, CLASS_NAME[cls]); s_add(&m, "! SPEND YOUR SKILL POINTS"); msg(m.b, RGB(160, 220, 255));
    sfx_levelup(); game_save();
}
static int skill_at(int col, int row) { return row < 7 ? col * 7 + row : -1; }

static void update_menu(void) {
    Player *p = &G.p;
    if (G.assign) {   // choosing a button slot for a skill/potion
        if (pressed(BTN_LEFT)) G.sub = (G.sub + 3) % 4;
        if (pressed(BTN_RIGHT)) G.sub = (G.sub + 1) % 4;
        for (int i = 0; i < 4; i++) if (pressed(act_btn(ACT_SK1 + i))) { G.sub = i; G.assign = 2; }   // the slot's own button
        if (pressed(BTN_A) || G.assign == 2) {
            int v = G.cur_y == 7 ? (G.cur_x == 0 ? SLOT_HPOT : SLOT_MPOT) : skill_at(G.cur_x, G.cur_y);
            for (int i = 0; i < 4; i++) if (p->slot[i] == v) p->slot[i] = -1;
            p->slot[G.sub] = (i8)v; G.assign = 0; sfx_equip(); game_save();
        }
        if (pressed(BTN_B)) G.assign = 0;
        return;
    }
    if (g_pt_ask) {   // keep the points just spent? A keep, X undo all, B keep editing
        if (pressed(BTN_A)) pt_keep();
        else if (pressed(BTN_X)) pt_undo();
        else if (pressed(BTN_B)) { g_pt_ask = 0; sfx_menu(); }
        return;
    }
    if (pt_changed() && (pressed(BTN_START) || pressed(BTN_B) || pressed(BTN_L) || pressed(BTN_R))) { g_pt_ask = 1; sfx_menu(); return; }
    if (g_salv_ask) {   // salvage question: A yes, B / Y no
        if (pressed(BTN_A)) { inv_action_salvage(); }
        else if (pressed(BTN_B) || pressed(BTN_Y) || pressed(BTN_START)) { g_salv_ask = 0; sfx_menu(); }
        return;
    }
    if (G.tab == TAB_SYS && g_rebind >= 0) {   // controls: the next button pressed takes the action
        for (int k = 0; k < ACT_N; k++) if (pressed(PHYS_BTN[k])) {
            Settings *t = settings(); int a = g_rebind, old = t->map[a];
            for (int b = 0; b < ACT_N; b++) if (t->map[b] == k) t->map[b] = (u8)old;   // whoever had it gets the old button
            t->map[a] = (u8)k; g_rebind = -1; settings_changed(); sfx_equip(); break;
        }
        return;
    }
    if (G.tab == TAB_SYS && g_sys_page && pressed(BTN_B)) { G.sel = g_sys_page == 1 ? 3 : 4; g_sys_page = 0; sfx_menu(); return; }
    u32 mb = act_btn(ACT_MENU); if (mb & (BTN_A | BTN_L | BTN_R)) mb = 0;   // those keep their menu jobs
    if (pressed(BTN_START) || pressed(BTN_B) || pressed(mb)) { g_pt_on = 0; G.state = ST_PLAY; sfx_menu(); game_save(); return; }
    if (pressed(BTN_L) || pressed(BTN_R)) {
        g_pt_on = 0;
        G.tab = (G.tab + (pressed(BTN_R) ? 1 : TAB_N - 1)) % TAB_N; sfx_menu();
        G.cur_x = G.tab == TAB_SKILLS ? G.p.cls : 0; G.cur_y = 0; G.cur_region = 1; G.sel = 0; return;
    }
    switch (G.tab) {
    case TAB_BAG:
        if (G.cur_region == 2) {   // potion pouch under the bag
            if (pressed(BTN_LEFT) && G.cur_x > 0) { G.cur_x--; sfx_menu(); }
            if (pressed(BTN_RIGHT) && G.cur_x < EL_TYPES - 1) { G.cur_x++; sfx_menu(); }
            if (pressed(BTN_UP)) { G.cur_region = 1; G.cur_y = BAG_ROWS - 1; G.cur_x = MIN(G.cur_x * 3, BAG_COLS - 1); sfx_menu(); }
            if (pressed(BTN_A)) drink_elixir(G.cur_x);
            break;
        }
        if (G.cur_region == 1 && G.cur_y == BAG_ROWS - 1 && pressed(BTN_DOWN)) { G.cur_region = 2; G.cur_x = MIN(G.cur_x / 3, EL_TYPES - 1); sfx_menu(); break; }
        nav_doll_bag();
        if (pressed(BTN_A)) inv_action_equip();
        if (pressed(BTN_Y)) { int eq, sl; Item *it = cursor_item(&eq, &sl); if (it && it->used && !eq) { g_salv_ask = 1; sfx_menu(); } }
        break;
    case TAB_SKILLS: {
        int mx = pressed(BTN_RIGHT) - pressed(BTN_LEFT), my = pressed(BTN_DOWN) - pressed(BTN_UP);
        if (mx || my) sfx_menu();
        G.cur_x = CLAMP(G.cur_x + mx, 0, 2); G.cur_y = CLAMP(G.cur_y + my, 0, 7);
        if (G.cur_y == 7 && G.cur_x == 2) G.cur_x = 1;
        int id = G.cur_y < 7 ? skill_at(G.cur_x, G.cur_y) : -1;
        if (pressed(BTN_A) && id >= 0) {
            if (SKILLS[id].tree != p->cls) { Str m; s_reset(&m); s_add(&m, CLASS_NAME[SKILLS[id].tree]); s_add(&m, " SKILL - CHANGE CLASS AT THE TRAINER"); msg(m.b, RGB(255, 140, 120)); }
            else if (p->unspent_skill <= 0) msg("NO SKILL POINTS - LEVEL UP!", RGB(255, 140, 120));
            else if (p->level < SKILLS[id].req) { Str s; s_reset(&s); s_add(&s, "REQUIRES LEVEL "); s_int(&s, SKILLS[id].req); msg(s.b, RGB(255, 140, 120)); }
            else if (p->sk[id] >= SK_MAX_RANK) msg("MAX RANK", RGB(200, 200, 200));
            else {
                pt_begin();
                p->sk[id]++; p->unspent_skill--; player_recalc(); sfx_menu();
                if (p->sk[id] == 1 && !SKILLS[id].passive) {   // auto-assign a fresh skill to a free slot
                    static const int ORDER[4] = {2, 3, 0, 1};   // skill slot 3 (L), then skill slot 4 (R), then the potions' buttons
                    for (int k = 0; k < 4; k++) { int i = ORDER[k]; if (p->slot[i] < 0) { p->slot[i] = (i8)id; break; } }
                }
            }
        }
        if (pressed(BTN_X) && id >= 0) {   // take back a point spent since the menu opened
            if (g_pt_on && p->sk[id] > g_pt_base.sk[id]) {
                p->sk[id]--; p->unspent_skill++;
                if (p->sk[id] == 0) for (int k = 0; k < 4; k++) if (p->slot[k] == id) p->slot[k] = g_pt_base.slot[k] == id ? (i8)id : g_pt_base.slot[k];
                player_recalc(); sfx_menu();
            } else msg("ONLY POINTS SPENT JUST NOW CAN BE TAKEN BACK", RGB(200, 200, 200));
        }
        if (pressed(BTN_Y)) {
            if (G.cur_y == 7 || (id >= 0 && p->sk[id] && !SKILLS[id].passive)) { G.assign = 1; G.sub = 0; sfx_menu(); }
            else msg("LEARN AN ACTIVE SKILL FIRST", RGB(200, 200, 200));
        }
        break; }
    case TAB_HERO:
        if (pressed(BTN_UP)) G.sel = (G.sel + 3) % 4;
        if (pressed(BTN_DOWN)) G.sel = (G.sel + 1) % 4;
        G.sel = MIN(G.sel, 3);
        if (pressed(BTN_A) || pressed(BTN_RIGHT)) {   // add a point (provisional until confirmed)
            if (p->unspent_attr > 0) { pt_begin(); p->attr[G.sel]++; p->unspent_attr--; player_recalc(); sfx_menu(); }
            else msg("NO ATTRIBUTE POINTS - LEVEL UP!", RGB(255, 140, 120));
        }
        if (pressed(BTN_X) || pressed(BTN_LEFT)) {   // take one back
            if (g_pt_on && p->attr[G.sel] > g_pt_base.attr[G.sel]) { p->attr[G.sel]--; p->unspent_attr++; player_recalc(); sfx_menu(); }
        }
        break;
    case TAB_GEMS:   // look only: combining and socketing happen at the jeweler
        if (pressed(BTN_LEFT)) G.cur_x = (G.cur_x + G_TIERS - 1) % G_TIERS;
        if (pressed(BTN_RIGHT)) G.cur_x = (G.cur_x + 1) % G_TIERS;
        if (pressed(BTN_UP)) G.cur_y = (G.cur_y + G_TYPES - 1) % G_TYPES;
        if (pressed(BTN_DOWN)) G.cur_y = (G.cur_y + 1) % G_TYPES;
        break;
    case TAB_SYS: {
        int n = g_sys_page == 0 ? SYS_MAIN_N : g_sys_page == 1 ? SYS_GFX_N : SYS_CTL_N;
        if (pressed(BTN_UP)) G.sel = (G.sel + n - 1) % n;
        if (pressed(BTN_DOWN)) G.sel = (G.sel + 1) % n;
        if (!pressed(BTN_A)) break;
        if (g_sys_page == 0) {
            if (G.sel == 0 && !G.in_town) { G.portal_floor = G.floor; sfx_stairs(); start_fade(0); }
            else if (G.sel == 0) msg("YOU ARE ALREADY IN TOWN", RGB(200, 200, 200));
            else if (G.sel == 1) { game_save(); open_door(G.slot_cur, G.char_cur); sfx_menu(); }
            else if (G.sel == 2) { game_save(); G.state = ST_TITLE; G.quit_t = 1; sfx_menu(); }
            else if (G.sel == 3) { g_sys_page = 1; G.sel = 0; sfx_menu(); }
            else if (G.sel == 4) { g_sys_page = 2; G.sel = 0; sfx_menu(); }
            else G.state = ST_PLAY;
        } else if (g_sys_page == 1) {
            if (G.sel == 0) g_gfx_fast ^= 1;
            else if (G.sel == 1) g_gfx_effects ^= 1;
            else if (G.sel == 2) g_show_perf ^= 1;
            else { g_sys_page = 0; G.sel = 3; }
            settings_changed();
            sfx_menu();
        } else {
            if (G.sel < ACT_N) { g_rebind = G.sel; sfx_menu(); }
            else if (G.sel == ACT_N) { Settings *t = settings(); settings_default_buttons(t); settings_changed(); sfx_equip(); msg("BUTTONS RESET", RGB(200, 220, 255)); }
            else { g_sys_page = 0; G.sel = 4; sfx_menu(); }
        }
        break; }
    }
}

// ---------- NPCs ----------
static void update_npc(void) {
    Player *p = &G.p;
    if (G.npc == NPC_JEWELER && (G.sub == 1 || G.sub == 2)) {   // gem picker, then "set this gem?" question
        int have[N_GEMS], n = 0; for (int i = 0; i < N_GEMS; i++) if (G.gems[i]) have[n++] = i;
        if (n == 0) { G.sub = 0; return; }
        G.sel = MIN(G.sel, n - 1);
        int eq, slot; Item *it = cursor_item(&eq, &slot);
        if (G.sub == 2) {
            if (pressed(BTN_B)) { G.sub = 1; sfx_menu(); return; }
            if (pressed(BTN_A)) {
                if (it && it->used) for (int g = 0; g < it->sockets; g++) if (!it->gem[g]) {
                    it->gem[g] = (u8)(have[G.sel] + 1); G.gems[have[G.sel]]--; sfx_equip(); player_recalc(); break;
                }
                G.sub = 0;
            }
            return;
        }
        if (pressed(BTN_B)) { G.sub = 0; return; }
        if (pressed(BTN_UP)) G.sel = (G.sel + n - 1) % n;
        if (pressed(BTN_DOWN)) G.sel = (G.sel + 1) % n;
        if (pressed(BTN_A)) {
            int free_s = 0; if (it && it->used) for (int g = 0; g < it->sockets; g++) free_s += !it->gem[g];
            if (free_s) { G.sub = 2; sfx_menu(); } else { msg("NO EMPTY SOCKET", RGB(255, 140, 120)); G.sub = 0; }
        }
        return;
    }
    if (G.npc == NPC_TRAINER && G.sub && pressed(BTN_B)) { G.sub = 0; sfx_menu(); return; }
    if (pressed(BTN_B) || pressed(BTN_START)) { G.state = ST_PLAY; sfx_menu(); game_save(); return; }
    int eq, slot; Item *it;
    switch (G.npc) {
    case NPC_MERCHANT:
        nav_two_grid(2);
        if (pressed(BTN_A)) {
            if (G.cur_region == 0) {
                int i = G.cur_y * BAG_COLS + G.cur_x;
                if (i < 2) { int *c = i ? &p->mpot : &p->hpot; if (*c >= 20) msg("YOU CAN'T CARRY MORE", RGB(200, 200, 200)); else if (spend(potion_price())) (*c)++; }
                else if (G.shop[i].used) {
                    int f = bag_free();
                    if (f < 0) msg("BAG FULL", RGB(255, 120, 120));
                    else if (spend(buy_price(&G.shop[i]))) { G.bag[f] = G.shop[i]; G.shop[i].used = 0; }
                }
            } else {
                Item *b = &G.bag[G.cur_y * BAG_COLS + G.cur_x];
                if (b->used) {
                    for (int g = 0; g < b->sockets; g++) if (b->gem[g]) G.gems[b->gem[g] - 1]++;
                    p->gold += (u32)item_value(b); sfx_gold();
                    for (int i = 2; i < SHOP_N; i++) if (!G.shop[i].used) { G.shop[i] = *b; break; }   // buyback
                    b->used = 0;
                }
            }
        }
        break;
    case NPC_SMITH:
        nav_doll_bag(); it = cursor_item(&eq, &slot);
        if (!it || !it->used) break;
        if (pressed(BTN_A)) {
            if (it->naff >= MAX_AFF) msg("THIS ITEM HOLDS NO MORE ENCHANTMENTS", RGB(200, 200, 200));
            else if (spend(enchant_cost(it))) { item_add_affixes(it, it->naff + 1); if (it->naff == 1) it->pre = it->at[0]; if (it->naff == 2) it->suf = it->at[1]; msg("ENCHANTED!", RGB(120, 160, 255)); player_recalc(); }
        }
        if (pressed(BTN_Y)) {
            if (!it->naff) msg("NOTHING TO REFORGE - ENCHANT FIRST", RGB(200, 200, 200));
            else if (spend(reforge_cost(it))) { int n = it->naff; it->naff = 0; item_add_affixes(it, n); it->pre = it->at[0]; if (n > 1) it->suf = it->at[1]; msg("REFORGED!", RGB(120, 160, 255)); player_recalc(); }
        }
        if (pressed(BTN_X)) {
            if (it->rarity >= R_EPIC || it->uniq) msg("ONLY COMMON TO ULTRA RARE CAN BE UPGRADED", RGB(200, 200, 200));
            else if (spend(upgrade_cost(it))) {
                it->rarity++; item_base_stats(it);
                for (int a = 0; a < it->naff; a++) it->av[a] = (i16)(it->av[a] * 115 / 100 + 1);
                item_add_affixes(it, it->rarity == R_EPIC ? 3 : AFFIX_COUNT_BY_RARITY[it->rarity]);
                it->pre = it->at[0]; if (it->naff > 1) it->suf = it->at[1];
                sfx_rare_drop(it->rarity >= R_EPIC ? 3 : 3); msg("UPGRADED!", rarity_col(it->rarity)); player_recalc();
            }
        }
        break;
    case NPC_JEWELER:
        if (pressed(BTN_L) || pressed(BTN_R)) { G.npc_tab ^= 1; G.sel = 0; sfx_menu(); break; }
        if (G.npc_tab == 0) {
            nav_doll_bag(); it = cursor_item(&eq, &slot);
            if (!it || !it->used) break;
            if (pressed(BTN_A)) {
                if (it->sockets >= max_sockets(it)) msg("NO ROOM FOR MORE SOCKETS", RGB(200, 200, 200));
                else if (spend(socket_cost(it))) { it->gem[it->sockets] = 0; it->sockets++; msg("SOCKET ADDED", RGB(200, 240, 255)); }
            }
            if (pressed(BTN_Y)) {
                int empty = 0; for (int g = 0; g < it->sockets; g++) if (!it->gem[g]) empty = 1;
                int any = 0; for (int i = 0; i < N_GEMS; i++) if (G.gems[i]) any = 1;
                if (!empty) msg("NO EMPTY SOCKET", RGB(200, 200, 200)); else if (!any) msg("YOU HAVE NO GEMS", RGB(200, 200, 200)); else { G.sub = 1; G.sel = 0; }
            }
            if (pressed(BTN_X)) {
                int any = 0; for (int g = 0; g < it->sockets; g++) if (it->gem[g]) any = 1;
                if (!any) msg("NO GEMS TO REMOVE", RGB(200, 200, 200));
                else if (spend(unsocket_cost(it))) { for (int g = 0; g < it->sockets; g++) if (it->gem[g]) { G.gems[it->gem[g] - 1]++; it->gem[g] = 0; } player_recalc(); }
            }
        } else {
            int mx = pressed(BTN_RIGHT) - pressed(BTN_LEFT), my = pressed(BTN_DOWN) - pressed(BTN_UP);
            G.cur_x = CLAMP(G.cur_x + mx, 0, G_TIERS - 2); G.cur_y = CLAMP(G.cur_y + my, 0, G_TYPES - 1);
            if (pressed(BTN_A)) {
                int id = G.cur_y * G_TIERS + G.cur_x;
                if (G.gems[id] < 3) msg("NEED 3 GEMS TO COMBINE", RGB(200, 200, 200));
                else if (spend(combine_cost(G.cur_x))) { G.gems[id] -= 3; G.gems[id + 1]++; sfx_rare_drop(3); }
            }
        }
        break;
    case NPC_TRAINER:
        if (G.sub) {   // choosing a new class
            if (pressed(BTN_LEFT)) { G.sel = (G.sel + CL_COUNT - 1) % CL_COUNT; if (G.sel == p->cls) G.sel = (G.sel + CL_COUNT - 1) % CL_COUNT; sfx_menu(); }
            if (pressed(BTN_RIGHT)) { G.sel = (G.sel + 1) % CL_COUNT; if (G.sel == p->cls) G.sel = (G.sel + 1) % CL_COUNT; sfx_menu(); }
            if (pressed(BTN_A)) {
                if (spend(class_change_cost())) { change_class(G.sel); G.sub = 0; }
            }
            break;
        }
        if (pressed(BTN_A)) { if (spend(respec_cost())) { respec(); msg("YOUR MIND IS CLEAR. SPEND YOUR POINTS ANEW", RGB(160, 220, 255)); } }
        if (pressed(BTN_X)) { G.sub = 1; G.sel = (p->cls + 1) % CL_COUNT; sfx_menu(); }
        break;
    case NPC_STASH:
        nav_two_grid(3);
        if (pressed(BTN_A)) {
            Item *a = G.cur_region == 0 ? &G.stash[G.cur_y * BAG_COLS + G.cur_x] : &G.bag[G.cur_y * BAG_COLS + G.cur_x];
            if (a->used) {
                Item *dst = 0;
                if (G.cur_region == 0) { int f = bag_free(); if (f >= 0) dst = &G.bag[f]; }
                else for (int i = 0; i < STASH_N; i++) if (!G.stash[i].used) { dst = &G.stash[i]; break; }
                if (!dst) msg("NO ROOM", RGB(255, 120, 120)); else { *dst = *a; a->used = 0; sfx_equip(); }
            }
        }
        break;
    }
}

static int waypoint_list(int *out) {
    int n = 0; out[n++] = 1;
    for (int w = 6; w <= G.waypoint && n < 10; w += 5) out[n++] = w;
    return n;
}
static void update_waypoint(void) {
    int list[12], n = waypoint_list(list);
    if (pressed(BTN_B) || pressed(BTN_START)) { G.state = ST_PLAY; return; }
    if (pressed(BTN_UP)) G.sel = (G.sel + n - 1) % n;
    if (pressed(BTN_DOWN)) G.sel = (G.sel + 1) % n;
    if (pressed(BTN_A)) { sfx_stairs(); G.state = ST_PLAY; start_fade(list[G.sel]); }
}

// ===================== drawing =====================
static void panel(void) { fill_rect(0, 0, SCR_W, SCR_H, RGB(10, 8, 12)); rect_outline(1, 1, SCR_W - 2, SCR_H - 2, RGB(110, 80, 50)); }
static void gold_line(void) { Str s; s_reset(&s); s_add(&s, "GOLD "); s_int(&s, (i32)G.p.gold); text(SCR_W - 6 - text_w(s.b), 4, s.b, pal(30)); }
static void footer(const char *s) { hline(4, SCR_H - 13, SCR_W - 8, RGB(60, 46, 34)); text(6, SCR_H - 9, s, RGB(170, 160, 150)); }
static void msg_line_x(int x, int y) { if (G.msg_t[0] > 0) text_shadow(x, y, G.msg[0], G.msg_col[0]); }
static void msg_line(int y) { msg_line_x(6, y); }

// potion pouch: elixirs carried (all strengths together), the selected one outlined
static void draw_pouch(int x, int y) {
    text(x, y + 6, "POUCH", RGB(200, 180, 140));
    for (int k = 0; k < EL_TYPES; k++) {
        int bx = x + 34 + k * 40, n = 0, sel = G.cur_region == 2 && G.cur_x == k;
        for (int t = 0; t < EL_TIERS; t++) n += G.elix[k][t];
        fill_rect(bx, y, 36, 19, sel ? RGB(60, 44, 30) : RGB(22, 18, 26)); rect_outline(bx, y, 36, 19, sel ? RGB(255, 220, 120) : RGB(50, 44, 54));
        blit_ex(&SPR_g_potion, bx + 4, y + 4, g_shade[n ? 7 : 3], g_remap_elix[k], 0, 1);
        Str s; s_reset(&s); s_chr(&s, 'X'); s_int(&s, n); text(bx + 15, y + 6, s.b, n ? RGB(255, 255, 255) : RGB(90, 80, 86));
        if (G.buff_t[k] > 0) fill_rect(bx + 2, y + 16, 32 * G.buff_t[k] / MAX(1, EL_SECS[G.buff_tier[k]] * TICKS_PER_SEC), 1, RGB(120, 255, 120));
    }
}
static void draw_doll_bag(int top) {
    for (int r = 0; r < 4; r++) for (int c = 0; c < 3; c++) {
        int sl = DOLL_GRID[r][c]; if (sl < 0) continue;
        draw_slot_box(8 + c * 30, top + r * 26, 22, &G.equip[sl], G.cur_region == 0 && G.cur_x == c && G.cur_y == r, sl);
    }
    for (int r = 0; r < BAG_ROWS; r++) for (int c = 0; c < BAG_COLS; c++)
        draw_slot_box(110 + c * 19, top + r * 19, 17, &G.bag[r * BAG_COLS + c], G.cur_region == 1 && G.cur_x == c && G.cur_y == r, -1);
}
// What the hero would end up with: tried for real (equip, recalc, look, put everything back)
#define HS_N 14
static const char *HS_NAME[HS_N] = {"DAMAGE", "ARMOR", "LIFE", "MANA", "CRIT CHANCE", "ATTACK SPEED", "MOVE SPEED", "LIFE STEAL",
                                    "MAGIC FIND", "LIFE REGEN", "MANA REGEN", "FIRE DAMAGE", "BLOCK", "SPELL POWER"};
static const u8 HS_PCT[HS_N] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1};
static void hero_stats(int v[HS_N]) {
    Player *p = &G.p;
    v[0] = (p->wmin + p->wmax) * p->mult[basic_school()] / 200; v[1] = p->armor; v[2] = p->hpmax; v[3] = p->mpmax;
    v[4] = p->crit; v[5] = p->aspd; v[6] = p->move; v[7] = p->leech; v[8] = p->mf; v[9] = p->regen; v[10] = p->mregen;
    v[11] = p->fire; v[12] = p->block; v[13] = p->mult[SC_SPELL] - 100;
}
// returns 0 = can be equipped (before/after filled in), else a reason
static const char *equip_preview(const Item *it, int before[HS_N], int after[HS_N], const Item **replaces) {
    int ic = item_class(it->base);
    if (ic >= 0 && ic != G.p.cls) return "YOUR CLASS CAN'T USE THIS";
    int s = equip_target_slot(it);
    if (BASE_SLOT[it->base] == S_RING1 && G.equip[S_RING1].used && G.equip[S_RING2].used) s = S_RING1;
    if (s == S_OFF && G.equip[S_MAIN].used && G.equip[S_MAIN].base == B_BOW) return "BOWS NEED BOTH HANDS";
    *replaces = G.equip[s].used ? &G.equip[s] : 0;
    Player save = G.p; Item keep = G.equip[s], keep_off = G.equip[S_OFF];
    hero_stats(before);
    G.equip[s] = *it;
    if (s == S_MAIN && it->base == B_BOW) G.equip[S_OFF].used = 0;
    player_recalc(); hero_stats(after);
    G.equip[s] = keep; G.equip[S_OFF] = keep_off; G.p = save;
    return 0;
}
static void draw_item_tooltip(int y, int compare) {
    hline(4, y - 4, SCR_W - 8, RGB(80, 60, 40));
    int eq, slot; Item *it = cursor_item(&eq, &slot);
    if (!it || !it->used) { text(6, y, "EMPTY", RGB(90, 86, 90)); return; }
    if (!compare || eq) { item_lines(it, 6, y, SCR_H - 24); return; }
    // left: the item itself; right: what changes for your hero if you put it on
    Str s; item_name(it, &s); text_shadow(6, y, s.b, rarity_col(it->rarity));
    Item shown = *it; shown.uniq = 0;
    { // the item's own stats (name already drawn above, so skip its first line)
        int yy = y + LH;
        s_reset(&s); s_add(&s, RARITY_NAME[it->rarity]); s_chr(&s, ' '); if (!majestic_icon(it)) { s_add(&s, BAND_NAME[item_band(it)]); s_chr(&s, ' '); } s_add(&s, BASE_KIND[it->base]); text(6, yy, s.b, RGB(150, 140, 130)); yy += LH;
        if (it->dmax) { s_reset(&s); s_add(&s, "DAMAGE "); s_int(&s, it->dmin); s_chr(&s, '-'); s_int(&s, it->dmax); text(6, yy, s.b, RGB(255, 255, 255)); yy += LH; }
        if (it->armor) { s_reset(&s); s_add(&s, "ARMOR "); s_int(&s, it->armor); text(6, yy, s.b, RGB(255, 255, 255)); yy += LH; }
        for (int a = 0; a < it->naff && yy < SCR_H - 24; a++) {
            s_reset(&s); s_chr(&s, '+'); s_int(&s, it->av[a]); if (AFFIX_FMT[it->at[a]][0] != '%') s_chr(&s, ' '); s_add(&s, AFFIX_FMT[it->at[a]]);
            text(6, yy, s.b, RGB(120, 160, 255)); yy += LH;
        }
        for (int g = 0; g < it->sockets && yy < SCR_H - 24; g++) { text(6, yy, it->gem[g] ? "SOCKETED GEM" : "EMPTY SOCKET", it->gem[g] ? RGB(200, 240, 255) : RGB(100, 96, 100)); yy += LH; }
        if (it->uniq && yy < SCR_H - 24) text(6, yy, UNIQUES[it->uniq - 1].desc, rarity_col(it->rarity));
    }
    (void)shown;
    int x = 138, yy = y + LH;
    vline(x - 5, y + LH, SCR_H - 18 - (y + LH), RGB(60, 48, 40));
    int before[HS_N], after[HS_N]; const Item *rep = 0;
    const char *why = equip_preview(it, before, after, &rep);
    if (why) { text(x, yy, "CAN'T EQUIP:", RGB(255, 120, 120)); text(x, yy + LH, why, RGB(255, 160, 140)); return; }
    text(x, yy, "IF YOU EQUIP IT:", RGB(255, 230, 160)); yy += LH + 2;
    int shown_n = 0;
    for (int k = 0; k < HS_N && yy < SCR_H - 34; k++) {
        int d = after[k] - before[k]; if (!d) continue;
        if (k == 13 && G.p.cls != CL_MAGE) continue;   // spell power only matters to mages
        s_reset(&s); s_add(&s, d > 0 ? "+" : ""); s_int(&s, d); if (HS_PCT[k]) s_chr(&s, '%'); s_chr(&s, ' '); s_add(&s, HS_NAME[k]);
        text(x, yy, s.b, d > 0 ? RGB(90, 240, 90) : RGB(255, 90, 80)); yy += LH; shown_n++;
    }
    if (!shown_n) { text(x, yy, "NO CHANGE", RGB(170, 160, 150)); yy += LH; }
    // what it replaces
    s_reset(&s);
    if (rep) { Str n; item_name(rep, &n); s_add(&s, "REPLACES: "); text(x, SCR_H - 31, "REPLACES:", RGB(150, 140, 130));
               char cut[26]; int i = 0; for (; n.b[i] && i < 25; i++) cut[i] = n.b[i]; cut[i] = 0; text(x, SCR_H - 22, cut, rarity_col(rep->rarity)); }
    else text(x, SCR_H - 22, "GOES IN AN EMPTY SLOT", RGB(150, 140, 130));
}

// all gems: one row per kind, one column per quality; the selected one is described underneath
static void draw_gem_grid(int y0) {
    Str s;
    for (int t = 0; t < G_TYPES; t++) {
        text(6, y0 + 4 + t * 18, GEM_TYPE_NAME[t], RGB(200, 200, 200));
        for (int k = 0; k < G_TIERS; k++) {
            int x = 60 + k * 40, y = y0 + t * 18, id = t * G_TIERS + k, sel = G.cur_y == t && G.cur_x == k;
            fill_rect(x, y, 36, 15, sel ? RGB(60, 44, 30) : RGB(22, 18, 26)); rect_outline(x, y, 36, 15, sel ? RGB(255, 220, 120) : RGB(50, 44, 54));
            draw_gem_icon(id, x + 3, y + 4);
            s_reset(&s); s_chr(&s, 'X'); s_int(&s, G.gems[id]); text(x + 13, y + 5, s.b, G.gems[id] ? RGB(255, 255, 255) : RGB(80, 74, 80));
        }
    }
    text(60, y0 + 110, "CHIPPED  FLAWED  NORMAL  FLAWLESS PERFECT", RGB(120, 110, 100));
    int id = G.cur_y * G_TIERS + G.cur_x; gem_name(id, &s);
    text_shadow(6, y0 + 124, s.b, RGB(200, 240, 255));
    s_reset(&s); s_chr(&s, '+'); s_int(&s, GEM_VAL[G.cur_y][G.cur_x]); s_chr(&s, ' '); s_add(&s, GEM_STAT[G.cur_y]); s_add(&s, " WHEN SOCKETED"); text(6, y0 + 134, s.b, RGB(200, 200, 200));
}
static void draw_menu_body(void);
static void draw_menu(void) {
    draw_menu_body();
    if (g_pt_ask) {   // keep the points just spent?
        Player *p = &G.p, *b = &g_pt_base; Str s;
        int na = b->unspent_attr - p->unspent_attr, ns = b->unspent_skill - p->unspent_skill;
        int bx = 33, by = 70, bw = 200, bh = 76;
        fill_rect(bx + 3, by + 3, bw, bh, RGB(0, 0, 0));
        fill_rect(bx, by, bw, bh, RGB(26, 20, 28)); rect_outline(bx, by, bw, bh, RGB(120, 255, 120));
        text_c(SCR_W / 2, by + 6, "KEEP THESE CHANGES?", RGB(180, 255, 180));
        int y = by + 18;
        if (na) { s_reset(&s); s_int(&s, na); s_add(&s, na == 1 ? " ATTRIBUTE POINT" : " ATTRIBUTE POINTS"); text_c(SCR_W / 2, y, s.b, RGB(230, 230, 230)); y += LH; }
        for (int i = 0; i < 4 && na; i++) if (p->attr[i] != b->attr[i]) { s_reset(&s); s_chr(&s, '+'); s_int(&s, p->attr[i] - b->attr[i]); s_chr(&s, ' '); s_add(&s, ATTR_NAME[i]); text_c(SCR_W / 2, y, s.b, RGB(120, 255, 120)); y += LH; }
        if (ns) { s_reset(&s); s_int(&s, ns); s_add(&s, ns == 1 ? " SKILL POINT" : " SKILL POINTS"); text_c(SCR_W / 2, y, s.b, RGB(230, 230, 230)); y += LH; }
        for (int i = 0; i < NSK && ns && y < by + bh - 18; i++) if (p->sk[i] != b->sk[i]) { s_reset(&s); s_add(&s, SKILLS[i].name); s_add(&s, " RANK "); s_int(&s, p->sk[i]); text_c(SCR_W / 2, y, s.b, RGB(120, 255, 120)); y += LH; }
        text_c(SCR_W / 2, by + bh - 12, "A KEEP   X UNDO   B BACK", RGB(230, 230, 230));
    }
}
static void draw_menu_body(void) {
    panel(); Player *p = &G.p; Str s;
    // tabs
    int x = 6;
    text(x, 4, "L<", RGB(120, 110, 100)); x += 12;
    for (int i = 0; i < TAB_N; i++) {
        int w = text_w(TAB_NAME[i]) + 6;
        if (i == G.tab) fill_rect(x - 2, 2, w, 9, RGB(90, 60, 30));
        text(x + 1, 4, TAB_NAME[i], i == G.tab ? RGB(255, 230, 160) : RGB(150, 140, 130));
        x += w + 2;
    }
    text(x, 4, ">R", RGB(120, 110, 100));
    gold_line();
    if (G.tab == TAB_BAG) {
        draw_doll_bag(16);
        s_reset(&s); s_add(&s, "DMG "); s_int(&s, p->wmin * p->mult[basic_school()] / 100); s_chr(&s, '-'); s_int(&s, p->wmax * p->mult[basic_school()] / 100);
        s_add(&s, "  ARMOR "); s_int(&s, p->armor); s_add(&s, "  LIFE "); s_int(&s, p->hpmax);
        text(110, 76, s.b, RGB(200, 200, 200));
        draw_pouch(110, 90);
        int eq, sl; cursor_item(&eq, &sl);
        if (G.cur_region == 2) {   // what the selected elixir does
            int k = G.cur_x; Str t;
            hline(4, 122, SCR_W - 8, RGB(80, 60, 40));
            s_reset(&t); s_add(&t, "ELIXIR OF "); s_add(&t, EL_NAME[k]); text_shadow(6, 126, t.b, pal(EL_REMAP[k][0] == 15 ? 15 : EL_REMAP[k][0]));
            text(6, 136, "DRINK FOR A BOOST THAT LASTS A WHILE.", RGB(170, 160, 150));
            text(6, 145, "THE STRONGEST ONE YOU CARRY IS USED FIRST.", RGB(170, 160, 150));
            for (int q = 0; q < EL_TIERS; q++) {
                int yy = 158 + q * 9;
                s_reset(&t); s_add(&t, EL_TIER_NAME[q][0] ? EL_TIER_NAME[q] : "NORMAL "); text(6, yy, t.b, G.elix[k][q] ? RGB(230, 220, 200) : RGB(90, 80, 86));
                s_reset(&t); s_chr(&t, 'X'); s_int(&t, G.elix[k][q]); text(60, yy, t.b, G.elix[k][q] ? RGB(255, 255, 255) : RGB(90, 80, 86));
                s_reset(&t); s_chr(&t, '+'); s_int(&t, EL_VAL[k][q]); s_add(&t, EL_STAT[k]); s_add(&t, "  "); s_int(&t, EL_SECS[q]); s_add(&t, " SEC");
                text(84, yy, t.b, G.elix[k][q] ? RGB(150, 210, 255) : RGB(90, 80, 86));
            }
            msg_line_x(110, 112);
            footer("A DRINK   B CLOSE   L/R TABS");
        } else {
            msg_line_x(110, 112);
            draw_item_tooltip(126, 1);
            footer(eq ? "A UNEQUIP   B CLOSE   L/R TABS" : "A EQUIP   Y SALVAGE   B CLOSE   L/R TABS");
        }
        if (g_salv_ask) {
            Item *it = cursor_item(&eq, &sl);
            if (!it || !it->used || eq) g_salv_ask = 0;
            else {
                int gems = 0; for (int g = 0; g < it->sockets; g++) gems += it->gem[g] != 0;
                int bx = 33, by = 78, bw = 200, bh = gems ? 70 : 60;
                fill_rect(bx + 3, by + 3, bw, bh, RGB(0, 0, 0));
                fill_rect(bx, by, bw, bh, RGB(26, 20, 28)); rect_outline(bx, by, bw, bh, RGB(255, 210, 120));
                text_c(SCR_W / 2, by + 6, "SALVAGE THIS ITEM?", RGB(255, 230, 160));
                item_name(it, &s); text_c(SCR_W / 2, by + 18, s.b, rarity_col(it->rarity));
                s_reset(&s); s_add(&s, "YOU GET "); s_int(&s, item_value(it)); s_add(&s, " GOLD"); text_c(SCR_W / 2, by + 30, s.b, pal(30));
                if (gems) { s_reset(&s); s_add(&s, "+ "); s_int(&s, gems); s_add(&s, gems == 1 ? " SOCKETED GEM BACK" : " SOCKETED GEMS BACK"); text_c(SCR_W / 2, by + 40, s.b, RGB(200, 240, 255)); }
                text_c(SCR_W / 2, by + bh - 12, "A SALVAGE     B KEEP IT", RGB(230, 230, 230));
            }
        }
    } else if (G.tab == TAB_GEMS) {
        int ng = 0; for (int i = 0; i < N_GEMS; i++) ng += G.gems[i];
        s_reset(&s); s_add(&s, "GEMS CARRIED: "); s_int(&s, ng); text(6, 13, s.b, RGB(200, 240, 255));
        draw_gem_grid(24);
        footer("JEWELER: SOCKET & COMBINE   B CLOSE   L/R TABS");
    } else if (G.tab == TAB_SKILLS) {
        s_reset(&s); s_add(&s, "SKILL POINTS: "); s_int(&s, p->unspent_skill);
        text(6, 13, s.b, p->unspent_skill ? RGB(120, 255, 120) : RGB(170, 160, 150));
        for (int c = 0; c < 3; c++) {
            int cx = 4 + c * 87;
            text_c(cx + 42, 23, TREE_NAME[c], c != p->cls ? RGB(90, 80, 86) : c == 0 ? RGB(255, 130, 100) : c == 1 ? RGB(130, 230, 130) : RGB(130, 170, 255));
            for (int r = 0; r < 7; r++) {
                int id = c * 7 + r, y = 31 + r * 15, sel = G.cur_x == c && G.cur_y == r;
                int locked = p->level < SKILLS[id].req, other = c != p->cls;
                fill_rect(cx, y, 84, 14, sel ? RGB(60, 44, 30) : RGB(22, 18, 26));
                rect_outline(cx, y, 84, 14, sel ? RGB(255, 220, 120) : p->sk[id] ? RGB(120, 100, 70) : RGB(50, 44, 54));
                u16 nc = other ? RGB(70, 64, 70) : locked ? RGB(80, 74, 80) : p->sk[id] ? RGB(255, 255, 255) : RGB(170, 160, 150);
                text(cx + 3, y + 1, SKILLS[id].name, nc);
                s_reset(&s);
                if (locked) { s_add(&s, "LV"); s_int(&s, SKILLS[id].req); }
                else { s_int(&s, p->sk[id]); s_chr(&s, '/'); s_int(&s, SK_MAX_RANK); }
                if (SKILLS[id].passive) s_add(&s, " P");
                text_small(cx + 82 - text_small_w(s.b), y + 8, s.b, locked ? RGB(110, 85, 85) : RGB(200, 180, 120));   // rank / level badge under the name
                for (int k = 0; k < 4; k++) if (p->slot[k] == id) text_small(cx + 3, y + 8, act_key_short(ACT_SK1 + k), RGB(255, 210, 120));
            }
        }
        // potions row, elixirs row
        for (int k = 0; k < 2; k++) {
            int cx = 4 + k * 87, y = 31 + 7 * 15, sel = G.cur_y == 7 && G.cur_x == k;
            fill_rect(cx, y, 84, 12, sel ? RGB(60, 44, 30) : RGB(22, 18, 26)); rect_outline(cx, y, 84, 12, sel ? RGB(255, 220, 120) : RGB(50, 44, 54));
            text(cx + 3, y + 2, k ? "MANA POTION" : "HEALTH POTION", k ? RGB(130, 160, 255) : RGB(255, 130, 130));
            for (int q = 0; q < 4; q++) if (p->slot[q] == (k ? SLOT_MPOT : SLOT_HPOT)) text_small(cx + 82 - text_small_w(act_key_short(ACT_SK1 + q)), y + 4, act_key_short(ACT_SK1 + q), RGB(255, 210, 120));
        }
        // slots bar
        int y = 152;
        hline(4, y - 3, SCR_W - 8, RGB(80, 60, 40));
        s_reset(&s);
        for (int i = 0; i < 4; i++) {
            int sl = p->slot[i]; s_add(&s, act_key(ACT_SK1 + i)); s_chr(&s, ':');
            s_add(&s, sl >= 0 && sl < NSK ? SKILLS[sl].abbr : sl == SLOT_HPOT ? "HP" : sl == SLOT_MPOT ? "MP" : "-"); s_add(&s, "  ");
        }
        text(6, y, s.b, RGB(255, 210, 120));
        if (G.assign) {
            text(6, y + 12, "ASSIGN TO WHICH BUTTON? PRESS X / Y / L / R", RGB(120, 255, 120));
            text(6, y + 21, "(OR LEFT/RIGHT + A)   B CANCEL", RGB(170, 160, 150));
            text(6 + G.sub * 7 * CH_W, y + 8, "^", RGB(255, 255, 255));
        } else if (G.cur_y < 7) {
            int id = skill_at(G.cur_x, G.cur_y); const SkillDef *d = &SKILLS[id];
            text_shadow(6, y + 10, d->name, RGB(255, 230, 160));
            text(6, y + 19, d->desc, RGB(200, 200, 200));
            s_reset(&s);
            int r = p->sk[id];
            if (r) { s_add(&s, "NOW: "); s_int(&s, skill_val(id)); s_add(&s, d->unit); s_add(&s, "   "); }
            if (r < SK_MAX_RANK) { int nv = d->passive ? d->per * (r + 1) : d->base + d->per * r; s_add(&s, "NEXT: "); s_int(&s, nv); s_add(&s, d->unit); }
            text(6, y + 28, s.b, RGB(140, 200, 255));
            s_reset(&s);
            if (!d->passive) { int t10 = (skill_cd_r(id, r) * 10 + 26) / 52; s_add(&s, "MANA "); s_int(&s, skill_cost_r(id, r)); s_add(&s, "  CD "); s_int(&s, t10 / 10); s_chr(&s, '.'); s_int(&s, t10 % 10); s_add(&s, "S  "); }
            s_add(&s, d->tree != p->cls ? (d->tree == 0 ? "WARRIOR ONLY" : d->tree == 1 ? "RANGER ONLY" : "MAGE ONLY") : d->tree == 0 ? "NEEDS SWORD" : d->tree == 1 ? "NEEDS BOW" : "STAFF +25%");
            text(6, y + 37, s.b, RGB(170, 160, 150));
            s_reset(&s); s_add(&s, "REQUIRES LEVEL "); s_int(&s, d->req); text(6, y + 46, s.b, p->level >= d->req ? RGB(120, 200, 120) : RGB(255, 120, 120));
        } else text(6, y + 12, "POTIONS CAN GO ON ANY SKILL BUTTON", RGB(200, 200, 200));
        msg_line(y + 56);
        footer(G.assign ? "SLOT BUTTON OR LEFT/RIGHT + A   B CANCEL" : g_pt_on ? "A ADD   X TAKE BACK   Y ASSIGN   B DONE" : "A LEARN   Y ASSIGN BUTTON   B CLOSE   L/R TABS");
    } else if (G.tab == TAB_HERO) {
        s_reset(&s); s_add(&s, "LEVEL "); s_int(&s, p->level); s_add(&s, "   XP "); s_int(&s, (i32)p->xp); s_chr(&s, '/'); s_int(&s, (i32)p->xpnext);
        text(6, 16, s.b, RGB(255, 230, 160));
        s_reset(&s); s_add(&s, "ATTRIBUTE POINTS: "); s_int(&s, p->unspent_attr);
        text(6, 26, s.b, p->unspent_attr ? RGB(120, 255, 120) : RGB(170, 160, 150));
        int vals[4] = {p->str, p->dex, p->intl, p->vit};
        for (int i = 0; i < 4; i++) {
            int y = 38 + i * 18, sel = G.sel == i;
            fill_rect(6, y, 120, 15, sel ? RGB(60, 44, 30) : RGB(22, 18, 26)); rect_outline(6, y, 120, 15, sel ? RGB(255, 220, 120) : RGB(50, 44, 54));
            text(10, y + 1, ATTR_NAME[i], RGB(255, 255, 255));
            int add = g_pt_on ? p->attr[i] - g_pt_base.attr[i] : 0;
            s_reset(&s); s_int(&s, vals[i]); text(122 - text_w(s.b), y + 1, s.b, add ? RGB(120, 255, 120) : RGB(255, 230, 160));
            if (add) { s_reset(&s); s_chr(&s, '+'); s_int(&s, add); text_small(122 - text_w(s.b) - 4 - text_small_w(s.b), y + 3, s.b, RGB(120, 255, 120)); }
            text_small(10, y + 9, ATTR_DESC[i], RGB(150, 140, 130));
        }
        int y = 38, x2 = 140;
#define ROW(label, val) do { text(x2, y, label, RGB(170, 160, 150)); s_reset(&s); val; text(x2 + 62, y, s.b, RGB(255, 255, 255)); y += LH; } while (0)
        ROW("LIFE", (s_int(&s, p->hpmax)));
        ROW("MANA", (s_int(&s, p->mpmax)));
        ROW("MELEE DMG", (s_int(&s, p->wmin * p->mult[0] / 100), s_chr(&s, '-'), s_int(&s, p->wmax * p->mult[0] / 100)));
        ROW("BOW DMG", (s_int(&s, p->wmin * p->mult[1] / 100), s_chr(&s, '-'), s_int(&s, p->wmax * p->mult[1] / 100)));
        ROW("SPELL DMG", (s_int(&s, p->wmin * p->mult[2] / 100), s_chr(&s, '-'), s_int(&s, p->wmax * p->mult[2] / 100)));
        ROW("ARMOR", (s_int(&s, p->armor), s_add(&s, " "), s_int(&s, armor_reduction(MAX(1, G.floor ? G.floor : G.deepest))), s_chr(&s, '%')));
        ROW("CRIT", (s_int(&s, p->crit), s_chr(&s, '%')));
        ROW("BLOCK/DODGE", (s_int(&s, p->block), s_add(&s, "% "), s_int(&s, p->dodge), s_chr(&s, '%')));
        ROW("LIFE STEAL", (s_int(&s, p->leech), s_chr(&s, '%')));
        ROW("ATK SPEED", (s_chr(&s, '+'), s_int(&s, p->aspd), s_chr(&s, '%')));
        ROW("MOVE", (s_chr(&s, '+'), s_int(&s, p->move), s_chr(&s, '%')));
        ROW("MAGIC FIND", (s_chr(&s, '+'), s_int(&s, p->mf), s_chr(&s, '%')));
        ROW("KILLS", (s_int(&s, p->kills)));
        ROW("DEEPEST", (s_int(&s, G.deepest)));
        msg_line(116);
        text(6, 192, "EACH LEVEL: +5 ATTRIBUTE POINTS, +1 SKILL POINT.", RGB(150, 140, 130));
        text(6, 200, "THE TRAINER IN TOWN CAN RESET YOUR CHOICES.", RGB(150, 140, 130));
        footer(g_pt_on ? "A/RIGHT ADD   X/LEFT TAKE BACK   B DONE" : "UP/DOWN CHOOSE   A/RIGHT ADD POINT   B CLOSE   L/R TABS");
    } else {
        draw_system_page();
    }
}

static void draw_npc_screen(void) {
    panel(); Player *p = &G.p; Str s;
    static const char *TITLE[NPC_COUNT] = {"MERCHANT", "BLACKSMITH", "JEWELER", "TRAINER", "STASH", "", ""};
    text_shadow(6, 4, TITLE[G.npc], RGB(255, 210, 140));
    gold_line();
    int eq, sl; Item *it;
    switch (G.npc) {
    case NPC_MERCHANT: {
        text(110, 4, "BUY", RGB(150, 140, 130));
        for (int r = 0; r < 2; r++) for (int c = 0; c < BAG_COLS; c++) {
            int i = r * BAG_COLS + c, x = 110 + c * 19, y = 14 + r * 19, sel = G.cur_region == 0 && G.cur_x == c && G.cur_y == r;
            if (i < 2) {
                fill_rect(x, y, 17, 17, RGB(22, 18, 26)); rect_outline(x, y, 17, 17, i ? RGB(70, 110, 220) : RGB(200, 60, 60));
                draw_potion(i, x + (17 - SPR_i_potion.w) / 2, y + (17 - SPR_i_potion.h) / 2, g_shade[7]);
                if (sel) rect_outline(x - 1, y - 1, 19, 19, (G.ticks & 16) ? RGB(255, 255, 255) : RGB(255, 220, 120));
            } else draw_slot_box(x, y, 17, &G.shop[i], sel, -1);
        }
        text(6, 18, "WARES FOR", RGB(150, 140, 130)); text(6, 26, "THE DEEP.", RGB(150, 140, 130));
        npc_portrait(NPC_MERCHANT, 44, 10, 2);
        text(110, 56, "YOUR BAG (A: SELL)", RGB(150, 140, 130));
        for (int r = 0; r < BAG_ROWS; r++) for (int c = 0; c < BAG_COLS; c++)
            draw_slot_box(110 + c * 19, 64 + r * 19, 17, &G.bag[r * BAG_COLS + c], G.cur_region == 1 && G.cur_x == c && G.cur_y == r, -1);
        hline(4, 126, SCR_W - 8, RGB(80, 60, 40));
        if (G.cur_region == 0 && G.cur_y == 0 && G.cur_x < 2) {
            text_shadow(6, 130, G.cur_x ? "MANA POTION" : "HEALTH POTION", G.cur_x ? RGB(130, 160, 255) : RGB(255, 130, 130));
            text(6, 140, G.cur_x ? "RESTORES 60% MANA" : "RESTORES 45% LIFE", RGB(200, 200, 200));
            s_reset(&s); s_add(&s, "PRICE "); s_int(&s, potion_price()); s_add(&s, " GOLD   YOU HAVE "); s_int(&s, G.cur_x ? p->mpot : p->hpot);
            text(6, 150, s.b, pal(30));
        } else {
            it = G.cur_region == 0 ? &G.shop[G.cur_y * BAG_COLS + G.cur_x] : &G.bag[G.cur_y * BAG_COLS + G.cur_x];
            if (it->used) {
                int y = item_lines(it, 6, 130, SCR_H - 26);
                (void)y;
                s_reset(&s); s_add(&s, G.cur_region == 0 ? "BUY FOR " : "SELLS FOR "); s_int(&s, G.cur_region == 0 ? buy_price(it) : item_value(it)); s_add(&s, " GOLD");
                text(150, 130, s.b, pal(30));
            }
        }
        msg_line(SCR_H - 22);
        footer("A BUY/SELL   B LEAVE");
        break; }
    case NPC_SMITH: case NPC_JEWELER:
        if (G.npc == NPC_JEWELER) {
            text(70, 4, G.npc_tab ? "L/R: [GEMS]  SOCKETS" : "L/R: GEMS  [SOCKETS]", RGB(170, 160, 150));
            if (G.npc_tab == 1) {
                draw_gem_grid(18);
                s_reset(&s); s_add(&s, "COMBINE 3 INTO 1 BETTER GEM: "); s_int(&s, combine_cost(G.cur_x)); s_add(&s, " GOLD"); text(6, 162, s.b, pal(30));
                msg_line(SCR_H - 22); footer("A COMBINE 3   L/R TAB   B LEAVE");
                break;
            }
        }
        draw_doll_bag(16);
        it = cursor_item(&eq, &sl);
        if (G.npc == NPC_SMITH) { text(110, 76, "ENCHANT  REFORGE  UPGRADE", RGB(150, 140, 130)); npc_portrait(NPC_SMITH, 104, 84, 1); }
        else { npc_portrait(NPC_JEWELER, 102, 80, 1); text(126, 84, "GEMS SET INTO SOCKETS", RGB(150, 140, 130)); text(126, 94, "GRANT EXTRA POWER", RGB(150, 140, 130)); }
        draw_item_tooltip(126, 0);
        if (it && it->used) {
            int y = 126;
            if (G.npc == NPC_SMITH) {
                s_reset(&s); s_add(&s, "A ENCHANT "); if (it->naff < MAX_AFF) s_int(&s, enchant_cost(it)); else s_add(&s, "MAX"); text(150, y, s.b, pal(30)); y += LH;
                s_reset(&s); s_add(&s, "Y REFORGE "); if (it->naff) s_int(&s, reforge_cost(it)); else s_add(&s, "-"); text(150, y, s.b, pal(30)); y += LH;
                s_reset(&s); s_add(&s, "X UPGRADE "); if (it->rarity < R_EPIC && !it->uniq) { s_int(&s, upgrade_cost(it)); text(150, y, s.b, pal(30)); y += LH; text(150, y, "  TO", RGB(150, 140, 130)); text(150 + 5 * CH_W, y, RARITY_NAME[it->rarity + 1], rarity_col(it->rarity + 1)); }
                else { s_add(&s, "-"); text(150, y, s.b, pal(30)); }
            } else {
                s_reset(&s); s_add(&s, "A ADD SOCKET "); if (it->sockets < max_sockets(it)) s_int(&s, socket_cost(it)); else s_add(&s, "MAX"); text(150, y, s.b, pal(30)); y += LH;
                text(150, y, "Y INSERT GEM", pal(30)); y += LH;
                s_reset(&s); s_add(&s, "X REMOVE GEMS "); s_int(&s, unsocket_cost(it)); text(150, y, s.b, pal(30)); y += LH;
                s_reset(&s); s_add(&s, "SOCKETS "); s_int(&s, it->sockets); s_chr(&s, '/'); s_int(&s, max_sockets(it)); text(150, y, s.b, RGB(200, 200, 200));
            }
        }
        if (G.npc == NPC_JEWELER && (G.sub == 1 || G.sub == 2)) {   // gem picker overlay
            dim_rect(60, 30, 146, 150); rect_outline(60, 30, 146, 150, RGB(200, 240, 255));
            text_c(SCR_W / 2, 34, "CHOOSE A GEM", RGB(200, 240, 255));
            int n = 0;
            for (int i = 0; i < N_GEMS; i++) if (G.gems[i]) {
                int y = 44 + n * 9; if (y > 170) break;
                if (n == G.sel) fill_rect(62, y - 1, 142, 8, RGB(60, 44, 30));
                draw_gem_icon(i, 64, y - 1); gem_name(i, &s); s_add(&s, " X"); s_int(&s, G.gems[i]); text(74, y, s.b, RGB(230, 230, 230));
                n++;
            }
            if (G.sub == 2) {   // confirm: what the gem does, on which item
                int gid = -1, k = 0; for (int i = 0; i < N_GEMS; i++) if (G.gems[i] && k++ == G.sel) { gid = i; break; }
                int eq2, sl2; Item *ti = cursor_item(&eq2, &sl2);
                if (gid >= 0 && ti && ti->used) {
                    int bx = 28, by = 66, bw = 210, bh = 74;
                    fill_rect(bx + 3, by + 3, bw, bh, RGB(0, 0, 0));
                    fill_rect(bx, by, bw, bh, RGB(26, 20, 28)); rect_outline(bx, by, bw, bh, RGB(200, 240, 255));
                    text_c(SCR_W / 2, by + 6, "SET THIS GEM?", RGB(200, 240, 255));
                    gem_name(gid, &s); draw_gem_icon(gid, SCR_W / 2 - text_w(s.b) / 2 - 10, by + 17); text_c(SCR_W / 2, by + 18, s.b, RGB(255, 255, 255));
                    s_reset(&s); s_chr(&s, '+'); s_int(&s, GEM_VAL[gid / G_TIERS][gid % G_TIERS]); if (GEM_STAT[gid / G_TIERS][0] != '%') s_chr(&s, ' ');
                    s_add(&s, GEM_STAT[gid / G_TIERS]); text_c(SCR_W / 2, by + 28, s.b, RGB(120, 255, 120));
                    item_name(ti, &s);
                    { Str t; s_reset(&t); s_add(&t, "INTO "); s_add(&t, s.b); text_c(SCR_W / 2, by + 38, t.b, rarity_col(ti->rarity)); }
                    s_reset(&s); s_add(&s, "TAKING IT OUT LATER COSTS "); s_int(&s, unsocket_cost(ti)); s_add(&s, " GOLD"); text_c(SCR_W / 2, by + 48, s.b, RGB(150, 140, 130));
                    text_c(SCR_W / 2, by + bh - 12, "A SET GEM     B BACK", RGB(230, 230, 230));
                }
            }
        }
        msg_line(SCR_H - 22);
        footer(G.npc == NPC_SMITH ? "A ENCHANT  Y REFORGE  X UPGRADE  B LEAVE" : (G.sub == 2 ? "A SET GEM   B BACK" : G.sub ? "UP/DOWN  A CHOOSE  B BACK" : "A SOCKET  Y GEM  X REMOVE  L/R TAB  B LEAVE"));
        break;
    case NPC_TRAINER: {
        npc_portrait(NPC_TRAINER, 16, 30, 2);
        int y = 34;
        text_shadow(70, y, "\"EVERY HERO FINDS THEIR PATH:", RGB(230, 220, 200)); y += 9;
        text_shadow(70, y, " BLADE, BOW, OR SPELL.\"", RGB(230, 220, 200)); y += 16;
        s_reset(&s); s_add(&s, "SPENT SKILL POINTS: "); int ss = 0; for (int i = 0; i < NSK; i++) ss += p->sk[i]; s_int(&s, ss); text(70, y, s.b, RGB(200, 200, 200)); y += LH;
        s_reset(&s); s_add(&s, "SPENT ATTRIBUTES: "); s_int(&s, p->attr[0] + p->attr[1] + p->attr[2] + p->attr[3]); text(70, y, s.b, RGB(200, 200, 200)); y += 14;
        s_reset(&s); s_add(&s, "RESET EVERYTHING FOR "); s_int(&s, respec_cost()); s_add(&s, " GOLD"); text(70, y, s.b, pal(30)); y += LH;
        text(70, y, "ALL POINTS RETURN TO YOU.", RGB(150, 140, 130)); y += 14;
        s_reset(&s); s_add(&s, "YOU ARE A "); s_add(&s, CLASS_NAME[p->cls]); text(70, y, s.b, RGB(230, 220, 200)); y += LH;
        s_reset(&s); s_add(&s, "NEW CLASS: "); s_int(&s, class_change_cost()); s_add(&s, " GOLD"); text(70, y, s.b, pal(30)); y += LH;
        text(70, y, "SKILLS RESET, OLD GEAR TO BAG.", RGB(150, 140, 130)); y += 12;
        if (G.sub) {
            for (int c = 0, x = 70; c < CL_COUNT; c++) {
                if (c == p->cls) continue;
                int sel = G.sel == c, w = 60;
                fill_rect(x, y, w, 13, sel ? RGB(60, 44, 30) : RGB(22, 18, 26)); rect_outline(x, y, w, 13, sel ? RGB(255, 220, 120) : RGB(50, 44, 54));
                text_c(x + w / 2, y + 3, CLASS_NAME[c], sel ? RGB(255, 255, 255) : RGB(170, 160, 150));
                x += w + 6;
            }
        }
        msg_line(SCR_H - 22); footer(G.sub ? "LEFT/RIGHT CLASS   A PAY & CHANGE   B BACK" : "A RESET POINTS   X CHANGE CLASS   B LEAVE");
        break; }
    case NPC_STASH:
        text(6, 16, "STASH", RGB(150, 140, 130));
        for (int r = 0; r < 3; r++) for (int c = 0; c < BAG_COLS; c++)
            draw_slot_box(60 + c * 19, 14 + r * 19, 17, &G.stash[r * BAG_COLS + c], G.cur_region == 0 && G.cur_x == c && G.cur_y == r, -1);
        text(6, 78, "BAG", RGB(150, 140, 130));
        for (int r = 0; r < BAG_ROWS; r++) for (int c = 0; c < BAG_COLS; c++)
            draw_slot_box(60 + c * 19, 76 + r * 19, 17, &G.bag[r * BAG_COLS + c], G.cur_region == 1 && G.cur_x == c && G.cur_y == r, -1);
        hline(4, 136, SCR_W - 8, RGB(80, 60, 40));
        it = G.cur_region == 0 ? &G.stash[G.cur_y * BAG_COLS + G.cur_x] : &G.bag[G.cur_y * BAG_COLS + G.cur_x];
        if (it->used) item_lines(it, 6, 140, SCR_H - 24);
        msg_line(SCR_H - 22); footer("A MOVE ITEM   B CLOSE");
        break;
    }
}

static void draw_waypoint(void) {
    dim_rect(0, 0, SCR_W, SCR_H);
    int list[12], n = waypoint_list(list);
    int h = 30 + n * 14;
    fill_rect(63, 40, 140, h, RGB(16, 12, 18)); rect_outline(63, 40, 140, h, RGB(150, 110, 60));
    text_c(SCR_W / 2, 46, "DESCEND TO...", RGB(255, 210, 140));
    for (int i = 0; i < n; i++) {
        int y = 60 + i * 14, sel = G.sel == i;
        if (sel) fill_rect(66, y - 2, 134, 11, RGB(70, 50, 30));
        Str s; s_reset(&s); s_add(&s, "DEPTH "); s_int(&s, list[i]); if (i == 0) s_add(&s, " (FROM THE TOP)"); else s_add(&s, " (WAYPOINT)");
        text_c(SCR_W / 2, y, s.b, sel ? RGB(255, 255, 255) : RGB(180, 170, 160));
    }
    text_c(SCR_W / 2, 40 + h - 10, "A GO   B CANCEL", RGB(150, 140, 130));
}

// ---------- load game: pick a save slot, then a hero on the revolving door ----------
static const i16 SIN91[91] = {0,4,9,13,18,22,27,31,36,40,44,49,53,58,62,66,71,75,79,83,88,92,96,100,104,108,112,116,120,124,128,132,136,139,143,147,150,154,158,161,165,168,171,175,178,181,184,187,190,193,196,199,202,204,207,210,212,215,217,219,222,224,226,228,230,232,234,236,237,239,241,242,243,245,246,247,248,249,250,251,252,253,254,254,255,255,255,256,256,256,256};
static int isin(int d) { d %= 360; if (d < 0) d += 360; return d <= 90 ? SIN91[d] : d <= 180 ? SIN91[180 - d] : d <= 270 ? -SIN91[d - 180] : -SIN91[360 - d]; }
static int icos(int d) { return isin(d + 90); }
static int g_door_rot, g_door_target;   // door angle in 1/16 degree (eases toward the target)
static void open_select(void) {          // the save-slot list
    G.state = ST_SELECT; G.sel_phase = 0; G.sel = CLAMP(G.slot_cur, 0, N_SLOTS - 1); G.del_confirm = 0; G.quit_t = 0;
}
#define DOOR_FADE_IN 70    // ticks (~1.3 s) for the chamber to fade in
#define DOOR_FADE_OUT 34   // ticks (~0.65 s) to fade out before the world loads
static int g_door_fade, g_door_out, g_door_bg_ok;
static void open_door(int slot, int ch) {
    G.state = ST_SELECT; G.sel_phase = 1; G.slot_cur = slot; G.sel = ch; G.del_confirm = 0; G.quit_t = 0;
    g_door_target = g_door_rot = -ch * 120 * 16; g_door_fade = 0; g_door_out = 0; g_door_bg_ok = 0;
    // stage the chamber in the world buffers (loading or starting a hero builds the real world afterwards)
    clear_level(); memset(g_amb, 0, sizeof g_amb);
    G.in_town = 0; G.floor = 1; G.p.powers = 0; G.boss_idx = -1;
    world_chamber(32, 32);
    place_player(32, 32); G.p.y += 40; G.camy = G.p.y - 30; refresh_vis();
}
static void update_select(void) {
    if (G.sel_phase == 0) {
        int mv = pressed(BTN_DOWN) - pressed(BTN_UP);
        if (mv) { G.sel = (G.sel + mv + N_SLOTS) % N_SLOTS; G.del_confirm = 0; sfx_menu(); }
        if (pressed(BTN_SELECT) && slot_heroes(G.sel)) {
            if (G.del_confirm) { memset(g_saves[G.sel], 0, sizeof g_saves[G.sel]); G.save_dirty = 1; G.del_confirm = 0; sfx_death(); }
            else { G.del_confirm = 1; sfx_menu(); }
            return;
        }
        if (pressed(BTN_B)) { if (G.del_confirm) G.del_confirm = 0; else { G.state = ST_TITLE; sfx_menu(); } return; }
        if (pressed(BTN_A) || pressed(BTN_START)) {
            int first = 0; for (int c = N_CHARS - 1; c >= 0; c--) if (hero_valid(G.sel, c)) first = c;
            open_door(G.sel, G.slot_cur == G.sel && hero_valid(G.sel, G.char_cur) ? G.char_cur : first); sfx_stairs();
        }
        return;
    }
    // hero chamber
    update_amb();
    if (g_door_fade < DOOR_FADE_IN) g_door_fade++;
    if (g_door_out) {   // fading out: enter the world once it's dark
        if (++g_door_out < DOOR_FADE_OUT) return;
        g_door_out = 0; G.char_cur = G.sel;
        if (hero_valid(G.slot_cur, G.sel)) { if (game_load(G.slot_cur, G.sel)) G.state = ST_PLAY; }
        else { new_game(G.seed + G.ticks * 2654435761u, G.sel); G.state = ST_PLAY; game_save(); }
        G.world_fade = 1;   // and fade the world in
        return;
    }
    int mv = pressed(BTN_RIGHT) - pressed(BTN_LEFT);
    if (mv) { G.sel = (G.sel + mv + N_CHARS) % N_CHARS; g_door_target -= mv * 120 * 16; G.del_confirm = 0; snd_play(180, 120, 2, 1400, 1); }
    if (pressed(BTN_SELECT) && hero_valid(G.slot_cur, G.sel)) {
        if (G.del_confirm) { memset(&g_saves[G.slot_cur][G.sel], 0, sizeof(SaveData)); memset(&g_persist.ext[G.slot_cur][G.sel], 0, sizeof(HeroExt)); G.save_dirty = 1; G.del_confirm = 0; sfx_death(); }
        else { G.del_confirm = 1; sfx_menu(); }
        return;
    }
    if (pressed(BTN_B)) { if (G.del_confirm) G.del_confirm = 0; else { G.sel_phase = 0; G.sel = G.slot_cur; sfx_menu(); } return; }
    if (pressed(BTN_A) || pressed(BTN_START)) {
        G.del_confirm = 0; g_door_out = 1; sfx_stairs();
    }
}
static void select_bg(void) {
    for (int y = 0; y < SCR_H; y++) fill_rect(0, y, SCR_W, 1, RGB(y / 14, y / 18, y / 12 + 3));
}
static const u16 CLASS_COL[CL_COUNT] = {RGB(255, 130, 100), RGB(130, 230, 130), RGB(130, 170, 255)};
static void draw_select(void) {
    Str s;
    if (G.sel_phase == 0) {
        select_bg();
        text2_c(SCR_W / 2, 10, "LOAD GAME", RGB(255, 200, 110));
        for (int i = 0; i < N_SLOTS; i++) {
            int x = 24, y = 40 + i * 52, w = SCR_W - 48, h = 44, sel = G.sel == i, n = slot_heroes(i);
            fill_rect(x, y, w, h, sel ? RGB(34, 26, 22) : RGB(18, 14, 20));
            rect_outline(x, y, w, h, sel ? ((G.ticks & 16) ? RGB(255, 230, 150) : RGB(220, 180, 100)) : RGB(60, 50, 56));
            s_reset(&s); s_add(&s, "SLOT "); s_int(&s, i + 1); text2(x + 8, y + 6, s.b, sel ? RGB(255, 240, 200) : RGB(160, 150, 140));
            if (!n) text(x + 8, y + 28, "EMPTY - START A NEW ADVENTURE", RGB(130, 120, 110));
            else {
                int best = 0, deep = 0; for (int c = 0; c < N_CHARS; c++) if (hero_valid(i, c)) { best = MAX(best, g_saves[i][c].level); deep = MAX(deep, g_saves[i][c].deepest); }
                s_reset(&s); s_int(&s, n); s_add(&s, n == 1 ? " HERO" : " HEROES"); s_add(&s, "   BEST LV "); s_int(&s, best); s_add(&s, "   DEEPEST "); s_int(&s, deep);
                text(x + 8, y + 28, s.b, RGB(200, 190, 180));
            }
            // little class pips for the heroes in this slot
            for (int c = 0; c < N_CHARS; c++) {
                int px = x + w - 14 - (N_CHARS - 1 - c) * 12, py = y + 10;
                if (hero_valid(i, c)) disc(px, py, 4, CLASS_COL[g_saves[i][c].cls]); else rect_outline(px - 4, py - 4, 9, 9, RGB(60, 50, 56));
            }
            if (sel && G.del_confirm) text(x + w - 120, y + 28, "SELECT AGAIN: DELETE", RGB(255, 90, 80));
        }
        footer(slot_heroes(G.sel) ? "UP/DOWN SLOT   A OPEN   SELECT DELETE SLOT   B BACK" : "UP/DOWN SLOT   A OPEN   B BACK");
        return;
    }
    // ---- the hero chamber: a torch-lit cave hall; the three heroes turn on a slow carousel ----
    { int d = g_door_target - g_door_rot; g_door_rot += d / 5 + (d > 0) - (d < 0); }
    gpu_fill(0, 0, SCR_W, SCR_H, 0);
    { int ff = g_gfx_fast; g_gfx_fast = 0; g_door_bg_build = 1;
      g_hide_player = 1; g_no_amb = 1; draw_world(); vignette(); g_hide_player = 0; g_no_amb = 0;
      g_gfx_fast = ff; g_door_bg_build = 0; }
    draw_amb();
    int cx = SCR_W / 2, cy = 150, R = 66, ry = 16;   // carousel centre on screen (feet line of the front hero)
    glow(cx, cy - 4, 44, 22, 5, 4, 2);               // warm pool where the heroes stand
    int rot = g_door_rot / 16;
    int order[3] = {0, 1, 2}, dep[3];
    for (int k = 0; k < 3; k++) dep[k] = icos(rot + k * 120);
    for (int i = 0; i < 3; i++) for (int j = i + 1; j < 3; j++) if (dep[order[j]] < dep[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < 3; i++) {
        int ch = order[i], a = rot + ch * 120, sx = isin(a), cz = icos(a);
        int hx = cx + sx * R / 256, hy = cy - 14 + cz * ry / 256;
        int valid = hero_valid(G.slot_cur, ch), cls = valid ? g_saves[G.slot_cur][ch].cls : ch;
        int front = cz > 200, lvl = cz > 200 ? 7 : cz > 0 ? 6 : 5;
        if (!valid && !front) lvl -= 1;
        int q = 6 + (cz + 256) * 3 / 512;              // 1.5x at the back up to 2.25x in front
#ifdef HAVE_HERO_ANIM
        { int nfr = HERO_ANIM_N[cls][HA_IDLE]; const CharSprite *cs = HERO_ANIM[cls][HA_IDLE][front ? 0 : 1][(G.ticks / 10 + ch * 2) % nfr];
          shadow(hx, hy, 6 + q * 2);
          char_origin_q(cs, hx, hy, lvl, !front && sx < 0, q); }
#endif
        if (!valid && front) { int bob = (G.ticks / 8) & 1; text_c(hx, hy - 78 - bob, "NEW", RGB(255, 230, 150)); }
    }
    // info about the hero in front
    int ch = G.sel, valid = hero_valid(G.slot_cur, ch), cls = valid ? g_saves[G.slot_cur][ch].cls : ch;
    int ty = cy + 18;
    dim_rect(0, ty - 4, SCR_W, SCR_H - 13 - (ty - 4));
    text2_c(cx, ty, CLASS_NAME[cls], CLASS_COL[cls]); ty += 18;
    if (valid) {
        const SaveData *sv = &g_saves[G.slot_cur][ch];
        s_reset(&s); s_add(&s, "LEVEL "); s_int(&s, sv->level); s_add(&s, "   DEEPEST "); s_int(&s, sv->deepest); text_c(cx, ty, s.b, RGB(255, 240, 200)); ty += LH;
        s_reset(&s); s_add(&s, "GOLD "); s_int(&s, (i32)sv->gold); s_add(&s, "   KILLS "); s_int(&s, sv->kills); text_c(cx, ty, s.b, RGB(200, 190, 180)); ty += LH;
        if (G.del_confirm) text_c(cx, ty + 2, "PRESS SELECT AGAIN TO DELETE THIS HERO", RGB(255, 90, 80));
    } else {
        static const char *BLURB[CL_COUNT] = {"SWORD AND STEEL - TOUGH, UP CLOSE", "BOW AND ARROW - SWIFT, FROM AFAR", "STAFF AND SPELL - FRAGILE, DEADLY"};
        text_c(cx, ty, "NEW HERO", RGB(255, 255, 255)); ty += LH;
        text_c(cx, ty, BLURB[cls], RGB(170, 160, 150));
    }
    s_reset(&s); s_add(&s, "SLOT "); s_int(&s, G.slot_cur + 1); s_add(&s, "  -  CHOOSE YOUR HERO"); text_shadow(SCR_W / 2 - text_w(s.b) / 2, 5, s.b, RGB(255, 210, 140));
    text(8, cy - 50, "<", (G.ticks & 16) ? RGB(255, 220, 140) : RGB(150, 120, 90)); text(SCR_W - 13, cy - 50, ">", (G.ticks & 16) ? RGB(255, 220, 140) : RGB(150, 120, 90));

    footer(valid ? "LEFT/RIGHT TURN   A PLAY   SELECT DELETE   B SLOTS" : "LEFT/RIGHT TURN   A BEGIN   B SLOTS");
    if (g_door_out) fade64(64 - fade_ease(g_door_out, DOOR_FADE_OUT));
    else fade64(fade_ease(g_door_fade, DOOR_FADE_IN));
}

static void perf_overlay(void) {
    if (!g_show_perf || !g_perf_fps) return;
    Str s; s_reset(&s); s_add(&s, "FPS "); s_int(&s, g_perf_fps); s_add(&s, "  "); s_int(&s, g_perf_ms); s_add(&s, "MS");
    s_add(&s, "  LOGIC "); s_int(&s, g_perf_split[0]); s_add(&s, " DRAW "); s_int(&s, g_perf_split[1]); s_add(&s, " SOUND "); s_int(&s, g_perf_split[2]);
    fill_rect(0, SCR_H - 28, text_small_w(s.b) + 4, 7, RGB(0, 0, 0)); text_small(2, SCR_H - 27, s.b, RGB(120, 255, 120));
}
static void game_render_inner(u16 *fb);
void game_render(u16 *fb) {
    lerp_begin(); g_present_bright = 64; g_present_vig = 0; g_gfb = fb;
    game_render_inner(fb);
    gpu_present(g_present_bright, g_present_vig);
    lerp_end();
}
static void game_render_inner(u16 *fb) {
    g_fb = fb; gfx_noclip();
    if (!g_remap_ready) remap_init();
    switch (G.state) {
    case ST_TITLE: draw_title(); return;
    case ST_SELECT: draw_select(); return;
    case ST_MENU: draw_menu(); return;
    case ST_NPC: draw_npc_screen(); return;
    default: break;
    }
    if (!G.in_town) gpu_fill(0, 0, SCR_W, SCR_H, 0);
    draw_world();
    vignette();
    draw_way_down();
    draw_hud();
    if (G.state == ST_WAYPOINT) draw_waypoint();
    if (G.state == ST_DEAD) draw_dead();
    if (G.state == ST_FADE) apply_fade(G.fade);
    if (G.world_fade) fade64(fade_ease(G.world_fade, WORLD_FADE_IN));
    perf_overlay();
}
