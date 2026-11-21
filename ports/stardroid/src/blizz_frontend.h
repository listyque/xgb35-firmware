#pragma once
// Authentic StarCraft & Brood War Glue Frontend State Machine & In-Game F10 Modal System
// (ported from StarDroid 1.9.11 NativeSurface.java, MatchSetup.java, MatchOutcome.java).

#include "blizz_screens.h"

namespace blizz {

enum class ScreenId {
    DataMissing,
    TitleSplash,
    MainMenu,
    CampaignSelect,
    Story,
    Cinematic,
    Briefing,
    CustomGame,
    LoadBrowser,
    Playing,
    Score,
    Credits
};

enum class ModalId {
    None,
    ExpansionMain,      // Single Player -> Original vs Expansion
    ExpansionCustom,    // Multiplayer/Custom -> Original vs Expansion
    ExpansionIntro,     // View Intro -> SC Intro vs BW Intro
    MissionHistory,     // gluhist.bin + phist.pcx
    AppOptions,         // Main menu Editor/Options planet modal
    MapDetails,         // Custom game map description modal
    MatchOptions,       // StarDroid QoL match options (Auto-Harvest, Multi-Larva, Smart-Cast, Hero Mode)
    // In-game F10 modals:
    GameMenu,           // gamemenu.bin
    GameSave,           // Save game slot picker
    GameLoad,           // Load game slot picker
    GameOptions,        // options.bin
    OptSpeed,
    OptSound,
    OptVideo,
    OptControls,
    OptCheats,
    GameTips,           // rez/tips.tbl
    GameObjectives,     // Mission objectives viewer
    AbortMenu,          // abrtmenu.bin
    OutcomePrompt       // Victory / Defeat modal before ScoreScreen
};

struct HitBtn {
    int x = 0, y = 0, w = 0, h = 0;
    int id = 0;
    bool enabled = true;
    bool contains(int px, int py) const { return enabled && px >= x && px < x + w && py >= y && py < y + h; }
};

struct MatchSetup {
    int mode = 0;        // 0=Melee, 1=Free For All, 2=Team Melee, 3=Use Map Settings
    int difficulty = 1;  // 0=Easy, 1=Normal, 2=Hard, 3=Original
    int speed = 6;       // 0..6 (Slowest..Fastest)
    bool hero_mode = false;
    unsigned match_opts = 0; // bit0=AutoHarvest(1), bit1=MultiLarva(2), bit2=SmartCast(4)
    int my_slot = 0;
    int races[8]       = {1, 0, 2, 1, 0, 2, 1, 0}; // 0=Zerg, 1=Terran, 2=Protoss, 3=Random
    int controllers[8] = {1, 2, 0, 0, 0, 0, 0, 0}; // 0=Closed, 1=Human, 2=Computer
    int teams[8]       = {1, 2, 1, 2, 1, 2, 1, 2}; // 1..4

    const char* mode_label() const {
        static const char* kM[4] = {"Melee", "Free For All", "Top vs Bottom / Teams", "Use Map Settings"};
        return kM[mode & 3];
    }
    const char* diff_label() const {
        static const char* kD[4] = {"Easy", "Normal", "Hard", "Original"};
        return kD[difficulty & 3];
    }
    const char* speed_label() const {
        static const char* kS[7] = {"Slowest", "Slower", "Slow", "Normal", "Fast", "Faster", "Fastest"};
        return kS[std::max(0, std::min(6, speed))];
    }
    int sim_fps() const {
        static const int kFps[7] = {8, 12, 15, 18, 21, 22, 24};
        return kFps[std::max(0, std::min(6, speed))];
    }

    void apply_ums(const MapMeta& m) {
        mode = 3;
        bool picked_human = false;
        for (int i = 0; i < 8; ++i) {
            int r = (m.sides[i] <= 2) ? m.sides[i] : 1;
            races[i] = r;
            teams[i] = std::max(1, std::min(4, m.forces[i] + 1));
            if (m.owners[i] == 6) {
                if (!picked_human) { controllers[i] = 1; my_slot = i; picked_human = true; }
                else controllers[i] = 2;
            } else if (m.owners[i] == 5) {
                controllers[i] = 2;
            } else {
                controllers[i] = 0;
            }
        }
        if (!picked_human) { controllers[0] = 1; my_slot = 0; }
    }

    void clamp_to_starts(int starts) {
        starts = std::max(2, std::min(8, starts));
        my_slot = 0;
        controllers[0] = 1;
        int comp_cnt = 0;
        for (int i = 1; i < 8; ++i) {
            if (i >= starts) controllers[i] = 0;
            else if (controllers[i] == 2) ++comp_cnt;
        }
        if (comp_cnt == 0) controllers[1] = 2;
    }
};

struct HistItem {
    int mission_idx = -1; // index into kMissions
    bool is_cinematic = false;
    std::string smk_path;
    std::string label;
    int unlock_code = 0;
};

struct FrontendUI {
    ScreenId screen = ScreenId::TitleSplash;
    ScreenId after_cinematic = ScreenId::MainMenu;
    ModalId modal = ModalId::None;

    PCX title_pcx, retail_ex_pcx, phist_pcx, load_pinfo_pcx;
    BlizzDialog gamemode_dlg, hist_dlg;
    BlizzGlue glue;
    BlizzHud hud;
    CampaignRunner campaign;
    StoryCrawl story;
    CinematicPlayer cinematic;
    ScoreScreen score;
    ArrowCursor cursor;
    ProgressStore progress;
    MatchSetup setup;

    // Campaign & Mission state
    bool ru_locale = false;
    bool has_broodwar = false;
    bool direct_fb_ok = false;

    std::string tr(const char* en, const char* ru) const {
        return ru_locale ? utf8_to_cp1251(ru) : std::string(en);
    }
    void detect_locale() {
        auto st = parse_tbl(mpq_read("rez\\stat_txt.tbl"));
        if (!st.empty() && has_cp1251_cyrillic(st[0])) { ru_locale = true; return; }
        for (const auto& w : glue.layout.widgets)
            if (has_cp1251_cyrillic(w.text)) { ru_locale = true; return; }
    }
    bool has_sc_camp = false;
    bool has_bw_camp = false;
    int active_mission_idx = -1; // -1 for custom/skirmish/replay
    int active_race = 1;         // 0=Zerg, 1=Terran, 2=Protoss
    bool active_is_replay = false;
    bool outcome_dismissed = false;
    int last_outcome = 0;

    // Mission History modal (gluhist)
    int hist_race = 1;
    std::vector<HistItem> hist_items;
    int hist_sel = 0;
    float hist_scroll = 0;

    // Briefing scrolling
    int last_brief_rev = -1;
    double brief_start_ms = 0, brief_len_ms = 0;
    float brief_scroll = 0, brief_manual = -1;
    double brief_manual_until = 0;
    BlizzPortrait brief_portraits[4];

    // Custom game & Load browser state
    std::vector<std::string> map_files;
    int map_sel = 0;
    float map_scroll = 0;
    MapMeta cur_map;
    bool cur_map_ok = false;

    bool load_is_replays = false;
    std::vector<std::string> browser_files;
    int browser_sel = 0;
    float browser_scroll = 0;

    // Splash / Credits / Tips / Toast
    double splash_ms = 0;
    double credits_ms = 0;
    int tip_idx = 0;
    int save_slot_sel = 0;
    std::string toast_msg;
    double toast_until = 0;

    // Runtime settings synced with host App
    int sim_fps = 24;
    float sfx_vol = 0.90f;
    float mus_vol = 0.60f;
    int hw_vol = 28;
    bool handheld_1to1 = false;
    double world_zoom = 1.0; // 0.75, 1.0, 1.25, 1.5
    bool request_fullscreen_toggle = false;
    bool request_quit = false;
    bool request_rescan = false;

    std::string save_dir, replay_dir, data_dir;
    std::vector<HitBtn> modal_btns;
    int last_hover_wid = -1;

    void show_toast(const std::string& msg, double now_ms, double dur = 3000.0) {
        toast_msg = msg;
        toast_until = now_ms + dur;
    }

    void draw_checkmark(Canvas& c, int cx, int cy) {
        uint32_t col = pack_rgba(120, 245, 110);
        for (int dx = -4; dx <= 5; ++dx) {
            int dy = (dx < -1) ? (-dx - 2) : (3 - dx);
            c.fill_rect(cx + dx, cy - dy, 2, 2, col);
        }
    }

    void close_brief_portraits() {
        for (auto& p : brief_portraits) p.close();
    }

