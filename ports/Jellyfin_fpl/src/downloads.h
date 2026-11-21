#pragma once
// Offline Download Queue, Sidecar Artwork/Subtitles, and Folder-Tree Storage (mirrors OfflineDownloadsBrowser).

#include "core.h"
#include "jellyfin_client.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace jf {

class DownloadManager {
    JellyfinClient& client_;
    std::string db_path_;
    std::string dl_dir_;
    mutable std::mutex mu_;
    std::vector<DownloadedEntry> downloaded_;
    std::vector<QueueEntry> queue_;

    std::atomic<bool> stop_all_{false};
    std::atomic<bool> abort_current_{false};
    std::string active_id_;
    std::thread worker_;

public:
    std::string default_audio_lang = "rus";

    explicit DownloadManager(JellyfinClient& cl) : client_(cl) {}
    ~DownloadManager() { shutdown(); }

    void init(const std::string& data_dir, const std::string& downloads_dir, const std::string& def_aud) {
        ensure_dir(data_dir);
        ensure_dir(downloads_dir);
        db_path_ = data_dir + "/downloads.json";
        dl_dir_ = downloads_dir;
        default_audio_lang = def_aud;
        {
            std::lock_guard<std::mutex> lk(mu_);
            load_downloads_db(db_path_, downloaded_, queue_);
        }
        stop_all_.store(false);
        worker_ = std::thread([this]() { worker_loop(); });
    }

    void shutdown() {
        stop_all_.store(true);
        abort_current_.store(true);
        if (worker_.joinable()) worker_.join();
    }

    void snapshot(std::vector<DownloadedEntry>& dl_out, std::vector<QueueEntry>& q_out) const {
        std::lock_guard<std::mutex> lk(mu_);
        dl_out = downloaded_;
        q_out = queue_;
    }

    int item_state(const std::string& item_id) const {
        // 0 = none, 1 = queued/downloading/paused, 2 = downloaded
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& d : downloaded_)
            if (d.item.id == item_id) return 2;
        for (const auto& q : queue_)
            if (q.item.id == item_id) return 1;
        return 0;
    }

    bool lookup_downloaded(const std::string& item_id, DownloadedEntry& out) const {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& d : downloaded_) {
            if (d.item.id == item_id) {
                out = d;
                return true;
            }
        }
        return false;
    }

    int64_t total_disk_bytes() const {
        std::lock_guard<std::mutex> lk(mu_);
        int64_t sum = 0;
        for (const auto& d : downloaded_) sum += d.file_bytes;
        return sum;
    }

    int enqueue_items(const std::vector<MediaItem>& items, int64_t bitrate, bool original) {
        int added = 0;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (const auto& it : items) {
                if (!is_playable_item(it) || it.id.empty()) continue;
                bool exists = false;
                for (const auto& d : downloaded_) if (d.item.id == it.id) { exists = true; break; }
                for (const auto& q : queue_) if (q.item.id == it.id) { exists = true; break; }
                if (exists) continue;
                QueueEntry qe;
                qe.item = it;
                qe.max_bitrate = bitrate;
                qe.original = original;
                qe.status = QueueStatus::Queued;
                queue_.push_back(std::move(qe));
                ++added;
            }
            if (added > 0) save_downloads_db(db_path_, downloaded_, queue_);
        }
        return added;
    }

    void toggle_pause_or_retry(const std::string& item_id) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& q : queue_) {
            if (q.item.id != item_id) continue;
            if (q.status == QueueStatus::Downloading || q.status == QueueStatus::Queued) {
                q.status = QueueStatus::Paused;
                if (active_id_ == item_id) abort_current_.store(true);
            } else {
                q.status = QueueStatus::Queued;
                q.error_msg.clear();
            }
            save_downloads_db(db_path_, downloaded_, queue_);
            break;
        }
    }

    void cancel_queue(const std::string& item_id) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = queue_.begin(); it != queue_.end(); ++it) {
            if (it->item.id == item_id) {
                if (active_id_ == item_id) abort_current_.store(true);
                queue_.erase(it);
                save_downloads_db(db_path_, downloaded_, queue_);
                break;
            }
        }
    }

    void delete_downloaded(const std::string& item_id) {
        DownloadedEntry removed;
        bool found = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto it = downloaded_.begin(); it != downloaded_.end(); ++it) {
                if (it->item.id == item_id) {
                    removed = *it;
                    downloaded_.erase(it);
                    save_downloads_db(db_path_, downloaded_, queue_);
                    found = true;
                    break;
                }
            }
        }
        if (found) unlink_entry_files(removed);
    }

    void delete_all() {
        std::vector<DownloadedEntry> old_dl;
        {
            std::lock_guard<std::mutex> lk(mu_);
            abort_current_.store(true);
            old_dl = std::move(downloaded_);
            downloaded_.clear();
            queue_.clear();
            save_downloads_db(db_path_, downloaded_, queue_);
        }
        for (const auto& d : old_dl) unlink_entry_files(d);
    }

    void update_local_progress(const std::string& item_id, int64_t pos_ticks, bool played) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& d : downloaded_) {
            if (d.item.id == item_id) {
                d.item.position_ticks = pos_ticks;
                if (played) d.item.played = true;
                save_downloads_db(db_path_, downloaded_, queue_);
                break;
            }
        }
    }

