#pragma once
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Archive slots:
//   0 = StarDat.mpq, 1 = BrooDat.mpq, 2 = Patch_rt.mpq,
//   3 = Active Map (.scx/.scm) or active Campaign MPQ (StarCraft.mpq / BroodWar.mpq),
//   4 = StarCraft.mpq (fonts, classic UI, SC music/story/cinematics),
//   5 = BroodWar.mpq  (BW UI, BW music/story/cinematics).
bool sd_open_archive(int slot, const char* path, char* err, size_t err_cap);
void sd_close_game(void);
void sd_close_all(void);

// Inspect map at path: returns true if valid MPQ with staredit/scenario.chk; fills title, dimensions, human/computer slot counts.
bool sd_inspect_map(const char* path, char* title_out, size_t title_cap, int* tile_w, int* tile_h, int* human_slots, int* comp_slots);
bool sd_map_scenario_chk(const char* path, uint8_t** out_buf, size_t* out_len);
const uint8_t* sd_campaign_chk(size_t* out_len);

// Game initializers
bool sd_init_skirmish(int w, int h, int my_race, int ai_count, int ai_race, int ai_diff,
                      bool expansion, bool ffa, bool heroes, unsigned match_opts, char* err, size_t err_cap);
bool sd_init_configured(int w, int h, int my_slot, const int races[8], const int controllers[8],
                        const int teams[8], bool team_game, int ai_diff, bool expansion,
                        bool ffa, bool heroes, bool ums, unsigned match_opts, char* err, size_t err_cap);
bool sd_init_ums(int w, int h, bool expansion, char* err, size_t err_cap);
bool sd_init_campaign(int w, int h, int mission_code, char* err, size_t err_cap);

// Simulation & rendering
void sd_set_hud_inset(int bottom_px, bool ext_minimap, bool ext_cursor);
void sd_resize_viewport(int w, int h, double view_scale);
void sd_step(void);
void sd_render(double now_ms);
const uint8_t* sd_framebuffer(int* w, int* h);
void sd_set_fast_indexed(bool on);
const uint8_t* sd_indexed_fb(int* w, int* h, int* pitch, uint16_t* pal565_out, int* drag_rect_out);
const uint8_t* sd_minimap_rgba(int* map_px_w, int* map_px_h);
const uint8_t* sd_cursor_rgba(int* box_size);

// Input & commands
void sd_mouse_move(int x, int y);
void sd_mouse_button(int down, int button, int x, int y, int clicks);
void sd_key(int down, int sym, int scancode);
void sd_pan(int dx, int dy);
void sd_minimap_click(int map_px_x, int map_px_y, bool right_button);
void sd_command(int op, int a, int b, int c, int d);

// Queries & HUD data
const char* sd_text(int id);
int sd_value(int id, int n);
const uint8_t* sd_icon_rgba(int frame, int* w, int* h);
const uint8_t* sd_res_icon_rgba(int which, int* w, int* h);
const uint8_t* sd_wire_rgba(int index, int* box);
const uint8_t* sd_cargo_wire_rgba(int index);
int sd_selection_count(void);
uint32_t sd_selected_raw_id(int index);

// Save / Load / Replay
bool sd_save_to_file(const char* path, char* err, size_t err_cap);
bool sd_load_from_file(const char* path, char* err, size_t err_cap);
bool sd_save_replay_file(const char* path, char* err, size_t err_cap);
bool sd_open_replay_file(const char* path, char* err, size_t err_cap);

// Audio (64-channel mixer: stereo int16 interleaved)
void sd_set_audio(bool paused, float effects_vol, float music_vol);
void sd_mix_audio(int16_t* pcm_stereo, int frames, int sample_rate);
bool sd_play_mpq_music(const char* asset_name);
int sd_play_mpq_sound(int slot, const char* asset_name, int channel, bool loop);
int sd_play_campaign_voice(int str_id);
void sd_play_campaign_effect(int str_id);
void sd_play_score_sound(int duration_ms);
bool sd_sound_playing(int channel);
int sd_sound_pos_ms(int channel);
void sd_sound_stop(int channel);

// Smacker (.smk) video streams (up to 24 simultaneous streams: menu planets, portraits, cinematics)
// info_out[5] = {width, height, frames, interval_ms, duration_ms}
int sd_movie_open_asset(int slot, const char* asset_name, bool with_audio, int info_out[5]);
int sd_movie_open_mem(const uint8_t* data, size_t len, bool with_audio, int info_out[5]);
const uint8_t* sd_movie_frame(int id, double time_ms, bool loop, int* w, int* h);
void sd_movie_close(int id);
void sd_movies_close_all(void);

// MPQ asset reader (slot -1 searches {2=Patch_rt, 1=BrooDat, 0=StarDat, 4=StarCraft, 5=BroodWar})
bool sd_read_mpq_asset(int slot, const char* filename, uint8_t** out_buf, size_t* out_len);
void sd_free_buf(void* p);

#ifdef __cplusplus
}
#endif
