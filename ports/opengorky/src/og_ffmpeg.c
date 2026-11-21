/*
 * og_ffmpeg.c — Minimal FFmpeg 6.x C bridge for OpenGorky on XGB35 (ARMv7).
 * Avoids Zig translate-c on huge FFmpeg headers (which triggers Zig 0.17.1
 * Windows LLVM ARM32 crashes) while keeping 100% exact FFmpeg 6.1.1 ABI.
 */
#include <stdlib.h>
#include <limits.h>
#include <pthread.h>
#include <libavutil/audio_fifo.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>

typedef struct OgFfVideo {
    AVFormatContext *fmt;
    AVCodecContext  *dec;
    struct SwsContext *sws;
    AVPacket        *pkt;
    AVFrame         *frm;
    int              stream;
    double           time_base;
    double           pts;
    int              eof;
    int              threaded, stop, ready;
    uint8_t         *pixels;
    void            *audio;
    pthread_t        worker;
    pthread_mutex_t  lock;
    pthread_cond_t   wake;
} OgFfVideo;

void og_ff_audio_fill(void *handle);

void *og_ff_open(const char *path, int *out_w, int *out_h) {
    av_log_set_level(AV_LOG_QUIET);
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path, NULL, NULL) < 0) return NULL;
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        avformat_close_input(&fmt);
        return NULL;
    }
    int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (idx < 0) {
        avformat_close_input(&fmt);
        return NULL;
    }
    AVStream *st = fmt->streams[idx];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return NULL;
    }
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    if (!dec) {
        avformat_close_input(&fmt);
        return NULL;
    }
    if (avcodec_parameters_to_context(dec, st->codecpar) < 0 ||
        avcodec_open2(dec, codec, NULL) < 0) {
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return NULL;
    }
    struct SwsContext *sws = sws_getContext(
        dec->width, dec->height, dec->pix_fmt,
        dec->width, dec->height, AV_PIX_FMT_RGBA,
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!sws) {
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return NULL;
    }
    AVPacket *pkt = av_packet_alloc();
    AVFrame  *frm = av_frame_alloc();
    OgFfVideo *v  = (OgFfVideo *)calloc(1, sizeof(OgFfVideo));
    if (!pkt || !frm || !v) {
        if (frm) av_frame_free(&frm);
        if (pkt) av_packet_free(&pkt);
        sws_freeContext(sws);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        free(v);
        return NULL;
    }
    v->fmt = fmt;
    v->dec = dec;
    v->sws = sws;
    v->pkt = pkt;
    v->frm = frm;
    v->stream = idx;
    v->time_base = (double)st->time_base.num / (double)(st->time_base.den > 0 ? st->time_base.den : 1);
    v->pts = -1.0;
    v->eof = 0;
    if (out_w) *out_w = dec->width;
    if (out_h) *out_h = dec->height;
    return v;
}

int og_ff_next(void *handle, uint8_t *rgba, uint32_t w, double *out_pts) {
    OgFfVideo *v = (OgFfVideo *)handle;
    if (!v) return 0;
    for (;;) {
        if (avcodec_receive_frame(v->dec, v->frm) == 0) {
            uint8_t *dst[4] = { rgba, NULL, NULL, NULL };
            int stride[4] = { (int)(w * 4), 0, 0, 0 };
            sws_scale(v->sws, (const uint8_t * const *)v->frm->data, v->frm->linesize,
                      0, v->dec->height, dst, stride);
            int64_t ts = (v->frm->best_effort_timestamp != AV_NOPTS_VALUE)
                         ? v->frm->best_effort_timestamp : v->frm->pts;
            v->pts = (ts != AV_NOPTS_VALUE) ? (double)ts * v->time_base : (v->pts + 1.0 / 15.0);
            if (out_pts) *out_pts = v->pts;
            return 1;
        }
        if (v->eof) return 0;
        if (av_read_frame(v->fmt, v->pkt) < 0) {
            v->eof = 1;
            avcodec_send_packet(v->dec, NULL);
            continue;
        }
        if (v->pkt->stream_index == v->stream) {
            avcodec_send_packet(v->dec, v->pkt);
        }
        av_packet_unref(v->pkt);
    }
}