    void open_title() {
        close_brief_portraits();
        glue.close();
        cinematic.close();
        story.clear();
        modal = ModalId::None;
        splash_ms = 0;
        detect_locale();
        if (load_pcx("glue\\title\\title.pcx", title_pcx, false, -1)) {
            screen = ScreenId::TitleSplash;
        } else {
            open_main_menu();
        }
    }

    void open_main_menu() {
        close_brief_portraits();
        cinematic.close();
        story.clear();
        modal = ModalId::None;
        sd_close_game();
        glue.open("glumain", "palmm", false);
        detect_locale();
        sd_play_mpq_music("music\\title.wav");
        screen = ScreenId::MainMenu;
    }

    void open_campaign_select() {
        close_brief_portraits();
        modal = ModalId::None;
        const char* bin = progress.expansion ? "gluexpcmpgn" : "glucmpgn";
        if (!glue.open(bin, "palcs", true)) glue.open("glucmpgn", "palnl", true);
        sd_play_mpq_sound(-1, "sound\\glue\\swishin.wav", 60, false);
        sd_play_mpq_music("music\\title.wav");
        screen = ScreenId::CampaignSelect;
    }

    void open_history_modal(int widget_id) {
        // glucmpgn/gluexpcmpgn: 6=Protoss, 7=Terran, 8=Zerg
        int r = (widget_id == 6) ? 2 : (widget_id == 7 ? 1 : 0);
        int ep = progress.expansion ? (r == 2 ? 3 : (r == 1 ? 4 : 5))
                                    : (r == 1 ? 0 : (r == 0 ? 1 : 2));
        hist_race = r;
        hist_items.clear();
        for (int i = 0; i < kMissionCount; ++i) {
            const auto& m = kMissions[i];
            if (m.episode != ep) continue;
            HistItem hi;
            hi.mission_idx = i;
            hi.unlock_code = m.code;
            if (m.code == 50) hi.label = std::string("Tutorial: ") + m.title;
            else if (m.number >= 99) hi.label = std::string("Secret: ") + m.title;
            else hi.label = std::to_string(m.number) + ". " + m.title;
            hist_items.push_back(hi);

            if (m.after_smk && m.after_smk[0] != '\0') {
                HistItem ci;
                ci.is_cinematic = true;
                ci.smk_path = m.after_smk;
                ci.label = std::string("Cinematic: ") + m.after_title;
                ci.unlock_code = (m.next_code > 0) ? m.next_code : m.code;
                hist_items.push_back(ci);
            }
        }
        hist_sel = 0;
        for (int i = 0; i < int(hist_items.size()); ++i) {
            if (!hist_items[size_t(i)].is_cinematic && progress.is_available(hist_items[size_t(i)].unlock_code))
                hist_sel = i;
        }
        hist_scroll = std::max(0.0f, float((hist_sel - 4) * 19));
        load_pcx("glue\\campaign\\phist.pcx", phist_pcx, true, glue.glass_idx);
        hist_dlg.load("gluhist");
        modal = ModalId::MissionHistory;
    }

    void start_mission_flow(int mission_idx) {
        if (mission_idx < 0 || mission_idx >= kMissionCount) return;
        const auto& m = kMissions[mission_idx];
        active_mission_idx = mission_idx;
        active_race = m.race;
        active_is_replay = false;
        progress.expansion = m.expansion;
        modal = ModalId::None;

        // Mount campaign MPQ in slot 3 so sd_init_campaign & briefing voices resolve
        const std::string camp_mpq = m.expansion ? (data_dir + "/BroodWar.mpq") : (data_dir + "/StarCraft.mpq");
        (void)camp_mpq; // main.cpp pre-mounts slot 3 via callback before calling open_briefing_for_mission

        if (m.opening && m.opening[0] != '\0' && story.open(m.opening)) {
            glue.close();
            sd_play_mpq_music("music\\title.wav");
            screen = ScreenId::Story;
        } else {
            open_briefing_room();
        }
    }

    bool prepare_campaign_chk_cb_ready = false;
    std::string starcraft_mpq_path, broodwar_mpq_path;

    void open_briefing_room() {
        modal = ModalId::None;
        close_brief_portraits();
        if (active_mission_idx >= 0 && active_mission_idx < kMissionCount) {
            const auto& m = kMissions[active_mission_idx];
            const std::string& mpq = m.expansion ? broodwar_mpq_path : starcraft_mpq_path;
            char err[256] = {};
            if (!mpq.empty()) sd_open_archive(3, mpq.c_str(), err, sizeof(err));
            // Extract scenario.chk from campaign prefix in slot 3
            std::string chk_path = std::string(m.prefix) + "staredit\\scenario.chk";
            auto chk_bytes = mpq_read(chk_path, 3);
            if (chk_bytes.empty()) chk_bytes = mpq_read(chk_path, -1);
            campaign.load_chk(chk_bytes.data(), chk_bytes.size(), m.player);
            active_race = m.race;
        }

        static const char kRL[3] = {'z', 't', 'p'};
        char rc = kRL[std::max(0, std::min(2, active_race))];
        std::string bin = std::string("glurdy") + rc;
        std::string pal = std::string("palr") + rc;
        glue.open(bin, pal, true);
        sd_play_mpq_sound(-1, "sound\\glue\\swishin.wav", 60, false);
        std::string rdy_mus = std::string("music\\") + rc + "rdyroom.wav";
        sd_play_mpq_music(rdy_mus.c_str());

        last_brief_rev = -1;
        brief_scroll = 0;
        brief_manual = -1;
        for (auto& bp : brief_portraits) bp.init_tables();
        screen = ScreenId::Briefing;
    }

    bool launch_from_briefing(double now_ms) {
        close_brief_portraits();
        glue.close();
        char err[256] = {};
        if (active_mission_idx >= 0 && active_mission_idx < kMissionCount) {
            const auto& m = kMissions[active_mission_idx];
            const std::string& mpq = m.expansion ? broodwar_mpq_path : starcraft_mpq_path;
            if (mpq.empty() || !sd_open_archive(3, mpq.c_str(), err, sizeof(err))) {
                show_toast(std::string("Missing campaign MPQ: ") + err, now_ms, 4500);
                open_campaign_select();
                return false;
            }
            if (!sd_init_campaign(W, H, m.code, err, sizeof(err))) {
                show_toast(std::string("Campaign init failed: ") + err, now_ms, 4500);
                open_campaign_select();
                return false;
            }
            size_t clen = 0;
            if (const uint8_t* cptr = sd_campaign_chk(&clen))
                campaign.load_chk(cptr, clen, m.player);
            campaign.enter_mission();
        } else {
            // UMS Custom Map launched after briefing!
            if (!start_custom_engine(now_ms, true)) return false;
            campaign.enter_mission();
        }
        enter_playing_common();
        return true;
    }

    void enter_playing_common() {
        sd_set_hud_inset(104, true, true);
        sd_resize_viewport(W, H, world_zoom);
        sd_set_audio(false, sfx_vol, mus_vol);
        int r = sd_value(5, 0);
        if (r < 0 || r > 2) r = active_race;
        active_race = std::max(0, std::min(2, r));
        hud.close();
        hud.ensure_loaded(active_race);

        int track_num = 1 + (std::rand() % 3);
        static const char* kRNames[3] = {"zerg", "terran", "protoss"};
        std::string mus = std::string("music\\") + kRNames[active_race] + std::to_string(track_num) + ".wav";
        if (!sd_play_mpq_music(mus.c_str())) {
            mus = std::string("music\\") + kRNames[active_race] + "1.wav";
            sd_play_mpq_music(mus.c_str());
        }
        outcome_dismissed = false;
        last_outcome = 0;
        modal = ModalId::None;
        screen = ScreenId::Playing;
    }

    void open_custom_game() {
        close_brief_portraits();
        modal = ModalId::None;
        active_mission_idx = -1;
        active_is_replay = false;
        glue.open("glucustm", "palnl", true);
        sd_play_mpq_sound(-1, "sound\\glue\\swishin.wav", 60, false);
        sd_play_mpq_music("music\\title.wav");
        if (map_sel >= int(map_files.size())) map_sel = 0;
        select_custom_map(map_sel);
        screen = ScreenId::CustomGame;
    }

    void select_custom_map(int idx) {
        if (map_files.empty()) { cur_map_ok = false; return; }
        map_sel = std::max(0, std::min(int(map_files.size()) - 1, idx));
        cur_map_ok = build_map_preview(map_files[size_t(map_sel)], cur_map);
        if (cur_map_ok) {
            if (cur_map.ums_only) setup.apply_ums(cur_map);
            else if (setup.mode == 3) { setup.mode = 0; setup.clamp_to_starts(cur_map.start_locs); }
            else setup.clamp_to_starts(cur_map.start_locs);
        }
    }

