// Lantern Deep - an endless isometric action RPG for Analogue Pocket.
// Unity build: this file pulls in all game modules.
#include "common.h"
#include "gfx.c"
#include "sound.c"
#include "items.c"
#include "world.c"

// ===================== constants & types =====================
#define FX 256                    // fixed point: 256 = one tile
#define MAX_MOBS 110
#define MAX_PROJ 96
#define MAX_DROPS 72
#define MAX_FLOAT 40
#define MAX_PART 200
#define MAX_CHESTS 6
#define MAX_EFX 24
#define BAG_N 24
#define STASH_N 24
#define SHOP_N 16

enum { ST_TITLE, ST_PLAY, ST_MENU, ST_NPC, ST_WAYPOINT, ST_DEAD, ST_FADE, ST_SELECT };
enum { CL_WARRIOR, CL_RANGER, CL_MAGE, CL_COUNT };
#define DEATH_HOLD 156     // ticks (3 s) the death screen stays up before it can be dismissed
#define WORLD_FADE_IN 80   // ticks (~1.5 s): the world fades in after a hero is loaded
static const char *CLASS_NAME[CL_COUNT] = {"WARRIOR", "RANGER", "MAGE"};
enum { MT_RAT, MT_SKEL, MT_SPIDER, MT_GOB, MT_BRUTE, MT_WRAITH, MT_COUNT };
enum { SC_MELEE, SC_RANGE, SC_SPELL };

typedef struct { const Sprite *spr; const char *name; i16 hp, dmg, speed, xp; u8 ranged, minfloor; } MobDef;
static const MobDef MOBDEF[MT_COUNT] = {
    {&SPR_m_rat, "GNAWER", 8, 2, 21, 3, 0, 1},
    {&SPR_m_skel, "BONEWALKER", 14, 4, 15, 5, 0, 1},
    {&SPR_m_spider, "CAVE SPIDER", 10, 3, 23, 4, 0, 1},
    {&SPR_m_gob, "GOBLIN ARCHER", 10, 3, 15, 6, 1, 2},
    {&SPR_m_brute, "PIT BRUTE", 40, 8, 12, 12, 0, 3},
    {&SPR_m_wraith, "WRAITH", 18, 5, 16, 9, 2, 4},
};
static const char *BOSS_NAMES[3] = {"GORLATH THE PIT KING", "VEXIS THE BONE TYRANT", "SKREEVA THE BROOD QUEEN"};
static const u8 BOSS_TYPE[3] = {MT_BRUTE, MT_SKEL, MT_SPIDER};
static const char *ELITE_FIRST[8] = {"GRIM", "VILE", "ASH", "DREAD", "ROT", "GLOOM", "BLIGHT", "HOLLOW"};
static const char *ELITE_LAST[8] = {"FANG", "MAW", "SKULL", "CLAW", "HIDE", "EYE", "HOWL", "SPINE"};
enum { EA_SWIFT, EA_STONE, EA_VAMP, EA_BERSERK, EA_COUNT };
static const char *ELITE_AFFIX[EA_COUNT] = {"SWIFT", "STONESKIN", "VAMPIRIC", "BERSERK"};

// ---------- skills ----------
enum {
    SK_CLEAVE, SK_IRON, SK_CHARGE, SK_WARCRY, SK_BLOOD, SK_WHIRL, SK_QUAKE,
    SK_MULTI, SK_EAGLE, SK_PIERCE, SK_EXPLO, SK_FLEET, SK_RAIN, SK_TRAP,
    SK_FIRE, SK_MIND, SK_NOVA, SK_CHAIN, SK_ELEM, SK_METEOR, SK_SHIELD, NSK
};
#define SK_MAX_RANK 10
#define SLOT_HPOT 100
#define SLOT_MPOT 101
#define SLOT_ELIX 102   // 102..104: elixir of each kind (the strongest one carried is drunk)
// Elixirs: timed boosts in five strengths that drop deeper down
enum { EL_WRATH, EL_PREC, EL_IRON, EL_TYPES };
#define EL_TIERS 5
static const char *EL_NAME[EL_TYPES] = {"WRATH", "PRECISION", "IRONSKIN"};
static const char *EL_ABBR[EL_TYPES] = {"WRA", "PRE", "IRN"};
static const char *EL_TIER_NAME[EL_TIERS] = {"MINOR ", "LESSER ", "", "GREATER ", "SUPREME "};
static const char *EL_STAT[EL_TYPES] = {"% DAMAGE", "% CRIT CHANCE", "% ARMOR"};
static const i16 EL_VAL[EL_TYPES][EL_TIERS] = {{15, 25, 35, 50, 70}, {5, 8, 12, 16, 22}, {25, 40, 60, 85, 120}};
static const u8 EL_SECS[EL_TIERS] = {30, 40, 50, 60, 75};
static const u8 EL_REMAP[EL_TYPES][2] = {{15, 14}, {16, 31}, {29, 30}};   // potion colours: main, shade
#define TICKS_PER_SEC 52
typedef struct { const char *name, *abbr; u8 tree, passive, req, cost; u16 cd; i16 base, per; const char *desc, *unit; } SkillDef;
static const SkillDef SKILLS[NSK] = {
    {"CLEAVE", "CLV", 0, 0, 1, 6, 45, 140, 14, "WIDE SWING HITS ALL FOES IN FRONT", "% DMG"},
    {"IRON SKIN", "IRN", 0, 1, 2, 0, 0, 0, 10, "PASSIVE: MORE ARMOR", "% ARMOR"},
    {"CHARGE", "CHG", 0, 0, 3, 10, 90, 120, 12, "RUSH FORWARD, STUNNING FOES HIT", "% DMG"},
    {"WAR CRY", "CRY", 0, 0, 6, 14, 520, 20, 5, "STUN NEARBY FOES, BOOST ARMOR 8S", "% ARMOR"},
    {"BLOODLUST", "BLD", 0, 1, 8, 0, 0, 0, 4, "PASSIVE: MELEE DMG AND LIFE STEAL", "% DMG"},
    {"WHIRLWIND", "WHL", 0, 0, 10, 18, 200, 55, 6, "SPIN, HITTING EVERYTHING AROUND", "% DMG/HIT"},
    {"EARTHQUAKE", "QKE", 0, 0, 15, 28, 320, 250, 25, "SHOCKWAVE STUNS AND CRUSHES", "% DMG"},
    {"MULTISHOT", "MLT", 1, 0, 1, 9, 60, 60, 6, "FAN OF ARROWS", "% DMG EACH"},
    {"EAGLE EYE", "EAG", 1, 1, 2, 0, 0, 0, 2, "PASSIVE: CRITICAL CHANCE", "% CRIT"},
    {"PIERCING SHOT", "PRC", 1, 0, 3, 10, 70, 220, 22, "HEAVY ARROW PIERCES ALL FOES", "% DMG"},
    {"EXPLOSIVE ARROW", "EXP", 1, 0, 6, 12, 75, 160, 16, "ARROW BURSTS ON IMPACT", "% DMG"},
    {"FLEET FOOT", "FLT", 1, 1, 8, 0, 0, 0, 4, "PASSIVE: MOVE SPEED AND DODGE", "% SPEED"},
    {"RAIN OF ARROWS", "RAN", 1, 0, 10, 22, 240, 50, 5, "ARROWS RAIN ON AN AREA", "% DMG EACH"},
    {"SPIKE TRAP", "TRP", 1, 0, 15, 14, 110, 300, 30, "TRAP BURSTS WHEN FOES STEP NEAR", "% DMG"},
    {"FIREBOLT", "FIR", 2, 0, 1, 6, 36, 140, 14, "FAST BOLT THAT SETS FOES ABLAZE", "% DMG"},
    {"ARCANE MIND", "ARC", 2, 1, 2, 0, 0, 0, 6, "PASSIVE: MAX MANA AND REGEN", "% MANA"},
    {"FROST NOVA", "NOV", 2, 0, 3, 12, 130, 80, 8, "ICE BLAST FREEZES NEARBY FOES", "% DMG"},
    {"CHAIN LIGHTNING", "CHN", 2, 0, 6, 12, 60, 110, 11, "LIGHTNING LEAPS BETWEEN FOES", "% DMG"},
    {"ELEMENTAL POWER", "ELE", 2, 1, 8, 0, 0, 0, 5, "PASSIVE: SPELL DAMAGE", "% SPELL"},
    {"METEOR", "MET", 2, 0, 10, 26, 240, 350, 35, "CALL A BURNING STAR FROM ABOVE", "% DMG"},
    {"ARCANE SHIELD", "SHD", 2, 0, 15, 20, 600, 40, 8, "MANA BARRIER ABSORBS DAMAGE", "% OF MANA"},
};
static const char *TREE_NAME[3] = {"MELEE", "RANGE", "MAGIC"};

typedef struct {
    u8 alive, type, elite, eaff, awake, boss, face, name1, name2;
    i32 x, y, hp, hpmax, dmg;
    i16 speed, cd, flash, anim, burn, stun, special, atk_t;
    i32 burn_dmg, wdist, kx, ky;
    u16 level, xp;
    u8 dir8;
} Mob;
typedef struct { u8 alive, type, elite, boss, dir8; i16 t; i32 x, y; } Corpse;
#define MAX_CORPSES 24
#define MOB_ATK_LEN 26
#define MOB_ATK_HIT 14

enum { PK_ARROW, PK_BOLT, PK_MOBARROW, PK_ORB, PK_FIRE, PK_LIGHT, PK_EXPLO, PK_BIG };
typedef struct { u8 alive, kind, owner, pierce, chain, crit; i8 last_hit; i32 x, y, vx, vy, dmg; i16 life; } Proj;

enum { DK_ITEM, DK_GOLD, DK_HPOT, DK_MPOT, DK_GEM, DK_ELIX };
typedef struct { u8 alive, kind; i32 x, y; i32 amount; i16 t; Item it; } Drop;
typedef struct { u8 alive, opened; i32 x, y; } Chest;
typedef struct { u8 alive; i32 x, y; i16 t; u16 col; char s[12]; } FloatText;
typedef struct { u8 alive; i32 x, y, z, vx, vy, vz; i16 t; u16 col; } Part;
enum { FX_RAIN, FX_METEOR, FX_TRAP, FX_QUAKE, FX_RING, FX_BOOM };
typedef struct { u8 alive, kind, crit; i32 x, y; i16 t, t0; i32 radius, dmg; u32 hit[4]; u16 col; } Effect;

typedef struct {
    i32 x, y, hp, mp; int hpmax, mpmax;
    int level; u32 xp, xpnext, gold; int hpot, mpot, kills;
    int face, dirx, diry, walk;
    i32 vx, vy, adist;                       // smoothed velocity, distance walked (drives the run cycle)
    int anim, anim_t, anim_len, act_fired, act_kind, dir8;
    i16 atk_cd, atk_anim, hurt, roll_t, roll_cd, whirl_t, charge_t, warcry_t, shield_t;
    i32 rollx, rolly, shield;
    u32 charge_hit[4];
    int shot_count; u8 undying_used;
    int cls;                                 // CL_*: picks the skill tree, weapon type and look
    // progression
    int attr[4], unspent_attr, unspent_skill;   // STR DEX INT VIT (points spent)
    u8 sk[NSK]; i16 skcd[NSK]; i8 slot[4];
    // derived stats
    int str, dex, intl, vit, wmin, wmax, armor, aspd, crit, leech, move, mf, regen, mregen, fire, block, dodge, wtype;
    int mult[3];
    u32 powers;
} Player;

static struct {
    int state; u32 seed; int floor; u32 ticks;
    u32 btn, btn_prev, pressed;
    Player p;
    Mob mobs[MAX_MOBS]; Proj proj[MAX_PROJ]; Drop drops[MAX_DROPS]; Chest chests[MAX_CHESTS];
    FloatText ft[MAX_FLOAT]; Part part[MAX_PART]; Effect efx[MAX_EFX];
    Item bag[BAG_N]; Item equip[S_COUNT]; Item stash[STASH_N]; Item shop[SHOP_N];
    u16 gems[N_GEMS];
    u8 elix[EL_TYPES][EL_TIERS];             // elixirs carried
    i16 buff_t[EL_TYPES]; u8 buff_tier[EL_TYPES];   // active elixirs: ticks left, strength
    int cur_region, cur_x, cur_y;            // item cursor (bag/doll or two-grid)
    int tab, npc, npc_tab, sel, sub, assign; // menu state
    int ptx, pty;
    int fade, fade_dir, fade_target;         // fade_target: >0 floor, 0 town
    int msg_t[3]; char msg[3][48]; u16 msg_col[3];
    int shake, boss_idx, show_map;
    int in_town, portal_floor, deepest, waypoint, has_save, death_gold;
    int interact_kind, interact_idx;        // what A would use right now
    int save_dirty, slot_cur, char_cur, sel_phase, del_confirm, quit_t;
    int world_fade, dead_t;               // dead_t: ticks since the hero fell (the death screen can't be skipped for 3 s)                          // >0: ticks into the fade-in after loading a hero
    int hitstop; u32 pressed_acc;
    i32 camx, camy;                         // smoothed camera (world fixed point)
    Corpse corpses[MAX_CORPSES];
} G;
enum { HA_IDLE, HA_WALK, HA_ATTACK, HA_CAST, HA_ROLL, HA_HURT, HA_DEATH };
static void set_anim(int a, int len) { G.p.anim = a; G.p.anim_t = 0; G.p.anim_len = len; }
// 8-way facing from a world vector: 0=E 1=SE 2=S 3=SW 4=W 5=NW 6=N 7=NE (screen directions)
static int dir8_of(i32 wx, i32 wy) {
    i32 sx = wx, sy = wy, ax = ABS(sx), ay = ABS(sy);
    if (!sx && !sy) return 1;
    if (ay * 1000 < ax * 414) return sx > 0 ? 0 : 4;
    if (ax * 1000 < ay * 414) return sy > 0 ? 2 : 6;
    return sx > 0 ? (sy > 0 ? 1 : 7) : (sy > 0 ? 3 : 5);
}

static void game_save(void);
// Save status reported by the platform (Pocket SD card): shown on the title screen.
enum { SD_NONE, SD_IDLE, SD_BUSY, SD_FAIL };
static int g_sd_state;            // SD_NONE on platforms that save elsewhere
static int g_show_perf;           // FPS / frame-time overlay (filled in by the platform)
static int g_perf_fps, g_perf_ms, g_perf_split[3];   // split: logic / world / hud+menus, in 0.1 ms units... set by the platform
static const char *g_sd_load_msg; // what happened when saves were read at boot
static void open_select(void);
static void open_door(int slot, int ch);
static void update_select(void);
static void update_menu(void); static void update_npc(void); static void update_waypoint(void);

