// Jellyfin_fpl — Native Jellyfin Folder Player & Offline Downloads Port for XGB35 (X11 Desktop + 480x320 fb0)
// Ported from user's streamyfin_my UI (HomeFolders, FolderBrowser, OfflineDownloadsBrowser, Settings, Clean Player).

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "core.h"
#include "downloads.h"
#include "i18n.h"
#include "jellyfin_client.h"
#include "platform.h"
#include "player.h"
#include "ui_canvas.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

static volatile sig_atomic_t g_quit = 0;
static void on_signal(int) { g_quit = 1; }

namespace jf {

// --- Async Thumbnail Cache (RAM LRU, decodes online & offline thumbnails on 1 background worker) ---

class ThumbnailCache {
    JellyfinClient& client_;
    mutable std::mutex mu_;
    std::map<std::string, RgbaImage> cache_;
    std::deque<std::pair<MediaItem, std::string>> pending_; // (item, optional local_thumb_path)
    std::map<std::string, bool> requested_;
    std::atomic<bool> stop_{false};
    int thumb_w_ = 96, thumb_h_ = 54;
    std::thread worker_;

public:
    explicit ThumbnailCache(JellyfinClient& cl) : client_(cl) {}
    ~ThumbnailCache() { shutdown(); }

    void start(int tw, int th) {
        thumb_w_ = tw;
        thumb_h_ = th;
        stop_.store(false);
        worker_ = std::thread([this]() { loop(); });
    }

    void shutdown() {
        stop_.store(true);
        if (worker_.joinable()) worker_.join();
    }

    const RgbaImage* get_or_request(const MediaItem& item, const std::string& local_path = "") {
        if (item.id.empty()) return nullptr;
        std::lock_guard<std::mutex> lk(mu_);
        auto it = cache_.find(item.id);
        if (it != cache_.end()) return it->second.valid() ? &it->second : nullptr;
        if (local_path.empty() && item.primary_tag.empty() && item.thumb_tag.empty()) return nullptr;
        if (!requested_[item.id] && pending_.size() < 64) {
            requested_[item.id] = true;
            pending_.push_back({item, local_path});
        }
        return nullptr;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mu_);
        cache_.clear();
        pending_.clear();
        requested_.clear();
    }

private:
    void loop() {
        while (!stop_.load(std::memory_order_relaxed)) {
            std::pair<MediaItem, std::string> job;
            bool has_job = false;
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (!pending_.empty()) {
                    job = std::move(pending_.front());
                    pending_.pop_front();
                    has_job = true;
                }
            }
            if (!has_job) {
                usleep(40000);
                continue;
            }
            std::string raw;
            if (!job.second.empty()) {
                raw = read_file_text(job.second);
            } else if (client_.has_session()) {
                client_.fetch_thumbnail_bytes(job.first, raw);
            }
            RgbaImage img;
            if (!raw.empty()) decode_image_to_rgba(raw.data(), raw.size(), thumb_w_, thumb_h_, img);
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (cache_.size() > 240) cache_.clear();
                cache_[job.first.id] = std::move(img);
            }
        }
    }
};

// --- Main Application Controller ---

struct FolderCrumb {
    std::string id;
    std::string title;
};

struct AppController {
    std::string port_dir;
    std::string data_dir;
    std::string config_path;
    bool x11_mode = false;

    AppSettings settings;
    JellyfinClient client;
    AlsaAudioSink audio;
    DownloadManager downloads;
    ThumbnailCache thumbs;
    MediaPlayer player;
    FontRenderer font;
    VirtualKeyboard osk;

    // Top-level tabs: 0 = Folders, 1 = Downloads, 2 = Settings
    int active_tab = 0;
    int focus_idx = 0;
    int scroll_top = 0;
    std::string toast_msg;
    double toast_until_ms = 0;

    // Login & Quick Connect state
    std::string login_server;
    std::string login_user;
    std::string login_pass;
    std::string login_err;
    bool login_busy = false;
    bool qc_active = false;
    std::string qc_secret;
    std::string qc_code;
    double qc_next_poll_ms = 0;

    // HomeFolders + FolderBrowser state
    mutable std::mutex data_mu;
    bool server_online = false;
    bool loading_folders = false;
    bool folder_load_error = false;
    std::vector<MediaItem> libraries;
    MediaItem last_played_folder;
    bool has_last_played = false;

    std::vector<FolderCrumb> folder_stack;
    std::vector<MediaItem> folder_items;
    bool folder_has_more = false;
    bool folder_fetching_more = false;
    std::string folder_search;
    std::vector<MediaItem> folder_index_cache;
    std::string folder_index_scope = "__none__";
    bool loading_index = false;
    uint64_t nav_gen = 0;

    // OfflineDownloadsBrowser state
    bool dl_show_queue = false;
    bool dl_tab_initialized = false;
    std::vector<FolderCrumb> dl_folder_stack;
    std::string dl_search;

    // Settings QuickConnect authorize
    std::string qc_auth_code_msg;
    bool qc_auth_ok = false;

    AppController()
        : downloads(client), thumbs(client), player(client, audio) {}

    void show_toast(const std::string& msg) {
        toast_msg = msg;
        toast_until_ms = monotonic_ms() + 3200.0;
    }

