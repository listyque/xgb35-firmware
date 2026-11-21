#pragma once
// Authentic StarCraft & Brood War In-Game Race Console (NativeHud),
// Campaign Briefing & Runtime Transmission Runner (NativeCampaign),
// and Classic F10 In-Game Menu (gamemenu / options / abrtmenu / tips / objectives).

#include "blizz_art.h"
#include "missions_table.h"

namespace blizz {

// --- Utility string splitters ---
inline std::vector<std::string> split_char(const std::string& s, char delim) {
    std::vector<std::string> out;
    size_t st = 0;
    for (;;) {
        size_t p = s.find(delim, st);
        if (p == std::string::npos) { out.push_back(s.substr(st)); break; }
        out.push_back(s.substr(st, p - st));
        st = p + 1;
    }
    return out;
}

inline int fld_int(const std::vector<std::string>& f, size_t idx, int def = 0) {
    if (idx >= f.size() || f[idx].empty()) return def;
    char* end = nullptr;
    long v = std::strtol(f[idx].c_str(), &end, 10);
    return (end == f[idx].c_str()) ? def : int(v);
}

// --- BriefingText: Weighted continuous speech-synced scrolling (from BriefingText.java) ---
namespace briefing_scroll {
    inline double line_weight(const std::string& line) {
        double r = 1.0;
        std::string cl = clean_ctrl(line);
        for (unsigned char ch : cl) {
            if (std::isalnum(ch)) r += 1.0;
            else if (std::strchr(".!?;:", ch)) r += 5.0;
            else if (ch == ',') r += 2.0;
            else r += 0.4;
        }
        return r;
    }
    inline float compute(const std::vector<std::string>& lines, double pos_ms, double dur_ms, int box_h) {
        float dist = float(std::max(0, int(lines.size()) * 19 - box_h));
        if (dist <= 0.0f || dur_ms <= 0.0 || pos_ms <= 0.0) return 0.0f;
        double total = 0.0;
        for (const auto& l : lines) total += line_weight(l);
        int vis = std::max(1, box_h / 19);
        double first_scr = 0.0;
        for (int i = 0; i < std::min(vis, int(lines.size())); ++i) first_scr += line_weight(lines[size_t(i)]);
        double start = std::max(0.0, first_scr / std::max(1.0, total) * dur_ms - 900.0);
        double end   = std::max(start + 1.0, dur_ms - 500.0);
        double t = std::max(0.0, std::min(1.0, (pos_ms - start) / (end - start)));
        return float(dist * t);
    }
} // namespace briefing_scroll

// --- NativeCampaign: CHK MBRF Briefing Script + In-Game Transmissions & Objectives ---
struct CampaignRunner {
    struct Action {
        int type = 0, text = 0, sound = 0, duration = 0, slot = 0, adjustment = 0, unit = 0, modifier = 0;
    };

    std::vector<uint8_t> str_sec;
    bool is_strx = false;
    std::unordered_map<int, int> unit_custom_names; // UNIS / UNIx unit_id -> str_id
    std::vector<Action> actions;

    std::string objectives, message, next_scenario;
    int index = 0, talking = -1;
    int portraits[4] = {-1, -1, -1, -1};
    double remaining = 0, text_remaining = 0, message_duration = 0, message_until = 0;
    double trans_pos = 0, trans_dur = 0;
    double brief_talk_ms[4] = {};
    int msg_revision = 0, spoken_len = 0, mission_voice = 0, mission_text = 0;
    bool transmission = false, portrait_speaking = false, active = false;

    static constexpr int MAX_MSG_LINES = 11;
    std::vector<std::string> messages;
    std::vector<double> message_ends;
    std::deque<Action> pending_speech;

    void clear() {
        str_sec.clear(); is_strx = false; unit_custom_names.clear(); actions.clear();
        objectives.clear(); message.clear(); next_scenario.clear();
        index = 0; talking = -1;
        for (int i = 0; i < 4; ++i) { portraits[i] = -1; brief_talk_ms[i] = 0; }
        remaining = text_remaining = message_duration = message_until = trans_pos = trans_dur = 0;
        msg_revision = spoken_len = mission_voice = mission_text = 0;
        transmission = portrait_speaking = active = false;
        messages.clear(); message_ends.clear(); pending_speech.clear();
        sd_sound_stop(62);
    }

    bool load_chk(const uint8_t* chk, size_t len, int player) {
        clear();
        if (!chk || len < 8) return false;
        player = std::max(0, std::min(11, player));

        const uint8_t* forc = nullptr; size_t forc_len = 0;
        std::vector<std::pair<const uint8_t*, size_t>> mbrfs;

        for (size_t p = 0; p + 8 <= len;) {
            uint32_t slen = u32_le(chk + p + 4);
            if (slen > len - p - 8) break;
            const uint8_t* body = chk + p + 8;
            if (!std::memcmp(chk + p, "FORC", 4)) { forc = body; forc_len = slen; }
            else if (!std::memcmp(chk + p, "MBRF", 4)) mbrfs.emplace_back(body, slen);
            else if (!std::memcmp(chk + p, "STRx", 4)) { str_sec.assign(body, body + slen); is_strx = true; }
            else if (!std::memcmp(chk + p, "STR ", 4) && !is_strx) { str_sec.assign(body, body + slen); }
            p += 8 + size_t(slen);
        }

        int force = (forc && player < 8 && size_t(player) < forc_len) ? forc[player] : -1;
        for (const auto& mb : mbrfs) {
            for (size_t t = 0; t + 2400 <= mb.second; t += 2400) {
                const uint8_t* tb = mb.first + t;
                bool owned = (tb[2372 + player] != 0) || (tb[2372 + 17] != 0) ||
                             (force >= 0 && force < 4 && tb[2372 + 18 + force] != 0);
                if (!owned) continue;
                for (size_t aoff = 320; aoff + 32 <= 2368; aoff += 32) {
                    const uint8_t* ab = tb + aoff;
                    int type = ab[26];
                    if (type == 0) break;
                    if ((ab[28] & 2) != 0) continue; // disabled action
                    Action a;
                    a.type       = type;
                    a.text       = int(u32_le(ab + 4));
                    a.sound      = int(u32_le(ab + 8));
                    a.duration   = int(std::min<uint32_t>(0x7FFFFFFFu, u32_le(ab + 12)));
                    a.slot       = int(u32_le(ab + 16));
                    a.adjustment = int(std::min<uint32_t>(0x7FFFFFFFu, u32_le(ab + 20)));
                    a.unit       = u16_le(ab + 24);
                    a.modifier   = ab[27];
                    actions.push_back(a);
                    if (type == 4) objectives = text(a.text);
                }
            }
        }
        active = true;
        return !actions.empty();
    }

    std::string text(int id) const {
        if (str_sec.empty() || id <= 0) return "";
        if (is_strx) {
            if (str_sec.size() < 4) return "";
            uint32_t cnt = u32_le(str_sec.data());
            if (uint32_t(id) > cnt || 4 + size_t(id) * 4 > str_sec.size()) return "";
            return cstr_at(str_sec.data(), str_sec.size(), u32_le(str_sec.data() + 4 + size_t(id - 1) * 4));
        } else {
            uint16_t wid = uint16_t(id & 0xFFFF);
            if (wid == 0 || str_sec.size() < 2) return "";
            uint16_t cnt = u16_le(str_sec.data());
            if (wid > cnt || 2 + size_t(wid) * 2 > str_sec.size()) return "";
            return cstr_at(str_sec.data(), str_sec.size(), u16_le(str_sec.data() + 2 + size_t(wid - 1) * 2));
        }
    }

    void show_text(const std::string& val, double dur) {
        message = val;
        message_duration = dur;
        ++msg_revision;
    }

    void replay_briefing() {
        index = 0; remaining = 0; talking = -1;
        for (int i = 0; i < 4; ++i) { portraits[i] = -1; brief_talk_ms[i] = 0; }
        show_text("", 0);
        text_remaining = 0;
        transmission = portrait_speaking = false;
        trans_pos = trans_dur = 0;
        sd_sound_stop(62);
    }

    bool briefing_speaking(int slot) const {
        return slot >= 0 && slot < 4 &&
               (brief_talk_ms[slot] > 0.0 || (transmission && talking == slot && remaining > 0.0));
    }

