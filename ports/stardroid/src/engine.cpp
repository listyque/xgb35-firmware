// StarDroid 1.9.11 C++ engine wrapper for Linux Desktop (X11) & XGB35 Handheld (480x320 fbdev)
#include "engine_api.h"
#include "../upstream/native/runtime.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

static void set_err(char* err, size_t cap, const char* msg) {
    if (err && cap > 0) {
        std::snprintf(err, cap, "%s", msg ? msg : "Unknown error");
    }
}

static void apply_low_ram_policy() {
    // XGB35 has 128-256 MB DDR: disable replay seek checkpoints and keep Classic memory profile.
    native_replay::checkpoint_disabled = true;
    set_graphics_memory_profile133(stardroid::remastered::live::CLASSIC133);
}

// Disk-streamed Smacker player for large fullscreen cinematics (> 4 MiB, e.g. 42MB starintr.smk / 86MB starxintr.smk)
struct DiskCinematic {
    FILE* fp = nullptr;
    std::string temp_path;
    smk handle = nullptr;
    unsigned long width = 0, height = 0, frames = 0, current = 0;
    double us = 66667.0;
    int audio_track = -1;
    unsigned char audio_ch = 0, audio_bits = 0;
    unsigned long audio_rate = 22050;
    std::shared_ptr<naudio::Clip> stream_clip;
    std::vector<uint8_t> rgba;

    void close() {
        naudio::stop(60);
        if (handle) { smk_close(handle); handle = nullptr; }
        fp = nullptr; // smk_close closes FILE* in SMK_MODE_DISK!
        if (!temp_path.empty()) { ::unlink(temp_path.c_str()); temp_path.clear(); }
        stream_clip.reset();
        rgba.clear();
        width = height = frames = current = 0;
        audio_track = -1;
    }
    ~DiskCinematic() { close(); }

    void pump_frame_audio() {
        if (audio_track < 0 || !stream_clip || !handle) return;
        const unsigned char* pcm = smk_get_audio(handle, audio_track);
        unsigned long raw_bytes = smk_get_audio_size(handle, audio_track);
        if (!pcm || !raw_bytes) return;
        size_t samples = raw_bytes / (audio_bits / 8);
        std::lock_guard<std::mutex> lk(naudio::mutex);
        auto& v = naudio::voices[60];
        // Compact consumed samples periodically so PCM buffer stays < 512 KB
        size_t consumed_frames = (v.clip == stream_clip && v.position > 44100) ? size_t(v.position) - 8192 : 0;
        if (consumed_frames * audio_ch >= 65536 && consumed_frames * audio_ch < stream_clip->pcm.size()) {
            stream_clip->pcm.erase(stream_clip->pcm.begin(), stream_clip->pcm.begin() + consumed_frames * audio_ch);
            v.position -= double(consumed_frames);
        }
        size_t old_sz = stream_clip->pcm.size();
        stream_clip->pcm.resize(old_sz + samples);
        float* dst = stream_clip->pcm.data() + old_sz;
        if (audio_bits == 16) {
            for (size_t i = 0; i < samples; ++i)
                dst[i] = int16_t(naudio::u16(pcm + i * 2)) / 32768.0f;
        } else {
            for (size_t i = 0; i < samples; ++i)
                dst[i] = (int(pcm[i]) - 128) / 128.0f;
        }
        if (v.clip != stream_clip) {
            v.clip = stream_clip;
            v.position = 0;
            v.left = v.right = 1.0f;
            v.loop = false;
        }
    }
};

static DiskCinematic g_disk_cin;
static constexpr int kDiskCinMovieId = 100;

