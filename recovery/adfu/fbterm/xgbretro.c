/* xgbretro: minimal 60fps Libretro frontend for ATM7051H / XGB35 (480x320 rotated ST7796S + 32-bit I2S ALSA).
 * Usage: xgbretro /path/to/core_libretro.so [/path/to/rom]
 * Hold SELECT + START in-game to quit cleanly back to xgbshell. Press L2 to toggle aspect/fullscreen.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <sound/asound.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "font8x16.h"
#include "cyr8x16.h"

/* --- Minimal Libretro ABI --- */
#define RETRO_ENVIRONMENT_GET_CAN_DUPE          3
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT      10
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY  9
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY    31
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE     27
#define RETRO_ENVIRONMENT_GET_VARIABLE          15
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE   17
#define RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK 62
#define RETRO_PIXEL_FORMAT_0RGB1555             0
#define RETRO_PIXEL_FORMAT_XRGB8888             1
#define RETRO_PIXEL_FORMAT_RGB565               2

#define RETRO_DEVICE_ID_JOYPAD_B        0
#define RETRO_DEVICE_ID_JOYPAD_Y        1
#define RETRO_DEVICE_ID_JOYPAD_SELECT   2
#define RETRO_DEVICE_ID_JOYPAD_START    3
#define RETRO_DEVICE_ID_JOYPAD_UP       4
#define RETRO_DEVICE_ID_JOYPAD_DOWN     5
#define RETRO_DEVICE_ID_JOYPAD_LEFT     6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT    7
#define RETRO_DEVICE_ID_JOYPAD_A        8
#define RETRO_DEVICE_ID_JOYPAD_X        9
#define RETRO_DEVICE_ID_JOYPAD_L        10
#define RETRO_DEVICE_ID_JOYPAD_R        11
#define RETRO_DEVICE_ID_JOYPAD_L2       12
#define RETRO_DEVICE_ID_JOYPAD_R2       13
#define RETRO_DEVICE_ID_JOYPAD_L3       14
#define RETRO_DEVICE_ID_JOYPAD_R3       15
#define RETRO_DEVICE_JOYPAD             1
#define RETRO_DEVICE_ANALOG             5
#define RETRO_DEVICE_DUALSHOCK          517

struct retro_game_info { const char *path; const void *data; size_t size; const char *meta; };
struct retro_system_info { const char *library_name, *library_version, *valid_extensions; bool need_fullpath, block_extract; };
struct retro_game_geometry { unsigned base_width, base_height, max_width, max_height; float aspect_ratio; };
struct retro_system_timing { double fps, sample_rate; };
struct retro_system_av_info { struct retro_game_geometry geometry; struct retro_system_timing timing; };
struct retro_variable { const char *key, *value; };

static int pix_fmt = RETRO_PIXEL_FORMAT_0RGB1555;
static int fb_fd = -1;
static struct fb_var_screeninfo fb_var;
static uint16_t *fb_mem;
static uint16_t *draw_fb;
static int fb_xres = 320, fb_yres = 480, fb_stride16 = 320;
static int fb_page = 0, fb_pages = 1;
static int hdmi_hw = 0;
static uint32_t fb_phys = 0, cur_src_w = 0, cur_src_h = 0;
static int cur_stretch = -1;
static volatile uint32_t *de_regs = NULL;
static int stretch = 0; /* 0 = sharp-pixels 4:3 (auto 2x integer for GB/GBA), 1 = integer 1:1, 2 = sharp-pixels 480x320 */
static int last_out_w = 0, last_out_h = 0;
static unsigned lut_sw = 0, lut_sh = 0, lut_ow = 0, lut_oh = 0;
static uint16_t lut_x0[480], lut_x1[480], lut_y0[320], lut_y1[320];
static uint8_t lut_wx[480], lut_wy[320];
static int16_t joy[16];
static int16_t analog[2][2] = { { 0, 0 }, { 0, 0 } }; /* [0]=Left(X,Y), [1]=Right(X,Y) in -32767..+32767 */
static int analog_mode = 1; /* 1 = true 16-bit analog DualShock, 0 = digital pad + stick-to-dpad */
static int is_psx_core = 0;
static int dpad_held[4] = { 0, 0, 0, 0 }; /* up, down, left, right */
static int evfds[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int adc_center[4] = { 515, 515, 515, 515 };
static int adc_ready = 0;
static volatile int quit_flag = 0;
static void on_sig(int s) { (void)s; quit_flag = 1; }
static volatile int menu_req = 0;
static int save_slot = 0;
static char rom_stem[160] = "game";
static size_t (*p_serialize_size)(void) = NULL;
static bool (*p_serialize)(void *, size_t) = NULL;
static bool (*p_unserialize)(const void *, size_t) = NULL;
static void (*p_reset)(void) = NULL;
static void *(*p_get_mem_data)(unsigned) = NULL;
static size_t (*p_get_mem_size)(unsigned) = NULL;
static void (*p_set_ctrl_dev)(unsigned, unsigned) = NULL;
#define ADC_SYS "/sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-adckeypad.0/input/input1/debug_adckey"
#define KEYMODE_SYS "/sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-adckeypad.0/input/input1/key_mode"
static int pcm_fd = -1;
static unsigned core_rate = 44100, hw_rate = 44100;
static snd_pcm_uframes_t hw_buf_frames = 8192;
static void (*audio_buf_cb)(bool active, unsigned occupancy, bool underrun_likely) = NULL;
static size_t frame_audio_frames = 0;
static int16_t sample_q[512 * 2];
static size_t sample_qn = 0;
static uint32_t resamp_pos = 0;
static int16_t resamp_prev[2] = { 0, 0 };

static char sys_dir[64] = "/tmp";
static char save_dir[64] = "/tmp";

static struct { char key[64]; char val[64]; } core_opts[64];
static int core_opts_n = 0;

static void set_opt_default(const char *k, const char *v)
{
    for (int i = 0; i < core_opts_n; i++) {
        if (strcmp(core_opts[i].key, k) == 0) {
            snprintf(core_opts[i].val, sizeof(core_opts[i].val), "%s", v);
            return;
        }
    }
    if (core_opts_n < 64) {
        snprintf(core_opts[core_opts_n].key, sizeof(core_opts[0].key), "%s", k);
        snprintf(core_opts[core_opts_n].val, sizeof(core_opts[0].val), "%s", v);
        core_opts_n++;
    }
}

static void init_dirs_and_opts(void)
{
    struct stat st;
    if (stat("/mnt/sd/onion", &st) == 0) {
        mkdir("/mnt/sd/onion/BIOS", 0755);
        mkdir("/mnt/sd/onion/Saves", 0755);
        mkdir("/mnt/sd/onion/Saves/States", 0755);
        snprintf(sys_dir, sizeof(sys_dir), "/mnt/sd/onion/BIOS");
        snprintf(save_dir, sizeof(save_dir), "/mnt/sd/onion/Saves");
    }
    /* Load persistent xgbretro UI settings (scale mode + save slot) */
    FILE *cf = fopen("/mnt/sd/onion/xgbretro.cfg", "r");
    if (cf) {
        char ln[64];
        while (fgets(ln, sizeof(ln), cf)) {
            int val = 0;
            if (sscanf(ln, "stretch=%d", &val) == 1 && val >= 0 && val <= 2) stretch = val;
            else if (sscanf(ln, "slot=%d", &val) == 1 && val >= 0 && val <= 9) save_slot = val;
            else if (sscanf(ln, "analog=%d", &val) == 1 && (val == 0 || val == 1)) analog_mode = val;
        }
        fclose(cf);
    }
    /* Optimal dual-core Cortex-A5 speed + clean-audio defaults for PS1 (pcsx_rearmed) */
    set_opt_default("pcsx_rearmed_drc", "enabled");
    set_opt_default("pcsx_rearmed_gpu_thread_rendering", "async");
    set_opt_default("pcsx_rearmed_spu_thread", "enabled");
    set_opt_default("pcsx_rearmed_spu_interpolation", "off");
    set_opt_default("pcsx_rearmed_spu_reverb", "disabled");
    set_opt_default("pcsx_rearmed_dithering", "disabled");
    set_opt_default("pcsx_rearmed_neon_enhancement_enable", "disabled");
    set_opt_default("pcsx_rearmed_frameskip_type", "auto");
    set_opt_default("pcsx_rearmed_frameskip_threshold", "33");
    set_opt_default("pcsx_rearmed_cd_readahead", "8");
    set_opt_default("pcsx_rearmed_psxclock", "57");

    /* User overrides from /mnt/sd/onion/core_options.cfg (key = value) */
    FILE *f = fopen("/mnt/sd/onion/core_options.cfg", "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = '\0';
        char *k = line, *v = eq + 1;
        while (*k == ' ' || *k == '\t') k++;
        char *ke = k + strlen(k);
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = '\0';
        while (*v == ' ' || *v == '\t' || *v == '"') v++;
        char *ve = v + strlen(v);
        while (ve > v && (ve[-1] == '\n' || ve[-1] == '\r' || ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '"')) *--ve = '\0';
        if (*k && *v) set_opt_default(k, v);
    }
    fclose(f);
}

static void log_cb(int level, const char *fmt, ...)
{
    (void)level;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: pix_fmt = *(const int *)data; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sys_dir; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = save_dir; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: *(void (**)(int, const char *, ...))data = log_cb; return true;
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
        audio_buf_cb = data ? *(void (**)(bool, unsigned, bool))data : NULL;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (data) *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *rv = (struct retro_variable *)data;
        rv->value = NULL;
        if (rv->key) {
            for (int i = 0; i < core_opts_n; i++) {
                if (strcmp(core_opts[i].key, rv->key) == 0) {
                    rv->value = core_opts[i].val;
                    return true;
                }
            }
        }
        return false;
    }
    default: return false;
    }
}

