/* Compare an actual DAT entry against the engine's full-file decoder. */
#define _FILE_OFFSET_BITS 64
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
static ssize_t test_pread(int fd, void *out, size_t count, long long offset) {
    if (_lseeki64(fd, offset, SEEK_SET) < 0) return -1;
    return _read(fd, out, (unsigned)count);
}
#define pread test_pread
#endif
#include "archive_audio.c"

int main(int argc, char **argv) {
    assert(argc == 4);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    uint32_t offset = strtoul(argv[2], NULL, 10), size = strtoul(argv[3], NULL, 10);
    unsigned char *data = malloc(size);
    assert(data && fseek(f, offset, SEEK_SET) == 0 && fread(data, 1, size, f) == size);
    int fd = fileno(f);
    assert(!og_audio_entry_open(fd, offset, 3));
    ArchiveAudio *s = og_audio_entry_open(fd, offset, size);
    assert(s);
    if (size > 1024 * 1024) assert(s->pos < size / 8);
    const unsigned channels = s->channels, rate = s->rate;
    const int wav = s->wav;
    og_audio_entry_close(s);
    uint64_t frames;
    int16_t *pcm;
    if (wav) {
        drwav_uint64 n;
        pcm = drwav_open_memory_and_read_pcm_frames_s16(data, size, NULL, NULL, &n, NULL);
        frames = n;
    } else {
        drmp3_uint64 n;
        pcm = drmp3_open_memory_and_read_pcm_frames_s16(data, size, NULL, &n, NULL);
        frames = n;
    }
    assert(pcm && frames);
    for (int loop = 0; loop <= 1; loop++) {
        for (int resample = 0; resample <= 1; resample++) {
            unsigned target = resample ? (rate == 44100 ? 32000 : 44100) : rate;
            uint64_t count = (frames * target + rate - 1) / rate;
            uint64_t limit = count > 1000000 ? 50000 : count * (loop ? 2 : 1) + 1;
            s = og_audio_entry_open(fd, offset, size);
            assert(s);
            for (uint64_t pos = 0; pos < limit;) {
                float out[2 * 509] = {0};
                unsigned chunk = (pos % 509) + 1;
                if (chunk > limit - pos) chunk = (unsigned)(limit - pos);
                int eof = og_audio_entry_mix(s, out, chunk, target, 1.f/32768, 0.5f/32768, loop);
                for (unsigned i = 0; i < chunk; i++) {
                    uint64_t time = (pos+i) * rate, a = time / target;
                    if (!loop && a >= frames) {
                        assert(eof && out[2*i] == 0 && out[2*i+1] == 0);
                        continue;
                    }
                    a %= frames;
                    uint64_t b = a+1 < frames ? a+1 : loop ? 0 : a;
                    float frac = (float)(time % target) / target;
                    for (int j = 0; j < 2; j++) {
                        int ch = j && channels == 2;
                        int v = pcm[a*channels+ch], w = pcm[b*channels+ch];
                        float expected = (v + (w-v)*frac) * ((j ? 0.5f : 1.f)/32768);
                        if (fabsf(out[2*i+j] - expected) > 0.000002f) {
                            fprintf(stderr, "mismatch at %llu, loop %d rate %u: %g vs %g\n",
                                    (unsigned long long)(pos+i), loop, target, out[2*i+j], expected);
                            return 1;
                        }
                    }
                }
                pos += chunk;
            }
            og_audio_entry_close(s);
        }
    }
    printf("%s %u Hz %u channels, offset %u OK\n", wav ? "WAV" : "MP3", rate, channels, offset);
    if (wav) drwav_free(pcm, NULL); else drmp3_free(pcm, NULL);
    free(data);
    fclose(f);
    return 0;
}