static void downsample_clip_if_large(std::shared_ptr<naudio::Clip>& clip) {
    if (!clip || clip->pcm.size() < 1024 * 1024) return;
    int ch = std::max(1, clip->channels);
    size_t frames = clip->pcm.size() / size_t(ch);
    // Downmix long music tracks (>4 MB float32) to mono & half rate (~27 MB -> ~6.7 MB RAM!)
    int step = (clip->rate > 16000) ? 2 : 1;
    size_t out_frames = frames / size_t(step);
    std::vector<float> compact(out_frames);
    const float* src = clip->pcm.data();
    for (size_t f = 0; f < out_frames; ++f) {
        float sum = 0.0f;
        for (int s = 0; s < step; ++s) {
            const float* p = src + (f * size_t(step) + size_t(s)) * size_t(ch);
            sum += (ch >= 2) ? 0.5f * (p[0] + p[1]) : p[0];
        }
        compact[f] = sum / float(step);
    }
    clip->pcm.swap(compact);
    std::vector<float>().swap(compact);
    clip->channels = 1;
    clip->rate /= step;
}

extern "C" {

bool sd_open_archive(int slot, const char* path, char* err, size_t err_cap) {
    try {
        apply_low_ram_policy();
        open_archive(slot, path ? path : "");
        return true;
    } catch (const std::exception& e) {
        set_err(err, err_cap, e.what());
        return false;
    }
}

void sd_close_game(void) {
    close_game();
}

void sd_close_all(void) {
    g_disk_cin.close();
    close_game();
    set_content_languages149("", {}, {});
    set_hd2_sprites("", false);
    for (auto& m : movies) m.reset();
    for (int i = 0; i < 11; ++i) {
        readers[i].reset();
        archives[i].close();
    }
    naudio::clear();
}

static void parse_chk_slots(const uint8_t* data, size_t sz, int owners[12], int sides[12],
                            int* tw, int* th, std::string* name_out) {
    for (int i = 0; i < 12; ++i) { owners[i] = 0; sides[i] = 0; }
    if (tw) *tw = 0;
    if (th) *th = 0;
    for (size_t p = 0; p + 8 <= sz;) {
        uint32_t len = uint32_t(data[p + 4]) | (uint32_t(data[p + 5]) << 8) |
                       (uint32_t(data[p + 6]) << 16) | (uint32_t(data[p + 7]) << 24);
        if (len > sz - p - 8) break;
        const uint8_t* body = data + p + 8;
        if (!std::memcmp(data + p, "OWNR", 4) && len >= 12) {
            for (int i = 0; i < 12; ++i) owners[i] = body[i];
        } else if (!std::memcmp(data + p, "SIDE", 4) && len >= 12) {
            for (int i = 0; i < 12; ++i) sides[i] = body[i];
        } else if (!std::memcmp(data + p, "DIM ", 4) && len >= 4) {
            if (tw) *tw = body[0] | (body[1] << 8);
            if (th) *th = body[2] | (body[3] << 8);
        }
        p += 8 + size_t(len);
    }
    (void)name_out;
}

bool sd_inspect_map(const char* path, char* title_out, size_t title_cap,
                    int* tile_w, int* tile_h, int* human_slots, int* comp_slots) {
    try {
        bwgame::data_loading::mpq_file<> archive(path);
        const char* chk_name = "staredit\\scenario.chk";
        auto* h = archive.mpq.find_hash_table_entry(chk_name);
        if (!h || h->block_index >= archive.mpq.block_table.size() ||
            archive.mpq.block_table[h->block_index].size > 64 * 1024 * 1024)
            return false;
        bwgame::a_vector<uint8_t> chk;
        archive(chk, chk_name);
        int owners[12], sides[12], tw = 0, th = 0;
        parse_chk_slots(chk.data(), chk.size(), owners, sides, &tw, &th, nullptr);
        int hum = 0, cmp = 0;
        for (int i = 0; i < 8; ++i) {
            if (owners[i] == 6) ++hum;
            else if (owners[i] == 5) ++cmp;
        }
        if (tile_w) *tile_w = tw;
        if (tile_h) *tile_h = th;
        if (human_slots) *human_slots = hum;
        if (comp_slots) *comp_slots = cmp;
        if (title_out && title_cap > 0) {
            const char* base = std::strrchr(path, '/');
            if (!base) base = std::strrchr(path, '\\');
            std::snprintf(title_out, title_cap, "%s", base ? base + 1 : path);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool sd_map_scenario_chk(const char* path, uint8_t** out_buf, size_t* out_len) {
    if (!out_buf || !out_len || !path) return false;
    *out_buf = nullptr;
    *out_len = 0;
    try {
        bwgame::data_loading::mpq_file<> archive(path);
        const char* chk_name = "staredit\\scenario.chk";
        auto* h = archive.mpq.find_hash_table_entry(chk_name);
        if (!h || h->block_index >= archive.mpq.block_table.size() ||
            archive.mpq.block_table[h->block_index].size > 64 * 1024 * 1024)
            return false;
        bwgame::a_vector<uint8_t> chk;
        archive(chk, chk_name);
        if (chk.empty()) return false;
        uint8_t* p = static_cast<uint8_t*>(std::malloc(chk.size()));
        if (!p) return false;
        std::memcpy(p, chk.data(), chk.size());
        *out_buf = p;
        *out_len = chk.size();
        return true;
    } catch (...) {
        return false;
    }
}

const uint8_t* sd_campaign_chk(size_t* out_len) {
    if (out_len) *out_len = openbw_campaign_chk_len();
    return openbw_campaign_chk();
}

bool sd_init_configured(int w, int h, int my_slot, const int races[8], const int controllers[8],
                        const int teams[8], bool team_game, int ai_diff, bool expansion,
                        bool ffa, bool heroes, bool ums, unsigned match_opts, char* err, size_t err_cap) {
    try {
        close_game();
        apply_low_ram_policy();
        g_skirmish_expansion = expansion;
        g_terran_ai_level = bwgame::clamp_ai_difficulty112(ai_diff);
        int r_copy[8], c_copy[8], t_copy[8];
        for (int i = 0; i < 8; ++i) {
            r_copy[i] = races[i];
            c_copy[i] = controllers[i];
            t_copy[i] = teams ? teams[i] : 0;
        }
        configured_init(w, h, my_slot, r_copy, c_copy, g_terran_ai_level,
                        expansion, ffa, false, teams ? t_copy : nullptr, team_game,
                        heroes, ums, nullptr, nullptr, 0, 0, 24, 0, match_opts & 15u, false);
        if (g_campaign) g_campaign->base_defence_enabled = nearby_defenders;
        if (g_ui) {
            g_ui->host_controls_edge_scroll = false;
            g_ui->external_minimap = true;
            openbw_set_external_cursor(1);
        }
        return true;
    } catch (const std::exception& e) {
        close_game();
        set_err(err, err_cap, e.what());
        return false;
    }
}

bool sd_init_skirmish(int w, int h, int my_race, int ai_count, int ai_race, int ai_diff,
                      bool expansion, bool ffa, bool heroes, unsigned match_opts, char* err, size_t err_cap) {
    try {
        close_game();
        apply_low_ram_policy();
        g_skirmish_expansion = expansion;
        g_terran_ai_level = bwgame::clamp_ai_difficulty112(ai_diff);

        bwgame::data_loading::mpq_file<bwgame::data_loading::js_file_reader<>> map("openbw.map");
        bwgame::a_vector<uint8_t> chk;
        map(chk, "staredit/scenario.chk");
        int owners[12], sides[12];
        parse_chk_slots(chk.data(), chk.size(), owners, sides, nullptr, nullptr, nullptr);

        int avail[8], navail = 0;
        for (int i = 0; i < 8; ++i) {
            if (owners[i] == 6 || owners[i] == 5) avail[navail++] = i;
        }
        if (navail == 0) {
            for (int i = 0; i < 8; ++i) avail[navail++] = i;
        }

        if (ai_count <= 0 || navail < 2) {
            openbw_init(w, h, my_race, avail[0]);
        } else {
            int races[8] = {}, controllers[8] = {};
            int my_slot = avail[0];
            controllers[my_slot] = 1;
            races[my_slot] = (my_race >= 0 && my_race <= 2) ? my_race : (std::rand() % 3);
            int placed_ai = 0;
            for (int k = 1; k < navail && placed_ai < ai_count; ++k) {
                int s = avail[k];
                controllers[s] = 2;
                races[s] = (ai_race >= 0 && ai_race <= 2) ? ai_race : ((placed_ai + races[my_slot] + 1) % 3);
                ++placed_ai;
            }
            configured_init(w, h, my_slot, races, controllers, g_terran_ai_level,
                            expansion, ffa, false, nullptr, false, heroes, false,
                            nullptr, nullptr, 0, 0, 24, 0, match_opts & 15u, false);
        }
        if (g_campaign) g_campaign->base_defence_enabled = nearby_defenders;
        if (g_ui) {
            g_ui->host_controls_edge_scroll = false;
            g_ui->external_minimap = true;
            openbw_set_external_cursor(1);
        }
        return true;
    } catch (const std::exception& e) {
        close_game();
        set_err(err, err_cap, e.what());
        return false;
    }
}

bool sd_init_ums(int w, int h, bool expansion, char* err, size_t err_cap) {
    try {
        close_game();
        apply_low_ram_policy();
        bwgame::data_loading::mpq_file<bwgame::data_loading::js_file_reader<>> map("openbw.map");
        bwgame::a_vector<uint8_t> chk;
        map(chk, "staredit/scenario.chk");
        int owners[12], sides[12];
        parse_chk_slots(chk.data(), chk.size(), owners, sides, nullptr, nullptr, nullptr);

        int controllers[8] = {}, races[8] = {};
        int my_slot = -1;
        for (int i = 0; i < 8; ++i) {
            races[i] = (sides[i] >= 0 && sides[i] <= 2) ? sides[i] : (i % 3);
            if (owners[i] == 6) {
                if (my_slot < 0) { my_slot = i; controllers[i] = 1; }
                else controllers[i] = 0;
            } else if (owners[i] == 5) {
                controllers[i] = 2;
            } else {
                controllers[i] = 0;
            }
        }
        if (my_slot < 0) return sd_init_skirmish(w, h, 1, 1, 0, 2, expansion, true, false, 0, err, err_cap);
        configured_init(w, h, my_slot, races, controllers, 2, expansion,
                        false, false, nullptr, false, false, true);
        if (g_ui) {
            g_ui->host_controls_edge_scroll = false;
            g_ui->external_minimap = true;
            openbw_set_external_cursor(1);
        }
        return true;
    } catch (const std::exception& e) {
        close_game();
        set_err(err, err_cap, e.what());
        return false;
    }
}

bool sd_init_campaign(int w, int h, int mission_code, char* err, size_t err_cap) {
    try {
        close_game();
        apply_low_ram_policy();
        openbw_init_campaign(w, h, mission_code);
        if (g_campaign) g_campaign->base_defence_enabled = nearby_defenders;
        if (g_ui) {
            g_ui->host_controls_edge_scroll = false;
            g_ui->external_minimap = true;
            openbw_set_external_cursor(1);
        }
        return true;
    } catch (const std::exception& e) {
        close_game();
        set_err(err, err_cap, e.what());
        return false;
    }
}

void sd_set_hud_inset(int bottom_px, bool ext_minimap, bool ext_cursor) {
    if (!g_ui) return;
    g_ui->hud_bottom_inset = std::max(0, bottom_px);
    g_ui->external_minimap = ext_minimap;
    openbw_set_external_cursor(ext_cursor ? 1 : 0);
    g_ui->clamp_viewport();
}

void sd_resize_viewport(int w, int h, double view_scale) {
    try {
        openbw_resize(w, h);
        openbw_set_view_scale(view_scale);
    } catch (...) {}
}

void sd_step(void) {
    native_step();
}

void sd_render(double now_ms) {
    if (!g_ui) return;
    openbw_render(now_ms);
}

const uint8_t* sd_framebuffer(int* w, int* h) {
    if (w) *w = openbw_framebuffer_width();
    if (h) *h = openbw_framebuffer_height();
    return openbw_framebuffer();
}

const uint8_t* sd_minimap_rgba(int* map_px_w, int* map_px_h) {
    if (!g_ui) return nullptr;
    if (map_px_w) *map_px_w = int(g_ui->game_st.map_width);
    if (map_px_h) *map_px_h = int(g_ui->game_st.map_height);
    minimap_cache165.refresh(*g_ui, render_generation165, !g_ui->alerts.empty());
    return minimap_cache165.pixels();
}

const uint8_t* sd_cursor_rgba(int* box_size) {
    if (box_size) *box_size = openbw_cursor_box();
    return openbw_cursor_rgba();
}

void sd_mouse_move(int x, int y) {
    openbw_mouse_move(x, y);
}

void sd_mouse_button(int down, int button, int x, int y, int clicks) {
    openbw_mouse_button(down, button, x, y, clicks);
}

void sd_key(int down, int sym, int scancode) {
    openbw_key(down, sym, scancode);
}

void sd_pan(int dx, int dy) {
    openbw_pan(dx, dy);
}

void sd_minimap_click(int map_px_x, int map_px_y, bool right_button) {
    minimap_cache165.invalidate();
    minimap_action(map_px_x, map_px_y, right_button);
}

void sd_command(int op, int a, int b, int c, int d) {
    try { command(op, a, b, c, d); } catch (...) {}
}

const char* sd_text(int id) {
    try { return text_query(id); } catch (...) { return ""; }
}

int sd_value(int id, int n) {
    try {
        if (id == 15) return g_ui && g_ui->rep_on && native_replay::recording.present && !native_replay::is_active();
        return value_query(id, n);
    } catch (...) { return 0; }
}

const uint8_t* sd_icon_rgba(int frame, int* w, int* h) {
    const uint8_t* p = openbw_icon(frame);
    if (w) *w = openbw_icon_w();
    if (h) *h = openbw_icon_h();
    return p;
}

const uint8_t* sd_res_icon_rgba(int which, int* w, int* h) {
    const uint8_t* p = openbw_res_icon(which);
    if (w) *w = openbw_icon_w();
    if (h) *h = openbw_icon_h();
    return p;
}

const uint8_t* sd_wire_rgba(int index, int* box) {
    if (box) *box = openbw_wire_box();
    return openbw_wire_rgba(index);
}

const uint8_t* sd_cargo_wire_rgba(int index) {
    try { return g_ui ? g_ui->render_cargo_wireframe(size_t(index)) : nullptr; }
    catch (...) { return nullptr; }
}

int sd_selection_count(void) {
    return g_ui ? int(g_ui->current_selection.size()) : 0;
}

uint32_t sd_selected_raw_id(int index) {
    if (!g_ui || index < 0 || size_t(index) >= g_ui->current_selection.size()) return 0;
    return g_ui->current_selection[size_t(index)].raw_value;
}

bool sd_save_to_file(const char* path, char* err, size_t err_cap) {
    try {
        auto data = save_game();
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot open save file for writing");
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) throw std::runtime_error("Failed writing save file");
        return true;
    } catch (const std::exception& e) {
        set_err(err, err_cap, e.what());
        return false;
    }
}

bool sd_load_from_file(const char* path, char* err, size_t err_cap) {
    try {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error("Save file not found");
        auto sz = f.tellg();
        if (sz <= 0 || uint64_t(sz) > native_save::MAX_BYTES) throw std::runtime_error("Invalid save size");
        std::vector<uint8_t> data(static_cast<size_t>(sz));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(data.data()), sz);
        restore_game(data);
        return true;
    } catch (const std::exception& e) {
        set_err(err, err_cap, e.what());
        return false;
    }
}

bool sd_save_replay_file(const char* path, char* err, size_t err_cap) {
    try {
        auto data = native_replay::save();
        if (data.empty()) throw std::runtime_error("No replay recorded for this match");
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot write replay file");
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        return bool(f);
    } catch (const std::exception& e) {
        set_err(err, err_cap, e.what());
        return false;
    }
}

bool sd_open_replay_file(const char* path, char* err, size_t err_cap) {
    try {
        close_game();
        apply_low_ram_policy();
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error("Replay file not found");
        auto sz = f.tellg();
        if (sz <= 0 || uint64_t(sz) > native_replay::MAX_FILE) throw std::runtime_error("Invalid replay size");
        std::vector<uint8_t> data(static_cast<size_t>(sz));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(data.data()), sz);
        native_replay::open(data.data(), data.size());
        if (g_ui) {
            g_ui->host_controls_edge_scroll = false;
            g_ui->external_minimap = true;
            openbw_set_external_cursor(1);
        }
        return true;
    } catch (const std::exception& e) {
        close_game();
        set_err(err, err_cap, e.what());
        return false;
    }
}

void sd_set_audio(bool paused, float effects_vol, float music_vol) {
    std::lock_guard<std::mutex> l(naudio::mutex);
    naudio::paused = paused;
    naudio::effects = effects_vol;
    naudio::music = music_vol;
}

void sd_mix_audio(int16_t* pcm_stereo, int frames, int sample_rate) {
    naudio::mix(pcm_stereo, frames, sample_rate);
}

bool sd_play_mpq_music(const char* asset_name) {
    try {
        naudio::stop(63);
        if (!asset_name || !asset_name[0]) return true;
        std::string norm = asset_name;
        for (char& c : norm) if (c == '/') c = '\\';
        auto data = read_asset(-1, norm);
        if (data.empty()) return false;
        auto clip = naudio::wav(data.data(), data.size());
        bwgame::a_vector<uint8_t>().swap(data);
        downsample_clip_if_large(clip);
        naudio::play_clip(std::move(clip), 63);
        std::lock_guard<std::mutex> l(naudio::mutex);
        naudio::voices[63].loop = true;
        return true;
    } catch (...) {
        return false;
    }
}

int sd_play_mpq_sound(int slot, const char* asset_name, int channel, bool loop) {
    try {
        if (channel < 0 || channel >= 64) return 0;
        naudio::stop(channel);
        if (!asset_name || !asset_name[0]) return 0;
        std::string norm = asset_name;
        for (char& c : norm) if (c == '/') c = '\\';
        auto data = read_asset(slot, norm);
        if (data.empty()) return 0;
        auto clip = naudio::wav(data.data(), data.size());
        bwgame::a_vector<uint8_t>().swap(data);
        if (channel == 63) downsample_clip_if_large(clip);
        int dur = optional_audio102::duration(clip);
        naudio::play_clip(std::move(clip), channel);
        if (loop) {
            std::lock_guard<std::mutex> lk(naudio::mutex);
            naudio::voices[channel].loop = true;
        }
        return dur;
    } catch (...) {
        return 0;
    }
}

int sd_play_campaign_voice(int str_id) {
    try { return optional_audio102::voice(str_id); } catch (...) { return 0; }
}

void sd_play_campaign_effect(int str_id) {
    try { optional_audio102::effect(str_id); } catch (...) {}
}

void sd_play_score_sound(int duration_ms) {
    try {
        auto f = read_asset(-1, "sound\\glue\\scorefill.wav");
        auto e = read_asset(-1, "sound\\glue\\scorefillend.wav");
        if (f.empty() || e.empty()) return;
        auto a = naudio::wav(f.data(), f.size());
        auto b = naudio::wav(e.data(), e.size());
        naudio::play_clip(naudio::score_clip(*a, *b, duration_ms), 57);
    } catch (...) {}
}

bool sd_sound_playing(int channel) {
    return channel >= 0 && channel < 64 ? naudio::playing(channel) : false;
}

int sd_sound_pos_ms(int channel) {
    return channel >= 0 && channel < 64 ? int(naudio::position_ms(channel)) : -1;
}

void sd_sound_stop(int channel) {
    if (channel >= 0 && channel < 64) naudio::stop(channel);
}

int sd_movie_open_mem(const uint8_t* data, size_t len, bool with_audio, int info_out[5]) {
    try {
        int id = open_movie(data, len, with_audio);
        auto& m = *movies[id];
        if (info_out) {
            info_out[0] = int(m.width);
            info_out[1] = int(m.height);
            info_out[2] = int(m.frames);
            info_out[3] = int(std::max(1.0, m.us / 1000.0));
            info_out[4] = int(std::ceil(double(m.frames) * std::max(1000.0, m.us) / 1000.0));
        }
        return id;
    } catch (...) {
        return -1;
    }
}

int sd_movie_open_asset(int slot, const char* asset_name, bool with_audio, int info_out[5]) {
    if (!asset_name || !asset_name[0]) return -1;
    try {
        std::string norm = asset_name;
        for (char& c : norm) if (c == '/') c = '\\';

        // Stream large fullscreen cinematics (> 4 MiB) from SD/disk via SMK_MODE_DISK (< 2 MiB RAM!)
        if (with_audio) {
            int found_slot = -1;
            const int order[] = {slot, 2, 1, 0, 4, 5};
            for (int s : order) {
                if (s < 0 || s >= 11 || !readers[s]) continue;
                if (readers[s]->mpq.file_exists(norm.c_str())) { found_slot = s; break; }
                if (slot >= 0) break;
            }
            if (found_slot >= 0) {
                auto* h = readers[found_slot]->mpq.find_hash_table_entry(norm.c_str());
                if (h && h->block_index < readers[found_slot]->mpq.block_table.size() &&
                    readers[found_slot]->mpq.block_table[h->block_index].size > 4 * 1024 * 1024) {
                    g_disk_cin.close();
                    const char* tmp_dir = (::access("/mnt/sd/ports/stardroid/saves", W_OK) == 0)
                                          ? "/mnt/sd/ports/stardroid/saves/.cin_stream.smk"
                                          : ".cin_stream.smk";
                    {
                        auto b = read_asset(found_slot, norm);
                        FILE* out = std::fopen(tmp_dir, "wb");
                        if (!out) return -1;
                        size_t wr = std::fwrite(b.data(), 1, b.size(), out);
                        std::fclose(out);
                        if (wr != b.size()) { ::unlink(tmp_dir); return -1; }
                    }
                    g_disk_cin.temp_path = tmp_dir;
                    g_disk_cin.fp = std::fopen(tmp_dir, "rb");
                    if (!g_disk_cin.fp) { g_disk_cin.close(); return -1; }
                    g_disk_cin.handle = smk_open_filepointer(g_disk_cin.fp, SMK_MODE_DISK);
                    if (!g_disk_cin.handle) { std::fclose(g_disk_cin.fp); g_disk_cin.fp = nullptr; g_disk_cin.close(); return -1; }
                    unsigned char scale = 0;
                    smk_info_video(g_disk_cin.handle, &g_disk_cin.width, &g_disk_cin.height, &scale);
                    smk_info_all(g_disk_cin.handle, nullptr, &g_disk_cin.frames, &g_disk_cin.us);
                    unsigned char mask = 0, ch[7] = {}, bits[7] = {};
                    unsigned long rates[7] = {};
                    smk_info_audio(g_disk_cin.handle, &mask, ch, bits, rates);
                    unsigned char track_mask = SMK_VIDEO_TRACK;
                    for (int t = 0; t < 7; ++t) {
                        if ((mask & (1 << t)) && ch[t] >= 1 && ch[t] <= 2 && (bits[t] == 8 || bits[t] == 16)) {
                            g_disk_cin.audio_track = t;
                            g_disk_cin.audio_ch = ch[t];
                            g_disk_cin.audio_bits = bits[t];
                            g_disk_cin.audio_rate = rates[t];
                            g_disk_cin.stream_clip = std::make_shared<naudio::Clip>();
                            g_disk_cin.stream_clip->rate = int(rates[t]);
                            g_disk_cin.stream_clip->channels = int(ch[t]);
                            track_mask |= (1 << t);
                            break;
                        }
                    }
                    smk_enable_all(g_disk_cin.handle, track_mask);
                    smk_first(g_disk_cin.handle);
                    g_disk_cin.current = 0;
                    g_disk_cin.rgba.resize(g_disk_cin.width * g_disk_cin.height * 4);
                    g_disk_cin.pump_frame_audio();
                    if (info_out) {
                        info_out[0] = int(g_disk_cin.width);
                        info_out[1] = int(g_disk_cin.height);
                        info_out[2] = int(g_disk_cin.frames);
                        info_out[3] = int(std::max(1.0, g_disk_cin.us / 1000.0));
                        info_out[4] = int(std::ceil(double(g_disk_cin.frames) * std::max(1000.0, g_disk_cin.us) / 1000.0));
                    }
                    return kDiskCinMovieId;
                }
            }
        }

        auto b = read_asset(slot, norm);
        if (b.empty()) return -1;
        return sd_movie_open_mem(b.data(), b.size(), with_audio, info_out);
    } catch (...) {
        return -1;
    }
}

const uint8_t* sd_movie_frame(int id, double time_ms, bool loop, int* w, int* h) {
    if (id == kDiskCinMovieId) {
        if (!g_disk_cin.handle) return nullptr;
        unsigned long target = (unsigned long)(std::max(0.0, time_ms) * 1000.0 / std::max(1000.0, g_disk_cin.us));
        if (!loop && target >= g_disk_cin.frames) target = g_disk_cin.frames - 1;
        while (g_disk_cin.current < target) {
            if (smk_next(g_disk_cin.handle) < 0) break;
            ++g_disk_cin.current;
            g_disk_cin.pump_frame_audio();
        }
        const auto* pal = smk_get_palette(g_disk_cin.handle);
        const auto* vid = smk_get_video(g_disk_cin.handle);
        size_t npx = g_disk_cin.width * g_disk_cin.height;
        for (size_t i = 0; i < npx; ++i) {
            int k = vid[i] * 3;
            g_disk_cin.rgba[4 * i + 0] = pal[k + 0];
            g_disk_cin.rgba[4 * i + 1] = pal[k + 1];
            g_disk_cin.rgba[4 * i + 2] = pal[k + 2];
            g_disk_cin.rgba[4 * i + 3] = 255;
        }
        if (w) *w = int(g_disk_cin.width);
        if (h) *h = int(g_disk_cin.height);
        return g_disk_cin.rgba.data();
    }

    try {
        auto* m = movie_frame(id, time_ms, loop);
        if (!m) return nullptr;
        if (w) *w = int(m->width);
        if (h) *h = int(m->height);
        return m->rgba.data();
    } catch (...) {
        return nullptr;
    }
}

void sd_movie_close(int id) {
    if (id == kDiskCinMovieId) { g_disk_cin.close(); return; }
    if (id >= 0 && id < 24) movies[id].reset();
}

void sd_movies_close_all(void) {
    g_disk_cin.close();
    for (auto& m : movies) m.reset();
}

bool sd_read_mpq_asset(int slot, const char* filename, uint8_t** out_buf, size_t* out_len) {
    if (!out_buf || !out_len) return false;
    *out_buf = nullptr;
    *out_len = 0;
    try {
        std::string norm = filename ? filename : "";
        for (char& c : norm) if (c == '/') c = '\\';
        auto v = read_asset(slot, norm);
        if (v.empty()) return false;
        uint8_t* p = static_cast<uint8_t*>(std::malloc(v.size() + 1));
        if (!p) return false;
        std::memcpy(p, v.data(), v.size());
        p[v.size()] = 0;
        *out_buf = p;
        *out_len = v.size();
        return true;
    } catch (...) {
        return false;
    }
}

void sd_free_buf(void* p) {
    std::free(p);
}

} // extern "C"
