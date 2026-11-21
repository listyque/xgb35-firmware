#pragma once
// Core portable domain logic for Jellyfin_fpl (shared by ARMv7 app and host selfcheck).

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace jf {

inline double monotonic_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

struct BitratePreset {
    int64_t bps;
    int max_w;
    int max_h;
    const char* label_ru;
    const char* label_en;
};

static constexpr BitratePreset kBitratePresets[] = {
    {0,        0,    0,    "Макс (Оригинал)", "Max (Direct)"},
    {20000000, 1920, 1080, "20 Мбит/с (1080p)", "20 Mbps (1080p)"},
    {8000000,  1920, 1080, "8 Мбит/с (1080p)",  "8 Mbps (1080p)"},
    {4000000,  1280, 720,  "4 Мбит/с (720p)",   "4 Mbps (720p)"},
    {2000000,  854,  480,  "2 Мбит/с (480p)",   "2 Mbps (480p)"},
    {1000000,  640,  360,  "1 Мбит/с (360p)",   "1 Mbps (360p)"},
    {420000,   426,  240,  "420 Кбит/с (240p)", "420 kbps (240p)"},
};
static constexpr int kBitratePresetCount = int(sizeof(kBitratePresets) / sizeof(kBitratePresets[0]));

inline int find_bitrate_preset_idx(int64_t bps) {
    for (int i = 0; i < kBitratePresetCount; ++i)
        if (kBitratePresets[i].bps == bps) return i;
    return 4; // Default 2 Mbps (480p) tuned for ATM7051H Cortex-A5
}

inline const BitratePreset& get_bitrate_preset(int64_t bps) {
    return kBitratePresets[find_bitrate_preset_idx(bps)];
}

struct MediaStreamInfo {
    int index = -1;
    std::string type;          // "Video", "Audio", "Subtitle"
    std::string codec;
    std::string language;
    std::string title;
    std::string display_title;
    bool is_default = false;
    bool is_external = false;
    bool is_text_sub = false;

    std::string label() const {
        if (!display_title.empty()) return display_title;
        std::string out;
        if (!language.empty()) out = language;
        if (!title.empty()) out = out.empty() ? title : (out + " - " + title);
        if (!codec.empty()) out = out.empty() ? codec : (out + " [" + codec + "]");
        return out.empty() ? ("#" + std::to_string(index)) : out;
    }
};

struct MediaItem {
    std::string id;
    std::string name;
    std::string type;
    std::string collection_type;
    std::string media_type;
    std::string parent_id;
    std::string series_id;
    std::string series_name;
    std::string season_id;
    std::string season_name;
    int parent_index_number = 0;
    int index_number = 0;
    std::string album_id;
    std::string album_name;
    std::string media_source_id;
    std::string container;
    std::string primary_tag;
    std::string thumb_tag;
    int64_t runtime_ticks = 0;
    int64_t position_ticks = 0;
    int64_t source_bitrate = 0;
    bool played = false;
    bool is_folder = false;
    bool is_placeholder = false;
    std::vector<MediaStreamInfo> streams;
};

struct DownloadFolder {
    std::string id;
    std::string name;
};

struct DownloadedEntry {
    MediaItem item;
    std::string local_media_path;
    std::string local_thumb_path;
    std::map<int, std::string> local_subs; // stream index -> local .vtt/.srt path
    bool has_folder_path = false;
    std::vector<DownloadFolder> folder_path;
    int64_t file_bytes = 0;
};

enum class QueueStatus { Queued = 0, Downloading = 1, Paused = 2, Failed = 3 };

struct QueueEntry {
    MediaItem item;
    int64_t max_bitrate = 2000000;
    bool original = false;
    QueueStatus status = QueueStatus::Queued;
    int64_t downloaded_bytes = 0;
    int64_t total_bytes = 0;
    std::string error_msg;
    bool has_folder_path = false;
    std::vector<DownloadFolder> folder_path;
};

struct AppSettings {
    std::string server_url;
    std::string user_id;
    std::string username;
    std::string server_name;
    std::string access_token;
    std::string device_id;
    std::string lang = "ru";              // "ru" or "en"
    int64_t stream_bitrate = 2000000;     // 2 Mbps (480p) default for XGB35
    int64_t download_bitrate = 2000000;   // 2 Mbps (480p) default
    bool download_original = false;
    std::string default_audio_lang = "rus";
    std::string default_sub_lang;         // "" = Off, "rus", "eng"
    int audio_output = 0;                 // 0 = Auto, 1 = HDMI, 2 = Speaker
    int video_fit = 0;                    // 0 = Fit (letterbox), 1 = Fill (crop)
    int hw_vol = 32;                      // 0..40 for tinymix 15
};

// --- Item & Folder Classification (mirrors utils/library/folders.ts) ---

inline bool is_folder_type(const std::string& type) {
    return type == "Folder" || type == "CollectionFolder" || type == "PhotoAlbum" ||
           type == "MusicAlbum" || type == "Series" || type == "Season" ||
           type == "UserView" || type == "AggregateFolder";
}

inline bool opens_as_folders(const MediaItem& item) {
    if (item.id.empty()) return false;
    return item.collection_type != "livetv" &&
           item.collection_type != "boxsets" &&
           item.collection_type != "playlists";
}

inline bool is_downloadable_type(const std::string& type) {
    return type == "Movie" || type == "Episode" || type == "Video" ||
           type == "MusicVideo" || type == "Audio" || type == "Trailer";
}

inline bool is_playable_item(const MediaItem& item) {
    if (item.is_folder || is_folder_type(item.type) || item.is_placeholder) return false;
    if (is_downloadable_type(item.type)) return true;
    return item.media_type == "Video" || item.media_type == "Audio";
}

inline bool is_audio_only(const MediaItem& item) {
    return item.type == "Audio" || (item.media_type == "Audio" && item.type != "MusicVideo");
}

inline bool is_text_subtitle_codec(std::string codec) {
    for (char& c : codec) c = char(std::tolower(static_cast<unsigned char>(c)));
    return codec == "srt" || codec == "subrip" || codec == "vtt" || codec == "webvtt" ||
           codec == "ass" || codec == "ssa" || codec == "mov_text" || codec == "text";
}

inline std::string format_ticks(int64_t ticks) {
    if (ticks <= 0) return "0:00";
    int64_t total_sec = ticks / 10000000LL;
    int64_t h = total_sec / 3600;
    int64_t m = (total_sec % 3600) / 60;
    int64_t s = total_sec % 60;
    char buf[32];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", (long long)h, (long long)m, (long long)s);
    else
        std::snprintf(buf, sizeof(buf), "%lld:%02lld", (long long)m, (long long)s);
    return buf;
}

inline std::string format_bytes(int64_t bytes) {
    char buf[32];
    if (bytes >= 1024LL * 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.2f GB", double(bytes) / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024LL * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024LL)
        std::snprintf(buf, sizeof(buf), "%lld KB", (long long)(bytes / 1024));
    else
        std::snprintf(buf, sizeof(buf), "%lld B", (long long)bytes);
    return buf;
}

// --- UTF-8 Decode/Encode & Cyrillic/Latin Search Normalization ---

inline uint32_t next_utf8(const std::string& s, size_t& i) {
    if (i >= s.size()) return 0;
    uint8_t c0 = static_cast<uint8_t>(s[i++]);
    if (c0 < 0x80u) return c0;
    if ((c0 & 0xE0u) == 0xC0u && i < s.size()) {
        uint8_t c1 = static_cast<uint8_t>(s[i++]);
        return ((uint32_t(c0 & 0x1Fu) << 6) | uint32_t(c1 & 0x3Fu));
    }
    if ((c0 & 0xF0u) == 0xE0u && i + 1 < s.size()) {
        uint8_t c1 = static_cast<uint8_t>(s[i++]);
        uint8_t c2 = static_cast<uint8_t>(s[i++]);
        return ((uint32_t(c0 & 0x0Fu) << 12) | (uint32_t(c1 & 0x3Fu) << 6) | uint32_t(c2 & 0x3Fu));
    }
    if ((c0 & 0xF8u) == 0xF0u && i + 2 < s.size()) {
        uint8_t c1 = static_cast<uint8_t>(s[i++]);
        uint8_t c2 = static_cast<uint8_t>(s[i++]);
        uint8_t c3 = static_cast<uint8_t>(s[i++]);
        return ((uint32_t(c0 & 0x07u) << 18) | (uint32_t(c1 & 0x3Fu) << 12) |
                (uint32_t(c2 & 0x3Fu) << 6) | uint32_t(c3 & 0x3Fu));
    }
    return 0xFFFDu;
}

inline void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80u) {
        out.push_back(char(cp));
    } else if (cp < 0x800u) {
        out.push_back(char(0xC0u | (cp >> 6)));
        out.push_back(char(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(char(0xE0u | (cp >> 12)));
        out.push_back(char(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(char(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(char(0xF0u | (cp >> 18)));
        out.push_back(char(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(char(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(char(0x80u | (cp & 0x3Fu)));
    }
}

inline void pop_utf8_char(std::string& s) {
    while (!s.empty()) {
        uint8_t b = static_cast<uint8_t>(s.back());
        s.pop_back();
        if ((b & 0xC0u) != 0x80u) break;
    }
}

inline uint32_t fold_codepoint(uint32_t cp, bool* is_alnum) {
    *is_alnum = true;
    if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
    if (cp >= 'a' && cp <= 'z') return cp;
    if (cp >= '0' && cp <= '9') return cp;
    // Cyrillic: Ё (0x0401) / ё (0x0451) -> е (0x0435), А..Я (0x0410..0x042F) -> а..я (0x0430..0x044F)
    if (cp == 0x0401u || cp == 0x0451u) return 0x0435u;
    if (cp >= 0x0410u && cp <= 0x042Fu) return cp + 0x20u;
    if (cp >= 0x0430u && cp <= 0x044Fu) return cp;
    // Latin-1 Supplement letters
    if (cp >= 0x00C0u && cp <= 0x00D6u) return cp + 0x20u;
    if (cp >= 0x00D8u && cp <= 0x00DEu) return cp + 0x20u;
    if (cp >= 0x00E0u && cp <= 0x00FFu && cp != 0x00F7u) return cp;
    // Other Unicode letters/CJK/Cyrillic ext
    if (cp >= 0x0100u && cp <= 0x052Fu) return cp;
    if (cp >= 0x3040u && cp <= 0x9FFFu) return cp;
    *is_alnum = false;
    return ' ';
}

inline std::string normalize_name(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    bool prev_space = true;
    size_t i = 0;
    while (i < raw.size()) {
        uint32_t cp = next_utf8(raw, i);
        if (cp == 0xFEFFu) continue;
        bool alnum = false;
        uint32_t folded = fold_codepoint(cp, &alnum);
        if (alnum) {
            append_utf8(out, folded);
            prev_space = false;
        } else if (!prev_space) {
            out.push_back(' ');
            prev_space = true;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

inline std::vector<MediaItem> find_folders(const std::vector<MediaItem>& folders, const std::string& search) {
    std::string query = normalize_name(search);
    if (query.empty()) return {};

    std::vector<std::string> words;
    for (size_t pos = 0; pos < query.size();) {
        size_t sp = query.find(' ', pos);
        if (sp == std::string::npos) {
            words.push_back(query.substr(pos));
            break;
        }
        if (sp > pos) words.push_back(query.substr(pos, sp - pos));
        pos = sp + 1;
    }

    struct Ranked {
        size_t idx;
        int rank;
    };
    std::vector<Ranked> hits;
    for (size_t idx = 0; idx < folders.size(); ++idx) {
        std::string name = normalize_name(folders[idx].name);
        bool all_match = true;
        for (const auto& w : words) {
            if (name.find(w) == std::string::npos) {
                all_match = false;
                break;
            }
        }
        if (!all_match) continue;
        int rank = (name == query) ? 0 : (name.rfind(query, 0) == 0 ? 1 : 2);
        hits.push_back({idx, rank});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Ranked& a, const Ranked& b) {
        return a.rank < b.rank;
    });
    std::vector<MediaItem> res;
    res.reserve(hits.size());
    for (const auto& h : hits) res.push_back(folders[h.idx]);
    return res;
}

// --- Natural Case-Insensitive / Numeric Compare & Offline Folder Tree ---

inline int natural_compare(const std::string& a_raw, const std::string& b_raw) {
    std::string a = normalize_name(a_raw);
    std::string b = normalize_name(b_raw);
    size_t ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size()) {
        bool da = std::isdigit(static_cast<unsigned char>(a[ia])) != 0;
        bool db = std::isdigit(static_cast<unsigned char>(b[ib])) != 0;
        if (da && db) {
            uint64_t va = 0, vb = 0;
            while (ia < a.size() && std::isdigit(static_cast<unsigned char>(a[ia])))
                va = va * 10 + uint64_t(a[ia++] - '0');
            while (ib < b.size() && std::isdigit(static_cast<unsigned char>(b[ib])))
                vb = vb * 10 + uint64_t(b[ib++] - '0');
            if (va != vb) return (va < vb) ? -1 : 1;
        } else {
            uint8_t ca = static_cast<uint8_t>(a[ia++]);
            uint8_t cb = static_cast<uint8_t>(b[ib++]);
            if (ca != cb) return (ca < cb) ? -1 : 1;
        }
    }
    if (a.size() == b.size()) return 0;
    return (a.size() < b.size()) ? -1 : 1;
}

inline std::string url_encode(const std::string& s) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(char(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0Fu]);
        }
    }
    return out;
}

struct FolderTreeNode {
    std::string id;
    std::string name;
    std::vector<FolderTreeNode> folders;
    std::vector<MediaItem> items;
};

inline std::vector<DownloadFolder> unknown_folder_path(const MediaItem& item, const std::string& unknown_label) {
    std::vector<DownloadFolder> path = {{"unknown", unknown_label}};
    if (!item.series_id.empty() && !item.series_name.empty()) {
        path.push_back({item.series_id, item.series_name});
        if (!item.season_name.empty()) {
            std::string sid = !item.season_id.empty()
                ? item.season_id
                : ("season-" + (item.parent_index_number > 0 ? std::to_string(item.parent_index_number) : item.season_name));
            path.push_back({sid, item.season_name});
        }
    } else if (!item.album_id.empty() && !item.album_name.empty()) {
        path.push_back({item.album_id, item.album_name});
    }
    return path;
}

inline void sort_folder_tree_node(FolderTreeNode& node) {
    std::sort(node.folders.begin(), node.folders.end(), [](const FolderTreeNode& a, const FolderTreeNode& b) {
        return natural_compare(a.name, b.name) < 0;
    });
    std::sort(node.items.begin(), node.items.end(), [](const MediaItem& a, const MediaItem& b) {
        return natural_compare(a.name, b.name) < 0;
    });
    for (auto& sub : node.folders) sort_folder_tree_node(sub);
}

inline FolderTreeNode build_download_folder_tree(
    const std::vector<DownloadedEntry>& downloads,
    const std::string& unknown_label
) {
    FolderTreeNode root{"", "", {}, {}};
    for (const auto& dl : downloads) {
        if (dl.item.id.empty()) continue;
        const std::vector<DownloadFolder> path =
            dl.has_folder_path && !dl.folder_path.empty()
                ? dl.folder_path
                : unknown_folder_path(dl.item, unknown_label);

        FolderTreeNode* cur = &root;
        for (const auto& f : path) {
            std::string cid = cur->id + ":" + url_encode(f.id);
            FolderTreeNode* found = nullptr;
            for (auto& child : cur->folders) {
                if (child.id == cid) {
                    found = &child;
                    break;
                }
            }
            if (!found) {
                cur->folders.push_back(FolderTreeNode{cid, f.name, {}, {}});
                found = &cur->folders.back();
            }
            cur = found;
        }
        cur->items.push_back(dl.item);
    }
    sort_folder_tree_node(root);
    return root;
}

inline const FolderTreeNode* find_tree_node(const FolderTreeNode& node, const std::string& target_id) {
    if (node.id == target_id) return &node;
    for (const auto& sub : node.folders) {
        if (const FolderTreeNode* hit = find_tree_node(sub, target_id)) return hit;
    }
    return nullptr;
}

inline void collect_downloaded_subfolders(const FolderTreeNode& folder, std::vector<MediaItem>& out) {
    for (const auto& sub : folder.folders) {
        MediaItem m;
        m.id = sub.id;
        m.name = sub.name;
        m.type = "Folder";
        m.is_folder = true;
        out.push_back(m);
        collect_downloaded_subfolders(sub, out);
    }
}

inline std::vector<MediaItem> get_downloaded_subfolders(const FolderTreeNode& folder) {
    std::vector<MediaItem> out;
    collect_downloaded_subfolders(folder, out);
    return out;
}

// --- SRT & WebVTT Subtitle Parser ---

struct SubtitleCue {
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::string text;
};

inline bool parse_timestamp_ms(const std::string& tok, int64_t* out_ms) {
    int p1 = 0, p2 = 0, p3 = 0, ms = 0;
    char sep = 0;
    // HH:MM:SS,mmm or HH:MM:SS.mmm
    if (std::sscanf(tok.c_str(), "%d:%d:%d%c%d", &p1, &p2, &p3, &sep, &ms) == 5 && (sep == ',' || sep == '.')) {
        while (ms > 0 && ms < 100 && tok.size() >= 2 && tok.back() != '0') break;
        size_t dot = tok.find_last_of(",.");
        int frac_len = (dot != std::string::npos) ? int(tok.size() - dot - 1) : 3;
        while (frac_len > 3) { ms /= 10; --frac_len; }
        while (frac_len < 3) { ms *= 10; ++frac_len; }
        *out_ms = ((int64_t(p1) * 3600 + int64_t(p2) * 60 + int64_t(p3)) * 1000LL) + ms;
        return true;
    }
    // MM:SS.mmm or MM:SS,mmm (short WebVTT format)
    if (std::sscanf(tok.c_str(), "%d:%d%c%d", &p1, &p2, &sep, &ms) == 4 && (sep == ',' || sep == '.')) {
        size_t dot = tok.find_last_of(",.");
        int frac_len = (dot != std::string::npos) ? int(tok.size() - dot - 1) : 3;
        while (frac_len > 3) { ms /= 10; --frac_len; }
        while (frac_len < 3) { ms *= 10; ++frac_len; }
        *out_ms = ((int64_t(p1) * 60 + int64_t(p2)) * 1000LL) + ms;
        return true;
    }
    return false;
}

inline std::string strip_subtitle_tags(const std::string& line) {
    std::string out;
    out.reserve(line.size());
    bool in_html = false;
    bool in_ass = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (!in_ass && c == '<') { in_html = true; continue; }
        if (in_html && c == '>') { in_html = false; continue; }
        if (!in_html && c == '{' && i + 1 < line.size() && line[i + 1] == '\\') { in_ass = true; continue; }
        if (in_ass && c == '}') { in_ass = false; continue; }
        if (!in_html && !in_ass) out.push_back(c);
    }
    return out;
}

inline std::vector<SubtitleCue> parse_subtitles(const std::string& raw) {
    std::vector<SubtitleCue> cues;
    size_t pos = 0;
    if (raw.size() >= 3 &&
        static_cast<uint8_t>(raw[0]) == 0xEFu &&
        static_cast<uint8_t>(raw[1]) == 0xBBu &&
        static_cast<uint8_t>(raw[2]) == 0xBFu) {
        pos = 3; // Skip UTF-8 BOM
    }

    auto next_line = [&](std::string& line) -> bool {
        if (pos >= raw.size()) return false;
        size_t nl = raw.find('\n', pos);
        if (nl == std::string::npos) {
            line = raw.substr(pos);
            pos = raw.size();
        } else {
            line = raw.substr(pos, nl - pos);
            pos = nl + 1;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    };

    std::string line;
    while (next_line(line)) {
        size_t arrow = line.find("-->");
        if (arrow == std::string::npos) continue;
        std::string left = line.substr(0, arrow);
        std::string right = line.substr(arrow + 3);
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t");
            if (a == std::string::npos) { s.clear(); return; }
            size_t b = s.find_last_not_of(" \t");
            s = s.substr(a, b - a + 1);
            size_t sp = s.find_first_of(" \t");
            if (sp != std::string::npos) s.resize(sp);
        };
        trim(left);
        trim(right);

        int64_t t0 = 0, t1 = 0;
        if (!parse_timestamp_ms(left, &t0) || !parse_timestamp_ms(right, &t1) || t1 <= t0) continue;

        std::string body;
        while (next_line(line) && !line.empty()) {
            std::string clean = strip_subtitle_tags(line);
            if (clean.empty()) continue;
            if (!body.empty()) body.push_back('\n');
            body += clean;
        }
        if (!body.empty()) cues.push_back({t0, t1, body});
    }
    return cues;
}

inline std::string active_subtitle_at(const std::vector<SubtitleCue>& cues, int64_t ms) {
    std::string combined;
    for (const auto& c : cues) {
        if (ms >= c.start_ms && ms <= c.end_ms) {
            if (!combined.empty()) combined.push_back('\n');
            combined += c.text;
        }
    }
    return combined;
}

// --- Trust-Boundary Validation & Jellyfin Headers ---

inline std::string normalize_server_url(std::string raw) {
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.erase(raw.begin());
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.pop_back();
    if (raw.empty()) return "";
    if (raw.rfind("http://", 0) != 0 && raw.rfind("https://", 0) != 0)
        raw = "http://" + raw;
    while (!raw.empty() && raw.back() == '/') raw.pop_back();
    return raw;
}

inline std::string safe_filename_id(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out.push_back(char(c));
        }
    }
    return out.empty() ? "item" : out;
}

inline std::string escape_header_quoted(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c != '"' && c != '\r' && c != '\n' && c != '\\') out.push_back(c);
    }
    return out;
}

inline std::string build_emby_auth_header(const std::string& device_id, const std::string& token) {
    std::string h = "MediaBrowser Client=\"Jellyfin_fpl\", Device=\"XGB35\", DeviceId=\"" +
                    escape_header_quoted(device_id.empty() ? "xgb35-fpl" : device_id) +
                    "\", Version=\"1.0.0\"";
    if (!token.empty())
        h += ", Token=\"" + escape_header_quoted(token) + "\"";
    return h;
}

inline bool lang_matches(std::string stream_lang, const std::string& pref) {
    if (pref.empty()) return false;
    for (char& c : stream_lang) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (stream_lang == pref) return true;
    if (pref == "rus" && (stream_lang == "ru" || stream_lang == "rus" || stream_lang == "russian")) return true;
    if (pref == "eng" && (stream_lang == "en" || stream_lang == "eng" || stream_lang == "english")) return true;
    return false;
}

inline int pick_default_stream(const MediaItem& item, const char* kind, const std::string& pref_lang, bool allow_off) {
    int first_idx = -1;
    int default_idx = -1;
    for (const auto& s : item.streams) {
        if (s.type != kind) continue;
        if (first_idx < 0) first_idx = s.index;
        if (s.is_default && default_idx < 0) default_idx = s.index;
        if (!pref_lang.empty() && lang_matches(s.language, pref_lang)) return s.index;
    }
    if (allow_off && pref_lang.empty()) return -1;
    return (default_idx >= 0) ? default_idx : first_idx;
}

} // namespace jf