/* The renderer copies a ready frame before releasing this single decode buffer. */
static void *video_worker(void *handle) {
    OgFfVideo *v = handle;
    pthread_mutex_lock(&v->lock);
    while (!v->stop) {
        int decode = v->ready == 0;
        pthread_mutex_unlock(&v->lock);
        og_ff_audio_fill(v->audio);
        int next = decode ? og_ff_next(v, v->pixels, v->dec->width, NULL) : 0;
        pthread_mutex_lock(&v->lock);
        if (decode) v->ready = next ? 1 : -1;
        // Keep feeding audio even when the next picture has a distant timestamp.
        if (!v->stop && v->ready) {
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_nsec += 100000000;
            if (until.tv_nsec >= 1000000000) { until.tv_nsec -= 1000000000; until.tv_sec++; }
            pthread_cond_timedwait(&v->wake, &v->lock, &until);
        }
    }
    pthread_mutex_unlock(&v->lock);
    return NULL;
}

int og_ff_start_async(void *handle, uint8_t *pixels, void *audio) {
    OgFfVideo *v = handle;
    AVRational rate = av_guess_frame_rate(v->fmt, v->fmt->streams[v->stream], NULL);
    /* The original 15 Hz videos need only one frame of lookahead. */
    if (rate.num <= 0 || rate.den <= 0 || av_cmp_q(rate, (AVRational){15, 1}) > 0) return 0;
    if (pthread_mutex_init(&v->lock, NULL)) return 0;
    if (pthread_cond_init(&v->wake, NULL)) {
        pthread_mutex_destroy(&v->lock);
        return 0;
    }
    v->pixels = pixels;
    v->audio = audio;
    if (pthread_create(&v->worker, NULL, video_worker, v)) {
        pthread_cond_destroy(&v->wake);
        pthread_mutex_destroy(&v->lock);
        return 0;
    }
    v->threaded = 1;
    return 1;
}

int og_ff_poll(void *handle, double elapsed) {
    OgFfVideo *v = handle;
    pthread_mutex_lock(&v->lock);
    int ready = v->ready;
    if (ready && elapsed < v->pts + (ready < 0 ? 1.0 / 15.0 : 0)) ready = 0;
    pthread_mutex_unlock(&v->lock);
    return ready;
}

void og_ff_release(void *handle) {
    OgFfVideo *v = handle;
    pthread_mutex_lock(&v->lock);
    v->ready = 0;
    pthread_cond_signal(&v->wake);
    pthread_mutex_unlock(&v->lock);
}

void og_ff_close(void *handle) {
    OgFfVideo *v = (OgFfVideo *)handle;
    if (!v) return;
    if (v->threaded) {
        pthread_mutex_lock(&v->lock);
        v->stop = 1;
        pthread_cond_signal(&v->wake);
        pthread_mutex_unlock(&v->lock);
        pthread_join(v->worker, NULL);
        pthread_cond_destroy(&v->wake);
        pthread_mutex_destroy(&v->lock);
    }
    if (v->frm) av_frame_free(&v->frm);
    if (v->pkt) av_packet_free(&v->pkt);
    if (v->sws) sws_freeContext(v->sws);
    if (v->dec) avcodec_free_context(&v->dec);
    if (v->fmt) avformat_close_input(&v->fmt);
    free(v);
}

/* The video thread keeps a short PCM queue full. The ALSA callback only reads it. */
typedef struct {
    AVFormatContext *fmt;
    AVCodecContext *dec;
    AVPacket *pkt;
    AVFrame *frame;
    SwrContext *swr;
    AVAudioFifo *fifo;
    pthread_mutex_t lock;
    float *converted;
    unsigned converted_size;
    int stream, draining, done;
} OgAudio;

void og_ff_audio_close(void *handle) {
    OgAudio *a = handle;
    if (!a) return;
    if (a->fifo) av_audio_fifo_free(a->fifo);
    av_free(a->converted);
    av_packet_free(&a->pkt);
    av_frame_free(&a->frame);
    swr_free(&a->swr);
    avcodec_free_context(&a->dec);
    avformat_close_input(&a->fmt);
    pthread_mutex_destroy(&a->lock);
    free(a);
}

