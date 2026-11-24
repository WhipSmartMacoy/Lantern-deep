// Tiny synth: square / noise voices with pitch slide and decay. 48kHz mono.
#include "common.h"

typedef struct { u32 phase, inc; i32 slide; i32 vol, decay; int noise; int len; int delay; } Voice;
static Voice g_voice[8];
static u32 g_noise = 0x1234567;

// freq in Hz, len in ms, slide in Hz per ms (signed), vol 0..8000
static void snd_play_d(int freq, int len_ms, int slide, int vol, int noise, int delay_ms) {
    int best = 0; i32 bv = 0x7fffffff;
    for (int i = 0; i < 8; i++) {
        if (g_voice[i].len <= 0 && g_voice[i].delay <= 0) { best = i; break; }
        if (g_voice[i].vol < bv) { bv = g_voice[i].vol; best = i; }
    }
    Voice *v = &g_voice[best];
    v->phase = 0;
    v->inc = (u32)freq * 89478u;              // 2^32 / 48000
    v->slide = slide * 1864;                   // per-sample increment change
    v->len = len_ms * 48; v->vol = vol << 8; v->decay = v->len ? (v->vol / v->len) : 0;
    v->noise = noise; v->delay = delay_ms * 48;
}
static void snd_play(int freq, int len_ms, int slide, int vol, int noise) { snd_play_d(freq, len_ms, slide, vol, noise, 0); }

void game_audio(i16 *out, int n) {
    // voice by voice into a mix buffer: silent voices cost nothing (most of the time only one or two play)
    static i32 mix[4096];
    if (n > 4096) n = 4096;
    for (int s = 0; s < n; s++) mix[s] = 0;
    for (int i = 0; i < 8; i++) {
        Voice *v = &g_voice[i];
        int s = 0;
        if (v->delay > 0) { int k = MIN(v->delay, n); v->delay -= k; s = k; }
        if (v->len <= 0) continue;
        for (; s < n && v->len > 0; s++) {
            int sample;
            if (v->noise) {
                u32 old = v->phase; v->phase += v->inc;
                if ((old ^ v->phase) & 0x10000000) { g_noise ^= g_noise << 13; g_noise ^= g_noise >> 17; g_noise ^= g_noise << 5; }
                sample = (g_noise & 1) ? 1 : -1;
            } else {
                v->phase += v->inc; sample = (v->phase & 0x80000000u) ? 1 : -1;
            }
            v->inc = (u32)((i32)v->inc + v->slide);
            mix[s] += sample * (v->vol >> 8);
            v->vol -= v->decay; if (v->vol < 0) v->vol = 0;
            v->len--;
        }
    }
    for (int s = 0; s < n; s++) out[s] = (i16)CLAMP(mix[s], -32000, 32000);
}

// sound events
static void sfx_swing(void) { snd_play(900, 60, -10, 1500, 1); }
static void sfx_shoot(void) { snd_play(1200, 70, -12, 1600, 0); }
static void sfx_bolt(void) { snd_play(500, 140, 4, 1800, 0); snd_play(700, 120, 5, 900, 0); }
static void sfx_hit(void) { snd_play(220, 70, -2, 2600, 1); }
static void sfx_hurt(void) { snd_play(160, 160, -1, 3200, 0); }
static void sfx_kill(void) { snd_play(300, 120, -3, 2400, 1); snd_play(140, 160, -1, 2000, 0); }
static void sfx_pickup(void) { snd_play(1000, 50, 6, 1500, 0); snd_play_d(1500, 60, 0, 1500, 0, 50); }
static void sfx_gold(void) { snd_play(2000, 40, 0, 1000, 0); snd_play_d(2600, 50, 0, 1000, 0, 40); }
static void sfx_potion(void) { snd_play(400, 220, 6, 2000, 0); }
static void sfx_stairs(void) { for (int i = 0; i < 5; i++) snd_play_d(600 - i * 80, 120, 0, 1600, 0, i * 90); }
static void sfx_levelup(void) { int f[5] = {523, 659, 784, 1046, 1318}; for (int i = 0; i < 5; i++) snd_play_d(f[i], 160, 0, 2000, 0, i * 80); }
static void sfx_rare_drop(int rarity) {
    if (rarity >= 5) { int f[7] = {784, 988, 1175, 1568, 1976, 2349, 3136}; for (int i = 0; i < 7; i++) snd_play_d(f[i], 260, 0, 2200, 0, i * 90); }
    else if (rarity == 4) { int f[4] = {659, 880, 1046, 1318}; for (int i = 0; i < 4; i++) snd_play_d(f[i], 220, 0, 2000, 0, i * 90); }
    else if (rarity == 3) { snd_play(880, 160, 0, 1600, 0); snd_play_d(1175, 200, 0, 1600, 0, 90); }
}
static void sfx_death(void) { for (int i = 0; i < 4; i++) snd_play_d(300 - i * 50, 300, -2, 2600, 0, i * 220); }
static void sfx_menu(void) { snd_play(1400, 25, 0, 900, 0); }
static void sfx_equip(void) { snd_play(500, 60, 0, 1500, 1); snd_play_d(900, 60, 0, 1200, 0, 40); }