    void tick_briefing(double dt_ms) {
        for (int s = 0; s < 4; ++s) brief_talk_ms[s] = std::max(0.0, brief_talk_ms[s] - dt_ms);
        if (text_remaining > 0.0) {
            text_remaining -= dt_ms;
            if (text_remaining <= 0.0) show_text("", 0);
        }
        if (transmission) {
            int pos = sd_sound_pos_ms(62);
            trans_pos = (pos >= 0) ? double(pos) : std::max(double(spoken_len), trans_pos + dt_ms);
            remaining = trans_dur - trans_pos;
        } else {
            remaining -= dt_ms;
        }
        if (remaining > 0.0) return;
        if (transmission) {
            sd_sound_stop(62);
            show_text("", 0);
        }
        transmission = false;
        talking = -1;

        for (int guard = 0; guard < 256 && index < int(actions.size()); ++guard) {
            const Action a = actions[size_t(index++)];
            int slot = std::max(0, std::min(3, a.slot));
            switch (a.type) {
            case 1: remaining = a.duration; return;
            case 2:
                if (sd_sound_playing(62)) { --index; return; }
                sd_play_campaign_voice(a.sound);
                break;
            case 3:
                show_text(text(a.text), a.duration);
                text_remaining = a.duration;
                break;
            case 4:
                objectives = text(a.text);
                break;
            case 5:
                portraits[slot] = a.unit;
                break;
            case 6:
                portraits[slot] = -1;
                brief_talk_ms[slot] = 0;
                break;
            case 7:
                talking = slot;
                brief_talk_ms[slot] = a.duration;
                break;
            case 8: {
                if (sd_sound_playing(62)) { --index; return; }
                talking = slot;
                text_remaining = 0;
                int dur = (a.sound == 0) ? 0 : sd_play_campaign_voice(a.sound);
                int base = (dur > 0) ? dur : a.duration;
                double rem = base;
                if (a.modifier == 7) rem = a.adjustment;
                else if (a.modifier == 8) rem += a.adjustment;
                else if (a.modifier == 9) rem -= a.adjustment;
                rem = std::max(1.0, std::max(double(base), rem));
                remaining = rem;
                show_text(text(a.text), rem);
                transmission = true;
                spoken_len = dur;
                trans_dur = rem;
                trans_pos = 0;
                return;
            }
            default: break;
            }
        }
    }

    void enter_mission() {
        index = int(actions.size());
        remaining = text_remaining = message_until = 0;
        transmission = portrait_speaking = false;
        trans_pos = trans_dur = 0;
        talking = -1;
        for (int i = 0; i < 4; ++i) { portraits[i] = -1; brief_talk_ms[i] = 0; }
        mission_voice = mission_text = 0;
        pending_speech.clear();
        messages.clear();
        message_ends.clear();
        show_text("", 0);
        sd_sound_stop(62);
    }

    static std::string msg_lines_norm(const std::string& v) {
        std::string out;
        out.reserve(v.size());
        for (char c : v) {
            if (c == '\r') continue;
            out.push_back(c == '\f' ? '\n' : c);
        }
        return out;
    }

    static int display_text_duration(const std::string& v) {
        return 4000 + std::max(0, int(v.size()) - 80) * 50;
    }

    bool is_rescue_cue(int snd_id) const {
        if (snd_id == 0) return false;
        std::string p = norm_path(text(snd_id));
        size_t sl = p.find_last_of('\\');
        std::string fn = (sl == std::string::npos) ? p : p.substr(sl + 1);
        return fn == "trescue.wav" || fn == "zrescue.wav" || fn == "prescue.wav";
    }

    void append_msg(const std::string& v, double end_frame) {
        auto lines = split_char(msg_lines_norm(v), '\n');
        size_t from = (lines.size() > MAX_MSG_LINES) ? (lines.size() - MAX_MSG_LINES) : 0;
        for (size_t i = from; i < lines.size(); ++i) {
            messages.push_back(lines[i]);
            message_ends.push_back(end_frame);
            if (int(messages.size()) > MAX_MSG_LINES) {
                messages.erase(messages.begin());
                message_ends.erase(message_ends.begin());
            }
        }
    }

    void present_speech(const Action& a, int frame) {
        portrait_speaking = false;
        int len_ms = 0;
        if (a.sound != 0) {
            mission_voice = a.sound;
            mission_text = a.text;
            len_ms = sd_play_campaign_voice(a.sound);
        }
        portraits[0] = a.unit;
        talking = 0;
        int dur = std::max(len_ms, std::max(0, a.duration));
        std::string val = text(a.text);
        if (!val.empty())
            append_msg(val, frame + std::max(dur, display_text_duration(val)) / 42.0);
        message_until = frame + std::max(dur, 3000) / 42.0;
    }

    void poll_mission() {
        if (!active) return;
        // Refresh scenario CHK string table from live engine if needed (e.g. UMS maps)
        if (str_sec.empty()) {
            size_t clen = 0;
            if (const uint8_t* cptr = sd_campaign_chk(&clen))
                load_chk(cptr, clen, 0);
        }
        int frame = sd_value(8, 0);
        const char* raw_ev = sd_text(5);
        if (raw_ev && *raw_ev) {
            for (const auto& row : split_char(raw_ev, '\n')) {
                auto a = split_char(row, '\t');
                if (a.size() < 5) continue;
                int type = std::atoi(a[0].c_str());
                int id   = int(std::strtoul(a[1].c_str(), nullptr, 10));
                int wav  = int(std::strtoul(a[2].c_str(), nullptr, 10));
                int dur  = std::max(0, int(std::strtol(a[3].c_str(), nullptr, 10)));
                int unit = std::atoi(a[4].c_str());

                if (type == 216) {
                    pending_speech.clear(); messages.clear(); message_ends.clear();
                    mission_voice = mission_text = 0; talking = -1; portrait_speaking = false;
                    message_until = 0; portraits[0] = -1; message.clear();
                    sd_sound_stop(62);
                    continue;
                }
                if (type == 41) next_scenario = norm_path(text(id));
                if (type == 12) objectives = text(id);
                if (type == 8)  sd_play_campaign_effect(wav);
                if (type == 7) {
                    Action sp{};
                    sp.text = id; sp.sound = wav; sp.duration = dur; sp.unit = unit;
                    if (is_rescue_cue(wav)) {
                        if (!sd_sound_playing(62) && pending_speech.empty()) present_speech(sp, frame);
                        else if (id != 0) {
                            std::string v = text(id);
                            if (!v.empty()) append_msg(v, frame + display_text_duration(v) / 42.0);
                        }
                        continue;
                    }
                    if (wav != 0 && (sd_sound_playing(62) || !pending_speech.empty())) {
                        if (pending_speech.size() < 128) pending_speech.push_back(sp);
                    } else {
                        present_speech(sp, frame);
                    }
                }
                if (type == 9) {
                    std::string v = text(id);
                    if (!v.empty()) append_msg(v, frame + display_text_duration(v) / 42.0);
                }
                if (type == 29) {
                    portraits[0] = unit;
                    talking = (unit >= 0 && unit < 228) ? 0 : -1;
                    portrait_speaking = (talking >= 0);
                    message_until = frame + dur / 42.0;
                }
            }
        }
        if (!sd_sound_playing(62) && !pending_speech.empty()) {
            Action nxt = pending_speech.front();
            pending_speech.pop_front();
            present_speech(nxt, frame);
        }
        std::string spk_txt = msg_lines_norm(text(mission_text));
        for (int i = int(messages.size()) - 1; i >= 0; --i) {
            if (frame > message_ends[size_t(i)]) {
                if (sd_sound_playing(62) && !messages[size_t(i)].empty() &&
                    spk_txt.find(messages[size_t(i)]) != std::string::npos)
                    continue;
                messages.erase(messages.begin() + i);
                message_ends.erase(message_ends.begin() + i);
            }
        }
        message.clear();
        for (size_t i = 0; i < messages.size(); ++i) {
            if (i) message.push_back('\n');
            message += messages[i];
        }
        if (talking >= 0 && frame > message_until && !sd_sound_playing(62)) {
            talking = -1;
            portrait_speaking = false;
        }
    }
};

// --- Command Card Slot Tables (exact compile-time port of commands.json + CommandSlots.java) ---
namespace cmd_tables {
    inline int order_icon(const std::string& label) {
        static const std::pair<const char*, int> kTbl[] = {
            {"Move", 228}, {"Stop", 229}, {"Attack", 230}, {"Gather", 231}, {"Repair", 232},
            {"Return Cargo", 233}, {"Build", 234}, {"Advanced", 235}, {"Cancel", 236},
            {"Back", 236}, {"Halt Construction", 236}, {"Patrol", 254}, {"Hold", 255},
            {"Hold Position", 255}, {"Set Rally Point", 286}, {"Lift Off", 282}, {"Land", 283},
            {"Burrow", 259}, {"Unburrow", 260}, {"Stim Pack", 237}, {"Siege Mode", 245},
            {"Tank Mode", 246}, {"Load", 309}, {"Unload", 312}, {"View Exit", 286}
        };
        for (const auto& p : kTbl) if (label == p.first) return p.second;
        return -1;
    }