/* Direct ALSA S32_LE stereo output (HDMI pcmC1D0p when HDMI active, else speaker/earphone pcmC0D0p) */
static void alsa_open(unsigned rate)
{
    if (rate < 8000 || rate > 96000) rate = 44100;
    core_rate = rate;
    unsigned req = (rate == 44100 || rate == 48000) ? rate : 48000;
    if (hdmi_hw) pcm_fd = open("/dev/snd/pcmC1D0p", O_RDWR);
    if (pcm_fd < 0) pcm_fd = open("/dev/snd/pcmC0D0p", O_RDWR);
    if (pcm_fd < 0) return;

    struct snd_pcm_hw_params p;
    for (int attempt = 0; attempt < 2; attempt++) {
        memset(&p, 0, sizeof(p));
        for (int i = 0; i < SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1; i++)
            p.intervals[i].min = 0, p.intervals[i].max = ~0U;
        for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
            memset(&p.masks[i], 0xFF, sizeof(p.masks[i]));
        p.masks[SNDRV_PCM_HW_PARAM_ACCESS - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[0] = 1U << SNDRV_PCM_ACCESS_RW_INTERLEAVED;
        p.masks[SNDRV_PCM_HW_PARAM_FORMAT - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[0] = 1U << SNDRV_PCM_FORMAT_S32_LE;
        p.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min =
        p.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 2;
        p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = req;
        p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = req;
        p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 512;
        p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 1024;
        p.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 4096;
        p.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 8192;
        if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_HW_PARAMS, &p) == 0) break;
        if (attempt == 0 && req != 48000) { req = 48000; continue; }
        close(pcm_fd); pcm_fd = -1; return;
    }

    hw_rate = p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    snd_pcm_uframes_t per = p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    snd_pcm_uframes_t buf = p.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    if (!hw_rate) hw_rate = req;
    if (!per) per = 1024;
    if (!buf) buf = 4096;
    hw_buf_frames = buf;

    /* Pre-fill 2 periods before starting I2S DMA so minor frame jitter never triggers XRUN */
    struct snd_pcm_sw_params sw;
    memset(&sw, 0, sizeof(sw));
    sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    sw.period_step = 1;
    sw.avail_min = per;
    sw.start_threshold = (per * 2 <= buf) ? per * 2 : buf;
    sw.stop_threshold = buf;
    snd_pcm_uframes_t boundary = buf;
    while (boundary <= (snd_pcm_uframes_t)(LONG_MAX / 2)) boundary <<= 1;
    sw.boundary = boundary;
    ioctl(pcm_fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw);

    if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0) < 0) {
        close(pcm_fd); pcm_fd = -1;
    }
}

static void alsa_write_s16(const int16_t *data, size_t frames)
{
    if (pcm_fd < 0 || !frames) return;
    int32_t buf[1024 * 2];
    size_t done = 0;
    int retries = 0;
    while (done < frames) {
        size_t chunk = frames - done > 1024 ? 1024 : frames - done;
        /* -8.5 dB headroom (0.375x) prevents ATC2603C DAC/1W speaker clipping on full-scale square waves */
        for (size_t i = 0; i < chunk * 2; i++) buf[i] = ((int32_t)data[done * 2 + i] * 3) << 13;
        struct snd_xferi x = { .buf = buf, .frames = chunk, .result = 0 };
        if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
            if (errno == EINTR) continue;
            if (errno == EPIPE && retries++ < 3) { ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0); continue; }
            break;
        }
        retries = 0;
        done += (x.result > 0) ? (size_t)x.result : chunk;
    }
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    if (pcm_fd < 0 || !frames) return frames;
    frame_audio_frames += frames;
    if (hw_rate == core_rate) {
        alsa_write_s16(data, frames);
        return frames;
    }
    /* Lightweight 16.16 linear resampler for non-44.1/48kHz cores (GB/SNES/MD) */
    uint32_t step = (uint32_t)(((uint64_t)core_rate << 16) / hw_rate);
    int16_t out[1024 * 2];
    size_t out_n = 0;
    uint32_t limit = (uint32_t)frames << 16;
    while (resamp_pos < limit) {
        uint32_t idx = resamp_pos >> 16;
        uint32_t frac = resamp_pos & 0xFFFFU;
        int32_t l0 = idx ? data[(idx - 1) * 2]     : resamp_prev[0];
        int32_t r0 = idx ? data[(idx - 1) * 2 + 1] : resamp_prev[1];
        int32_t l1 = data[idx * 2];
        int32_t r1 = data[idx * 2 + 1];
        out[out_n * 2]     = (int16_t)(l0 + (((l1 - l0) * (int32_t)frac) >> 16));
        out[out_n * 2 + 1] = (int16_t)(r0 + (((r1 - r0) * (int32_t)frac) >> 16));
        if (++out_n == 1024) { alsa_write_s16(out, out_n); out_n = 0; }
        resamp_pos += step;
    }
    if (out_n) alsa_write_s16(out, out_n);
    resamp_pos -= limit;
    resamp_prev[0] = data[(frames - 1) * 2];
    resamp_prev[1] = data[(frames - 1) * 2 + 1];
    return frames;
}

static void audio_sample_cb(int16_t l, int16_t r)
{
    sample_q[sample_qn * 2]     = l;
    sample_q[sample_qn * 2 + 1] = r;
    if (++sample_qn == 512) {
        audio_batch_cb(sample_q, 512);
        sample_qn = 0;
    }
}