    bool start_custom_engine(double now_ms, bool skip_briefing_check = false) {
        if (map_files.empty() || !cur_map_ok) {
            show_toast("Select a valid .scx/.scm map first.", now_ms);
            return false;
        }
        const std::string& mpath = map_files[size_t(map_sel)];
        if (!skip_briefing_check && setup.mode == 3) {
            uint8_t* raw = nullptr; size_t sz = 0;
            if (sd_map_scenario_chk(mpath.c_str(), &raw, &sz) && raw) {
                bool has_brief = campaign.load_chk(raw, sz, setup.my_slot);
                sd_free_buf(raw);
                if (has_brief) {
                    active_mission_idx = -1;
                    active_race = std::max(0, std::min(2, setup.races[setup.my_slot]));
                    open_briefing_room();
                    return true;
                }
            }
        }
        char err[256] = {};
        if (!sd_open_archive(3, mpath.c_str(), err, sizeof(err))) {
            show_toast(std::string("Map open failed: ") + err, now_ms, 4500);
            return false;
        }
        int resolved_races[8];
        for (int i = 0; i < 8; ++i)
            resolved_races[i] = (setup.races[i] >= 0 && setup.races[i] <= 2) ? setup.races[i] : (std::rand() % 3);

        bool bw = progress.expansion && has_broodwar;
        bool ok = sd_init_configured(W, H, setup.my_slot, resolved_races, setup.controllers,
                                     setup.teams, setup.mode == 2, setup.difficulty, bw,
                                     setup.mode == 1, setup.hero_mode, setup.mode == 3,
                                     setup.match_opts, err, sizeof(err));
        if (!ok) {
            show_toast(std::string("Launch error: ") + err, now_ms, 4500);
            return false;
        }
        sim_fps = setup.sim_fps();
        active_race = resolved_races[setup.my_slot];
        size_t clen = 0;
        if (const uint8_t* cptr = sd_campaign_chk(&clen))
            campaign.load_chk(cptr, clen, setup.my_slot);
        campaign.enter_mission();
        glue.close();
        enter_playing_common();
        return true;
    }

    void open_load_browser(bool replays, const std::vector<std::string>& files) {
        close_brief_portraits();
        modal = ModalId::None;
        load_is_replays = replays;
        browser_files = files;
        browser_sel = 0;
        browser_scroll = 0;
        if (!glue.open("gluload", "palnl", true)) glue.open("glucustm", "palnl", true);
        load_pcx("glue\\load\\pinfo.pcx", load_pinfo_pcx, true, glue.glass_idx);
        sd_play_mpq_sound(-1, "sound\\glue\\swishin.wav", 60, false);
        sd_play_mpq_music("music\\title.wav");
        screen = ScreenId::LoadBrowser;
    }

    void finish_score_screen() {
        sd_sound_stop(62);
        sd_close_game();
        if (active_mission_idx >= 0 && active_mission_idx < kMissionCount && score.won) {
            const auto& m = kMissions[active_mission_idx];
            // Resolve next mission from Set Next Scenario trigger (type 41) or missions.json next_code
            int next_idx = -1;
            if (!campaign.next_scenario.empty()) {
                for (int i = 0; i < kMissionCount; ++i) {
                    std::string id_low = norm_path(kMissions[i].id);
                    if (!id_low.empty() && campaign.next_scenario.find(id_low) != std::string::npos) {
                        next_idx = i;
                        break;
                    }
                }
            }
            if (next_idx < 0 && m.next_code > 0) {
                for (int i = 0; i < kMissionCount; ++i)
                    if (kMissions[i].code == m.next_code) { next_idx = i; break; }
            }
            progress.mark_completed(m.code, next_idx >= 0 ? kMissions[next_idx].code : m.next_code);

            // Play post-mission cinematic if defined in CinematicCatalog!
            if (m.after_smk && m.after_smk[0] != '\0' && cinematic.open(m.after_smk, m.after_title)) {
                active_mission_idx = next_idx;
                after_cinematic = (next_idx >= 0) ? ScreenId::Briefing : ScreenId::CampaignSelect;
                screen = ScreenId::Cinematic;
                return;
            }
            if (next_idx >= 0) {
                start_mission_flow(next_idx);
                return;
            }
            open_campaign_select();
            return;
        }
        if (active_mission_idx >= 0) open_campaign_select();
        else open_custom_game();
    }

    // --- Hover & Audio Feedback ---
    void set_glue_hover(int wid) {
        if (wid != glue.hot_id) {
            glue.hot_id = wid;
            if (wid >= 0 && glue.ready())
                sd_play_mpq_sound(-1, "sound\\glue\\mouseover.wav", 61, false);
        }
    }

    void update_pointer(int mx, int my) {
        if (modal != ModalId::None) return;
        if (screen == ScreenId::MainMenu) {
            int hit = -1;
            for (int id : {3, 4, 5, 8, 9, 2}) {
                if (const auto* w = glue.layout.get(id))
                    if (w->hit_contains(mx, my, 0)) { hit = id; break; }
            }
            set_glue_hover(hit);
        } else if (screen == ScreenId::CampaignSelect) {
            int hit = -1;
            for (int id : {6, 7, 8, 5, 11, 10, 9}) {
                if (const auto* w = glue.layout.get(id))
                    if (glue.label_visible(id) && w->hit_contains(mx, my, glue.shift(w))) { hit = id; break; }
            }
            set_glue_hover(hit);
        } else if (screen == ScreenId::Briefing) {
            int hit = -1;
            for (int id : {13, 14, 20}) {
                if (const auto* w = glue.layout.get(id))
                    if (w->hit_contains(mx, my, glue.shift(w))) { hit = id; break; }
            }
            set_glue_hover(hit);
        } else if (screen == ScreenId::CustomGame) {
            int hit = -1;
            for (int id : {12, 13}) {
                if (const auto* w = glue.layout.get(id))
                    if (w->hit_contains(mx, my, glue.shift(w))) { hit = id; break; }
            }
            set_glue_hover(hit);
        } else if (screen == ScreenId::LoadBrowser) {
            int hit = -1;
            for (const auto& w : glue.layout.widgets) {
                if ((w.type == 14 || w.type == 1 || w.type == 2) && w.hit_contains(mx, my, glue.shift(&w))) {
                    hit = w.id; break;
                }
            }
            set_glue_hover(hit);
        } else if (screen == ScreenId::Score) {
            score.update_hover(mx, my);
        }
    }

    void draw_cursor_overlay(Canvas& c, double now_ms, int mx, int my) {
        if (screen == ScreenId::Cinematic || screen == ScreenId::TitleSplash) return;
        if (screen == ScreenId::Playing && modal == ModalId::None) {
            int cbox = 0;
            if (const uint8_t* cur = sd_cursor_rgba(&cbox))
                c.blit(reinterpret_cast<const uint32_t*>(cur), cbox, cbox, mx - cbox / 2, my - cbox / 2);
            else
                cursor.draw(c, mx, my, now_ms);
        } else {
            cursor.draw(c, mx, my, now_ms);
        }
    }