    inline int label_slot(const std::string& label) {
        static const std::pair<const char*, int> kTbl[] = {
            {"Move", 0}, {"Stop", 1}, {"Attack", 2}, {"Patrol", 3}, {"Hold", 4}, {"Hold Position", 4},
            {"Gather", 4}, {"Return Cargo", 4}, {"Repair", 3}, {"Build", 6}, {"Advanced", 7},
            {"Stim Pack", 6}, {"Spider Mines", 6}, {"Siege Mode", 6}, {"Tank Mode", 6},
            {"Burrow", 8}, {"Unburrow", 8}, {"Lift Off", 8}, {"Land", 8}, {"Set Rally Point", 5},
            {"Select Larva", 0}, {"Build Interceptor", 6}, {"Build Scarab", 6}, {"Merge Archon", 8},
            {"Dark Archon", 8}, {"Yamato Gun", 6}, {"Lockdown", 6}, {"Nuclear Strike", 8},
            {"Defensive Matrix", 6}, {"EMP Shockwave", 7}, {"Irradiate", 8}, {"Psionic Storm", 6},
            {"Hallucination", 7}, {"Recall", 6}, {"Stasis Field", 7}, {"Disruption Web", 6},
            {"Feedback", 6}, {"Mind Control", 7}, {"Maelstrom", 8}, {"Parasite", 6},
            {"Spawn Broodlings", 7}, {"Ensnare", 8}, {"Infest CC", 5}, {"Dark Swarm", 6},
            {"Plague", 7}, {"Consume", 8}, {"Restoration", 7}, {"Optical Flare", 8}, {"Heal", 6},
            {"Cloak", 6}, {"Decloak", 6}, {"Unload", 8}, {"Scanner Sweep", 0},
            {"Build Nydus Exit", 0}, {"Load", 6}, {"View Exit", 8}, {"Recharge Shields", 0}
        };
        for (const auto& p : kTbl) if (label == p.first) return p.second;
        return -1;
    }

    inline int build_slot(int unit_id) {
        static const std::pair<int, int> kTbl[] = {
            {106,0},{109,1},{110,2},{111,3},{112,6},{113,0},{114,1},{116,2},{122,4},{123,3},{124,5},{125,7},
            {131,0},{134,2},{135,6},{136,4},{138,1},{139,4},{140,3},{141,0},{142,3},{143,1},{149,2},
            {154,0},{155,0},{156,1},{157,2},{159,7},{160,3},{162,5},{163,2},{164,5},{165,3},{166,4},{167,1},{169,4},{170,7},{171,6},{172,6}
        };
        for (const auto& p : kTbl) if (p.first == unit_id) return p.second;
        return -1;
    }

    inline int training_slot(int trainer, int unit_id) {
        struct TP { int tr, u, s; };
        static const TP kTbl[] = {
            {35,37,1},{35,38,3},{35,39,7},{35,41,0},{35,42,2},{35,43,4},{35,45,6},{35,46,8},{35,47,5},
            {38,103,6},{43,44,6},{43,62,7},{106,7,0},{106,107,6},{106,108,7},
            {111,0,0},{111,1,2},{111,32,1},{111,34,3},{113,2,0},{113,3,2},{113,5,1},{113,120,6},
            {114,8,0},{114,9,2},{114,11,1},{114,12,3},{114,58,4},{114,115,6},
            {131,132,6},{132,133,6},{141,137,6},{143,144,6},{143,146,7},{154,64,0},
            {155,69,0},{155,83,1},{155,84,2},{160,61,3},{160,65,0},{160,66,1},{160,67,2},
            {167,60,3},{167,70,0},{167,71,2},{167,72,1},
            {122,16,2},{122,19,3},{122,20,4},{122,23,5},{122,28,6},{122,99,7},
            {139,48,3},{139,51,4},{139,53,5},{139,54,6},{139,55,7},{139,104,8},
            {166,75,3},{166,77,4},{166,78,5},{166,79,6},{166,82,7},{166,88,8}
        };
        for (const auto& p : kTbl) if (p.tr == trainer && p.u == unit_id) return p.s;
        return -1;
    }

    inline int upgrade_slot(int upg) {
        static const std::pair<int, int> kTbl[] = {
            {0,1},{1,3},{2,4},{3,2},{4,1},{5,1},{6,1},{7,0},{8,0},{9,1},{10,0},{11,1},{12,0},{13,0},{14,0},
            {15,2},{16,0},{17,0},{19,2},{20,3},{21,4},{22,1},{23,1},{24,3},{25,4},{26,5},{27,0},{28,1},{29,0},
            {30,1},{31,2},{32,2},{33,2},{34,0},{35,0},{36,1},{37,2},{38,1},{39,0},{40,2},{41,0},{42,1},{43,2},
            {44,2},{47,4},{49,5},{51,5},{52,1},{53,0},{54,3}
        };
        for (const auto& p : kTbl) if (p.first == upg) return p.second;
        return -1;
    }

    inline int research_slot(int tech) {
        static const std::pair<int, int> kTbl[] = {
            {0,1},{1,0},{2,1},{3,1},{5,2},{7,0},{8,0},{9,0},{10,1},{11,2},{13,0},{15,0},{16,1},{17,1},
            {19,0},{20,1},{21,0},{22,1},{24,3},{25,3},{27,3},{30,4},{31,4},{32,2}
        };
        for (const auto& p : kTbl) if (p.first == tech) return p.second;
        return -1;
    }

    inline int slot_override(int utype, const std::string& label, int normal) {
        if (utype == 116) {
            if (label == "Terran Covert Ops" || label == "Covert Ops") return 6;
            if (label == "Terran Physics Lab" || label == "Physics Lab") return 7;
        }
        bool cloak = (label == "Cloak" || label == "Decloak");
        if (utype == 1 || utype == 16 || utype == 99 || utype == 100 || utype == 104) {
            if (cloak) return 6;
            if (label == "Lockdown") return 7;
        }
        if ((utype == 46 || utype == 52) && (label == "Burrow" || label == "Unburrow")) return 5;
        if (utype == 51) {
            if (cloak) return 6;
            if (label == "Psionic Storm") return 8;
            if (label == "Ensnare") return 7;
            if (label == "Consume") return 5;
        }
        return normal;
    }
} // namespace cmd_tables

// --- NativeHud: Authentic Race Console (zconsole / tconsole / pconsole) + F10 Dialogs ---
struct BlizzHud {
    static constexpr int WORLD_BOTTOM = 376;

    int race = -1; // 0=Zerg, 1=Terran, 2=Protoss
    int loaded_tileset = -1;
    PCX console, portrait_overlay, prog_empty, prog_full, ticon, tfontgam;
    uint32_t palette[256] = {};
    BlizzFont tiny, small, large, error_shadow;
    BlizzDialog cmd_layout, prod_layout, gamemenu_dlg, options_dlg, abrt_dlg;
    std::vector<uint8_t> cmd_grp, frames_grp, dlg_grp;
    std::vector<std::string> stat_txt, tips_txt;
    ImageRGBA res_icons[3], sup_icons[3], dlg_btns[5], dipl_btns[3], slice_btns[6];
    std::unordered_map<int, ImageRGBA> frame_cache;
    std::unordered_map<int, ImageRGBA> icon_cache;
    BlizzPortrait portrait;