static inline uint16_t to_rgb565(const uint8_t *row, unsigned x)
{
    if (pix_fmt == RETRO_PIXEL_FORMAT_RGB565)
        return ((const uint16_t *)row)[x];
    if (pix_fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
        uint32_t p = ((const uint32_t *)row)[x];
        return (uint16_t)(((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
    }
    uint16_t p = ((const uint16_t *)row)[x];
    return (uint16_t)(((p & 0x7FE0) << 1) | (p & 0x001F));
}

static inline uint16_t lerp565(uint16_t a, uint16_t b, unsigned t)
{
    /* Carry-free 5-bit (0..31) RGB565 linear blend */
    uint32_t ax = (a | ((uint32_t)a << 16)) & 0x07E0F81FU;
    uint32_t bx = (b | ((uint32_t)b << 16)) & 0x07E0F81FU;
    uint32_t rx = ((ax * (32U - t) + bx * t) >> 5) & 0x07E0F81FU;
    return (uint16_t)(rx | (rx >> 16));
}

static void build_sharp_lut(unsigned in_n, unsigned out_n, uint16_t *i0, uint16_t *i1, uint8_t *w)
{
    for (unsigned o = 0; o < out_n; o++) {
        double u0 = (double)o * (double)in_n / (double)out_n;
        double u1 = (double)(o + 1U) * (double)in_n / (double)out_n;
        unsigned p0 = (unsigned)u0;
        unsigned p1 = (unsigned)u1;
        if (p0 >= in_n) p0 = in_n - 1U;
        if (p1 >= in_n) p1 = in_n - 1U;
        if (p0 == p1 || out_n <= in_n) {
            i0[o] = (uint16_t)p0;
            i1[o] = (uint16_t)p0;
            w[o] = 0;
        } else {
            /* Area-weighted box filter: blend only the exact fraction crossing boundary p0+1 */
            double frac = (u1 - (double)(p0 + 1U)) / (u1 - u0);
            unsigned wt = (unsigned)(frac * 32.0 + 0.5);
            if (wt == 0) {
                i0[o] = (uint16_t)p0; i1[o] = (uint16_t)p0; w[o] = 0;
            } else if (wt >= 32U) {
                i0[o] = (uint16_t)p1; i1[o] = (uint16_t)p1; w[o] = 0;
            } else {
                i0[o] = (uint16_t)p0; i1[o] = (uint16_t)(p0 + 1U < in_n ? p0 + 1U : p0); w[o] = (uint8_t)wt;
            }
        }
    }
}

static void draw_vol_osd_if_active(void);

/* Blit core frame: zero-CPU OWL Display Engine 8-tap HW upscaler on HDMI, or rotated 480x320 on built-in LCD */
static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || !width || !height) return;
    if (hdmi_hw && de_regs) {
        unsigned w = width <= (unsigned)fb_xres ? width : (unsigned)fb_xres;
        unsigned h = height <= (unsigned)fb_yres ? height : (unsigned)fb_yres;
        for (unsigned y = 0; y < h; y++) {
            const uint8_t *row = (const uint8_t *)data + y * pitch;
            uint16_t *dst = fb_mem + y * fb_stride16;
            if (pix_fmt == RETRO_PIXEL_FORMAT_RGB565) {
                memcpy(dst, row, w * 2);
            } else {
                for (unsigned x = 0; x < w; x++) dst[x] = to_rgb565(row, x);
            }
        }
        if (w != cur_src_w || h != cur_src_h || stretch != cur_stretch) {
            cur_src_w = w; cur_src_h = h; cur_stretch = stretch;
            int out_w = fb_xres, out_h = fb_yres;
            if (stretch != 2) {
                int fit_w = fb_yres * 4 / 3;
                if (fit_w <= fb_xres) out_w = fit_w;
            }
            int ox = (fb_xres - out_w) / 2, oy = (fb_yres - out_h) / 2;
            uint32_t sr_x = (w * 8192U + (uint32_t)out_w / 2U) / (uint32_t)out_w;
            uint32_t sr_y = (h * 8192U + (uint32_t)out_h / 2U) / (uint32_t)out_h;
            de_regs[0x220 / 4] = 0x04080770U;
            de_regs[0x250 / 4] = fb_phys;
            de_regs[0x268 / 4] = (uint32_t)(fb_stride16 * 2) / 8U;
            de_regs[0x224 / 4] = ((h - 1U) << 16) | (w - 1U);
            de_regs[0x228 / 4] = (((uint32_t)out_h - 1U) << 16) | ((uint32_t)out_w - 1U);
            de_regs[0x22C / 4] = (sr_y << 16) | sr_x;
            de_regs[0x230 / 4] = w <= 640U ? 0x00004000U : 0x00102010U;
            de_regs[0x114 / 4] = ((uint32_t)oy << 16) | (uint32_t)ox;
            de_regs[0x10C / 4] = 0;
            de_regs[0x100 / 4] &= ~0x200U; /* Native RGB 4:4:4 (sRGB) */
            de_regs[0x104 / 4] = 1U;
        }
        return;
    }

    int k = 0;
    if (width <= 480 && height <= 320) {
        int kx = 480 / (int)width, ky = 320 / (int)height;
        int best_k = kx < ky ? kx : ky;
        /* Mode 1: always integer k*k; Mode 0: auto integer when k>=2 (GBA 480x320, GB/GBC 320x288) */
        if (stretch == 1 || (stretch == 0 && best_k >= 2)) k = best_k;
    }

    int out_w, out_h;
    if (k >= 1) {
        out_w = (int)width * k;
        out_h = (int)height * k;
    } else if (stretch == 2) {
        out_w = 480; out_h = 320;
    } else {
        /* Mode 0 (default): true 4:3 (426x320, 89% screen) for NES/SNES/MD/PS1 */
        out_h = 320;
        out_w = 426;
    }
    if (out_w != last_out_w || out_h != last_out_h) {
        memset(fb_mem, 0, (size_t)fb_pages * (size_t)fb_stride16 * (size_t)fb_yres * 2);
        last_out_w = out_w; last_out_h = out_h;
    }
    int ox = (480 - out_w) / 2, oy = (320 - out_h) / 2;
    draw_fb = fb_mem + (size_t)fb_page * (size_t)fb_yres * (size_t)fb_stride16;

    if (k >= 1) {
        /* Exact k x k integer pixel-perfect blit (zero uneven pixels) */
        for (unsigned sy = 0; sy < height; sy++) {
            const uint8_t *row = (const uint8_t *)data + sy * pitch;
            for (int dy = 0; dy < k; dy++) {
                int fb_x = fb_xres - 1 - (oy + (int)sy * k + dy);
                for (unsigned sx = 0; sx < width; sx++) {
                    uint16_t c = to_rgb565(row, sx);
                    int base = ox + (int)sx * k;
                    for (int dx = 0; dx < k; dx++)
                        draw_fb[(base + dx) * fb_stride16 + fb_x] = c;
                }
            }
        }
        draw_vol_osd_if_active();
        if (fb_pages > 1) {
            fb_var.xoffset = 0;
            fb_var.yoffset = (uint32_t)(fb_page * fb_yres);
            ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var);
            fb_page = (fb_page + 1) % fb_pages;
        }
        return;
    }

    /* Fast LUT-driven Area-Weighted Sharp-Bilinear (Pixellate): crisp pixel interiors, zero shimmer */
    if (lut_sw != width || lut_sh != height || lut_ow != (unsigned)out_w || lut_oh != (unsigned)out_h) {
        build_sharp_lut(width, (unsigned)out_w, lut_x0, lut_x1, lut_wx);
        build_sharp_lut(height, (unsigned)out_h, lut_y0, lut_y1, lut_wy);
        lut_sw = width; lut_sh = height; lut_ow = (unsigned)out_w; lut_oh = (unsigned)out_h;
    }

    uint16_t tmp0[640], tmp1[640];
    int use_raw565 = (pix_fmt == RETRO_PIXEL_FORMAT_RGB565);
    unsigned conv_w = width <= 640U ? width : 640U;
    for (int Y = 0; Y < out_h; Y++) {
        unsigned sy0 = lut_y0[Y], sy1 = lut_y1[Y], wy = lut_wy[Y];
        const uint8_t *raw0 = (const uint8_t *)data + sy0 * pitch;
        const uint16_t *r0;
        if (use_raw565) {
            r0 = (const uint16_t *)raw0;
        } else {
            for (unsigned x = 0; x < conv_w; x++) tmp0[x] = to_rgb565(raw0, x);
            r0 = tmp0;
        }
        int fb_x = fb_xres - 1 - (oy + Y);
        if (!wy) {
            for (int X = 0; X < out_w; X++) {
                uint8_t wx = lut_wx[X];
                uint16_t c = wx ? lerp565(r0[lut_x0[X]], r0[lut_x1[X]], wx) : r0[lut_x0[X]];
                draw_fb[(ox + X) * fb_stride16 + fb_x] = c;
            }
        } else {
            const uint8_t *raw1 = (const uint8_t *)data + sy1 * pitch;
            const uint16_t *r1;
            if (use_raw565) {
                r1 = (const uint16_t *)raw1;
            } else {
                for (unsigned x = 0; x < conv_w; x++) tmp1[x] = to_rgb565(raw1, x);
                r1 = tmp1;
            }
            for (int X = 0; X < out_w; X++) {
                uint8_t wx = lut_wx[X];
                uint16_t c0 = wx ? lerp565(r0[lut_x0[X]], r0[lut_x1[X]], wx) : r0[lut_x0[X]];
                uint16_t c1 = wx ? lerp565(r1[lut_x0[X]], r1[lut_x1[X]], wx) : r1[lut_x0[X]];
                draw_fb[(ox + X) * fb_stride16 + fb_x] = lerp565(c0, c1, wy);
            }
        }
    }
    draw_vol_osd_if_active();
    if (fb_pages > 1) {
        fb_var.xoffset = 0;
        fb_var.yoffset = (uint32_t)(fb_page * fb_yres);
        ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var);
        fb_page = (fb_page + 1) % fb_pages;
    }
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void draw_vol_osd_if_active(void)
{
    static long long last_check = 0, osd_ts = 0;
    static int osd_vol = -1, was_drawn = 0;
    long long now = now_ms();
    if (now - last_check >= 60) {
        last_check = now;
        FILE *f = fopen("/tmp/.xgb_vol", "r");
        if (f) {
            int v = -1; long long ts = 0;
            if (fscanf(f, "%d %lld", &v, &ts) == 2) { osd_vol = v; osd_ts = ts; }
            fclose(f);
        }
    }
    if (osd_vol >= 0 && now - osd_ts < 1400) {
        int bx = 150, by = 10, bw = 180, bh = 22;
        int bar_x = bx + 26, bar_w = bw - 36, fill_w = (osd_vol * bar_w) / 40;
        uint16_t c_bg = 0x18E5, c_bd = 0x9CF3, c_tr = 0x31A6;
        uint16_t c_fg = osd_vol == 0 ? 0xF800 : 0x2F33;
        for (int Y = by; Y < by + bh; Y++) {
            int fb_x = fb_xres - 1 - Y;
            for (int X = bx; X < bx + bw; X++) {
                int border = (X == bx || X == bx + bw - 1 || Y == by || Y == by + bh - 1);
                uint16_t c = border ? c_bd : c_bg;
                int lx = X - (bx + 8), ly = Y - (by + 5);
                if (lx >= 0 && lx < 10 && ly >= 0 && ly < 12) {
                    if ((lx <= 3 && ly >= 3 && ly <= 8) ||
                        (lx >= 4 && lx <= 7 && ly >= (7 - lx) && ly <= (4 + lx)))
                        c = osd_vol == 0 ? 0xF800 : 0xFFFF;
                }
                if (X >= bar_x && X < bar_x + bar_w && Y >= by + 7 && Y < by + 15)
                    c = (X - bar_x < fill_w) ? c_fg : c_tr;
                draw_fb[X * fb_stride16 + fb_x] = c;
            }
        }
        was_drawn = 1;
    } else if (was_drawn) {
        was_drawn = 0;
        last_out_w = 0; /* clear top border cleanly on next frame */
    }
}

