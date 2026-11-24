// Loot: bases, affixes, rarities, uniques (legendary & majestic) and item generation.
#include "common.h"

// ---------- RNG (xorshift32) ----------
static u32 g_rng = 1;
static u32 rnd(void) { u32 x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x ? x : 0x9E3779B9u; }
static int rn(int n) { return n <= 0 ? 0 : (int)(((u64)rnd() * (u32)n) >> 32); }      // [0,n)
static int rr(int lo, int hi) { return lo + rn(hi - lo + 1); }                          // [lo,hi]
static u32 hash32(u32 x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }

// Scaling curve (percent): grows ~linearly early, quadratically late. Used by mobs & item level.
static i32 scale_hp(int lvl) { int l = lvl - 1; return 100 + 22 * l + (l * l * 12) / 10; }
static i32 scale_dmg(int lvl) { int l = lvl - 1; return 100 + 14 * l + (l * l * 6) / 10; }

// ---------- rarity ----------
enum { R_COMMON, R_RARE, R_ULTRA, R_EPIC, R_LEGEND, R_MAJESTIC, R_COUNT };
static const char *RARITY_NAME[R_COUNT] = {"COMMON", "RARE", "ULTRA RARE", "EPIC", "LEGENDARY", "MAJESTIC"};
static const u8 RARITY_RGB[R_COUNT][3] = {{200, 200, 196}, {72, 210, 80}, {70, 130, 255}, {176, 90, 240}, {255, 140, 30}, {255, 210, 60}};
static u16 g_frame_counter;
static u16 rarity_col(int r) {
    if (r == R_MAJESTIC && (g_frame_counter & 16)) return RGB(255, 246, 180);    // gold shimmer
    return RGB(RARITY_RGB[r][0], RARITY_RGB[r][1], RARITY_RGB[r][2]);
}

// ---------- bases & slots ----------
enum { B_SWORD, B_BOW, B_STAFF, B_SHIELD, B_TOME, B_HELM, B_AMULET, B_CHEST, B_LEGS, B_BOOTS, B_GLOVES, B_RING, B_CAPE, B_QUIVER, B_COUNT };
enum { S_HEAD, S_NECK, S_BODY, S_LEGS, S_BOOTS, S_GLOVES, S_RING1, S_RING2, S_BACK, S_MAIN, S_OFF, S_COUNT };
static const char *SLOT_NAME[S_COUNT] = {"HEAD", "NECK", "BODY", "LEGS", "BOOTS", "GLOVES", "RING", "RING", "BACK", "MAIN HAND", "OFF HAND"};
static const u8 BASE_SLOT[B_COUNT] = {S_MAIN, S_MAIN, S_MAIN, S_OFF, S_OFF, S_HEAD, S_NECK, S_BODY, S_LEGS, S_BOOTS, S_GLOVES, S_RING1, S_BACK, S_BACK};
static const char *BASE_KIND[B_COUNT] = {"SWORD", "BOW", "STAFF", "SHIELD", "TOME", "HELM", "AMULET", "BODY ARMOR", "LEG ARMOR", "BOOTS", "GLOVES", "RING", "CAPE", "QUIVER"};
static const char *BASE_NAMES[B_COUNT][6] = {
    {"RUSTY BLADE", "SHORT SWORD", "BROADSWORD", "FALCHION", "RUNEBLADE", "STARSTEEL EDGE"},
    {"CRUDE BOW", "HUNTING BOW", "LONGBOW", "COMPOSITE BOW", "WYRMHORN BOW", "CELESTIAL BOW"},
    {"GNARLED STICK", "OAK STAFF", "RUNE STAFF", "ARCHON STAFF", "VOIDWOOD STAFF", "ASTRAL SCEPTER"},
    {"PLANK SHIELD", "BUCKLER", "KITE SHIELD", "TOWER SHIELD", "AEGIS", "BASTION"},
    {"TATTERED TOME", "PRIMER", "GRIMOIRE", "CODEX", "ARCANUM", "ETERNAL TOME"},
    {"LEATHER CAP", "IRON HELM", "GREAT HELM", "VISAGE", "WARCROWN", "HALO HELM"},
    {"BONE CHARM", "PENDANT", "TALISMAN", "MEDALLION", "SIGIL", "STAR AMULET"},
    {"RAGS", "LEATHER VEST", "CHAINMAIL", "PLATEMAIL", "DRAGONSCALE", "SERAPH PLATE"},
    {"BREECHES", "LEATHER PANTS", "CHAIN LEGGINGS", "PLATE GREAVES", "SCALE GREAVES", "SERAPH GREAVES"},
    {"SANDALS", "LEATHER BOOTS", "CHAIN BOOTS", "SABATONS", "WYRM TREADS", "SERAPH STRIDERS"},
    {"WRAPS", "LEATHER GLOVES", "CHAIN GLOVES", "GAUNTLETS", "WYRM GRIPS", "SERAPH GRASP"},
    {"COPPER RING", "SILVER RING", "GOLD RING", "JEWELED RING", "RUNIC BAND", "STAR BAND"},
    {"TATTERED CAPE", "WOOL CLOAK", "RANGER CLOAK", "NOBLE MANTLE", "SHADOW CLOAK", "STARWEAVE CAPE"},
    {"HIDE QUIVER", "LEATHER QUIVER", "RANGER QUIVER", "HUNTER QUIVER", "WYRM QUIVER", "STAR QUIVER"},
};
static const u8 BASE_ARMOR[B_COUNT] = {0, 0, 0, 5, 0, 3, 0, 6, 4, 2, 2, 0, 1, 0};
static int base_tier(int ilvl) { return ilvl < 6 ? 0 : ilvl < 12 ? 1 : ilvl < 20 ? 2 : ilvl < 30 ? 3 : ilvl < 42 ? 4 : 5; }

