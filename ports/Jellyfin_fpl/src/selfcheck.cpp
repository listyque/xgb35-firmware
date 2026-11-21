// Host self-check suite for Jellyfin_fpl core logic (folders, downloads tree, subtitles, security).
#include "core.h"
#include "i18n.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace jf;

    // 1. UTF-8 Cyrillic/Latin normalization & folder search ranking (mirrors utils/library/folders.test.ts)
    assert(normalize_name("  Ёжик в тумане! (1975) ") == "ежик в тумане 1975");
    assert(normalize_name("ПРИКЛЮЧЕНИЯ Шерлока") == "приключения шерлока");

    std::vector<MediaItem> folders(4);
    folders[0].id = "1"; folders[0].name = "Мои сериалы — Сезон 1"; folders[0].type = "Folder";
    folders[1].id = "2"; folders[1].name = "Сезон 1";               folders[1].type = "Season";
    folders[2].id = "3"; folders[2].name = "Сезон 10: Финал";       folders[2].type = "Season";
    folders[3].id = "4"; folders[3].name = "Чёрное зеркало";        folders[3].type = "Series";

    auto hits = find_folders(folders, "сезон 1");
    assert(hits.size() == 3);
    assert(hits[0].id == "2"); // Exact match rank 0
    assert(hits[1].id == "3"); // Prefix match rank 1
    assert(hits[2].id == "1"); // Contains match rank 2

    auto yo_hits = find_folders(folders, "черное");
    assert(yo_hits.size() == 1 && yo_hits[0].id == "4");

    // 2. Library filter & playable checks
    MediaItem lib_movies; lib_movies.id = "m"; lib_movies.collection_type = "movies";
    MediaItem lib_tv;     lib_tv.id = "ltv";   lib_tv.collection_type = "livetv";
    MediaItem lib_box;    lib_box.id = "bx";   lib_box.collection_type = "boxsets";
    assert(opens_as_folders(lib_movies));
    assert(!opens_as_folders(lib_tv));
    assert(!opens_as_folders(lib_box));
    assert(format_ticks(65LL * 10000000LL) == "1:05");
    assert(format_ticks(3661LL * 10000000LL) == "1:01:01");

    // 3. Offline Download Folder Tree + natural sort + unknown fallback
    DownloadedEntry d1, d2, d3;
    d1.item.id = "ep10"; d1.item.name = "Episode 10"; d1.item.type = "Episode";
    d1.has_folder_path = true;
    d1.folder_path = {{"lib1", "Сериалы"}, {"ser1", "Клиника"}, {"s1", "Сезон 1"}};

    d2.item.id = "ep2"; d2.item.name = "Episode 2"; d2.item.type = "Episode";
    d2.has_folder_path = true;
    d2.folder_path = {{"lib1", "Сериалы"}, {"ser1", "Клиника"}, {"s1", "Сезон 1"}};

    d3.item.id = "orphan"; d3.item.name = "Пилот"; d3.item.type = "Episode";
    d3.item.series_id = "s_unk"; d3.item.series_name = "Светлячок"; d3.item.season_name = "Сезон 1";
    d3.has_folder_path = false;

    auto tree = build_download_folder_tree({d1, d2, d3}, kStringsRu.unknown_folder);
    assert(tree.folders.size() == 2);
    const FolderTreeNode* s1 = find_tree_node(tree, ":lib1:ser1:s1");
    assert(s1 != nullptr && s1->items.size() == 2);
    assert(s1->items[0].id == "ep2" && s1->items[1].id == "ep10"); // Natural numeric order!
    auto subs = get_downloaded_subfolders(tree);
    assert(subs.size() >= 5);

    // 4. SRT & WebVTT UTF-8 subtitle parser
    const char* sample_vtt =
        "\xEF\xBB\xBFWEBVTT\r\n\r\n"
        "00:05.500 --> 00:08.200 align:middle\r\n"
        "<i>Привет, мир!</i>\r\n\r\n"
        "2\r\n"
        "00:01:10,050 --> 00:01:12,900\r\n"
        "{\\an8}Вторая строка\r\nПродолжение\r\n";
    auto cues = parse_subtitles(sample_vtt);
    assert(cues.size() == 2);
    assert(active_subtitle_at(cues, 6000) == "Привет, мир!");
    assert(active_subtitle_at(cues, 9000).empty());
    assert(active_subtitle_at(cues, 71000) == "Вторая строка\nПродолжение");

    // 5. Security & trust-boundary sanitization
    assert(normalize_server_url(" 192.168.1.5:8096/ ") == "http://192.168.1.5:8096");
    assert(normalize_server_url("https://jf.example.com///") == "https://jf.example.com");
    assert(safe_filename_id("../../etc/passwd") == "etcpasswd");
    std::string hdr = build_emby_auth_header("xgb\"35\r\nx", "tok123");
    assert(hdr.find('\r') == std::string::npos && hdr.find('\n') == std::string::npos);

    // 6. Stream language picker
    MediaItem movie;
    movie.streams = {
        {1, "Audio", "aac", "eng", "English", "English AAC", true, false, false},
        {2, "Audio", "ac3", "rus", "Дубляж", "Русский AC3", false, false, false},
        {3, "Subtitle", "subrip", "rus", "Full", "Русский SRT", false, true, true},
    };
    assert(pick_default_stream(movie, "Audio", "rus", false) == 2);
    assert(pick_default_stream(movie, "Subtitle", "", true) == -1);
    assert(pick_default_stream(movie, "Subtitle", "rus", true) == 3);

    std::printf("Jellyfin_fpl selfcheck OK (6/6 test groups passed)\n");
    return 0;
}
