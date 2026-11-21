#pragma once
// Clean FFmpeg Video + Audio Player (Direct Play, Jellyfin Transcoding, & Offline Files; zero touch bloat).

#include "core.h"
#include "i18n.h"
#include "jellyfin_client.h"
#include "platform.h"
#include "ui_canvas.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace jf {

class MediaPlayer {
public:
    struct PlaylistContext {
        std::vector<MediaItem> items;
        int current_index = 0;
        bool offline_mode = false;
    };

private:
    JellyfinClient& client_;
    AlsaAudioSink& audio_;
    CpuBoostGuard cpu_boost_;

    std::atomic<bool> active_{false};
    std::atomic<bool> stop_worker_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> buffering_{false};
    std::atomic<bool> finished_item_{false};
    std::atomic<bool> has_error_{false};
    std::atomic<int64_t> position_ticks_{0};
    std::atomic<int64_t> duration_ticks_{0};
    std::atomic<int64_t> seek_req_ticks_{-1};
    std::atomic<int> target_vw_{480};
    std::atomic<int> target_vh_{270};

    std::thread worker_;
    std::thread report_thread_;

    mutable std::mutex state_mu_;
    MediaItem current_item_;
    PlaylistContext playlist_;
    std::string local_path_;
    std::map<int, std::string> local_subs_;
    NegotiatedStream neg_;
    int64_t stream_bitrate_ = 2000000;
    int selected_audio_idx_ = -1;
    int selected_sub_idx_ = -1;
    int video_fit_ = 0;
    std::string error_text_;
    RgbaImage video_frame_;
    int src_aspect_w_ = 16;
    int src_aspect_h_ = 9;
    std::vector<SubtitleCue> sub_cues_;
    std::string embedded_sub_line_;
    int64_t embedded_sub_end_ms_ = 0;

public:
    // OSD UI state (focused button 0..9, auto-hide timer)
    bool osd_visible = true;
    double osd_last_activity_ms = 0;
    int osd_focus = 2; // Default on Play/Pause
    int modal_picker = 0; // 0 = none, 1 = Audio, 2 = Subtitles, 3 = Quality
    int picker_sel = 0;

    // Callback invoked on main thread when position/watched updates so UI & offline DB stay synced
    std::function<void(const std::string& item_id, int64_t pos_ticks, bool finished)> on_progress_saved;
    std::function<const DownloadedEntry*(const std::string& item_id)> lookup_offline_entry;

    MediaPlayer(JellyfinClient& cl, AlsaAudioSink& aud) : client_(cl), audio_(aud) {}
    ~MediaPlayer() { close(); }

    bool is_active() const { return active_.load(std::memory_order_acquire); }
    bool is_paused() const { return paused_.load(std::memory_order_relaxed); }

    void start_playlist(
        const PlaylistContext& pl,
        const AppSettings& settings,
        bool x11_active
    ) {
        if (pl.items.empty() || pl.current_index < 0 || pl.current_index >= int(pl.items.size())) return;
        close();
        if (!audio_.is_open()) audio_.start(settings.audio_output, x11_active);
        cpu_boost_.engage();

        {
            std::lock_guard<std::mutex> lk(state_mu_);
            playlist_ = pl;
            stream_bitrate_ = settings.stream_bitrate;
            video_fit_ = settings.video_fit;
        }
        open_index(pl.current_index, settings, -1, true, true);
    }

    void close() {
        bool was_active = active_.exchange(false);
        stop_worker_.store(true);
        if (worker_.joinable()) worker_.join();

        if (was_active) {
            MediaItem it;
            NegotiatedStream ns;
            std::string loc;
            int64_t pos = position_ticks_.load();
            int64_t dur = duration_ticks_.load();
            {
                std::lock_guard<std::mutex> lk(state_mu_);
                it = current_item_;
                ns = neg_;
                loc = local_path_;
            }
            bool completed = (dur > 0 && pos >= (dur * 92) / 100);
            if (on_progress_saved && !it.id.empty())
                on_progress_saved(it.id, completed ? 0 : pos, completed);

            if (loc.empty() && client_.has_session() && !it.id.empty()) {
                std::thread([cl = &client_, it_id = it.id, msid = ns.media_source_id, psid = ns.play_session_id,
                             is_tr = ns.is_transcoding, pos, completed]() {
                    cl->report_playback("Stopped", it_id, msid, psid, pos, false);
                    if (is_tr && !psid.empty()) cl->stop_active_encoding(psid);
                    if (completed) cl->set_played(it_id, true);
                }).detach();
            }
        }
        audio_.set_paused(true);
        audio_.flush();
        cpu_boost_.release();
    }

    void toggle_pause() {
        bool np = !paused_.load();
        paused_.store(np);
        audio_.set_paused(np);
        osd_touch();
    }

    void osd_touch() {
        osd_visible = true;
        osd_last_activity_ms = monotonic_ms();
    }

    void seek_to_ticks(int64_t new_ticks, const AppSettings& settings) {
        int64_t dur = duration_ticks_.load();
        if (dur > 0) new_ticks = std::max<int64_t>(0, std::min(dur - 10000000LL, new_ticks));
        else new_ticks = std::max<int64_t>(0, new_ticks);

        bool trans = false;
        int cur_idx = 0;
        {
            std::lock_guard<std::mutex> lk(state_mu_);
            trans = local_path_.empty() && neg_.is_transcoding;
            cur_idx = playlist_.current_index;
        }
        position_ticks_.store(new_ticks);
        osd_touch();
        if (trans) {
            open_index(cur_idx, settings, new_ticks, false, false);
        } else {
            seek_req_ticks_.store(new_ticks);
        }
    }

