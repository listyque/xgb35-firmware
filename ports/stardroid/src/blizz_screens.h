#pragma once
// Authentic StarCraft & Brood War Pre/Post-Game Screens & Modals (ported from StarDroid 1.9.11):
//   TitleSplash, MainMenu (glumain), ExpansionModal (glugamemode + retail_ex.pcx),
//   CampaignSelect (glucmpgn / gluexpcmpgn) + MissionHistoryModal (gluhist + phist.pcx),
//   StoryCrawl (rez/est*.txt), CinematicPlayer (smk/*.smk), BriefingRoom (glurdy{t,z,p}),
//   CustomGame (glucustm + live 128x128 CHK preview + 8-slot roster + Match Options),
//   LoadSaveReplay (gluload), ScoreScreen (gluscore + iscore.grp), and Classic F10 Menus.

#include "blizz_hud.h"
#include <fstream>
#include <set>

namespace blizz {

// --- Persistent Campaign Progress & User Preferences ---
struct ProgressStore {
    std::set<int> completed;
    std::set<int> unlocked;
    bool unlock_all = false;
    bool expansion = false;
    std::string path;

    void init_defaults() {
        // First playable mission of every SC & BW episode is unlocked out of the box (plus Tutorial 50)
        for (int c : {50, 1, 101, 201, 301, 401, 501}) unlocked.insert(c);
    }

    void load(const std::string& file_path) {
        path = file_path;
        completed.clear();
        unlocked.clear();
        init_defaults();
        std::ifstream f(path);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("unlock_all=", 0) == 0) unlock_all = (std::atoi(line.c_str() + 11) != 0);
            else if (line.rfind("expansion=", 0) == 0) expansion = (std::atoi(line.c_str() + 10) != 0);
            else if (line.rfind("done=", 0) == 0) {
                for (const auto& t : split_char(line.substr(5), ','))
                    if (!t.empty()) completed.insert(std::atoi(t.c_str()));
            } else if (line.rfind("open=", 0) == 0) {
                for (const auto& t : split_char(line.substr(5), ','))
                    if (!t.empty()) unlocked.insert(std::atoi(t.c_str()));
            }
        }
        for (int c : completed) {
            unlocked.insert(c);
            for (int i = 0; i < kMissionCount; ++i)
                if (kMissions[i].code == c && kMissions[i].next_code > 0)
                    unlocked.insert(kMissions[i].next_code);
        }
    }

    void save() const {
        if (path.empty()) return;
        std::ofstream f(path, std::ios::trunc);
        if (!f) return;
        f << "unlock_all=" << (unlock_all ? 1 : 0) << "\n";
        f << "expansion=" << (expansion ? 1 : 0) << "\n";
        f << "done=";
        bool first = true;
        for (int c : completed) { if (!first) f << ","; f << c; first = false; }
        f << "\nopen=";
        first = true;
        for (int c : unlocked) { if (!first) f << ","; f << c; first = false; }
        f << "\n";
    }

    bool is_available(int code) const {
        return unlock_all || unlocked.count(code) > 0 || completed.count(code) > 0;
    }

    void mark_completed(int code, int next_code) {
        if (code > 0) completed.insert(code);
        if (next_code > 0) unlocked.insert(next_code);
        save();
    }
};

// --- Episode Opening Story Crawl (rez/est*.txt, ported from StoryScript151 + StoryLayout) ---
struct StoryCrawl {
    struct Page {
        ImageRGBA bg;
        BlizzFont font;
        std::string text;
        bool lower = false;
        int duration_ms = 5000;
    };
    std::vector<Page> pages;
    int page_idx = 0;
    double page_time_ms = 0;

    void clear() { pages.clear(); page_idx = 0; page_time_ms = 0; }
    bool active() const { return page_idx >= 0 && page_idx < int(pages.size()); }