// ===================== settings: button mapping + graphics (saved with the heroes) =====================
// Every action except movement can go on any button. Actions are listed in the order of their default buttons,
// so the game logic keeps using BTN_A..BTN_SELECT and the mapping is applied once, to the raw buttons, in play.
enum { ACT_ATTACK, ACT_ROLL, ACT_SK1, ACT_SK2, ACT_SK3, ACT_SK4, ACT_MENU, ACT_MAP, ACT_N };
static const u32 PHYS_BTN[ACT_N] = {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_L, BTN_R, BTN_START, BTN_SELECT};
static const char *PHYS_NAME[ACT_N] = {"A", "B", "X", "Y", "L", "R", "START", "SELECT"};
static const char *PHYS_SHORT[ACT_N] = {"A", "B", "X", "Y", "L", "R", "ST", "SE"};
static const char *ACT_NAME[ACT_N] = {"ATTACK / TALK / USE", "ROLL", "SKILL SLOT 1", "SKILL SLOT 2", "SKILL SLOT 3", "SKILL SLOT 4", "MENU", "MAP"};
#define SET_MAGIC 0x4C445354u   // "LDST"
typedef struct {
    u32 magic;
    u8 map[ACT_N];      // action -> physical button (index into PHYS_BTN); always a permutation
    u8 fancy;           // smooth lighting, blended ground edges, full-size light pools
    u8 effects;         // extra light and particles (hero's lantern, glowing embers, more dust)
    u8 vignette;        // darker screen edges
    u8 show_fps;
    u8 reserved[20];
} Settings;
static int g_plat_fancy = 1, g_plat_effects = 1;   // platform defaults (set before game_init)
static int g_gfx_effects = 1, g_gfx_vignette = 1;
static int g_sys_page, g_rebind = -1;              // system menu: 0 main, 1 graphics, 2 controls; action waiting for a button
static void settings_default_buttons(Settings *t) {   // attack B, roll A, potions X/Y, skills L then R
    t->map[ACT_ATTACK] = 1; t->map[ACT_ROLL] = 0; t->map[ACT_SK1] = 2; t->map[ACT_SK2] = 3;
    t->map[ACT_SK3] = 4; t->map[ACT_SK4] = 5; t->map[ACT_MENU] = 6; t->map[ACT_MAP] = 7;
}
static void settings_default(Settings *t) {
    memset(t, 0, sizeof *t); t->magic = SET_MAGIC;
    for (int a = 0; a < ACT_N; a++) t->map[a] = (u8)a;
    settings_default_buttons(t);
    t->reserved[0] = 2;   // defaults version
    t->fancy = (u8)g_plat_fancy; t->effects = (u8)g_plat_effects; t->vignette = 1; t->show_fps = 0;
}
static Settings *settings(void);
static void settings_apply(void);
static u32 act_btn(int a) { return PHYS_BTN[settings()->map[a]]; }   // physical button of an action
static const char *act_key(int a) { return PHYS_NAME[settings()->map[a]]; }
static const char *act_key_short(int a) { return PHYS_SHORT[settings()->map[a]]; }
static u32 map_buttons(u32 raw) {   // physical buttons -> the default buttons of the actions they trigger
    u32 o = raw & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT);
    for (int a = 0; a < ACT_N; a++) if (raw & act_btn(a)) o |= PHYS_BTN[a];
    return o;
}