    int prog_fill_top = -1, prog_fill_bot = -1;
    bool occlusion_sent = false;
    bool grid_hotkeys = false;
    bool show_group_bar = true;
    int card_page = 0;
    std::string prev_card_sig;
    int last_err_seq = -1;
    std::string err_msg;
    double err_until = 0;

    struct Hit {
        int x = 0, y = 0, w = 0, h = 0;
        int slot = -1;
        int card_idx = -1, cmd_token = -1;
        bool enabled = true, local_cancel = false, more = false;
        bool menu_btn = false, terrain_toggle = false;
        int cancel_q = -1, select_unit = -1, center_unit = -1, unload_id = -1, group_num = -1;
        char hotkey = 0;
        std::string tip;
        bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    };
    std::vector<Hit> hits;
    int min_x = 6, min_y = 347, min_w = 128, min_h = 128;

    void close() {
        portrait.close();
        frame_cache.clear();
        icon_cache.clear();
        race = -1;
        loaded_tileset = -1;
        occlusion_sent = false;
    }

    void ensure_loaded(int want_race) {
        want_race = std::max(0, std::min(2, want_race));
        int cur_era = sd_value(13, 0) & 7;
        if (race == want_race && loaded_tileset == cur_era && console.image.valid()) return;

        close();
        race = want_race;
        loaded_tileset = cur_era;
        static const char* kLetters[3] = {"z", "t", "p"};
        static const char* kRaces[3]   = {"zerg", "terran", "protoss"};
        static const char* kEras[8]    = {"badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight"};
        std::string L = kLetters[race];

        load_pcx("game\\" + L + "console.pcx", console, true, -1);
        load_pcx("game\\" + L + "conover.pcx", portrait_overlay, true, -1);
        load_pcx("game\\" + L + "pbrempt.pcx", prog_empty, false, -1);
        load_pcx("game\\" + L + "pbrfull.pcx", prog_full, true, -1);
        load_pcx("unit\\cmdbtns\\ticon.pcx", ticon, false, -1);
        load_pcx("game\\tfontgam.pcx", tfontgam, false, -1);
        cmd_layout.load("rez\\statbtn" + L + ".bin");
        prod_layout.load("rez\\statdata.bin");
        gamemenu_dlg.load("rez\\gamemenu.bin");
        options_dlg.load("rez\\options.bin");
        abrt_dlg.load("rez\\abrtmenu.bin");

        std::memcpy(palette, console.palette, sizeof(palette));
        auto wpe = mpq_read(std::string("tileset\\") + kEras[cur_era] + ".wpe");
        if (wpe.size() >= 1024) {
            for (int i = 0; i < 256; ++i)
                palette[i] = pack_rgba(wpe[i * 4], wpe[i * 4 + 1], wpe[i * 4 + 2], 255);
        }

        tiny.load("8", tfontgam, palette, 0);
        small.load("10", tfontgam, palette, 0);
        large.load("16", tfontgam, palette, 1);
        error_shadow = small.with_shaded(pack_rgba(0, 0, 0, 255));

        cmd_grp    = mpq_read("unit\\cmdbtns\\cmdicons.grp");
        frames_grp = mpq_read("unit\\cmdbtns\\" + L + "cmdbtns.grp");
        dlg_grp    = mpq_read(std::string("dlgs\\") + kRaces[race] + ".grp");
        stat_txt   = parse_tbl(mpq_read("rez\\stat_txt.tbl"));
        tips_txt   = parse_tbl(mpq_read("rez\\tips.tbl"));

        for (int i = 0; i < 5; ++i) dlg_btns[i] = decode_grp_frame(dlg_grp, i, palette, true, false);
        for (int i = 0; i < 3; ++i) dipl_btns[i] = decode_grp_frame(dlg_grp, 83 + i, palette, true, false);
        for (int i = 0; i < 6; ++i) slice_btns[i] = decode_grp_frame(dlg_grp, 112 + i, palette, true, false);

        auto icons_grp = mpq_read("game\\icons.grp");
        int r_ids[3] = {0, 1 + race, 4 + race};
        for (int i = 0; i < 3; ++i) {
            res_icons[i] = decode_grp_frame(icons_grp, r_ids[i], palette, true, false);
            sup_icons[i] = decode_grp_frame(icons_grp, 4 + i, palette, true, false);
        }

        // Clear 60x56 portrait opening at (413, 410) so stale console pixels never cover moving SMK portrait
        if (console.image.valid()) {
            for (int y = 410; y < 466 && y < console.height; ++y)
                for (int x = 413; x < 473 && x < console.width; ++x)
                    console.image.px[size_t(y) * console.width + x] = 0;
        }

        // Measure green progress bar rows in pbrfull.pcx (ProductionProgress152)
        prog_fill_top = -1; prog_fill_bot = -1;
        if (prog_full.image.valid() && prog_empty.image.valid()) {
            int pw = prog_full.width, ph = prog_full.height;
            for (int y = 0; y < ph; ++y) {
                for (int x = pw / 8; x < pw - pw / 8; ++x) {
                    uint32_t p = prog_full.image.px[size_t(y) * pw + x];
                    uint32_t bg = prog_empty.image.px[size_t(y * prog_empty.height / ph) * prog_empty.width + (x * prog_empty.width / pw)];
                    if (px_a(p) >= 128 && px_g(p) > px_r(p) + 8 && px_g(p) > px_b(p) + 8 &&
                        ((p & 0x00FFFFFFu) != (bg & 0x00FFFFFFu))) {
                        if (prog_fill_top < 0) prog_fill_top = y;
                        prog_fill_bot = y + 1;
                        break;
                    }
                }
            }
        }

        portrait.init_tables();
        portrait.set_target(-1, false, sd_value(45, race));
        send_occlusion();
    }

    void send_occlusion() {
        if (!console.image.valid()) return;
        sd_command(46, 0, 0, 0, 0);
        sd_command(47, 0, WORLD_BOTTOM, W, H);
        sd_command(47, W - 204, 2, W, 22);
        sd_command(47, W - 144, H - 126, W, H);
        sd_command(47, 6, 347, 134, 475);
        int start = 0, prev = WORLD_BOTTOM;
        for (int x = 0; x <= W; ++x) {
            int top = WORLD_BOTTOM;
            if (x < W) {
                for (int y = 300; y < WORLD_BOTTOM; ++y) {
                    if (px_a(console.image.px[size_t(y) * console.width + x]) != 0) { top = y; break; }
                }
            }
            if (top != prev || x == W) {
                if (prev < WORLD_BOTTOM) sd_command(47, start, prev, x, WORLD_BOTTOM);
                start = x; prev = top;
            }
        }
        occlusion_sent = true;
    }

    const ImageRGBA& frame(int id) {
        auto it = frame_cache.find(id);
        if (it != frame_cache.end()) return it->second;
        return frame_cache[id] = decode_grp_frame(frames_grp, id, palette, false, false);
    }

    const ImageRGBA& cmd_icon(int id, int ramp) {
        int key = (id << 3) | (ramp & 7);
        auto it = icon_cache.find(key);
        if (it != icon_cache.end()) return it->second;
        uint32_t pal[256];
        std::memcpy(pal, ticon.palette, sizeof(pal));
        if (!ticon.pixels.empty()) {
            for (int i = 0; i < 16; ++i) {
                size_t ti = size_t(ramp) * 16 + size_t(i);
                if (ti < ticon.pixels.size()) pal[i] = ticon.palette[ticon.pixels[ti]];
            }
        }
        return icon_cache[key] = decode_grp_frame(cmd_grp, id, pal, false, true);
    }

    std::string stat_str(int idx, const std::string& fallback) const {
        if (idx >= 0 && idx < int(stat_txt.size()) && !stat_txt[size_t(idx)].empty())
            return clean_ctrl(stat_txt[size_t(idx)]);
        return fallback;
    }

    std::string unit_name(int utype, const std::string& fallback, const CampaignRunner& camp) const {
        if (utype >= 0 && utype < 228) {
            int sidx = sd_value(34, utype);
            if (sidx > 0) {
                std::string custom = clean_ctrl(camp.text(sidx));
                if (!custom.empty()) return custom;
            }
        }
        return stat_str(utype, fallback);
    }