static inline int16_t adc_to_s16(int delta)
{
    const int dz = 36, span = 350; /* ~10% deadzone, full 10-bit throw */
    int mag = delta < 0 ? -delta : delta;
    if (mag <= dz) return 0;
    int scaled = ((mag - dz) * 32767) / (span - dz);
    if (scaled > 32767) scaled = 32767;
    return (int16_t)(delta < 0 ? -scaled : scaled);
}

static void poll_adc_stick(int *ax_u, int *ax_d, int *ax_l, int *ax_r)
{
    char buf[64];
    int fd = open(ADC_SYS, O_RDONLY);
    if (fd < 0) return;
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    char *p = buf;
    int v[4];
    for (int i = 0; i < 4; i++) {
        char *end = NULL;
        v[i] = (int)strtol(p, &end, 10);
        if (end == p) return;
        p = end;
    }
    if (!adc_ready) {
        for (int i = 0; i < 4; i++)
            adc_center[i] = (v[i] > 250 && v[i] < 780) ? v[i] : 515;
        adc_ready = 1;
    }
    /* True 16-bit proportional axes matching desktop calibration:
     * Left Stick = ax[2](X), ax[3](Y); Right Stick = ax[0](X), ax[1](Y); center - raw > 0 = Right/Down */
    int dx_l = adc_center[2] - v[2];
    int dy_l = adc_center[3] - v[3];
    int dx_r = adc_center[0] - v[0];
    int dy_r = adc_center[1] - v[1];
    analog[0][0] = adc_to_s16(dx_l);
    analog[0][1] = adc_to_s16(dy_l);
    analog[1][0] = adc_to_s16(dx_r);
    analog[1][1] = adc_to_s16(dy_r);
    /* Mirror left stick to D-pad on digital systems (NES/MD/SNES/GBA/GB) or when Digital mode selected */
    int mirror_dpad = (!is_psx_core || !analog_mode);
    *ax_r = mirror_dpad && (dx_l > 140);
    *ax_l = mirror_dpad && (dx_l < -140);
    *ax_d = mirror_dpad && (dy_l > 140);
    *ax_u = mirror_dpad && (dy_l < -140);
}