// ===================== helpers =====================
static int isqrt(i32 v) { if (v <= 0) return 0; i32 r = 0, b = 1 << 30; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
static i32 dist_fx(i32 ax, i32 ay, i32 bx, i32 by) { i32 dx = (ax - bx) >> 2, dy = (ay - by) >> 2; return isqrt(dx * dx + dy * dy) << 2; }
static int pressed(u32 b) { return (G.pressed & b) != 0; }

static void msg(const char *s, u16 col) {
    for (int i = 2; i > 0; i--) { G.msg_t[i] = G.msg_t[i - 1]; memcpy(G.msg[i], G.msg[i - 1], 48); G.msg_col[i] = G.msg_col[i - 1]; }
    int n = 0; while (s[n] && n < 47) { G.msg[0][n] = s[n]; n++; } G.msg[0][n] = 0;
    G.msg_t[0] = 240; G.msg_col[0] = col;
}
static void float_text(i32 x, i32 y, const char *s, u16 col) {
    for (int i = 0; i < MAX_FLOAT; i++) if (!G.ft[i].alive) {
        FloatText *f = &G.ft[i]; f->alive = 1; f->x = x; f->y = y; f->t = 40; f->col = col;
        int n = 0; while (s[n] && n < 11) { f->s[n] = s[n]; n++; } f->s[n] = 0; return;
    }
}
static void float_num(i32 x, i32 y, int v, u16 col) { Str s; s_reset(&s); s_int(&s, v); float_text(x, y, s.b, col); }
// short flashes of light where blows land (drawn as glows when Effects are high)
#define MAX_IMPACT 12
static struct { i32 x, y; u8 t, crit; } g_impact[MAX_IMPACT];
static void impact_light(i32 x, i32 y, int crit) {
    int k = 0; for (int i = 1; i < MAX_IMPACT; i++) if (g_impact[i].t < g_impact[k].t) k = i;
    g_impact[k].x = x; g_impact[k].y = y; g_impact[k].t = crit ? 12 : 8; g_impact[k].crit = (u8)crit;
}
static void burst(i32 x, i32 y, int n, u16 col, int spd) {
    for (int k = 0; k < n; k++) for (int i = 0; i < MAX_PART; i++) if (!G.part[i].alive) {
        Part *p = &G.part[i]; p->alive = 1; p->x = x; p->y = y; p->z = 6 << 8;
        p->vx = rr(-spd, spd); p->vy = rr(-spd, spd); p->vz = rr(100, 300); p->t = (i16)rr(16, 32); p->col = col; break;
    }
}
static Effect *add_efx(int kind, i32 x, i32 y, int t, i32 radius, i32 dmg, int crit, u16 col) {
    for (int i = 0; i < MAX_EFX; i++) if (!G.efx[i].alive) {
        Effect *e = &G.efx[i]; memset(e, 0, sizeof *e);
        e->alive = 1; e->kind = (u8)kind; e->x = x; e->y = y; e->t = e->t0 = (i16)t; e->radius = radius; e->dmg = dmg; e->crit = (u8)crit; e->col = col;
        return e;
    }
    return 0;
}

// ===================== player stats =====================
static int skill_val(int id) { int r = G.p.sk[id]; if (!r) return 0; const SkillDef *d = &SKILLS[id]; return d->passive ? d->per * r : d->base + d->per * (r - 1); }
// Skill scaling: mana cost grows with rank and with hero level (so skills stay a real spend as the mana pool grows),
// cooldown shrinks 4% per rank (rank 10 = 64%), damage grows per rank via base + per*(rank-1).
static int g_casts;   // test counter
static int skill_cost_r(int id, int r) { const SkillDef *d = &SKILLS[id]; if (r < 1) r = 1; return d->cost * (100 + 12 * (r - 1) + 4 * (G.p.level - 1)) / 100; }
static int skill_cost(int id) { return skill_cost_r(id, G.p.sk[id]); }
static int skill_cd_r(int id, int r) { if (r < 1) r = 1; return SKILLS[id].cd * (100 - 4 * (r - 1)) / 100; }
static int skill_cd(int id) { return skill_cd_r(id, G.p.sk[id]); }

static void player_recalc(void) {
    Player *p = &G.p;
    p->str = 5 + p->attr[0]; p->dex = 5 + p->attr[1]; p->intl = 5 + p->attr[2]; p->vit = 5 + p->attr[3];
    g_item_cls = p->cls;
    if (p->cls == CL_WARRIOR) { p->str += 3; p->vit += 1; }   // class leanings
    else if (p->cls == CL_RANGER) { p->dex += 5; p->vit += 1; }
    else p->intl += 6;
    p->wmin = 1 + p->level / 3; p->wmax = 3 + p->level / 2; p->wtype = -1;
    p->armor = 0; p->aspd = 0; p->crit = 5; p->leech = 0; p->move = 0; p->mf = 0; p->regen = 0; p->mregen = 1 + p->level / 5;
    p->fire = 0; p->block = 0; p->dodge = 0; p->powers = 0;
    int life = 0, mana = 0, dmgpct = 0, bowdmg = 0, spell = 0;
    for (int s = 0; s < S_COUNT; s++) {
        Item *it = &G.equip[s]; if (!it->used) continue;
        if (s == S_MAIN) { p->wmin = it->dmin; p->wmax = it->dmax; p->wtype = it->base; }
        p->armor += it->armor;
        if (it->base == B_SHIELD) p->block += 10;
        if (it->uniq) p->powers |= UNIQUES[it->uniq - 1].power;
        for (int a = 0; a < it->naff + 3; a++) {
            int t, v;
            if (a < it->naff) { t = it->at[a]; v = it->av[a]; }
            else { int g = a - it->naff; if (g >= it->sockets || !it->gem[g]) continue; int id = it->gem[g] - 1; t = GEM_AFFIX[id / G_TIERS]; v = GEM_VAL[id / G_TIERS][id % G_TIERS]; }
            switch (t) {
            case A_STR: p->str += v; break; case A_DEX: p->dex += v; break; case A_INT: p->intl += v; break;
            case A_VIT: life += v; break; case A_MANA: mana += v; break; case A_DMG: dmgpct += v; break;
            case A_ARMOR: p->armor += v; break; case A_ASPD: p->aspd += v; break; case A_CRIT: p->crit += v; break;
            case A_LEECH: p->leech += v; break; case A_MOVE: p->move += v; break; case A_MF: p->mf += v; break;
            case A_REGEN: p->regen += v; break; case A_MREGEN: p->mregen += v; break; case A_FIRE: p->fire += v; break;
            case A_BOWDMG: bowdmg += v; break; case A_BLOCK: p->block += v; break; case A_SPELL: spell += v; break;
            }
        }
    }
    if (p->powers & P_SWIFT) { p->move += 25; p->dodge += 10; }
    if (p->powers & P_HASTE) p->aspd += 25;
    if (p->powers & P_BLOCK) p->block += 25;
    if (p->powers & P_LIGHT) p->mf += 30;
    if (p->powers & P_MIDAS) p->mf += 100;
    if (p->powers & P_CLEAVE) p->leech += 6;
    // passives
    p->armor = p->armor * (100 + skill_val(SK_IRON) + (p->warcry_t > 0 ? skill_val(SK_WARCRY) : 0)) / 100 + p->str / 4;
    if (G.buff_t[EL_IRON] > 0) p->armor = p->armor * (100 + EL_VAL[EL_IRON][G.buff_tier[EL_IRON]]) / 100 + 5 + 5 * G.buff_tier[EL_IRON];
    if (G.buff_t[EL_PREC] > 0) p->crit += EL_VAL[EL_PREC][G.buff_tier[EL_PREC]];
    if (G.buff_t[EL_WRATH] > 0) dmgpct += EL_VAL[EL_WRATH][G.buff_tier[EL_WRATH]];
    p->leech += p->sk[SK_BLOOD];
    p->crit += p->dex / 10 + skill_val(SK_EAGLE);
    p->move += skill_val(SK_FLEET); p->dodge += p->sk[SK_FLEET] * 2;
    p->mult[SC_MELEE] = 100 + p->str + dmgpct + skill_val(SK_BLOOD);
    p->mult[SC_RANGE] = 100 + p->dex + dmgpct + bowdmg;
    p->mult[SC_SPELL] = 100 + p->intl + dmgpct + spell + skill_val(SK_ELEM) + (p->wtype == B_STAFF ? 25 : 0);
    p->hpmax = 40 + 8 * p->level + p->vit * 4 + life;
    if (p->powers & P_DEEPHEART) p->hpmax = p->hpmax * 3 / 2;
    p->mpmax = (20 + 3 * p->level + p->intl + mana) * (100 + skill_val(SK_MIND)) / 100;
    p->mregen = p->mregen * (100 + p->sk[SK_MIND] * 10) / 100;
    p->block = MIN(p->block, 60); p->crit = MIN(p->crit, 60); p->move = MIN(p->move, 80); p->aspd = MIN(p->aspd, 75); p->dodge = MIN(p->dodge, 40);
    if (p->hp > p->hpmax << 8) p->hp = p->hpmax << 8;
    if (p->mp > p->mpmax << 8) p->mp = p->mpmax << 8;
}
static int basic_school(void) { return G.p.wtype == B_BOW ? SC_RANGE : G.p.wtype == B_STAFF ? SC_SPELL : SC_MELEE; }
static int attack_frames(void) {
    int base = G.p.wtype == B_BOW ? 17 : G.p.wtype == B_STAFF ? 19 : 15;
    return MAX(6, base * 100 / (100 + G.p.aspd));
}
static int armor_reduction(int mob_level) {   // percent
    int a = G.p.armor, k = 25 * scale_dmg(mob_level) / 100;
    return MIN(75, a * 100 / (a + k + 1));
}
static i32 roll_dmg(int school, int pct, int *crit) {
    Player *p = &G.p;
    i32 d = rr(p->wmin, p->wmax) * p->mult[school] / 100 * pct / 100 + p->fire * pct / 100;
    *crit = rn(100) < p->crit; if (*crit) d *= 2;
    return MAX(1, d);
}

// ===================== spawning =====================
#define EARLY_FLOORS 6   // floors 1..5 get extra monster damage, tapering off
#define EARLY_DMG 60     // % per floor below EARLY_FLOORS (floor 1: +200% for normal monsters; elites are tough enough already)
static Mob *spawn_mob(int type, int tx, int ty, int elite) {
    for (int i = 0; i < MAX_MOBS; i++) if (!G.mobs[i].alive) {
        Mob *m = &G.mobs[i]; memset(m, 0, sizeof *m);
        const MobDef *d = &MOBDEF[type];
        m->alive = 1; m->type = (u8)type; m->elite = (u8)elite;
        m->x = tx * FX + FX / 2 + rr(-60, 60); m->y = ty * FX + FX / 2 + rr(-60, 60);
        m->level = (u16)G.floor;
        m->hpmax = d->hp * scale_hp(G.floor) / 100 * 115 / 100; m->dmg = MAX(1, d->dmg * scale_dmg(G.floor) / 100 * 112 / 100);
        m->dmg = MAX(1, m->dmg * (100 + MAX(0, EARLY_FLOORS - G.floor) * (elite ? 0 : EARLY_DMG)) / 100);   // the first floors bite harder (elites already do)   // v0.17: +15% life, +12% damage
        m->speed = d->speed; m->xp = (u16)(d->xp * (10 + G.floor) / 10);
        m->cd = (i16)rr(16, 42);   // a little quicker to start swinging
        if (elite == 1) { m->hpmax = m->hpmax * 5 / 2; m->dmg = m->dmg * 7 / 5; m->xp *= 3; }
        if (elite == 2) {
            m->hpmax *= G.floor < 3 ? 3 : 4; m->dmg = m->dmg * 8 / 5; m->xp *= 6;   // a little softer on the first floors
            m->eaff = (u8)rn(EA_COUNT); m->name1 = (u8)rn(8); m->name2 = (u8)rn(8);
            if (m->eaff == EA_SWIFT) m->speed = m->speed * 3 / 2;
            if (m->eaff == EA_BERSERK) m->dmg = m->dmg * 3 / 2;
        }
        m->hp = m->hpmax;
        return m;
    }
    return 0;
}
static void spawn_boss(void) {
    Room *r = &g_rooms[g_stairs_room];
    int bi = (G.floor / 5 - 1) % 3;
    Mob *m = spawn_mob(BOSS_TYPE[bi], r->x + r->w / 2, r->y + r->h / 2 - 2, 0);
    if (!m) return;
    m->boss = (u8)(bi + 1);
    m->hpmax = MOBDEF[m->type].hp * 22 * scale_hp(G.floor) / 100 * 115 / 100; m->hp = m->hpmax;
    m->dmg = MOBDEF[m->type].dmg * 2 * scale_dmg(G.floor) / 100 * 112 / 100;
    m->speed = 13; m->xp = (u16)(150 + G.floor * 20);
    G.boss_idx = (int)(m - G.mobs);
}
static int g_dropstat[8];   // test statistics: item drops by rarity
static void drop_add(int kind, i32 x, i32 y, i32 amount, const Item *it) {
    if (kind == DK_ITEM && it) g_dropstat[it->rarity]++;
    for (int i = 0; i < MAX_DROPS; i++) if (!G.drops[i].alive) {
        Drop *d = &G.drops[i]; d->alive = 1; d->kind = (u8)kind; d->amount = amount; d->t = 0;
        d->x = x + rr(-80, 80); d->y = y + rr(-80, 80);
        if (is_solid(d->x >> 8, d->y >> 8)) { d->x = x; d->y = y; }
        if (it) d->it = *it;
        if (kind == DK_ITEM && it->rarity >= R_EPIC) sfx_rare_drop(it->rarity);
        return;
    }
}
static void drop_elixir(i32 x, i32 y) {   // strength follows depth: minor near the top, supreme from about depth 24
    int k = CLAMP(G.floor / 6 + (rn(4) == 0), 0, EL_TIERS - 1);
    drop_add(DK_ELIX, x, y, rn(EL_TYPES) * EL_TIERS + k, 0);
}
static void drop_item(i32 x, i32 y, int min_r) { Item it; item_random(&it, G.floor, G.p.mf, min_r); drop_add(DK_ITEM, x, y, 0, &it); }
static void drop_gem(i32 x, i32 y) {
    int maxt = MIN(G_TIERS - 1, G.floor / 7), t = 0;
    while (t < maxt && rn(100) < 45) t++;
    drop_add(DK_GEM, x, y, rn(G_TYPES) * G_TIERS + t, 0);
}

static void floor_populate(void) {
    G.boss_idx = -1;
    int packs = 8 + G.floor / 2 + rn(3); packs = MIN(packs, 22);
    int rare_placed = 0;
    for (int k = 0; k < packs; k++) {
        int ri = 1 + rn(g_nrooms - 1); Room *r = &g_rooms[ri];
        int type; do { type = rn(MT_COUNT); } while (MOBDEF[type].minfloor > G.floor);
        int elite = 0;
        if (rn(100) < 12 + G.floor / 2) elite = 1;
        if (!rare_placed && rn(100) < 40 + G.floor * 2) { elite = 2; rare_placed = 1; }
        int n = elite == 2 ? rr(2, 3) : rr(3, 5 + MIN(G.floor / 6, 3));
        for (int i = 0; i < n; i++) {
            int tx = rr(r->x, r->x + r->w - 1), ty = rr(r->y, r->y + r->h - 1);
            if (is_solid(tx, ty)) continue;
            spawn_mob(type, tx, ty, (elite == 2 && i > 0) ? 1 : elite);
        }
    }
    if (G.floor % 5 == 0) spawn_boss();
    int nch = rr(1, 3);
    for (int c = 0; c < nch && c < MAX_CHESTS; c++) {
        Room *r = &g_rooms[1 + rn(g_nrooms - 1)];
        int tx = r->x + rr(0, r->w - 1), ty = r->y + rr(0, r->h - 1);
        if (tile_at(tx, ty) != T_FLOOR) continue;
        G.chests[c].alive = 1; G.chests[c].opened = 0; G.chests[c].x = tx * FX + FX / 2; G.chests[c].y = ty * FX + FX / 2;
    }
}

static void refresh_vis(void) {
    if (G.in_town) { g_vis_stamp++; if (!g_vis_stamp) g_vis_stamp = 1; memset(g_vis, g_vis_stamp, sizeof g_vis); return; }
    int r = (G.p.powers & P_LIGHT) ? 10 : 8;
    world_update_vis(G.ptx, G.pty, r);
    bfs_from(G.ptx, G.pty, g_flow);
}
static void clear_level(void) {
    memset(G.mobs, 0, sizeof G.mobs); memset(G.proj, 0, sizeof G.proj); memset(G.drops, 0, sizeof G.drops);
    memset(G.chests, 0, sizeof G.chests); memset(G.part, 0, sizeof G.part); memset(G.ft, 0, sizeof G.ft); memset(G.efx, 0, sizeof G.efx);
    G.p.roll_t = G.p.whirl_t = G.p.charge_t = 0; G.p.vx = G.p.vy = 0;
    memset(G.corpses, 0, sizeof G.corpses);
    set_anim(HA_IDLE, 1);
}
static void place_player(int tx, int ty) { G.p.x = tx * FX + FX / 2; G.p.y = ty * FX + FX / 2; G.ptx = tx; G.pty = ty; G.camx = G.p.x; G.camy = G.p.y; }

// the dungeon level for a depth (the same every time for a hero's seed)
static void build_floor(int floor) {
    world_generate(G.seed, floor);
    if (g_deep_tx >= 0 && (floor + 1) % 5 == 0 && G.waypoint <= floor + 1) {   // a +2 drop would skip a guardian not yet beaten
        g_map[g_deep_ty][g_deep_tx] = T_FLOOR; g_torch[g_deep_ty][g_deep_tx] = 0; g_deep_tx = g_deep_ty = -1;
    }
}
static void enter_floor(int floor) {
    G.in_town = 0; G.floor = floor; if (floor > G.deepest) G.deepest = floor;
    clear_level();
    build_floor(floor);
    place_player(g_start_x, g_start_y);
    G.p.undying_used = 0;
    g_rng = hash32(G.seed * 31u + (u32)floor * 7u + G.ticks) | 1;
    floor_populate();
    refresh_vis();
    Str s; s_reset(&s); s_add(&s, "DEPTH "); s_int(&s, floor);
    if (floor % 5 == 0) s_add(&s, " - A GREAT EVIL STIRS");
    msg(s.b, RGB(255, 200, 120));
    game_save();
}

// New stock on every visit to town: six items in random places, one of them a good find for this hero
// (Ultra Rare or better, made for their class, around their level); the rest ordinary.
#define SHOP_ITEMS 6
static void restock_shop(void) {
    int lvl = MAX(1, MAX(G.deepest, G.p.level - 1));
    for (int i = 2; i < SHOP_N; i++) G.shop[i].used = 0;
    int pos[SHOP_N - 2]; for (int i = 0; i < SHOP_N - 2; i++) pos[i] = i + 2;
    for (int i = SHOP_N - 3; i > 0; i--) { int j = rn(i + 1), t = pos[i]; pos[i] = pos[j]; pos[j] = t; }   // shuffle the places
    int keep = g_item_cls; g_item_cls = G.p.cls;
    for (int k = 0; k < SHOP_ITEMS; k++) {
        Item *it = &G.shop[pos[k]];
        if (k == 0) {   // the decent one
            for (int tries = 0; tries < 20; tries++) {
                item_random(it, lvl + 1, 150, R_ULTRA);
                int ic = item_class(it->base);
                if (it->rarity <= R_EPIC && (ic < 0 || ic == G.p.cls)) break;
            }
        } else item_random(it, lvl, 0, 0);
    }
    g_item_cls = keep;
    G.shop[0].used = G.shop[1].used = 0;
}
static void enter_town(int how) {   // how: 0 new/death, 1 portal, 2 walked up
    G.in_town = 1; G.floor = 0;
    clear_level();
    world_town(G.seed);
    if (how == 1) place_player(g_npcs[NPC_PORTAL].tx, g_npcs[NPC_PORTAL].ty + 2);
    else if (how == 2) place_player(32, 18);
    else place_player(g_town_spawn_x, g_town_spawn_y);
    G.p.hp = G.p.hpmax << 8; G.p.mp = G.p.mpmax << 8;
    G.boss_idx = -1;
    restock_shop();
    refresh_vis();
    msg("EMBERHOLD", RGB(255, 200, 120));
    game_save();
}

#define XP_FIRST 120
#define XP_STEP(l) (100 + (u32)(l) * (u32)(l) * 38)   // slower, more drawn-out levelling (was 30 + 12*l^2, first 42)
static int class_weapon(int cls) { return cls == CL_RANGER ? B_BOW : cls == CL_MAGE ? B_STAFF : B_SWORD; }
static void new_game(u32 seed, int cls) {
    memset(&G.p, 0, sizeof G.p); memset(G.bag, 0, sizeof G.bag); memset(G.equip, 0, sizeof G.equip);
    memset(G.stash, 0, sizeof G.stash); memset(G.gems, 0, sizeof G.gems);
    memset(G.elix, 0, sizeof G.elix); memset(G.buff_t, 0, sizeof G.buff_t);
    G.seed = seed; G.deepest = 0; G.waypoint = 1; G.portal_floor = 0;
    g_rng = hash32(seed) | 1;
    Player *p = &G.p;
    p->cls = cls;
    p->level = 1; p->xpnext = XP_FIRST; p->hpot = 4; p->mpot = 3; p->gold = 60; p->dirx = 181; p->diry = 181;
    p->unspent_skill = 0; p->unspent_attr = 0;
    p->sk[cls * 7] = 1;   // the class's first skill (Cleave / Multishot / Firebolt) comes learned and ready on X
    p->slot[0] = SLOT_MPOT; p->slot[1] = SLOT_HPOT; p->slot[2] = (i8)(cls * 7); p->slot[3] = -1;   // mana on X, health on Y, first skill on L
    item_make(&G.equip[S_MAIN], class_weapon(cls), R_COMMON, 1);
    item_make(&G.equip[S_BODY], B_CHEST, R_COMMON, 1);
    player_recalc();
    enter_town(0);
    { Str m; s_reset(&m); s_add(&m, SKILLS[cls * 7].name); s_add(&m, " IS READY ON "); s_add(&m, act_key(ACT_SK3)); msg(m.b, RGB(160, 220, 255)); }
}

// ===================== save / load: three hero slots (kept in memory; the browser build stores them) =====================
#define SAVE_MAGIC 0x4C445356u
#define SAVE_VER 3
#define N_SLOTS 3   // save slots (files) ...
#define N_CHARS 3   // ... each holding three heroes, shown on a revolving door
typedef struct {
    u32 magic, version, seed; int level; u32 xp, xpnext, gold; int hpot, mpot, kills;
    int attr[4], unspent_attr, unspent_skill; u8 sk[NSK]; i8 slot[4];
    int deepest, waypoint;
    Item equip[S_COUNT], bag[BAG_N], stash[STASH_N];
    u16 gems[N_GEMS];
    int cls;   // added in v3 (everything above matches the old v2 layout)
} SaveData;
// everything that is saved: the heroes, then the settings (older files simply end before the settings)
// per-hero additions after v0.33 live in their own block, so older files (which simply end earlier) still load
#define EXT_MAGIC 0x4C444558u   // "LDEX"
typedef struct { u32 magic; u8 elix[EL_TYPES][EL_TIERS]; u8 reserved[45]; } HeroExt;
struct Persist { SaveData saves[N_SLOTS][N_CHARS]; Settings set; HeroExt ext[N_SLOTS][N_CHARS]; };
static struct Persist g_persist;
#define g_saves g_persist.saves   // the old 3-hero layout is exactly slot 1 of this one
static Settings *settings(void) { return &g_persist.set; }
static void settings_apply(void) {   // after loading: fill in defaults for missing / damaged settings, then use them
    Settings *t = &g_persist.set; int ok = t->magic == SET_MAGIC;
    if (ok) { u8 seen[ACT_N] = {0}; for (int a = 0; a < ACT_N; a++) { if (t->map[a] >= ACT_N || seen[t->map[a]]) ok = 0; else seen[t->map[a]] = 1; } }
    if (!ok) settings_default(t);
    else if (t->reserved[0] < 1) {   // older settings: move roll to R, whatever had R gets roll's old button
        int r = -1; for (int a = 0; a < ACT_N; a++) if (t->map[a] == 5) r = a;
        if (r >= 0 && r != ACT_ROLL) { t->map[r] = t->map[ACT_ROLL]; t->map[ACT_ROLL] = 5; }
        t->reserved[0] = 1;
    }
    if (t->reserved[0] < 2) { settings_default_buttons(t); t->reserved[0] = 2; }   // v0.39: new default layout for everyone
    g_gfx_fast = !t->fancy; g_gfx_effects = t->effects; g_gfx_vignette = t->vignette; g_show_perf = t->show_fps;
}
static void settings_changed(void) {
    Settings *t = &g_persist.set;
    t->fancy = (u8)!g_gfx_fast; t->effects = (u8)g_gfx_effects; t->vignette = (u8)g_gfx_vignette; t->show_fps = (u8)g_show_perf;
    G.save_dirty = 1;
}
static int hero_valid(int s, int c) { return g_saves[s][c].magic == SAVE_MAGIC && g_saves[s][c].version == SAVE_VER; }
static int slot_heroes(int s) { int n = 0; for (int c = 0; c < N_CHARS; c++) n += hero_valid(s, c); return n; }
static int any_save(void) { for (int s = 0; s < N_SLOTS; s++) if (slot_heroes(s)) return 1; return 0; }
static void game_save(void) {
    SaveData *s = &g_saves[G.slot_cur][G.char_cur]; Player *p = &G.p;
    memset(s, 0, sizeof *s);
    s->magic = SAVE_MAGIC; s->version = SAVE_VER; s->seed = G.seed; s->level = p->level; s->xp = p->xp; s->xpnext = p->xpnext; s->gold = p->gold;
    s->hpot = p->hpot; s->mpot = p->mpot; s->kills = p->kills;
    memcpy(s->attr, p->attr, sizeof s->attr); s->unspent_attr = p->unspent_attr; s->unspent_skill = p->unspent_skill;
    memcpy(s->sk, p->sk, sizeof s->sk); memcpy(s->slot, p->slot, sizeof s->slot);
    s->deepest = G.deepest; s->waypoint = G.waypoint; s->cls = p->cls;
    memcpy(s->equip, G.equip, sizeof s->equip); memcpy(s->bag, G.bag, sizeof s->bag); memcpy(s->stash, G.stash, sizeof s->stash);
    memcpy(s->gems, G.gems, sizeof s->gems);
    { HeroExt *x = &g_persist.ext[G.slot_cur][G.char_cur]; memset(x, 0, sizeof *x); x->magic = EXT_MAGIC; memcpy(x->elix, G.elix, sizeof x->elix); x->reserved[0] = 1; }
    G.save_dirty = 1;
}
static int game_load(int slot, int ch) {
    SaveData *s = &g_saves[slot][ch]; Player *p = &G.p;
    if (!hero_valid(slot, ch)) return 0;
    G.slot_cur = slot; G.char_cur = ch;
    memset(p, 0, sizeof *p);
    G.seed = s->seed; p->level = s->level; p->xp = s->xp; p->xpnext = s->xpnext; p->gold = s->gold;
    p->hpot = s->hpot; p->mpot = s->mpot; p->kills = s->kills;
    memcpy(p->attr, s->attr, sizeof s->attr); p->unspent_attr = s->unspent_attr; p->unspent_skill = s->unspent_skill;
    memcpy(p->sk, s->sk, sizeof s->sk); memcpy(p->slot, s->slot, sizeof s->slot);
    G.deepest = s->deepest; G.waypoint = MAX(1, s->waypoint); p->cls = CLAMP(s->cls, 0, CL_COUNT - 1);
    memcpy(G.equip, s->equip, sizeof s->equip); memcpy(G.bag, s->bag, sizeof s->bag); memcpy(G.stash, s->stash, sizeof s->stash);
    memcpy(G.gems, s->gems, sizeof s->gems);
    { HeroExt *x = &g_persist.ext[slot][ch]; if (x->magic == EXT_MAGIC) memcpy(G.elix, x->elix, sizeof G.elix); else memset(G.elix, 0, sizeof G.elix);
      if (x->magic != EXT_MAGIC || !(x->reserved[0] & 1)) {   // v0.37: health and mana potions swap buttons (once)
          for (int k = 0; k < 4; k++) p->slot[k] = p->slot[k] == SLOT_HPOT ? SLOT_MPOT : p->slot[k] == SLOT_MPOT ? SLOT_HPOT : p->slot[k];
      } }
    memset(G.buff_t, 0, sizeof G.buff_t);
    for (int k = 0; k < 4; k++) if (p->slot[k] >= SLOT_ELIX) p->slot[k] = -1;   // elixirs live in the pouch now
    p->dirx = 181; p->diry = 181; G.portal_floor = 0;
    player_recalc();
    enter_town(0);
    return 1;
}
// An old single-slot (v2) save was copied into slot 0: work out its class from its skills/weapon and move it to that class's slot.
static void import_v2_save(void) {
    SaveData *s = &g_saves[0][0];
    if (s->magic != SAVE_MAGIC || s->version != 2) return;
    int pts[3] = {0, 0, 0};
    for (int i = 0; i < NSK; i++) pts[SKILLS[i].tree] += s->sk[i];
    int cls = pts[1] > pts[0] && pts[1] >= pts[2] ? CL_RANGER : pts[2] > pts[0] && pts[2] > pts[1] ? CL_MAGE : CL_WARRIOR;
    if (!pts[0] && !pts[1] && !pts[2] && s->equip[S_MAIN].used) cls = s->equip[S_MAIN].base == B_BOW ? CL_RANGER : s->equip[S_MAIN].base == B_STAFF ? CL_MAGE : CL_WARRIOR;
    for (int i = 0; i < NSK; i++) if (SKILLS[i].tree != cls) { s->unspent_skill += s->sk[i]; s->sk[i] = 0; }   // refund points spent outside the class
    for (int k = 0; k < 4; k++) if (s->slot[k] >= 0 && s->slot[k] < NSK && !s->sk[(int)s->slot[k]]) s->slot[k] = -1;
    s->cls = cls; s->version = SAVE_VER;
    if (cls != 0) { g_saves[0][cls] = *s; memset(s, 0, sizeof *s); }
}

// ===================== combat =====================
static void gain_xp(u32 xp) {
    Player *p = &G.p; p->xp += xp;
    while (p->xp >= p->xpnext) {
        p->level++; p->xpnext += XP_STEP(p->level);
        p->unspent_attr += 5; p->unspent_skill += 1;
        player_recalc(); p->hp = p->hpmax << 8; p->mp = p->mpmax << 8;
        Str s; s_reset(&s); s_add(&s, "LEVEL "); s_int(&s, p->level); s_add(&s, "! +5 ATTRIBUTES +1 SKILL (START)");
        msg(s.b, RGB(255, 240, 120)); sfx_levelup(); burst(p->x, p->y, 30, pal(16), 80);
    }
}
static int mobs_left(void) { int n = 0; for (int i = 0; i < MAX_MOBS; i++) n += G.mobs[i].alive; return n; }
static void mob_die(Mob *m) {
    m->alive = 0; G.p.kills++;
    if (!G.in_town && mobs_left() == 0) { msg("FLOOR CLEARED! FOLLOW THE ARROW", RGB(255, 220, 90)); sfx_levelup(); }
    for (int i = 0; i < MAX_CORPSES; i++) if (!G.corpses[i].alive) {
        Corpse *c = &G.corpses[i]; c->alive = 1; c->type = m->type; c->elite = m->elite; c->boss = m->boss; c->dir8 = m->dir8; c->t = 40; c->x = m->x; c->y = m->y; break;
    }
    G.hitstop = MAX(G.hitstop, m->boss ? 10 : 3);
    burst(m->x, m->y, m->boss ? 40 : 10, m->type == MT_SKEL ? pal(26) : pal(13), 70);
    sfx_kill();
    Player *p = &G.p;
    gain_xp(m->xp);
    if (p->powers & P_KILLHEAL) p->hp = MIN(p->hp + (p->hpmax * 4 / 100 << 8), p->hpmax << 8);
    int gmul = (p->powers & P_MIDAS) ? 2 : 1;
    if (m->boss) {
        drop_item(m->x, m->y, R_RARE);                          // v0.19: one green-or-better, sometimes a second, rarely a legend
        if (rn(100) < 35) drop_item(m->x, m->y, 0);
        if (rn(100) < 6) drop_item(m->x, m->y, R_LEGEND);
        drop_add(DK_GOLD, m->x, m->y, (50 + G.floor * 25) * gmul, 0);
        drop_add(DK_HPOT, m->x, m->y, 1, 0); drop_add(DK_MPOT, m->x, m->y, 1, 0);
        drop_gem(m->x, m->y); drop_gem(m->x, m->y);
        drop_elixir(m->x, m->y); if (rn(100) < 35) drop_elixir(m->x, m->y);
        if (G.floor + 1 > G.waypoint) { G.waypoint = G.floor + 1; msg("NEW WAYPOINT UNLOCKED IN TOWN", RGB(120, 200, 255)); }
        msg("THE GUARDIAN FALLS!", RGB(255, 140, 30));
        G.boss_idx = -1;
    } else if (m->elite == 2) {
        drop_item(m->x, m->y, rn(3) ? 0 : R_RARE);
        drop_add(DK_GOLD, m->x, m->y, (20 + G.floor * 8) * gmul, 0);
        if (rn(100) < 40) drop_gem(m->x, m->y);
        if (rn(100) < 15) drop_elixir(m->x, m->y);
    } else if (m->elite == 1) {
        if (rn(100) < 30) drop_item(m->x, m->y, 0);
        if (rn(100) < 40) drop_add(DK_GOLD, m->x, m->y, (8 + G.floor * 4) * gmul, 0);
        if (rn(100) < 12) drop_gem(m->x, m->y);
        if (rn(100) < 6) drop_elixir(m->x, m->y);
    } else {
        int r = rn(100);
        if (r < 4) drop_item(m->x, m->y, 0);
        else if (r < 43) drop_add(DK_GOLD, m->x, m->y, rr(2, 6 + G.floor * 2) * gmul, 0);
        else if (r < 48) drop_add(DK_HPOT, m->x, m->y, 1, 0);
        else if (r < 51) drop_add(DK_MPOT, m->x, m->y, 1, 0);
        else if (r < 54) drop_gem(m->x, m->y);
        else if (r < 56) drop_elixir(m->x, m->y);
    }
}

static void hit_mob_ex(Mob *m, i32 dmg, int crit, int burn, int stun, int echo) {
    if (!m->alive) return;
    if (m->elite == 2 && m->eaff == EA_STONE) dmg /= 2;
    if (dmg < 1) dmg = 1;
    m->hp -= dmg; m->flash = 5; m->awake = 1;
    if (!m->boss) {   // knockback away from the hero
        i32 dx = m->x - G.p.x, dy = m->y - G.p.y, l = MAX(1, dist_fx(dx, dy, 0, 0)), k = crit ? 70 : 38;
        m->kx = dx * k / l; m->ky = dy * k / l;
        if (m->atk_t > 0 && m->atk_t < MOB_ATK_HIT && crit) m->atk_t = 0;   // crits interrupt wind-ups
    }
    if (crit) { G.hitstop = MAX(G.hitstop, 2); G.shake = MAX(G.shake, 2); }
    if (stun && !m->boss) m->stun = (i16)MAX(m->stun, stun);
    else if (stun) m->stun = (i16)MAX(m->stun, stun / 4);
    float_num(m->x, m->y - 30, dmg, crit ? RGB(255, 230, 60) : RGB(255, 255, 255));
    if (G.p.leech) G.p.hp = MIN(G.p.hp + (dmg * G.p.leech << 8) / 100, G.p.hpmax << 8);
    if (burn || (G.p.powers & P_IGNITE)) { m->burn = 150; m->burn_dmg = MAX(m->burn_dmg, MAX(1, dmg / 6)); }
    if (G.p.fire && rn(100) < 30) { m->burn = MAX(m->burn, 90); m->burn_dmg = MAX(m->burn_dmg, G.p.fire / 2 + 1); }
    burst(m->x, m->y, g_gfx_effects ? (crit ? 9 : 5) : 3, pal(14), g_gfx_effects && crit ? 70 : 40);
    if (g_gfx_effects) { impact_light(m->x, m->y, crit); if (crit) burst(m->x, m->y, 4, RGB(255, 240, 160), 90); }
    sfx_hit();
    if (m->hp <= 0) mob_die(m);
    else if (echo && (G.p.powers & P_ECHO) && rn(100) < 15) hit_mob_ex(m, dmg, crit, 0, 0, 0);
}
static void hit_mob(Mob *m, i32 dmg, int crit) { hit_mob_ex(m, dmg, crit, 0, 0, 1); }

static void hurt_player(i32 dmg, int mob_level, Mob *src) {
    Player *p = &G.p;
    if (p->roll_t > 0 || p->charge_t > 0 || G.state != ST_PLAY) return;
    if (rn(100) < p->block && G.equip[S_OFF].used && G.equip[S_OFF].base == B_SHIELD) { float_text(p->x, p->y - 40, "BLOCK", RGB(180, 200, 255)); snd_play(1500, 40, 0, 1200, 1); return; }
    if (rn(100) < p->dodge) { float_text(p->x, p->y - 40, "DODGE", RGB(180, 255, 200)); return; }
    dmg = dmg * (100 - armor_reduction(mob_level)) / 100;
    if (p->powers & P_SHROUD) dmg = dmg * 7 / 10;
    if (dmg < 1) dmg = 1;
    if (p->shield > 0) {
        i32 a = MIN(p->shield, dmg << 8); p->shield -= a; dmg -= a >> 8;
        if (dmg <= 0) { float_text(p->x, p->y - 40, "ABSORB", RGB(150, 120, 255)); return; }
    }
    p->hp -= dmg << 8; p->hurt = 10; G.shake = 3;
    if (p->anim == HA_IDLE || p->anim == HA_WALK) set_anim(HA_HURT, 10);
    float_num(p->x, p->y - 36, dmg, RGB(255, 80, 80));
    sfx_hurt();
    if (src && (p->powers & P_THORNS)) hit_mob_ex(src, MAX(1, dmg * 60 / 100), 0, 0, 0, 0);
    if (p->hp <= 0) {
        if ((p->powers & P_UNDYING) && !p->undying_used) {
            p->undying_used = 1; p->hp = (p->hpmax / 2) << 8;
            msg("THE CROWN DENIES DEATH!", RGB(255, 210, 60)); burst(p->x, p->y, 40, pal(30), 90); return;
        }
        p->hp = 0; G.state = ST_DEAD; G.dead_t = 0; G.death_gold = (int)(p->gold / 10); sfx_death(); set_anim(HA_DEATH, 40);
    }
}

static Proj *spawn_proj(int kind, int owner, i32 x, i32 y, i32 dirx, i32 diry, int speed, i32 dmg) {
    for (int i = 0; i < MAX_PROJ; i++) if (!G.proj[i].alive) {
        Proj *q = &G.proj[i]; memset(q, 0, sizeof *q);
        q->alive = 1; q->kind = (u8)kind; q->owner = (u8)owner; q->x = x; q->y = y;
        q->vx = dirx * speed / 256; q->vy = diry * speed / 256; q->dmg = dmg; q->life = 70; q->last_hit = -1;
        return q;
    }
    return 0;
}
static Mob *find_target(i32 range, int cone) {
    Mob *best = 0; i32 bd = range;
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &G.mobs[i]; if (!m->alive || !tile_visible(m->x >> 8, m->y >> 8)) continue;
        i32 dx = m->x - G.p.x, dy = m->y - G.p.y, d = dist_fx(m->x, m->y, G.p.x, G.p.y);
        if (d >= bd) continue;
        if (cone && d > 0 && (dx * G.p.dirx + dy * G.p.diry) / d < 90) continue;
        bd = d; best = m;
    }
    return best;
}
static void aim_dir(i32 range, i32 *dx, i32 *dy) {   // auto-aim toward the best target in front
    Player *p = &G.p; *dx = p->dirx; *dy = p->diry;
    Mob *t = find_target(range, 1);
    if (t) { i32 d = dist_fx(t->x, t->y, p->x, p->y); if (d > 0) { *dx = (t->x - p->x) * 256 / d; *dy = (t->y - p->y) * 256 / d; } }
}
static void rotate(i32 *x, i32 *y, int deg8) {
    static const i16 C[9] = {256, 255, 251, 245, 237, 226, 213, 198, 181}, S[9] = {0, 25, 50, 74, 98, 121, 142, 162, 181};
    int a = ABS(deg8); if (a > 8) a = 8;
    i32 c = C[a], s = deg8 < 0 ? -S[a] : S[a];
    i32 nx = (*x * c - *y * s) / 256, ny = (*x * s + *y * c) / 256; *x = nx; *y = ny;
}
// breakable props: crates, barrels and pots shatter when hit; a small chance of a little gold inside
static int breakable_at(int tx, int ty) {
    if (!in_map(tx, ty) || g_map[ty][tx] != T_PROP) return 0;
    int k = g_prop[ty][tx]; return k == PR_BARREL || k == PR_CRATE || k == PR_URN;
}
static void break_prop(int tx, int ty) {
    if (!breakable_at(tx, ty)) return;
    int k = g_prop[ty][tx];
    g_map[ty][tx] = g_prop_floor[ty][tx]; g_prop[ty][tx] = PR_RUBBLE;   // leaves debris on the floor
    i32 x = tx * FX + 128, y = ty * FX + 160;
    u16 c1 = k == PR_URN ? RGB(170, 100, 60) : RGB(140, 95, 50), c2 = k == PR_URN ? RGB(220, 170, 70) : RGB(90, 60, 30);
    burst(x, y - 60, 12, c1, 90); burst(x, y - 40, 6, c2, 70);
    snd_play(k == PR_URN ? 700 : 260, 90, -4, 2600, 1); snd_play(140, 60, -1, 1800, 1);
    G.shake = MAX(G.shake, 2);
    if (rn(100) < 12) {   // small chance of a few coins
        int gmul = (G.p.powers & P_MIDAS) ? 2 : 1;
        drop_add(DK_GOLD, x, y, rr(1, 3 + G.floor) * gmul, 0);
    }
}
static void break_props_near(i32 x, i32 y, i32 radius) {
    int tx0 = (x - radius) >> 8, tx1 = (x + radius) >> 8, ty0 = (y - radius) >> 8, ty1 = (y + radius) >> 8;
    for (int ty = ty0; ty <= ty1; ty++) for (int tx = tx0; tx <= tx1; tx++)
        if (breakable_at(tx, ty) && dist_fx(tx * FX + 128, ty * FX + 128, x, y) < radius + 128) break_prop(tx, ty);
}
static void explode(i32 x, i32 y, i32 dmg, int crit, i32 radius, int burn, int stun) {
    break_props_near(x, y, radius);
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &G.mobs[i]; if (!m->alive) continue;
        if (dist_fx(m->x, m->y, x, y) < radius + (m->boss ? 120 : 0)) hit_mob_ex(m, dmg, crit, burn, stun, 0);
    }
    add_efx(FX_BOOM, x, y, 14, radius, 0, 0, burn ? pal(15) : pal(16));
    burst(x, y, 14, burn ? pal(15) : pal(16), 100);
    snd_play(120, 160, -1, 2600, 1);
}
static void melee_arc(int pct, i32 range, int min_dot, int stun) {   // min_dot: -256 = full circle
    Player *p = &G.p;
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &G.mobs[i]; if (!m->alive) continue;
        i32 dx = m->x - p->x, dy = m->y - p->y, d = dist_fx(m->x, m->y, p->x, p->y);
        if (d > range + (m->boss ? 200 : 0)) continue;
        if (d > 60 && (dx * p->dirx + dy * p->diry) / d < min_dot) continue;
        int crit; i32 dmg = roll_dmg(SC_MELEE, pct, &crit);
        hit_mob_ex(m, dmg, crit, 0, stun, 1);
        if (pct == 100) p->mp = MIN(p->mp + 256, p->mpmax << 8);   // basic hits restore mana
    }
    for (int ty = (p->y >> 8) - 2; ty <= (p->y >> 8) + 2; ty++) for (int tx = (p->x >> 8) - 2; tx <= (p->x >> 8) + 2; tx++) {   // smash crates and pots in the swing
        if (!breakable_at(tx, ty)) continue;
        i32 cx = tx * FX + 128, cy = ty * FX + 128, dx = cx - p->x, dy = cy - p->y, d = dist_fx(cx, cy, p->x, p->y);
        if (d > range + 128) continue;
        if (d > 60 && (dx * p->dirx + dy * p->diry) / d < min_dot - 60) continue;
        break_prop(tx, ty);
    }
}
static void target_point(i32 range, i32 *tx, i32 *ty) {
    Player *p = &G.p; Mob *t = find_target(range, 0);
    if (t) { *tx = t->x; *ty = t->y; return; }
    *tx = p->x + p->dirx * 3; *ty = p->y + p->diry * 3;
}

