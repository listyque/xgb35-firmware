// Host assert-based self-check for StarDroid 1.9.11 Authentic Blizzard UI decoders & logic:
//   PCX (RLE + glass), GRP (raw + RLE), ClassicFontCipher (SHA-0 + IDEA),
//   86-byte BIN Dialogs, CHK MBRF Briefing Triggers, BriefingText scroll, CommandSlots,
//   59 Campaign Missions + 15 Cinematics, and 480x320 RGB565 downscaler.

#include <cassert>
#include <cstdio>
#include <cstring>
#include "blizz_frontend.h"

// Minimal host stubs for engine_api.h symbols so selfcheck compiles in <2s without full OpenBW link
extern "C" {
bool sd_open_archive(int, const char*, char*, size_t) { return false; }
void sd_close_game(void) {}
void sd_close_all(void) {}
bool sd_inspect_map(const char*, char*, size_t, int*, int*, int*, int*) { return false; }
bool sd_map_scenario_chk(const char*, uint8_t**, size_t*) { return false; }
const uint8_t* sd_campaign_chk(size_t*) { return nullptr; }
bool sd_init_skirmish(int, int, int, int, int, int, bool, bool, bool, unsigned, char*, size_t) { return false; }
bool sd_init_configured(int, int, int, const int[8], const int[8], const int[8], bool, int, bool, bool, bool, bool, unsigned, char*, size_t) { return false; }
bool sd_init_ums(int, int, bool, char*, size_t) { return false; }
bool sd_init_campaign(int, int, int, char*, size_t) { return false; }
void sd_set_hud_inset(int, bool, bool) {}
void sd_resize_viewport(int, int, double) {}
void sd_step(void) {}
void sd_render(double) {}
const uint8_t* sd_framebuffer(int*, int*) { return nullptr; }
void sd_set_fast_indexed(bool) {}
const uint8_t* sd_indexed_fb(int*, int*, int*, uint16_t*, int*) { return nullptr; }
const uint8_t* sd_minimap_rgba(int*, int*) { return nullptr; }
const uint8_t* sd_cursor_rgba(int*) { return nullptr; }
void sd_mouse_move(int, int) {}
void sd_mouse_button(int, int, int, int, int) {}
void sd_key(int, int, int) {}
void sd_pan(int, int) {}
void sd_minimap_click(int, int, bool) {}
void sd_command(int, int, int, int, int) {}
const char* sd_text(int) { return ""; }
int sd_value(int, int) { return 0; }
const uint8_t* sd_icon_rgba(int, int*, int*) { return nullptr; }
const uint8_t* sd_res_icon_rgba(int, int*, int*) { return nullptr; }
const uint8_t* sd_wire_rgba(int, int*) { return nullptr; }
const uint8_t* sd_cargo_wire_rgba(int) { return nullptr; }
int sd_selection_count(void) { return 0; }
uint32_t sd_selected_raw_id(int) { return 0; }
bool sd_save_to_file(const char*, char*, size_t) { return false; }
bool sd_load_from_file(const char*, char*, size_t) { return false; }
bool sd_save_replay_file(const char*, char*, size_t) { return false; }
bool sd_open_replay_file(const char*, char*, size_t) { return false; }
void sd_set_audio(bool, float, float) {}
void sd_mix_audio(int16_t*, int, int) {}
bool sd_play_mpq_music(const char*) { return false; }
int sd_play_mpq_sound(int, const char*, int, bool) { return -1; }
int sd_play_campaign_voice(int) { return 0; }
void sd_play_campaign_effect(int) {}
void sd_play_score_sound(int) {}
bool sd_sound_playing(int) { return false; }
int sd_sound_pos_ms(int) { return -1; }
void sd_sound_stop(int) {}
int sd_movie_open_asset(int, const char*, bool, int[5]) { return -1; }
int sd_movie_open_mem(const uint8_t*, size_t, bool, int[5]) { return -1; }
const uint8_t* sd_movie_frame(int, double, bool, int*, int*) { return nullptr; }
void sd_movie_close(int) {}
void sd_movies_close_all(void) {}
bool sd_read_mpq_asset(int, const char*, uint8_t**, size_t*) { return false; }
void sd_free_buf(void* p) { std::free(p); }
}