    static std::string make_device_id() {
        std::random_device rd;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "xgb35-%08x%08x", rd(), rd());
        return buf;
    }

    bool init(const std::string& pdir, bool is_x11) {
        port_dir = pdir;
        x11_mode = is_x11;
        std::string storage_root = (::access("/mnt/sd", W_OK) == 0) ? "/mnt/sd/ports/Jellyfin_fpl" : port_dir;
        data_dir = storage_root + "/data";
        std::string dl_dir = storage_root + "/downloads";
        ensure_dir(data_dir);
        ensure_dir(dl_dir);
        config_path = data_dir + "/config.json";

        settings.device_id = make_device_id();
        load_app_settings(config_path, settings);
        save_app_settings(config_path, settings);

        client.ca_bundle_path = port_dir + "/share/certs/ca-certificates.crt";
        client.configure(settings);

        std::string reg_font = port_dir + "/share/fonts/DejaVuSans.ttf";
        std::string bold_font = port_dir + "/share/fonts/DejaVuSans-Bold.ttf";
        if (!font.init(reg_font, bold_font)) {
            std::fprintf(stderr, "Failed to initialize FreeType / DejaVuSans.ttf\n");
            return false;
        }

        downloads.init(data_dir, dl_dir, settings.default_audio_lang);
        thumbs.start(is_x11 ? 96 : 64, is_x11 ? 54 : 36);

        static DownloadedEntry tl_entry;
        player.lookup_offline_entry = [this](const std::string& item_id) -> const DownloadedEntry* {
            if (downloads.lookup_downloaded(item_id, tl_entry)) return &tl_entry;
            return nullptr;
        };
        player.on_progress_saved = [this](const std::string& item_id, int64_t pos_ticks, bool finished) {
            downloads.update_local_progress(item_id, pos_ticks, finished);
            std::lock_guard<std::mutex> lk(data_mu);
            for (auto& it : folder_items) {
                if (it.id == item_id) {
                    it.position_ticks = pos_ticks;
                    if (finished) it.played = true;
                }
            }
        };

        login_server = settings.server_url.empty() ? "http://192.168.1." : settings.server_url;
        login_user = settings.username;

        if (client.has_session()) {
            refresh_home_async();
        }
        return true;
    }

    void shutdown() {
        player.close();
        thumbs.shutdown();
        downloads.shutdown();
        audio.shutdown();
        save_app_settings(config_path, settings);
    }

    // --- Async Jellyfin Data Loaders ---

    void refresh_home_async() {
        uint64_t gen;
        {
            std::lock_guard<std::mutex> lk(data_mu);
            loading_folders = true;
            folder_load_error = false;
            gen = ++nav_gen;
        }
        std::thread([this, gen]() {
            std::vector<MediaItem> views;
            bool ok = client.get_views(views);
            MediaItem last_f;
            bool has_last = ok && client.get_last_played_folder(last_f);

            std::lock_guard<std::mutex> lk(data_mu);
            if (gen != nav_gen) return;
            server_online = ok;
            loading_folders = false;
            folder_load_error = !ok;
            if (ok) {
                libraries = std::move(views);
                last_played_folder = std::move(last_f);
                has_last_played = has_last;
            }
        }).detach();
    }

    void load_folder_async(const std::string& folder_id, bool append_page) {
        int start_idx = 0;
        uint64_t gen;
        {
            std::lock_guard<std::mutex> lk(data_mu);
            if (append_page) {
                if (folder_fetching_more || !folder_has_more) return;
                folder_fetching_more = true;
                start_idx = int(folder_items.size());
                gen = nav_gen;
            } else {
                loading_folders = true;
                folder_load_error = false;
                folder_items.clear();
                folder_has_more = false;
                gen = ++nav_gen;
            }
        }
        std::thread([this, folder_id, start_idx, append_page, gen]() {
            std::vector<MediaItem> page;
            const int kPageSize = 50;
            bool ok = client.get_folder_page(folder_id, start_idx, kPageSize, page);
            std::lock_guard<std::mutex> lk(data_mu);
            if (gen != nav_gen) return;
            loading_folders = false;
            folder_fetching_more = false;
            if (!ok) {
                if (!append_page) folder_load_error = true;
                return;
            }
            server_online = true;
            folder_has_more = (int(page.size()) == kPageSize);
            for (auto& m : page) folder_items.push_back(std::move(m));
        }).detach();
    }

    void ensure_folder_search_index_async(const std::string& scope_folder_id) {
        {
            std::lock_guard<std::mutex> lk(data_mu);
            if (loading_index || folder_index_scope == scope_folder_id) return;
            loading_index = true;
            folder_index_scope = scope_folder_id;
            folder_index_cache.clear();
        }
        std::thread([this, scope_folder_id]() {
            std::vector<MediaItem> idx;
            client.load_recursive_items(scope_folder_id, true, idx);
            std::lock_guard<std::mutex> lk(data_mu);
            loading_index = false;
            if (folder_index_scope == scope_folder_id)
                folder_index_cache = std::move(idx);
        }).detach();
    }

    void open_folder(const std::string& id, const std::string& title) {
        {
            std::lock_guard<std::mutex> lk(data_mu);
            folder_stack.push_back({id, title});
            folder_search.clear();
        }
        focus_idx = 0;
        scroll_top = 0;
        load_folder_async(id, false);
    }

    void pop_folder() {
        std::string parent_id;
        bool at_root = false;
        {
            std::lock_guard<std::mutex> lk(data_mu);
            if (folder_stack.empty()) return;
            folder_stack.pop_back();
            folder_search.clear();
            if (folder_stack.empty()) at_root = true;
            else parent_id = folder_stack.back().id;
        }
        focus_idx = 0;
        scroll_top = 0;
        if (at_root) refresh_home_async();
        else load_folder_async(parent_id, false);
    }

    void download_entire_folder_async(const std::string& folder_id) {
        const Strings& s = tr(settings.lang);
        show_toast(s.loading);
        int64_t br = settings.download_bitrate;
        bool orig = settings.download_original;
        std::thread([this, folder_id, br, orig]() {
            std::vector<MediaItem> items;
            if (client.load_recursive_items(folder_id, false, items)) {
                int n = downloads.enqueue_items(items, br, orig);
                char msg[96];
                std::snprintf(msg, sizeof(msg), tr(settings.lang).queue_tab_fmt, n);
                show_toast(msg);
            }
        }).detach();
    }

    void launch_playable_from_list(const std::vector<MediaItem>& list, size_t clicked_idx, bool offline) {
        MediaPlayer::PlaylistContext pl;
        pl.offline_mode = offline;
        for (size_t i = 0; i < list.size(); ++i) {
            if (is_playable_item(list[i])) {
                if (i == clicked_idx) pl.current_index = int(pl.items.size());
                pl.items.push_back(list[i]);
            }
        }
        if (!pl.items.empty())
            player.start_playlist(pl, settings, x11_mode);
    }

    // --- Quick Connect Polling ---

    void tick_quick_connect(double now_ms) {
        if (!qc_active || qc_secret.empty() || now_ms < qc_next_poll_ms) return;
        qc_next_poll_ms = now_ms + 2500.0;
        std::string srv = login_server;
        std::string sec = qc_secret;
        std::thread([this, srv, sec]() {
            AppSettings updated = settings;
            int r = client.qc_poll_and_finish(srv, sec, updated);
            std::lock_guard<std::mutex> lk(data_mu);
            if (!qc_active || qc_secret != sec) return;
            if (r == 1) {
                qc_active = false;
                settings = updated;
                save_app_settings(config_path, settings);
                focus_idx = 0;
                scroll_top = 0;
                std::thread([this]() { refresh_home_async(); }).detach();
            } else if (r < 0) {
                qc_active = false;
                login_err = tr(settings.lang).qc_authorize_fail;
            }
        }).detach();
    }

    // --- Interactive Item Row Model for Unified Navigation & Rendering ---

    struct UiEntry {
        enum Kind {
            SearchBox,
            ClearSearch,
            BackBtn,
            DownloadFolderBtn,
            LastPlayedCard,
            LibraryCard,
            MediaRow,
            RetryBtn,
            QueueToggle,
            SettingRow,
            LoginField,
            LoginBtn,
        } kind;
        std::string id;
        std::string title;
        std::string subtitle;
        MediaItem item;
        bool folder = false;
        int extra = 0;
    };

    std::vector<UiEntry> build_entries(const Strings& s) {
        std::vector<UiEntry> list;
        if (active_tab == 0) {
            if (!client.has_session()) {
                if (qc_active) {
                    list.push_back({UiEntry::LoginBtn, "qc_cancel", s.qc_cancel, "", {}, false, 3});
                } else {
                    list.push_back({UiEntry::LoginField, "srv", s.server_url, login_server, {}, false, 0});
                    list.push_back({UiEntry::LoginField, "usr", s.username, login_user, {}, false, 1});
                    list.push_back({UiEntry::LoginField, "pwd", s.password, std::string(login_pass.size(), '*'), {}, false, 2});
                    list.push_back({UiEntry::LoginBtn, "signin", s.sign_in, "", {}, false, 1});
                    list.push_back({UiEntry::LoginBtn, "qc", s.quick_connect_btn, "", {}, false, 2});
                }
                return list;
            }

            std::lock_guard<std::mutex> lk(data_mu);
            bool in_sub = !folder_stack.empty();
            bool searching = !folder_search.empty();

            if (in_sub) {
                list.push_back({UiEntry::BackBtn, "back", std::string("<- ") + s.back + " (" + folder_stack.back().title + ")", "", {}, false, 0});
            }
            list.push_back({UiEntry::SearchBox, "search", folder_search.empty() ? s.search_folders : folder_search, "", {}, false, 0});
            if (searching) {
                list.push_back({UiEntry::ClearSearch, "clear", s.clear_search, "", {}, false, 0});
                std::string scope = in_sub ? folder_stack.back().id : "";
                if (folder_index_scope != scope && !loading_index) {
                    std::thread([this, scope]() { ensure_folder_search_index_async(scope); }).detach();
                }
                auto hits = find_folders(folder_index_cache, folder_search);
                for (const auto& h : hits) {
                    list.push_back({UiEntry::MediaRow, h.id, h.name, "", h, true, 0});
                }
                return list;
            }

            if (!in_sub) {
                if (has_last_played && !last_played_folder.id.empty()) {
                    list.push_back({UiEntry::LastPlayedCard, last_played_folder.id, last_played_folder.name, s.last_played_folder, last_played_folder, true, 0});
                }
                for (const auto& lib : libraries) {
                    list.push_back({UiEntry::LibraryCard, lib.id, lib.name, "", lib, true, 0});
                }
                if (folder_load_error) {
                    list.push_back({UiEntry::RetryBtn, "retry_home", s.retry, s.server_unreachable, {}, false, 0});
                }
            } else {
                list.push_back({UiEntry::DownloadFolderBtn, folder_stack.back().id, s.download_folder, "", {}, false, 0});
                for (size_t i = 0; i < folder_items.size(); ++i) {
                    const auto& it = folder_items[i];
                    bool fld = it.is_folder || is_folder_type(it.type);
                    list.push_back({UiEntry::MediaRow, it.id, it.name, "", it, fld, int(i)});
                }
                if (folder_load_error) {
                    list.push_back({UiEntry::RetryBtn, "retry_fld", s.retry, "", {}, false, 1});
                }
            }
        } else if (active_tab == 1) {
            std::vector<DownloadedEntry> dl_items;
            std::vector<QueueEntry> q_items;
            downloads.snapshot(dl_items, q_items);
            if (!dl_tab_initialized) {
                dl_show_queue = !q_items.empty();
                dl_tab_initialized = true;
            }

            if (dl_folder_stack.empty()) {
                char q_lbl[64];
                std::snprintf(q_lbl, sizeof(q_lbl), s.queue_tab_fmt, int(q_items.size()));
                list.push_back({UiEntry::QueueToggle, "q_tab", q_lbl, s.downloaded_tab, {}, false, dl_show_queue ? 0 : 1});
            } else {
                list.push_back({UiEntry::BackBtn, "dl_back", std::string("<- ") + s.back + " (" + dl_folder_stack.back().title + ")", "", {}, false, 1});
            }

            if (dl_show_queue && dl_folder_stack.empty()) {
                for (const auto& q : q_items) {
                    std::string st = (q.status == QueueStatus::Paused) ? s.status_paused
                                   : (q.status == QueueStatus::Failed) ? (std::string(s.status_failed) + (q.error_msg.empty() ? "" : (": " + q.error_msg)))
                                   : (q.status == QueueStatus::Downloading) ? s.status_downloading
                                   : s.status_queued;
                    if (q.downloaded_bytes > 0) {
                        st += " · " + format_bytes(q.downloaded_bytes);
                        if (q.total_bytes > 0) st += " / " + format_bytes(q.total_bytes);
                    }
                    list.push_back({UiEntry::MediaRow, q.item.id, q.item.name, st, q.item, false, -2});
                }
            } else {
                list.push_back({UiEntry::SearchBox, "dl_search", dl_search.empty() ? s.search_folders : dl_search, "", {}, false, 1});
                if (!dl_search.empty())
                    list.push_back({UiEntry::ClearSearch, "dl_clear", s.clear_search, "", {}, false, 1});

                auto tree = build_download_folder_tree(dl_items, s.unknown_folder);
                std::string cur_id = dl_folder_stack.empty() ? "" : dl_folder_stack.back().id;
                const FolderTreeNode* cur = find_tree_node(tree, cur_id);
                if (cur) {
                    if (!dl_search.empty()) {
                        auto subf = get_downloaded_subfolders(*cur);
                        for (const auto& h : find_folders(subf, dl_search))
                            list.push_back({UiEntry::MediaRow, h.id, h.name, "", h, true, -1});
                    } else {
                        for (const auto& f : cur->folders) {
                            MediaItem fm; fm.id = f.id; fm.name = f.name; fm.type = "Folder"; fm.is_folder = true;
                            list.push_back({UiEntry::MediaRow, fm.id, fm.name, "", fm, true, -1});
                        }
                        for (size_t i = 0; i < cur->items.size(); ++i) {
                            list.push_back({UiEntry::MediaRow, cur->items[i].id, cur->items[i].name, "", cur->items[i], false, int(i)});
                        }
                    }
                }
            }
        } else {
            // Settings tab
            bool is_en = (settings.lang == "en");
            const auto& s_br = get_bitrate_preset(settings.stream_bitrate);
            const auto& d_br = get_bitrate_preset(settings.download_bitrate);

            if (client.has_session()) {
                std::string who = settings.username + " @ " + (settings.server_name.empty() ? settings.server_url : settings.server_name);
                list.push_back({UiEntry::SettingRow, "logout", s.log_out, who, {}, false, 0});
                list.push_back({UiEntry::SettingRow, "qc_auth", s.qc_authorize_btn, qc_auth_code_msg.empty() ? s.qc_authorize_title : qc_auth_code_msg, {}, false, 1});
            }
            list.push_back({UiEntry::SettingRow, "s_br", s.stream_quality, is_en ? s_br.label_en : s_br.label_ru, {}, false, 2});
            list.push_back({UiEntry::SettingRow, "d_br", s.download_quality, is_en ? d_br.label_en : d_br.label_ru, {}, false, 3});
            list.push_back({UiEntry::SettingRow, "d_orig", s.download_original, settings.download_original ? s.yes_str : s.no_str, {}, false, 4});

            std::string aud_lbl = (settings.default_audio_lang == "eng") ? s.lang_en : s.lang_ru;
            std::string sub_lbl = settings.default_sub_lang.empty() ? s.lang_off
                                : (settings.default_sub_lang == "eng" ? s.lang_en : s.lang_ru);
            std::string out_lbl = (settings.audio_output == 1) ? s.audio_hdmi
                                : (settings.audio_output == 2 ? s.audio_speaker : s.audio_auto);

            list.push_back({UiEntry::SettingRow, "def_aud", s.default_audio, aud_lbl, {}, false, 5});
            list.push_back({UiEntry::SettingRow, "def_sub", s.default_subs, sub_lbl, {}, false, 6});
            list.push_back({UiEntry::SettingRow, "aud_out", s.audio_output, out_lbl, {}, false, 7});
            list.push_back({UiEntry::SettingRow, "ui_lang", s.app_language, is_en ? "English" : "Русский", {}, false, 8});
            list.push_back({UiEntry::SettingRow, "clear_dl", s.delete_all_downloads, format_bytes(downloads.total_disk_bytes()), {}, false, 9});
        }
        return list;
    }

    void activate_entry(const UiEntry& e, int sub_action = 0) {
        const Strings& s = tr(settings.lang);
        bool start_ru = (settings.lang != "en");

        switch (e.kind) {
        case UiEntry::LoginField:
            if (e.extra == 0) {
                osk.open(s.server_url, login_server, false, false, [this](const std::string& v) { login_server = v; });
            } else if (e.extra == 1) {
                osk.open(s.username, login_user, false, false, [this](const std::string& v) { login_user = v; });
            } else {
                osk.open(s.password, login_pass, true, false, [this](const std::string& v) { login_pass = v; });
            }
            break;

        case UiEntry::LoginBtn:
            if (e.extra == 1 && !login_busy) {
                login_busy = true;
                login_err.clear();
                std::string srv = login_server, usr = login_user, pwd = login_pass;
                std::thread([this, srv, usr, pwd]() {
                    AppSettings next = settings;
                    bool ok = client.login_password(srv, usr, pwd, next);
                    std::lock_guard<std::mutex> lk(data_mu);
                    login_busy = false;
                    if (ok) {
                        settings = next;
                        save_app_settings(config_path, settings);
                        focus_idx = 0;
                        scroll_top = 0;
                        std::thread([this]() { refresh_home_async(); }).detach();
                    } else {
                        login_err = tr(settings.lang).login_error;
                    }
                }).detach();
            } else if (e.extra == 2 && !login_busy) {
                login_busy = true;
                login_err.clear();
                std::string srv = login_server;
                std::thread([this, srv]() {
                    std::string sec, code;
                    bool ok = client.qc_initiate(srv, sec, code);
                    std::lock_guard<std::mutex> lk(data_mu);
                    login_busy = false;
                    if (ok) {
                        qc_secret = sec;
                        qc_code = code;
                        qc_active = true;
                        qc_next_poll_ms = monotonic_ms() + 2000.0;
                    } else {
                        login_err = tr(settings.lang).server_unreachable;
                    }
                }).detach();
            } else if (e.extra == 3) {
                qc_active = false;
                qc_secret.clear();
            }
            break;

        case UiEntry::SearchBox:
            if (e.extra == 0) {
                std::string cur;
                { std::lock_guard<std::mutex> lk(data_mu); cur = folder_search; }
                osk.open(s.search_folders, cur, false, start_ru, [this](const std::string& v) {
                    std::string scope;
                    {
                        std::lock_guard<std::mutex> lk(data_mu);
                        folder_search = v;
                        scope = folder_stack.empty() ? "" : folder_stack.back().id;
                    }
                    if (!v.empty()) ensure_folder_search_index_async(scope);
                });
            } else {
                osk.open(s.search_folders, dl_search, false, start_ru, [this](const std::string& v) {
                    dl_search = v;
                });
            }
            break;

        case UiEntry::ClearSearch:
            if (e.extra == 0) { std::lock_guard<std::mutex> lk(data_mu); folder_search.clear(); }
            else dl_search.clear();
            break;

        case UiEntry::BackBtn:
            if (e.extra == 0) pop_folder();
            else if (!dl_folder_stack.empty()) { dl_folder_stack.pop_back(); focus_idx = 0; scroll_top = 0; }
            break;

        case UiEntry::DownloadFolderBtn:
            download_entire_folder_async(e.id);
            break;

        case UiEntry::LastPlayedCard:
        case UiEntry::LibraryCard:
            open_folder(e.id, e.title);
            break;

        case UiEntry::RetryBtn:
            if (e.extra == 0) refresh_home_async();
            else {
                std::string fid;
                { std::lock_guard<std::mutex> lk(data_mu); if (!folder_stack.empty()) fid = folder_stack.back().id; }
                if (!fid.empty()) load_folder_async(fid, false);
            }
            break;

        case UiEntry::QueueToggle:
            dl_show_queue = !dl_show_queue;
            break;

        case UiEntry::MediaRow:
            if (active_tab == 0) {
                if (e.folder) {
                    open_folder(e.id, e.title);
                } else if (sub_action == 1) {
                    // X button or Download icon click -> queue single file download
                    int n = downloads.enqueue_items({e.item}, settings.download_bitrate, settings.download_original);
                    show_toast(n > 0 ? s.queued_badge : s.downloaded_badge);
                } else {
                    std::vector<MediaItem> copy_list;
                    { std::lock_guard<std::mutex> lk(data_mu); copy_list = folder_items; }
                    if (e.extra >= 0 && e.extra < int(copy_list.size()))
                        launch_playable_from_list(copy_list, size_t(e.extra), false);
                }
            } else if (active_tab == 1) {
                if (e.extra == -2) {
                    // Queue item: Confirm pauses/resumes, ActionX cancels
                    if (sub_action == 1) downloads.cancel_queue(e.id);
                    else downloads.toggle_pause_or_retry(e.id);
                } else if (e.folder) {
                    dl_folder_stack.push_back({e.id, e.title});
                    dl_search.clear();
                    focus_idx = 0;
                    scroll_top = 0;
                } else if (sub_action == 1) {
                    downloads.delete_downloaded(e.id);
                } else {
                    std::vector<DownloadedEntry> dl_items;
                    std::vector<QueueEntry> q_items;
                    downloads.snapshot(dl_items, q_items);
                    auto tree = build_download_folder_tree(dl_items, s.unknown_folder);
                    std::string cur_id = dl_folder_stack.empty() ? "" : dl_folder_stack.back().id;
                    if (const FolderTreeNode* cur = find_tree_node(tree, cur_id)) {
                        if (e.extra >= 0 && e.extra < int(cur->items.size()))
                            launch_playable_from_list(cur->items, size_t(e.extra), true);
                    }
                }
            }
            break;

        case UiEntry::SettingRow:
            cycle_setting(e.extra, 1);
            break;
        }
    }

    void cycle_setting(int which, int dir) {
        const Strings& s = tr(settings.lang);
        switch (which) {
        case 0: // Log out
            settings.access_token.clear();
            settings.user_id.clear();
            client.configure(settings);
            save_app_settings(config_path, settings);
            thumbs.clear();
            {
                std::lock_guard<std::mutex> lk(data_mu);
                folder_stack.clear();
                folder_items.clear();
                libraries.clear();
                has_last_played = false;
            }
            active_tab = 0;
            focus_idx = 0;
            break;
        case 1: // Quick Connect authorize another device
            osk.open(s.qc_authorize_title, "", false, false, [this](const std::string& code) {
                if (code.empty()) return;
                std::thread([this, code]() {
                    bool ok = client.qc_authorize(code);
                    std::lock_guard<std::mutex> lk(data_mu);
                    qc_auth_ok = ok;
                    qc_auth_code_msg = ok ? tr(settings.lang).qc_authorized_ok : tr(settings.lang).qc_authorize_fail;
                }).detach();
            });
            break;
        case 2: {
            int idx = (find_bitrate_preset_idx(settings.stream_bitrate) + dir + kBitratePresetCount) % kBitratePresetCount;
            settings.stream_bitrate = kBitratePresets[idx].bps;
            save_app_settings(config_path, settings);
            break;
        }
        case 3: {
            int idx = (find_bitrate_preset_idx(settings.download_bitrate) + dir + kBitratePresetCount) % kBitratePresetCount;
            settings.download_bitrate = kBitratePresets[idx].bps;
            save_app_settings(config_path, settings);
            break;
        }
        case 4:
            settings.download_original = !settings.download_original;
            save_app_settings(config_path, settings);
            break;
        case 5:
            settings.default_audio_lang = (settings.default_audio_lang == "rus") ? "eng" : "rus";
            downloads.default_audio_lang = settings.default_audio_lang;
            save_app_settings(config_path, settings);
            break;
        case 6:
            if (settings.default_sub_lang.empty()) settings.default_sub_lang = "rus";
            else if (settings.default_sub_lang == "rus") settings.default_sub_lang = "eng";
            else settings.default_sub_lang.clear();
            save_app_settings(config_path, settings);
            break;
        case 7:
            settings.audio_output = (settings.audio_output + dir + 3) % 3;
            audio.shutdown();
            save_app_settings(config_path, settings);
            break;
        case 8:
            settings.lang = (settings.lang == "ru") ? "en" : "ru";
            save_app_settings(config_path, settings);
            break;
        case 9:
            downloads.delete_all();
            show_toast(s.delete_all_downloads);
            break;
        }
    }

    void step(InputState& inp, Canvas& c, std::vector<HitZone>& hits) {
        double now = monotonic_ms();
        tick_quick_connect(now);

        const Strings& s = tr(settings.lang);

        // 1. Active Media Player takes input & renders full screen
        if (player.is_active()) {
            player.handle_input(inp, settings, s, hits);
            hits.clear();
            if (player.is_active()) {
                player.render(c, font, s, hits);
                if (inp.mouse_visible && !x11_mode) c.draw_cursor(inp.mouse_x, inp.mouse_y);
                return;
            }
        }

        // 2. Virtual Keyboard modal handling
        if (osk.active) {
            if (!inp.typed_utf8.empty()) osk.text += inp.typed_utf8;
            if (inp.backspace_pressed) pop_utf8_char(osk.text);
            for (NavAction a : inp.actions) {
                if (a == NavAction::Up) osk.move(-1, 0);
                else if (a == NavAction::Down) osk.move(1, 0);
                else if (a == NavAction::Left) osk.move(0, -1);
                else if (a == NavAction::Right) osk.move(0, 1);
                else if (a == NavAction::Confirm) osk.activate_selected();
                else if (a == NavAction::Back) osk.active = false;
                else if (a == NavAction::ActionX) pop_utf8_char(osk.text);
            }
            if (inp.rmb_clicked) osk.active = false;
            if (inp.lmb_clicked) {
                for (const auto& hz : hits) {
                    if (hz.id >= 10000 && hz.r.contains(inp.mouse_x, inp.mouse_y)) {
                        int rem = hz.id - 10000;
                        osk.sel_row = rem / 32;
                        osk.sel_col = rem % 32;
                        osk.activate_selected();
                        break;
                    }
                }
            }
            hits.clear();
            render_browser(c, s, hits);
            osk.render(c, font, s, hits);
            if (inp.mouse_visible && !x11_mode) c.draw_cursor(inp.mouse_x, inp.mouse_y);
            return;
        }

        // 3. Main Browser & Settings Input
        auto entries = build_entries(s);
        int cnt = int(entries.size());
        if (cnt > 0) focus_idx = std::max(0, std::min(cnt - 1, focus_idx));
        else focus_idx = 0;

        if (inp.wheel_delta != 0 && cnt > 0) {
            focus_idx = std::max(0, std::min(cnt - 1, focus_idx - inp.wheel_delta));
        }

        if (inp.rmb_clicked) {
            if (active_tab == 0 && !folder_stack.empty()) pop_folder();
            else if (active_tab == 1 && !dl_folder_stack.empty()) { dl_folder_stack.pop_back(); focus_idx = 0; }
        }

        if (inp.lmb_clicked) {
            for (const auto& hz : hits) {
                if (!hz.r.contains(inp.mouse_x, inp.mouse_y)) continue;
                if (hz.id >= 1 && hz.id <= 3) {
                    active_tab = hz.id - 1;
                    focus_idx = 0;
                    scroll_top = 0;
                    break;
                } else if (hz.id >= 1000 && hz.id < 3000) {
                    int idx = hz.id - 1000;
                    if (idx >= 0 && idx < cnt) {
                        focus_idx = idx;
                        activate_entry(entries[size_t(idx)], 0);
                    }
                    break;
                } else if (hz.id >= 3000 && hz.id < 5000) {
                    int idx = hz.id - 3000;
                    if (idx >= 0 && idx < cnt) {
                        focus_idx = idx;
                        activate_entry(entries[size_t(idx)], 1);
                    }
                    break;
                }
            }
        }

        for (NavAction a : inp.actions) {
            switch (a) {
            case NavAction::PrevTab:
                active_tab = (active_tab + 2) % 3;
                focus_idx = 0;
                scroll_top = 0;
                break;
            case NavAction::NextTab:
                active_tab = (active_tab + 1) % 3;
                focus_idx = 0;
                scroll_top = 0;
                break;
            case NavAction::Up:
                if (cnt > 0) focus_idx = std::max(0, focus_idx - 1);
                break;
            case NavAction::Down:
                if (cnt > 0) focus_idx = std::min(cnt - 1, focus_idx + 1);
                break;
            case NavAction::PageUp:
                if (cnt > 0) focus_idx = std::max(0, focus_idx - 6);
                break;
            case NavAction::PageDown:
                if (cnt > 0) focus_idx = std::min(cnt - 1, focus_idx + 6);
                break;
            case NavAction::Left:
            case NavAction::Right:
                if (cnt > 0 && entries[size_t(focus_idx)].kind == UiEntry::SettingRow && entries[size_t(focus_idx)].extra >= 2) {
                    cycle_setting(entries[size_t(focus_idx)].extra, (a == NavAction::Right) ? 1 : -1);
                } else if (cnt > 0 && entries[size_t(focus_idx)].kind == UiEntry::QueueToggle) {
                    dl_show_queue = !dl_show_queue;
                }
                break;
            case NavAction::Confirm:
                if (cnt > 0) activate_entry(entries[size_t(focus_idx)], 0);
                break;
            case NavAction::ActionX:
                if (cnt > 0) activate_entry(entries[size_t(focus_idx)], 1);
                break;
            case NavAction::ActionY:
                // Quick folder search shortcut from anywhere in Folders or Downloads
                for (int i = 0; i < cnt; ++i) {
                    if (entries[size_t(i)].kind == UiEntry::SearchBox) {
                        focus_idx = i;
                        activate_entry(entries[size_t(i)], 0);
                        break;
                    }
                }
                break;
            case NavAction::Back:
                if (active_tab == 0 && !folder_stack.empty()) pop_folder();
                else if (active_tab == 1 && !dl_folder_stack.empty()) { dl_folder_stack.pop_back(); focus_idx = 0; }
                break;
            default:
                break;
            }
        }

        // Auto-paginate FolderBrowser when scrolling near the end
        if (active_tab == 0) {
            std::string cur_fid;
            bool need_more = false;
            {
                std::lock_guard<std::mutex> lk(data_mu);
                if (!folder_stack.empty() && folder_search.empty() && folder_has_more && !folder_fetching_more &&
                    focus_idx + 8 >= int(folder_items.size())) {
                    cur_fid = folder_stack.back().id;
                    need_more = true;
                }
            }
            if (need_more && !cur_fid.empty()) load_folder_async(cur_fid, true);
        }

        hits.clear();
        render_browser(c, s, hits);
        if (inp.mouse_visible && !x11_mode) c.draw_cursor(inp.mouse_x, inp.mouse_y);
    }

    void render_browser(Canvas& c, const Strings& s, std::vector<HitZone>& hits) {
        c.clear(col::Bg);
        bool compact = (c.w < 640);
        int pad = compact ? 10 : 18;
        int top_h = compact ? 34 : 46;

        // Top Navigation Bar: [Папки] [Загрузки] [Настройки] + status
        c.fill_rect({0, 0, c.w, top_h}, col::TopBar);
        c.fill_rect({0, top_h - 1, c.w, 1}, col::Divider);

        const char* tab_names[3] = {s.tab_folders, s.tab_downloads, s.tab_settings};
        int tx = pad;
        for (int ti = 0; ti < 3; ++ti) {
            int tw = font.text_width(tab_names[ti], compact ? 13 : 16, ti == active_tab) + (compact ? 18 : 28);
            Rect tr_box{tx, 4, tw, top_h - 8};
            if (ti == active_tab)
                c.round_rect(tr_box, 8, col::FocusBg, col::FocusBorder);
            c.draw_text_centered(font, tab_names[ti], tr_box, compact ? 13 : 16,
                                 ti == active_tab ? col::TextMain : col::TextMuted, ti == active_tab);
            hits.push_back({1 + ti, tr_box});
            tx += tw + (compact ? 6 : 10);
        }

        if (!client.has_session() || !server_online) {
            int bw = font.text_width(s.offline_badge, compact ? 11 : 13) + 14;
            Rect badge{c.w - pad - bw, (top_h - (compact ? 20 : 24)) / 2, bw, compact ? 20 : 24};
            c.round_rect(badge, 6, rgba(60, 28, 32), col::TextRed);
            c.draw_text_centered(font, s.offline_badge, badge, compact ? 11 : 13, col::TextRed);
        }

        // Quick Connect Code Display Banner
        int body_y = top_h + (compact ? 6 : 12);
        if (active_tab == 0 && !client.has_session() && qc_active) {
            int box_w = std::min(c.w - pad * 2, 460);
            int bx = (c.w - box_w) / 2;
            Rect card{bx, body_y + 10, box_w, compact ? 150 : 190};
            c.round_rect(card, 12, col::Panel, col::FocusBorder);
            c.draw_text_centered(font, s.qc_waiting_hint, {card.x + 8, card.y + 14, card.w - 16, 26}, compact ? 12 : 15, col::TextMuted);
            c.draw_text_centered(font, qc_code, {card.x, card.y + (compact ? 48 : 62), card.w, 48}, compact ? 28 : 38, col::AccentSoft, true);

            Rect cancel_r{bx + (box_w - 140) / 2, card.y + card.h - (compact ? 38 : 46), 140, compact ? 30 : 36};
            c.round_rect(cancel_r, 8, col::FocusBg, col::FocusBorder);
            c.draw_text_centered(font, s.qc_cancel, cancel_r, compact ? 13 : 15, col::TextMain, true);
            hits.push_back({1000, cancel_r});
            return;
        }

        auto entries = build_entries(s);
        int cnt = int(entries.size());
        if (cnt > 0) focus_idx = std::max(0, std::min(cnt - 1, focus_idx));

        int row_h = compact ? 42 : 58;
        int avail_h = c.h - body_y - (compact ? 6 : 12);
        int vis_rows = std::max(1, avail_h / row_h);
        if (focus_idx < scroll_top) scroll_top = focus_idx;
        if (focus_idx >= scroll_top + vis_rows) scroll_top = focus_idx - vis_rows + 1;
        scroll_top = std::max(0, std::min(std::max(0, cnt - vis_rows), scroll_top));

        c.set_clip({0, body_y, c.w, avail_h});

        if (cnt == 0 || (active_tab == 0 && loading_folders && folder_items.empty() && !folder_stack.empty())) {
            const char* empty_txt = loading_folders ? s.loading
                                  : (!folder_search.empty() || !dl_search.empty()) ? s.no_matching_folders
                                  : (active_tab == 1 ? (dl_show_queue ? s.no_active_downloads : s.no_downloads_yet) : s.no_items);
            c.draw_text_centered(font, empty_txt, {pad, body_y + 24, c.w - pad * 2, 32}, compact ? 14 : 17, col::TextMuted);
        }

        int y = body_y;
        for (int idx = scroll_top; idx < cnt && y < body_y + avail_h; ++idx) {
            const auto& e = entries[size_t(idx)];
            bool sel = (idx == focus_idx);
            Rect r{pad, y, c.w - pad * 2, row_h - (compact ? 4 : 6)};

            if (e.kind == UiEntry::SearchBox) {
                c.round_rect(r, 10, sel ? col::FocusBg : col::Panel, sel ? col::FocusBorder : col::CardBorder);
                bool placeholder = (active_tab == 0 ? folder_search.empty() : dl_search.empty());
                c.draw_text(font, std::string("[?] ") + e.title, r.x + 12, r.y + (r.h - (compact ? 13 : 15)) / 2,
                            compact ? 13 : 15, placeholder ? col::TextMuted : col::TextMain, false, r.w - 24);
                hits.push_back({1000 + idx, r});
            } else if (e.kind == UiEntry::LastPlayedCard) {
                c.round_rect(r, 10, sel ? col::FocusBg : col::LastPlayedBg, sel ? col::FocusBorder : col::LastPlayedBd);
                c.draw_text(font, e.subtitle, r.x + 12, r.y + 4, compact ? 10 : 12, col::AccentSoft, false, r.w - 24);
                c.draw_text(font, e.title, r.x + 12, r.y + (compact ? 18 : 24), compact ? 13 : 16, col::TextMain, true, r.w - 36);
                c.draw_text(font, ">", r.x + r.w - 18, r.y + (r.h - 14) / 2, 14, col::TextMuted, true);
                hits.push_back({1000 + idx, r});
            } else if (e.kind == UiEntry::QueueToggle) {
                int half = (r.w - 8) / 2;
                Rect r_q{r.x, r.y, half, r.h};
                Rect r_d{r.x + half + 8, r.y, r.w - half - 8, r.h};
                c.round_rect(r_q, 8, dl_show_queue ? col::FocusBg : col::Card, (sel && dl_show_queue) ? col::FocusBorder : col::CardBorder);
                c.round_rect(r_d, 8, !dl_show_queue ? col::FocusBg : col::Card, (sel && !dl_show_queue) ? col::FocusBorder : col::CardBorder);
                c.draw_text_centered(font, e.title, r_q, compact ? 12 : 15, col::TextMain, dl_show_queue);
                c.draw_text_centered(font, e.subtitle, r_d, compact ? 12 : 15, col::TextMain, !dl_show_queue);
                hits.push_back({1000 + idx, r});
            } else if (e.kind == UiEntry::MediaRow) {
                c.round_rect(r, 8, sel ? col::FocusBg : col::Card, sel ? col::FocusBorder : col::Divider);
                int pw = compact ? 56 : 88;
                int ph = r.h - 8;
                Rect prev{r.x + 6, r.y + 4, pw, ph};
                c.round_rect(prev, 6, rgba(14, 14, 18));

                std::string loc_thumb;
                if (active_tab == 1 && !e.folder) {
                    DownloadedEntry de;
                    if (downloads.lookup_downloaded(e.id, de)) loc_thumb = de.local_thumb_path;
                }
                const RgbaImage* thumb = thumbs.get_or_request(e.item, loc_thumb);
                if (thumb && thumb->valid()) {
                    c.set_clip(prev);
                    c.blit_rgba(*thumb, prev.x + (prev.w - thumb->w) / 2, prev.y + (prev.h - thumb->h) / 2);
                    c.set_clip({0, body_y, c.w, avail_h});
                } else if (e.folder) {
                    c.draw_folder_icon(prev.x + prev.w / 2, prev.y + prev.h / 2, compact ? 18 : 24, col::AccentSoft);
                } else {
                    c.draw_play_icon(prev.x + prev.w / 2, prev.y + prev.h / 2, compact ? 14 : 18, col::TextMuted);
                }

                int text_x = prev.x + prev.w + 10;
                int right_reserve = e.folder ? 24 : (compact ? 62 : 96);
                int text_max_w = std::max(40, r.w - (text_x - r.x) - right_reserve);

                c.draw_text(font, e.title, text_x, r.y + (compact ? 4 : 7), compact ? 13 : 15, col::TextMain, true, text_max_w);

                // Status subtitle line (Watched / Stopped at / Duration / Queue progress)
                std::string meta = e.subtitle;
                uint32_t meta_col = col::TextMuted;
                if (meta.empty() && !e.folder) {
                    if (e.item.played) {
                        meta = s.watched;
                        meta_col = col::TextGreen;
                    } else if (e.item.position_ticks > 0) {
                        meta = std::string(s.stopped_at_prefix) + format_ticks(e.item.position_ticks);
                        meta_col = col::AccentSoft;
                    } else {
                        meta = s.unwatched;
                    }
                    if (e.item.runtime_ticks > 0) meta += " · " + format_ticks(e.item.runtime_ticks);
                }
                if (!meta.empty()) {
                    c.draw_text(font, meta, text_x, r.y + (compact ? 21 : 30), compact ? 11 : 12, meta_col, false, text_max_w);
                }

                if (e.folder) {
                    c.draw_text(font, ">", r.x + r.w - 18, r.y + (r.h - 14) / 2, 14, col::TextMuted, true);
                    hits.push_back({1000 + idx, r});
                } else {
                    int act_w = compact ? 50 : 80;
                    Rect act_r{r.x + r.w - act_w - 6, r.y + 6, act_w, r.h - 12};
                    std::string act_lbl;
                    uint32_t act_col = col::TextMain;
                    if (active_tab == 0) {
                        int dst = downloads.item_state(e.id);
                        act_lbl = (dst == 2) ? "OK" : (dst == 1 ? "..." : (compact ? "[v]" : s.download_btn));
                        act_col = (dst == 2) ? col::TextGreen : col::AccentSoft;
                    } else {
                        act_lbl = (e.extra == -2) ? "X" : (compact ? "[X]" : s.delete_btn);
                        act_col = col::TextRed;
                    }
                    c.round_rect(act_r, 6, col::Panel, col::CardBorder);
                    c.draw_text_centered(font, act_lbl, act_r, compact ? 10 : 12, act_col, true);
                    hits.push_back({1000 + idx, {r.x, r.y, r.w - act_w - 8, r.h}});
                    hits.push_back({3000 + idx, act_r});
                }
            } else if (e.kind == UiEntry::SettingRow || e.kind == UiEntry::LoginField) {
                c.round_rect(r, 8, sel ? col::FocusBg : col::Card, sel ? col::FocusBorder : col::CardBorder);
                c.draw_text(font, e.title, r.x + 12, r.y + (compact ? 4 : 7), compact ? 12 : 15,
                            (e.id == "logout" || e.id == "clear_dl") ? col::TextRed : col::TextMain, true, r.w - 24);
                c.draw_text(font, e.subtitle, r.x + 12, r.y + (compact ? 21 : 30), compact ? 11 : 13, col::AccentSoft, false, r.w - 24);
                hits.push_back({1000 + idx, r});
            } else {
                // Buttons / Library cards / Back / Download entire folder
                bool accent = (e.kind == UiEntry::DownloadFolderBtn || (e.kind == UiEntry::LoginBtn && e.extra == 1));
                uint32_t bg = accent ? (sel ? col::Accent : rgba(88, 28, 135)) : (sel ? col::FocusBg : col::Panel);
                c.round_rect(r, 8, bg, sel ? col::FocusBorder : col::CardBorder);
                if (e.kind == UiEntry::LibraryCard) {
                    c.draw_folder_icon(r.x + 20, r.y + r.h / 2, compact ? 16 : 20, col::AccentSoft);
                    c.draw_text(font, e.title, r.x + 36, r.y + (r.h - (compact ? 13 : 16)) / 2,
                                compact ? 13 : 16, col::TextMain, true, r.w - 56);
                    c.draw_text(font, ">", r.x + r.w - 18, r.y + (r.h - 14) / 2, 14, col::TextMuted, true);
                } else {
                    c.draw_text_centered(font, e.title, r, compact ? 13 : 15, col::TextMain, true);
                }
                hits.push_back({1000 + idx, r});
            }

            y += row_h;
        }

        if (active_tab == 0 && !client.has_session() && !login_err.empty()) {
            c.draw_text_centered(font, login_err, {pad, c.h - (compact ? 24 : 32), c.w - pad * 2, 22}, compact ? 11 : 14, col::TextRed, true);
        }

        c.reset_clip();

        // Transient toast banner
        if (!toast_msg.empty() && monotonic_ms() < toast_until_ms) {
            int tw = std::min(c.w - 24, font.text_width(toast_msg, compact ? 12 : 14, true) + 24);
            Rect tr_pill{(c.w - tw) / 2, c.h - (compact ? 32 : 42), tw, compact ? 24 : 30};
            c.round_rect(tr_pill, 8, rgba(35, 24, 48, 235), col::FocusBorder);
            c.draw_text_centered(font, toast_msg, tr_pill, compact ? 12 : 14, col::AccentSoft, true);
        }
    }
};

} // namespace jf