    void seek_relative_sec(int delta_sec, const AppSettings& settings) {
        seek_to_ticks(position_ticks_.load() + int64_t(delta_sec) * 10000000LL, settings);
    }

    void step_playlist(int delta, const AppSettings& settings) {
        int next_idx = -1;
        {
            std::lock_guard<std::mutex> lk(state_mu_);
            int cand = playlist_.current_index + delta;
            if (cand >= 0 && cand < int(playlist_.items.size()))
                next_idx = cand;
        }
        if (next_idx >= 0) open_index(next_idx, settings, 0, true, true);
    }

private:
    void load_external_subtitles_async(const MediaItem& item, const std::string& msid, int sub_idx, const std::map<int, std::string>& loc_subs) {
        {
            std::lock_guard<std::mutex> lk(state_mu_);
            sub_cues_.clear();
            embedded_sub_line_.clear();
        }
        if (sub_idx < 0) return;
        auto it_loc = loc_subs.find(sub_idx);
        if (it_loc != loc_subs.end() && !it_loc->second.empty()) {
            std::string txt = read_file_text(it_loc->second);
            auto cues = parse_subtitles(txt);
            std::lock_guard<std::mutex> lk(state_mu_);
            if (current_item_.id == item.id && selected_sub_idx_ == sub_idx)
                sub_cues_ = std::move(cues);
            return;
        }
        if (!client_.has_session()) return;
        std::string item_id = item.id;
        std::thread([this, item_id, msid, sub_idx]() {
            std::string txt = client_.fetch_subtitle_text(item_id, msid, sub_idx);
            if (txt.empty()) return;
            auto cues = parse_subtitles(txt);
            std::lock_guard<std::mutex> lk(state_mu_);
            if (current_item_.id == item_id && selected_sub_idx_ == sub_idx)
                sub_cues_ = std::move(cues);
        }).detach();
    }

    void open_index(
        int idx,
        const AppSettings& settings,
        int64_t override_ticks,
        bool pick_defaults,
        bool reset_frame
    ) {
        stop_worker_.store(true);
        if (worker_.joinable()) worker_.join();

        MediaItem item;
        std::string prev_psid;
        bool was_trans = false;
        std::string loc_media;
        std::map<int, std::string> loc_subs;
        int aud_idx = -1, sub_idx = -1;
        int64_t br = stream_bitrate_;

        {
            std::lock_guard<std::mutex> lk(state_mu_);
            if (idx < 0 || idx >= int(playlist_.items.size())) return;
            playlist_.current_index = idx;
            item = playlist_.items[size_t(idx)];
            prev_psid = neg_.play_session_id;
            was_trans = neg_.is_transcoding;
            neg_ = NegotiatedStream{};

            if (lookup_offline_entry) {
                if (const DownloadedEntry* dl = lookup_offline_entry(item.id)) {
                    loc_media = dl->local_media_path;
                    loc_subs = dl->local_subs;
                    if (item.streams.empty()) item.streams = dl->item.streams;
                    if (override_ticks < 0 && dl->item.position_ticks > 0)
                        item.position_ticks = dl->item.position_ticks;
                }
            }

            current_item_ = item;
            local_path_ = loc_media;
            local_subs_ = loc_subs;
            if (reset_frame) video_frame_ = RgbaImage{};
            error_text_.clear();

            if (pick_defaults) {
                selected_audio_idx_ = pick_default_stream(item, "Audio", settings.default_audio_lang, false);
                selected_sub_idx_ = pick_default_stream(item, "Subtitle", settings.default_sub_lang, true);
            }
            aud_idx = selected_audio_idx_;
            sub_idx = selected_sub_idx_;
            br = stream_bitrate_;
        }

        if (was_trans && !prev_psid.empty() && client_.has_session())
            client_.stop_active_encoding(prev_psid);

        int64_t start_ticks = (override_ticks >= 0) ? override_ticks : item.position_ticks;
        if (item.runtime_ticks > 0 && start_ticks >= (item.runtime_ticks * 95) / 100)
            start_ticks = 0;

        position_ticks_.store(start_ticks);
        duration_ticks_.store(item.runtime_ticks);
        seek_req_ticks_.store(-1);
        paused_.store(false);
        buffering_.store(true);
        finished_item_.store(false);
        has_error_.store(false);
        stop_worker_.store(false);
        active_.store(true);
        audio_.flush();
        audio_.set_paused(false);
        osd_touch();

        worker_ = std::thread([this, item, loc_media, loc_subs, start_ticks, br, aud_idx, sub_idx]() {
            worker_loop(item, loc_media, loc_subs, start_ticks, br, aud_idx, sub_idx);
        });
    }