void og_ff_audio_fill(void *handle) {
    OgAudio *a = handle;
    if (!a || a->done) return;
    pthread_mutex_lock(&a->lock);
    int needed = 32768 - av_audio_fifo_size(a->fifo);
    pthread_mutex_unlock(&a->lock);
    if (needed < 8192) return;
    while (needed > 0) {
        int status = avcodec_receive_frame(a->dec, a->frame);
        if (status == AVERROR(EAGAIN) && !a->draining) {
            do {
                av_packet_unref(a->pkt);
                if (av_read_frame(a->fmt, a->pkt) < 0) {
                    a->draining = 1;
                    avcodec_send_packet(a->dec, NULL);
                    break;
                }
            } while (a->pkt->stream_index != a->stream);
            if (!a->draining && avcodec_send_packet(a->dec, a->pkt) < 0) break;
            continue;
        }
        int flush = status == AVERROR_EOF;
        if (status < 0 && !flush) break;
        int n = flush ? 0 : a->frame->nb_samples;
        int cap = swr_get_out_samples(a->swr, n);
        if (cap <= 0 || cap > INT_MAX / (2 * (int)sizeof(float))) break;
        av_fast_malloc(&a->converted, &a->converted_size, (size_t)cap * 2 * sizeof(float));
        if (!a->converted) break;
        uint8_t *dst[] = { (uint8_t *)a->converted };
        int got = swr_convert(a->swr, dst, cap, flush ? NULL : (const uint8_t **)a->frame->extended_data, n);
        if (got < 0 || (flush && got == 0)) break;
        if (got) {
            void *data[] = { a->converted };
            pthread_mutex_lock(&a->lock);
            int queued = av_audio_fifo_write(a->fifo, data, got);
            pthread_mutex_unlock(&a->lock);
            if (queued != got) break;
            needed -= got;
        }
    }
    if (needed > 0) a->done = 1;
}

void *og_ff_audio_open(const char *path) {
    OgAudio *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    if (pthread_mutex_init(&a->lock, NULL) != 0) { free(a); return NULL; }
    if (avformat_open_input(&a->fmt, path, NULL, NULL) < 0 ||
        avformat_find_stream_info(a->fmt, NULL) < 0) goto fail;
    const AVCodec *codec = NULL;
    a->stream = av_find_best_stream(a->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (a->stream < 0 || !codec) goto fail;
    a->dec = avcodec_alloc_context3(codec);
    if (!a->dec || avcodec_parameters_to_context(a->dec, a->fmt->streams[a->stream]->codecpar) < 0 ||
        avcodec_open2(a->dec, codec, NULL) < 0) goto fail;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&a->swr, &stereo, AV_SAMPLE_FMT_FLT, 44100,
        &a->dec->ch_layout, a->dec->sample_fmt, a->dec->sample_rate, 0, NULL) < 0 ||
        swr_init(a->swr) < 0) goto fail;
    a->pkt = av_packet_alloc();
    a->frame = av_frame_alloc();
    a->fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLT, 2, 32768 + 4096);
    if (!a->pkt || !a->frame || !a->fifo) goto fail;
    og_ff_audio_fill(a);
    if (av_audio_fifo_size(a->fifo) > 0) return a;
fail:
    og_ff_audio_close(a);
    return NULL;
}

void og_ff_audio_mix(void *handle, float *out, size_t count, float gain) {
    OgAudio *a = handle;
    float samples[2 * 512];
    void *data[] = { samples };
    pthread_mutex_lock(&a->lock);
    while (count >= 2) {
        int want = count / 2 > 512 ? 512 : (int)(count / 2);
        int got = av_audio_fifo_read(a->fifo, data, want);
        if (got <= 0) break;
        for (int i = 0; i < got * 2; i++) out[i] += samples[i] * gain;
        out += 2 * got;
        count -= 2 * got;
        if (got < want) break;
    }
    pthread_mutex_unlock(&a->lock);
}