    bool open(const std::string& asset_path) {
        clear();
        if (asset_path.empty()) return false;
        auto raw = mpq_read(asset_path);
        if (raw.empty()) return false;
        std::string script(reinterpret_cast<const char*>(raw.data()), raw.size());

        std::string bg_path, tint_path, accum;
        bool lower = false;
        int dur = 5000;

        for (const auto& rline : split_char(script, '\n')) {
            std::string ln = rline;
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ' || ln.back() == '\t')) ln.pop_back();
            size_t ls = 0;
            while (ls < ln.size() && (ln[ls] == ' ' || ln[ls] == '\t')) ++ls;
            std::string trimmed = ln.substr(ls);

            if (trimmed.rfind("</", 0) != 0) {
                accum += rline;
                if (!accum.empty() && accum.back() == '\r') accum.pop_back();
                accum.push_back('\n');
                continue;
            }
            if (trimmed.back() != '>') continue;
            std::string tag = trimmed.substr(2, trimmed.size() - 3);
            size_t sp = 0;
            while (sp < tag.size() && !std::isspace(static_cast<unsigned char>(tag[sp]))) ++sp;
            std::string key = tag.substr(0, sp);
            for (char& ch : key) ch = char(std::toupper(static_cast<unsigned char>(ch)));
            std::string val = (sp < tag.size()) ? tag.substr(sp + 1) : "";
            while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) val.erase(0, 1);
            while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) val.pop_back();

            if (key == "BACKGROUND") bg_path = val;
            else if (key == "FONTCOLOR") tint_path = val;
            else if (key == "SCREENLOWERLEFT") lower = true;
            else if (key == "SCREENLEFT" || key == "SCREENCENTER" || key == "SCREENUPPERLEFT" ||
                     key == "SCREENRIGHT" || key == "SCREENTOP" || key == "SCREENBOTTOM") lower = false;
            else if (key == "DISPLAYTIME") dur = std::max(1500, std::min(60000, std::atoi(val.c_str())));
            else if (key == "PAGE") {
                while (!accum.empty() && std::isspace(static_cast<unsigned char>(accum.front()))) accum.erase(0, 1);
                while (!accum.empty() && std::isspace(static_cast<unsigned char>(accum.back()))) accum.pop_back();
                if (!accum.empty() && !bg_path.empty()) {
                    PCX bg_pcx, tint_pcx;
                    if (load_pcx(bg_path, bg_pcx, false, -1)) {
                        if (tint_path.empty() || !load_pcx(tint_path, tint_pcx, false, -1))
                            load_pcx("glue\\palmm\\tfont.pcx", tint_pcx, false, -1);
                        Page p;
                        p.bg = std::move(bg_pcx.image);
                        p.font.load("16", tint_pcx, bg_pcx.palette, 1);
                        p.font = p.font.with_shaded(pack_rgba(0x30, 0xE0, 0x50, 255));
                        p.text = accum;
                        p.lower = lower;
                        p.duration_ms = dur;
                        pages.push_back(std::move(p));
                    }
                }
                accum.clear();
            }
        }
        return !pages.empty();
    }

    void next_page() {
        ++page_idx;
        page_time_ms = 0;
    }

    // Returns true while still playing; false when finished all pages
    bool draw(Canvas& c, double dt_ms) {
        if (!active()) return false;
        Page& p = pages[size_t(page_idx)];
        page_time_ms += dt_ms;
        double total = double(p.duration_ms) + 800.0;
        if (page_time_ms >= total) {
            next_page();
            if (!active()) return false;
        }
        Page& cur = pages[size_t(page_idx)];
        double fade = std::min(1.0, std::min(page_time_ms / 400.0, (double(cur.duration_ms) + 800.0 - page_time_ms) / 400.0));
        uint8_t alpha = uint8_t(std::max(0.0, std::min(255.0, fade * 255.0)));

        c.fill_rect(0, 0, W, H, pack_rgba(0, 0, 0));
        c.blit_img(cur.bg, 0, 0, 1.0f, alpha);

        int lh = std::max(22, cur.font.height + 4);
        auto lines = cur.font.wrap_lines(cur.text, 576);
        int th = int(lines.size()) * lh;
        int top = cur.lower ? std::max(32, 448 - th) : 52;
        cur.font.draw(c, cur.text, 32, top, 576, 0, 2, lh, alpha);
        return true;
    }
};

// --- Fullscreen Smacker Cinematic Player (streamed from disk in < 2 MB RAM) ---
struct CinematicPlayer {
    int movie_id = -1;
    int mw = 0, mh = 0, frames = 0, interval_ms = 67, duration_ms = 0;
    double elapsed_ms = 0;
    std::string title;

    void close() {
        if (movie_id >= 0) { sd_movie_close(movie_id); movie_id = -1; }
        elapsed_ms = 0;
    }
    bool active() const { return movie_id >= 0; }