    void worker_loop(
        MediaItem item,
        std::string loc_media,
        std::map<int, std::string> loc_subs,
        int64_t start_ticks,
        int64_t max_bitrate,
        int req_audio_idx,
        int req_sub_idx
    ) {
        std::string open_url = loc_media;
        NegotiatedStream neg;
        int64_t base_ticks = 0;

        if (open_url.empty()) {
            if (!client_.negotiate_stream(item, start_ticks, max_bitrate, req_audio_idx, req_sub_idx, false, neg)) {
                buffering_.store(false);
                has_error_.store(true);
                return;
            }
            open_url = neg.url;
            if (neg.is_transcoding) base_ticks = start_ticks;
            {
                std::lock_guard<std::mutex> lk(state_mu_);
                neg_ = neg;
                if (!neg.streams.empty()) current_item_.streams = neg.streams;
            }
            client_.report_playback("Start", item.id, neg.media_source_id, neg.play_session_id, start_ticks, false);
        }

        load_external_subtitles_async(item, neg.media_source_id.empty() ? item.media_source_id : neg.media_source_id, req_sub_idx, loc_subs);

        AVFormatContext* fmt = avformat_alloc_context();
        if (!fmt) { buffering_.store(false); has_error_.store(true); return; }
        fmt->interrupt_callback.callback = +[](void* opaque) -> int {
            return static_cast<std::atomic<bool>*>(opaque)->load(std::memory_order_relaxed) ? 1 : 0;
        };
        fmt->interrupt_callback.opaque = &stop_worker_;

        AVDictionary* opts = nullptr;
        if (loc_media.empty()) {
            std::string auth;
            {
                std::lock_guard<std::mutex> lk(client_.auth_mu);
                auth = "X-Emby-Authorization: " + build_emby_auth_header(client_.device_id, client_.token) + "\r\n";
            }
            av_dict_set(&opts, "headers", auth.c_str(), 0);
            av_dict_set(&opts, "reconnect", "1", 0);
            av_dict_set(&opts, "reconnect_streamed", "1", 0);
            av_dict_set(&opts, "reconnect_delay_max", "4", 0);
            av_dict_set(&opts, "rw_timeout", "15000000", 0);
        }

        if (avformat_open_input(&fmt, open_url.c_str(), nullptr, &opts) < 0) {
            av_dict_free(&opts);
            avformat_free_context(fmt);
            buffering_.store(false);
            has_error_.store(true);
            return;
        }
        av_dict_free(&opts);

        if (avformat_find_stream_info(fmt, nullptr) < 0) {
            avformat_close_input(&fmt);
            buffering_.store(false);
            has_error_.store(true);
            return;
        }

        if (duration_ticks_.load() <= 0 && fmt->duration > 0)
            duration_ticks_.store((fmt->duration * 10LL)); // AV_TIME_BASE (1e6) -> Jellyfin ticks (1e7)

        // Populate stream list from container if offline or missing metadata
        {
            std::lock_guard<std::mutex> lk(state_mu_);
            if (current_item_.streams.empty()) {
                for (unsigned i = 0; i < fmt->nb_streams; ++i) {
                    AVStream* st = fmt->streams[i];
                    MediaStreamInfo ms;
                    ms.index = int(i);
                    if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ms.type = "Audio";
                    else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) ms.type = "Subtitle";
                    else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ms.type = "Video";
                    else continue;
                    const AVCodecDescriptor* desc = avcodec_descriptor_get(st->codecpar->codec_id);
                    ms.codec = desc ? desc->name : "";
                    if (AVDictionaryEntry* lg = av_dict_get(st->metadata, "language", nullptr, 0)) ms.language = lg->value;
                    if (AVDictionaryEntry* ti = av_dict_get(st->metadata, "title", nullptr, 0)) ms.title = ti->value;
                    ms.is_default = (st->disposition & AV_DISPOSITION_DEFAULT) != 0;
                    ms.is_text_sub = (ms.type == "Subtitle") && is_text_subtitle_codec(ms.codec);
                    current_item_.streams.push_back(ms);
                }
            }
        }

