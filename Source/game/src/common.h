// Lantern Deep - shared definitions. Pure integer C, no libc required.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8; typedef int8_t i8; typedef uint16_t u16; typedef int16_t i16;
typedef uint32_t u32; typedef int32_t i32; typedef uint64_t u64; typedef int64_t i64;

#define SCR_W 266
#define SCR_H 240

// Pocket controller bitmask (matches openFPGA cont1_key)
enum {
    BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
    BTN_A = 1 << 4, BTN_B = 1 << 5, BTN_X = 1 << 6, BTN_Y = 1 << 7,
    BTN_L = 1 << 8, BTN_R = 1 << 9, BTN_SELECT = 1 << 14, BTN_START = 1 << 15
};

// Platform -> game API
void game_init(u32 seed);
void game_tick(u32 buttons);        // advance logic by one 60Hz tick
void game_render(u16 *fb);          // draw a frame (RGB565, SCR_W x SCR_H)
void game_audio(i16 *out, int n);   // fill n mono samples @ 48kHz

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define CLAMP(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define ABS(a) ((a)<0?-(a):(a))
#define COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))

#ifndef HOST_BUILD
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
#else
#include <string.h>
#endif