    bool open(const std::string& smk_path, const std::string& caption = "") {
        close();
        if (smk_path.empty()) return false;
        int info[5] = {};
        std::string np = norm_path(smk_path);
        movie_id = sd_movie_open_asset(-1, np.c_str(), true, info);
        if (movie_id < 0) return false;
        mw = info[0]; mh = info[1];
        frames = std::max(1, info[2]);
        interval_ms = std::max(15, info[3]);
        duration_ms = std::max(interval_ms, info[4]);
        elapsed_ms = 0;
        title = caption;
        return true;
    }

    bool draw(Canvas& c, double dt_ms) {
        if (movie_id < 0) return false;
        elapsed_ms += dt_ms;
        if (elapsed_ms >= double(duration_ms) + 120.0) {
            close();
            return false;
        }
        c.fill_rect(0, 0, W, H, pack_rgba(0, 0, 0));
        int fw = 0, fh = 0;
        if (const uint8_t* fr = sd_movie_frame(movie_id, elapsed_ms, false, &fw, &fh)) {
            if (fw > 0 && fh > 0) {
                // Fit aspect ratio cleanly inside 640x480 (classic 640x240 / 320x200 SMK letterboxed)
                int dw = W;
                int dh = std::min(H, (fh * W) / std::max(1, fw));
                // Original 640x240 interlaced/widescreen Blizzard cinematics scale 2x vertically or 1.5x cleanly
                if (fw >= 600 && fh <= 260) dh = std::min(H, fh * 3 / 2);
                int dy = (H - dh) / 2;
                c.blit_scaled(reinterpret_cast<const uint32_t*>(fr), fw, fh, 0, dy, dw, dh);
            }
        }
        return true;
    }
};

// --- NativeResults: Authentic Post-Match Score Screen (rez/gluscore.bin + iscore.grp) ---
struct ScoreScreen {
    static constexpr double COUNT_MS = 2200.0;

    BlizzDialog layout;
    PCX bg, tint, main_pan, inset_pan, scorebox_pan, untab_pan;
    BlizzFont tiny, small, medium, large, title_fnt;
    ImageRGBA bar_parts[12][6];
    int outcome = 1, race = 1, elapsed_sec = 0;
    bool won = true, can_save_replay = true, exiting = false, sound_started = false, ru_locale = false;
    double anim_ms = 0, count_ms = 0, progress = 0;
    int tab = 0, hot_id = -1;

    std::string tr(const char* en, const char* ru) const {
        return ru_locale ? utf8_to_cp1251(ru) : std::string(en);
    }

    struct Row {
        int f[19] = {};
        std::string faction;
        int cpu_diff = -1;
    };
    std::vector<Row> rows;

    static void apply_results_glass(PCX& pan, int glass_idx, const uint32_t expected_pal[256]) {
        if (!pan.image.valid() || glass_idx <= 0 || glass_idx >= 256) return;
        bool same_pal = (std::memcmp(pan.palette, expected_pal, 256 * sizeof(uint32_t)) == 0);
        uint32_t gc = pan.palette[glass_idx];
        bool is_dark = (px_r(gc) <= 24 && px_g(gc) <= 24 && px_b(gc) <= 24);
        if (!same_pal || !is_dark) return;
        for (int y = 0; y < pan.height; ++y) {
            for (int x = 0; x < pan.width; ++x) {
                if (pan.pixels[size_t(y) * pan.stride + x] == glass_idx) {
                    uint32_t& px = pan.image.px[size_t(y) * pan.width + x];
                    if (px_a(px) > 80) px = (px & 0x00FFFFFFu) | (80u << 24);
                }
            }
        }
    }