        int vid_st = -1, aud_st = -1, emb_sub_st = -1;
        for (unsigned i = 0; i < fmt->nb_streams; ++i) {
            AVCodecParameters* cp = fmt->streams[i]->codecpar;
            if (cp->codec_type == AVMEDIA_TYPE_VIDEO && vid_st < 0 &&
                !(fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                vid_st = int(i);
            } else if (cp->codec_type == AVMEDIA_TYPE_AUDIO) {
                if (aud_st < 0 || int(i) == req_audio_idx) aud_st = int(i);
            } else if (cp->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                if (int(i) == req_sub_idx) emb_sub_st = int(i);
            }
        }

        AVCodecContext* vctx = nullptr;
        AVCodecContext* actx = nullptr;
        AVCodecContext* sctx = nullptr;

        if (vid_st >= 0) {
            const AVCodec* vdec = avcodec_find_decoder(fmt->streams[vid_st]->codecpar->codec_id);
            if (vdec) {
                vctx = avcodec_alloc_context3(vdec);
                avcodec_parameters_to_context(vctx, fmt->streams[vid_st]->codecpar);
                vctx->thread_count = 4; // Use all 4 Cortex-A5 cores on ATM7051H
                vctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
                if (avcodec_open2(vctx, vdec, nullptr) < 0) { avcodec_free_context(&vctx); vid_st = -1; }
                else if (vctx->width > 0 && vctx->height > 0) {
                    std::lock_guard<std::mutex> lk(state_mu_);
                    src_aspect_w_ = vctx->width;
                    src_aspect_h_ = vctx->height;
                }
            } else vid_st = -1;
        }

        if (aud_st >= 0) {
            const AVCodec* adec = avcodec_find_decoder(fmt->streams[aud_st]->codecpar->codec_id);
            if (adec) {
                actx = avcodec_alloc_context3(adec);
                avcodec_parameters_to_context(actx, fmt->streams[aud_st]->codecpar);
                if (avcodec_open2(actx, adec, nullptr) < 0) { avcodec_free_context(&actx); aud_st = -1; }
            } else aud_st = -1;
        }

        if (emb_sub_st >= 0) {
            const AVCodec* sdec = avcodec_find_decoder(fmt->streams[emb_sub_st]->codecpar->codec_id);
            if (sdec) {
                sctx = avcodec_alloc_context3(sdec);
                avcodec_parameters_to_context(sctx, fmt->streams[emb_sub_st]->codecpar);
                if (avcodec_open2(sctx, sdec, nullptr) < 0) { avcodec_free_context(&sctx); emb_sub_st = -1; }
            }
        }

        if (vid_st < 0 && aud_st < 0) {
            if (sctx) avcodec_free_context(&sctx);
            avformat_close_input(&fmt);
            buffering_.store(false);
            has_error_.store(true);
            return;
        }

        // Direct Play / Offline initial seek if resuming
        if (!neg.is_transcoding && start_ticks > 0) {
            int64_t target_av = start_ticks / 10LL;
            av_seek_frame(fmt, -1, target_av, AVSEEK_FLAG_BACKWARD);
            if (vctx) avcodec_flush_buffers(vctx);
            if (actx) avcodec_flush_buffers(actx);
        }

        SwrContext* swr = nullptr;
        unsigned out_rate = audio_.is_open() ? audio_.sample_rate() : 44100u;
        if (actx) {
            AVChannelLayout out_ch = AV_CHANNEL_LAYOUT_STEREO;
            AVChannelLayout in_ch = actx->ch_layout;
            if (in_ch.nb_channels <= 0) av_channel_layout_default(&in_ch, 2);
            swr_alloc_set_opts2(&swr, &out_ch, AV_SAMPLE_FMT_S16, int(out_rate),
                                &in_ch, actx->sample_fmt, actx->sample_rate, 0, nullptr);
            if (!swr || swr_init(swr) < 0) { if (swr) swr_free(&swr); }
        }

        SwsContext* sws = nullptr;
        int sws_sw = 0, sws_sh = 0, sws_dw = 0, sws_dh = 0;
        AVPixelFormat sws_fmt = AV_PIX_FMT_NONE;

        AVPacket* pkt = av_packet_alloc();
        AVFrame* frm = av_frame_alloc();
        std::vector<int16_t> pcm_buf(16384 * 2);

        double audio_pts_sec = double(start_ticks - base_ticks) / 10000000.0;
        double wall_ref_ms = monotonic_ms();
        double wall_pts_sec = audio_pts_sec;
        double last_report_ms = monotonic_ms();
        buffering_.store(false);

        auto master_pts_sec = [&]() -> double {
            if (actx && audio_.is_open()) {
                double q_sec = double(audio_.queued_frames()) / double(out_rate);
                return std::max(0.0, audio_pts_sec - q_sec);
            }
            return wall_pts_sec + (paused_.load() ? 0.0 : (monotonic_ms() - wall_ref_ms) / 1000.0);
        };

        bool eof_reached = false;
        while (!stop_worker_.load(std::memory_order_relaxed)) {
            int64_t sreq = seek_req_ticks_.exchange(-1);
            if (sreq >= 0 && !neg.is_transcoding) {
                int64_t target_av = sreq / 10LL;
                av_seek_frame(fmt, -1, target_av, AVSEEK_FLAG_BACKWARD);
                if (vctx) avcodec_flush_buffers(vctx);
                if (actx) avcodec_flush_buffers(actx);
                if (sctx) avcodec_flush_buffers(sctx);
                audio_.flush();
                audio_pts_sec = double(sreq) / 10000000.0;
                wall_pts_sec = audio_pts_sec;
                wall_ref_ms = monotonic_ms();
                eof_reached = false;
            }

            if (paused_.load(std::memory_order_relaxed)) {
                wall_pts_sec = master_pts_sec();
                wall_ref_ms = monotonic_ms();
                usleep(15000);
                continue;
            }

            // Throttle demux when audio ring buffer is nearly full
            if (actx && audio_.is_open() && audio_.queued_frames() > 44100) {
                usleep(8000);
                continue;
            }

            int rd = av_read_frame(fmt, pkt);
            if (rd < 0) {
                if (rd == AVERROR_EOF) {
                    // Wait for queued audio to drain before marking item finished
                    if (actx && audio_.is_open() && audio_.queued_frames() > 512) {
                        usleep(15000);
                        continue;
                    }
                    eof_reached = true;
                }
                break;
            }

            if (pkt->stream_index == aud_st && actx) {
                if (avcodec_send_packet(actx, pkt) == 0) {
                    while (avcodec_receive_frame(actx, frm) == 0) {
                        if (frm->best_effort_timestamp != AV_NOPTS_VALUE) {
                            audio_pts_sec = frm->best_effort_timestamp * av_q2d(fmt->streams[aud_st]->time_base);
                        }
                        if (swr) {
                            int max_out = swr_get_out_samples(swr, frm->nb_samples) + 64;
                            if (size_t(max_out) * 2 > pcm_buf.size()) pcm_buf.resize(size_t(max_out) * 2);
                            uint8_t* out_ptr[1] = {reinterpret_cast<uint8_t*>(pcm_buf.data())};
                            int conv = swr_convert(swr, out_ptr, max_out,
                                                   const_cast<const uint8_t**>(frm->extended_data), frm->nb_samples);
                            if (conv > 0) {
                                audio_pts_sec += double(conv) / double(out_rate);
                                if (audio_.is_open()) {
                                    size_t pushed = 0;
                                    while (pushed < size_t(conv) && !stop_worker_.load(std::memory_order_relaxed)) {
                                        pushed += audio_.push_s16_stereo(pcm_buf.data() + pushed * 2, size_t(conv) - pushed);
                                        if (pushed < size_t(conv)) usleep(4000);
                                    }
                                } else {
                                    usleep(useconds_t((double(conv) * 1000000.0) / double(out_rate)));
                                }
                            }
                        }
                        int64_t cur_t = base_ticks + int64_t(master_pts_sec() * 10000000.0);
                        position_ticks_.store(std::max<int64_t>(0, cur_t), std::memory_order_relaxed);
                    }
                }
            } else if (pkt->stream_index == vid_st && vctx) {
                // Drop non-ref decoding temporarily if lagging severely on heavy bitrates
                double lag_pre = master_pts_sec();
                if (avcodec_send_packet(vctx, pkt) == 0) {
                    while (avcodec_receive_frame(vctx, frm) == 0 && !stop_worker_.load(std::memory_order_relaxed)) {
                        double vpts = (frm->best_effort_timestamp != AV_NOPTS_VALUE)
                            ? (frm->best_effort_timestamp * av_q2d(fmt->streams[vid_st]->time_base))
                            : lag_pre;
                        if (!actx) {
                            double target_wait = vpts - master_pts_sec();
                            if (target_wait > 0.004 && target_wait < 0.5)
                                usleep(useconds_t(target_wait * 1000000.0));
                        } else {
                            double diff = vpts - master_pts_sec();
                            if (diff < -0.095) {
                                // Behind audio by >95ms: skip scaling/blitting this frame to keep A/V synced
                                vctx->skip_frame = (diff < -0.35) ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
                                continue;
                            }
                            vctx->skip_frame = AVDISCARD_DEFAULT;
                            while (diff > 0.012 && diff < 1.0 &&
                                   !stop_worker_.load(std::memory_order_relaxed) &&
                                   !paused_.load(std::memory_order_relaxed) &&
                                   seek_req_ticks_.load() < 0) {
                                usleep(useconds_t(std::min(diff, 0.015) * 1000000.0));
                                diff = vpts - master_pts_sec();
                            }
                        }

                        int dw = std::max(64, target_vw_.load(std::memory_order_relaxed));
                        int dh = std::max(36, target_vh_.load(std::memory_order_relaxed));
                        AVPixelFormat pfmt = static_cast<AVPixelFormat>(frm->format);
                        if (!sws || sws_sw != frm->width || sws_sh != frm->height ||
                            sws_dw != dw || sws_dh != dh || sws_fmt != pfmt) {
                            if (sws) sws_freeContext(sws);
                            sws_sw = frm->width; sws_sh = frm->height;
                            sws_dw = dw; sws_dh = dh; sws_fmt = pfmt;
                            sws = sws_getContext(sws_sw, sws_sh, sws_fmt, dw, dh, AV_PIX_FMT_RGBA,
                                                 SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                        }
                        if (sws) {
                            RgbaImage scaled;
                            scaled.w = dw;
                            scaled.h = dh;
                            scaled.px.resize(size_t(dw) * size_t(dh));
                            uint8_t* dplanes[4] = {reinterpret_cast<uint8_t*>(scaled.px.data()), nullptr, nullptr, nullptr};
                            int dlines[4] = {dw * 4, 0, 0, 0};
                            sws_scale(sws, frm->data, frm->linesize, 0, frm->height, dplanes, dlines);
                            std::lock_guard<std::mutex> lk(state_mu_);
                            src_aspect_w_ = frm->width;
                            src_aspect_h_ = frm->height;
                            video_frame_ = std::move(scaled);
                        }

                        int64_t cur_t = base_ticks + int64_t((actx ? master_pts_sec() : vpts) * 10000000.0);
                        position_ticks_.store(std::max<int64_t>(0, cur_t), std::memory_order_relaxed);
                    }
                }
            } else if (pkt->stream_index == emb_sub_st && sctx) {
                AVSubtitle sub{};
                int got_sub = 0;
                if (avcodec_decode_subtitle2(sctx, &sub, &got_sub, pkt) >= 0 && got_sub) {
                    std::string line;
                    for (unsigned r = 0; r < sub.num_rects; ++r) {
                        if (sub.rects[r]->ass) {
                            // Strip ASS dialogue prefix (9 commas)
                            const char* p = sub.rects[r]->ass;
                            int commas = 0;
                            while (*p && commas < 8) { if (*p++ == ',') ++commas; }
                            std::string clean = strip_subtitle_tags(p);
                            if (!clean.empty()) { if (!line.empty()) line += "\n"; line += clean; }
                        } else if (sub.rects[r]->text) {
                            std::string clean = strip_subtitle_tags(sub.rects[r]->text);
                            if (!line.empty()) line += "\n";
                            line += clean;
                        }
                    }
                    int64_t cur_ms = position_ticks_.load() / 10000LL;
                    int64_t end_ms = cur_ms + int64_t(sub.end_display_time > 0 ? sub.end_display_time : 4000);
                    {
                        std::lock_guard<std::mutex> lk(state_mu_);
                        embedded_sub_line_ = line;
                        embedded_sub_end_ms_ = end_ms;
                    }
                    avsubtitle_free(&sub);
                }
            }

            av_packet_unref(pkt);

            // Periodic 10s progress save & server report
            double now_ms = monotonic_ms();
            if (now_ms - last_report_ms >= 10000.0) {
                last_report_ms = now_ms;
                int64_t pos = position_ticks_.load();
                if (on_progress_saved) on_progress_saved(item.id, pos, false);
                if (loc_media.empty() && client_.has_session()) {
                    std::thread([cl = &client_, id = item.id, msid = neg.media_source_id,
                                 psid = neg.play_session_id, pos, ps = paused_.load()]() {
                        cl->report_playback("Progress", id, msid, psid, pos, ps);
                    }).detach();
                }
            }
        }

        av_frame_free(&frm);
        av_packet_free(&pkt);
        if (sws) sws_freeContext(sws);
        if (swr) swr_free(&swr);
        if (sctx) avcodec_free_context(&sctx);
        if (actx) avcodec_free_context(&actx);
        if (vctx) avcodec_free_context(&vctx);
        avformat_close_input(&fmt);

        if (eof_reached && !stop_worker_.load())
            finished_item_.store(true);
    }

public:
    void handle_input(InputState& inp, AppSettings& settings, const Strings& s, const std::vector<HitZone>& hits) {
        if (!is_active()) return;

        // Auto-advance to next playable item in folder when current finishes
        if (finished_item_.exchange(false)) {
            int next_idx = -1;
            std::string finished_id;
            {
                std::lock_guard<std::mutex> lk(state_mu_);
                finished_id = current_item_.id;
                if (playlist_.current_index + 1 < int(playlist_.items.size()))
                    next_idx = playlist_.current_index + 1;
            }
            if (on_progress_saved && !finished_id.empty())
                on_progress_saved(finished_id, 0, true);
            if (client_.has_session() && !finished_id.empty()) {
                std::thread([cl = &client_, finished_id]() { cl->set_played(finished_id, true); }).detach();
            }
            if (next_idx >= 0) {
                open_index(next_idx, settings, 0, true, true);
            } else {
                close();
            }
            return;
        }

        if (inp.lmb_clicked || inp.rmb_clicked || inp.wheel_delta != 0 || !inp.actions.empty())
            osd_touch();

        // Modal track/quality picker handling
        if (modal_picker != 0) {
            auto opts = current_picker_options(s);
            int cnt = std::max(1, int(opts.size()));
            for (NavAction a : inp.actions) {
                if (a == NavAction::Up) picker_sel = (picker_sel - 1 + cnt) % cnt;
                else if (a == NavAction::Down) picker_sel = (picker_sel + 1) % cnt;
                else if (a == NavAction::Back) { modal_picker = 0; return; }
                else if (a == NavAction::Confirm) { apply_picker_choice(picker_sel, settings); modal_picker = 0; return; }
            }
            if (inp.rmb_clicked) { modal_picker = 0; return; }
            if (inp.lmb_clicked) {
                for (const auto& hz : hits) {
                    if (hz.id >= 500 && hz.id < 600 && hz.r.contains(inp.mouse_x, inp.mouse_y)) {
                        apply_picker_choice(hz.id - 500, settings);
                        modal_picker = 0;
                        return;
                    }
                }
                modal_picker = 0;
            }
            return;
        }

        if (inp.rmb_clicked) {
            close();
            return;
        }

        if (inp.lmb_clicked) {
            for (const auto& hz : hits) {
                if (!hz.r.contains(inp.mouse_x, inp.mouse_y)) continue;
                if (hz.id == 200 && hz.r.w > 8) {
                    int64_t dur = duration_ticks_.load();
                    if (dur > 0) {
                        double frac = std::max(0.0, std::min(1.0, double(inp.mouse_x - hz.r.x) / double(hz.r.w)));
                        seek_to_ticks(int64_t(frac * double(dur)), settings);
                    }
                    return;
                }
                if (hz.id >= 100 && hz.id < 110) {
                    osd_focus = hz.id - 100;
                    trigger_osd_button(osd_focus, settings);
                    return;
                }
            }
            toggle_pause();
            return;
        }

        for (NavAction a : inp.actions) {
            switch (a) {
            case NavAction::PlayPause:
                toggle_pause();
                break;
            case NavAction::Left:
                if (osd_visible && osd_focus >= 0) osd_focus = (osd_focus + 9) % 10;
                else seek_relative_sec(-10, settings);
                break;
            case NavAction::Right:
                if (osd_visible && osd_focus >= 0) osd_focus = (osd_focus + 1) % 10;
                else seek_relative_sec(10, settings);
                break;
            case NavAction::Up:
                seek_relative_sec(10, settings);
                break;
            case NavAction::Down:
                seek_relative_sec(-10, settings);
                break;
            case NavAction::PrevTab:
                seek_relative_sec(-30, settings);
                break;
            case NavAction::NextTab:
                seek_relative_sec(30, settings);
                break;
            case NavAction::Confirm:
                trigger_osd_button(osd_focus, settings);
                break;
            case NavAction::Back:
                close();
                return;
            default:
                break;
            }
        }
    }

    struct PickerOpt { int val; std::string label; bool active; };

    std::vector<PickerOpt> current_picker_options(const Strings& s) const {
        std::lock_guard<std::mutex> lk(state_mu_);
        std::vector<PickerOpt> out;
        if (modal_picker == 1) {
            for (const auto& st : current_item_.streams) {
                if (st.type == "Audio")
                    out.push_back({st.index, st.label(), st.index == selected_audio_idx_});
            }
            if (out.empty()) out.push_back({-1, s.lang_off, true});
        } else if (modal_picker == 2) {
            out.push_back({-1, s.lang_off, selected_sub_idx_ < 0});
            for (const auto& st : current_item_.streams) {
                if (st.type == "Subtitle")
                    out.push_back({st.index, st.label(), st.index == selected_sub_idx_});
            }
        } else if (modal_picker == 3) {
            bool is_en = (std::strcmp(s.play, "Play") == 0);
            for (int i = 0; i < kBitratePresetCount; ++i) {
                const auto& bp = kBitratePresets[i];
                out.push_back({i, is_en ? bp.label_en : bp.label_ru, bp.bps == stream_bitrate_});
            }
        }
        return out;
    }

    void apply_picker_choice(int choice_idx, AppSettings& settings) {
        const Strings& s = tr(settings.lang);
        auto opts = current_picker_options(s);
        if (choice_idx < 0 || choice_idx >= int(opts.size())) return;
        int chosen = opts[size_t(choice_idx)].val;
        int cur_idx = 0;
        bool need_reopen = false;
        MediaItem item;
        std::string msid;
        std::map<int, std::string> lsubs;

        {
            std::lock_guard<std::mutex> lk(state_mu_);
            cur_idx = playlist_.current_index;
            item = current_item_;
            msid = neg_.media_source_id.empty() ? item.media_source_id : neg_.media_source_id;
            lsubs = local_subs_;

            if (modal_picker == 1 && chosen != selected_audio_idx_) {
                selected_audio_idx_ = chosen;
                need_reopen = true;
            } else if (modal_picker == 2 && chosen != selected_sub_idx_) {
                selected_sub_idx_ = chosen;
                // Check if chosen subtitle is bitmap-only requiring transcode burn-in
                bool is_text = (chosen < 0);
                for (const auto& st : item.streams)
                    if (st.index == chosen && st.is_text_sub) is_text = true;
                if (is_text) {
                    load_external_subtitles_async(item, msid, chosen, lsubs);
                } else {
                    need_reopen = true;
                }
            } else if (modal_picker == 3 && local_path_.empty()) {
                int64_t new_br = kBitratePresets[chosen].bps;
                if (new_br != stream_bitrate_) {
                    stream_bitrate_ = new_br;
                    settings.stream_bitrate = new_br;
                    need_reopen = true;
                }
            }
        }
        if (need_reopen)
            open_index(cur_idx, settings, position_ticks_.load(), false, false);
    }

    void trigger_osd_button(int btn, AppSettings& settings) {
        switch (btn) {
        case 0: step_playlist(-1, settings); break;
        case 1: seek_relative_sec(-10, settings); break;
        case 2: toggle_pause(); break;
        case 3: seek_relative_sec(10, settings); break;
        case 4: step_playlist(1, settings); break;
        case 5: modal_picker = 1; picker_sel = 0; break;
        case 6: modal_picker = 2; picker_sel = 0; break;
        case 7: modal_picker = 3; picker_sel = 0; break;
        case 8: {
            std::lock_guard<std::mutex> lk(state_mu_);
            video_fit_ = 1 - video_fit_;
            settings.video_fit = video_fit_;
            break;
        }
        case 9: close(); break;
        }
    }

    void render(Canvas& c, FontRenderer& font, const Strings& s, std::vector<HitZone>& hits) {
        c.clear(rgba(0, 0, 0));
        bool compact = (c.w < 640);

        MediaItem item;
        RgbaImage frame;
        int aw = 16, ah = 9, fit = 0;
        std::string sub_line;
        bool is_local = false;
        bool is_trans = false;
        int pl_idx = 0, pl_cnt = 1;

        int64_t pos_ticks = position_ticks_.load();
        int64_t dur_ticks = duration_ticks_.load();

        {
            std::lock_guard<std::mutex> lk(state_mu_);
            item = current_item_;
            frame = video_frame_;
            aw = std::max(1, src_aspect_w_);
            ah = std::max(1, src_aspect_h_);
            fit = video_fit_;
            is_local = !local_path_.empty();
            is_trans = neg_.is_transcoding;
            pl_idx = playlist_.current_index;
            pl_cnt = int(playlist_.items.size());

            int64_t cur_ms = pos_ticks / 10000LL;
            if (!sub_cues_.empty())
                sub_line = active_subtitle_at(sub_cues_, cur_ms);
            else if (!embedded_sub_line_.empty() && cur_ms <= embedded_sub_end_ms_)
                sub_line = embedded_sub_line_;
        }

        // Compute target video viewport rectangle (Fit vs Fill)
        int vw = c.w, vh = c.h;
        if (fit == 0) {
            if (c.w * ah > c.h * aw) { vh = c.h; vw = (c.h * aw) / ah; }
            else { vw = c.w; vh = (c.w * ah) / aw; }
        }
        vw = std::max(64, vw & ~1);
        vh = std::max(36, vh & ~1);
        target_vw_.store(vw, std::memory_order_relaxed);
        target_vh_.store(vh, std::memory_order_relaxed);

        if (frame.valid()) {
            c.blit_rgba(frame, (c.w - frame.w) / 2, (c.h - frame.h) / 2);
        } else if (is_audio_only(item)) {
            int card_w = std::min(c.w - 32, compact ? 380 : 540);
            int card_h = compact ? 110 : 150;
            Rect card{(c.w - card_w) / 2, (c.h - card_h) / 2 - 20, card_w, card_h};
            c.round_rect(card, 14, col::Panel, col::CardBorder);
            c.draw_text_centered(font, item.name, {card.x, card.y + 18, card.w, 34}, compact ? 15 : 20, col::TextMain, true);
            std::string sub_meta = !item.album_name.empty() ? item.album_name : item.series_name;
            if (!sub_meta.empty())
                c.draw_text_centered(font, sub_meta, {card.x, card.y + 58, card.w, 28}, compact ? 13 : 16, col::AccentSoft);
        }

        if (buffering_.load())
            c.draw_text_centered(font, s.buffering, {0, c.h / 2 - 16, c.w, 32}, compact ? 14 : 18, col::AccentSoft, true);
        if (has_error_.load())
            c.draw_text_centered(font, s.player_error, {0, c.h / 2 - 16, c.w, 32}, col::TextRed ? 15 : 18, col::TextRed, true);

        // Crisp subtitle pill overlay
        if (!sub_line.empty()) {
            int sub_fsz = compact ? 13 : 20;
            std::vector<std::string> lines;
            for (size_t p = 0; p < sub_line.size();) {
                size_t nl = sub_line.find('\n', p);
                if (nl == std::string::npos) { lines.push_back(sub_line.substr(p)); break; }
                lines.push_back(sub_line.substr(p, nl - p));
                p = nl + 1;
            }
            int bottom_y = c.h - (osd_visible ? (compact ? 74 : 98) : (compact ? 14 : 28));
            for (int li = int(lines.size()) - 1; li >= 0; --li) {
                int tw = std::min(c.w - 24, font.text_width(lines[size_t(li)], sub_fsz, true) + 18);
                Rect pill{(c.w - tw) / 2, bottom_y - (sub_fsz + 8), tw, sub_fsz + 8};
                c.round_rect(pill, 6, rgba(0, 0, 0, 195));
                c.draw_text_centered(font, lines[size_t(li)], pill, sub_fsz, rgba(255, 255, 255), true);
                bottom_y -= (sub_fsz + 10);
            }
        }

        // Auto-hide OSD after 4s during active video playback
        if (osd_visible && !paused_.load() && modal_picker == 0 && !is_audio_only(item) &&
            (monotonic_ms() - osd_last_activity_ms > 4000.0)) {
            osd_visible = false;
        }

        if (!osd_visible && modal_picker == 0) return;

        // Top info bar
        int top_h = compact ? 28 : 40;
        c.fill_rect({0, 0, c.w, top_h}, rgba(12, 12, 16, 210));
        std::string mode_tag = is_local ? ("[" + std::string(s.offline_badge) + "] ")
                             : (is_trans ? "[Transcode] " : "[Direct] ");
        std::string idx_tag = (pl_cnt > 1) ? (" (" + std::to_string(pl_idx + 1) + "/" + std::to_string(pl_cnt) + ")") : "";
        c.draw_text(font, mode_tag + item.name + idx_tag, 10, (top_h - (compact ? 13 : 16)) / 2,
                    compact ? 13 : 16, col::TextMain, true, c.w - 20);

        // Bottom transport panel
        int bot_h = compact ? 64 : 84;
        int by = c.h - bot_h;
        c.fill_rect({0, by, c.w, bot_h}, rgba(12, 12, 16, 222));

        // Progress bar + timestamps
        std::string t_cur = format_ticks(pos_ticks);
        std::string t_tot = format_ticks(dur_ticks);
        int t_fsz = compact ? 11 : 14;
        c.draw_text(font, t_cur, 10, by + 6, t_fsz, col::TextMain);
        int tot_w = font.text_width(t_tot, t_fsz) + 10;
        c.draw_text(font, t_tot, c.w - tot_w, by + 6, t_fsz, col::TextMuted);

        int bar_x = 10 + font.text_width(t_cur, t_fsz) + 8;
        int bar_w = std::max(40, (c.w - tot_w - 8) - bar_x);
        Rect bar_r{bar_x, by + (compact ? 9 : 11), bar_w, compact ? 8 : 10};
        c.round_rect(bar_r, 4, rgba(65, 65, 75));
        if (dur_ticks > 0) {
            double frac = std::max(0.0, std::min(1.0, double(pos_ticks) / double(dur_ticks)));
            int fw = std::max(4, int(frac * double(bar_r.w)));
            c.round_rect({bar_r.x, bar_r.y, fw, bar_r.h}, 4, col::Accent);
        }
        hits.push_back({200, {bar_r.x, bar_r.y - 6, bar_r.w, bar_r.h + 12}});

        // 10 clean discrete transport buttons
        const char* btn_labels[10] = {
            "|<<",
            "-10s",
            paused_.load() ? s.play : s.pause,
            "+10s",
            ">>| ",
            s.audio_track,
            s.sub_track,
            s.quality_btn,
            (fit == 0) ? s.fit_mode : s.fill_mode,
            s.close_player,
        };
        int btn_y = by + (compact ? 26 : 36);
        int btn_h = compact ? 32 : 38;
        int gap = compact ? 3 : 6;
        int total_w = c.w - 16;
        int bw = (total_w - gap * 9) / 10;
        for (int i = 0; i < 10; ++i) {
            int bx = 8 + i * (bw + gap);
            int cur_bw = (i == 9) ? (c.w - 8 - bx) : bw;
            Rect br{bx, btn_y, cur_bw, btn_h};
            bool sel = (modal_picker == 0 && osd_focus == i);
            c.round_rect(br, 6, sel ? col::FocusBg : col::Panel, sel ? col::FocusBorder : col::CardBorder);
            c.draw_text_centered(font, btn_labels[i], br, compact ? 10 : 13, col::TextMain, i == 2 || sel);
            hits.push_back({100 + i, br});
        }

        // Modal track / quality picker overlay
        if (modal_picker != 0) {
            auto opts = current_picker_options(s);
            int mw = std::min(c.w - 24, compact ? 320 : 420);
            int row_h = compact ? 28 : 34;
            int vis = std::min(int(opts.size()), compact ? 7 : 9);
            int mh = vis * row_h + 16;
            int mx = (c.w - mw) / 2;
            int my = (c.h - mh) / 2;
            c.round_rect({mx, my, mw, mh}, 10, col::TopBar, col::FocusBorder);

            int start = std::max(0, std::min(int(opts.size()) - vis, picker_sel - vis / 2));
            for (int vi = 0; vi < vis; ++vi) {
                int oi = start + vi;
                Rect rr{mx + 8, my + 8 + vi * row_h, mw - 16, row_h - 3};
                bool sel = (oi == picker_sel);
                c.round_rect(rr, 6, sel ? col::FocusBg : col::Card, sel ? col::FocusBorder : 0);
                std::string prefix = opts[size_t(oi)].active ? "* " : "  ";
                c.draw_text(font, prefix + opts[size_t(oi)].label, rr.x + 8, rr.y + (rr.h - (compact ? 12 : 14)) / 2,
                            compact ? 12 : 14, opts[size_t(oi)].active ? col::AccentSoft : col::TextMain, sel, rr.w - 16);
                hits.push_back({500 + oi, rr});
            }
        }
    }
};

} // namespace jf