// ---------- affixes ----------
enum { A_STR, A_DEX, A_INT, A_VIT, A_MANA, A_DMG, A_ARMOR, A_ASPD, A_CRIT, A_LEECH, A_MOVE, A_MF, A_REGEN, A_MREGEN, A_FIRE, A_BOWDMG, A_BLOCK, A_SPELL, A_COUNT };
static const char *AFFIX_FMT[A_COUNT] = {"STRENGTH", "DEXTERITY", "INTELLECT", "LIFE", "MANA", "% DAMAGE", "ARMOR", "% ATTACK SPEED", "% CRIT CHANCE",
                                         "% LIFE STEAL", "% MOVE SPEED", "% MAGIC FIND", "LIFE REGEN", "MANA REGEN", "FIRE DAMAGE", "% BOW DAMAGE", "% BLOCK", "% SPELL DAMAGE"};
static const char *AFFIX_PRE[A_COUNT] = {"MIGHTY", "NIMBLE", "WISE", "STALWART", "ARCANE", "CRUEL", "STURDY", "SWIFT", "KEEN", "VAMPIRIC", "FLEET", "LUCKY", "VITAL", "MYSTIC", "BLAZING", "DEADEYE", "GUARDING", "SORCEROUS"};
static const char *AFFIX_SUF[A_COUNT] = {"OF THE BEAR", "OF THE FOX", "OF THE OWL", "OF LIFE", "OF STARS", "OF RUIN", "OF STONE", "OF HASTE", "OF PRECISION",
                                         "OF THE LEECH", "OF WINDS", "OF FORTUNE", "OF MENDING", "OF CLARITY", "OF EMBERS", "OF THE HAWK", "OF THE WALL", "OF SORCERY"};

// ---------- uniques ----------
enum { P_IGNITE = 1 << 0, P_SWIFT = 1 << 1, P_KILLHEAL = 1 << 2, P_THORNS = 1 << 3, P_PIERCE = 1 << 4, P_CHAIN = 1 << 5, P_CLEAVE = 1 << 6,
       P_TRIPLE = 1 << 7, P_LIGHT = 1 << 8, P_BLOCK = 1 << 9, P_HASTE = 1 << 10, P_ECHO = 1 << 11,
       P_UNDYING = 1 << 12, P_TWIN = 1 << 13, P_FIVE = 1 << 14, P_STARFALL = 1 << 15, P_DEEPHEART = 1 << 16, P_MIDAS = 1 << 17, P_SHROUD = 1 << 18 };