    static int hp_color(const std::string& life) {
        size_t sl = life.find('/');
        if (sl == std::string::npos) return 7;
        int cur = std::atoi(life.c_str());
        int mx  = std::max(1, std::atoi(life.c_str() + sl + 1));
        int pct = cur * 100 / mx;
        return (pct > 66) ? 7 : ((pct >= 33) ? 3 : 6);
    }

    void draw_3slice_btn(Canvas& c, int x, int y, int w, int h, const std::string& txt, bool hov, bool enabled = true) {
        int idx = enabled ? 3 : 0;
        const ImageRGBA& t0 = slice_btns[idx + 0];
        const ImageRGBA& t1 = slice_btns[idx + 1];
        const ImageRGBA& t2 = slice_btns[idx + 2];
        int cap = std::min(8, w / 2);
        if (t0.valid()) c.blit_scaled(t0.px.data(), t0.w, t0.h, x, y, cap, h, hov ? 1.2f : 1.0f);
        if (t1.valid()) {
            for (int px = cap; px < w - cap; px += 8) {
                int sw = std::min(8, w - cap - px);
                c.blit_scaled(t1.px.data(), t1.w, t1.h, x + px, y, sw, h, hov ? 1.2f : 1.0f);
            }
        }
        if (t2.valid()) c.blit_scaled(t2.px.data(), t2.w, t2.h, x + w - cap, y, cap, h, hov ? 1.2f : 1.0f);
        if (!t0.valid()) {
            c.fill_rect(x, y, w, h, hov ? pack_rgba(32, 68, 128, 235) : pack_rgba(18, 36, 82, 225));
            c.stroke_rect(x, y, w, h, hov ? pack_rgba(130, 215, 255) : pack_rgba(61, 66, 177));
        }
        int col = !enabled ? 5 : (hov ? 4 : 3);
        small.draw(c, txt, x + 4, y + std::max(0, (h - small.height) / 2), w - 8, 1, col, 0);
    }

    void draw_dialog_panel(Canvas& c, int x, int y, int w, int h) {
        c.fill_rect(x, y, w, h, pack_rgba(26, 44, 114, 212)); // 0xB01A2C72 classic StarCraft dialog glass
        c.stroke_rect(x, y, w, h, pack_rgba(61, 66, 177, 235));
        c.stroke_rect(x + 1, y + 1, w - 2, h - 2, pack_rgba(16, 24, 68, 235));
    }

    void draw_tooltip(Canvas& c, const std::string& tip) {
        if (tip.empty()) return;
        int box_w = 256, lh = std::max(14, small.height + 2);
        auto lines = small.wrap_lines(tip, box_w - 16);
        int box_h = int(lines.size()) * lh + 12;
        int lx = W - box_w - 8;
        int ty = std::max(8, 346 - box_h);
        c.fill_rect(lx, ty, box_w, box_h, pack_rgba(8, 16, 16, 242));
        uint32_t b1 = (race == 0) ? pack_rgba(228, 60, 60) : ((race == 2) ? pack_rgba(240, 204, 72) : pack_rgba(64, 136, 255));
        uint32_t b2 = (race == 0) ? pack_rgba(88, 24, 24)  : ((race == 2) ? pack_rgba(88, 72, 20)   : pack_rgba(24, 52, 88));
        c.stroke_rect(lx, ty, box_w, box_h, b1);
        c.stroke_rect(lx + 1, ty + 1, box_w - 2, box_h - 2, b2);
        int ry = ty + 6;
        for (const auto& ln : lines) {
            small.draw(c, ln, lx + 8, ry, box_w - 16, 0, 2, 0);
            ry += lh;
        }
    }