static std::string resolve_port_dir(const char* argv0) {
    char real_buf[1024] = {};
    if (argv0 && ::realpath(argv0, real_buf)) {
        std::string p(real_buf);
        size_t sl = p.find_last_of('/');
        if (sl != std::string::npos) {
            std::string bin_dir = p.substr(0, sl);
            size_t sl2 = bin_dir.find_last_of('/');
            if (sl2 != std::string::npos && bin_dir.substr(sl2 + 1) == "bin")
                return bin_dir.substr(0, sl2);
            return bin_dir;
        }
    }
    return ".";
}

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    bool force_fb = false;
    bool force_x11 = false;
    bool start_fs = false;
    bool headless_test = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fb") == 0) force_fb = true;
        else if (std::strcmp(argv[i], "--x11") == 0) force_x11 = true;
        else if (std::strcmp(argv[i], "--fullscreen") == 0) start_fs = true;
        else if (std::strcmp(argv[i], "--headless-test") == 0) headless_test = true;
    }

    std::string port_dir = resolve_port_dir(argc > 0 ? argv[0] : nullptr);

    jf::AppController app;
    jf::X11Api x11;
    Display* dpy = nullptr;
    if (!headless_test && !force_fb && (force_x11 || std::getenv("DISPLAY"))) {
        if (x11.load()) dpy = x11.OpenDisplay(nullptr);
    }

    if (!app.init(port_dir, dpy != nullptr)) {
        curl_global_cleanup();
        return 1;
    }

    if (headless_test) {
        jf::Canvas test_canvas;
        test_canvas.resize(480, 320);
        jf::InputState inp;
        std::vector<jf::HitZone> hits;
        app.step(inp, test_canvas, hits);
        app.shutdown();
        curl_global_cleanup();
        std::printf("Jellyfin_fpl ARMv7 headless-test OK (FreeType + FFmpeg + cURL initialized)\n");
        return 0;
    }

    jf::Canvas canvas;
    jf::InputState inp;
    std::vector<jf::HitZone> hits;

    if (dpy) {
        int scr = DefaultScreen(dpy);
        int sw = DisplayWidth(dpy, scr), sh = DisplayHeight(dpy, scr);
        int win_w = (sw >= 1280 && sh >= 720) ? 1024 : std::min(sw, 854);
        int win_h = (win_w == 1024) ? 600 : std::min(sh, 480);
        bool fullscreen = false;

        Window win = x11.CreateSimpleWindow(
            dpy, RootWindow(dpy, scr),
            std::max(0, (sw - win_w) / 2), std::max(0, (sh - win_h) / 2),
            unsigned(win_w), unsigned(win_h), 0, 0, 0
        );
        x11.StoreName(dpy, win, "Jellyfin FPL");
        x11.SelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | PointerMotionMask | StructureNotifyMask);
        Atom wm_del = x11.InternAtom(dpy, "WM_DELETE_WINDOW", False);
        x11.SetWMProtocols(dpy, win, &wm_del, 1);
        x11.MapWindow(dpy, win);

        auto toggle_fs = [&]() {
            fullscreen = !fullscreen;
            Atom wm_state = x11.InternAtom(dpy, "_NET_WM_STATE", False);
            Atom wm_fs = x11.InternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
            XEvent xev{};
            xev.type = ClientMessage;
            xev.xclient.window = win;
            xev.xclient.message_type = wm_state;
            xev.xclient.format = 32;
            xev.xclient.data.l[0] = fullscreen ? 1 : 0;
            xev.xclient.data.l[1] = long(wm_fs);
            x11.SendEvent(dpy, RootWindow(dpy, scr), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);
            if (fullscreen) x11.MoveResizeWindow(dpy, win, 0, 0, unsigned(sw), unsigned(sh));
            x11.Flush(dpy);
        };
        if (start_fs) toggle_fs();

        int depth = DefaultDepth(dpy, scr);
        Visual* vis = DefaultVisual(dpy, scr);
        GC gc = DefaultGC(dpy, scr);
        XImage* ximg = nullptr;
        XShmSegmentInfo shminfo{};
        bool using_shm = false;
        std::vector<uint8_t> xbuf;
        int img_w = 0, img_h = 0;

        auto destroy_ximg = [&]() {
            if (!ximg) return;
            if (using_shm) {
                x11.ShmDetach(dpy, &shminfo);
                x11.Sync(dpy, False);
                XDestroyImage(ximg);
                if (shminfo.shmaddr && shminfo.shmaddr != reinterpret_cast<char*>(-1))
                    shmdt(shminfo.shmaddr);
                using_shm = false;
            } else {
                ximg->data = nullptr;
                XDestroyImage(ximg);
            }
            ximg = nullptr;
        };

        auto ensure_ximg = [&](int w, int h) {
            w = std::max(320, w);
            h = std::max(240, h);
            if (ximg && w == img_w && h == img_h) return;
            destroy_ximg();
            img_w = w; img_h = h;
            canvas.resize(img_w, img_h);

            if (x11.has_shm && x11.ShmQueryExtension(dpy)) {
                std::memset(&shminfo, 0, sizeof(shminfo));
                ximg = x11.ShmCreateImage(dpy, vis, unsigned(depth), ZPixmap, nullptr, &shminfo, unsigned(img_w), unsigned(img_h));
                if (ximg) {
                    size_t bytes = size_t(ximg->bytes_per_line) * size_t(ximg->height);
                    shminfo.shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
                    if (shminfo.shmid >= 0) {
                        shminfo.shmaddr = static_cast<char*>(shmat(shminfo.shmid, nullptr, 0));
                        if (shminfo.shmaddr != reinterpret_cast<char*>(-1)) {
                            ximg->data = shminfo.shmaddr;
                            shminfo.readOnly = False;
                            if (x11.ShmAttach(dpy, &shminfo)) {
                                x11.Sync(dpy, False);
                                shmctl(shminfo.shmid, IPC_RMID, nullptr);
                                using_shm = true;
                            } else {
                                shmdt(shminfo.shmaddr);
                                shmctl(shminfo.shmid, IPC_RMID, nullptr);
                            }
                        } else shmctl(shminfo.shmid, IPC_RMID, nullptr);
                    }
                    if (!using_shm) { XDestroyImage(ximg); ximg = nullptr; }
                }
            }
            if (!using_shm) {
                int bpp = (depth <= 16) ? 2 : 4;
                xbuf.assign(size_t(img_w) * size_t(img_h) * size_t(bpp), 0);
                ximg = x11.CreateImage(dpy, vis, unsigned(depth), ZPixmap, 0,
                                       reinterpret_cast<char*>(xbuf.data()),
                                       unsigned(img_w), unsigned(img_h), bpp * 8, img_w * bpp);
            }
        };
        ensure_ximg(win_w, win_h);

        while (!g_quit) {
            double t0 = jf::monotonic_ms();
            inp.clear_frame();
            inp.mouse_visible = true;

            while (x11.Pending(dpy) > 0) {
                XEvent ev;
                x11.NextEvent(dpy, &ev);
                if (ev.type == ClientMessage && Atom(ev.xclient.data.l[0]) == wm_del) { g_quit = 1; break; }
                else if (ev.type == ConfigureNotify) {
                    ensure_ximg(ev.xconfigure.width, ev.xconfigure.height);
                } else if (ev.type == MotionNotify) {
                    inp.mouse_x = ev.xmotion.x;
                    inp.mouse_y = ev.xmotion.y;
                } else if (ev.type == ButtonPress) {
                    inp.mouse_x = ev.xbutton.x;
                    inp.mouse_y = ev.xbutton.y;
                    if (ev.xbutton.button == 1) inp.lmb_clicked = true;
                    else if (ev.xbutton.button == 3) inp.rmb_clicked = true;
                    else if (ev.xbutton.button == 4) inp.wheel_delta += 1;
                    else if (ev.xbutton.button == 5) inp.wheel_delta -= 1;
                } else if (ev.type == KeyPress) {
                    char ch[16] = {};
                    KeySym ks = NoSymbol;
                    int n = x11.LookupString(&ev.xkey, ch, sizeof(ch) - 1, &ks, nullptr);
                    if (ks == XK_F11) toggle_fs();
                    else if (ks == XK_Up) inp.actions.push_back(jf::NavAction::Up);
                    else if (ks == XK_Down) inp.actions.push_back(jf::NavAction::Down);
                    else if (ks == XK_Left) inp.actions.push_back(jf::NavAction::Left);
                    else if (ks == XK_Right) inp.actions.push_back(jf::NavAction::Right);
                    else if (ks == XK_Return || ks == XK_KP_Enter) inp.actions.push_back(jf::NavAction::Confirm);
                    else if (ks == XK_Escape) inp.actions.push_back(jf::NavAction::Back);
                    else if (ks == XK_Tab) inp.actions.push_back((ev.xkey.state & ShiftMask) ? jf::NavAction::PrevTab : jf::NavAction::NextTab);
                    else if (ks == XK_Page_Up) inp.actions.push_back(jf::NavAction::PageUp);
                    else if (ks == XK_Page_Down) inp.actions.push_back(jf::NavAction::PageDown);
                    else if (ks == XK_BackSpace) inp.backspace_pressed = true;
                    else if (ks == XK_Delete) inp.actions.push_back(jf::NavAction::ActionX);
                    else if (n > 0 && static_cast<unsigned char>(ch[0]) >= 32) {
                        inp.typed_utf8.append(ch, size_t(n));
                        if (ch[0] == ' ' && !app.osk.active)
                            inp.actions.push_back(jf::NavAction::PlayPause);
                    }
                }
            }

            app.step(inp, canvas, hits);

            if (ximg && ximg->data) {
                uint8_t* dst_base = reinterpret_cast<uint8_t*>(ximg->data);
                size_t pitch = size_t(ximg->bytes_per_line);
                if (ximg->bits_per_pixel == 16) {
                    for (int y = 0; y < img_h; ++y) {
                        jf::rgba_row_to_rgb565(canvas.px.data() + size_t(y) * size_t(img_w),
                                               reinterpret_cast<uint16_t*>(dst_base + size_t(y) * pitch), img_w);
                    }
                } else {
                    for (int y = 0; y < img_h; ++y) {
                        const uint32_t* srow = canvas.px.data() + size_t(y) * size_t(img_w);
                        uint32_t* drow = reinterpret_cast<uint32_t*>(dst_base + size_t(y) * pitch);
                        for (int x = 0; x < img_w; ++x) {
                            uint32_t p = srow[x];
                            drow[x] = ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu);
                        }
                    }
                }
                if (using_shm) {
                    x11.ShmPutImage(dpy, win, gc, ximg, 0, 0, 0, 0, unsigned(img_w), unsigned(img_h), False);
                    x11.Sync(dpy, False);
                } else {
                    x11.PutImage(dpy, win, gc, ximg, 0, 0, 0, 0, unsigned(img_w), unsigned(img_h));
                    x11.Flush(dpy);
                }
            }

            double dt = jf::monotonic_ms() - t0;
            double frame_budget = app.player.is_active() ? 16.0 : 25.0;
            if (dt < frame_budget) usleep(useconds_t((frame_budget - dt) * 1000.0));
        }

        destroy_ximg();
        x11.CloseDisplay(dpy);
    } else {
        // Handheld 480x320 /dev/fb0 mode (with ST7796S 320x480 hardware rotation support)
        int fb_fd = ::open("/dev/fb0", O_RDWR);
        if (fb_fd < 0) {
            std::perror("open /dev/fb0");
            app.shutdown();
            curl_global_cleanup();
            return 1;
        }
        struct fb_var_screeninfo v{};
        struct fb_fix_screeninfo f{};
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &v);
        ioctl(fb_fd, FBIOGET_FSCREENINFO, &f);
        ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
        uint8_t* fb_raw = static_cast<uint8_t*>(mmap(nullptr, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0));
        if (fb_raw == MAP_FAILED) {
            std::perror("mmap /dev/fb0");
            ::close(fb_fd);
            app.shutdown();
            curl_global_cleanup();
            return 1;
        }
        std::memset(fb_raw, 0, f.smem_len);

        bool rotate90 = (v.xres == 320 && v.yres == 480);
        int cw = rotate90 ? 480 : int(v.xres > 0 ? v.xres : 480);
        int ch = rotate90 ? 320 : int(v.yres > 0 ? v.yres : 320);
        canvas.resize(cw, ch);

        jf::EvdevHub evhub;
        evhub.open_all();

        std::vector<uint16_t> land565(size_t(cw) * size_t(ch));
        uint16_t* fb16 = reinterpret_cast<uint16_t*>(fb_raw);
        int stride16 = int(f.line_length) / 2;

        while (!g_quit) {
            double t0 = jf::monotonic_ms();
            inp.clear_frame();
            evhub.poll(inp, cw, ch, app.settings.hw_vol, t0);
            for (jf::NavAction a : inp.actions)
                if (a == jf::NavAction::Quit) g_quit = 1;

            app.step(inp, canvas, hits);

            for (int y = 0; y < ch; ++y)
                jf::rgba_row_to_rgb565(canvas.px.data() + size_t(y) * size_t(cw), land565.data() + size_t(y) * size_t(cw), cw);

            if (rotate90 && cw == 480 && ch == 320) {
                for (int X = 0; X < 480; ++X) {
                    uint16_t* dst = fb16 + X * stride16;
                    const uint16_t* col = land565.data() + X;
                    for (int Y = 0; Y < 320; ++Y)
                        dst[319 - Y] = col[Y * 480];
                }
            } else if (v.bits_per_pixel == 16) {
                int copy_w = std::min(cw, int(v.xres));
                int copy_h = std::min(ch, int(v.yres));
                for (int Y = 0; Y < copy_h; ++Y)
                    std::memcpy(fb16 + Y * stride16, land565.data() + size_t(Y) * size_t(cw), size_t(copy_w) * 2);
            } else if (v.bits_per_pixel == 32) {
                uint32_t* fb32 = reinterpret_cast<uint32_t*>(fb_raw);
                int stride32 = int(f.line_length) / 4;
                int copy_w = std::min(cw, int(v.xres));
                int copy_h = std::min(ch, int(v.yres));
                for (int Y = 0; Y < copy_h; ++Y) {
                    const uint32_t* srow = canvas.px.data() + size_t(Y) * size_t(cw);
                    uint32_t* drow = fb32 + Y * stride32;
                    for (int X = 0; X < copy_w; ++X) {
                        uint32_t p = srow[X];
                        drow[X] = 0xFF000000u | ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu);
                    }
                }
            }

            double dt = jf::monotonic_ms() - t0;
            double frame_budget = app.player.is_active() ? 20.0 : 28.0;
            if (dt < frame_budget) usleep(useconds_t((frame_budget - dt) * 1000.0));
        }

        evhub.close_all();
        std::memset(fb_raw, 0, f.smem_len);
        munmap(fb_raw, f.smem_len);
        ::close(fb_fd);
    }

    app.shutdown();
    curl_global_cleanup();
    return 0;
}