static void basic_attack(void) {
    Player *p = &G.p; int crit; i32 dmg;
    if (p->wtype == B_BOW) {
        i32 dx, dy; aim_dir(9 * FX, &dx, &dy);
        int n = 1; p->shot_count++;
        if (p->powers & P_FIVE) n = 5; else if ((p->powers & P_TRIPLE) && p->shot_count % 3 == 0) n = 3;
        for (int k = 0; k < n; k++) {
            i32 vx = dx, vy = dy; rotate(&vx, &vy, (k - n / 2) * 2);
            dmg = roll_dmg(SC_RANGE, 100, &crit);
            Proj *q = spawn_proj(PK_ARROW, 0, p->x, p->y, vx, vy, 125, dmg);
            if (q) { q->crit = (u8)crit; q->pierce = (p->powers & P_PIERCE) ? 1 : 0; }
        }
        sfx_shoot();
    } else if (p->wtype == B_STAFF) {
        if (p->mp < 2 << 8) { if (pressed(BTN_A)) msg("NOT ENOUGH MANA", RGB(120, 160, 255)); return; }
        p->mp -= 2 << 8;
        i32 dx, dy; aim_dir(9 * FX, &dx, &dy);
        dmg = roll_dmg(SC_SPELL, 110, &crit);
        Proj *q = spawn_proj(PK_BOLT, 0, p->x, p->y, dx, dy, 100, dmg);
        if (q) { q->crit = (u8)crit; q->chain = (p->powers & P_CHAIN) ? 2 : 0; }
        sfx_bolt();
    } else {
        int strikes = (p->powers & P_TWIN) ? 2 : 1;
        for (int s = 0; s < strikes; s++) melee_arc(100, (p->powers & P_CLEAVE) ? 420 : 340, (p->powers & P_CLEAVE) ? -256 : 40, 0);
        sfx_swing();
    }
}