static void input_poll_cb(void)
{
    static int ax_u = 0, ax_d = 0, ax_l = 0, ax_r = 0;
    struct input_event ev[32];
    for (int i = 0; i < 8; i++) {
        if (evfds[i] < 0) continue;
        int n;
        while ((n = read(evfds[i], ev, sizeof(ev))) > 0) {
            for (int j = 0; j < n / (int)sizeof(ev[0]); j++) {
                if (ev[j].type != EV_KEY || ev[j].value == 2) continue;
                int c = ev[j].code, v = ev[j].value == 1;
                /* XGB35 physical layout (verified in xgb-keytest / desktop):
                 * Diamond: Right=352(RetroPad A), Bottom=158(RetroPad B), Top=308(RetroPad X), Left=139(RetroPad Y) */
                switch (c) {
                case 103: dpad_held[0] = v; break;
                case 108: dpad_held[1] = v; break;
                case 105: dpad_held[2] = v; break;
                case 106: dpad_held[3] = v; break;
                case 352: case 45: joy[RETRO_DEVICE_ID_JOYPAD_A] = v; break;
                case 158: case 44: joy[RETRO_DEVICE_ID_JOYPAD_B] = v; break;
                case 308: case 31: joy[RETRO_DEVICE_ID_JOYPAD_X] = v; break;
                case 139: case 30: joy[RETRO_DEVICE_ID_JOYPAD_Y] = v; break;
                case 314: case 54: joy[RETRO_DEVICE_ID_JOYPAD_SELECT] = v; break;
                case 315: case 28: joy[RETRO_DEVICE_ID_JOYPAD_START] = v; break;
                case 412: joy[RETRO_DEVICE_ID_JOYPAD_L] = v; break;
                case 407: joy[RETRO_DEVICE_ID_JOYPAD_R] = v; break;
                case 312:
                    if (v && joy[RETRO_DEVICE_ID_JOYPAD_SELECT]) {
                        stretch = (stretch + 1) % 3;
                        last_out_w = 0;
                    } else {
                        joy[RETRO_DEVICE_ID_JOYPAD_L2] = v;
                    }
                    break;
                case 313: joy[RETRO_DEVICE_ID_JOYPAD_R2] = v; break;
                case 317: joy[RETRO_DEVICE_ID_JOYPAD_L3] = v; break;
                case 318: joy[RETRO_DEVICE_ID_JOYPAD_R3] = v; break;
                case 174: case 1: if (v) menu_req = 1; break;
                }
                if (joy[RETRO_DEVICE_ID_JOYPAD_SELECT] && joy[RETRO_DEVICE_ID_JOYPAD_START]) menu_req = 1;
            }
        }
    }
    poll_adc_stick(&ax_u, &ax_d, &ax_l, &ax_r);
    joy[RETRO_DEVICE_ID_JOYPAD_UP]    = dpad_held[0] | ax_u;
    joy[RETRO_DEVICE_ID_JOYPAD_DOWN]  = dpad_held[1] | ax_d;
    joy[RETRO_DEVICE_ID_JOYPAD_LEFT]  = dpad_held[2] | ax_l;
    joy[RETRO_DEVICE_ID_JOYPAD_RIGHT] = dpad_held[3] | ax_r;
}

static int16_t input_state_cb(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
    if (port != 0) return 0;
    unsigned base_dev = dev & 0xFFU;
    if (base_dev == RETRO_DEVICE_JOYPAD && id < 16) return joy[id];
    if (base_dev == RETRO_DEVICE_ANALOG && idx < 2 && id < 2) return analog[idx][id];
    return 0;
}

static void apply_controller_mode(void)
{
    if (!p_set_ctrl_dev) return;
    if (is_psx_core)
        p_set_ctrl_dev(0, analog_mode ? RETRO_DEVICE_DUALSHOCK : RETRO_DEVICE_JOYPAD);
    else
        p_set_ctrl_dev(0, analog_mode ? RETRO_DEVICE_ANALOG : RETRO_DEVICE_JOYPAD);
}

/* ==================== PERSISTENT SRAM, SAVE STATES & IN-GAME QUICK MENU ==================== */

static void save_ui_cfg(void)
{
    FILE *f = fopen("/mnt/sd/onion/xgbretro.cfg", "w");
    if (!f) return;
    fprintf(f, "stretch=%d\nslot=%d\nanalog=%d\n", stretch, save_slot, analog_mode);
    fclose(f);
}

static void init_rom_stem(const char *path)
{
    if (!path || !path[0]) { snprintf(rom_stem, sizeof(rom_stem), "game"); return; }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(rom_stem, sizeof(rom_stem), "%s", base);
    char *dot = strrchr(rom_stem, '.');
    if (dot && dot != rom_stem) *dot = '\0';
}

static void sram_io(int do_save)
{
    if (!p_get_mem_data || !p_get_mem_size) return;
    size_t sz = p_get_mem_size(0); /* RETRO_MEMORY_SAVE_RAM */
    void *ptr = p_get_mem_data(0);
    if (!ptr || !sz) return;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.srm", save_dir, rom_stem);
    FILE *f = fopen(path, do_save ? "wb" : "rb");
    if (!f) return;
    if (do_save) { fwrite(ptr, 1, sz, f); fflush(f); }
    else         { (void)fread(ptr, 1, sz, f); }
    fclose(f);
}

static void state_path(char *out, size_t n, int slot)
{
    snprintf(out, n, "%s/States/%s.state%d", save_dir, rom_stem, slot);
}

static int state_exists(int slot)
{
    char p[256]; struct stat st;
    state_path(p, sizeof(p), slot);
    return stat(p, &st) == 0 && st.st_size > 0;
}

static int state_io(int do_save, int slot)
{
    if (!p_serialize_size || !p_serialize || !p_unserialize) return 0;
    size_t sz = p_serialize_size();
    if (!sz) return 0;
    void *buf = malloc(sz);
    if (!buf) return 0;
    char p[256];
    state_path(p, sizeof(p), slot);
    int ok = 0;
    if (do_save) {
        if (p_serialize(buf, sz)) {
            FILE *f = fopen(p, "wb");
            if (f) { ok = (fwrite(buf, 1, sz, f) == sz); fclose(f); }
        }
        sram_io(1);
    } else {
        FILE *f = fopen(p, "rb");
        if (f) {
            size_t got = fread(buf, 1, sz, f);
            fclose(f);
            if (got > 0 && p_unserialize(buf, got)) ok = 1;
        }
    }
    free(buf);
    return ok;
}

/* Per-system look + recommended CPU speed. Matched by ROM folder (Onion Roms/<FOLDER>) or core name. */
struct sys_profile {
    const char *folder, *core, *title, *subtitle;
    uint16_t bg, header, accent, sel, dots[4];
    int cpu_khz; /* recommended cpufreq step */
};
static const struct sys_profile sys_profiles[] = {
    { "FC",     "fceumm",       "NINTENDO NES",       "Famicom / Dendy",       0x18C3, 0xB000, 0xF800, 0x6000, { 0xF800, 0xF800, 0x8410, 0x8410 }, 360000 },
    { "GB",     "gambatte",     "GAME BOY",           "DMG-01",                0x1100, 0x2A20, 0x8E62, 0x3C40, { 0x8E62, 0x5C40, 0x8E62, 0x5C40 }, 360000 },
    { "GBC",    NULL,           "GAME BOY COLOR",     "CGB-001",               0x180C, 0x4810, 0xC81F, 0x6014, { 0xF800, 0xFFE0, 0x07E0, 0x001F }, 360000 },
    { "GBA",    "gpsp",         "GAME BOY ADVANCE",   "AGB-001",               0x1009, 0x3014, 0x7A1F, 0x4818, { 0x7A1F, 0x5014, 0x7A1F, 0x5014 }, 600000 },
    { "MD",     "picodrive",    "SEGA MEGA DRIVE",    "Genesis / 16-BIT",      0x0841, 0x10A2, 0x2B5F, 0x1110, { 0x2B5F, 0xF800, 0x2B5F, 0xF800 }, 600000 },
    { "SFC",    "snes9x",       "SUPER NINTENDO",     "Super Famicom",         0x18E7, 0x3A0F, 0xA41F, 0x5193, { 0xF800, 0xFFE0, 0x07E0, 0x001F }, 900000 },
    { "PCE",    "pce",          "PC ENGINE",          "TurboGrafx-16",         0x1882, 0x6900, 0xFC00, 0x8200, { 0xFC00, 0xFFFF, 0xFC00, 0xFFFF }, 600000 },
    { "PS",     "pcsx",         "PLAYSTATION",        "PSX / PS one",          0x10A3, 0x2945, 0xBDF7, 0x3A2A, { 0x07EF, 0xF810, 0x033F, 0xF9A6 }, 1200000 },
    { "ARCADE", "fbalpha",      "ARCADE",             "FinalBurn / MAME",      0x1800, 0x5800, 0xFEA0, 0x7800, { 0xFEA0, 0xF800, 0xFEA0, 0xF800 }, 900000 },
    { "PORTS",  "2048",         "PORTS",              "Native games",          0x0863, 0x1A2B, 0x4C9F, 0x2293, { 0x4C9F, 0x4C9F, 0x4C9F, 0x4C9F }, 240000 },
};
static const struct sys_profile sys_default = {
    "", NULL, "МЕНЮ ЭМУЛЯТОРА", "Libretro", 0x10A4, 0x1947, 0x4C9F, 0x2293, { 0x4C9F, 0x4C9F, 0x4C9F, 0x4C9F }, 900000
};
static const struct sys_profile *cur_sys = &sys_default;