typedef struct { const char *name; u8 base; u8 rarity; u32 power; const char *desc; } Unique;
static const Unique UNIQUES[] = {
    {"EMBERHEART", B_AMULET, R_LEGEND, P_IGNITE, "HITS SET FOES ABLAZE"},
    {"WOLFSTRIDE", B_BOOTS, R_LEGEND, P_SWIFT, "+25% SPEED, 10% DODGE"},
    {"GRAVECALLER BAND", B_RING, R_LEGEND, P_KILLHEAL, "KILLS RESTORE 4% LIFE"},
    {"THORNHIDE MAIL", B_CHEST, R_LEGEND, P_THORNS, "REFLECT 60% MELEE DAMAGE"},
    {"SUNPIERCER", B_BOW, R_LEGEND, P_PIERCE, "ARROWS PIERCE ALL FOES"},
    {"STORMCALLER", B_STAFF, R_LEGEND, P_CHAIN, "BOLTS CHAIN TO 2 FOES"},
    {"BLOODEDGE", B_SWORD, R_LEGEND, P_CLEAVE, "WIDE CLEAVE, 6% LIFE STEAL"},
    {"QUIVER OF PLENTY", B_QUIVER, R_LEGEND, P_TRIPLE, "EVERY 3RD SHOT FIRES 3"},
    {"LANTERN OF THE DEEP", B_HELM, R_LEGEND, P_LIGHT, "VAST LIGHT, +30% MAGIC FIND"},
    {"WARDENS WALL", B_SHIELD, R_LEGEND, P_BLOCK, "+25% BLOCK CHANCE"},
    {"GLOVES OF HASTE", B_GLOVES, R_LEGEND, P_HASTE, "+25% ATTACK SPEED"},
    {"MANTLE OF ECHOES", B_CAPE, R_LEGEND, P_ECHO, "15% CHANCE TO STRIKE TWICE"},
    {"CROWN OF THE UNDYING", B_HELM, R_MAJESTIC, P_UNDYING, "CHEAT DEATH ONCE PER FLOOR"},
    {"TWINFANG", B_SWORD, R_MAJESTIC, P_TWIN | P_CLEAVE, "EVERY SWING STRIKES TWICE"},
    {"HEAVENSPLIT", B_BOW, R_MAJESTIC, P_FIVE, "ALWAYS FIRES 5 ARROWS"},
    {"STARFALL SCEPTER", B_STAFF, R_MAJESTIC, P_STARFALL, "BOLTS BURST INTO STARS"},
    {"HEART OF THE DEEP", B_AMULET, R_MAJESTIC, P_DEEPHEART, "+50% LIFE, FAST REGEN"},
    {"MIDAS LOOP", B_RING, R_MAJESTIC, P_MIDAS, "+100% GOLD AND MAGIC FIND"},
    {"SHROUD OF NIGHT", B_CAPE, R_MAJESTIC, P_SHROUD, "TAKE 30% LESS DAMAGE"},
};
#define N_UNIQUES COUNT(UNIQUES)

// ---------- item ----------
#define MAX_AFF 6
typedef struct {
    u8 used, base, rarity, naff;
    u8 uniq;            // 0 = none, else UNIQUES index + 1
    u8 pre, suf;        // name parts (affix ids) for ultra/epic
    u8 sockets;
    u8 gem[3];          // 0 = empty socket, else gem id + 1
    u16 ilvl, dmin, dmax, armor;
    u8 at[MAX_AFF]; i16 av[MAX_AFF];
} Item;

static int affix_allowed(int a, int base) {
    if (a == A_BOWDMG) return base == B_QUIVER || base == B_BOW;
    if (a == A_BLOCK) return base == B_SHIELD;
    if (a == A_SPELL) return base == B_TOME || base == B_STAFF || base == B_AMULET;
    if (a == A_FIRE) return base <= B_STAFF || base == B_GLOVES || base == B_RING || base == B_AMULET;
    if (a == A_MOVE) return base == B_BOOTS || base == B_CAPE || base == B_AMULET;
    if (a == A_ASPD) return base <= B_STAFF || base == B_GLOVES || base == B_QUIVER || base == B_RING;
    if (a == A_ARMOR) return BASE_ARMOR[base] > 0 || base == B_CAPE;
    if (a == A_LEECH) return base <= B_STAFF || base == B_RING || base == B_AMULET || base == B_GLOVES;
    return 1;
}

static int affix_roll(int a, int ilvl, int rarity) {
    int s = scale_dmg(ilvl), v;
    switch (a) {
    case A_STR: case A_DEX: case A_INT: v = rr(1 + ilvl / 2, 3 + ilvl); break;
    case A_VIT: v = rr(3, 8) * scale_hp(ilvl) / 100; break;
    case A_MANA: v = rr(3, 8) * (10 + ilvl) / 10; break;
    case A_DMG: v = rr(5, 14) + ilvl / 3; break;
    case A_ARMOR: v = rr(2, 6) * s / 100; break;
    case A_ASPD: v = rr(4, 12); break;
    case A_CRIT: v = rr(2, 6); break;
    case A_LEECH: v = rr(1, 4); break;
    case A_MOVE: v = rr(3, 10); break;
    case A_MF: v = rr(5, 20); break;
    case A_REGEN: v = rr(1, 3) * s / 100; break;
    case A_MREGEN: v = rr(1, 3) + ilvl / 10; break;
    case A_FIRE: v = rr(1, 4) * s / 100; break;
    case A_BOWDMG: v = rr(10, 28) + ilvl / 3; break;
    case A_BLOCK: v = rr(5, 15); break;
    default: v = rr(10, 25) + ilvl / 3; break;   // A_SPELL
    }
    v = v * (100 + 15 * rarity) / 100;
    if (a == A_ASPD) v = MIN(v, 30);
    if (a == A_CRIT) v = MIN(v, 15);
    return MAX(v, 1);
}