private:
    static void unlink_entry_files(const DownloadedEntry& d) {
        if (!d.local_media_path.empty()) std::remove(d.local_media_path.c_str());
        if (!d.local_thumb_path.empty()) std::remove(d.local_thumb_path.c_str());
        for (const auto& kv : d.local_subs)
            if (!kv.second.empty()) std::remove(kv.second.c_str());
    }

    void backfill_missing_paths() {
        if (!client_.has_session()) return;
        std::vector<std::string> missing_ids;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (const auto& d : downloaded_)
                if (!d.has_folder_path && !d.item.id.empty()) missing_ids.push_back(d.item.id);
        }
        if (missing_ids.empty()) return;

        bool changed = false;
        for (const auto& id : missing_ids) {
            if (stop_all_.load()) break;
            std::vector<DownloadFolder> fp;
            if (client_.get_ancestors_folder_path(id, fp) && !fp.empty()) {
                std::lock_guard<std::mutex> lk(mu_);
                for (auto& d : downloaded_) {
                    if (d.item.id == id) {
                        d.folder_path = std::move(fp);
                        d.has_folder_path = true;
                        changed = true;
                        break;
                    }
                }
            }
        }
        if (changed) {
            std::lock_guard<std::mutex> lk(mu_);
            save_downloads_db(db_path_, downloaded_, queue_);
        }
    }

    void worker_loop() {
        double last_backfill_ms = -60000.0;
        while (!stop_all_.load(std::memory_order_relaxed)) {
            double now = monotonic_ms();
            if (now - last_backfill_ms > 30000.0) {
                last_backfill_ms = now;
                backfill_missing_paths();
            }

            QueueEntry task;
            bool has_task = false;
            if (client_.has_session()) {
                std::lock_guard<std::mutex> lk(mu_);
                for (auto& q : queue_) {
                    if (q.status == QueueStatus::Queued) {
                        q.status = QueueStatus::Downloading;
                        q.downloaded_bytes = 0;
                        active_id_ = q.item.id;
                        abort_current_.store(false);
                        task = q;
                        has_task = true;
                        break;
                    }
                }
            }

            if (!has_task) {
                usleep(250000);
                continue;
            }

            // 1. Resolve folder hierarchy for OfflineDownloadsBrowser folder tree
            if (!task.has_folder_path) {
                std::vector<DownloadFolder> fp;
                if (client_.get_ancestors_folder_path(task.item.id, fp) && !fp.empty()) {
                    task.folder_path = std::move(fp);
                    task.has_folder_path = true;
                }
            }

            // 2. Negotiate original or transcoded download stream
            NegotiatedStream neg;
            int aud_idx = pick_default_stream(task.item, "Audio", default_audio_lang, false);
            int64_t req_br = task.original ? 0 : task.max_bitrate;
            bool ok = client_.negotiate_stream(task.item, 0, req_br, aud_idx, -1, true, neg);

            std::string safe_id = safe_filename_id(task.item.id);
            std::string ext = safe_filename_id(neg.container.empty() ? (is_audio_only(task.item) ? "mp3" : "mp4") : neg.container);
            std::string media_path = dl_dir_ + "/" + safe_id + "." + ext;
            int64_t final_bytes = 0;
            std::string dl_err;

            if (ok && !abort_current_.load()) {
                ok = client_.download_to_file(
                    neg.url,
                    media_path,
                    abort_current_,
                    [this, id = task.item.id](int64_t dl_now, int64_t dl_tot) {
                        std::lock_guard<std::mutex> lk(mu_);
                        for (auto& q : queue_) {
                            if (q.item.id == id) {
                                q.downloaded_bytes = dl_now;
                                q.total_bytes = dl_tot;
                                break;
                            }
                        }
                    },
                    &final_bytes,
                    &dl_err
                );
                if (neg.is_transcoding && !neg.play_session_id.empty())
                    client_.stop_active_encoding(neg.play_session_id);
            } else if (!ok) {
                dl_err = "PlaybackInfo failed";
            }

            if (ok && !stop_all_.load()) {
                DownloadedEntry de;
                de.item = task.item;
                if (!neg.streams.empty()) de.item.streams = neg.streams;
                de.local_media_path = media_path;
                de.file_bytes = final_bytes;
                de.has_folder_path = task.has_folder_path;
                de.folder_path = task.folder_path;

                // Download thumbnail artwork for offline browsing
                std::string img_bytes;
                if (client_.fetch_thumbnail_bytes(de.item, img_bytes)) {
                    std::string tpath = dl_dir_ + "/" + safe_id + ".jpg";
                    if (write_file_atomic(tpath, img_bytes.data(), img_bytes.size()))
                        de.local_thumb_path = tpath;
                }

                // Download sidecar text subtitles (.vtt) for offline subtitle switching
                std::string msid = neg.media_source_id.empty() ? de.item.media_source_id : neg.media_source_id;
                for (const auto& st : de.item.streams) {
                    if (st.type == "Subtitle" && st.is_text_sub) {
                        std::string vtt = client_.fetch_subtitle_text(de.item.id, msid, st.index);
                        if (!vtt.empty()) {
                            std::string spath = dl_dir_ + "/" + safe_id + ".sub_" + std::to_string(st.index) + ".vtt";
                            if (write_file_atomic(spath, vtt.data(), vtt.size()))
                                de.local_subs[st.index] = spath;
                        }
                    }
                }

                std::lock_guard<std::mutex> lk(mu_);
                active_id_.clear();
                for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                    if (it->item.id == task.item.id) { queue_.erase(it); break; }
                }
                downloaded_.push_back(std::move(de));
                save_downloads_db(db_path_, downloaded_, queue_);
            } else {
                std::lock_guard<std::mutex> lk(mu_);
                active_id_.clear();
                for (auto& q : queue_) {
                    if (q.item.id == task.item.id) {
                        if (q.status != QueueStatus::Paused) {
                            q.status = QueueStatus::Failed;
                            q.error_msg = dl_err;
                        }
                        break;
                    }
                }
                save_downloads_db(db_path_, downloaded_, queue_);
            }
        }
    }
};

} // namespace jf