/* Safe cpufreq steps only: the kernel OPP table raises DCDC1 voltage with the clock.
 * 1272 MHz is gone: it is outside the OPP table and needs more than the 1.2 V rail max. */
static const int cpu_steps_khz[] = { 0, 240000, 360000, 600000, 900000, 1200000 };
#define CPU_STEPS ((int)(sizeof cpu_steps_khz / sizeof cpu_steps_khz[0]))
static int cpu_khz = -1; /* -1 = not chosen yet, 0 = auto (system profile) */

static void detect_system(const char *core_path, const char *rom_path)
{
    for (size_t i = 0; i < sizeof sys_profiles / sizeof sys_profiles[0]; i++) {
        char needle[24];
        snprintf(needle, sizeof needle, "/%s/", sys_profiles[i].folder);
        if (rom_path && strstr(rom_path, needle)) { cur_sys = &sys_profiles[i]; return; }
    }
    for (size_t i = 0; i < sizeof sys_profiles / sizeof sys_profiles[0]; i++) {
        if (sys_profiles[i].core && core_path && strstr(core_path, sys_profiles[i].core)) {
            cur_sys = &sys_profiles[i];
            return;
        }
    }
}

static void write_sysfs(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { (void)write(fd, val, strlen(val)); close(fd); }
}

static void cpu_apply(int khz)
{
    const char *cf = "/sys/devices/system/cpu/cpu0/cpufreq/";
    char p[96], v[24];
    if (khz <= 0) {
        int rc = system("/bin/perf-mode apply >/dev/null 2>&1"); (void)rc;
        return;
    }
    int rc = system("killall xgbcpud >/dev/null 2>&1"); (void)rc; /* smart governor writes COREPLL directly */
    snprintf(p, sizeof p, "%sscaling_governor", cf);  write_sysfs(p, "userspace\n");
    snprintf(v, sizeof v, "%d\n", khz);
    /* Widen the window first or the kernel clamps the request to the profile's limits. */
    snprintf(p, sizeof p, "%sscaling_max_freq", cf);  write_sysfs(p, "1200000\n");
    snprintf(p, sizeof p, "%sscaling_min_freq", cf);  write_sysfs(p, "240000\n");
    snprintf(p, sizeof p, "%sscaling_setspeed", cf);  write_sysfs(p, v);
}

static int cpu_effective_khz(void) { return cpu_khz > 0 ? cpu_khz : cur_sys->cpu_khz; }

/* CPU choice is remembered per system: /mnt/sd/onion/xgbretro-<FOLDER>.cfg, "cpu=<kHz>" (0 = auto). */
static void sys_cfg_path(char *out, size_t n)
{
    snprintf(out, n, "/mnt/sd/onion/xgbretro-%s.cfg", *cur_sys->folder ? cur_sys->folder : "OTHER");
}

static void load_sys_cfg(void)
{
    char p[96];
    sys_cfg_path(p, sizeof p);
    cpu_khz = 0;
    FILE *f = fopen(p, "r");
    if (!f) return;
    int v;
    if (fscanf(f, "cpu=%d", &v) == 1)
        for (int i = 0; i < CPU_STEPS; i++)
            if (cpu_steps_khz[i] == v) cpu_khz = v;
    fclose(f);
}

static void save_sys_cfg(void)
{
    char p[96];
    sys_cfg_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "cpu=%d\n", cpu_khz);
    fclose(f);
}

static int cpu_step_index(void)
{
    for (int i = 0; i < CPU_STEPS; i++)
        if (cpu_steps_khz[i] == cpu_khz) return i;
    return 0;
}

static int menu_vol_rw(int set_vol)
{
    static int cached_vol = 28;
    if (set_vol < 0) {
        FILE *f = fopen("/tmp/.xgb_vol", "r");
        if (f) { int v = 28; if (fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 40) cached_vol = v; fclose(f); }
        return cached_vol;
    }
    if (set_vol > 40) set_vol = 40;
    cached_vol = set_vol;
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1; "
             "LD_LIBRARY_PATH=/lib /bin/tinymix 35 %d >/dev/null 2>&1",
             cached_vol, cached_vol > 0 ? 1 : 0);
    int rc = system(cmd); (void)rc;
    FILE *f = fopen("/tmp/.xgb_vol", "w");
    if (f) { fprintf(f, "%d %lld\n", cached_vol, now_ms()); fclose(f); }
    return cached_vol;
}

static inline void lcd_px(int x, int y, uint16_t c)
{
    if ((unsigned)x < 480U && (unsigned)y < 320U)
        draw_fb[x * fb_stride16 + (fb_xres - 1 - y)] = c;
}

static void lcd_rect(int x, int y, int w, int h, uint16_t c)
{
    for (int Y = y; Y < y + h; Y++)
        for (int X = x; X < x + w; X++)
            lcd_px(X, Y, c);
}

static void lcd_text(int x, int y, const char *s, uint16_t fg)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p && x <= 472) {
        const unsigned char *glyph = NULL;
        if (*p < 0x80) {
            glyph = &font8x16[(*p) * 16];
            p++;
        } else if ((p[0] == 0xD0 || p[0] == 0xD1) && p[1]) {
            unsigned cp = ((unsigned)(p[0] & 0x1FU) << 6) | (unsigned)(p[1] & 0x3FU);
            if (cp >= 0x0410 && cp <= 0x044F) glyph = &cyr8x16[(cp - 0x0410) * 16];
            else if (cp == 0x0401) glyph = &cyr8x16[64 * 16];
            else if (cp == 0x0451) glyph = &cyr8x16[65 * 16];
            p += 2;
        } else {
            p++;
        }
        if (glyph) {
            for (int r = 0; r < 16; r++) {
                unsigned char bits = glyph[r];
                for (int c = 0; c < 8; c++)
                    if (bits & (0x80U >> c)) lcd_px(x + c, y + r, fg);
            }
        }
        x += 8;
    }
}