static void item_add_affixes(Item *it, int n) {
    while (it->naff < n && it->naff < MAX_AFF) {
        int a = rn(A_COUNT), dup = 0;
        if (!affix_allowed(a, it->base)) continue;
        if (a == A_LEECH && (it->rarity < R_LEGEND || rn(4))) continue;   // life steal: Legendary and Majestic only, and even then uncommon
        for (int i = 0; i < it->naff; i++) if (it->at[i] == a) dup = 1;
        if (dup) continue;
        it->at[it->naff] = (u8)a; it->av[it->naff] = (i16)affix_roll(a, it->ilvl, it->rarity); it->naff++;
    }
}

static void item_base_stats(Item *it) {
    int s = scale_dmg(it->ilvl), mult = 100 + 12 * it->rarity;
    it->dmin = it->dmax = it->armor = 0;
    if (it->base == B_SWORD) { it->dmin = (u16)(2 * s * mult / 10000); it->dmax = (u16)(5 * s * mult / 10000); }
    else if (it->base == B_BOW) { it->dmin = (u16)(2 * s * mult / 10000); it->dmax = (u16)(6 * s * mult / 10000); }
    else if (it->base == B_STAFF) { it->dmin = (u16)(3 * s * mult / 10000); it->dmax = (u16)(7 * s * mult / 10000); }
    if (it->base <= B_STAFF) { if (it->dmin < 1) it->dmin = 1; if (it->dmax <= it->dmin) it->dmax = it->dmin + 1; }
    if (BASE_ARMOR[it->base]) it->armor = (u16)MAX(1, BASE_ARMOR[it->base] * s * mult / 10000 + rn(2));
}

static const int AFFIX_COUNT_BY_RARITY[R_COUNT] = {0, 1, 2, 3, 4, 6};

static void item_make(Item *it, int base, int rarity, int ilvl) {
    memset(it, 0, sizeof *it);
    it->used = 1; it->base = (u8)base; it->rarity = (u8)rarity; it->ilvl = (u16)MAX(1, ilvl);
    item_base_stats(it);
    int n = AFFIX_COUNT_BY_RARITY[rarity];
    if (rarity == R_EPIC) n = rr(3, 4);
    item_add_affixes(it, n);
    if (it->naff > 0) it->pre = it->at[0];
    if (it->naff > 1) it->suf = it->at[1];
}

static void item_make_unique(Item *it, int u, int ilvl) {
    const Unique *q = &UNIQUES[u];
    item_make(it, q->base, q->rarity, ilvl);
    it->uniq = (u8)(u + 1);
    if (q->rarity == R_MAJESTIC) { it->dmin = it->dmin * 3 / 2; it->dmax = it->dmax * 3 / 2; it->armor = it->armor * 3 / 2; }
}

// rarity roll: depth and magic find push odds toward the top tiers
static int roll_rarity(int floor, int mf, int min_r) {
    i32 w[R_COUNT] = {8600, 1150, 240, 50, 8, 1};   // v0.19: good loot is genuinely rare
    w[0] = MAX(4000, w[0] - floor * 25);
    w[1] += floor * 6; w[2] += floor * 3; w[3] += floor * 3 / 2; w[4] += floor / 2; w[5] += floor / 12;
    for (int r = 1; r < R_COUNT; r++) w[r] = w[r] * (100 + mf) / 100;
    for (int r = 0; r < min_r; r++) w[r] = 0;
    i32 tot = 0; for (int r = 0; r < R_COUNT; r++) tot += w[r];
    i32 x = rn(tot);
    for (int r = 0; r < R_COUNT; r++) { if (x < w[r]) return r; x -= w[r]; }
    return 0;
}