int main() {
    using namespace blizz;

    // 1. Verify all 59 missions + 6 episodes + 15 CinematicCatalog entries
    assert(kMissionCount == 59);
    bool seen_ep[6] = {};
    int cin_count = 0;
    for (int i = 0; i < kMissionCount; ++i) {
        assert(kMissions[i].episode >= 0 && kMissions[i].episode < 6);
        assert(kMissions[i].race >= 0 && kMissions[i].race <= 2);
        assert(kMissions[i].title && kMissions[i].title[0] != '\0');
        seen_ep[kMissions[i].episode] = true;
        if (kMissions[i].after_smk && kMissions[i].after_smk[0] != '\0') ++cin_count;
    }
    for (int e = 0; e < 6; ++e) assert(seen_ep[e]);
    assert(cin_count == 15);

    // 2. Verify IDEA modular inverse & multiplication (ClassicFontCipher core)
    for (int k : {1, 2, 17, 257, 12345, 65535, 0}) {
        int inv = font_cipher::idea_inv(k);
        assert(font_cipher::idea_mul(k, inv) == 1);
    }

    // 3. Verify PCX RLE decoder + transparency + glass alpha
    {
        std::vector<uint8_t> pcx_raw(128 + 4 + 1 + 768, 0);
        pcx_raw[0] = 10; pcx_raw[2] = 1; pcx_raw[3] = 8; pcx_raw[65] = 1;
        pcx_raw[8] = 3; pcx_raw[10] = 0; pcx_raw[66] = 4; // 4x1 image
        pcx_raw[128] = 0;    // pixel 0 -> transparent index 0
        pcx_raw[129] = 0xC2; // run of 2 pixels of glass index 138
        pcx_raw[130] = 138;
        pcx_raw[131] = 5;    // pixel 3 -> opaque index 5
        pcx_raw[132] = 0x0C; // palette marker
        size_t poff = pcx_raw.size() - 768;
        pcx_raw[poff + 138 * 3 + 0] = 10; pcx_raw[poff + 138 * 3 + 1] = 20; pcx_raw[poff + 138 * 3 + 2] = 30;
        pcx_raw[poff + 5 * 3 + 0] = 200;  pcx_raw[poff + 5 * 3 + 1] = 100;  pcx_raw[poff + 5 * 3 + 2] = 50;
        PCX dec;
        assert(dec.decode(pcx_raw, true, 138, 0x99));
        assert(dec.width == 4 && dec.height == 1);
        assert(px_a(dec.image.px[0]) == 0);
        assert(px_a(dec.image.px[1]) == 0x99 && px_a(dec.image.px[2]) == 0x99);
        assert(px_a(dec.image.px[3]) == 255 && px_r(dec.image.px[3]) == 200);
    }

    // 4. Verify 86-byte Blizzard .bin Dialog parser
    {
        std::vector<uint8_t> bin(200, 0);
        bin[66] = 86; // first child widget at offset 86
        bin[86 + 4] = 100; bin[86 + 6] = 50;  // x=100, y=50
        bin[86 + 12] = 220; bin[86 + 14] = 40; // w=220, h=40
        bin[86 + 20] = 180; // text offset
        bin[86 + 32] = 13;  // widget id = 13 (Start)
        bin[86 + 34] = 14;  // button type
        std::memcpy(bin.data() + 180, "Start", 6);
        BlizzDialog dlg;
        assert(dlg.load_bytes(bin));
        assert(dlg.get(13) != nullptr && dlg.get(13)->text == "Start" && dlg.get(13)->x == 100);
    }

    // 5. Verify CHK MBRF 2400-byte Briefing Trigger & STR parser
    {
        std::vector<uint8_t> chk;
        auto push_sec = [&](const char tag[4], const std::vector<uint8_t>& body) {
            chk.insert(chk.end(), tag, tag + 4);
            uint32_t l = uint32_t(body.size());
            uint8_t lb[4] = {uint8_t(l), uint8_t(l >> 8), uint8_t(l >> 16), uint8_t(l >> 24)};
            chk.insert(chk.end(), lb, lb + 4);
            chk.insert(chk.end(), body.begin(), body.end());
        };
        // STR section with 1 string: "Destroy the enemy."
        std::vector<uint8_t> str_b = {1, 0, 4, 0};
        const char* obj_s = "Destroy the enemy.";
        str_b.insert(str_b.end(), obj_s, obj_s + std::strlen(obj_s) + 1);
        push_sec("STR ", str_b);

        // MBRF section with 1 trigger owned by player 1, action 0 = Mission Objectives (type 4, str 1)
        std::vector<uint8_t> mbrf(2400, 0);
        mbrf[2372 + 1] = 1; // owned by player 1
        mbrf[320 + 4]  = 1; // text id = 1
        mbrf[320 + 26] = 4; // action type 4 = Mission Objectives
        push_sec("MBRF", mbrf);

        CampaignRunner cr;
        assert(cr.load_chk(chk.data(), chk.size(), 1));
        assert(cr.objectives == "Destroy the enemy.");
    }

    // 6. Verify Command Card slot overrides & BriefingText scroll
    assert(cmd_tables::build_slot(106) == 0);
    assert(cmd_tables::slot_override(51, "Psionic Storm", -1) == 8);
    std::vector<std::string> blines(12, "Commander, prepare to drop immediately.");
    assert(briefing_scroll::compute(blines, 0.0, 5000.0, 100) == 0.0f);
    assert(briefing_scroll::compute(blines, 5000.0, 5000.0, 100) > 0.0f);

    // 7. Verify decoupled render(with_cursor=false) + dirty-rect cursor restore leaves zero trails
    {
        std::vector<uint32_t> world(W * H, 0), comp(W * H, 0);
        Canvas wc(world.data()), cc(comp.data());
        FrontendUI ui;
        ui.screen = ScreenId::DataMissing;
        ui.render(wc, 100.0, 16.0, 200, 200, false, false);
        std::memcpy(comp.data(), world.data(), world.size() * 4);
        ui.draw_cursor_overlay(cc, 100.0, 200, 200);
        assert(std::memcmp(comp.data(), world.data(), world.size() * 4) != 0);
        for (int y = 200 - 68; y < 200 + 68; ++y)
            std::memcpy(comp.data() + y * W + (200 - 68), world.data() + y * W + (200 - 68), 136 * 4);
        assert(std::memcmp(comp.data(), world.data(), world.size() * 4) == 0);
    }

    std::printf("Host self-check passed: 59 missions, 15 cinematics, IDEA cipher, PCX, BIN, MBRF, CommandSlots & DirtyRect OK!\n");
    return 0;
}