static int skill_usable(int id, int say) {
    int t = SKILLS[id].tree;
    if (t == 0 && G.p.wtype != B_SWORD && G.p.wtype != -1) { if (say) msg("NEEDS A SWORD", RGB(255, 140, 120)); return 0; }
    if (t == 1 && G.p.wtype != B_BOW) { if (say) msg("NEEDS A BOW", RGB(255, 140, 120)); return 0; }
    return 1;
}

static void cast_skill(int id) {
    Player *p = &G.p;
    if (!p->sk[id] || SKILLS[id].passive) return;
    if (p->skcd[id] > 0 || p->roll_t > 0) return;
    if (!skill_usable(id, 1)) return;
    int cost = skill_cost(id);
    if (p->mp < cost << 8) { if (pressed(BTN_X | BTN_Y | BTN_L | BTN_R)) msg("NOT ENOUGH MANA", RGB(120, 160, 255)); return; }
    p->mp -= cost << 8;
    p->skcd[id] = (i16)skill_cd(id); g_casts++;
    int v = skill_val(id), crit; i32 dmg, dx, dy, tx, ty;
    p->atk_anim = 10; p->atk_cd = MAX(p->atk_cd, 8);
    if (id != SK_CHARGE && id != SK_WHIRL && id != SK_SHIELD) {
        set_anim(SKILLS[id].tree == 2 ? HA_CAST : HA_ATTACK, 16); p->act_fired = 1; p->vx = p->vy = 0;
        Mob *t = find_target(9 * FX, 0);
        if (t && SKILLS[id].tree != 0) { i32 d = MAX(1, dist_fx(t->x, t->y, p->x, p->y)); p->dirx = (t->x - p->x) * 256 / d; p->diry = (t->y - p->y) * 256 / d; }
    }
    switch (id) {
    case SK_CLEAVE: melee_arc(v, 440, -40, 0); sfx_swing(); burst(p->x + p->dirx, p->y + p->diry, 10, pal(5), 90); break;
    case SK_CHARGE: p->charge_t = 16; memset(p->charge_hit, 0, sizeof p->charge_hit); snd_play(300, 200, 3, 2200, 1); break;
    case SK_WARCRY:
        p->warcry_t = 480; player_recalc();
        for (int i = 0; i < MAX_MOBS; i++) { Mob *m = &G.mobs[i]; if (m->alive && dist_fx(m->x, m->y, p->x, p->y) < 3 * FX) { m->stun = (i16)(m->boss ? 20 : 60 + 6 * p->sk[id]); m->awake = 1; } }
        add_efx(FX_RING, p->x, p->y, 18, 3 * FX, 0, 0, pal(15)); snd_play(150, 400, -1, 3000, 0); snd_play(220, 400, -1, 2000, 1);
        break;
    case SK_WHIRL: p->whirl_t = 90; break;
    case SK_QUAKE: add_efx(FX_QUAKE, p->x, p->y, 30, 4 * FX, v, 0, pal(11)); G.shake = 12; snd_play(60, 600, 0, 3500, 1); break;
    case SK_MULTI: {
        aim_dir(9 * FX, &dx, &dy); int n = 4 + p->sk[id] / 3;
        for (int k = 0; k < n; k++) { i32 vx = dx, vy = dy; rotate(&vx, &vy, (k - n / 2) * 3); dmg = roll_dmg(SC_RANGE, v, &crit);   // wider fan: fewer arrows stack on one target
            Proj *q = spawn_proj(PK_ARROW, 0, p->x, p->y, vx, vy, 125, dmg); if (q) { q->crit = (u8)crit; q->pierce = (p->powers & P_PIERCE) ? 1 : 0; q->chain = 1; } }   // chain=1: skill arrow, no mana refund
        sfx_shoot(); break; }
    case SK_PIERCE: { aim_dir(10 * FX, &dx, &dy); dmg = roll_dmg(SC_RANGE, v, &crit);
        Proj *q = spawn_proj(PK_BIG, 0, p->x, p->y, dx, dy, 145, dmg); if (q) { q->pierce = 1; q->crit = (u8)crit; q->life = 50; }
        snd_play(700, 120, -6, 2400, 0); break; }
    case SK_EXPLO: { aim_dir(10 * FX, &dx, &dy); dmg = roll_dmg(SC_RANGE, v, &crit);
        Proj *q = spawn_proj(PK_EXPLO, 0, p->x, p->y, dx, dy, 115, dmg); if (q) q->crit = (u8)crit; sfx_shoot(); break; }
    case SK_RAIN: target_point(9 * FX, &tx, &ty); dmg = roll_dmg(SC_RANGE, v, &crit); add_efx(FX_RAIN, tx, ty, 60, 2 * FX, dmg, crit, pal(26)); sfx_shoot(); break;
    case SK_TRAP: {
        int n = 0, oldest = -1, ot = 0x7fff;
        for (int i = 0; i < MAX_EFX; i++) if (G.efx[i].alive && G.efx[i].kind == FX_TRAP) { n++; if (G.efx[i].t < ot) { ot = G.efx[i].t; oldest = i; } }
        if (n >= 3 && oldest >= 0) G.efx[oldest].alive = 0;
        dmg = roll_dmg(SC_RANGE, v, &crit); add_efx(FX_TRAP, p->x, p->y, 1200, 2 * FX, dmg, crit, pal(28)); snd_play(900, 60, 0, 1500, 1); break; }
    case SK_FIRE: { aim_dir(10 * FX, &dx, &dy); dmg = roll_dmg(SC_SPELL, v, &crit);
        Proj *q = spawn_proj(PK_FIRE, 0, p->x, p->y, dx, dy, 130, dmg); if (q) q->crit = (u8)crit; snd_play(400, 150, -2, 2000, 1); break; }
    case SK_NOVA:
        for (int i = 0; i < MAX_MOBS; i++) { Mob *m = &G.mobs[i]; if (m->alive && dist_fx(m->x, m->y, p->x, p->y) < 5 * FX / 2) { dmg = roll_dmg(SC_SPELL, v, &crit); hit_mob_ex(m, dmg, crit, 0, 90, 0); } }
        add_efx(FX_RING, p->x, p->y, 16, 5 * FX / 2, 0, 0, pal(22)); burst(p->x, p->y, 24, pal(22), 140); snd_play(1800, 300, -5, 2000, 1); break;
    case SK_CHAIN: { aim_dir(10 * FX, &dx, &dy); dmg = roll_dmg(SC_SPELL, v, &crit);
        Proj *q = spawn_proj(PK_LIGHT, 0, p->x, p->y, dx, dy, 155, dmg); if (q) { q->crit = (u8)crit; q->chain = (u8)(3 + p->sk[id] / 3); }
        snd_play(2400, 120, -20, 1800, 1); break; }
    case SK_METEOR: target_point(9 * FX, &tx, &ty); dmg = roll_dmg(SC_SPELL, v, &crit); add_efx(FX_METEOR, tx, ty, 40, 9 * FX / 4, dmg, crit, pal(15)); snd_play(200, 600, -1, 1500, 0); break;
    case SK_SHIELD: p->shield = (p->mpmax * v / 100) << 8; p->shield_t = 600; snd_play(600, 300, 3, 1800, 0); burst(p->x, p->y, 16, pal(24), 80); break;
    }
}