static int max_sockets(const Item *it) { return 1 + (it->rarity >= R_ULTRA) + (it->rarity >= R_LEGEND); }
// Class gear: weapons and some off-hands belong to one class (0 warrior, 1 ranger, 2 mage); -1 = anyone.
static int item_class(int base) {
    switch (base) { case B_SWORD: case B_SHIELD: return 0; case B_BOW: case B_QUIVER: return 1; case B_STAFF: case B_TOME: return 2; }
    return -1;
}
static int g_item_cls = -1;   // the current hero's class: drops lean toward gear they can use
static const u8 CLASS_GEAR[3][2] = {{B_SWORD, B_SHIELD}, {B_BOW, B_QUIVER}, {B_STAFF, B_TOME}};
static void item_random(Item *it, int floor, int mf, int min_r) {
    int r = roll_rarity(floor, mf, min_r);
    int ilvl = MAX(1, floor + rr(-1, 1));
    if (r >= R_LEGEND) {
        int cand[N_UNIQUES], nc = 0;
        int own = g_item_cls >= 0 && rn(100) < 75;   // usually a unique this class can wear
        for (int i = 0; i < (int)N_UNIQUES; i++) if (UNIQUES[i].rarity == r) { int ic = item_class(UNIQUES[i].base); if (!own || ic < 0 || ic == g_item_cls) cand[nc++] = i; }
        if (!nc) for (int i = 0; i < (int)N_UNIQUES; i++) if (UNIQUES[i].rarity == r) cand[nc++] = i;
        item_make_unique(it, cand[rn(nc)], ilvl);
    } else {
        // weapons slightly more common than any single armor piece
        int base = rn(100) < 30 ? rn(3) : 3 + rn(B_COUNT - 3);
        int ic = item_class(base);
        if (g_item_cls >= 0 && ic >= 0 && ic != g_item_cls && rn(100) < 75) base = CLASS_GEAR[g_item_cls][base <= B_STAFF ? 0 : 1];   // mostly gear you can use
        item_make(it, base, r, ilvl);
    }
    if (rn(100) < 12 + it->rarity * 6) it->sockets = (u8)rr(1, max_sockets(it));
}

// ---------- gems ----------
enum { G_RUBY, G_SAPPHIRE, G_EMERALD, G_TOPAZ, G_AMETHYST, G_DIAMOND, G_TYPES };
#define G_TIERS 5
#define N_GEMS (G_TYPES * G_TIERS)
static const char *GEM_TYPE_NAME[G_TYPES] = {"RUBY", "SAPPHIRE", "EMERALD", "TOPAZ", "AMETHYST", "DIAMOND"};
static const char *GEM_TIER_NAME[G_TIERS] = {"CHIPPED ", "FLAWED ", "", "FLAWLESS ", "PERFECT "};
static const char *GEM_STAT[G_TYPES] = {"% DAMAGE", "MANA", "LIFE", "% MAGIC FIND", "% CRIT CHANCE", "ARMOR"};
static const u8 GEM_AFFIX[G_TYPES] = {A_DMG, A_MANA, A_VIT, A_MF, A_CRIT, A_ARMOR};
static const i16 GEM_VAL[G_TYPES][G_TIERS] = {{3, 5, 8, 12, 18}, {5, 10, 18, 30, 45}, {6, 12, 22, 36, 55}, {4, 7, 11, 16, 24}, {1, 2, 3, 4, 6}, {3, 6, 11, 18, 28}};
static const u8 GEM_REMAP[G_TYPES][2] = {{14, 13}, {21, 20}, {18, 17}, {16, 15}, {24, 23}, {28, 29}};   // main, shade
static void gem_name(int id, Str *s) { s_reset(s); s_add(s, GEM_TIER_NAME[id % G_TIERS]); s_add(s, GEM_TYPE_NAME[id / G_TIERS]); }

static void item_name(const Item *it, Str *s) {
    s_reset(s);
    if (it->uniq) { s_add(s, UNIQUES[it->uniq - 1].name); return; }
    const char *bn = BASE_NAMES[it->base][base_tier(it->ilvl)];
    if (it->rarity >= R_ULTRA && it->naff > 0) { s_add(s, AFFIX_PRE[it->pre]); s_chr(s, ' '); }
    s_add(s, bn);
    if (it->rarity >= R_EPIC && it->naff > 1) { s_chr(s, ' '); s_add(s, AFFIX_SUF[it->suf]); }
}

static int item_value(const Item *it) {   // salvage / sell gold
    return (5 + it->ilvl * 3) * (1 + it->rarity * it->rarity * 2) + it->sockets * 10;
}