static void run_quick_menu(void)
{
    if (hdmi_hw) { quit_flag = 1; return; }
    /* Draw menu directly onto the currently displayed page */
    int vis_page = (fb_page + fb_pages - 1) % fb_pages;
    draw_fb = fb_mem + (size_t)vis_page * (size_t)fb_yres * (size_t)fb_stride16;
    /* Dim current game frame on LCD as crisp translucent backdrop */
    for (int Y = 0; Y < 320; Y++) {
        int fb_x = fb_xres - 1 - Y;
        for (int X = 0; X < 480; X++) {
            uint16_t c = draw_fb[X * fb_stride16 + fb_x];
            draw_fb[X * fb_stride16 + fb_x] = (uint16_t)((c >> 1) & 0x7BEFU);
        }
    }

    int sel = 0;
    const int n_items = 9;
    int vol = menu_vol_rw(-1);
    char msg[64] = "A: выбрать   </>: менять   B: в игру";
    int dirty = 1;
    const struct sys_profile *S = cur_sys;

    /* Drain any held keys so menu opens cleanly */
    usleep(120000);
    struct input_event ev[32];
    for (int i = 0; i < 8; i++) if (evfds[i] >= 0) while (read(evfds[i], ev, sizeof(ev)) > 0) {}

    for (;;) {
        if (dirty) {
            dirty = 0;
            int bx = 48, by = 10, bw = 384, bh = 300;
            lcd_rect(bx, by, bw, bh, S->bg);
            /* System-coloured frame + header with the console's own button colours */
            lcd_rect(bx, by, bw, 2, S->accent);
            lcd_rect(bx, by + bh - 2, bw, 2, S->accent);
            lcd_rect(bx, by, 2, bh, S->accent);
            lcd_rect(bx + bw - 2, by, 2, bh, S->accent);
            lcd_rect(bx + 2, by + 2, bw - 4, 38, S->header);
            lcd_rect(bx + 2, by + 40, bw - 4, 2, S->accent);
            lcd_text(bx + 14, by + 5, S->title, 0xFFFF);
            lcd_text(bx + 14, by + 22, S->subtitle, S->accent);
            for (int d = 0; d < 4; d++) {
                int cx = bx + bw - 70 + d * 15, cy = by + 14;
                lcd_rect(cx, cy, 10, 10, S->dots[d]);
            }

            static const char *scale_names[3] = { "Sharp 4:3", "Пиксель 1:1", "Весь экран" };
            char cpu_txt[40];
            int eff = cpu_effective_khz() / 1000;
            if (cpu_khz <= 0)
                snprintf(cpu_txt, sizeof cpu_txt, "Авто (%d МГц)", eff);
            else
                snprintf(cpu_txt, sizeof cpu_txt, "%d МГц%s", eff,
                         cpu_khz == S->cpu_khz ? " *" : "");
            char rows[9][72];
            int has_st = state_exists(save_slot);
            snprintf(rows[0], sizeof(rows[0]), "Продолжить игру");
            snprintf(rows[1], sizeof(rows[1]), "Сохранить: слот %d %s", save_slot, has_st ? "[*]" : "[ ]");
            snprintf(rows[2], sizeof(rows[2]), "Загрузить: слот %d %s", save_slot, has_st ? "[*]" : "[нет]");
            snprintf(rows[3], sizeof(rows[3]), "Экран:   < %s >", scale_names[stretch % 3]);
            snprintf(rows[4], sizeof(rows[4]), "Стики:   < %s >", analog_mode ? "Аналог (DualShock)" : "Крестовина");
            snprintf(rows[5], sizeof(rows[5]), "CPU:     < %s >", cpu_txt);
            snprintf(rows[6], sizeof(rows[6]), "Громкость: < %d%% >", (vol * 100 + 20) / 40);
            snprintf(rows[7], sizeof(rows[7]), "Сбросить игру");
            snprintf(rows[8], sizeof(rows[8]), "Сохранить и выйти");

            for (int i = 0; i < n_items; i++) {
                int ry = by + 48 + i * 25;
                if (i == sel) {
                    lcd_rect(bx + 10, ry - 2, bw - 20, 22, S->sel);
                    lcd_rect(bx + 10, ry - 2, 4, 22, S->accent);
                    lcd_text(bx + 22, ry + 1, rows[i], 0xFFFF);
                } else {
                    lcd_text(bx + 22, ry + 1, rows[i], 0xC618);
                }
            }
            lcd_rect(bx + 2, by + bh - 24, bw - 4, 22, S->header);
            lcd_text(bx + 12, by + bh - 21, msg, 0xFFFF);
        }

        usleep(16000);
        for (int i = 0; i < 8; i++) {
            if (evfds[i] < 0) continue;
            int n;
            while ((n = read(evfds[i], ev, sizeof(ev))) > 0) {
                for (int j = 0; j < n / (int)sizeof(ev[0]); j++) {
                    if (ev[j].type != EV_KEY || ev[j].value != 1) continue;
                    int c = ev[j].code;
                    if (c == 103) { sel = (sel + n_items - 1) % n_items; dirty = 1; }
                    else if (c == 108) { sel = (sel + 1) % n_items; dirty = 1; }
                    else if (c == 105 || c == 106) {
                        int d = (c == 106) ? 1 : -1;
                        if (sel == 1 || sel == 2) {
                            save_slot = (save_slot + 10 + d) % 10;
                            save_ui_cfg();
                            snprintf(msg, sizeof(msg), "Выбран слот %d", save_slot);
                        } else if (sel == 3) {
                            stretch = (stretch + 3 + d) % 3;
                            save_ui_cfg();
                            snprintf(msg, sizeof(msg), "Режим экрана сохранён");
                        } else if (sel == 4) {
                            analog_mode = !analog_mode;
                            apply_controller_mode();
                            save_ui_cfg();
                            snprintf(msg, sizeof(msg), analog_mode ? "Аналоговые стики ВКЛ" : "Режим крестовины ВКЛ");
                        } else if (sel == 5) {
                            int i = cpu_step_index();
                            cpu_khz = cpu_steps_khz[(i + CPU_STEPS + d) % CPU_STEPS];
                            cpu_apply(cpu_effective_khz());
                            save_sys_cfg();
                            snprintf(msg, sizeof(msg), "CPU %d МГц (рек. %d)",
                                     cpu_effective_khz() / 1000, S->cpu_khz / 1000);
                        } else if (sel == 6) {
                            vol += d * 2;
                            if (vol < 0) vol = 0;
                            if (vol > 40) vol = 40;
                            menu_vol_rw(vol);
                        }
                        dirty = 1;
                    } else if (c == 352 || c == 174 || c == 1) {
                        goto resume_game;
                    } else if (c == 158 || c == 315 || c == 28) {
                        if (sel == 0) goto resume_game;
                        if (sel == 1) {
                            if (state_io(1, save_slot)) snprintf(msg, sizeof(msg), "Сохранено в слот %d!", save_slot);
                            else snprintf(msg, sizeof(msg), "Ошибка сохранения слота");
                            dirty = 1;
                        } else if (sel == 2) {
                            if (state_io(0, save_slot)) goto resume_game;
                            snprintf(msg, sizeof(msg), "Слот %d пуст или повреждён", save_slot);
                            dirty = 1;
                        } else if (sel == 3) {
                            stretch = (stretch + 1) % 3; save_ui_cfg(); dirty = 1;
                        } else if (sel == 4) {
                            analog_mode = !analog_mode; apply_controller_mode(); save_ui_cfg(); dirty = 1;
                        } else if (sel == 5) {
                            int i = cpu_step_index();
                            cpu_khz = cpu_steps_khz[(i + 1) % CPU_STEPS];
                            cpu_apply(cpu_effective_khz());
                            save_sys_cfg();
                            dirty = 1;
                        } else if (sel == 7) {
                            if (p_reset) p_reset();
                            goto resume_game;
                        } else if (sel == 8) {
                            sram_io(1);
                            quit_flag = 1;
                            goto resume_game;
                        }
                    }
                }
            }
        }
    }

resume_game:
    memset(joy, 0, sizeof(joy));
    memset(dpad_held, 0, sizeof(dpad_held));
    menu_req = 0;
    last_out_w = 0; /* force full clean redraw on next game frame */
    if (pcm_fd >= 0) ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "Usage: %s <core.so> [rom]\n", argv[0]); return 1; }
    signal(SIGTERM, on_sig);
    signal(SIGINT, on_sig);
    dlopen("/usr/lib/libgcompat.so", RTLD_NOW | RTLD_GLOBAL);
    void *dl = dlopen(argv[1], RTLD_LAZY);
    if (!dl) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

    void (*retro_set_environment)(void *) = dlsym(dl, "retro_set_environment");
    void (*retro_set_video_refresh)(void *) = dlsym(dl, "retro_set_video_refresh");
    void (*retro_set_audio_sample)(void *) = dlsym(dl, "retro_set_audio_sample");
    void (*retro_set_audio_sample_batch)(void *) = dlsym(dl, "retro_set_audio_sample_batch");
    void (*retro_set_input_poll)(void *) = dlsym(dl, "retro_set_input_poll");
    void (*retro_set_input_state)(void *) = dlsym(dl, "retro_set_input_state");
    void (*retro_init)(void) = dlsym(dl, "retro_init");
    void (*retro_deinit)(void) = dlsym(dl, "retro_deinit");
    void (*retro_get_system_info)(struct retro_system_info *) = dlsym(dl, "retro_get_system_info");
    void (*retro_get_system_av_info)(struct retro_system_av_info *) = dlsym(dl, "retro_get_system_av_info");
    bool (*retro_load_game)(const struct retro_game_info *) = dlsym(dl, "retro_load_game");
    void (*retro_unload_game)(void) = dlsym(dl, "retro_unload_game");
    void (*retro_run)(void) = dlsym(dl, "retro_run");
    p_reset          = dlsym(dl, "retro_reset");
    p_serialize_size = dlsym(dl, "retro_serialize_size");
    p_serialize      = dlsym(dl, "retro_serialize");
    p_unserialize    = dlsym(dl, "retro_unserialize");
    p_get_mem_data   = dlsym(dl, "retro_get_memory_data");
    p_get_mem_size   = dlsym(dl, "retro_get_memory_size");
    p_set_ctrl_dev   = dlsym(dl, "retro_set_controller_port_device");
    is_psx_core      = (strstr(argv[1], "pcsx") != NULL);

    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) { perror("fb"); return 1; }
    struct fb_fix_screeninfo f;
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_var); ioctl(fb_fd, FBIOGET_FSCREENINFO, &f);
    fb_xres = fb_var.xres; fb_yres = fb_var.yres; fb_stride16 = f.line_length / 2; fb_phys = (uint32_t)f.smem_start;
    fb_pages = (fb_var.yres_virtual >= fb_var.yres * 3) ? 3 : (fb_var.yres_virtual >= fb_var.yres * 2) ? 2 : 1;
    fb_mem = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    draw_fb = fb_mem;
    memset(fb_mem, 0, (size_t)fb_pages * (size_t)fb_stride16 * (size_t)fb_yres * 2);
    ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
    fb_var.xoffset = 0; fb_var.yoffset = 0; ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var);

    /* Ensure ATC2603C ADC stick mode matches desktop (xgbjoyx11); never EVIOCGRAB so xgbpowerd handles VOL+/VOL-/POWER/screenshot */
    {
        int kfd = open(KEYMODE_SYS, O_WRONLY);
        if (kfd >= 0) { (void)write(kfd, "1\n", 2); close(kfd); }
    }
    for (int i = 0; i < 8; i++) {
        char p[32], name[64] = ""; snprintf(p, sizeof(p), "/dev/input/event%d", i);
        int efd = open(p, O_RDONLY | O_NONBLOCK);
        if (efd < 0) continue;
        ioctl(efd, EVIOCGNAME(sizeof(name)), name);
        if (strstr(name, "onoff")) { close(efd); continue; }
        evfds[i] = efd;
    }

    init_dirs_and_opts();
    detect_system(argv[1], argc >= 3 ? argv[2] : NULL);
    load_sys_cfg();
    cpu_apply(cpu_effective_khz());
    retro_set_environment(env_cb);
    retro_init();
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_sample_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);

    struct retro_system_info si = { 0 };
    retro_get_system_info(&si);
    struct retro_game_info gi = { 0 };
    void *rom_buf = NULL;
    if (argc >= 3 && argv[2][0]) {
        init_rom_stem(argv[2]);
        gi.path = argv[2];
        struct stat st;
        if (!si.need_fullpath || (stat(argv[2], &st) == 0 && st.st_size > 0 && st.st_size <= 16 * 1024 * 1024)) {
            FILE *rf = fopen(argv[2], "rb");
            if (!rf) { perror(argv[2]); goto cleanup; }
            fseek(rf, 0, SEEK_END); gi.size = ftell(rf); fseek(rf, 0, SEEK_SET);
            rom_buf = malloc(gi.size);
            if (!rom_buf || fread(rom_buf, 1, gi.size, rf) != gi.size) { fclose(rf); goto cleanup; }
            fclose(rf); gi.data = rom_buf;
        }
    }
    bool loaded = retro_load_game(argc >= 3 ? &gi : NULL);
    if (!loaded && gi.data && gi.path) {
        gi.path = NULL; /* fallback for glibc cores whose stat(path) mismatches musl */
        loaded = retro_load_game(&gi);
    }
    if (!loaded) { fprintf(stderr, "retro_load_game failed\n"); goto cleanup; }
    apply_controller_mode();
    sram_io(0); /* auto-load battery/memory-card SRAM save */

    struct retro_system_av_info av = { 0 };
    retro_get_system_av_info(&av);
    double fps = av.timing.fps > 10.0 ? av.timing.fps : 60.0;
    alsa_open((unsigned)(av.timing.sample_rate > 0 ? av.timing.sample_rate : 44100));

    long frame_ns = (long)(1000000000.0 / fps);
    struct timespec next_ts;
    clock_gettime(CLOCK_MONOTONIC, &next_ts);

    while (!quit_flag) {
        if (audio_buf_cb && pcm_fd >= 0 && hw_buf_frames > 0) {
            snd_pcm_sframes_t del = 0;
            unsigned occ = 50;
            if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_DELAY, &del) == 0 && del >= 0) {
                occ = (unsigned)((del * 100) / (snd_pcm_sframes_t)hw_buf_frames);
                if (occ > 100) occ = 100;
            }
            audio_buf_cb(true, occ, occ < 25);
        }
        frame_audio_frames = 0;
        retro_run();
        if (menu_req) {
            run_quick_menu();
            if (quit_flag) break;
            clock_gettime(CLOCK_MONOTONIC, &next_ts);
            continue;
        }
        if (sample_qn) { audio_batch_cb(sample_q, sample_qn); sample_qn = 0; }
        /* When ALSA is active, blocking WRITEI_FRAMES paces to the DAC crystal with zero drift/XRUNs */
        if (pcm_fd < 0 || frame_audio_frames == 0) {
            next_ts.tv_nsec += frame_ns;
            while (next_ts.tv_nsec >= 1000000000L) { next_ts.tv_sec++; next_ts.tv_nsec -= 1000000000L; }
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec < next_ts.tv_sec || (now.tv_sec == next_ts.tv_sec && now.tv_nsec < next_ts.tv_nsec))
                clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_ts, NULL);
            else
                next_ts = now;
        }
    }
    sram_io(1); /* auto-save battery/memory-card SRAM on exit */
    retro_unload_game();
cleanup:
    retro_deinit();
    if (rom_buf) free(rom_buf);
    if (pcm_fd >= 0) close(pcm_fd);
    if (fb_fd >= 0) {
        fb_var.xoffset = 0; fb_var.yoffset = 0;
        ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var);
        close(fb_fd);
    }
    for (int i = 0; i < 8; i++) if (evfds[i] >= 0) close(evfds[i]);
    cpu_apply(0); /* back to the global perf-mode profile for menus */
    return 0;
}