static void elixir_name(int type, int tier, Str *s) { s_reset(s); s_add(s, EL_TIER_NAME[tier]); s_add(s, "ELIXIR OF "); s_add(s, EL_NAME[type]); }
static int elixir_best(int type) { for (int k = EL_TIERS - 1; k >= 0; k--) if (G.elix[type][k]) return k; return -1; }
static int drink_elixir(int type) {
    int k = elixir_best(type); Str s;
    if (k < 0) { s_reset(&s); s_add(&s, "NO ELIXIRS OF "); s_add(&s, EL_NAME[type]); msg(s.b, RGB(255, 120, 120)); return 0; }
    if (G.buff_t[type] > 0 && G.buff_tier[type] > k) { msg("A STRONGER ELIXIR IS STILL WORKING", RGB(200, 200, 200)); return 0; }
    G.elix[type][k]--; G.buff_t[type] = (i16)(EL_SECS[k] * TICKS_PER_SEC); G.buff_tier[type] = (u8)k;
    player_recalc(); sfx_potion();
    static const u8 COL[EL_TYPES] = {15, 16, 29};
    burst(G.p.x, G.p.y, 16, pal(COL[type]), 60);
    s_reset(&s); s_chr(&s, '+'); s_int(&s, EL_VAL[type][k]); s_add(&s, EL_STAT[type]);
    float_text(G.p.x, G.p.y - 30, s.b, pal(COL[type]));
    return 1;
}
static void update_buffs(void) {
    for (int t = 0; t < EL_TYPES; t++) if (G.buff_t[t] > 0 && --G.buff_t[t] == 0) {
        player_recalc(); Str s; s_reset(&s); s_add(&s, EL_NAME[t]); s_add(&s, " WEARS OFF"); msg(s.b, RGB(200, 200, 200));
    }
}
static int any_buff(void) { for (int t = 0; t < EL_TYPES; t++) if (G.buff_t[t] > 0) return 1; return 0; }
static int use_slot(int i, int held) {
    Player *p = &G.p; int s = p->slot[i];
    if (s >= SLOT_ELIX && s < SLOT_ELIX + EL_TYPES) return held ? 0 : drink_elixir(s - SLOT_ELIX);
    if (s == SLOT_HPOT || s == SLOT_MPOT) {
        if (held) return 0;
        int *cnt = s == SLOT_HPOT ? &p->hpot : &p->mpot;
        if (*cnt <= 0) { msg(s == SLOT_HPOT ? "NO HEALTH POTIONS" : "NO MANA POTIONS", RGB(255, 120, 120)); return 0; }
        if (s == SLOT_HPOT) { if (p->hp >= p->hpmax << 8) return 0; p->hp = MIN(p->hp + (p->hpmax * 45 / 100 << 8), p->hpmax << 8); burst(p->x, p->y, 12, pal(14), 40); }
        else { if (p->mp >= p->mpmax << 8) return 0; p->mp = MIN(p->mp + (p->mpmax * 60 / 100 << 8), p->mpmax << 8); burst(p->x, p->y, 12, pal(21), 40); }
        (*cnt)--; sfx_potion(); return 1;
    }
    if (s >= 0 && s < NSK) { cast_skill(s); return 1; }
    if (!held) msg("EMPTY SLOT - ASSIGN SKILLS IN START MENU", RGB(200, 200, 200));
    return 0;
}

// ===================== update =====================
static void move_entity(i32 *x, i32 *y, i32 dx, i32 dy, i32 rad) {
    if (dx) {
        i32 nx = *x + dx, ex = (nx + (dx > 0 ? rad : -rad)) >> 8;
        int a = is_solid(ex, (*y - rad) >> 8), b = is_solid(ex, (*y + rad) >> 8);
        if (!a && !b) *x = nx;
        else if (a != b && !is_solid(ex, *y >> 8)) { i32 cy = (*y & ~255) + 128, s = MIN(ABS(dx), ABS(cy - *y)); *y += cy > *y ? s : -s; }
    }
    if (dy) {
        i32 ny = *y + dy, ey = (ny + (dy > 0 ? rad : -rad)) >> 8;
        int a = is_solid((*x - rad) >> 8, ey), b = is_solid((*x + rad) >> 8, ey);
        if (!a && !b) *y = ny;
        else if (a != b && !is_solid(*x >> 8, ey)) { i32 cx = (*x & ~255) + 128, s = MIN(ABS(dy), ABS(cx - *x)); *x += cx > *x ? s : -s; }
    }
}
static const i16 DIR8[9][2] = {
    {-181, -181}, {-256, 0}, {-181, 181},
    {0, -256}, {0, 0}, {0, 256},
    {181, -181}, {256, 0}, {181, 181},
};

enum { IK_NONE, IK_STAIRS, IK_DEEP, IK_CHEST, IK_NPC };
static void find_interact(void) {
    Player *p = &G.p; G.interact_kind = IK_NONE;
    if (G.in_town) {
        i32 bd = 360;
        for (int i = 0; i < NPC_COUNT; i++) {
            if (i == NPC_PORTAL && !G.portal_floor) continue;
            i32 d = dist_fx(p->x, p->y, g_npcs[i].tx * FX + 128, g_npcs[i].ty * FX + 128);
            if (d < bd) { bd = d; G.interact_kind = IK_NPC; G.interact_idx = i; }
        }
        return;
    }
    int t = tile_at(G.ptx, G.pty);
    if (t == T_STAIRS) { G.interact_kind = IK_STAIRS; return; }
    if (t == T_DEEP) { G.interact_kind = IK_DEEP; return; }
    for (int c = 0; c < MAX_CHESTS; c++)
        if (G.chests[c].alive && !G.chests[c].opened && dist_fx(G.chests[c].x, G.chests[c].y, p->x, p->y) < 220) { G.interact_kind = IK_CHEST; G.interact_idx = c; return; }
}
static void start_fade(int target) { G.state = ST_FADE; G.fade = 0; G.fade_dir = 1; G.fade_target = target; }

static void do_interact(void) {
    Player *p = &G.p;
    switch (G.interact_kind) {
    case IK_STAIRS: case IK_DEEP:
        if (G.boss_idx >= 0 && G.mobs[G.boss_idx].alive) { msg("THE GUARDIAN BARS THE WAY", RGB(255, 100, 100)); return; }   // no way down past a living guardian
        sfx_stairs(); start_fade(G.floor + (G.interact_kind == IK_DEEP ? 2 : 1)); return;
    case IK_CHEST: {
        Chest *ch = &G.chests[G.interact_idx];
        ch->opened = 1; drop_item(ch->x, ch->y, 0); if (rn(100) < 15) drop_item(ch->x, ch->y, 0);
        drop_add(DK_GOLD, ch->x, ch->y, rr(10, 20 + G.floor * 6), 0);
        if (rn(2)) drop_add(DK_HPOT, ch->x, ch->y, 1, 0);
        if (rn(3) == 0) drop_gem(ch->x, ch->y);
        if (rn(100) < 20) drop_elixir(ch->x, ch->y);
        burst(ch->x, ch->y, 20, pal(30), 70); snd_play(700, 80, 6, 2000, 1);
        return; }
    case IK_NPC: {
        int k = G.interact_idx; sfx_menu();
        if (k == NPC_ENTRANCE) { G.state = ST_WAYPOINT; G.sel = 0; return; }
        if (k == NPC_PORTAL) { int f = G.portal_floor; G.portal_floor = 0; sfx_stairs(); start_fade(f); return; }
        G.state = ST_NPC; G.npc = k; G.npc_tab = 0; G.cur_region = 1; G.cur_x = 0; G.cur_y = 0; G.sub = 0; G.sel = 0;
        if (k == NPC_MERCHANT) G.cur_region = 0;
        (void)p; return; }
    }
}

static void update_player(void) {
    Player *p = &G.p;
    int sx = ((G.btn & BTN_RIGHT) ? 1 : 0) - ((G.btn & BTN_LEFT) ? 1 : 0);
    int sy = ((G.btn & BTN_DOWN) ? 1 : 0) - ((G.btn & BTN_UP) ? 1 : 0);
    const i16 *dv = DIR8[(sx + 1) * 3 + (sy + 1)];
    for (int i = 0; i < NSK; i++) if (p->skcd[i] > 0) p->skcd[i]--;
    if (p->atk_cd > 0) p->atk_cd--;
    if (p->atk_anim > 0) p->atk_anim--;
    if (p->roll_cd > 0) p->roll_cd--;
    if (p->hurt > 0) p->hurt--;
    if (p->warcry_t > 0 && --p->warcry_t == 0) player_recalc();
    if (p->shield_t > 0 && --p->shield_t == 0) p->shield = 0;

    if (p->anim_t < 1000) p->anim_t++;
    int acting = (p->anim == HA_ATTACK || p->anim == HA_CAST) && p->anim_t < p->anim_len;
    if (p->roll_t > 0) {                     // roll: eased dash, invulnerable, tumble animation
        int k = p->roll_t; p->roll_t--;
        i32 spd = 22 + 40 * k / 16;
        move_entity(&p->x, &p->y, p->rollx * spd / 256, p->rolly * spd / 256, 70);
        p->vx = p->rollx * 24 / 256; p->vy = p->rolly * 24 / 256;
        if (p->roll_t % 3 == 0) burst(p->x, p->y, 1, pal(11), 24);
        if (p->roll_t == 0) set_anim(HA_IDLE, 1);
    } else if (p->charge_t > 0) {            // charge: dash, hitting & stunning
        p->charge_t--; move_entity(&p->x, &p->y, p->dirx * 60 / 256, p->diry * 60 / 256, 70);
        p->adist += 60; p->anim = HA_WALK;
        for (int i = 0; i < MAX_MOBS; i++) {
            Mob *m = &G.mobs[i]; if (!m->alive || (p->charge_hit[i >> 5] & (1u << (i & 31)))) continue;
            if (dist_fx(m->x, m->y, p->x, p->y) < 260 + (m->boss ? 150 : 0)) {
                p->charge_hit[i >> 5] |= 1u << (i & 31); int crit; i32 dmg = roll_dmg(SC_MELEE, skill_val(SK_CHARGE), &crit);
                hit_mob_ex(m, dmg, crit, 0, 50 + 5 * p->sk[SK_CHARGE], 0);
            }
        }
        burst(p->x, p->y, 1, pal(5), 30);
    } else {
        // smoothed movement: quick acceleration, quicker stop; planted while swinging (like Torchlight)
        int spd = 24 * (100 + p->move) / 100;
        i32 tvx = 0, tvy = 0;
        if ((sx || sy) && (!acting || p->whirl_t)) { tvx = dv[0] * spd / 256; tvy = dv[1] * spd / 256; p->dirx = dv[0]; p->diry = dv[1]; }
        int k = (tvx || tvy) ? 92 : 150;
        p->vx += (tvx - p->vx) * k / 256; p->vy += (tvy - p->vy) * k / 256;
        if (!tvx && ABS(p->vx) < 2) p->vx = 0;
        if (!tvy && ABS(p->vy) < 2) p->vy = 0;
        i32 ox = p->x, oy = p->y;
        if (p->vx || p->vy) move_entity(&p->x, &p->y, p->vx, p->vy, 70);
        i32 moved = ABS(p->x - ox) + ABS(p->y - oy);
        u32 before = (u32)p->adist / 208;
        p->adist += moved * 181 / 256;
        if ((u32)p->adist / 208 != before) snd_play(140, 18, 0, 380, 1);   // soft footstep twice per stride
        if (!acting && p->anim != HA_HURT) p->anim = moved > 3 ? HA_WALK : HA_IDLE;
        if (p->anim == HA_HURT && p->anim_t >= p->anim_len) p->anim = HA_IDLE;
        p->walk = moved > 3 ? p->walk + 1 : 0;
        if (pressed(BTN_B) && p->roll_cd == 0) {
            p->roll_t = 15; p->roll_cd = 48; p->rollx = (sx || sy) ? dv[0] : p->dirx; p->rolly = (sx || sy) ? dv[1] : p->diry;
            p->dirx = p->rollx; p->diry = p->rolly;
            set_anim(HA_ROLL, 15); p->act_fired = 1;
            snd_play(300, 90, -2, 1200, 1);
        }
    }
    if (p->whirl_t > 0) {
        p->whirl_t--;
        if (p->whirl_t % 10 == 0) { melee_arc(skill_val(SK_WHIRL), 460, -256, 0); sfx_swing(); }
    }
    // the attack lands on its impact frame
    if (p->anim == HA_ATTACK && !p->act_fired && p->anim_t >= p->anim_len * (p->wtype == B_BOW ? 60 : p->wtype == B_STAFF ? 55 : 48) / 100) {
        p->act_fired = 1; basic_attack();
    }
    // A: interact or start an attack
    find_interact();
    if (pressed(BTN_A) && G.interact_kind != IK_NONE) do_interact();
    else if ((G.btn & BTN_A) && p->atk_cd == 0 && p->roll_t == 0 && p->charge_t == 0 && G.interact_kind == IK_NONE && !G.in_town) {
        if (p->wtype == B_STAFF && p->mp < 2 << 8) { if (pressed(BTN_A)) msg("NOT ENOUGH MANA", RGB(120, 160, 255)); }
        else {
            int len = attack_frames() + 3;
            set_anim(HA_ATTACK, len); p->act_fired = 0; p->atk_cd = (i16)len; p->atk_anim = (i16)len;
            Mob *t = find_target(p->wtype == B_SWORD || p->wtype < 0 ? 3 * FX : 9 * FX, 0);   // turn to face the nearest foe
            if (t) { i32 d = MAX(1, dist_fx(t->x, t->y, p->x, p->y)); p->dirx = (t->x - p->x) * 256 / d; p->diry = (t->y - p->y) * 256 / d; }
            p->vx = p->vx / 3; p->vy = p->vy / 3;
        }
    }
    if (G.state != ST_PLAY) return;
    // skills
    static const u32 SB[4] = {BTN_X, BTN_Y, BTN_L, BTN_R};
    if (!G.in_town) for (int i = 0; i < 4; i++) {
        if (pressed(SB[i])) use_slot(i, 0);
        else if ((G.btn & SB[i]) && p->slot[i] >= 0 && p->slot[i] < NSK && p->skcd[p->slot[i]] == 0) use_slot(i, 1);
    } else for (int i = 0; i < 4; i++) if (pressed(SB[i])) msg("SKILLS REST WITHIN THE TOWN WALLS", RGB(200, 200, 200));
    // regen
    int rg = p->regen + p->hpmax / 100 + ((p->powers & P_DEEPHEART) ? p->hpmax * 2 / 100 : 0);
    p->hp = MIN(p->hp + rg * 256 / 60, p->hpmax << 8);
    p->mp = MIN(p->mp + (p->mregen * 3 + p->mpmax * 6 / 100 + 1) * 256 / 60, p->mpmax << 8);
    int tx = p->x >> 8, ty = p->y >> 8;
    if (tx != G.ptx || ty != G.pty) { G.ptx = tx; G.pty = ty; refresh_vis(); }
}