    // --- Render Complete Frame onto 640x480 Canvas ---
    void render(Canvas& c, double now_ms, double dt_ms, int mx, int my, bool lmb_down, bool with_cursor = true) {
        modal_btns.clear();
        update_pointer(mx, my);

        switch (screen) {
        case ScreenId::DataMissing: {
            c.fill_rect(0, 0, W, H, pack_rgba(10, 14, 22));
            c.stroke_rect(24, 24, W - 48, H - 48, pack_rgba(192, 0, 36));
            auto draw_fb = [&](int x, int y, const char* s, uint32_t col) {
                for (int i = 0; s[i]; ++i) {
                    const uint8_t* g = font8x16 + uint8_t(s[i]) * 16;
                    for (int gy = 0; gy < 16; ++gy)
                        for (int gx = 0; gx < 8; ++gx)
                            if (g[gy] & (0x80u >> gx)) c.blend_px(x + i * 8 + gx, y + gy, col);
                }
            };
            draw_fb(48, 52, "STARDROID 1.9.11 - ORIGINAL STARCRAFT DATA REQUIRED", pack_rgba(255, 210, 90));
            draw_fb(48, 92, "Place StarDat.mpq, BrooDat.mpq, Patch_rt.mpq,", pack_rgba(210, 225, 245));
            draw_fb(48, 114, "StarCraft.mpq & BroodWar.mpq into:", pack_rgba(210, 225, 245));
            draw_fb(48, 144, data_dir.c_str(), pack_rgba(110, 235, 145));
            draw_fb(48, 196, "Press [R] or [A / ENTER] to rescan, [ESC] to exit.", pack_rgba(180, 195, 215));
            break;
        }
        case ScreenId::TitleSplash: {
            splash_ms += dt_ms;
            c.blit_img(title_pcx.image, 0, 0);
            if (splash_ms >= 2400.0) open_main_menu();
            break;
        }
        case ScreenId::MainMenu: {
            glue.draw_base(c, dt_ms);
            glue.draw_label(c, 3, "Single Player");
            glue.draw_label(c, 4, tr("Skirmish / Custom", "Своя игра").c_str(), true, true);
            glue.draw_label(c, 5, tr("Options", "Настройки").c_str(), true, true);
            glue.draw_label(c, 8, "View Intro");
            glue.draw_label(c, 9, "Credits");
            glue.draw_label(c, 2, "Exit");
            break;
        }
        case ScreenId::CampaignSelect: {
            glue.draw_base(c, dt_ms);
            bool bw = progress.expansion;
            glue.draw_label(c, 65535, bw ? "Brood War" : "StarCraft");
            glue.draw_label(c, 6, bw ? "Protoss\nEpisode IV" : "Protoss\nEpisode III");
            glue.draw_label(c, 7, bw ? "Terran\nEpisode V"  : "Terran\nEpisode I");
            glue.draw_label(c, 8, bw ? "Zerg\nEpisode VI"   : "Zerg\nEpisode II");
            glue.draw_label(c, 5, "Load Saved");
            glue.draw_label(c, 11, tr("Load Replay", "Повторы").c_str());
            glue.draw_label(c, 10, "Play Custom");
            glue.draw_label(c, 9, "Cancel");
            break;
        }
        case ScreenId::Story: {
            if (!story.draw(c, dt_ms)) open_briefing_room();
            break;
        }
        case ScreenId::Cinematic: {
            if (!cinematic.draw(c, dt_ms)) {
                if (after_cinematic == ScreenId::Briefing && active_mission_idx >= 0)
                    start_mission_flow(active_mission_idx);
                else if (after_cinematic == ScreenId::CampaignSelect)
                    open_campaign_select();
                else
                    open_main_menu();
            }
            break;
        }
        case ScreenId::Briefing: {
            if (glue.ready() && modal == ModalId::None) campaign.tick_briefing(dt_ms);
            glue.draw_base(c, dt_ms);

            if (campaign.msg_revision != last_brief_rev) {
                last_brief_rev = campaign.msg_revision;
                brief_start_ms = now_ms;
                brief_len_ms   = campaign.message_duration;
                brief_manual   = -1;
                brief_scroll   = 0;
            }
            if (const auto* tb = glue.layout.get(65526)) {
                int sx = glue.shift(tb);
                c.set_clip(tb->x + sx, tb->y, tb->x + sx + tb->w, tb->y + tb->h);
                auto lines = glue.dialogue.wrap_lines(campaign.message, tb->w);
                double pos = campaign.transmission ? campaign.trans_pos : (now_ms - brief_start_ms);
                double spk = (campaign.transmission && campaign.spoken_len > 0) ? double(campaign.spoken_len) : brief_len_ms;
                float auto_sc = briefing_scroll::compute(lines, pos, spk, tb->h);
                if (brief_manual >= 0 && now_ms < brief_manual_until) brief_scroll = brief_manual;
                else {
                    brief_manual = -1;
                    brief_scroll += (auto_sc - brief_scroll) * float(1.0 - std::exp(-std::min(100.0, dt_ms) / 125.0));
                }
                glue.dialogue.draw(c, campaign.message, tb->x + sx, tb->y - int(brief_scroll), tb->w, 0, 2, 19);
                c.reset_clip();
            }
            if (const auto* gl = glue.layout.get(65525)) {
                int sx = glue.shift(gl);
                c.set_clip(gl->x + sx, gl->y, gl->x + sx + gl->w, gl->y + gl->h);
                glue.small.draw(c, campaign.objectives, gl->x + sx, gl->y, gl->w, 0, 2, 0);
                c.reset_clip();
            }
            glue.draw_label(c, 65535, "Mission Objectives");
            for (int i = 0; i < 4; ++i) {
                int ut = campaign.portraits[i];
                bool tk = campaign.briefing_speaking(i);
                const auto* w = glue.layout.get(15 + i);
                brief_portraits[i].set_target(ut, tk);
                if (w && ut >= 0) {
                    int px = w->x + glue.shift(w);
                    const auto* msk = (glue.portrait_masks[i].size() == size_t(w->w) * size_t(w->h))
                                      ? &glue.portrait_masks[i] : nullptr;
                    brief_portraits[i].draw(c, px, w->y, w->w, w->h, now_ms, modal != ModalId::None, true, msk);
                    c.blit_img(glue.portrait_frames[i], px, w->y);
                    if (tk) c.blit_img(glue.speaker_frames[i], px, w->y);
                }
            }
            glue.draw_label(c, 13, "Start");
            glue.draw_label(c, 14, "Cancel");
            glue.draw_label(c, 20, "Replay");
            break;
        }
        case ScreenId::CustomGame: {
            render_custom_game(c, dt_ms, mx, my);
            break;
        }
        case ScreenId::LoadBrowser: {
            render_load_browser(c, dt_ms, mx, my);
            break;
        }
        case ScreenId::Playing: {
            sd_render(now_ms);
            int fbw = 0, fbh = 0;
            if (const uint8_t* fb = sd_framebuffer(&fbw, &fbh)) {
                if (fbw == W && fbh == H) {
                    if (direct_fb_ok) c.dst = const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(fb));
                    else std::memcpy(c.dst, fb, W * H * 4);
                } else {
                    c.blit_scaled(reinterpret_cast<const uint32_t*>(fb), fbw, fbh, 0, 0, W, H);
                }
            }
            bool is_paused = (modal != ModalId::None);
            if (!is_paused) campaign.poll_mission();
            hud.draw(c, now_ms, is_paused, mx, my, lmb_down, campaign);

            int outc = sd_value(7, 0);
            if ((outc == 1 || outc == 2) && !outcome_dismissed && modal == ModalId::None) {
                last_outcome = outc;
                sd_command(6, 1, 0, 0, 0);
                modal = ModalId::OutcomePrompt;
            }
            break;
        }
        case ScreenId::Score: {
            score.draw(c, dt_ms);
            break;
        }
        case ScreenId::Credits: {
            render_credits(c, dt_ms);
            break;
        }
        }

        if (modal != ModalId::None) render_modal(c, mx, my);

        if (!toast_msg.empty() && now_ms < toast_until) {
            BlizzFont& tf = (screen == ScreenId::Playing) ? hud.small : glue.small;
            int tw = std::min(560, std::max(220, tf.measure(toast_msg) + 28));
            int tx = (W - tw) / 2, ty = 14;
            c.fill_rect(tx, ty, tw, 24, pack_rgba(14, 24, 38, 238));
            c.stroke_rect(tx, ty, tw, 24, pack_rgba(255, 195, 75));
            tf.draw(c, toast_msg, tx + 10, ty + 5, tw - 20, 1, 4, 0);
        }

        // Authentic StarCraft animated cursor!
        if (with_cursor) draw_cursor_overlay(c, now_ms, mx, my);
    }

    void add_btn(int id, int x, int y, int w, int h, bool en = true) {
        modal_btns.push_back({x, y, w, h, id, en});
    }

    void draw_glue_red_btn(Canvas& c, int id, int x, int y, int w, int h, const std::string& txt, int mx, int my, bool en = true) {
        bool hov = en && mx >= x && mx < x + w && my >= y && my < y + h;
        glue.draw_box_frame(c, x, y, w, h, true, hov, en);
        c.set_clip(x + 2, y + 1, x + w - 2, y + h - 1);
        glue.small.draw(c, txt, x + 3, y + std::max(0, (h - glue.small.height) / 2), w - 6, 1, !en ? 5 : (hov ? 4 : 3), 0);
        c.reset_clip();
        add_btn(id, x, y, w, h, en);
    }