    void draw(Canvas& c, double now_ms, bool paused, int mx, int my, bool lmb_down, CampaignRunner& camp) {
        int sel_race = sd_value(5, 0);
        if (sel_race < 0 || sel_race > 2) sel_race = (race >= 0 ? race : 1);
        ensure_loaded(sel_race);
        if (!occlusion_sent) send_occlusion();

        hits.clear();

        // 1. Live Talking Unit / Advisor / Campaign Transmission Portrait at (413, 410, 60x56)
        int utype      = sd_value(1, 0);
        int identity   = sd_value(40, 0);
        int ready_type = sd_value(30, 0);
        int trans_type = (camp.talking == 0 && camp.portraits[0] >= 0) ? camp.portraits[0] : -1;
        bool trans_talk = (trans_type >= 0) && (sd_sound_playing(62) || camp.portrait_speaking);

        int shown_type = (trans_type >= 0) ? trans_type : ((ready_type >= 0) ? ready_type : utype);
        portrait.set_selection(trans_type >= 0 ? (-2 - trans_type) : (ready_type >= 0 ? (-232 - ready_type) : identity));
        bool talk = sd_value(12, 0) != 0;
        if (trans_type >= 0 || ready_type >= 0)
            portrait.set_target(shown_type, trans_type >= 0 ? trans_talk : true);
        else if (utype >= 0)
            portrait.set_target(utype, talk, sd_value(63, utype));
        else
            portrait.set_target(-1, false, sd_value(45, race));

        c.fill_rect(413, 410, 60, 56, pack_rgba(8, 8, 8));
        portrait.draw(c, 413, 410, 60, 56, now_ms, paused, false);

        // 2. Race Console (skip empty middle rows 16..295 = 179K transparent pixels!) + Portrait Rim Overlay
        if (console.image.valid() && console.image.w == W && console.image.h == H) {
            c.blit(console.image.px.data(), W, 16, 0, 0);
            c.blit(console.image.px.data() + 296 * W, W, H - 296, 0, 296);
        } else {
            c.blit_img(console.image, 0, 0);
        }
        c.blit_img(portrait_overlay.image, 413, 410);

        if (identity > 0 && trans_type < 0 && ready_type < 0) {
            Hit h{}; h.x = 413; h.y = 410; h.w = 60; h.h = 56; h.center_unit = identity;
            h.tip = "Center on unit\nMoves the camera to this unit.";
            hits.push_back(std::move(h));
        }

        // 3. Aspect-correct 128x128 Minimap at (6, 347)
        int mw_tiles = std::max(1, sd_value(9, 0));
        int mh_tiles = std::max(1, sd_value(10, 0));
        int max_t    = std::max(mw_tiles, mh_tiles);
        min_w = std::max(1, (mw_tiles * 128 + max_t / 2) / max_t);
        min_h = std::max(1, (mh_tiles * 128 + max_t / 2) / max_t);
        min_x = 6 + (128 - min_w) / 2;
        min_y = 347 + (128 - min_h) / 2;
        int mp_px_w = 0, mp_px_h = 0;
        if (const uint8_t* mm = sd_minimap_rgba(&mp_px_w, &mp_px_h))
            c.blit(reinterpret_cast<const uint32_t*>(mm), 128, 128, 6, 347);

        // 4. MENU button (410, 387) & Minimap Terrain toggle (6, 323)
        bool menu_hov = (mx >= 410 && mx < 476 && my >= 387 && my < 408);
        bool menu_dn  = menu_hov && lmb_down;
        c.blit_img(dlg_btns[menu_dn ? 1 : 0], 410, 387);
        small.draw(c, "MENU", 410, menu_dn ? 392 : 391, 65, 1, menu_dn ? 2 : 3, 0);
        {
            Hit h{}; h.x = 410; h.y = 387; h.w = 66; h.h = 21; h.menu_btn = true;
            h.tip = "Game Menu [\x03""F10 / START\x02]\nSave, Load, Options, Objectives, End Mission.";
            hits.push_back(std::move(h));
        }

        bool terr_on = sd_value(14, 0) != 0;
        bool terr_dn = lmb_down && (mx >= 6 && mx < 67 && my >= 322 && my < 344);
        c.blit_img(dlg_btns[terr_on ? 4 : 3], 6, terr_dn ? 324 : 323);
        {
            Hit h{}; h.x = 6; h.y = 322; h.w = 61; h.h = 22; h.terrain_toggle = true;
            h.tip = std::string(terr_on ? "Hide minimap terrain" : "Show minimap terrain") + " [\x03Tab\x02]";
            hits.push_back(std::move(h));
        }

        // 5. Top-Right Resources (Minerals, Vespene Gas, Supply)
        {
            int rx0 = W - 204 + sd_value(52, 0), ry0 = 2 + sd_value(53, 0);
            auto rf = split_char(sd_text(1), '\t');
            for (int i = 0; i < 2; ++i) {
                c.blit_img(res_icons[i], rx0 + i * 68, ry0);
                if (size_t(i) < rf.size())
                    small.draw(c, rf[size_t(i)], rx0 + 16 + i * 68, ry0, 50, 0, 7, 0);
            }
            int sup_mask = fld_int(rf, 4, 1 << race);
            int row = 0;
            int order[3] = {race, (race + 1) % 3, (race + 2) % 3};
            for (int sr : order) {
                if ((sup_mask & (1 << sr)) == 0) continue;
                int used = fld_int(rf, size_t(5 + sr * 2), fld_int(rf, 2, 0));
                int avail = fld_int(rf, size_t(6 + sr * 2), fld_int(rf, 3, 0));
                int sy = ry0 + row++ * 18;
                c.blit_img(sup_icons[sr], rx0 + 136, sy);
                small.draw(c, std::to_string(used) + "/" + std::to_string(avail),
                           rx0 + 152, sy, 50, 0, used > avail ? 6 : 7, 0);
            }
        }

        // 6. Center Selection Panel: Wireframes, HP/Shields/Energy, Cargo, Production Queue, Details
        std::string card_raw  = sd_text(0);
        std::string wires_raw = sd_text(3);
        std::string det_raw   = sd_text(4);
        std::string prod_raw  = sd_text(2);
        auto card_lines = card_raw.empty()  ? std::vector<std::string>{} : split_char(card_raw, '\n');
        auto wire_lines = wires_raw.empty() ? std::vector<std::string>{} : split_char(wires_raw, '\n');
        while (!wire_lines.empty() && wire_lines.back().empty()) wire_lines.pop_back();
        auto hdr = card_lines.empty() ? std::vector<std::string>{} : split_char(card_lines[0], '\t');
        std::string card_title = hdr.empty() ? "" : hdr[0];

        int count = int(wire_lines.size());
        std::string cargo_raw = (count == 1 && prod_raw.empty()) ? sd_text(14) : "";
        bool showing_cargo = (count == 1 && prod_raw.empty() && !cargo_raw.empty());

        if (count > 0) {
            int cols = (count + 1) / 2;
            int group_w = cols * 33 + (cols - 1) * 3;
            int gx0 = 168 + (213 - group_w) / 2;

            for (int i = 0; i < std::min(12, count); ++i) {
                auto wf = split_char(wire_lines[size_t(i)], '\t');
                int wbox = 64;
                const uint8_t* wr = sd_wire_rgba(i, &wbox);
                int ix = (count == 1) ? 168 : (gx0 + (i % cols) * 36);
                int iy = (count == 1) ? 388 : (396 + (i / cols) * 37);
                if (count > 1) c.blit_img(frame(fld_int(wf, 6, 13)), ix, iy);
                if (wr && wbox > 0) c.blit(reinterpret_cast<const uint32_t*>(wr), wbox, wbox, ix, iy);
                Hit h{}; h.x = ix; h.y = iy; h.w = wbox; h.h = wbox;
                h.select_unit = fld_int(wf, 0, -1);
                h.tip = "Select unit\nSelects this unit from the group.";
                hits.push_back(std::move(h));
            }

            if (count == 1) {
                std::string uname = unit_name(utype, card_title, camp);
                c.set_clip(240, 388, 392, 388 + small.height + 2);
                small.draw(c, uname, 240, 388, 152, 1, 2, 0);
                c.reset_clip();

                auto det_lines = det_raw.empty() ? std::vector<std::string>{} : split_char(det_raw, '\n');
                auto dv = det_lines.empty() ? std::vector<std::string>{} : split_char(det_lines[0], '\t');

                std::string life = (hdr.size() > 1) ? hdr[1] : "";
                if (life.rfind("HP ", 0) == 0) life.erase(0, 3);
                int hcol = hp_color(life);
                std::string life_disp = std::string(1, char(hcol)) + life;
                if (!dv.empty() && fld_int(dv, 1, -1) >= 0 && dv.size() > 2)
                    life_disp = "\x02" + dv[1] + "/" + dv[2] + " " + life_disp;
                int lw = tiny.measure(life_disp);
                tiny.draw(c, life_disp, 200 - lw / 2, 452, 0, 0, hcol, 0);

                if (!showing_cargo) {
                    std::string sec = (hdr.size() > 2) ? hdr[2] : "";
                    bool res_amt = (sec.rfind("Gas", 0) == 0 || sec.rfind("Minerals", 0) == 0);
                    if (!dv.empty() && !res_amt) {
                        sec = (fld_int(dv, 3, -1) >= 0 && dv.size() > 4) ? ("\x10" + dv[3] + "/" + dv[4]) : "";
                    }
                    if (res_amt) small.draw(c, sec, 240, 434, 152, 1, 2, 0);
                    else if (!sec.empty()) tiny.draw(c, sec, 160, 464, 80, 1, 2, 0);

                    std::string ustat = (hdr.size() > 3) ? clean_ctrl(hdr[3]) : "";
                    bool det = !dv.empty() && fld_int(dv, 6, 0) != 0;
                    if (det && ustat.empty()) ustat = "\x03" + stat_str(830, "Detector");
                    if (!ustat.empty()) {
                        c.set_clip(240, 402, 392, 417);
                        (small.measure(ustat) <= 152 ? small : tiny).draw(c, ustat, 240, 403, 152, 1, 2, 0);
                        c.reset_clip();
                    }
                }
            }
        }

        if (count == 1 && !prod_raw.empty()) {
            draw_production(c, prod_raw, 138, 388);
        } else if (showing_cargo) {
            draw_cargo(c, cargo_raw);
        } else if (count == 1 && !det_raw.empty()) {
            draw_details(c, det_raw);
        }

        // 7. Right 3x3 Command Card (statbtn{z,t,p}.bin)
        draw_commands(c, card_lines, utype, card_title, mx, my, lmb_down);

        // 8. World Overlays: Campaign Transmissions / Chat Messages, Countdown Timer, Leaderboard, Errors
        std::string world_msg = camp.active && !camp.message.empty() ? camp.message : std::string(sd_text(12));
        if (!world_msg.empty()) {
            auto mlines = small.wrap_lines(world_msg, 420);
            int my0 = std::max(28, 304 - int(mlines.size()) * (small.height + 2));
            for (const auto& ml : mlines) {
                error_shadow.draw(c, ml, 19, my0 + 1, 420, 0, 2, 0);
                small.draw(c, ml, 18, my0, 420, 0, 2, 0);
                my0 += small.height + 2;
            }
        }
        if (sd_value(32, 0)) {
            std::string tmr = sd_text(7);
            if (!tmr.empty()) large.draw(c, tmr, 18, 6, 220, 0, 4, 0);
        }
        {
            std::string board = sd_text(15);
            if (!board.empty()) small.draw(c, board, 18, 26, 280, 0, 2, 0);
        }
        {
            const char* es = sd_text(8);
            if (es && *es) {
                const char* tb = std::strchr(es, '\t');
                if (tb) {
                    int seq = std::atoi(es);
                    if (seq != last_err_seq && *(tb + 1)) {
                        last_err_seq = seq;
                        err_msg = tb + 1;
                        err_until = now_ms + 2600.0;
                    }
                }
            }
            if (!err_msg.empty() && now_ms < err_until) {
                error_shadow.draw(c, err_msg, 91, 333, 460, 1, 2, 0);
                small.draw(c, err_msg, 90, 332, 460, 1, 2, 0);
            }
        }

        // 9. Optional Compact Control-Group Quick Pills (for Handheld/Gamepad convenience, only when populated)
        if (show_group_bar) {
            int gx = 148;
            for (int g = 1; g <= 9; ++g) {
                int gn = sd_value(21, g);
                if (gn <= 0) continue;
                c.fill_rect(gx, 2, 28, 15, pack_rgba(12, 20, 32, 196));
                c.stroke_rect(gx, 2, 28, 15, pack_rgba(78, 128, 188, 220));
                tiny.draw(c, std::to_string(g) + ":" + std::to_string(gn), gx + 2, 4, 24, 1, 3, 0);
                Hit h{}; h.x = gx; h.y = 2; h.w = 28; h.h = 15; h.group_num = g;
                h.tip = "Control Group " + std::to_string(g) + "\nLeft-click: Recall · R2+Click: Assign";
                hits.push_back(std::move(h));
                gx += 31;
                if (gx > W - 240) break;
            }
        }

        // 10. Hover Tooltip
        if (!paused) {
            for (const auto& h : hits) {
                if (h.contains(mx, my) && !h.tip.empty()) {
                    draw_tooltip(c, h.tip);
                    break;
                }
            }
        }
    }