    bool open(int theme_race, int match_outcome, const std::string& stats_raw, bool replay_avail) {
        race = std::max(0, std::min(2, theme_race));
        outcome = match_outcome;
        won = (outcome == 1);
        can_save_replay = replay_avail;
        exiting = false;
        sound_started = false;
        anim_ms = count_ms = progress = 0;
        tab = 0; hot_id = -1;
        rows.clear();

        static const char* kRL[3] = {"z", "t", "p"};
        std::string key  = std::string(kRL[race]) + (won ? "v" : "d");
        std::string pal  = "glue\\pal" + key + "\\";
        std::string pref = "glue\\score" + key + "\\";

        if (!layout.load("rez\\gluscore.bin")) return false;
        ru_locale = false;
        for (const auto& w : layout.widgets) if (has_cp1251_cyrillic(w.text)) { ru_locale = true; break; }
        if (!ru_locale) {
            auto st = parse_tbl(mpq_read("rez\\stat_txt.tbl"));
            if (!st.empty() && has_cp1251_cyrillic(st[0])) ru_locale = true;
        }
        if (!load_pcx(pal + "backgnd.pcx", bg, false, -1) ||
            !load_pcx(pal + "tfont.pcx", tint, false, -1))
            return false;

        PCX teff;
        int glass = 138;
        if (load_pcx(pal + "teffect.pcx", teff, false, -1) && teff.pixels.size() > 1)
            glass = teff.pixels[1];

        load_pcx(pref + "pmain.pcx", main_pan, true, -1);
        apply_results_glass(main_pan, glass, bg.palette);
        strip_edge_backdrop(main_pan.image, bg.image, 0, 0);

        load_pcx(pref + "pinset.pcx", inset_pan, true, -1);
        apply_results_glass(inset_pan, glass, bg.palette);

        load_pcx(pref + "scorebox.pcx", scorebox_pan, true, -1);
        load_pcx(pref + "untab.pcx", untab_pan, true, -1);

        auto bars = mpq_read(pref + "iscore.grp");
        for (int col = 0; col < 12; ++col)
            for (int pt = 0; pt < 6; ++pt)
                bar_parts[col][pt] = decode_grp_frame(bars, 6 + col * 6 + pt, bg.palette, true, false);

        tiny.load("8", tint, bg.palette, 1);
        small.load("10", tint, bg.palette, 1);
        medium.load("14", tint, bg.palette, 1);
        large.load("16", tint, bg.palette, 1);
        title_fnt.load("16x", tint, bg.palette, 1);

        // Place Save Replay (61) beside OK (7) in footer & normalize Elapsed Time (9)
        if (auto* ok = layout.get(7)) {
            if (auto* sv = layout.get(61)) {
                sv->w = std::min(210, std::max(1, ok->x - 10));
                sv->x = ok->x - 10 - sv->w;
                sv->y = ok->y; sv->h = ok->h;
                sv->rx = sv->ry = 0; sv->rx2 = sv->w - 1; sv->ry2 = sv->h - 1;
            }
        }
        if (auto* tm = layout.get(9)) { tm->x = 36; tm->y = 438; tm->w = 206; tm->h = 28; }

        auto lines = split_char(stats_raw, '\n');
        if (!lines.empty()) {
            auto h0 = split_char(lines[0], '\t');
            elapsed_sec = std::max(0, fld_int(h0, 1, 0) / 24);
        }
        for (size_t i = 1; i < lines.size() && rows.size() < 8; ++i) {
            auto f = split_char(lines[i], '\t');
            if (f.size() < 16) continue;
            Row r;
            for (int j = 0; j < 19; ++j) r.f[j] = fld_int(f, size_t(j), 0);
            if (f.size() > 19) r.faction = clean_ctrl(f[19]);
            int df = fld_int(f, 20, -1);
            r.cpu_diff = (df >= 0 && df < 4) ? df : -1;
            rows.push_back(std::move(r));
        }
        return true;
    }

    void vals_for_tab(const Row& r, int out[3]) const {
        switch (tab) {
        case 1: out[0] = r.f[3];  out[1] = r.f[8];  out[2] = r.f[9]; break;
        case 2: out[0] = r.f[4];  out[1] = r.f[10]; out[2] = r.f[11]; break;
        case 3: out[0] = r.f[5];  out[1] = r.f[6];  out[2] = r.f[14] + r.f[15]; break;
        default: out[0] = r.f[12]; out[1] = r.f[13]; out[2] = r.f[5] + r.f[6]; break;
        }
    }

    void draw_3bar(Canvas& c, const ImageRGBA parts[6], int first, int x, int y, int w) {
        const auto& l = parts[first];
        const auto& m = parts[first + 1];
        const auto& r = parts[first + 2];
        if (!l.valid() || !m.valid() || !r.valid() || w <= 0) return;
        c.blit_img(l, x, y);
        if (w > l.w + r.w)
            c.blit_scaled(m.px.data(), m.w, m.h, x + l.w, y, w - l.w - r.w, m.h);
        c.blit_img(r, x + w - r.w, y);
    }