    void render_custom_game(Canvas& c, double dt_ms, int mx, int my) {
        glue.draw_base(c, dt_ms);
        if (!glue.ready()) return;

        bool bw = progress.expansion && has_broodwar;
        glue.draw_label(c, 65535, bw ? "Brood War" : "StarCraft");
        if (const auto* w6 = glue.layout.get(6))
            glue.large.draw(c, tr("Select Map", "Выбор карты"), w6->x, w6->y, w6->w, 0, 2, 0);

        // Match Options button top-left beside Maps header
        draw_glue_red_btn(c, 100, 232, 54, 126, 20, tr("Match Options...", "Опции матча..."), mx, my, true);

        // Map list box (widget 5 or fallback 44..356, 82..242)
        int lx = 44, ly = 82, lw = 312, lh = 160;
        if (const auto* lb = glue.layout.get(5)) { lx = lb->x; ly = lb->y; lw = lb->w; lh = lb->h; }
        int right = lx + lw - 20;
        float max_sc = std::max(0.0f, float(int(map_files.size()) * 20 - lh));
        map_scroll = std::max(0.0f, std::min(map_scroll, max_sc));

        c.set_clip(lx, ly, right, ly + lh);
        for (int i = std::max(0, int(map_scroll / 20)); i < int(map_files.size()) && (i * 20 - map_scroll) < lh; ++i) {
            int ry = ly + int(i * 20 - map_scroll);
            size_t sl = map_files[size_t(i)].find_last_of("/\\");
            std::string fn = (sl == std::string::npos) ? map_files[size_t(i)] : map_files[size_t(i)].substr(sl + 1);
            bool sel = (i == map_sel);
            if (sel) c.fill_rect(lx + 1, ry, right - lx - 2, 19, pack_rgba(90, 14, 24, 150));
            glue.small.draw(c, fn, lx + 4, ry + 2, right - lx - 8, 0, sel ? 4 : 2, 0);
            add_btn(1000 + i, lx, std::max(ly, ry), right - lx, 20, true);
        }
        c.reset_clip();
        glue.draw_scrollbar(c, right + 2, ly, lh, max_sc > 0 ? (map_scroll / max_sc) : 0.0f);
        add_btn(101, right, ly, 20, 24, map_scroll > 0);
        add_btn(102, right, ly + lh - 24, 20, 24, map_scroll < max_sc);

        // Right panel: Map Title, 128x128 Live Terrain Preview, Size, Tileset, Speed
        if (cur_map_ok) {
            c.set_clip(408, 76, 602, 104);
            glue.dialogue.draw(c, cur_map.title, 410, 80, 190, 1, 4, 0);
            c.reset_clip();
            c.fill_rect(439, 106, 132, 132, pack_rgba(5, 8, 12));
            int ox = 441 + (128 - cur_map.preview.w) / 2;
            int oy = 108 + (128 - cur_map.preview.h) / 2;
            c.blit_img(cur_map.preview, ox, oy);
            bool phov = (mx >= 438 && mx < 572 && my >= 105 && my < 239);
            c.stroke_rect(438, 105, 134, 134, phov ? pack_rgba(255, 65, 75) : pack_rgba(192, 0, 36));
            add_btn(103, 438, 105, 134, 134, true); // Map Details popup

            glue.tiny.draw(c, "Size: " + std::to_string(cur_map.tw) + "x" + std::to_string(cur_map.th) +
                              "  Starts: " + std::to_string(cur_map.start_locs), 414, 244, 184, 0, 2, 0);
            glue.tiny.draw(c, "Tileset: " + cur_map.tileset_name, 414, 260, 184, 0, 2, 0);
        }

        // Game Type, Difficulty, Speed
        glue.small.draw(c, "Game Type", 58, 252, 112, 0, 2, 0);
        draw_glue_red_btn(c, 104, 180, 250, 172, 18, setup.mode_label(), mx, my, cur_map_ok);

        glue.small.draw(c, "Difficulty", 58, 274, 112, 0, 2, 0);
        draw_glue_red_btn(c, 105, 180, 272, 172, 18, setup.mode == 3 ? "Map Settings" : setup.diff_label(), mx, my, setup.mode != 3);

        draw_glue_red_btn(c, 106, 410, 282, 190, 22, std::string("Speed: ") + setup.speed_label(), mx, my, true);

        // 8-Slot Player / AI Roster (y = 315 + row * 18)
        static const char* kR[4] = {"Zerg", "Terran", "Protoss", "Random"};
        for (int s = 0; s < 8; ++s) {
            int ry = 313 + s * 18;
            bool act = (setup.controllers[s] != 0);
            std::string cname = (setup.controllers[s] == 1) ? ("P" + std::to_string(s + 1) + ": You")
                              : (setup.controllers[s] == 2 ? ("P" + std::to_string(s + 1) + ": Computer") : "Closed");
            bool can_tog = (setup.mode != 3) ? (s > 0 && s < std::max(2, cur_map.start_locs)) : (cur_map.owners[s] == 6);
            draw_glue_red_btn(c, 200 + s, 34, ry, 138, 16, cname, mx, my, can_tog);
            draw_glue_red_btn(c, 210 + s, 176, ry, 104, 16, act ? kR[setup.races[s] & 3] : "-", mx, my, act && setup.mode != 3);
            if (setup.mode == 2 || setup.mode == 3) {
                draw_glue_red_btn(c, 220 + s, 284, ry, 68, 16, act ? ("Team " + std::to_string(setup.teams[s])) : "-", mx, my, act && setup.mode == 2);
            }
        }

        glue.draw_label(c, 12, "OK");
        glue.draw_label(c, 13, "Cancel");
    }

    void render_load_browser(Canvas& c, double dt_ms, int mx, int my) {
        glue.draw_base(c, dt_ms);
        bool has_pinfo = false;
        for (const auto& w : glue.layout.widgets)
            if (w.type == 5 && norm_path(w.text).find("\\pinfo.pcx") != std::string::npos && glue.panels.count(w.id))
                has_pinfo = true;
        if (glue.name == "gluload" && !has_pinfo && load_pinfo_pcx.image.valid()) {
            BinWidget tmp{}; tmp.x = 388; tmp.y = 55; tmp.w = 240; tmp.h = 301; tmp.type = 5;
            c.blit_scaled(load_pinfo_pcx.image.px.data(), load_pinfo_pcx.width, load_pinfo_pcx.height,
                          388 + glue.shift(&tmp), 55, 240, 301);
        }
        if (!glue.ready()) return;

        glue.large.draw(c, load_is_replays ? tr("Load Saved Replay", "Загрузка повтора")
                                           : tr("Load Saved Game", "Загрузка игры"), 44, 62, 290, 0, 2, 0);

        int lx = 64, ly = 92, lw = 286, lh = 218;
        for (const auto& w : glue.layout.widgets) if (w.type == 12) { lx = w.x; ly = w.y; lw = w.w; lh = w.h; break; }
        int right = lx + lw - 20;
        float max_sc = std::max(0.0f, float(int(browser_files.size()) * 22 - lh));
        browser_scroll = std::max(0.0f, std::min(browser_scroll, max_sc));

        c.set_clip(lx, ly, right, ly + lh);
        if (browser_files.empty()) {
            glue.small.draw(c, load_is_replays ? "No saved replays (.rep)" : "No saved games (.sav)", lx + 6, ly + 10, lw - 28, 0, 2, 0);
        }
        for (int i = std::max(0, int(browser_scroll / 22)); i < int(browser_files.size()) && (i * 22 - browser_scroll) < lh; ++i) {
            int ry = ly + int(i * 22 - browser_scroll);
            size_t sl = browser_files[size_t(i)].find_last_of("/\\");
            std::string fn = (sl == std::string::npos) ? browser_files[size_t(i)] : browser_files[size_t(i)].substr(sl + 1);
            bool sel = (i == browser_sel);
            if (sel) c.fill_rect(lx + 1, ry, right - lx - 2, 20, pack_rgba(90, 14, 24, 150));
            glue.small.draw(c, fn, lx + 4, ry + 3, right - lx - 8, 0, sel ? 4 : 2, 0);
            add_btn(2000 + i, lx, std::max(ly, ry), right - lx, 22, true);
        }
        c.reset_clip();
        glue.draw_scrollbar(c, right + 2, ly, lh, max_sc > 0 ? (browser_scroll / max_sc) : 0.0f);
        add_btn(301, right, ly, 20, 24, browser_scroll > 0);
        add_btn(302, right, ly + lh - 24, 20, 24, browser_scroll < max_sc);

        if (!browser_files.empty() && browser_sel >= 0 && browser_sel < int(browser_files.size())) {
            size_t sl = browser_files[size_t(browser_sel)].find_last_of("/\\");
            std::string fn = (sl == std::string::npos) ? browser_files[size_t(browser_sel)] : browser_files[size_t(browser_sel)].substr(sl + 1);
            glue.dialogue.draw(c, fn, 404, 114, 196, 0, 4, 0);
            glue.small.draw(c, load_is_replays ? "Recorded StarCraft Match\nPress OK to watch replay."
                                               : "Saved StarCraft Session\nPress OK to resume game.", 404, 150, 196, 0, 2, 18);
            draw_glue_red_btn(c, 303, 64, 326, 156, 22, "Delete File", mx, my, true);
        }

        // OK & Cancel widgets
        int ok_id = 4, cancel_id = 5;
        if (glue.name == "glucustm") { ok_id = 12; cancel_id = 13; }
        else {
            for (const auto& w : glue.layout.widgets) {
                if ((w.type == 14 || w.type == 1 || w.type == 2) && (w.flags & 0x80000u)) ok_id = w.id;
                if ((w.type == 14 || w.type == 1 || w.type == 2) && (w.flags & 64u)) cancel_id = w.id;
            }
        }
        glue.draw_label(c, ok_id, "OK", !browser_files.empty());
        glue.draw_label(c, cancel_id, "Cancel", true);
    }