    void draw_production(Canvas& c, const std::string& raw, int x, int y) {
        auto pf = split_char(raw, '\t');
        int pct  = std::max(0, std::min(100, fld_int(pf, 0, 0)));
        int kind = sd_value(3, 0), target = sd_value(4, 0);
        bool merging = (kind == 5), morph = (kind == 4 || merging);
        int lbl_id = (morph && target < 0) ? 65506 : 65521;
        int bar_id = (morph && target < 0) ? 13 : (kind == 2 ? 14 : (kind == 3 ? 16 : 7));
        const auto* lw = prod_layout.get(lbl_id);
        const auto* bw = prod_layout.get(bar_id);
        int utype = sd_value(1, 0), urace = sd_value(5, 0);
        if (urace < 0 || urace > 2) urace = race;
        int sidx = morph ? (798 + urace) : ((kind == 2 || kind == 3) ? -1 : ((utype == 160 || utype == 167 ? 770 : 773) + urace));
        std::string title = merging ? "Summoning" : ((utype == 36 || utype == 59 || utype == 97) ? "Morphing"
                          : stat_str(sidx, kind == 2 ? "Researching" : (kind == 3 ? "Upgrading" : "Building")));
        if (lw) small.draw(c, title, x + lw->x, y + lw->y, lw->w, 1, 2, 0);
        if (bw && prog_empty.image.valid()) {
            int bx = x + bw->x, by = y + bw->y;
            c.blit_img(prog_empty.image, bx, by);
            if (pct > 0 && prog_full.image.valid() && prog_empty.width > 4) {
                int fill_w = (prog_empty.width - 4) * pct / 100;
                c.set_clip(bx + 2, by + 2, bx + 2 + fill_w, by + prog_empty.height - 2);
                if (prog_fill_top >= 0 && prog_fill_bot > prog_fill_top) {
                    int sh = prog_fill_bot - prog_fill_top;
                    c.blit_scaled(prog_full.image.px.data() + size_t(prog_fill_top) * prog_full.width,
                                  prog_full.width, sh, bx, by + 2, prog_empty.width, prog_empty.height - 4);
                } else {
                    c.blit_img(prog_full.image, bx, by);
                }
                c.reset_clip();
            }
        }
        int n = morph ? (target >= 0 ? 1 : 0) : (kind == 1 ? 5 : 1);
        for (int i = 0; i < n; ++i) {
            const auto* w = prod_layout.get(i + 2);
            if (!w) continue;
            int wx = x + w->x, wy = y + w->y;
            c.blit_img(frame((i == 0 || i == 4) ? 2 : 4), wx, wy);
            int icon_id = (i == 0) ? sd_value(2, 0) : sd_value(4, i);
            if (icon_id >= 0) {
                c.blit_img(cmd_icon(icon_id, 0), wx, wy + 1);
                if (kind == 1) tiny.draw(c, std::to_string(i + 1), wx + 2, wy + 2, 0, 0, i == 0 ? 3 : 2, 0);
                Hit h{}; h.x = wx; h.y = wy; h.w = w->w; h.h = w->h; h.cancel_q = i;
                h.tip = "Cancel production\nCancels this item in the production queue.";
                hits.push_back(std::move(h));
            } else if (kind == 1) {
                const auto& num_fr = frame(i + 6);
                if (num_fr.valid()) c.blit_img(num_fr, wx + 18 - num_fr.w / 2, wy + 18 - num_fr.h / 2);
            }
        }
        if (kind == 1) {
            if (const auto* conn = prod_layout.get(8)) {
                c.set_clip(x + conn->x, y + conn->y, x + conn->x + conn->w, y + conn->y + conn->h);
                c.blit_img(frame(11), x + conn->x, y + conn->y);
                c.reset_clip();
            }
        }
    }

    void draw_cargo(Canvas& c, const std::string& raw) {
        auto rows = split_char(raw, '\n');
        while (!rows.empty() && rows.back().empty()) rows.pop_back();
        if (rows.empty()) return;
        auto f0 = split_char(rows[0], '\t');
        int cap = fld_int(f0, 6, 8);
        int four = (cap < 7) ? 20 : 18, two = (cap < 7) ? 22 : 21, one = (cap < 7) ? 27 : 25;
        for (int sp : {4, 2, 1}) {
            for (int i = 0; i < std::min(8, int(rows.size())); ++i) {
                auto d = split_char(rows[size_t(i)], '\t');
                if (fld_int(d, 4, 1) != sp) continue;
                const auto* w = prod_layout.get(sp == 4 ? four : (sp == 2 ? two : one));
                four += sp / 4; two += sp / 2; one += sp;
                if (!w) continue;
                int wx = 138 + w->x, wy = 388 + w->y;
                c.blit_img(frame(fld_int(d, 5, 13)), wx, wy);
                if (const uint8_t* cw = sd_cargo_wire_rgba(i)) {
                    int pw = (sp == 4) ? 64 : 32, ph = (sp == 1) ? 32 : 64;
                    int dx = wx + (w->w - pw) / 2, dy = wy + (w->h - ph) / 2;
                    for (int ry = 0; ry < ph; ++ry) {
                        const uint32_t* srow = reinterpret_cast<const uint32_t*>(cw) + size_t(ry) * 64;
                        c.blit(srow, pw, 1, dx, dy + ry);
                    }
                }
                Hit h{}; h.x = wx; h.y = wy; h.w = w->w; h.h = w->h;
                h.unload_id = fld_int(d, 0, -1);
                h.tip = stat_str(fld_int(d, 1, -1), "Passenger") + "\nClick to unload.";
                hits.push_back(std::move(h));
            }
        }
    }

    void draw_details(Canvas& c, const std::string& raw) {
        auto lines = split_char(raw, '\n');
        if (lines.empty()) return;
        auto d0 = split_char(lines[0], '\t');
        bool building = fld_int(d0, 5, 0) != 0;
        bool supply   = fld_int(d0, 7, 0) > 0 && fld_int(d0, 11, 0) == 0;
        if (supply) {
            int srace = std::max(0, std::min(2, fld_int(d0, 10, race)));
            int ids[4] = {820 + srace, 814 + srace, 817 + srace, 823 + srace};
            const char* lbls[4] = {"Used:", "Provided:", "Total:", "Max:"};
            int vals[4] = {fld_int(d0, 8, 0), fld_int(d0, 7, 0), fld_int(d0, 9, 0), 200};
            for (int i = 0; i < 4; ++i)
                tiny.draw(c, stat_str(ids[i], lbls[i]) + " " + std::to_string(vals[i]), 281, 418 + i * 14, 111, 1, 2, 0);
        }
        if (!building && !supply && (raw.find("\nweapon\t") != std::string::npos || raw.find("\nammo\t") != std::string::npos))
            small.draw(c, "Kills: " + (d0.empty() ? "0" : d0[0]), 240, 418, 152, 1, 2, 0);
        if (building && raw.find("\nweapon\t") == std::string::npos) return;

        struct DetRow { int ord; std::vector<std::string> f; };
        std::vector<DetRow> rows;
        for (size_t i = 1; i < lines.size(); ++i) {
            if (lines[i].empty()) continue;
            auto a = split_char(lines[i], '\t');
            if (a.empty() || (building && a[0] == "armor")) continue;
            if (supply && a[0] != "armor") continue;
            int ord = (a[0] == "armor") ? 0 : (a[0] == "shield" ? 1 : (a[0] == "weapon" ? 2 : 3));
            rows.push_back({ord, std::move(a)});
        }
        std::stable_sort(rows.begin(), rows.end(), [](const DetRow& a, const DetRow& b) { return a.ord < b.ord; });
        for (size_t i = 0; i < std::min<size_t>(4, rows.size()); ++i) {
            const auto& a = rows[i].f;
            int bx = 242 + int(i) * 39, by = 439;
            c.blit_img(cmd_icon(fld_int(a, 1, -1), 5), bx, by + 1);
            c.blit_img(frame(12), bx, by);
            if (a.size() > 2) tiny.draw(c, a[2], bx + 22, by + 27, 12, 2, 2, 0);
            Hit h{}; h.x = bx; h.y = by; h.w = 36; h.h = 36;
            std::string lbl = (a[0] == "ammo") ? (fld_int(a, 1, 0) == 85 ? "Scarabs" : "Interceptors")
                            : stat_str(fld_int(a, 6, 0) - 1, a[0] == "weapon" ? "Damage" : "Armor");
            std::string amt = (a.size() > 3 ? a[3] : "0") + (fld_int(a, 4, 0) ? ("+" + a[4]) : "");
            h.tip = lbl + "\n" + ((a[0] == "weapon" || a[0] == "ammo") ? "Damage: " : "Armor: ") + amt +
                    (a[0] == "ammo" ? "" : ("\nUpgrade level: " + (a.size() > 2 ? a[2] : "0")));
            hits.push_back(std::move(h));
        }
    }