    void draw_score_bar(Canvas& c, int color, int x, int y, int w, float frac) {
        const auto* parts = bar_parts[std::max(0, std::min(11, color))];
        draw_3bar(c, parts, 3, x, y, w);
        if (frac <= 0.0f) return;
        int fw = int(float(w - 2) * std::min(1.0f, frac));
        if (fw <= 0) return;
        int prev_cx0 = c.cx0, prev_cy0 = c.cy0, prev_cx1 = c.cx1, prev_cy1 = c.cy1;
        c.set_clip(x + 1, y + 2, x + 1 + fw, y + 16);
        draw_3bar(c, parts, 0, x + 2, y + 2, w - 4);
        c.cx0 = prev_cx0; c.cy0 = prev_cy0; c.cx1 = prev_cx1; c.cy1 = prev_cy1;
    }

    void update_hover(int mx, int my) {
        hot_id = -1;
        if (exiting || anim_ms < BlizzGlue::ENTER_MS) return;
        for (int id : {3, 4, 5, 6, 7, 61}) {
            if (id == 61 && !can_save_replay) continue;
            if (const auto* w = layout.get(id))
                if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h)
                    hot_id = id;
        }
    }

    // Returns clicked widget id (3..6 = tab, 7 = OK, 61 = Save Replay) or -1
    int on_click(int mx, int my) {
        if (exiting) return -1;
        if (anim_ms < BlizzGlue::ENTER_MS) { anim_ms = BlizzGlue::ENTER_MS; return -1; }
        if (progress < 1.0) { count_ms = COUNT_MS; progress = 1.0; return -1; }
        update_hover(mx, my);
        if (hot_id >= 3 && hot_id <= 6) {
            sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);
            tab = hot_id - 3;
            return hot_id;
        }
        if (hot_id == 7 || (hot_id == 61 && can_save_replay)) {
            sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);
            return hot_id;
        }
        return -1;
    }

    void draw(Canvas& c, double dt_ms) {
        anim_ms += std::max(0.0, dt_ms);
        double p = std::min(1.0, anim_ms / (exiting ? BlizzGlue::EXIT_MS : BlizzGlue::ENTER_MS));
        int sx = -int(std::lround(640.0 * (exiting ? (1.0 - std::pow(1.0 - p, 3.0)) : std::pow(1.0 - p, 3.0))));

        if (!exiting && anim_ms >= BlizzGlue::ENTER_MS) {
            if (!sound_started) {
                sound_started = true;
                sd_play_mpq_sound(-1, "sound\\glue\\swishlock.wav", 60, false);
                sd_play_score_sound(int(COUNT_MS));
            }
            count_ms += std::max(0.0, dt_ms);
            progress = std::min(1.0, count_ms / COUNT_MS);
        }

        c.reset_clip();
        c.blit_img(bg.image, 0, 0);
        c.blit_img(main_pan.image, sx, 0);

        for (int i = 0; i < 4; ++i) {
            if (i == tab) continue;
            if (const auto* w = layout.get(3 + i)) {
                int th = std::min(w->h, untab_pan.height);
                c.blit_scaled(untab_pan.image.px.data(), untab_pan.width, untab_pan.height,
                              w->x + sx, w->y + w->h - th, w->w, th);
            }
        }
        for (size_t i = 0; i < rows.size(); ++i) {
            if (const auto* iw = layout.get(13 + int(i) * 6))
                c.blit_scaled(inset_pan.image.px.data(), inset_pan.width, inset_pan.height, iw->x + sx, iw->y, iw->w, iw->h);
            if (const auto* sw = layout.get(18 + int(i) * 6))
                c.blit_scaled(scorebox_pan.image.px.data(), scorebox_pan.width, scorebox_pan.height, sw->x + sx, sw->y, sw->w, sw->h);
        }

        auto draw_wtxt = [&](int id, const std::string& txt, BlizzFont& fnt, int col, int align) {
            if (const auto* w = layout.get(id)) {
                c.set_clip(w->x + sx, w->y, w->x + sx + w->w, w->y + w->h);
                fnt.draw(c, txt, w->x + sx, w->y + std::max(0, (w->h - fnt.height) / 2), w->w, align, col, 0);
                c.reset_clip();
            }
        };

        draw_wtxt(2, won ? tr("Victory!", "Победа!") : tr("Defeat!", "Поражение!"), title_fnt, won ? 4 : 2, 1);

        const std::string kTabs[4] = {
            tr("Overview", "Итоги"), tr("Units", "Войска"),
            tr("Structures", "Строения"), tr("Resources", "Ресурсы")
        };
        for (int i = 0; i < 4; ++i)
            draw_wtxt(3 + i, kTabs[i], small, (i == tab || hot_id == 3 + i) ? 4 : 2, 1);

        const std::string kHeads[4][3] = {
            {tr("Units", "Войска"), tr("Structures", "Строения"), tr("Resources", "Ресурсы")},
            {tr("Produced", "Создано"), tr("Killed", "Убито"), tr("Lost", "Потеряно")},
            {tr("Constructed", "Построено"), tr("Razed", "Разрушено"), tr("Lost", "Потеряно")},
            {tr("Minerals", "Минералы"), tr("Vespene Gas", "Веспен"), tr("Spent", "Потрачено")}
        };
        for (int i = 0; i < 3; ++i) draw_wtxt(10 + i, kHeads[tab][i], small, 2, 1);
        draw_wtxt(8, tr("Total Score", "Общий счёт"), title_fnt, 2, 0);

        int max_val = 1;
        for (const auto& r : rows) {
            int v[3]; vals_for_tab(r, v);
            for (int k = 0; k < 3; ++k) max_val = std::max(max_val, v[k]);
        }

        static const char* kRaceNames[3] = {"Zerg", "Terran", "Protoss"};
        static const char* kDiffNames[4] = {"Easy", "Normal", "Hard", "Original"};
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            int base = 13 + int(i) * 6;
            if (const auto* nw = layout.get(base + 1)) {
                const auto* s1 = layout.get(base + 2);
                int maxw = std::max(1, std::min(nw->w, s1 ? (s1->x - nw->x - 8) : 144));
                std::string pname = (r.f[0] != 0) ? "Commander" : (!r.faction.empty() ? r.faction : ("Computer " + std::to_string(i)));
                BlizzFont& pf = (large.measure(pname) <= maxw) ? large : ((medium.measure(pname) <= maxw) ? medium : small);
                c.set_clip(nw->x + sx, nw->y, nw->x + sx + maxw, nw->y + 34);
                pf.draw(c, pname, nw->x + sx, nw->y + std::max(0, (19 - pf.height) / 2), maxw, 0, r.f[0] != 0 ? 4 : 2, 0);
                std::string sub = (r.f[0] != 0 && !r.faction.empty()) ? r.faction : kRaceNames[std::max(0, std::min(2, r.f[1]))];
                if (r.cpu_diff >= 0) sub += std::string(" (") + kDiffNames[r.cpu_diff] + ")";
                tiny.draw(c, sub, nw->x + sx, nw->y + 19, maxw, 0, 2, 0);
                c.reset_clip();
            }
            int v[3]; vals_for_tab(r, v);
            for (int j = 0; j < 3; ++j) {
                draw_wtxt(base + 2 + j, std::to_string(int(double(v[j]) * progress)), small, 2, 1);
                if (const auto* w = layout.get(base + 2 + j)) {
                    float frac = float(double(std::max(0, v[j])) / double(max_val) * progress);
                    draw_score_bar(c, r.f[2], w->x + sx, w->y + 17, w->w, frac);
                }
            }
            long long tot = (long long)(r.f[12]) + r.f[13] + r.f[5] + r.f[6];
            draw_wtxt(base + 5, std::to_string((long long)(double(tot) * progress)), large, 2, 1);
        }

        char tbuf[64];
        std::snprintf(tbuf, sizeof(tbuf), "%s: %d:%02d", tr("Elapsed Time", "Время").c_str(), elapsed_sec / 60, elapsed_sec % 60);
        draw_wtxt(9, tbuf, large, 2, 0);

        draw_wtxt(7, tr("OK", "ОК"), title_fnt, hot_id == 7 ? 4 : 3, 1);
        draw_wtxt(61, tr("Save Replay", "Сохранить запись"), title_fnt, can_save_replay ? (hot_id == 61 ? 4 : 3) : 5, 1);
    }
};

} // namespace blizz