    void render_credits(Canvas& c, double dt_ms) {
        credits_ms += dt_ms;
        c.blit_img(glue.background.image, 0, 0);
        c.fill_rect(0, 0, W, H, pack_rgba(0, 0, 0, 165));
        static const char* kCreds[] = {
            "\x04STARCRAFT & STARCRAFT: BROOD WAR",
            "Original Game by Blizzard Entertainment",
            "",
            "\x04STARDROID 1.9.11 ENGINE",
            "OpenBW C++ Simulation & StarDroid Port Architecture",
            "kinopiowarrior & OpenBW Contributors",
            "",
            "\x04XGB35 LINUX DESKTOP & HANDHELD PORT",
            "Native ARMv7 X11 + 480x320 ST7796S Framebuffer",
            "Dual-Stick Console Controls, USB Mouse/Keyboard & ALSA Audio",
            "",
            "Click or press ESC / START / A to return to Main Menu"
        };
        int y0 = H - int(credits_ms * 0.045);
        for (size_t i = 0; i < sizeof(kCreds) / sizeof(kCreds[0]); ++i) {
            int y = y0 + int(i) * 28;
            if (y > -30 && y < H + 10)
                glue.large.draw(c, kCreds[i], 40, y, W - 80, 1, 2, 0);
        }
        if (y0 + int(sizeof(kCreds) / sizeof(kCreds[0])) * 28 < 40) open_main_menu();
    }