    void draw_commands(Canvas& c, const std::vector<std::string>& lines, int utype,
                       const std::string& title, int mx, int my, bool lmb_down) {
        std::string sig;
        for (size_t i = 1; i < lines.size(); ++i) { sig += lines[i]; sig.push_back('\n'); }
        if (sig != prev_card_sig) { card_page = 0; prev_card_sig = sig; }

        std::vector<std::vector<std::string>> cmds;
        bool worker = (utype == 7 || utype == 41 || utype == 64);
        for (size_t i = 1; i < lines.size(); ++i) {
            auto f = split_char(lines[i], '\t');
            if (f.size() < 3) continue;
            int authored = fld_int(f, 18, -1);
            if (worker && (authored < 0 || authored >= 9) &&
                (f[1] == "Patrol" || f[1].rfind("Hold", 0) == 0)) continue;
            cmds.push_back(std::move(f));
        }
        bool is_build = (title == "Build" || title == "Advanced Build");
        bool has_esc = false;
        for (const auto& f : cmds) if (!f[0].empty() && f[0][0] == '\x1b') has_esc = true;
        int local_cancel_idx = -1;
        if ((is_build || sd_value(0, 0) == 2) && !has_esc) {
            local_cancel_idx = int(cmds.size());
            cmds.push_back({"\x1b", "Cancel", "1", "-1"});
        }

        int pages = (cmds.size() > 9) ? int((cmds.size() + 7) / 8) : 1;
        bool used[9] = {};
        size_t from = 0, to = cmds.size();
        if (pages > 1) {
            from = size_t(card_page % pages) * 8;
            to = std::min(from + 8, cmds.size());
            used[8] = true;
        }

        std::vector<std::pair<int, size_t>> placed;
        for (size_t idx = from; idx < to; ++idx) {
            const auto& f = cmds[idx];
            int slot = -1;
            if (pages == 1) {
                int authored = fld_int(f, 18, -1);
                if (authored >= 0 && authored < 9) slot = authored;
                else if (!f[0].empty() && f[0][0] == '\x1b') slot = 8;
                else if (is_build) slot = cmd_tables::build_slot(fld_int(f, 3, -1));
                else if (fld_int(f, 11, -1) >= 0) slot = cmd_tables::upgrade_slot(fld_int(f, 11, -1));
                else if (fld_int(f, 12, -1) >= 0) slot = cmd_tables::research_slot(fld_int(f, 12, -1));
                else {
                    slot = cmd_tables::label_slot(f[1]);
                    if (f[1] == "Set Rally Point" && (utype >= 130 && utype <= 133)) slot = 1;
                    if (slot < 0) slot = cmd_tables::training_slot(utype, fld_int(f, 3, -1));
                    slot = cmd_tables::slot_override(utype, f[1], slot);
                }
            }
            if (slot >= 0 && slot < 9 && !used[slot]) {
                used[slot] = true;
                placed.emplace_back(slot, idx);
            } else {
                placed.emplace_back(-1, idx);
            }
        }
        for (auto& pl : placed) {
            if (pl.first < 0) {
                for (int s = 0; s < 9; ++s) if (!used[s]) { used[s] = true; pl.first = s; break; }
            }
        }

        auto slot_rect = [&](int s, int& bx, int& by) {
            if (const auto* w = cmd_layout.get(s + 1)) {
                bx = (W - 144) + w->x;
                by = 354 + w->y;
            } else {
                bx = 504 + (s % 3) * 46;
                by = 358 + (s / 3) * 40;
            }
        };

        static const char kGridKeys[10] = "QWEASDZXC";
        for (const auto& pl : placed) {
            if (pl.first < 0) continue;
            const auto& f = cmds[pl.second];
            int bx = 0, by = 0;
            slot_rect(pl.first, bx, by);
            c.blit_img(frame(0), bx, by);

            int icon_id = fld_int(f, 10, fld_int(f, 3, -1));
            if (icon_id < 0) icon_id = cmd_tables::order_icon(f[1]);
            int br = sd_value(5, 0); if (br < 0 || br > 2) br = race;
            if (f[1] == "Build")    icon_id = (br == 0 ? 257 : (br == 1 ? 234 : 272));
            if (f[1] == "Advanced") icon_id = (br == 0 ? 258 : (br == 1 ? 235 : 273));

            bool en = (f[2] != "0");
            bool act = (fld_int(f, 9, 0) == 1);
            const auto& ic = cmd_icon(icon_id, !en ? 1 : (act ? 2 : 0));
            if (ic.valid()) c.blit_img(ic, bx, by);
            else tiny.draw(c, f[1], bx + 2, by + 8, 34, 1, 3, 0);

            if (lmb_down && mx >= bx && mx < bx + 36 && my >= by && my < by + 34)
                c.stroke_rect(bx + 1, by + 1, 34, 34, en ? pack_rgba(238, 229, 162) : pack_rgba(116, 123, 138));

            Hit h{}; h.x = bx; h.y = by; h.w = 36; h.h = 34; h.slot = pl.first;
            h.enabled = en;
            h.local_cancel = (int(pl.second) == local_cancel_idx);
            h.card_idx  = fld_int(f, 16, -1);
            h.cmd_token = fld_int(f, 17, 0);
            h.hotkey    = grid_hotkeys ? kGridKeys[pl.first] : (f[0].empty() ? 0 : f[0][0]);

            std::string hk_lbl;
            if (h.hotkey == '\x1b') hk_lbl = "Esc";
            else if (h.hotkey >= 32) hk_lbl = std::string(1, char(std::toupper(static_cast<unsigned char>(h.hotkey))));
            int u = fld_int(f, 3, -1);
            std::string disp_lbl = (u >= 0 && u < 228 && f[1] != "Select Larva" && f[1] != "Build Nydus Exit" &&
                                    f[1] != "Build Interceptor" && f[1] != "Build Scarab" && f[1] != "Merge Archon" &&
                                    f[1] != "Dark Archon") ? stat_str(u, f[1]) : clean_ctrl(f[1]);
            h.tip = disp_lbl + (hk_lbl.empty() ? "" : (" [\x03" + hk_lbl + "\x02]"));
            int mc = fld_int(f, 4, 0), gc = fld_int(f, 5, 0);
            if (mc > 0 || gc > 0) {
                h.tip += "\n";
                if (mc > 0) h.tip += "Minerals: " + std::to_string(mc) + (gc > 0 ? "  " : "");
                if (gc > 0) h.tip += "Gas: " + std::to_string(gc);
            }
            if (f.size() > 7 && !f[7].empty()) h.tip += "\n\x06Requires: " + clean_ctrl(f[7]);
            hits.push_back(std::move(h));
        }

        if (pages > 1) {
            int bx = 0, by = 0;
            slot_rect(8, bx, by);
            c.blit_img(frame(0), bx, by);
            tiny.draw(c, "More", bx, by + 10, 36, 1, 3, 0);
            Hit h{}; h.x = bx; h.y = by; h.w = 38; h.h = 36; h.slot = 8; h.more = true;
            h.tip = "More commands\nShows the next page of commands.";
            hits.push_back(std::move(h));
        }
    }
};

} // namespace blizz
