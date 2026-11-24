// Browser backend: the same game code compiled to WebAssembly (+ save slot via localStorage).
#include "../src/game.c"
static u16 fb[SCR_W * SCR_H];
static i16 audio[4096];
__attribute__((export_name("init"))) void w_init(u32 seed) { game_init(seed); }
__attribute__((export_name("tick"))) void w_tick(u32 buttons) { game_tick(buttons); }
__attribute__((export_name("frame"))) void w_frame(u32 buttons, u32 frames) { game_frame(buttons, frames); }
__attribute__((export_name("render"))) u16 *w_render(void) { game_render(fb); return fb; }
__attribute__((export_name("audio"))) i16 *w_audio(int n) { if (n > 4096) n = 4096; game_audio(audio, n); return audio; }
__attribute__((export_name("save_ptr"))) void *w_save_ptr(void) { return &g_persist; }
__attribute__((export_name("save_size"))) int w_save_size(void) { return (int)sizeof g_persist; }
__attribute__((export_name("save_dirty"))) int w_save_dirty(void) { int d = G.save_dirty; G.save_dirty = 0; return d; }
__attribute__((export_name("save_loaded"))) int w_save_loaded(void) { import_v2_save(); settings_apply(); G.has_save = any_save(); return G.has_save; }