    // --- Render Active Modal (Glue Modals & In-Game F10 Classic Menus) ---
    void render_modal(Canvas& c, int mx, int my) {
        c.fill_rect(0, 0, W, H, pack_rgba(0, 0, 0, 132));

        if (modal == ModalId::ExpansionMain || modal == ModalId::ExpansionCustom || modal == ModalId::ExpansionIntro) {
            if (!retail_ex_pcx.image.valid()) load_pcx("glue\\palmm\\retail_ex.pcx", retail_ex_pcx, false, -1);
            if (gamemode_dlg.widgets.empty()) gamemode_dlg.load("glugamemode");
            if (retail_ex_pcx.image.valid() && !gamemode_dlg.widgets.empty()) {
                int lx = (W - retail_ex_pcx.width) / 2, ly = (H - retail_ex_pcx.height) / 2;
                c.blit_img(retail_ex_pcx.image, lx, ly);
                const int ids[3] = {6, 7, 3};
                const std::string lbls[3] = {
                    tr("Original", "Оригинал"),
                    tr("Brood War", "Brood War"),
                    tr("Cancel", "Отмена")
                };
                bool en[3] = {
                    modal != ModalId::ExpansionMain || has_sc_camp,
                    has_broodwar && (modal != ModalId::ExpansionMain || has_bw_camp),
                    true
                };
                for (int i = 0; i < 3; ++i) {
                    if (const auto* w = gamemode_dlg.get(ids[i])) {
                        int bx = lx + w->x, by = ly + w->y;
                        bool hov = en[i] && mx >= bx && mx < bx + w->w && my >= by && my < by + w->h;
                        int ty = by + (i < 2 ? (w->h - glue.button.height) : (w->h - glue.button.height) / 2);
                        glue.button.draw(c, lbls[i], bx, ty, w->w, 1, !en[i] ? 5 : (hov ? 4 : 3), 0);
                        add_btn(500 + i, bx, by, w->w, w->h, en[i]);
                    }
                }
                return;
            }
            // Clean fallback box if retail_ex.pcx missing
            glue.draw_box_frame(c, 160, 140, 320, 200, false);
            glue.large.draw(c, "Select Game Version", 176, 156, 288, 1, 4, 0);
            draw_glue_red_btn(c, 500, 190, 196, 260, 32, "StarCraft (Original)", mx, my, true);
            draw_glue_red_btn(c, 501, 190, 240, 260, 32, "StarCraft: Brood War", mx, my, has_broodwar);
            draw_glue_red_btn(c, 502, 190, 290, 260, 32, "Cancel", mx, my, true);
            return;
        }

        if (modal == ModalId::MissionHistory) {
            int ox = 130, oy = 114;
            if (phist_pcx.image.valid()) c.blit_img(phist_pcx.image, ox, oy);
            else glue.draw_box_frame(c, ox, oy, 380, 252, false);

            int bx = 22, by = 36, bw = 336, bh = 158;
            std::string head_txt = "Select Mission";
            if (const auto* hw = hist_dlg.get(65535)) head_txt = hw->text;
            if (const auto* lb = hist_dlg.get(6)) { bx = lb->x; by = lb->y; bw = lb->w; bh = lb->h; }
            glue.small.draw(c, head_txt, ox + 20, oy + 12, 340, 1, 2, 0);

            int vleft = ox + bx, vtop = oy + by, vright = ox + bx + bw - 26, vbot = oy + by + bh;
            float max_sc = std::max(0.0f, float(int(hist_items.size()) * 19 - bh));
            hist_scroll = std::max(0.0f, std::min(hist_scroll, max_sc));

            c.set_clip(vleft, vtop, vright, vbot);
            for (int i = std::max(0, int(hist_scroll / 19)); i < int(hist_items.size()) && (i * 19 - hist_scroll) < bh; ++i) {
                const auto& it = hist_items[size_t(i)];
                int ry = vtop + int(i * 19 - hist_scroll);
                bool avail = progress.is_available(it.unlock_code);
                bool done  = !it.is_cinematic && progress.completed.count(it.unlock_code) > 0;
                if (i == hist_sel) c.fill_rect(vleft, ry, vright - vleft, 19, pack_rgba(90, 14, 24, 145));
                if (done) draw_checkmark(c, vleft + 9, ry + 10);
                glue.dialogue.draw(c, it.label, vleft + 20, ry + 1, vright - vleft - 24, 0,
                                   !avail ? 5 : (i == hist_sel ? 4 : 3), 0);
                add_btn(3000 + i, vleft, std::max(vtop, ry), vright - vleft, 19, true);
            }
            c.reset_clip();

            if (int(hist_items.size()) * 19 > bh) {
                glue.draw_scrollbar(c, vright + 4, vtop, bh, max_sc > 0 ? (hist_scroll / max_sc) : 0.0f);
                add_btn(510, vright, vtop, 26, 24, hist_scroll > 0);
                add_btn(511, vright, vbot - 24, 26, 24, hist_scroll < max_sc);
            }

            c.fill_rect(ox + 12, oy + 202, 356, 44, pack_rgba(0, 0, 0));
            bool sel_ok = (hist_sel >= 0 && hist_sel < int(hist_items.size()) &&
                           progress.is_available(hist_items[size_t(hist_sel)].unlock_code));
            if (!sel_ok && hist_sel >= 0 && hist_sel < int(hist_items.size()))
                glue.tiny.draw(c, "Locked (finish prior mission or enable Unlock All in Options)", ox + 14, oy + 190, 352, 1, 2, 0);

            draw_glue_red_btn(c, 512, ox + 20, oy + 208, 164, 32, tr("OK", "ОК"), mx, my, sel_ok);
            draw_glue_red_btn(c, 513, ox + 196, oy + 208, 164, 32, tr("Cancel", "Отмена"), mx, my, true);
            return;
        }

        if (modal == ModalId::AppOptions) {
            glue.draw_box_frame(c, 120, 74, 400, 332, false);
            glue.large.draw(c, "StarDroid Options", 140, 88, 360, 1, 4, 0);
            char b1[80], b2[80], b3[80], b4[80], b5[80];
            std::snprintf(b1, sizeof(b1), "Sound Effects Volume: %d%%", int(sfx_vol * 100 + 0.5f));
            std::snprintf(b2, sizeof(b2), "Music Volume: %d%%", int(mus_vol * 100 + 0.5f));
            std::snprintf(b3, sizeof(b3), "Handheld LCD Mode (L2): %s", handheld_1to1 ? "1:1 480x320 Follow" : "Full 640x480");
            std::snprintf(b4, sizeof(b4), "Battlefield Zoom: %.2fx", world_zoom);
            std::snprintf(b5, sizeof(b5), "Unlock All Campaign Missions: %s", progress.unlock_all ? "ON" : "OFF");
            draw_glue_red_btn(c, 520, 150, 122, 340, 28, b1, mx, my);
            draw_glue_red_btn(c, 521, 150, 156, 340, 28, b2, mx, my);
            draw_glue_red_btn(c, 522, 150, 190, 340, 28, b3, mx, my);
            draw_glue_red_btn(c, 523, 150, 224, 340, 28, b4, mx, my);
            draw_glue_red_btn(c, 524, 150, 258, 340, 28, b5, mx, my);
            draw_glue_red_btn(c, 525, 150, 292, 340, 28, hud.grid_hotkeys ? "Command Hotkeys: Grid (QWE/ASD/ZXC)" : "Command Hotkeys: Classic StarCraft", mx, my);
            draw_glue_red_btn(c, 526, 150, 326, 164, 28, "Rescan Data", mx, my);
            draw_glue_red_btn(c, 527, 326, 326, 164, 28, "Fullscreen (F11)", mx, my);
            draw_glue_red_btn(c, 528, 220, 364, 200, 30, "OK", mx, my);
            return;
        }

        if (modal == ModalId::MapDetails) {
            glue.draw_box_frame(c, 100, 76, 440, 328, false);
            glue.large.draw(c, cur_map.title, 120, 92, 400, 1, 4, 0);
            glue.small.draw(c, "Size: " + std::to_string(cur_map.tw) + "x" + std::to_string(cur_map.th) +
                               "  ·  Tileset: " + cur_map.tileset_name +
                               "  ·  Start Locations: " + std::to_string(cur_map.start_locs), 120, 118, 400, 1, 2, 0);
            c.set_clip(120, 144, 520, 348);
            glue.small.draw(c, cur_map.desc.empty() ? "No map description provided." : cur_map.desc, 120, 146, 400, 0, 2, 16);
            c.reset_clip();
            draw_glue_red_btn(c, 530, 230, 360, 180, 30, "Close", mx, my);
            return;
        }

        if (modal == ModalId::MatchOptions) {
            glue.draw_box_frame(c, 140, 96, 360, 288, false);
            glue.large.draw(c, "Match & Ruleset Options", 160, 110, 320, 1, 4, 0);
            bool ah = (setup.match_opts & 1u) != 0;
            bool ml = (setup.match_opts & 2u) != 0;
            bool sc = (setup.match_opts & 4u) != 0;
            draw_glue_red_btn(c, 540, 170, 144, 300, 28, std::string("Ruleset: ") + ((progress.expansion && has_broodwar) ? "Brood War" : "Classic StarCraft"), mx, my, has_broodwar);
            draw_glue_red_btn(c, 541, 170, 178, 300, 28, std::string("Auto-Harvest Workers: ") + (ah ? "ON" : "OFF"), mx, my);
            draw_glue_red_btn(c, 542, 170, 212, 300, 28, std::string("Auto-Train / Multi-Larva: ") + (ml ? "ON" : "OFF"), mx, my);
            draw_glue_red_btn(c, 543, 170, 246, 300, 28, std::string("Smart-Cast Spells: ") + (sc ? "ON" : "OFF"), mx, my);
            draw_glue_red_btn(c, 544, 170, 280, 300, 28, std::string("Hero Mode: ") + (setup.hero_mode ? "ON" : "OFF"), mx, my);
            draw_glue_red_btn(c, 545, 230, 332, 180, 32, "Done", mx, my);
            return;
        }

        // --- In-Game Classic StarCraft F10 Dialogs ---
        auto draw_sc_btn = [&](int id, int x, int y, int w, int h, const std::string& txt, bool en = true) {
            bool hov = en && mx >= x && mx < x + w && my >= y && my < y + h;
            hud.draw_3slice_btn(c, x, y, w, h, txt, hov, en);
            add_btn(id, x, y, w, h, en);
        };

        if (modal == ModalId::OutcomePrompt) {
            bool won = (last_outcome == 1);
            int px = (W - 288) / 2, py = (H - 138) / 2;
            hud.draw_dialog_panel(c, px, py, 288, 138);
            hud.large.draw(c, won ? tr("Victory!", "Победа!") : tr("Defeat!", "Поражение!"), px + 16, py + 18, 256, 1, won ? 7 : 6, 0);
            if (won) {
                draw_sc_btn(600, px + 34, py + 80, 220, 28, tr("Continue", "Продолжить"));
            } else {
                draw_sc_btn(600, px + 34, py + 58, 220, 26, tr("End Mission", "Завершить миссию"));
                draw_sc_btn(601, px + 34, py + 92, 220, 26, active_mission_idx >= 0 ? tr("Restart Mission", "Начать заново") : tr("Continue Observing", "Наблюдать"));
            }
            return;
        }

        if (modal == ModalId::GameMenu) {
            int px = (W - 264) / 2, py = 64;
            hud.draw_dialog_panel(c, px, py, 264, 308);
            auto gm_lbl = [&](int id, const char* en, const char* ru) {
                if (const auto* w = hud.gamemenu_dlg.get(id)) {
                    std::string s = w->text;
                    if ((w->flags & 0x200u) && !s.empty()) s.erase(0, 1);
                    if (!s.empty()) return s;
                }
                return tr(en, ru);
            };
            hud.large.draw(c, gm_lbl(65535, "Game Menu", "Игровое меню"), px + 16, py + 14, 232, 1, 3, 0);
            draw_sc_btn(610, px + 24, py + 46,  216, 26, gm_lbl(1, "Save Game", "Сохранить игру"), !active_is_replay);
            draw_sc_btn(611, px + 24, py + 78,  216, 26, gm_lbl(2, "Load Game", "Загрузить игру"));
            draw_sc_btn(612, px + 24, py + 110, 216, 26, gm_lbl(3, "Game Options", "Параметры игры"));
            draw_sc_btn(613, px + 24, py + 142, 216, 26, gm_lbl(4, "StarCraft Tips", "Советы StarCraft"));
            draw_sc_btn(614, px + 24, py + 174, 216, 26, gm_lbl(5, "Mission Objectives", "Задачи миссии"));
            draw_sc_btn(615, px + 24, py + 206, 216, 26, gm_lbl(6, "End Mission", "Завершить миссию"));
            draw_sc_btn(616, px + 24, py + 258, 216, 28, gm_lbl(65533, "Return to Game (ESC)", "Вернуться в игру"));
            return;
        }

        if (modal == ModalId::AbortMenu) {
            int px = (W - 264) / 2, py = 116;
            hud.draw_dialog_panel(c, px, py, 264, 212);
            hud.large.draw(c, tr("End Mission", "Завершить миссию"), px + 16, py + 14, 232, 1, 3, 0);
            draw_sc_btn(620, px + 24, py + 50,  216, 28, tr("Restart Mission", "Начать заново"), active_mission_idx >= 0 || cur_map_ok);
            draw_sc_btn(621, px + 24, py + 86,  216, 28, tr("Quit to Menu", "Выйти в меню"));
            draw_sc_btn(622, px + 24, py + 122, 216, 28, tr("Exit Program", "Выход из игры"));
            draw_sc_btn(623, px + 24, py + 166, 216, 28, tr("Previous", "Назад"));
            return;
        }

        if (modal == ModalId::GameOptions) {
            int px = (W - 284) / 2, py = 84;
            hud.draw_dialog_panel(c, px, py, 284, 268);
            hud.large.draw(c, "Game Options", px + 16, py + 14, 252, 1, 3, 0);
            draw_sc_btn(630, px + 24, py + 48,  236, 28, "Speed (" + std::to_string(sim_fps) + " fps)");
            draw_sc_btn(631, px + 24, py + 84,  236, 28, "Sound & Music");
            draw_sc_btn(632, px + 24, py + 120, 236, 28, "Video & Handheld Display");
            draw_sc_btn(633, px + 24, py + 156, 236, 28, "Controls & Console Help");
            draw_sc_btn(634, px + 24, py + 192, 236, 28, "Single-Player Cheats");
            draw_sc_btn(635, px + 24, py + 230, 236, 28, "Previous");
            return;
        }

        if (modal == ModalId::OptSpeed) {
            int px = (W - 284) / 2, py = 96;
            hud.draw_dialog_panel(c, px, py, 284, 248);
            hud.large.draw(c, "Game Speed", px + 16, py + 14, 252, 1, 3, 0);
            draw_sc_btn(640, px + 24, py + 48,  236, 26, "Normal (18 fps)");
            draw_sc_btn(641, px + 24, py + 80,  236, 26, "Faster (21 fps)");
            draw_sc_btn(642, px + 24, py + 112, 236, 26, "Fastest - Standard (24 fps)");
            draw_sc_btn(643, px + 24, py + 144, 236, 26, "Turbo (32 fps)");
            draw_sc_btn(644, px + 24, py + 176, 236, 26, "Order Lines: " + std::string(sd_value(18, 0) ? "ON" : "OFF"));
            draw_sc_btn(645, px + 24, py + 210, 236, 26, "Previous");
            return;
        }

        if (modal == ModalId::OptSound) {
            int px = (W - 284) / 2, py = 104;
            hud.draw_dialog_panel(c, px, py, 284, 224);
            hud.large.draw(c, "Sound & Music", px + 16, py + 14, 252, 1, 3, 0);
            draw_sc_btn(650, px + 24, py + 50,  236, 28, "SFX Volume: " + std::to_string(int(sfx_vol * 100 + 0.5f)) + "%");
            draw_sc_btn(651, px + 24, py + 86,  236, 28, "Music Volume: " + std::to_string(int(mus_vol * 100 + 0.5f)) + "%");
            draw_sc_btn(652, px + 24, py + 122, 236, 28, "Next Music Track");
            draw_sc_btn(653, px + 24, py + 174, 236, 28, "Previous");
            return;
        }

        if (modal == ModalId::OptVideo) {
            int px = (W - 304) / 2, py = 96;
            hud.draw_dialog_panel(c, px, py, 304, 248);
            hud.large.draw(c, "Video & Handheld", px + 16, py + 14, 272, 1, 3, 0);
            char zbuf[64];
            std::snprintf(zbuf, sizeof(zbuf), "Battlefield Zoom: %.2fx", world_zoom);
            draw_sc_btn(660, px + 24, py + 48,  256, 28, zbuf);
            draw_sc_btn(661, px + 24, py + 84,  256, 28, std::string("3.5\" LCD (L2): ") + (handheld_1to1 ? "1:1 480x320" : "Full 640x480"));
            draw_sc_btn(662, px + 24, py + 120, 256, 28, std::string("Group Quick-Bar: ") + (hud.show_group_bar ? "ON" : "OFF"));
            draw_sc_btn(663, px + 24, py + 156, 256, 28, "Toggle HDMI Fullscreen (F11)");
            draw_sc_btn(664, px + 24, py + 204, 256, 28, "Previous");
            return;
        }

        if (modal == ModalId::OptControls) {
            int px = (W - 420) / 2, py = 66;
            hud.draw_dialog_panel(c, px, py, 420, 310);
            hud.large.draw(c, "Controls & Console Adaptation", px + 16, py + 14, 388, 1, 3, 0);
            const char* guide =
                "\x03Left Stick / Mouse\x02 : Move cursor (Hold \x03L1\x02 for precision)\n"
                "\x03Right Stick / D-Pad\x02 : Pan battlefield camera\n"
                "\x03A / Left Click\x02 : Select, Drag-Box, Double-Tap same units\n"
                "\x03B / Right Click\x02 : Smart Order (Move/Gather/Attack) or Cancel\n"
                "\x03X\x02 : Attack-Move ('A')   ·   \x03Y\x02 : Build / Stop\n"
                "\x03R1 + L1\x02 : Recall Groups 1/2 (Hold \x03R2\x02 + Group to Assign)\n"
                "\x03L2 / F2\x02 : Toggle 640x480 vs 1:1 480x320 LCD Zoom\n"
                "\x03SELECT\x02 : Jump to last alert   ·   \x03START / F10\x02 : Game Menu";
            hud.small.draw(c, guide, px + 20, py + 46, 380, 0, 2, 18);
            draw_sc_btn(670, px + 40, py + 226, 340, 28, hud.grid_hotkeys ? "Keyboard Layout: Grid (QWE/ASD/ZXC)" : "Keyboard Layout: Classic StarCraft");
            draw_sc_btn(671, px + 102, py + 266, 216, 28, "Previous");
            return;
        }

        if (modal == ModalId::OptCheats) {
            int px = (W - 310) / 2, py = 74;
            hud.draw_dialog_panel(c, px, py, 310, 290);
            hud.large.draw(c, "Single-Player Cheats", px + 16, py + 14, 278, 1, 3, 0);
            draw_sc_btn(680, px + 24, py + 48,  262, 26, "Show Me The Money (+10,000)");
            draw_sc_btn(681, px + 24, py + 80,  262, 26, "Black Sheep Wall (Reveal Map)");
            draw_sc_btn(682, px + 24, py + 112, 262, 26, "Operation CWAL (Instant Build)");
            draw_sc_btn(683, px + 24, py + 144, 262, 26, std::string("Power Overwhelming: ") + (sd_value(22, 0) ? "ON" : "OFF"));
            draw_sc_btn(684, px + 24, py + 176, 262, 26, std::string("Food For Thought: ") + (sd_value(24, 0) ? "ON" : "OFF"));
            draw_sc_btn(685, px + 24, py + 208, 262, 26, "There Is No Cow Level (Instant Win)");
            draw_sc_btn(686, px + 47, py + 248, 216, 28, "Previous");
            return;
        }

        if (modal == ModalId::GameTips) {
            int px = (W - 380) / 2, py = 104;
            hud.draw_dialog_panel(c, px, py, 380, 224);
            hud.large.draw(c, "StarCraft Tips", px + 16, py + 14, 348, 1, 3, 0);
            std::string tip = "Build additional workers early to maximize mineral and Vespene Gas harvesting.";
            if (!hud.tips_txt.empty()) {
                tip_idx = (tip_idx + int(hud.tips_txt.size())) % int(hud.tips_txt.size());
                tip = clean_ctrl(hud.tips_txt[size_t(tip_idx)]);
            }
            c.set_clip(px + 20, py + 48, px + 360, py + 172);
            hud.small.draw(c, tip, px + 22, py + 52, 336, 0, 2, 16);
            c.reset_clip();
            draw_sc_btn(690, px + 24,  py + 180, 156, 28, "Next Tip");
            draw_sc_btn(691, px + 200, py + 180, 156, 28, "Close");
            return;
        }

        if (modal == ModalId::GameObjectives) {
            int px = (W - 400) / 2, py = 86;
            hud.draw_dialog_panel(c, px, py, 400, 264);
            hud.large.draw(c, "Mission Objectives", px + 16, py + 14, 368, 1, 3, 0);
            std::string obj = !campaign.objectives.empty() ? campaign.objectives : "Eliminate all enemy structures.";
            c.set_clip(px + 20, py + 48, px + 380, py + 208);
            hud.small.draw(c, obj, px + 24, py + 52, 352, 0, 2, 16);
            c.reset_clip();
            draw_sc_btn(695, px + 92, py + 220, 216, 28, "Return to Game");
            return;
        }

        if (modal == ModalId::GameSave || modal == ModalId::GameLoad) {
            bool is_save = (modal == ModalId::GameSave);
            int px = (W - 340) / 2, py = 64;
            hud.draw_dialog_panel(c, px, py, 340, 320);
            hud.large.draw(c, is_save ? "Save Game" : "Load Game", px + 16, py + 14, 308, 1, 3, 0);
            for (int s = 0; s < 6; ++s) {
                std::string sname = (s == 0) ? "quicksave.sav" : ("slot_" + std::to_string(s) + ".sav");
                std::string full = save_dir + "/" + sname;
                std::ifstream chk(full, std::ios::binary);
                bool exists = chk.good();
                std::string lbl = std::to_string(s + 1) + ". " + sname + (exists ? "" : " (Empty)");
                draw_sc_btn(700 + s, px + 24, py + 46 + s * 34, 292, 28, lbl, is_save || exists);
            }
            draw_sc_btn(710, px + 62, py + 272, 216, 30, "Cancel");
            return;
        }
    }
};

} // namespace blizz