static int bag_free(void) { for (int i = 0; i < BAG_N; i++) if (!G.bag[i].used) return i; return -1; }

static void update_drops(void) {
    Player *p = &G.p;
    for (int i = 0; i < MAX_DROPS; i++) {
        Drop *d = &G.drops[i]; if (!d->alive) continue;
        d->t++;
        if (d->t < 16 || dist_fx(d->x, d->y, p->x, p->y) > 170) continue;
        Str s;
        switch (d->kind) {
        case DK_GOLD: p->gold += (u32)d->amount; d->alive = 0; sfx_gold(); float_num(d->x, d->y - 20, d->amount, pal(30)); break;
        case DK_HPOT: if (p->hpot < 20) { p->hpot++; d->alive = 0; sfx_pickup(); float_text(d->x, d->y - 20, "+HP POT", RGB(255, 120, 120)); } break;
        case DK_MPOT: if (p->mpot < 20) { p->mpot++; d->alive = 0; sfx_pickup(); float_text(d->x, d->y - 20, "+MP POT", RGB(120, 160, 255)); } break;
        case DK_GEM: G.gems[d->amount]++; d->alive = 0; sfx_pickup(); gem_name(d->amount, &s); msg(s.b, RGB(200, 240, 255)); break;
        case DK_ELIX: { int t = d->amount / EL_TIERS, k = d->amount % EL_TIERS;
            if (G.elix[t][k] >= 20) break;
            G.elix[t][k]++; d->alive = 0; sfx_pickup(); elixir_name(t, k, &s); msg(s.b, RGB(255, 200, 120)); break; }
        default: {
            int slot = bag_free();
            if (slot < 0) { if (d->t % 120 == 20) msg("BAG FULL - SELL OR SALVAGE (START)", RGB(255, 120, 120)); continue; }
            G.bag[slot] = d->it; d->alive = 0; sfx_pickup();
            item_name(&d->it, &s); msg(s.b, rarity_col(d->it.rarity));
        }
        }
    }
}

static void update_mobs(void) {
    Player *p = &G.p;
    for (int i = 0; i < MAX_MOBS; i++) {
        Mob *m = &G.mobs[i]; if (!m->alive) continue;
        if (m->flash > 0) m->flash--;
        if (m->burn > 0) {
            m->burn--;
            if (m->burn % 30 == 0) { m->hp -= m->burn_dmg; float_num(m->x, m->y - 30, m->burn_dmg, RGB(255, 150, 40)); if (m->hp <= 0) { mob_die(m); continue; } }
        }
        int tx = m->x >> 8, ty = m->y >> 8;
        i32 d = dist_fx(m->x, m->y, p->x, p->y);
        if (!m->awake) { if (d < 7 * FX && tile_visible(tx, ty)) m->awake = 1; else continue; }
        if (d > 20 * FX) continue;
        if (m->kx || m->ky) { move_entity(&m->x, &m->y, m->kx, m->ky, 60); m->kx = m->kx * 3 / 4; m->ky = m->ky * 3 / 4; if (ABS(m->kx) < 3) m->kx = 0; if (ABS(m->ky) < 3) m->ky = 0; }
        if (m->stun > 0) { m->stun--; m->atk_t = 0; continue; }
        m->anim++;
        const MobDef *def = &MOBDEF[m->type];
        int ranged = def->ranged && !m->boss;
        i32 mdx = 0, mdy = 0; int want_move = 1;
        i32 reach = m->boss ? 340 : (m->type == MT_BRUTE ? 270 : 220);
        if (m->atk_t > 0) {   // attack in progress: wind-up, strike on the hit frame, recover
            m->atk_t++;
            m->dir8 = (u8)dir8_of(p->x - m->x, p->y - m->y);
            if (m->atk_t == MOB_ATK_HIT) {
                if (ranged) {
                    i32 dx = (p->x - m->x) * 256 / MAX(d, 1), dy = (p->y - m->y) * 256 / MAX(d, 1);
                    spawn_proj(def->ranged == 2 ? PK_ORB : PK_MOBARROW, 1, m->x, m->y, dx, dy, def->ranged == 2 ? 30 : 48, m->dmg);
                } else if (d < reach + 70) {
                    hurt_player(m->dmg, m->level, m);
                    if (m->elite == 2 && m->eaff == EA_VAMP) m->hp = MIN(m->hp + m->dmg, m->hpmax);
                    if (G.state == ST_DEAD) return;
                } else snd_play(700, 40, -8, 600, 1);   // whiff
                if (m->boss && ++m->special % 3 == 0) {
                    i32 vx = 256, vy = 0;
                    for (int k = 0; k < 12; k++) {
                        spawn_proj(PK_ORB, 1, m->x, m->y, vx, vy, 26, m->dmg * 2 / 3);
                        for (int r = 0; r < 3; r++) rotate(&vx, &vy, 8);
                        i32 l = MAX(1, dist_fx(vx, vy, 0, 0)); vx = vx * 256 / l; vy = vy * 256 / l;
                    }
                }
            }
            if (m->atk_t >= MOB_ATK_LEN) { m->atk_t = 0; m->cd = (i16)(ranged ? (def->ranged == 2 ? 60 : 40) : m->boss ? 28 : m->type == MT_BRUTE ? 32 : 20); }
            goto separate;
        }
        if (ranged) {
            if (d < 3 * FX) { mdx = m->x - p->x; mdy = m->y - p->y; }
            else if (d < 7 * FX && tile_visible(tx, ty)) want_move = 0;
        } else if (d < reach) want_move = 0;
        if (want_move && !(mdx || mdy)) {
            if (d < 2 * FX || g_flow[ty][tx] == 0xFFFF) { mdx = p->x - m->x; mdy = p->y - m->y; }
            else {
                static const int ox[8] = {1, -1, 0, 0, 1, 1, -1, -1}, oy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
                int best = g_flow[ty][tx], bx = 0, by = 0;
                for (int k = 0; k < 8; k++) {
                    int nx = tx + ox[k], ny = ty + oy[k];
                    if (!in_map(nx, ny) || g_flow[ny][nx] >= best) continue;
                    if (k >= 4 && (is_solid(tx + ox[k], ty) || is_solid(tx, ty + oy[k]))) continue;
                    best = g_flow[ny][nx]; bx = ox[k]; by = oy[k];
                }
                mdx = (tx + bx) * FX + FX / 2 - m->x; mdy = (ty + by) * FX + FX / 2 - m->y;
            }
        }
        if (mdx || mdy) {
            i32 l = dist_fx(mdx, mdy, 0, 0); if (l < 1) l = 1;
            i32 ox = m->x, oy = m->y;
            move_entity(&m->x, &m->y, mdx * m->speed / l, mdy * m->speed / l, 60);
            m->wdist += ABS(m->x - ox) + ABS(m->y - oy);
            m->dir8 = (u8)dir8_of(mdx, mdy);
            if (mdx - mdy != 0) m->face = (mdx - mdy) < 0;
        } else m->dir8 = (u8)dir8_of(p->x - m->x, p->y - m->y);
    separate:
        for (int j = i + 1; j < MAX_MOBS; j++) {
            Mob *o = &G.mobs[j]; if (!o->alive) continue;
            i32 sx = m->x - o->x, sy = m->y - o->y;
            if (ABS(sx) > 140 || ABS(sy) > 140) continue;
            i32 sd = dist_fx(m->x, m->y, o->x, o->y); i32 minsep = (m->boss || o->boss) ? 260 : 130;
            if (sd < minsep) {
                if (sd < 4) { sx = rr(-10, 10); sy = rr(-10, 10); sd = 10; }
                i32 push = (minsep - sd) / 4;
                move_entity(&m->x, &m->y, sx * push / sd, sy * push / sd, 60);
                move_entity(&o->x, &o->y, -sx * push / sd, -sy * push / sd, 60);
            }
        }
        if (m->cd > 0) m->cd--;
        if (m->cd == 0 && m->atk_t == 0) {   // begin a telegraphed attack
            if ((ranged && d < 8 * FX && tile_visible(tx, ty)) || (!ranged && d < reach + 30)) { m->atk_t = 1; m->dir8 = (u8)dir8_of(p->x - m->x, p->y - m->y); }
        }
    }
}

static void update_proj(void) {
    Player *p = &G.p;
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj *q = &G.proj[i]; if (!q->alive) continue;
        q->x += q->vx / 8; q->y += q->vy / 8;
        if (q->kind == PK_FIRE && (G.ticks & 1)) burst(q->x, q->y, 1, pal(15), 10);
        if (q->owner == 0 && breakable_at(q->x >> 8, q->y >> 8)) {   // arrows and bolts smash pots too (and stop there)
            break_prop(q->x >> 8, q->y >> 8);
            if (q->kind == PK_EXPLO) explode(q->x, q->y, q->dmg, q->crit, 460, 0, 0);
            q->alive = 0; continue;
        }
        if (--q->life <= 0 || is_solid(q->x >> 8, q->y >> 8)) {
            if (q->kind == PK_EXPLO) explode(q->x, q->y, q->dmg, q->crit, 460, 0, 0);
            else if (q->kind == PK_BOLT && (p->powers & P_STARFALL)) explode(q->x, q->y, q->dmg / 2, 0, 300, 0, 0);
            q->alive = 0; burst(q->x, q->y, 3, pal(4), 30); continue;
        }
        if (q->owner == 0) {
            for (int j = 0; j < MAX_MOBS; j++) {
                Mob *m = &G.mobs[j]; if (!m->alive || j == q->last_hit) continue;
                if (dist_fx(m->x, m->y, q->x, q->y) >= (m->boss ? 230 : 130)) continue;
                if (q->kind == PK_EXPLO) { explode(q->x, q->y, q->dmg, q->crit, 460, 0, 0); q->alive = 0; break; }
                hit_mob_ex(m, q->dmg, q->crit, q->kind == PK_FIRE, 0, 1);
                if (q->kind == PK_ARROW && !q->chain) p->mp = MIN(p->mp + 128, p->mpmax << 8);   // only basic bow shots restore mana
                q->last_hit = (i8)j;
                if (q->kind == PK_BOLT && (p->powers & P_STARFALL)) explode(q->x, q->y, q->dmg / 2, 0, 300, 0, 0);
                if ((q->kind == PK_BOLT || q->kind == PK_LIGHT) && q->chain > 0) {
                    Mob *best = 0; i32 bd = 5 * FX;
                    for (int k = 0; k < MAX_MOBS; k++) {
                        Mob *o = &G.mobs[k]; if (!o->alive || k == j) continue;
                        i32 dd = dist_fx(o->x, o->y, q->x, q->y); if (dd < bd) { bd = dd; best = o; }
                    }
                    if (best) { q->chain--; i32 sp = q->kind == PK_LIGHT ? 110 : 60; q->vx = (best->x - q->x) * sp / MAX(bd, 1); q->vy = (best->y - q->y) * sp / MAX(bd, 1); q->life = 40; break; }
                }
                if (!q->pierce) q->alive = 0;
                break;
            }
        } else if (dist_fx(p->x, p->y, q->x, q->y) < 110) {
            if (p->roll_t > 0) continue;
            hurt_player(q->dmg, G.floor, 0); q->alive = 0;
            if (G.state == ST_DEAD) return;
        }
    }
}

static void update_efx(void) {
    for (int i = 0; i < MAX_EFX; i++) {
        Effect *e = &G.efx[i]; if (!e->alive) continue;
        e->t--;
        switch (e->kind) {
        case FX_RAIN:
            if (e->t % 3 == 0) {
                i32 x = e->x + rr(-e->radius, e->radius), y = e->y + rr(-e->radius, e->radius);
                for (int k = 0; k < MAX_PART; k++) if (!G.part[k].alive) { Part *q = &G.part[k]; q->alive = 1; q->x = x; q->y = y; q->z = 40 << 8; q->vx = q->vy = 0; q->vz = -300; q->t = 14; q->col = pal(26); break; }
                for (int j = 0; j < MAX_MOBS; j++) { Mob *m = &G.mobs[j]; if (m->alive && dist_fx(m->x, m->y, x, y) < 190 + (m->boss ? 120 : 0)) hit_mob_ex(m, e->dmg, e->crit, 0, 0, 0); }
            }
            break;
        case FX_METEOR: if (e->t <= 0) { explode(e->x, e->y, e->dmg, e->crit, e->radius, 1, 20); G.shake = 8; } break;
        case FX_TRAP:
            for (int j = 0; j < MAX_MOBS; j++) {
                Mob *m = &G.mobs[j]; if (!m->alive || dist_fx(m->x, m->y, e->x, e->y) > 230) continue;
                explode(e->x, e->y, e->dmg, e->crit, e->radius, 0, 70); e->alive = 0; break;
            }
            break;
        case FX_QUAKE: {
            i32 rad = (e->t0 - e->t) * e->radius / e->t0;
            for (int j = 0; j < MAX_MOBS; j++) {
                Mob *m = &G.mobs[j]; if (!m->alive || (e->hit[j >> 5] & (1u << (j & 31)))) continue;
                if (dist_fx(m->x, m->y, e->x, e->y) < rad) {
                    e->hit[j >> 5] |= 1u << (j & 31);
                    int crit; i32 dmg = roll_dmg(SC_MELEE, e->dmg, &crit); hit_mob_ex(m, dmg, crit, 0, 40, 0);
                }
            }
            if (e->t % 2 == 0) for (int k = 0; k < 6; k++) { i32 vx = 256, vy = 0; rotate(&vx, &vy, rn(17) - 8); if (rn(2)) { vx = -vx; vy = -vy; } if (rn(2)) { i32 t = vx; vx = -vy; vy = t; } burst(e->x + vx * rad / 256, e->y + vy * rad / 256, 1, pal(11), 30); }
            break; }
        }
        if (e->t <= 0) e->alive = 0;
    }
}

