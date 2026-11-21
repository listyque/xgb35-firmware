#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dr_mp3.h"
#include "dr_wav.h"

/* Each voice has its own cursor in the shared DAT descriptor. */
typedef struct {
    int fd, wav;
    uint32_t offset, size, pos, channels, rate;
    union { drmp3 mp3; drwav wave; } decoder;
    int16_t block[1024 * 2], current[2], next[2];
    unsigned index, count, phase;
    int has_current, has_next, wrapped;
} ArchiveAudio;

static size_t read_entry(void *ctx, void *out, size_t count) {
    ArchiveAudio *s = ctx;
    if (count > s->size - s->pos) count = s->size - s->pos;
    size_t read = 0;
    while (read < count) {
        ssize_t n = pread(s->fd, (char *)out + read, count - read, (off_t)s->offset + s->pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        s->pos += (uint32_t)n;
        read += (size_t)n;
    }
    return read;
}

static int seek_entry(ArchiveAudio *s, int delta, int64_t base) {
    int64_t pos = base + delta;
    if (pos < 0 || pos > s->size) return 0;
    s->pos = (uint32_t)pos;
    return 1;
}
static drmp3_bool32 seek_mp3(void *ctx, int delta, drmp3_seek_origin origin) {
    ArchiveAudio *s = ctx;
    return seek_entry(s, delta, origin == DRMP3_SEEK_SET ? 0 : origin == DRMP3_SEEK_END ? s->size : s->pos);
}
static drwav_bool32 seek_wav(void *ctx, int delta, drwav_seek_origin origin) {
    ArchiveAudio *s = ctx;
    return seek_entry(s, delta, origin == DRWAV_SEEK_SET ? 0 : origin == DRWAV_SEEK_END ? s->size : s->pos);
}
static drmp3_bool32 tell_mp3(void *ctx, drmp3_int64 *pos) { *pos = ((ArchiveAudio *)ctx)->pos; return 1; }
static drwav_bool32 tell_wav(void *ctx, drwav_int64 *pos) { *pos = ((ArchiveAudio *)ctx)->pos; return 1; }

void og_audio_entry_close(void *ctx) {
    ArchiveAudio *s = ctx;
    if (s->wav) drwav_uninit(&s->decoder.wave); else drmp3_uninit(&s->decoder.mp3);
    free(s);
}

void *og_audio_entry_open(int fd, uint32_t offset, uint32_t size) {
    ArchiveAudio *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->fd = fd;
    s->offset = offset;
    s->size = size;
    char magic[4];
    if (read_entry(s, magic, 4) != 4) goto fail;
    s->pos = 0;
    s->wav = memcmp(magic, "RIFF", 4) == 0;
    if (s->wav) {
        if (!drwav_init(&s->decoder.wave, read_entry, seek_wav, tell_wav, s, NULL)) goto fail;
        s->channels = s->decoder.wave.channels;
        s->rate = s->decoder.wave.sampleRate;
    } else {
        if (!drmp3_init(&s->decoder.mp3, read_entry, seek_mp3, tell_mp3, NULL, s, NULL)) goto fail;
        s->channels = s->decoder.mp3.channels;
        s->rate = s->decoder.mp3.sampleRate;
    }
    if ((s->channels == 1 || s->channels == 2) && s->rate > 0) return s;
    og_audio_entry_close(s);
    return NULL;
fail:
    free(s);
    return NULL;
}

static unsigned fill(ArchiveAudio *s) {
    s->index = 0;
    s->count = s->wav ? drwav_read_pcm_frames_s16(&s->decoder.wave, 1024, s->block)
                      : drmp3_read_pcm_frames_s16(&s->decoder.mp3, 1024, s->block);
    return s->count;
}

static int refill(ArchiveAudio *s, int loop) {
    s->wrapped = 0;
    if (fill(s)) return 1;
    if (!loop) return 0;
    int ok = s->wav ? drwav_seek_to_pcm_frame(&s->decoder.wave, 0)
                    : drmp3_seek_to_pcm_frame(&s->decoder.mp3, 0);
    s->wrapped = 1;
    return ok && fill(s);
}

static int next_sample(ArchiveAudio *s, int16_t pair[2], int loop) {
    s->wrapped = 0;
    if (s->index == s->count && !refill(s, loop)) return 0;
    const int16_t *p = s->block + s->index++ * s->channels;
    pair[0] = p[0];
    pair[1] = p[s->channels - 1];
    return 1;
}

/* Returns 1 at EOF. Gains already include the s16-to-float scale. */
int og_audio_entry_mix(void *ctx, float *out, size_t frames, uint32_t rate, float left, float right, int loop) {
    ArchiveAudio *s = ctx;
    if (s->rate == rate) {
        while (frames) {
            if (s->index == s->count && !refill(s, loop)) return 1;
            size_t n = s->count - s->index;
            if (n > frames) n = frames;
            const int16_t *p = s->block + s->index * s->channels;
            for (size_t i = 0; i < n; i++, p += s->channels) {
                *out++ += p[0] * left;
                *out++ += p[s->channels - 1] * right;
            }
            s->index += n;
            frames -= n;
        }
        return 0;
    }
    if (!s->has_current) {
        s->has_current = next_sample(s, s->current, 0);
        if (!s->has_current) return 1;
        s->has_next = next_sample(s, s->next, loop);
    }
    while (frames--) {
        int next = s->has_next && (loop || !s->wrapped);
        const int16_t *b = next ? s->next : s->current;
        float t = (float)s->phase / rate;
        *out++ += (s->current[0] + (b[0] - s->current[0]) * t) * left;
        *out++ += (s->current[1] + (b[1] - s->current[1]) * t) * right;
        uint64_t phase = (uint64_t)s->phase + s->rate;
        while (phase >= rate) {
            phase -= rate;
            if (!s->has_next || (!loop && s->wrapped)) return 1;
            memcpy(s->current, s->next, sizeof(s->current));
            s->has_next = next_sample(s, s->next, loop);
        }
        s->phase = (unsigned)phase;
    }
    return 0;
}