// ---- ambient particles (pure decoration): dust motes, embers, fireflies, falling leaves ----
enum { AM_MOTE, AM_EMBER, AM_FLY, AM_LEAF };
typedef struct { u8 alive, kind; i32 x, y, z, vx, vy, vz; i16 t, t0; } Amb;
#define MAX_AMB 64
static Amb g_amb[MAX_AMB];
static Amb *amb_new(int kind, i32 x, i32 y, i32 z, int life) {
    for (int i = 0; i < MAX_AMB; i++) if (!g_amb[i].alive) {
        Amb *a = &g_amb[i]; a->alive = 1; a->kind = (u8)kind; a->x = x; a->y = y; a->z = z; a->vx = a->vy = a->vz = 0; a->t = a->t0 = (i16)life; return a;
    }
    return 0;
}
static void update_impacts(void) { for (int i = 0; i < MAX_IMPACT; i++) if (g_impact[i].t) g_impact[i].t--; }
static void update_amb(void) {
    int n[4] = {0, 0, 0, 0};
    for (int i = 0; i < MAX_AMB; i++) {
        Amb *a = &g_amb[i]; if (!a->alive) continue;
        if ((a->kind == AM_FLY || a->kind == AM_LEAF) != G.in_town && a->kind != AM_EMBER) { a->alive = 0; continue; }   // left over from the other area
        n[a->kind]++;
        switch (a->kind) {
        case AM_MOTE: a->vx += rr(-2, 2); a->vy += rr(-2, 2); a->vz += rr(-3, 3); a->vx = MAX(-14, MIN(14, a->vx)); a->vy = MAX(-10, MIN(10, a->vy)); a->vz = MAX(-30, MIN(30, a->vz)); break;
        case AM_EMBER: a->vx += rr(-6, 6); a->vx = MAX(-30, MIN(30, a->vx)); break;
        case AM_FLY: a->vx += rr(-5, 5); a->vy += rr(-5, 5); a->vz += rr(-8, 8); a->vx = MAX(-24, MIN(24, a->vx)); a->vy = MAX(-20, MIN(20, a->vy)); a->vz = MAX(-60, MIN(60, a->vz)); if (a->z < (6 << 8)) a->vz = 40; break;
        case AM_LEAF: a->vx = (i32)(((G.ticks + i * 7) & 31) < 16 ? 24 : -24); break;
        }
        a->x += a->vx / 8; a->y += a->vy / 8; a->z += a->vz / 8;
        if (--a->t <= 0 || a->z < 0) a->alive = 0;
    }
    Player *p = &G.p;
    // dust drifting through the air (dungeon) / fireflies over the grass (town)
    if (!G.in_town && n[AM_MOTE] < (g_gfx_effects ? 40 : 22) && rn(g_gfx_effects ? 2 : 3) == 0) {
        Amb *a = amb_new(AM_MOTE, p->x + rr(-5 * FX, 5 * FX), p->y + rr(-5 * FX, 5 * FX), rr(8, 40) << 8, rr(120, 260));
        if (a) { a->vx = rr(-8, 8); a->vy = rr(-6, 6); }
    }
    if (G.in_town && n[AM_FLY] < 14 && rn(4) == 0) {
        i32 x = p->x + rr(-6 * FX, 6 * FX), y = p->y + rr(-5 * FX, 5 * FX);
        int t = tile_at(x >> 8, y >> 8);
        if (t == T_GRASS || t == T_TREE || t == T_PROP) amb_new(AM_FLY, x, y, rr(6, 24) << 8, rr(150, 300));
    }
    // embers from torches, braziers and candles; leaves from trees
    if ((G.ticks & 1) == 0) for (int ty = G.pty - 6; ty <= G.pty + 6; ty++) for (int tx = G.ptx - 6; tx <= G.ptx + 6; tx++) {
        if (!in_map(tx, ty)) continue;
        int tq = g_torch[ty][tx];
        if (tq && rn(tq == 1 ? 10 : 7) == 0 && n[AM_EMBER] < 26) {
            i32 ex = tx * FX + 128 + rr(-40, 40), ey = ty * FX + (tq == 1 ? 300 : 140);
            i32 ez = (tq == 1 ? 26 : g_prop[ty][tx] == PR_TORCH ? 30 : g_prop[ty][tx] == PR_BRAZIER ? 14 : 8) << 8;
            Amb *a = amb_new(AM_EMBER, ex, ey, ez, rr(24, 48)); if (a) { a->vz = rr(60, 130); a->vx = rr(-12, 12); }
            n[AM_EMBER]++;
        }
        if (G.in_town && g_map[ty][tx] == T_TREE && rn(900) == 0 && n[AM_LEAF] < 6) {
            Amb *a = amb_new(AM_LEAF, tx * FX + 128 + rr(-80, 80), ty * FX + 128, rr(30, 40) << 8, 200); if (a) a->vz = -14;
        }
    }
}
static void update_fx(void) {
    update_amb(); update_impacts();
    for (int i = 0; i < MAX_FLOAT; i++) if (G.ft[i].alive && --G.ft[i].t <= 0) G.ft[i].alive = 0;
    for (int i = 0; i < MAX_PART; i++) {
        Part *q = &G.part[i]; if (!q->alive) continue;
        q->x += q->vx / 8; q->y += q->vy / 8; q->z += q->vz / 8; q->vz -= 12;
        if (q->z < 0) { q->z = 0; q->vz = -q->vz / 3; }
        if (--q->t <= 0) q->alive = 0;
    }
    for (int i = 0; i < 3; i++) if (G.msg_t[i] > 0) G.msg_t[i]--;
    if (G.shake > 0) G.shake--;
}

// ---- game speed: logic runs at 54 ticks/s (90% of 60); rendering interpolates between ticks ----
#define TICK_HZ_NUM 26   // ticks per 30 displayed frames: 52 ticks/s (about 87% speed)
#define TICK_HZ_DEN 30
static int g_alpha = 256;   // 0..256: how far the render is between the previous and the current tick
static i32 g_prv_px, g_prv_py, g_prv_cx, g_prv_cy, g_prv_mx[MAX_MOBS], g_prv_my[MAX_MOBS], g_prv_qx[MAX_PROJ], g_prv_qy[MAX_PROJ];
static i32 g_sav_px, g_sav_py, g_sav_cx, g_sav_cy, g_sav_mx[MAX_MOBS], g_sav_my[MAX_MOBS], g_sav_qx[MAX_PROJ], g_sav_qy[MAX_PROJ];
static void lerp_snapshot(void) {
    g_prv_px = G.p.x; g_prv_py = G.p.y; g_prv_cx = G.camx; g_prv_cy = G.camy;
    for (int i = 0; i < MAX_MOBS; i++) { g_prv_mx[i] = G.mobs[i].x; g_prv_my[i] = G.mobs[i].y; }
    for (int i = 0; i < MAX_PROJ; i++) { g_prv_qx[i] = G.proj[i].x; g_prv_qy[i] = G.proj[i].y; }
}
static inline i32 lerp1(i32 a, i32 b) { i32 d = b - a; return (d > 512 || d < -512) ? b : a + d * g_alpha / 256; }
static void lerp_begin(void) {
    if (g_alpha >= 256) return;
    g_sav_px = G.p.x; g_sav_py = G.p.y; g_sav_cx = G.camx; g_sav_cy = G.camy;
    G.p.x = lerp1(g_prv_px, G.p.x); G.p.y = lerp1(g_prv_py, G.p.y); G.camx = lerp1(g_prv_cx, G.camx); G.camy = lerp1(g_prv_cy, G.camy);
    for (int i = 0; i < MAX_MOBS; i++) { Mob *m = &G.mobs[i]; g_sav_mx[i] = m->x; g_sav_my[i] = m->y; if (m->alive) { m->x = lerp1(g_prv_mx[i], m->x); m->y = lerp1(g_prv_my[i], m->y); } }
    for (int i = 0; i < MAX_PROJ; i++) { Proj *q = &G.proj[i]; g_sav_qx[i] = q->x; g_sav_qy[i] = q->y; if (q->alive) { q->x = lerp1(g_prv_qx[i], q->x); q->y = lerp1(g_prv_qy[i], q->y); } }
}
static void lerp_end(void) {
    if (g_alpha >= 256) return;
    G.p.x = g_sav_px; G.p.y = g_sav_py; G.camx = g_sav_cx; G.camy = g_sav_cy;
    for (int i = 0; i < MAX_MOBS; i++) { G.mobs[i].x = g_sav_mx[i]; G.mobs[i].y = g_sav_my[i]; }
    for (int i = 0; i < MAX_PROJ; i++) { G.proj[i].x = g_sav_qx[i]; G.proj[i].y = g_sav_qy[i]; }
}
void game_tick(u32 buttons);
// called once per displayed frame (frames = display frames elapsed): runs 0..n logic ticks and sets the blend
static u32 g_tick_acc;
void game_frame(u32 buttons, u32 frames) {
    g_tick_acc += TICK_HZ_NUM * frames;
    int n = 0;
    while (g_tick_acc >= TICK_HZ_DEN && n < 5) { lerp_snapshot(); game_tick(buttons); g_tick_acc -= TICK_HZ_DEN; n++; }
    if (g_tick_acc >= TICK_HZ_DEN) g_tick_acc %= TICK_HZ_DEN;
    g_alpha = (int)(g_tick_acc * 256 / TICK_HZ_DEN);
}
void game_tick(u32 buttons) {
    G.ticks++; g_frame_counter = (u16)G.ticks;
    {   // in play the buttons go through the player's mapping; menus always use the buttons as printed
        static u32 raw_prev; u32 b = buttons, bp = raw_prev; raw_prev = buttons;
        if (G.state == ST_PLAY) { b = map_buttons(b); bp = map_buttons(bp); }
        G.btn_prev = bp; G.btn = b; G.pressed = b & ~bp;
    }
    switch (G.state) {
    case ST_TITLE:
        if (pressed(BTN_START) || pressed(BTN_A)) { open_select(); sfx_menu(); }
        break;
    case ST_SELECT: update_select(); break;
    case ST_PLAY:
        if (G.world_fade && ++G.world_fade >= WORLD_FADE_IN) G.world_fade = 0;
        if (G.hitstop > 0) { G.hitstop--; G.pressed_acc |= G.pressed; update_fx(); break; }
        G.pressed |= G.pressed_acc; G.pressed_acc = 0;
        if (pressed(BTN_START)) { G.state = ST_MENU; G.tab = 0; G.cur_region = 1; G.cur_x = 0; G.cur_y = 0; G.assign = 0; g_sys_page = 0; g_rebind = -1; sfx_menu(); break; }
        if (pressed(BTN_SELECT)) G.show_map ^= 1;
        update_player(); if (G.state != ST_PLAY) break;
        update_buffs();
        if (!G.in_town) { update_mobs(); if (G.state != ST_PLAY) break; update_proj(); if (G.state != ST_PLAY) break; update_efx(); }
        update_drops(); update_fx();
        {   // camera glides after the hero with a little lead in the direction of travel
            i32 tx = G.p.x + G.p.vx * 10, ty = G.p.y + G.p.vy * 10;
            G.camx += (tx - G.camx) / 7; G.camy += (ty - G.camy) / 7;
        }
        for (int i = 0; i < MAX_CORPSES; i++) if (G.corpses[i].alive && --G.corpses[i].t <= 0) G.corpses[i].alive = 0;
        break;
    case ST_MENU: update_menu(); update_fx(); break;
    case ST_NPC: update_npc(); update_fx(); break;
    case ST_WAYPOINT: update_waypoint(); break;
    case ST_DEAD:
        update_fx(); if (G.p.anim_t < 1000) G.p.anim_t++;
        if (G.dead_t < DEATH_HOLD) G.dead_t++;
        if (G.dead_t >= DEATH_HOLD && (pressed(BTN_START) || pressed(BTN_A))) {
            G.p.gold -= (u32)G.death_gold; G.portal_floor = 0;
            enter_town(0); G.state = ST_PLAY;
            Str s; s_reset(&s); s_add(&s, "YOU AWAKEN IN TOWN. LOST "); s_int(&s, G.death_gold); s_add(&s, " GOLD"); msg(s.b, RGB(255, 140, 120));
        }
        break;
    case ST_FADE:
        G.fade += G.fade_dir * 3;
        if (G.fade >= 32 && G.fade_dir > 0) { G.fade = 32; G.fade_dir = -1; if (G.fade_target > 0) enter_floor(G.fade_target); else enter_town(G.fade_target == 0 ? 1 : 2); }
        if (G.fade <= 0 && G.fade_dir < 0) { G.fade = 0; G.state = ST_PLAY; }
        break;
    }
}

// ===================== save states (the Pocket's sleep mode / Memories) =====================
// Everything that changes during play lives in G; the map itself is rebuilt from the hero's seed and depth,
// then what had been explored is put back.
#define SS_MAGIC 0x4C445353u   // "LDSS"
typedef struct { u32 magic, ver, bytes, sum, rng; } SsHead;
static u32 ss_sum(const u8 *p, u32 n) { u32 h = 0x811C9DC5u; for (u32 i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
// G holds pointers into the program, so a state only fits the exact build that made it
#define SS_VER (ss_sum((const u8 *)__DATE__ " " __TIME__, sizeof(__DATE__ " " __TIME__)) ^ (u32)sizeof G)
// pieces of the state, in order (the map is rebuilt; g_seen goes on top of it afterwards)
#define SS_PARTS(X) X(&G, sizeof G) X(g_seen, sizeof g_seen) X(g_amb, sizeof g_amb) X(g_impact, sizeof g_impact) X(&g_tick_acc, sizeof g_tick_acc)
#define SS_ADD(p, n) + (u32)(n)
#define SS_BYTES ((u32)sizeof(SsHead) SS_PARTS(SS_ADD))
static u32 ss_save(u8 *buf, u32 cap) {   // returns bytes written
    if (SS_BYTES > cap) return 0;
    u8 *o = buf + sizeof(SsHead);
#define SS_PUT(p, n) memcpy(o, (p), (n)); o += (n);
    SS_PARTS(SS_PUT)
    SsHead h = {SS_MAGIC, SS_VER, SS_BYTES, ss_sum(buf + sizeof h, SS_BYTES - sizeof h), g_rng};
    memcpy(buf, &h, sizeof h);
    return SS_BYTES;
}
static int ss_load(const u8 *buf) {
    SsHead h; memcpy(&h, buf, sizeof h);
    if (h.magic != SS_MAGIC || h.ver != SS_VER || h.bytes != SS_BYTES) return 0;
    if (ss_sum(buf + sizeof h, h.bytes - sizeof h) != h.sum) return 0;
    memcpy(&G, buf + sizeof h, sizeof G);
    if (G.in_town) world_town(G.seed); else build_floor(G.floor);   // the map, from the seed
    const u8 *in = buf + sizeof h;
#define SS_GET(p, n) memcpy((p), in, (n)); in += (n);
    SS_PARTS(SS_GET)
    g_rng = h.rng;
    refresh_vis(); lerp_snapshot();
    return 1;
}

void game_init(u32 seed) {
    memset(&G, 0, sizeof G); memset(&g_persist, 0, sizeof g_persist);   // the browser / SD card fill the slots after init
    settings_apply();
    gfx_init(); tiles_init();
    G.seed = seed ? seed : 0xC0FFEE; G.state = ST_TITLE;
}

#include "render.c"
#include "menu.c"
