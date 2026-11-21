/* OpenGorky Native Platform Backend for XGB35 (Actions ATM7051H / Cortex-A5 NEON)
 *
 * Implements the og_sdl_* ABI (D:\grav\g18\port_zig\src\viewer\sdl2_compat.h) directly
 * with ZERO SDL2 dependency:
 *   - ATM7051H Display Engine (DE_ML0 @ 0xB02F0000) hardware scaler for 0.0 ms HDMI fullscreen
 *   - X11 Desktop (dlopen libX11 + XShm/XPutImage) & Handheld 3.5" LCD (/dev/fb0 480x320 ST7796S)
 *   - ARM NEON RGB565/RGBA8888 blitter, 16-bit RGB565/ARGB1555 static texture compression (50% RAM)
 *   - Direct ALSA S32_LE stereo audio (HDMI pcmC1D0p / Speaker pcmC0D0p + tinymix)
 *   - Evdev XGB35 gamepad + analog sticks + USB mouse/keyboard
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>
#include <stdatomic.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sound/asound.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>

#include "sdl_xgb35.h"
#include "menu_pixels.h"
#include "sgx_menu.h"
#include "sgx_scene.h"
#include "sgx_sprites.h"

#define LOG_W 640
#define LOG_H 480
#define EVENT_Q_CAP 256
#define MAX_EVDEVS 16

/* Custom blend mode tags returned by og_sdl_ComposeCustomBlendMode */
#define OG_BLEND_ADD_ONE  0x10000001u
#define OG_BLEND_DARKEN   0x10000002u
#define OG_BLEND_SCREEN   0x10000003u
#define OG_BLEND_MULTIPLY 0x10000004u

/* --- Global state --- */
static volatile sig_atomic_t g_quit_req = 0;
static char g_old_gov[64] = {0};
static int g_hw_vol = 36;
static bool g_handheld_1to1 = false;
static bool g_cursor_visible = true;
static bool g_text_input = false;
static bool g_key_state[512];
static uint16_t g_key_mod = 0;
static float g_mouse_x = 320.0f, g_mouse_y = 240.0f;
static uint64_t g_t0_ns = 0;
static unsigned hidden_passes;
#define OG_F5 (SDLK_F1 + 4)

unsigned og_xgb35_HiddenPasses(void) { return hidden_passes; }

bool og_xgb35_TogglePass(SDL_Keycode key) {
    if (key < OG_F5 || key > OG_F5 + 4) return false;
    unsigned index = key - OG_F5, bit = 1u << index;
    hidden_passes ^= bit;
    if (bit == OG_HIDE_BACKGROUND_Z) {
        if (hidden_passes & bit) setenv("OG_DEBUG_SKIP_BG_Z", "1", 1);
        else unsetenv("OG_DEBUG_SKIP_BG_Z");
    }
    fprintf(stderr, "frame debug F%u %s (mask %u)\n", 5 + index,
            hidden_passes & bit ? "off" : "on", hidden_passes);
    return true;
}

/* --- ATM7051H / ATS3605 Display Engine (DE_ML0 @ 0xB02F0000) hardware scaler --- */
typedef struct {
    int fd;
    volatile uint32_t *de;
    uint32_t orig_coor0, orig_isize, orig_osize, orig_sr;
    uint32_t orig_coef[8];
    volatile sig_atomic_t active;
    int cur_ox, cur_oy, cur_ow, cur_oh;
} DeHwScaler;

static DeHwScaler g_de = { .fd = -1, .cur_ox = -1, .cur_oy = -1, .cur_ow = -1, .cur_oh = -1 };

static bool de_init(int fb_w, int fb_h, size_t fb_pitch) {
    int mfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mfd < 0) return false;
    void *p = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0xB02F0000u);
    if (p == MAP_FAILED) { close(mfd); return false; }
    volatile uint32_t *regs = (volatile uint32_t *)p;
    uint32_t expected_sz = ((uint32_t)(fb_h - 1) << 16) | (uint32_t)(fb_w - 1);
    const uint32_t input = regs[0x224 / 4], output = regs[0x228 / 4];
    const uint32_t origin = regs[0x114 / 4], ratio = regs[0x22C / 4];
    // Recover an interrupted OpenGorky session only when its complete 720p scale matches.
    const bool stale_720p = fb_w == 1280 && fb_h == 720 &&
        input == ((LOG_H - 1u) << 16 | (LOG_W - 1u)) &&
        output == ((720u - 1u) << 16 | (960u - 1u)) &&
        origin == 160u && ratio == 0x15551555u;
    const bool desktop_scale = fb_w == 1920 && fb_h == 1080 &&
        input == ((720u - 1u) << 16 | (1280u - 1u)) && output == expected_sz &&
        origin == 0 && (ratio == 0x15551555u || ratio == 0x15561556u);
    if ((!stale_720p && !desktop_scale && input != expected_sz) ||
        regs[0x268 / 4] != (uint32_t)(fb_pitch / 8)) {
        munmap(p, 0x1000);
        close(mfd);
        return false;
    }
    g_de.fd = mfd;
    g_de.de = regs;
    g_de.orig_coor0 = stale_720p ? 0 : origin;
    g_de.orig_isize = stale_720p ? expected_sz : input;
    g_de.orig_osize = stale_720p ? expected_sz : output;
    g_de.orig_sr    = stale_720p ? 0x20002000u : ratio;
    for (int i = 0; i < 8; i++) g_de.orig_coef[i] = regs[(0x230 / 4) + i];
    return true;
}

static void de_apply(int ox, int oy, int ow, int oh) {
    if (!g_de.de) return;
    const uint32_t input = ((uint32_t)(LOG_H - 1) << 16) | (uint32_t)(LOG_W - 1);
    if (g_de.active && ox == g_de.cur_ox && oy == g_de.cur_oy && ow == g_de.cur_ow &&
        oh == g_de.cur_oh && g_de.de[0x224 / 4] == input) return;
    static const uint32_t bilinear[8] = {
        0x00004000u, 0x00083800u, 0x00103000u, 0x00182800u,
        0x00202000u, 0x00281800u, 0x00301000u, 0x00380800u
    };
    g_de.de[0x114 / 4] = ((uint32_t)oy << 16) | (uint32_t)ox;
    g_de.de[0x224 / 4] = input;
    g_de.de[0x228 / 4] = ((uint32_t)(oh - 1) << 16) | (uint32_t)(ow - 1);
    g_de.de[0x22C / 4] = ((((uint32_t)LOG_H << 13) / (uint32_t)oh) << 16) |
                         (((uint32_t)LOG_W << 13) / (uint32_t)ow);
    for (int i = 0; i < 8; i++) g_de.de[(0x230 / 4) + i] = bilinear[i];
    g_de.de[0x104 / 4] = 1u; /* latch on VSYNC */
    g_de.cur_ox = ox; g_de.cur_oy = oy; g_de.cur_ow = ow; g_de.cur_oh = oh;
    g_de.active = 1;
}

static void de_restore(void) {
    if (!g_de.de || !g_de.active) return;
    g_de.de[0x114 / 4] = g_de.orig_coor0;
    g_de.de[0x224 / 4] = g_de.orig_isize;
    g_de.de[0x228 / 4] = g_de.orig_osize;
    g_de.de[0x22C / 4] = g_de.orig_sr;
    for (int i = 0; i < 8; i++) g_de.de[(0x230 / 4) + i] = g_de.orig_coef[i];
    g_de.de[0x104 / 4] = 1u;
    g_de.active = 0;
    g_de.cur_ox = g_de.cur_oy = g_de.cur_ow = g_de.cur_oh = -1;
}

static void de_shutdown(void) {
    de_restore();
    if (g_de.de) { munmap((void *)g_de.de, 0x1000); g_de.de = NULL; }
    if (g_de.fd >= 0) { close(g_de.fd); g_de.fd = -1; }
}

static void gov_restore(void) {
    if (g_old_gov[0]) {
        int fd = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", O_WRONLY);
        if (fd >= 0) { (void)write(fd, g_old_gov, strlen(g_old_gov)); close(fd); }
        g_old_gov[0] = 0;
    }
}

static void gov_engage(void) {
    int fd = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, g_old_gov, sizeof(g_old_gov) - 1);
        close(fd);
        if (n > 0) g_old_gov[n] = 0;
    }
    fd = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", O_WRONLY);
    if (fd >= 0) { (void)write(fd, "performance\n", 12); close(fd); }
}

static void on_term_sig(int sig) { (void)sig; g_quit_req = 1; }
static void on_crash_sig(int sig) {
    de_restore();
    gov_restore();
    char msg[64];
    int n = snprintf(msg, sizeof(msg), "\n[OPENGORKY CRASH] signal=%d\n", sig);
    if (n > 0) (void)write(2, msg, (size_t)n);
    _exit(128 + sig);
}

/* --- Time helpers --- */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t og_sdl_GetTicks(void) {
    if (!g_t0_ns) g_t0_ns = now_ns();
    return (now_ns() - g_t0_ns) / 1000000ull;
}
uint64_t og_sdl_GetTicksNS(void) {
    if (!g_t0_ns) g_t0_ns = now_ns();
    return now_ns() - g_t0_ns;
}
static struct { uint64_t ns; const void *caller; char tag; } model_trace[4096];
static unsigned model_trace_count;
static int model_trace_enabled=-1;
static void model_trace_note(char tag, const void *caller, uint64_t ns) {
    if (model_trace_enabled<0) model_trace_enabled=getenv("OG_MODEL_CPU_TRACE")!=NULL;
    if (!model_trace_enabled || model_trace_count>=sizeof(model_trace)/sizeof(*model_trace)) return;
    model_trace[model_trace_count].ns=ns;
    model_trace[model_trace_count].caller=caller;
    model_trace[model_trace_count++].tag=tag;
}
uint64_t og_sdl_GetPerformanceCounter(void) {
    uint64_t t=now_ns();
    model_trace_note('T',__builtin_return_address(0),t);
    return t;
}
uint64_t og_sdl_GetPerformanceFrequency(void) { return 1000000000ull; }
void og_sdl_DelayPrecise(uint64_t ns) {
    if (ns == 0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000ull);
    ts.tv_nsec = (long)(ns % 1000000000ull);
    nanosleep(&ts, NULL);
}

/* --- Event Queue --- */
static SDL_Event g_evq[EVENT_Q_CAP];
static int g_ev_head = 0, g_ev_tail = 0;

static bool evq_push(const SDL_Event *ev) {
    int next = (g_ev_tail + 1) % EVENT_Q_CAP;
    if (next == g_ev_head) return false;
    g_evq[g_ev_tail] = *ev;
    if (ev->type == SDL_EVENT_TEXT_INPUT && ev->text.text) {
        SDL_TextInputEvent *text = &g_evq[g_ev_tail].text;
        snprintf(text->buf, sizeof(text->buf), "%s", ev->text.text);
    }
    g_ev_tail = next;
    return true;
}

static void push_text(const char *text) {
    SDL_Event ev = {0};
    ev.type = SDL_EVENT_TEXT_INPUT;
    ev.text.windowID = 1;
    ev.text.text = text;
    evq_push(&ev);
}

static void push_key(bool down, uint32_t sym, int scan) {
    if (scan >= 0 && scan < 512) g_key_state[scan] = down;
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.windowID = 1;
    ev.key.scancode = scan;
    ev.key.key = sym;
    ev.key.mod = g_key_mod;
    ev.key.down = down;
    evq_push(&ev);
}

static void push_mouse_move(float x, float y, float dx, float dy) {
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;
    if (x > (float)(LOG_W - 1)) x = (float)(LOG_W - 1);
    if (y > (float)(LOG_H - 1)) y = (float)(LOG_H - 1);
    g_mouse_x = x; g_mouse_y = y;
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.windowID = 1;
    ev.motion.x = x; ev.motion.y = y;
    ev.motion.xrel = dx; ev.motion.yrel = dy;
    evq_push(&ev);
}

static void push_mouse_btn(bool down, uint8_t btn) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.windowID = 1;
    ev.button.button = btn;
    ev.button.down = down;
    ev.button.clicks = 1;
    ev.button.x = g_mouse_x;
    ev.button.y = g_mouse_y;
    evq_push(&ev);
}

static void push_mouse_wheel(float wy) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_EVENT_MOUSE_WHEEL;
    ev.wheel.windowID = 1;
    ev.wheel.y = wy;
    ev.wheel.mouse_x = g_mouse_x;
    ev.wheel.mouse_y = g_mouse_y;
    evq_push(&ev);
}

/* --- Color & NEON pixel helpers --- */
static inline uint8_t clamp_u8f(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

static inline uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline uint16_t pack1555(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint16_t)((a >= 128 ? 0x8000u : 0u) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

static inline uint16_t avg565(uint16_t a, uint16_t b) {
    return (uint16_t)((a & b) + (((a ^ b) & 0xF7DEu) >> 1));
}

static inline void unpack565(uint16_t p, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t r5 = (uint8_t)(p >> 11);
    uint8_t g6 = (uint8_t)((p >> 5) & 63);
    uint8_t b5 = (uint8_t)(p & 31);
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

static inline uint16_t blend565_rgba(uint16_t dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa, uint32_t mode) {
    if (mode == SDL_BLENDMODE_NONE) return pack565(sr, sg, sb);
    if (mode == SDL_BLENDMODE_BLEND) {
        if (sa == 0) return dst;
        if (sa == 255) return pack565(sr, sg, sb);
        /* Fast SWAR 5-6-5 alpha blend (zero divisions, 5-bit alpha precision) */
        uint32_t a = (uint32_t)(sa + 4u) >> 3; /* 0..32 */
        if (a > 32u) a = 32u;
        uint32_t ia = 32u - a;
        uint32_t d_rb = dst & 0xF81Fu;
        uint32_t d_g  = dst & 0x07E0u;
        uint16_t sp = pack565(sr, sg, sb);
        uint32_t s_rb = sp & 0xF81Fu;
        uint32_t s_g  = sp & 0x07E0u;
        uint32_t rb = ((s_rb * a + d_rb * ia) >> 5) & 0xF81Fu;
        uint32_t g  = ((s_g  * a + d_g  * ia) >> 5) & 0x07E0u;
        return (uint16_t)(rb | g);
    }
    uint8_t dr, dg, db;
    unpack565(dst, &dr, &dg, &db);
    if (mode == OG_BLEND_ADD_ONE) {
        uint32_t r = dr + sr;
        uint32_t g = dg + sg;
        uint32_t b = db + sb;
        return pack565((uint8_t)(r > 255 ? 255 : r), (uint8_t)(g > 255 ? 255 : g), (uint8_t)(b > 255 ? 255 : b));
    }
    if (mode == SDL_BLENDMODE_ADD) {
        uint32_t r = dr + ((sr * sa) >> 8);
        uint32_t g = dg + ((sg * sa) >> 8);
        uint32_t b = db + ((sb * sa) >> 8);
        return pack565((uint8_t)(r > 255 ? 255 : r), (uint8_t)(g > 255 ? 255 : g), (uint8_t)(b > 255 ? 255 : b));
    }
    if (mode == OG_BLEND_DARKEN) {
        uint8_t r = (uint8_t)((dr * (255u - sr)) >> 8);
        uint8_t g = (uint8_t)((dg * (255u - sg)) >> 8);
        uint8_t b = (uint8_t)((db * (255u - sb)) >> 8);
        return pack565(r, g, b);
    }
    if (mode == OG_BLEND_SCREEN) {
        uint8_t r = (uint8_t)(sr + ((dr * (255u - sr)) >> 8));
        uint8_t g = (uint8_t)(sg + ((dg * (255u - sg)) >> 8));
        uint8_t b = (uint8_t)(sb + ((db * (255u - sb)) >> 8));
        return pack565(r, g, b);
    }
    if (mode == OG_BLEND_MULTIPLY) {
        uint8_t r = (uint8_t)((dr * sr) >> 8);
        uint8_t g = (uint8_t)((dg * sg) >> 8);
        uint8_t b = (uint8_t)((db * sb) >> 8);
        return pack565(r, g, b);
    }
    return pack565(sr, sg, sb);
}

/* NEON fast row converters */
static void rgb565_row_to_bgrx32(const uint16_t *src, uint32_t *dst, int n) {
    int i = 0;
#if defined(__ARM_NEON)
    for (; i + 8 <= n; i += 8) {
        uint16x8_t p = vld1q_u16(src + i);
        uint8x8_t r = vmovn_u16(vshrq_n_u16(p, 11));
        uint8x8_t g = vmovn_u16(vandq_u16(vshrq_n_u16(p, 5), vdupq_n_u16(63)));
        uint8x8_t b = vmovn_u16(vandq_u16(p, vdupq_n_u16(31)));
        uint8x8x4_t out;
        out.val[0] = vorr_u8(vshl_n_u8(b, 3), vshr_n_u8(b, 2));
        out.val[1] = vorr_u8(vshl_n_u8(g, 2), vshr_n_u8(g, 4));
        out.val[2] = vorr_u8(vshl_n_u8(r, 3), vshr_n_u8(r, 2));
        out.val[3] = vdup_n_u8(255);
        vst4_u8((uint8_t *)(dst + i), out);
    }
#endif
    for (; i < n; i++) {
        uint8_t r, g, b;
        unpack565(src[i], &r, &g, &b);
        dst[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

static void blend_row_565_avg(const uint16_t *a, const uint16_t *b, uint16_t *out, int n) {
    int i = 0;
#if defined(__ARM_NEON)
    const uint16x8_t mask = vdupq_n_u16(0xF7DEu);
    for (; i + 8 <= n; i += 8) {
        uint16x8_t va = vld1q_u16(a + i);
        uint16x8_t vb = vld1q_u16(b + i);
        uint16x8_t m = vaddq_u16(vandq_u16(va, vb), vshrq_n_u16(vandq_u16(veorq_u16(va, vb), mask), 1));
        vst1q_u16(out + i, m);
    }
#endif
    for (; i < n; i++) out[i] = avg565(a[i], b[i]);
}

static void scale_640x480_to_480x320_565(const uint16_t *src, uint16_t *dst) {
    uint16_t tmp[LOG_W];
    for (int dy = 0; dy < 320; dy++) {
        int sy = (dy * 3) >> 1;
        const uint16_t *r0 = src + sy * LOG_W;
        const uint16_t *row;
        if (dy & 1) {
            int sy1 = (sy + 1 < LOG_H) ? sy + 1 : sy;
            blend_row_565_avg(r0, src + sy1 * LOG_W, tmp, LOG_W);
            row = tmp;
        } else {
            row = r0;
        }
        uint16_t *out = dst + dy * 480;
        /* 4 source pixels -> 3 destination pixels */
        for (int sx = 0, dx = 0; sx < LOG_W; sx += 4, dx += 3) {
            out[dx + 0] = row[sx + 0];
            out[dx + 1] = avg565(row[sx + 1], row[sx + 2]);
            out[dx + 2] = row[sx + 3];
        }
    }
}

/* --- Texture --- */
enum { PIX_NONE = 0, PIX_RGB565 = 1, PIX_ARGB1555 = 2, PIX_RGBA8888 = 3 };

typedef struct { int w, h; int32_t u, v, du, dv; } Linear565Key;
static size_t g_linear565_pixels;

struct SDL_Texture {
    int w, h;
    int access;
    int pix_fmt;
    void *data;
    int pitch;
    uint32_t blend;
    int scale_mode;
    uint8_t cr, cg, cb, ca;
    unsigned sgx_id, sgx_generation;
    OgGpuTexture gpu;
    bool gpu_only;
    SDL_Rect gpu_locked;
    uint16_t *linear565;
    size_t linear565_capacity;
    Linear565Key linear565_key;
};

/* --- X11 Dynamic API --- */
typedef struct {
    void *libx11, *libxext;
    Display *(*OpenDisplay)(const char *);
    int (*CloseDisplay)(Display *);
    Window (*CreateSimpleWindow)(Display *, Window, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long);
    int (*SelectInput)(Display *, Window, long);
    int (*MapWindow)(Display *, Window);
    int (*StoreName)(Display *, Window, const char *);
    Atom (*InternAtom)(Display *, const char *, Bool);
    Status (*SetWMProtocols)(Display *, Window, Atom *, int);
    int (*Pending)(Display *);
    int (*NextEvent)(Display *, XEvent *);
    GC (*CreateGC)(Display *, Drawable, unsigned long, XGCValues *);
    int (*FreeGC)(Display *, GC);
    XImage *(*CreateImage)(Display *, Visual *, unsigned, int, int, char *, unsigned, unsigned, int, int);
    int (*PutImage)(Display *, Drawable, GC, XImage *, int, int, int, int, unsigned, unsigned);
    int (*Flush)(Display *);
    int (*Sync)(Display *, Bool);
    int (*SendEvent)(Display *, Window, Bool, long, XEvent *);
    KeySym (*LookupKeysym)(XKeyEvent *, int);
    int (*LookupString)(XKeyEvent *, char *, int, KeySym *, XComposeStatus *);
    Status (*GetWindowAttributes)(Display *, Window, XWindowAttributes *);
    Pixmap (*CreateBitmapFromData)(Display *, Drawable, const char *, unsigned, unsigned);
    Cursor (*CreatePixmapCursor)(Display *, Pixmap, Pixmap, XColor *, XColor *, unsigned, unsigned);
    int (*DefineCursor)(Display *, Window, Cursor);
    int (*FreeCursor)(Display *, Cursor);
    int (*FreePixmap)(Display *, Pixmap);
    /* XShm */
    Bool (*ShmQueryExtension)(Display *);
    XImage *(*ShmCreateImage)(Display *, Visual *, unsigned, int, char *, XShmSegmentInfo *, unsigned, unsigned);
    Bool (*ShmAttach)(Display *, XShmSegmentInfo *);
    Bool (*ShmDetach)(Display *, XShmSegmentInfo *);
    Bool (*ShmPutImage)(Display *, Drawable, GC, XImage *, int, int, int, int, unsigned, unsigned, Bool);
} X11Api;

/* --- Evdev Hub --- */
typedef struct {
    int fd, idx;
    bool is_gamepad;
} EvDev;

typedef struct {
    int center, half, raw;
} AxisCal;

typedef struct {
    EvDev devs[MAX_EVDEVS];
    int count;
    AxisCal ax[4];
    bool btn_sel, btn_start, btn_l1, btn_r2;
    bool select_chord;
    unsigned debug_buttons;
    bool dpad_u, dpad_d, dpad_l, dpad_r;
    bool pan_u, pan_d, pan_l, pan_r;
    uint64_t last_scan_ms;
    uint64_t last_tick_ms;
} EvdevHub;

/* --- Window & Renderer --- */
struct SDL_Window {
    int w, h;
    bool fullscreen;
    bool use_x11;
    /* X11 */
    X11Api x11;
    Display *dpy;
    int screen;
    Window win;
    GC gc;
    Atom wm_delete, net_wm_state, net_wm_fs;
    int x11_depth;
    XImage *ximg;
    XShmSegmentInfo shminfo;
    bool use_shm;
    uint8_t *xbuf;
    int xbuf_w, xbuf_h, xbuf_bpp;
    /* Direct /dev/fb1 (HDMI) */
    int dfb_fd;
    uint8_t *dfb_raw, *dfb_ptr;
    size_t dfb_len, dfb_pitch;
    int dfb_w, dfb_h;
    bool dfb_de_ok;
    /* Handheld /dev/fb0 */
    int fb_fd;
    uint8_t *fb_raw;
    size_t fb_len, fb_pitch;
    int fb_w, fb_h, fb_bpp;
    bool fb_rot90;
    /* Evdev */
    EvdevHub evdev;
};

struct SDL_Renderer {
    SDL_Window *win;
    uint16_t *backbuf;
    SDL_Texture *target;
    uint8_t dr, dg, db, da;
    uint32_t draw_blend;
    bool has_clip;
    SDL_Rect clip;
    bool has_vp;
    SDL_Rect vp;
    float scale_x, scale_y;
    int log_w, log_h, log_mode;
    uint64_t fps_start;
    unsigned fps_frames;
    char fps_text[16];
};

static SDL_Window *g_win = NULL;
static SDL_Renderer *g_ren = NULL;
static void *gpu_library;
static const OgGpuSprites *gpu;
static SceneShadow *gpu_shadow;
static SceneModels *gpu_models;
static size_t gpu_reclaimed;
static bool gpu_hdmi_scaled;
static bool gpu_async_requested;
static int gpu_display_w,gpu_display_h;
static bool gpu_open(void) {
    const char *name=getenv("OG_SGX_DIRECT");
    if (!name) return true;
    gpu_library=dlopen("opengorky-sgx.so",RTLD_NOW|RTLD_LOCAL);
    const OgGpuSprites *(*get_ops)(void)=gpu_library?
        (const OgGpuSprites *(*)(void))dlsym(gpu_library,"og_sgx_sprites_v3"):NULL;
    if (get_ops) gpu=get_ops();
    int index=name[0]=='0'?0:1;
    if (index==1) {
        int fd=open("/dev/fb1",O_RDONLY|O_CLOEXEC);
        struct fb_fix_screeninfo f;
        struct fb_var_screeninfo v;
        if (fd>=0 && !ioctl(fd,FBIOGET_FSCREENINFO,&f) && !ioctl(fd,FBIOGET_VSCREENINFO,&v) &&
            v.xres>=LOG_W && v.yres>=LOG_H && (g_de.de || de_init(v.xres,v.yres,f.line_length))) {
            index=2; gpu_display_w=v.xres; gpu_display_h=v.yres;
        }
        if (fd>=0) close(fd);
    }
    if (gpu && gpu->open(index)) {
        gpu_async_requested=getenv("OG_SGX_ASYNC")!=NULL;
        gpu_shadow=(!gpu_async_requested && getenv("OG_SGX_GPU_SHADOW"))?
            (SceneShadow *)dlsym(gpu_library,"og_sgx_direct_shadow"):NULL;
        gpu_models=(getenv("OG_SGX_GPU_MODELS") || getenv("OG_SGX_RESIDENT"))?
            (SceneModels *)dlsym(gpu_library,"og_sgx_direct_models"):NULL;
        gpu_hdmi_scaled=index==2;
        gpu->target(NULL,LOG_W,LOG_H);
        fprintf(stderr,"OpenGorky: SGX sprites via fb%s\n",name[0]=='0'?"0":"1");
        return true;
    }
    fprintf(stderr,"OpenGorky: could not open SGX fbdev sprite renderer\n");
    gpu=NULL;
    if (gpu_library) dlclose(gpu_library);
    gpu_library=NULL;
    return false;
}

static bool gpu_texture(SDL_Texture *t) {
    bool ok=gpu->texture(&t->gpu,t->data,t->w,t->h,t->pitch,t->pix_fmt,
                         t->access==SDL_TEXTUREACCESS_TARGET,t->scale_mode==1);
    if (ok && t->gpu_only && t->data) {
        gpu_reclaimed+=(size_t)t->pitch*t->h;
        free(t->data);
        t->data=NULL;
    }
    return ok;
}

void og_xgb35_TextureImmutable(SDL_Texture *t) {
    if (gpu && t && t->access==SDL_TEXTUREACCESS_STATIC) t->gpu_only=true;
}

bool og_xgb35_CanDrawToScanout(int w,int h) {
    return gpu_hdmi_scaled && gpu && w==LOG_W && h==LOG_H;
}

static bool g_x11_threaded;

/* Dual-core pipelined /dev/fb0 presenter (Core 1!) with lock-free triple-buffer pointer swap */
static uint16_t *g_fb_stage[3] = { NULL, NULL, NULL };
static uint16_t g_land480[480 * 320];
static uint16_t g_rot480[480 * 320];
static pthread_t g_fb_thread;
static bool g_fb_thread_started = false;
static pthread_mutex_t g_fb_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_fb_cv = PTHREAD_COND_INITIALIZER;
static int g_fb_front = 0, g_fb_ready = 1, g_fb_back = 2;
static bool g_fb_has_job = false, g_fb_stop = false;
static bool g_fb_job_1to1 = false;
static int g_fb_job_mx = 320, g_fb_job_my = 240;

static pthread_mutex_t g_fb_output = PTHREAD_MUTEX_INITIALIZER;

static void fb0_crop_origin(int mx, int my, int *x, int *y) {
    *x = mx < 240 ? 0 : mx > LOG_W - 240 ? LOG_W - 480 : mx - 240;
    *y = my < 160 ? 0 : my > LOG_H - 160 ? LOG_H - 320 : my - 160;
}

static void fb0_draw(SDL_Window *w, const uint16_t *src, bool crop, int mx, int my) {
    pthread_mutex_lock(&g_fb_output);
    uint16_t *fb16 = (uint16_t *)w->fb_raw;
    int stride16 = (int)(w->fb_pitch / 2);
    if (crop) {
        int vx, vy;
        fb0_crop_origin(mx, my, &vx, &vy);
        for (int y = 0; y < 320; y++)
            memcpy(g_land480 + y * 480, src + (vy + y) * LOG_W + vx, 480 * 2);
    } else {
        scale_640x480_to_480x320_565(src, g_land480);
    }

    if (w->fb_rot90) {
        for (int ty = 0; ty < 320; ty += 16) {
            for (int tx = 0; tx < 480; tx += 16) {
                for (int y = ty; y < ty + 16; y++) {
                    const uint16_t *srow = g_land480 + y * 480;
                    int dst_col = 319 - y;
                    for (int x = tx; x < tx + 16; x++)
                        g_rot480[x * 320 + dst_col] = srow[x];
                }
            }
        }
        for (int x = 0; x < 480; x++)
            memcpy(fb16 + x * stride16, g_rot480 + x * 320, 320 * 2);
    } else if (w->fb_bpp == 16) {
        int cw = w->fb_w < 480 ? w->fb_w : 480;
        int ch = w->fb_h < 320 ? w->fb_h : 320;
        for (int y = 0; y < ch; y++)
            memcpy(fb16 + y * stride16, g_land480 + y * 480, (size_t)cw * 2);
    }
    pthread_mutex_unlock(&g_fb_output);
}

static void *fb0_worker_fn(void *arg) {
    SDL_Window *w = (SDL_Window *)arg;
    for (;;) {
        bool crop;
        int mx, my;
        const uint16_t *src;
        pthread_mutex_lock(&g_fb_mtx);
        while (!g_fb_has_job && !g_fb_stop) pthread_cond_wait(&g_fb_cv, &g_fb_mtx);
        if (g_fb_stop && !g_fb_has_job) { pthread_mutex_unlock(&g_fb_mtx); break; }
        int tmp = g_fb_front; g_fb_front = g_fb_ready; g_fb_ready = tmp;
        src = g_fb_stage[g_fb_front];
        crop = g_fb_job_1to1;
        mx = g_fb_job_mx; my = g_fb_job_my;
        g_fb_has_job = false;
        pthread_mutex_unlock(&g_fb_mtx);

        fb0_draw(w, src, crop, mx, my);
    }
    return NULL;
}

/* --- Direct ALSA Audio Engine (S32_LE stereo) --- */
#define AUDIO_RING_FLOATS (16384 * 2) /* 16K stereo frames = ~371 ms @ 44.1 kHz */
struct SDL_AudioStream {
    int pcm_fd;
    unsigned hw_rate;
    unsigned in_rate;
    bool speaker_amp;
    atomic_bool paused;
    atomic_bool stop;
    OgMixAudio mix;
    void *mix_data;
    pthread_t thread;
    bool thread_started;
    pthread_mutex_t mtx;
    float ring[AUDIO_RING_FLOATS];
    size_t rpos, wpos, count;
};

static void *audio_thread_fn(void *arg) {
    SDL_AudioStream *s = (SDL_AudioStream *)arg;
    int32_t out_buf[512 * 2];
    float ftmp[1024 * 2];
    const int ratio = (s->hw_rate > 0 && s->in_rate >= s->hw_rate * 2) ? 2 : 1;
    bool pcm_active = false;
    while (!atomic_load(&s->stop)) {
        if (atomic_load(&s->paused)) {
            if (pcm_active) {
                ioctl(s->pcm_fd, SNDRV_PCM_IOCTL_DROP);
                ioctl(s->pcm_fd, SNDRV_PCM_IOCTL_PREPARE);
                pcm_active = false;
            }
            usleep(2000);
            continue;
        }
        size_t need_floats = (size_t)(512 * 2 * ratio);
        if (s->mix) {
            s->mix(s->mix_data, ftmp, need_floats);
        } else {
            size_t got = 0;
            pthread_mutex_lock(&s->mtx);
            while (got < need_floats && s->count > 0) {
                ftmp[got++] = s->ring[s->rpos];
                s->rpos = (s->rpos + 1) % AUDIO_RING_FLOATS;
                s->count--;
            }
            pthread_mutex_unlock(&s->mtx);
            memset(ftmp + got, 0, (need_floats - got) * sizeof(float));
        }
        if (ratio == 2) {
            for (int i = 0; i < 512; i++) {
                float fl = 0.5f * (ftmp[i * 4 + 0] + ftmp[i * 4 + 2]);
                float fr = 0.5f * (ftmp[i * 4 + 1] + ftmp[i * 4 + 3]);
                if (fl > 1.0f) fl = 1.0f; else if (fl < -1.0f) fl = -1.0f;
                if (fr > 1.0f) fr = 1.0f; else if (fr < -1.0f) fr = -1.0f;
                out_buf[i * 2 + 0] = (int32_t)(fl * 2147483000.0f);
                out_buf[i * 2 + 1] = (int32_t)(fr * 2147483000.0f);
            }
        } else {
            for (int i = 0; i < 512 * 2; i++) {
                float v = ftmp[i];
                if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
                out_buf[i] = (int32_t)(v * 2147483000.0f);
            }
        }
        snd_pcm_uframes_t sent = 0;
        pcm_active = true;
        while (sent < 512 && !atomic_load(&s->stop) && !atomic_load(&s->paused)) {
            struct snd_xferi xfer = { .buf = out_buf + sent * 2, .frames = 512 - sent };
            snd_pcm_sframes_t written = ioctl(s->pcm_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xfer) < 0
                ? -errno : xfer.result;
            if (written > 0) {
                sent += written;
            } else if (written == -EPIPE || written == -ESTRPIPE) {
                if (ioctl(s->pcm_fd, SNDRV_PCM_IOCTL_PREPARE) < 0) break;
            } else if (written != -EINTR) {
                usleep(2000);
                break;
            }
        }
    }
    return NULL;
}

static bool alsa_configure(int fd, unsigned rate) {
    struct snd_pcm_hw_params p;
    memset(&p, 0, sizeof(p));
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++) {
        p.intervals[i].min = 0;
        p.intervals[i].max = ~0u;
    }
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
        memset(&p.masks[i], 0xFF, sizeof(p.masks[i]));
    p.masks[SNDRV_PCM_HW_PARAM_ACCESS - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[0] = 1u << SNDRV_PCM_ACCESS_RW_INTERLEAVED;
    p.masks[SNDRV_PCM_HW_PARAM_FORMAT - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[0] = 1u << SNDRV_PCM_FORMAT_S32_LE;
    p.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 2;
    p.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 2;
    p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = rate;
    p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = rate;
    p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 256;
    p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 1024;
    p.intervals[SNDRV_PCM_HW_PARAM_PERIODS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 2;
    p.intervals[SNDRV_PCM_HW_PARAM_PERIODS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 4;
    p.rmask = ~0u;
    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &p) < 0) return false;
    snd_pcm_uframes_t period = p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    snd_pcm_uframes_t buffer = p.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min;
    if (period == 0 || buffer <= period) return false;
    struct snd_pcm_sw_params sw;
    memset(&sw, 0, sizeof(sw));
    sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    sw.period_step = 1;
    sw.avail_min = period;
    sw.start_threshold = buffer - period;
    sw.stop_threshold = buffer;
    sw.boundary = buffer;
    while (sw.boundary <= (LONG_MAX - buffer) / 2) sw.boundary *= 2;
    return ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) == 0 &&
           ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) == 0;
}

static SDL_AudioStream *open_audio(const SDL_AudioSpec *spec, OgMixAudio mix, void *userdata) {
    SDL_AudioStream *s = (SDL_AudioStream *)calloc(1, sizeof(SDL_AudioStream));
    if (!s) return NULL;
    s->pcm_fd = -1;
    s->in_rate = (spec && spec->freq > 0) ? (unsigned)spec->freq : 44100u;
    atomic_init(&s->paused, true);
    atomic_init(&s->stop, false);
    s->mix = mix;
    s->mix_data = userdata;
    pthread_mutex_init(&s->mtx, NULL);

    bool prefer_hdmi = (g_win && g_win->use_x11);
    if (prefer_hdmi) {
        int afd = open("/etc/asound.conf", O_RDONLY);
        if (afd >= 0) {
            char abuf[512] = {0};
            ssize_t n = read(afd, abuf, sizeof(abuf) - 1);
            close(afd);
            if (n > 0 && (strstr(abuf, "card 0") || strstr(abuf, "hw:0,0"))) prefer_hdmi = false;
        }
    }
    if (prefer_hdmi) s->pcm_fd = open("/dev/snd/pcmC1D0p", O_RDWR);
    if (s->pcm_fd < 0) {
        s->pcm_fd = open("/dev/snd/pcmC0D0p", O_RDWR);
        if (s->pcm_fd >= 0) {
            (void)system("LD_LIBRARY_PATH=/lib /bin/tinymix 35 1 >/dev/null 2>&1");
            s->speaker_amp = true;
        }
    }
    if (s->pcm_fd >= 0) {
        if (alsa_configure(s->pcm_fd, s->in_rate)) {
            s->hw_rate = s->in_rate;
        } else if (alsa_configure(s->pcm_fd, 22050u)) {
            s->hw_rate = 22050u;
        } else {
            close(s->pcm_fd);
            s->pcm_fd = -1;
        }
    }
    if (s->pcm_fd >= 0 && pthread_create(&s->thread, NULL, audio_thread_fn, s) == 0) {
        s->thread_started = true;
        return s;
    }
    og_sdl_DestroyAudioStream(s);
    return NULL;
}

SDL_AudioStream *og_sdl_OpenAudioDeviceStream(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec, void *cb, void *userdata) {
    (void)devid; (void)cb; (void)userdata;
    return open_audio(spec, NULL, NULL);
}

SDL_AudioStream *og_xgb35_OpenMixer(const SDL_AudioSpec *spec, OgMixAudio mix, void *userdata) {
    return open_audio(spec, mix, userdata);
}

bool og_sdl_ResumeAudioStreamDevice(SDL_AudioStream *s) { if (s) atomic_store(&s->paused, false); return s != NULL; }
bool og_sdl_PauseAudioStreamDevice(SDL_AudioStream *s)  { if (s) atomic_store(&s->paused, true);  return s != NULL; }
int og_sdl_GetAudioStreamQueued(SDL_AudioStream *s) {
    if (!s) return -1;
    if (s->pcm_fd < 0) return 44100; /* fake full queue when headless/no audio */
    pthread_mutex_lock(&s->mtx);
    int bytes = (int)(s->count * sizeof(float));
    pthread_mutex_unlock(&s->mtx);
    return bytes;
}
bool og_sdl_PutAudioStreamData(SDL_AudioStream *s, const void *buf, int len) {
    if (!s || !buf || len <= 0) return false;
    if (s->pcm_fd < 0) return true;
    const float *in = (const float *)buf;
    size_t n = (size_t)len / sizeof(float);
    pthread_mutex_lock(&s->mtx);
    for (size_t i = 0; i < n; i++) {
        if (s->count >= AUDIO_RING_FLOATS) break;
        s->ring[s->wpos] = in[i];
        s->wpos = (s->wpos + 1) % AUDIO_RING_FLOATS;
        s->count++;
    }
    pthread_mutex_unlock(&s->mtx);
    return true;
}
void og_sdl_DestroyAudioStream(SDL_AudioStream *s) {
    if (!s) return;
    atomic_store(&s->stop, true);
    if (s->thread_started) pthread_join(s->thread, NULL);
    if (s->pcm_fd >= 0) close(s->pcm_fd);
    pthread_mutex_destroy(&s->mtx);
    free(s);
}

/* --- Evdev Input (XGB35 Gamepad + USB Mouse/Keyboard) --- */
static void ev_init_axis(int fd, int code, AxisCal *a) {
    struct input_absinfo ai;
    memset(&ai, 0, sizeof(ai));
    if (ioctl(fd, EVIOCGABS(code), &ai) == 0 && ai.maximum > ai.minimum) {
        a->center = (ai.minimum + ai.maximum) / 2;
        a->half = (ai.maximum - ai.minimum) / 2;
        a->raw = ai.value;
    } else {
        a->center = 127; a->half = 127; a->raw = 127;
    }
}

static float stick_vel(const AxisCal *a, float max_px) {
    float n = (float)(a->raw - a->center) / (float)(a->half > 0 ? a->half : 127);
    if (n < -1.0f) n = -1.0f; else if (n > 1.0f) n = 1.0f;
    float m = fabsf(n);
    if (m <= 0.14f) return 0.0f;
    float u = (m - 0.14f) / 0.86f;
    float c = 0.30f * u + 0.70f * u * u;
    return (n < 0.0f ? -c : c) * max_px;
}

static void ev_rescan(EvdevHub *h, uint64_t now_ms) {
    if (now_ms - h->last_scan_ms < 2000 && h->last_scan_ms != 0) return;
    h->last_scan_ms = now_ms;
    for (int i = 0; i <= 16 && h->count < MAX_EVDEVS; i++) {
        bool dup = false;
        for (int k = 0; k < h->count; k++) if (h->devs[k].idx == i) { dup = true; break; }
        if (dup) continue;
        char path[32], name[64] = {0};
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        bool gp = (strstr(name, "adckeypad") || strstr(name, "gpio") ||
                   strstr(name, "joystick") || strstr(name, "matrix"));
        // Xorg is paused in SGX Debug and would replay the SELECT chords on resume.
        if (strstr(name, "adckeypad") && getenv("OG_SGX_DIRECT") && getenv("OG_SGX_PROFILE") &&
            ioctl(fd, EVIOCGRAB, 1) < 0)
            fprintf(stderr, "OpenGorky: gamepad grab: %s\n", strerror(errno));
        if (strstr(name, "adckeypad") || strstr(name, "joystick")) {
            ev_init_axis(fd, 0, &h->ax[0]);
            ev_init_axis(fd, 1, &h->ax[1]);
            ev_init_axis(fd, 3, &h->ax[2]);
            ev_init_axis(fd, 4, &h->ax[3]);
        }
        h->devs[h->count].fd = fd;
        h->devs[h->count].idx = i;
        h->devs[h->count].is_gamepad = gp;
        h->count++;
    }
}

static void ev_open(EvdevHub *h) {
    int km = open("/sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-adckeypad.0/input/input1/key_mode", O_WRONLY);
    if (km >= 0) { (void)write(km, "1\n", 2); close(km); }
    h->last_scan_ms = 0;
    h->last_tick_ms = og_sdl_GetTicks();
    ev_rescan(h, h->last_tick_ms);
}

static void ev_close(EvdevHub *h) {
    for (int i = 0; i < h->count; i++) if (h->devs[i].fd >= 0) close(h->devs[i].fd);
    h->count = 0;
}

static void translate_linux_key(uint16_t code, uint32_t *sym, int *scan) {
    *sym = 0; *scan = 0;
    if (code >= KEY_F5 && code <= KEY_F9) { *sym = OG_F5 + code - KEY_F5; return; }
    if (code >= KEY_Q && code <= KEY_P) { *sym = "qwertyuiop"[code - KEY_Q]; return; }
    if (code >= KEY_A && code <= KEY_L) { *sym = "asdfghjkl"[code - KEY_A]; return; }
    if (code >= KEY_Z && code <= KEY_M) { *sym = "zxcvbnm"[code - KEY_Z]; return; }
    if (code >= KEY_1 && code <= KEY_0) { *sym = "1234567890"[code - KEY_1]; return; }
    switch (code) {
    case KEY_ESC:       *sym = SDLK_ESCAPE; break;
    case KEY_ENTER:     *sym = SDLK_RETURN; break;
    case KEY_KPENTER:   *sym = SDLK_KP_ENTER; break;
    case KEY_SPACE:     *sym = SDLK_SPACE; break;
    case KEY_BACKSPACE: *sym = SDLK_BACKSPACE; break;
    case KEY_TAB:       *sym = SDLK_TAB; break;
    case KEY_MINUS:     *sym = SDLK_MINUS; break;
    case KEY_EQUAL:     *sym = SDLK_EQUALS; break;
    case KEY_LEFTBRACE: *sym = SDLK_LEFTBRACKET; break;
    case KEY_RIGHTBRACE:*sym = SDLK_RIGHTBRACKET; break;
    case KEY_GRAVE:     *sym = SDLK_GRAVE; break;
    case KEY_BACKSLASH: *sym = '\\'; break;
    case KEY_SEMICOLON: *sym = ';'; break;
    case KEY_APOSTROPHE:*sym = '\''; break;
    case KEY_COMMA:     *sym = ','; break;
    case KEY_DOT:       *sym = '.'; break;
    case KEY_SLASH:     *sym = '/'; break;
    case KEY_F1:  *sym = SDLK_F1; break;  case KEY_F2:  *sym = SDLK_F2; break;
    case KEY_F10: *sym = SDLK_F10; break; case KEY_F11: *sym = SDLK_F11; break;
    case KEY_HOME:     *sym = SDLK_HOME; break;
    case KEY_PAGEUP:   *sym = SDLK_PAGEUP; break;
    case KEY_END:      *sym = SDLK_END; break;
    case KEY_PAGEDOWN: *sym = SDLK_PAGEDOWN; break;
    case KEY_RIGHT: *sym = SDLK_RIGHT; *scan = SDL_SCANCODE_RIGHT; break;
    case KEY_LEFT:  *sym = SDLK_LEFT;  *scan = SDL_SCANCODE_LEFT; break;
    case KEY_DOWN:  *sym = SDLK_DOWN;  *scan = SDL_SCANCODE_DOWN; break;
    case KEY_UP:    *sym = SDLK_UP;    *scan = SDL_SCANCODE_UP; break;
    default: break;
    }
}

static void push_evdev_text(uint32_t sym, bool caps_lock) {
    if (!g_text_input || sym < 32 || sym > 126 ||
        (g_key_mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) return;
    bool shift = (g_key_mod & SDL_KMOD_SHIFT) != 0;
    if (sym >= 'a' && sym <= 'z') {
        if (shift != caps_lock) sym -= 'a' - 'A';
    } else if (shift) {
        static const char plain[] = "1234567890-=[]\\;',./\x60";
        static const char shifted[] = "!@#$%^&*()_+{}|:\"<>?~";
        const char *match = strchr(plain, (int)sym);
        if (match) sym = (unsigned char)shifted[match - plain];
    }
    const char text[] = { (char)sym, '\0' };
    push_text(text);
}

static void ev_poll(EvdevHub *h, bool x11_active) {
    static bool caps_lock;
    uint64_t now = og_sdl_GetTicks();
    ev_rescan(h, now);
    struct input_event ev[32];
    int rel_dx = 0, rel_dy = 0;

    for (int i = 0; i < h->count; ) {
        EvDev *d = &h->devs[i];
        bool disc = false;
        ssize_t n;
        while ((n = read(d->fd, ev, sizeof(ev))) > 0) {
            int cnt = (int)(n / (ssize_t)sizeof(ev[0]));
            for (int j = 0; j < cnt; j++) {
                const struct input_event *e = &ev[j];
                if (e->type == EV_REL && !x11_active) {
                    if (e->code == REL_X) rel_dx += e->value;
                    else if (e->code == REL_Y) rel_dy += e->value;
                    else if (e->code == REL_WHEEL && e->value != 0) push_mouse_wheel((float)e->value);
                } else if (e->type == EV_ABS) {
                    if (e->code == 0)      h->ax[0].raw = e->value;
                    else if (e->code == 1) h->ax[1].raw = e->value;
                    else if (e->code == 3) h->ax[2].raw = e->value;
                    else if (e->code == 4) h->ax[3].raw = e->value;
                } else if (e->type == EV_KEY && e->value != 2) {
                    bool dn = (e->value == 1);
                    if (!x11_active) {
                        if (e->code == BTN_LEFT)  { push_mouse_btn(dn, SDL_BUTTON_LEFT); continue; }
                        if (e->code == BTN_RIGHT) { push_mouse_btn(dn, SDL_BUTTON_RIGHT); continue; }
                    }
                    if (d->is_gamepad) {
                        static const uint16_t debug_codes[] = {352, 158, 139, 308, 407};
                        bool debug_event = false;
                        for (unsigned k = 0; k < sizeof(debug_codes)/sizeof(debug_codes[0]); k++) {
                            unsigned bit = 1u << k;
                            if (e->code != debug_codes[k]) continue;
                            if (dn && h->btn_sel) {
                                h->select_chord = true;
                                h->debug_buttons |= bit;
                            }
                            if (h->debug_buttons & bit) {
                                push_key(dn, OG_F5 + k, 0);
                                if (!dn) h->debug_buttons &= ~bit;
                                debug_event = true;
                            }
                            break;
                        }
                        if (debug_event) continue;
                        switch (e->code) {
                        case 314:
                            if (dn) h->select_chord = false;
                            else if (h->btn_sel && !h->select_chord) {
                                push_key(true, SDLK_F, 0);
                                push_key(false, SDLK_F, 0);
                            }
                            h->btn_sel = dn;
                            break;
                        case 315: h->btn_start = dn; push_key(dn, SDLK_ESCAPE, 0); break;/* START -> Esc / Menu */
                        case 352: push_mouse_btn(dn, SDL_BUTTON_LEFT); break;            /* A -> Left Click */
                        case 158: push_mouse_btn(dn, SDL_BUTTON_RIGHT); break;           /* B -> Right Click (aim/cancel) */
                        case 139: push_key(dn, SDLK_SPACE, 0); break;                    /* X -> Space (dialogue/panel) */
                        case 308: push_key(dn, SDLK_I, 0); break;                        /* Y -> Inventory */
                        case 412: h->btn_l1 = dn; push_key(dn, SDLK_Q, 0); break;        /* L1 -> Slow cursor + Next weapon */
                        case 407: push_key(dn, SDLK_E, 0); break;                        /* R1 -> End turn */
                        case 312: if (dn) g_handheld_1to1 = !g_handheld_1to1; break;     /* L2 -> Full / 1:1 Zoom */
                        case 313: h->btn_r2 = dn; push_key(dn, SDLK_TAB, 0); break;      /* R2 -> Battle grid */
                        case 103: h->dpad_u = dn; push_key(dn, SDLK_UP, SDL_SCANCODE_UP); break;
                        case 108: h->dpad_d = dn; push_key(dn, SDLK_DOWN, SDL_SCANCODE_DOWN); break;
                        case 105: h->dpad_l = dn; push_key(dn, SDLK_LEFT, SDL_SCANCODE_LEFT); break;
                        case 106: h->dpad_r = dn; push_key(dn, SDLK_RIGHT, SDL_SCANCODE_RIGHT); break;
                        case 115:
                            if (dn) {
                                g_hw_vol = (g_hw_vol + 4 > 40) ? 40 : g_hw_vol + 4;
                                char c[80]; snprintf(c, sizeof(c), "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1", g_hw_vol);
                                (void)system(c);
                            }
                            break;
                        case 114:
                            if (dn) {
                                g_hw_vol = (g_hw_vol - 4 < 0) ? 0 : g_hw_vol - 4;
                                char c[80]; snprintf(c, sizeof(c), "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1", g_hw_vol);
                                (void)system(c);
                            }
                            break;
                        case 174: if (dn) g_quit_req = 1; break;
                        }
                        if (h->btn_sel && h->btn_start) g_quit_req = 1;
                    } else if (!x11_active) {
                        static const uint16_t modifiers[] = {
                            [KEY_LEFTSHIFT] = SDL_KMOD_LSHIFT, [KEY_RIGHTSHIFT] = SDL_KMOD_RSHIFT,
                            [KEY_LEFTCTRL] = SDL_KMOD_LCTRL, [KEY_RIGHTCTRL] = SDL_KMOD_RCTRL,
                            [KEY_LEFTALT] = SDL_KMOD_LALT, [KEY_RIGHTALT] = SDL_KMOD_RALT,
                            [KEY_LEFTMETA] = SDL_KMOD_LGUI, [KEY_RIGHTMETA] = SDL_KMOD_RGUI,
                        };
                        uint16_t mod = e->code < sizeof(modifiers)/sizeof(modifiers[0]) ? modifiers[e->code] : 0;
                        if (mod) {
                            if (dn) g_key_mod |= mod; else g_key_mod &= ~mod;
                        } else if (dn && e->code == KEY_CAPSLOCK) {
                            caps_lock = !caps_lock;
                        } else if (dn && e->code == KEY_F2) {
                            g_handheld_1to1 = !g_handheld_1to1;
                        } else {
                            uint32_t sym = 0; int scan = 0;
                            translate_linux_key(e->code, &sym, &scan);
                            if (sym || scan) push_key(dn, sym, scan);
                            if (dn) push_evdev_text(sym, caps_lock);
                        }
                    }
                }
            }
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) disc = true;
        if (disc) {
            close(d->fd);
            h->devs[i] = h->devs[--h->count];
        } else {
            i++;
        }
    }

    /* Analog stick integration */
    uint64_t dt_ms = now - h->last_tick_ms;
    if (dt_ms > 100) dt_ms = 100;
    if (dt_ms >= 8) {
        h->last_tick_ms = now;
        float dt = (float)dt_ms / 1000.0f;
        float speed = h->btn_l1 ? 150.0f : 360.0f;
        float vx = stick_vel(&h->ax[0], speed) * dt;
        float vy = stick_vel(&h->ax[1], speed) * dt;
        if (rel_dx != 0 || rel_dy != 0 || vx != 0.0f || vy != 0.0f) {
            push_mouse_move(g_mouse_x + (float)rel_dx + vx, g_mouse_y + (float)rel_dy + vy, (float)rel_dx + vx, (float)rel_dy + vy);
        }
        /* Right stick -> arrow keys for smooth map scroll */
        float rx = stick_vel(&h->ax[2], 1.0f);
        float ry = stick_vel(&h->ax[3], 1.0f);
        bool nl = (rx < -0.25f), nr = (rx > 0.25f), nu = (ry < -0.25f), nd = (ry > 0.25f);
        if (nl != h->pan_l) { h->pan_l = nl; g_key_state[SDL_SCANCODE_LEFT]  = nl || h->dpad_l; }
        if (nr != h->pan_r) { h->pan_r = nr; g_key_state[SDL_SCANCODE_RIGHT] = nr || h->dpad_r; }
        if (nu != h->pan_u) { h->pan_u = nu; g_key_state[SDL_SCANCODE_UP]    = nu || h->dpad_u; }
        if (nd != h->pan_d) { h->pan_d = nd; g_key_state[SDL_SCANCODE_DOWN]  = nd || h->dpad_d; }
    } else if (rel_dx != 0 || rel_dy != 0) {
        push_mouse_move(g_mouse_x + (float)rel_dx, g_mouse_y + (float)rel_dy, (float)rel_dx, (float)rel_dy);
    }
}

/* --- X11 helpers --- */
static bool x11_load(X11Api *x) {
    x->libx11 = dlopen("libX11.so.6", RTLD_LAZY);
    if (!x->libx11) return false;
    static bool threads_checked;
    if (!threads_checked) {
        int (*init_threads)(void) = dlsym(x->libx11, "XInitThreads");
        g_x11_threaded = init_threads && init_threads();
        threads_checked = true;
    }
#define LOAD(n) x->n = dlsym(x->libx11, "X" #n); if (!x->n) return false
    LOAD(OpenDisplay); LOAD(CloseDisplay); LOAD(CreateSimpleWindow);
    LOAD(SelectInput); LOAD(MapWindow); LOAD(StoreName);
    LOAD(InternAtom); LOAD(SetWMProtocols); LOAD(Pending); LOAD(NextEvent);
    LOAD(CreateGC); LOAD(FreeGC); LOAD(CreateImage); LOAD(PutImage);
    LOAD(Flush); LOAD(Sync); LOAD(SendEvent); LOAD(LookupKeysym);
    LOAD(LookupString); LOAD(GetWindowAttributes);
    LOAD(CreateBitmapFromData); LOAD(CreatePixmapCursor); LOAD(DefineCursor);
    LOAD(FreeCursor); LOAD(FreePixmap);
#undef LOAD
    x->libxext = dlopen("libXext.so.6", RTLD_LAZY);
    if (x->libxext) {
        x->ShmQueryExtension = dlsym(x->libxext, "XShmQueryExtension");
        x->ShmCreateImage    = dlsym(x->libxext, "XShmCreateImage");
        x->ShmAttach         = dlsym(x->libxext, "XShmAttach");
        x->ShmDetach         = dlsym(x->libxext, "XShmDetach");
        x->ShmPutImage       = dlsym(x->libxext, "XShmPutImage");
    }
    return true;
}

static void x11_destroy_img(SDL_Window *w) {
    if (!w->ximg) return;
    if (w->use_shm && w->x11.ShmDetach) {
        w->x11.ShmDetach(w->dpy, &w->shminfo);
        shmdt(w->shminfo.shmaddr);
        shmctl(w->shminfo.shmid, IPC_RMID, NULL);
        w->ximg->data = NULL;
    } else if (w->xbuf) {
        free(w->xbuf);
        w->ximg->data = NULL;
    }
    XDestroyImage(w->ximg);
    w->ximg = NULL;
    w->xbuf = NULL;
    w->use_shm = false;
}

static void x11_ensure_img(SDL_Window *w, int bw, int bh) {
    if (w->ximg && w->xbuf_w == bw && w->xbuf_h == bh) return;
    x11_destroy_img(w);
    Visual *vis = DefaultVisual(w->dpy, w->screen);
    int bpp = (w->x11_depth <= 16) ? 2 : 4;
    if (w->x11.ShmQueryExtension && w->x11.ShmCreateImage && w->x11.ShmQueryExtension(w->dpy)) {
        XImage *im = w->x11.ShmCreateImage(w->dpy, vis, (unsigned)w->x11_depth, ZPixmap, NULL, &w->shminfo, (unsigned)bw, (unsigned)bh);
        if (im) {
            w->shminfo.shmid = shmget(IPC_PRIVATE, (size_t)im->bytes_per_line * (size_t)bh, IPC_CREAT | 0600);
            if (w->shminfo.shmid >= 0) {
                w->shminfo.shmaddr = im->data = (char *)shmat(w->shminfo.shmid, NULL, 0);
                w->shminfo.readOnly = False;
                if (w->shminfo.shmaddr != (char *)-1 && w->x11.ShmAttach(w->dpy, &w->shminfo)) {
                    w->x11.Sync(w->dpy, False);
                    w->ximg = im;
                    w->xbuf = (uint8_t *)im->data;
                    w->xbuf_w = bw; w->xbuf_h = bh; w->xbuf_bpp = bpp;
                    w->use_shm = true;
                    return;
                }
                if (w->shminfo.shmaddr != (char *)-1) shmdt(w->shminfo.shmaddr);
                shmctl(w->shminfo.shmid, IPC_RMID, NULL);
            }
            im->data = NULL;
            XDestroyImage(im);
        }
    }
    w->xbuf = (uint8_t *)calloc((size_t)bw * (size_t)bh, (size_t)bpp);
    w->ximg = w->x11.CreateImage(w->dpy, vis, (unsigned)w->x11_depth, ZPixmap, 0, (char *)w->xbuf, (unsigned)bw, (unsigned)bh, bpp * 8, bw * bpp);
    w->xbuf_w = bw; w->xbuf_h = bh; w->xbuf_bpp = bpp;
}

static void x11_set_fs(SDL_Window *w, bool fs) {
    if (!w || !w->use_x11) return;
    w->fullscreen = fs;
    if (!fs) de_restore();
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = w->win;
    ev.xclient.message_type = w->net_wm_state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = fs ? 1 : 0;
    ev.xclient.data.l[1] = (long)w->net_wm_fs;
    w->x11.SendEvent(w->dpy, DefaultRootWindow(w->dpy), False, SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    w->x11.Flush(w->dpy);
}

static void x11_map_coords(SDL_Window *w, int wx, int wy, float *lx, float *ly) {
    int ww = w->w > 0 ? w->w : LOG_W;
    int wh = w->h > 0 ? w->h : LOG_H;
    /* 4:3 letterboxed viewport */
    int vh = wh, vw = (vh * LOG_W) / LOG_H;
    if (vw > ww) { vw = ww; vh = (vw * LOG_H) / LOG_W; }
    int ox = (ww - vw) / 2, oy = (wh - vh) / 2;
    *lx = (float)(wx - ox) * (float)LOG_W / (float)(vw > 0 ? vw : 1);
    *ly = (float)(wy - oy) * (float)LOG_H / (float)(vh > 0 ? vh : 1);
    if (*lx < 0.0f) *lx = 0.0f; else if (*lx > LOG_W - 1) *lx = LOG_W - 1;
    if (*ly < 0.0f) *ly = 0.0f; else if (*ly > LOG_H - 1) *ly = LOG_H - 1;
}

static void translate_x11_keysym(KeySym ks, uint32_t *sym, int *scan) {
    *sym = 0; *scan = 0;
    if (ks >= XK_F5 && ks <= XK_F9) { *sym = OG_F5 + ks - XK_F5; return; }
    if (ks >= 'a' && ks <= 'z') { *sym = (uint32_t)ks; return; }
    if (ks >= 'A' && ks <= 'Z') { *sym = (uint32_t)(ks - 'A' + 'a'); return; }
    if (ks >= '0' && ks <= '9') { *sym = (uint32_t)ks; return; }
    switch (ks) {
    case XK_Escape:    *sym = SDLK_ESCAPE; break;
    case XK_Return:    *sym = SDLK_RETURN; break;
    case XK_KP_Enter:  *sym = SDLK_KP_ENTER; break;
    case XK_space:     *sym = SDLK_SPACE; break;
    case XK_BackSpace: *sym = SDLK_BACKSPACE; break;
    case XK_Tab:       *sym = SDLK_TAB; break;
    case XK_minus:     *sym = SDLK_MINUS; break;
    case XK_equal:     *sym = SDLK_EQUALS; break;
    case XK_bracketleft:  *sym = SDLK_LEFTBRACKET; break;
    case XK_bracketright: *sym = SDLK_RIGHTBRACKET; break;
    case XK_grave:     *sym = SDLK_GRAVE; break;
    case XK_F1:  *sym = SDLK_F1; break;  case XK_F2:  *sym = SDLK_F2; break;
    case XK_F10: *sym = SDLK_F10; break; case XK_F11: *sym = SDLK_F11; break;
    case XK_Home:      *sym = SDLK_HOME; break;
    case XK_Page_Up:   *sym = SDLK_PAGEUP; break;
    case XK_End:       *sym = SDLK_END; break;
    case XK_Page_Down: *sym = SDLK_PAGEDOWN; break;
    case XK_Right: *sym = SDLK_RIGHT; *scan = SDL_SCANCODE_RIGHT; break;
    case XK_Left:  *sym = SDLK_LEFT;  *scan = SDL_SCANCODE_LEFT; break;
    case XK_Down:  *sym = SDLK_DOWN;  *scan = SDL_SCANCODE_DOWN; break;
    case XK_Up:    *sym = SDLK_UP;    *scan = SDL_SCANCODE_UP; break;
    default: break;
    }
}

static void x11_pump(SDL_Window *w) {
    if (!w || !w->use_x11) return;
    while (w->x11.Pending(w->dpy) > 0) {
        XEvent xe;
        w->x11.NextEvent(w->dpy, &xe);
        switch (xe.type) {
        case ClientMessage:
            if ((Atom)xe.xclient.data.l[0] == w->wm_delete) g_quit_req = 1;
            break;
        case ConfigureNotify:
            if (xe.xconfigure.width != w->w || xe.xconfigure.height != w->h) {
                w->w = xe.xconfigure.width;
                w->h = xe.xconfigure.height;
                SDL_Event ev; memset(&ev, 0, sizeof(ev));
                ev.type = SDL_EVENT_WINDOW_RESIZED;
                ev.window.windowID = 1; ev.window.data1 = w->w; ev.window.data2 = w->h;
                evq_push(&ev);
            }
            break;
        case MotionNotify: {
            float lx, ly;
            x11_map_coords(w, xe.xmotion.x, xe.xmotion.y, &lx, &ly);
            push_mouse_move(lx, ly, lx - g_mouse_x, ly - g_mouse_y);
            break;
        }
        case ButtonPress:
        case ButtonRelease: {
            bool dn = (xe.type == ButtonPress);
            float lx, ly;
            x11_map_coords(w, xe.xbutton.x, xe.xbutton.y, &lx, &ly);
            g_mouse_x = lx; g_mouse_y = ly;
            if (xe.xbutton.button == 1) push_mouse_btn(dn, SDL_BUTTON_LEFT);
            else if (xe.xbutton.button == 3) push_mouse_btn(dn, SDL_BUTTON_RIGHT);
            else if (dn && xe.xbutton.button == 4) push_mouse_wheel(1.0f);
            else if (dn && xe.xbutton.button == 5) push_mouse_wheel(-1.0f);
            break;
        }
        case KeyPress:
        case KeyRelease: {
            bool dn = (xe.type == KeyPress);
            g_key_mod = 0;
            if (xe.xkey.state & ShiftMask)   g_key_mod |= SDL_KMOD_LSHIFT;
            if (xe.xkey.state & ControlMask) g_key_mod |= SDL_KMOD_LCTRL;
            if (xe.xkey.state & Mod1Mask)    g_key_mod |= SDL_KMOD_LALT;
            KeySym ks = w->x11.LookupKeysym(&xe.xkey, 0);
            if (dn && ks == XK_F11) { x11_set_fs(w, !w->fullscreen); break; }
            uint32_t sym = 0; int scan = 0;
            translate_x11_keysym(ks, &sym, &scan);
            if (sym || scan) push_key(dn, sym, scan);
            if (dn && g_text_input) {
                char buf[32] = {0};
                int n = w->x11.LookupString(&xe.xkey, buf, sizeof(buf) - 1, NULL, NULL);
                if (n > 0 && (unsigned char)buf[0] >= 32) push_text(buf);
            }
            break;
        }
        case LeaveNotify: {
            SDL_Event ev; memset(&ev, 0, sizeof(ev));
            ev.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
            ev.window.windowID = 1;
            evq_push(&ev);
            break;
        }
        default: break;
        }
    }
}

/* --- Init / Window / Lifecycle --- */
bool og_sdl_Init(uint32_t flags) {
    (void)flags;
    if (!g_t0_ns) g_t0_ns = now_ns();
    gov_engage();
    signal(SIGINT, on_term_sig);
    signal(SIGTERM, on_term_sig);
    signal(SIGSEGV, on_crash_sig);
    signal(SIGABRT, on_crash_sig);
    signal(SIGBUS, on_crash_sig);
    signal(SIGFPE, on_crash_sig);
    atexit(de_shutdown);
    atexit(gov_restore);
    return true;
}
bool og_sdl_InitSubSystem(uint32_t flags) { (void)flags; return true; }
void og_sdl_QuitSubSystem(uint32_t flags) { (void)flags; }
void og_sdl_Quit(void) {
    for (unsigned i=0;i<model_trace_count;i++)
        fprintf(stderr,"MTRACE %c %p %llu\n",model_trace[i].tag,model_trace[i].caller,
                (unsigned long long)model_trace[i].ns);
    de_shutdown(); gov_restore();
}
const char *og_sdl_GetError(void) { return ""; }
void og_sdl_free(void *mem) { free(mem); }

bool og_sdl_CreateWindowAndRenderer(const char *title, int w, int h, SDL_WindowFlags flags, SDL_Window **window, SDL_Renderer **renderer) {
    (void)w; (void)h;
    SDL_Window *win = (SDL_Window *)calloc(1, sizeof(SDL_Window));
    SDL_Renderer *ren = (SDL_Renderer *)calloc(1, sizeof(SDL_Renderer));
    if (!win || !ren) { free(win); free(ren); return false; }
    win->dfb_fd = -1;
    win->fb_fd = -1;
    win->w = LOG_W;
    win->h = LOG_H;
    if (!g_fb_stage[0]) g_fb_stage[0] = (uint16_t *)calloc(LOG_W * LOG_H, sizeof(uint16_t));
    if (!g_fb_stage[1]) g_fb_stage[1] = (uint16_t *)calloc(LOG_W * LOG_H, sizeof(uint16_t));
    if (!g_fb_stage[2]) g_fb_stage[2] = (uint16_t *)calloc(LOG_W * LOG_H, sizeof(uint16_t));
    ren->win = win;
    ren->backbuf = g_fb_stage[g_fb_back];
    ren->scale_x = 1.0f; ren->scale_y = 1.0f;
    ren->log_w = LOG_W; ren->log_h = LOG_H;

    const char *mode = getenv("OG_MODE");
    const char *dpy_env = getenv("DISPLAY");
    bool want_x11 = (mode && strcmp(mode, "x11") == 0) || (!mode && dpy_env && dpy_env[0]);

    if (want_x11 && x11_load(&win->x11)) {
        win->dpy = win->x11.OpenDisplay(NULL);
        if (win->dpy) {
            win->use_x11 = true;
            win->screen = DefaultScreen(win->dpy);
            win->x11_depth = DefaultDepth(win->dpy, win->screen);
            int sw = DisplayWidth(win->dpy, win->screen);
            int sh = DisplayHeight(win->dpy, win->screen);
            bool start_fs = (flags & SDL_WINDOW_FULLSCREEN) || (getenv("OG_FULLSCREEN") && getenv("OG_FULLSCREEN")[0] == '1');
            win->w = start_fs ? sw : LOG_W;
            win->h = start_fs ? sh : LOG_H;
            win->win = win->x11.CreateSimpleWindow(win->dpy, DefaultRootWindow(win->dpy), 0, 0,
                                                   (unsigned)win->w, (unsigned)win->h, 0, 0, 0);
            win->x11.StoreName(win->dpy, win->win, title ? title : "OpenGorky");
            win->x11.SelectInput(win->dpy, win->win,
                ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                PointerMotionMask | ButtonPressMask | ButtonReleaseMask | LeaveWindowMask);
            win->wm_delete = win->x11.InternAtom(win->dpy, "WM_DELETE_WINDOW", False);
            win->net_wm_state = win->x11.InternAtom(win->dpy, "_NET_WM_STATE", False);
            win->net_wm_fs = win->x11.InternAtom(win->dpy, "_NET_WM_STATE_FULLSCREEN", False);
            win->x11.SetWMProtocols(win->dpy, win->win, &win->wm_delete, 1);
            win->gc = win->x11.CreateGC(win->dpy, win->win, 0, NULL);
            /* Hide X11 cursor since Gorky draws its own software cursor */
            char blank[8] = {0};
            Pixmap pm = win->x11.CreateBitmapFromData(win->dpy, win->win, blank, 1, 1);
            XColor col = {0};
            Cursor cur = win->x11.CreatePixmapCursor(win->dpy, pm, pm, &col, &col, 0, 0);
            win->x11.DefineCursor(win->dpy, win->win, cur);
            win->x11.FreeCursor(win->dpy, cur);
            win->x11.FreePixmap(win->dpy, pm);
            win->x11.MapWindow(win->dpy, win->win);
            if (start_fs) x11_set_fs(win, true);
            win->x11.Flush(win->dpy);

            /* Probe Direct /dev/fb1 (HDMI) + ATM7051H DE_ML0 hardware scaler! */
            int fd = open("/dev/fb1", O_RDWR);
            if (fd >= 0) {
                struct fb_var_screeninfo dv; struct fb_fix_screeninfo df;
                if (ioctl(fd, FBIOGET_VSCREENINFO, &dv) == 0 &&
                    ioctl(fd, FBIOGET_FSCREENINFO, &df) == 0 &&
                    dv.bits_per_pixel == 16 && (int)dv.xres == sw && (int)dv.yres == sh) {
                    void *m = mmap(NULL, df.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                    if (m != MAP_FAILED) {
                        win->dfb_fd = fd;
                        win->dfb_raw = (uint8_t *)m;
                        win->dfb_len = df.smem_len;
                        win->dfb_pitch = df.line_length;
                        win->dfb_ptr = win->dfb_raw + (size_t)dv.yoffset * win->dfb_pitch + (size_t)dv.xoffset * 2;
                        win->dfb_w = (int)dv.xres;
                        win->dfb_h = (int)dv.yres;
                        win->dfb_de_ok = de_init(win->dfb_w, win->dfb_h, win->dfb_pitch);
                        fprintf(stderr, "[OpenGorky/X11] Direct /dev/fb1 %dx%d RGB565, DE_ML0 hw-scaler=%s\n",
                                win->dfb_w, win->dfb_h, win->dfb_de_ok ? "ON (0ms CPU)" : "off");
                    } else close(fd);
                } else close(fd);
            }
        }
    }

    if (!win->use_x11) {
        win->fb_fd = open("/dev/fb0", O_RDWR);
        if (win->fb_fd >= 0) {
            struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
            ioctl(win->fb_fd, FBIOGET_VSCREENINFO, &v);
            ioctl(win->fb_fd, FBIOGET_FSCREENINFO, &f);
            ioctl(win->fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
            void *m = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, win->fb_fd, 0);
            if (m != MAP_FAILED) {
                win->fb_raw = (uint8_t *)m;
                win->fb_len = f.smem_len;
                win->fb_pitch = f.line_length;
                win->fb_w = (int)v.xres;
                win->fb_h = (int)v.yres;
                win->fb_bpp = (int)v.bits_per_pixel;
                win->fb_rot90 = (v.xres == 320 && v.yres == 480);
                memset(win->fb_raw, 0, win->fb_len);
                fprintf(stderr, "[OpenGorky/FB0] %dx%d %dbpp rot90=%d (dual-core pipelined)\n", win->fb_w, win->fb_h, win->fb_bpp, win->fb_rot90);
                g_fb_stop = false;
                g_fb_has_job = false;
                if (pthread_create(&g_fb_thread, NULL, fb0_worker_fn, win) == 0) g_fb_thread_started = true;
            } else {
                close(win->fb_fd); win->fb_fd = -1;
            }
        }
    }

    ev_open(&win->evdev);
    g_win = win;
    if (!gpu_open()) {
        og_sdl_DestroyWindow(win);
        free(ren);
        return false;
    }
    g_ren = ren;
    *window = win;
    *renderer = ren;
    og_sdl_SetRenderDrawColor(ren, 0, 0, 0, 255);
    og_sdl_RenderClear(ren);
    og_sdl_RenderPresent(ren);
    return true;
}

static void stop_menu_worker(void);
static void close_sgx_menu(bool reopen);

void og_sdl_DestroyRenderer(SDL_Renderer *r) {
    og_xgb35_EndLoading();
    stop_menu_worker();
    close_sgx_menu(true);
    if (gpu) {
        if (gpu_reclaimed) fprintf(stderr,"OpenGorky: released %zu KiB of CPU texture copies\n",gpu_reclaimed/1024);
        gpu_reclaimed=0;
        gpu->close();
        gpu=NULL;
        dlclose(gpu_library);
        gpu_library=NULL;
    }
    if (r == g_ren) g_ren = NULL;
    free(r);
}
void og_sdl_DestroyWindow(SDL_Window *w) {
    if (!w) return;
    if (g_fb_thread_started) {
        pthread_mutex_lock(&g_fb_mtx);
        g_fb_stop = true;
        pthread_cond_signal(&g_fb_cv);
        pthread_mutex_unlock(&g_fb_mtx);
        pthread_join(g_fb_thread, NULL);
        g_fb_thread_started = false;
    }
    ev_close(&w->evdev);
    de_shutdown();
    if (w->dfb_raw) munmap(w->dfb_raw, w->dfb_len);
    if (w->dfb_fd >= 0) close(w->dfb_fd);
    if (w->fb_raw) munmap(w->fb_raw, w->fb_len);
    if (w->fb_fd >= 0) close(w->fb_fd);
    if (w->use_x11) {
        x11_destroy_img(w);
        if (w->gc) w->x11.FreeGC(w->dpy, w->gc);
        if (w->dpy) w->x11.CloseDisplay(w->dpy);
    }
    if (w == g_win) g_win = NULL;
    free(w);
}

uint32_t og_sdl_GetWindowID(SDL_Window *w) { return w ? 1u : 0u; }
SDL_WindowFlags og_sdl_GetWindowFlags(SDL_Window *w) {
    if (!w) return 0;
    return SDL_WINDOW_INPUT_FOCUS | (w->fullscreen ? SDL_WINDOW_FULLSCREEN : 0u);
}
bool og_sdl_GetWindowSize(SDL_Window *w, int *out_w, int *out_h) {
    (void)w;
    if (out_w) *out_w = LOG_W;
    if (out_h) *out_h = LOG_H;
    return true;
}
float og_sdl_GetWindowPixelDensity(SDL_Window *w) { (void)w; return 1.0f; }
bool og_sdl_SetWindowMinimumSize(SDL_Window *w, int min_w, int min_h) { (void)w; (void)min_w; (void)min_h; return true; }
bool og_sdl_SetWindowFullscreen(SDL_Window *w, bool fullscreen) { if (w && w->use_x11) x11_set_fs(w, fullscreen); return true; }
bool og_sdl_SetWindowIcon(SDL_Window *w, SDL_Surface *icon) { (void)w; (void)icon; return true; }
bool og_sdl_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *msg, SDL_Window *w) {
    (void)flags; (void)w;
    fprintf(stderr, "[%s] %s\n", title ? title : "OpenGorky", msg ? msg : "");
    return true;
}
void og_sdl_ShowOpenFolderDialog(SDL_DialogFileCallback cb, void *userdata, SDL_Window *w, const char *default_loc, bool allow_many) {
    (void)w; (void)default_loc; (void)allow_many;
    if (cb) { const char *empty[1] = { NULL }; cb(userdata, empty, -1); }
}
SDL_Locale **og_sdl_GetPreferredLocales(int *count) {
    static SDL_Locale ru = { "ru", "RU" };
    SDL_Locale **arr = (SDL_Locale **)malloc(sizeof(SDL_Locale *));
    if (!arr) { if (count) *count = 0; return NULL; }
    arr[0] = &ru;
    if (count) *count = 1;
    return arr;
}

/* --- Render state --- */
bool og_sdl_SetRenderVSync(SDL_Renderer *r, int vsync) { (void)r; (void)vsync; return true; }
bool og_sdl_GetRenderOutputSize(SDL_Renderer *r, int *w, int *h) {
    if (r && r->target) { if (w) *w = r->target->w; if (h) *h = r->target->h; return true; }
    if (w) *w = LOG_W;
    if (h) *h = LOG_H;
    return true;
}
bool og_sdl_SetRenderLogicalPresentation(SDL_Renderer *r, int w, int h, int mode) {
    if (!r) return false;
    r->log_w = w > 0 ? w : LOG_W;
    r->log_h = h > 0 ? h : LOG_H;
    r->log_mode = mode;
    return true;
}
bool og_sdl_SetRenderScale(SDL_Renderer *r, float sx, float sy) {
    if (!r) return false;
    r->scale_x = sx > 0.0f ? sx : 1.0f;
    r->scale_y = sy > 0.0f ? sy : 1.0f;
    return true;
}
bool og_sdl_SetRenderViewport(SDL_Renderer *r, const SDL_Rect *rect) {
    if (!r) return false;
    if (rect) { r->has_vp = true; r->vp = *rect; } else r->has_vp = false;
    return true;
}
bool og_sdl_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t) {
    if (!r) return false;
    if (gpu) {
        if (t && !gpu_texture(t)) return false;
        gpu->target(t?&t->gpu:NULL,t?t->w:LOG_W,t?t->h:LOG_H);
    }
    r->target = t;
    r->has_clip = false;
    r->has_vp = false;
    return true;
}
SDL_Texture *og_sdl_GetRenderTarget(SDL_Renderer *r) { return r ? r->target : NULL; }
bool og_sdl_SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode mode) { if (r) r->draw_blend = mode; return r != NULL; }
bool og_sdl_SetRenderDrawColor(SDL_Renderer *r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    if (!r) return false;
    r->dr = cr; r->dg = cg; r->db = cb; r->da = ca;
    return true;
}
bool og_sdl_SetRenderDrawColorFloat(SDL_Renderer *r, float cr, float cg, float cb, float ca) {
    return og_sdl_SetRenderDrawColor(r, clamp_u8f(cr), clamp_u8f(cg), clamp_u8f(cb), clamp_u8f(ca));
}
bool og_sdl_SetRenderClipRect(SDL_Renderer *r, const SDL_Rect *rect) {
    if (!r) return false;
    if (rect) { r->has_clip = true; r->clip = *rect; } else r->has_clip = false;
    return true;
}
bool og_sdl_GetRectIntersection(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *res) {
    if (!a || !b || !res) return false;
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int y1 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    if (x1 <= x0 || y1 <= y0) { res->x = res->y = res->w = res->h = 0; return false; }
    res->x = x0; res->y = y0; res->w = x1 - x0; res->h = y1 - y0;
    return true;
}

/* --- Drawing target abstraction --- */
static void get_target_bounds(SDL_Renderer *r, int *tw, int *th, int *cx0, int *cy0, int *cx1, int *cy1) {
    *tw = r->target ? r->target->w : LOG_W;
    *th = r->target ? r->target->h : LOG_H;
    int vx = r->has_vp ? r->vp.x : 0;
    int vy = r->has_vp ? r->vp.y : 0;
    int vw = r->has_vp ? r->vp.w : *tw;
    int vh = r->has_vp ? r->vp.h : *th;
    *cx0 = vx; *cy0 = vy; *cx1 = vx + vw; *cy1 = vy + vh;
    if (r->has_clip) {
        int x0 = vx + r->clip.x, y0 = vy + r->clip.y;
        int x1 = x0 + r->clip.w, y1 = y0 + r->clip.h;
        if (x0 > *cx0) *cx0 = x0;
        if (y0 > *cy0) *cy0 = y0;
        if (x1 < *cx1) *cx1 = x1;
        if (y1 < *cy1) *cy1 = y1;
    }
    if (*cx0 < 0) *cx0 = 0;
    if (*cy0 < 0) *cy0 = 0;
    if (*cx1 > *tw) *cx1 = *tw;
    if (*cy1 > *th) *cy1 = *th;
}

static SDL_Rect gpu_clip(SDL_Renderer *r) {
    int w,h,x0,y0,x1,y1;
    get_target_bounds(r,&w,&h,&x0,&y0,&x1,&y1);
    return (SDL_Rect){x0,y0,x1-x0,y1-y0};
}

static void gpu_solid(SDL_Rect clip,SDL_FRect rect,uint8_t r,uint8_t g,uint8_t b,uint8_t a,uint32_t mode) {
    SDL_FColor color={r/255.f,g/255.f,b/255.f,a/255.f};
    gpu->rect(0,mode,clip,rect,(SDL_FRect){0,0,1,1},color);
}

static bool gpu_blit(SDL_Renderer *r,SDL_Texture *t,SDL_FRect src,SDL_FRect dst,
                     double angle,const SDL_FPoint *pivot,int flip) {
    if (!gpu_texture(t)) return false;
    SDL_FRect uv={src.x/t->w,src.y/t->h,src.w/t->w,src.h/t->h};
    if (flip&1) { uv.x+=uv.w; uv.w=-uv.w; }
    if (flip&2) { uv.y+=uv.h; uv.h=-uv.h; }
    if (t->access==SDL_TEXTUREACCESS_TARGET) { uv.y=1-uv.y; uv.h=-uv.h; }
    SDL_FColor color={t->cr/255.f,t->cg/255.f,t->cb/255.f,t->ca/255.f};
    SDL_Rect clip=gpu_clip(r);
    if (fabs(angle)<.01) gpu->rect(t->gpu.id,t->blend,clip,dst,uv,color);
    else {
        SDL_Vertex v[4]={
            {{dst.x,dst.y},color,{uv.x,uv.y}},
            {{dst.x+dst.w,dst.y},color,{uv.x+uv.w,uv.y}},
            {{dst.x,dst.y+dst.h},color,{uv.x,uv.y+uv.h}},
            {{dst.x+dst.w,dst.y+dst.h},color,{uv.x+uv.w,uv.y+uv.h}},
        };
        float cx=dst.x+(pivot?pivot->x*r->scale_x:dst.w*.5f);
        float cy=dst.y+(pivot?pivot->y*r->scale_y:dst.h*.5f);
        float radians=(float)(angle*0.0174532925199433), c=cosf(radians), s=sinf(radians);
        for (int i=0;i<4;i++) {
            float x=v[i].position.x-cx, y=v[i].position.y-cy;
            v[i].position=(SDL_FPoint){cx+x*c-y*s,cy+x*s+y*c};
        }
        gpu->quad(t->gpu.id,t->blend,clip,v);
    }
    return true;
}

static inline uint16_t *get_dst565(SDL_Renderer *r, int *stride_px) {
    if (!r->target) { *stride_px = LOG_W; return r->backbuf; }
    if (r->target->pix_fmt == PIX_RGB565 && r->target->data) {
        *stride_px = r->target->w;
        return (uint16_t *)r->target->data;
    }
    return NULL;
}

static inline void put_pixel_target(SDL_Renderer *r, int x, int y, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa, uint32_t mode) {
    int stride = 0;
    uint16_t *dbuf = get_dst565(r, &stride);
    if (dbuf) {
        uint16_t *p = &dbuf[y * stride + x];
        *p = blend565_rgba(*p, sr, sg, sb, sa, mode);
    } else if (r->target->pix_fmt == PIX_RGBA8888 && r->target->data) {
        uint8_t *p = (uint8_t *)r->target->data + y * r->target->pitch + x * 4;
        if (mode == SDL_BLENDMODE_NONE) {
            p[0] = sr; p[1] = sg; p[2] = sb; p[3] = sa;
        } else if (mode == SDL_BLENDMODE_BLEND) {
            if (sa == 0) return;
            if (sa == 255) { p[0] = sr; p[1] = sg; p[2] = sb; p[3] = 255; return; }
            uint32_t ia = 255u - sa;
            p[0] = (uint8_t)((sr * sa + p[0] * ia + 127u) / 255u);
            p[1] = (uint8_t)((sg * sa + p[1] * ia + 127u) / 255u);
            p[2] = (uint8_t)((sb * sa + p[2] * ia + 127u) / 255u);
            uint32_t na = sa + ((p[3] * ia) >> 8);
            p[3] = (uint8_t)(na > 255 ? 255 : na);
        } else if (mode == OG_BLEND_ADD_ONE) {
            uint32_t nr = p[0] + sr, ng = p[1] + sg, nb = p[2] + sb;
            p[0] = (uint8_t)(nr > 255 ? 255 : nr);
            p[1] = (uint8_t)(ng > 255 ? 255 : ng);
            p[2] = (uint8_t)(nb > 255 ? 255 : nb);
        } else if (mode == SDL_BLENDMODE_ADD) {
            uint32_t nr = p[0] + ((sr * sa) >> 8);
            uint32_t ng = p[1] + ((sg * sa) >> 8);
            uint32_t nb = p[2] + ((sb * sa) >> 8);
            p[0] = (uint8_t)(nr > 255 ? 255 : nr);
            p[1] = (uint8_t)(ng > 255 ? 255 : ng);
            p[2] = (uint8_t)(nb > 255 ? 255 : nb);
        } else if (mode == OG_BLEND_DARKEN) {
            p[0] = (uint8_t)((p[0] * (255u - sr)) >> 8);
            p[1] = (uint8_t)((p[1] * (255u - sg)) >> 8);
            p[2] = (uint8_t)((p[2] * (255u - sb)) >> 8);
        } else if (mode == OG_BLEND_SCREEN) {
            p[0] = (uint8_t)(sr + ((p[0] * (255u - sr)) >> 8));
            p[1] = (uint8_t)(sg + ((p[1] * (255u - sg)) >> 8));
            p[2] = (uint8_t)(sb + ((p[2] * (255u - sb)) >> 8));
        } else if (mode == OG_BLEND_MULTIPLY) {
            p[0] = (uint8_t)((p[0] * sr) >> 8);
            p[1] = (uint8_t)((p[1] * sg) >> 8);
            p[2] = (uint8_t)((p[2] * sb) >> 8);
        } else {
            p[0] = sr; p[1] = sg; p[2] = sb; p[3] = sa;
        }
    }
}

bool og_sdl_RenderClear(SDL_Renderer *r) {
    if (gpu && r) {
        gpu->clear(r->dr/255.f,r->dg/255.f,r->db/255.f,r->da/255.f);
        return true;
    }
    if (!r) return false;
    int stride = 0;
    uint16_t *dbuf = get_dst565(r, &stride);
    if (dbuf) {
        int tw = r->target ? r->target->w : LOG_W;
        int th = r->target ? r->target->h : LOG_H;
        uint16_t c = pack565(r->dr, r->dg, r->db);
        int n = tw * th;
        if (c == 0) memset(dbuf, 0, (size_t)n * 2);
        else {
            uint32_t c2 = ((uint32_t)c << 16) | c;
            uint32_t *p = (uint32_t *)dbuf;
            for (int i = 0; i < n / 2; i++) p[i] = c2;
            if (n & 1) dbuf[n - 1] = c;
        }
    } else if (r->target->pix_fmt == PIX_RGBA8888 && r->target->data) {
        uint32_t c = ((uint32_t)r->da << 24) | ((uint32_t)r->db << 16) | ((uint32_t)r->dg << 8) | r->dr;
        uint32_t *p = (uint32_t *)r->target->data;
        int n = r->target->w * r->target->h;
        if (c == 0) memset(p, 0, (size_t)n * 4);
        else for (int i = 0; i < n; i++) p[i] = c;
    }
    return true;
}

/* Fast NEON/SWAR constant-color RGB565 span blend (battle tiles, UI rects, geometry spans) */
static inline void blend_span565_const(uint16_t *dst, int count, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa, uint32_t mode) {
    if (count <= 0) return;
    if (mode == SDL_BLENDMODE_NONE || (mode == SDL_BLENDMODE_BLEND && sa == 255)) {
        uint16_t c = pack565(sr, sg, sb);
        uint32_t c2 = ((uint32_t)c << 16) | c;
        int x = 0;
        if (((uintptr_t)dst & 2u) && x < count) dst[x++] = c;
        uint32_t *d32 = (uint32_t *)(dst + x);
        int pairs = (count - x) >> 1;
        for (int i = 0; i < pairs; i++) d32[i] = c2;
        x += pairs << 1;
        if (x < count) dst[x] = c;
        return;
    }
    if (mode == SDL_BLENDMODE_BLEND) {
        if (sa == 0) return;
        uint16_t a5 = (uint16_t)((sa + 4u) >> 3);
        if (a5 == 0) return;
        if (a5 >= 32) {
            uint16_t c = pack565(sr, sg, sb);
            for (int x = 0; x < count; x++) dst[x] = c;
            return;
        }
        uint16_t inv = (uint16_t)(32u - a5);
        uint16_t sr5 = (uint16_t)(sr >> 3);
        uint16_t sg6 = (uint16_t)(sg >> 2);
        uint16_t sb5 = (uint16_t)(sb >> 3);
        int x = 0;
#if defined(__ARM_NEON)
        uint16x8_t v_sr = vdupq_n_u16((uint16_t)(sr5 * a5));
        uint16x8_t v_sg = vdupq_n_u16((uint16_t)(sg6 * a5));
        uint16x8_t v_sb = vdupq_n_u16((uint16_t)(sb5 * a5));
        uint16x8_t m63  = vdupq_n_u16(63u);
        uint16x8_t m31  = vdupq_n_u16(31u);
        for (; x + 8 <= count; x += 8) {
            uint16x8_t d  = vld1q_u16(dst + x);
            uint16x8_t dr = vshrq_n_u16(d, 11);
            uint16x8_t dg = vandq_u16(vshrq_n_u16(d, 5), m63);
            uint16x8_t db = vandq_u16(d, m31);
            uint16x8_t or_ = vshrq_n_u16(vmlaq_n_u16(v_sr, dr, inv), 5);
            uint16x8_t og_ = vshrq_n_u16(vmlaq_n_u16(v_sg, dg, inv), 5);
            uint16x8_t ob_ = vshrq_n_u16(vmlaq_n_u16(v_sb, db, inv), 5);
            vst1q_u16(dst + x, vorrq_u16(vorrq_u16(vshlq_n_u16(or_, 11), vshlq_n_u16(og_, 5)), ob_));
        }
#endif
        for (; x < count; x++) dst[x] = blend565_rgba(dst[x], sr, sg, sb, sa, SDL_BLENDMODE_BLEND);
        return;
    }
    if (mode == OG_BLEND_ADD_ONE || mode == SDL_BLENDMODE_ADD) {
        uint8_t ar = (mode == SDL_BLENDMODE_ADD) ? (uint8_t)((sr * sa + 127u) >> 8) : sr;
        uint8_t ag = (mode == SDL_BLENDMODE_ADD) ? (uint8_t)((sg * sa + 127u) >> 8) : sg;
        uint8_t ab = (mode == SDL_BLENDMODE_ADD) ? (uint8_t)((sb * sa + 127u) >> 8) : sb;
        if ((ar | ag | ab) == 0) return;
        uint16_t sr5 = (uint16_t)(ar >> 3), sg6 = (uint16_t)(ag >> 2), sb5 = (uint16_t)(ab >> 3);
        int x = 0;
#if defined(__ARM_NEON)
        uint16x8_t v_sr = vdupq_n_u16(sr5), v_sg = vdupq_n_u16(sg6), v_sb = vdupq_n_u16(sb5);
        uint16x8_t m63 = vdupq_n_u16(63u), m31 = vdupq_n_u16(31u);
        for (; x + 8 <= count; x += 8) {
            uint16x8_t d  = vld1q_u16(dst + x);
            uint16x8_t or_ = vminq_u16(vaddq_u16(vshrq_n_u16(d, 11), v_sr), m31);
            uint16x8_t og_ = vminq_u16(vaddq_u16(vandq_u16(vshrq_n_u16(d, 5), m63), v_sg), m63);
            uint16x8_t ob_ = vminq_u16(vaddq_u16(vandq_u16(d, m31), v_sb), m31);
            vst1q_u16(dst + x, vorrq_u16(vorrq_u16(vshlq_n_u16(or_, 11), vshlq_n_u16(og_, 5)), ob_));
        }
#endif
        for (; x < count; x++) dst[x] = blend565_rgba(dst[x], sr, sg, sb, sa, mode);
        return;
    }
    for (int x = 0; x < count; x++) dst[x] = blend565_rgba(dst[x], sr, sg, sb, sa, mode);
}

bool og_sdl_RenderFillRect(SDL_Renderer *r, const SDL_FRect *rect) {
    if (!r) return false;
    int tw, th, cx0, cy0, cx1, cy1;
    get_target_bounds(r, &tw, &th, &cx0, &cy0, &cx1, &cy1);
    int vx = r->has_vp ? r->vp.x : 0;
    int vy = r->has_vp ? r->vp.y : 0;
    int x0 = cx0, y0 = cy0, x1 = cx1, y1 = cy1;
    if (rect) {
        x0 = vx + (int)floorf(rect->x * r->scale_x);
        y0 = vy + (int)floorf(rect->y * r->scale_y);
        x1 = vx + (int)ceilf((rect->x + rect->w) * r->scale_x);
        y1 = vy + (int)ceilf((rect->y + rect->h) * r->scale_y);
        if (x0 < cx0) x0 = cx0; if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1; if (y1 > cy1) y1 = cy1;
    }
    if (x0 >= x1 || y0 >= y1) return true;
    if (gpu) {
        gpu_solid((SDL_Rect){cx0,cy0,cx1-cx0,cy1-cy0},(SDL_FRect){x0,y0,x1-x0,y1-y0},
                  r->dr,r->dg,r->db,r->da,r->draw_blend);
        return true;
    }
    uint32_t mode = r->draw_blend;
    int dstride = 0;
    uint16_t *dbuf = get_dst565(r, &dstride);
    if (dbuf) {
        int w = x1 - x0;
        for (int y = y0; y < y1; y++)
            blend_span565_const(dbuf + y * dstride + x0, w, r->dr, r->dg, r->db, r->da, mode);
        return true;
    }
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            put_pixel_target(r, x, y, r->dr, r->dg, r->db, r->da, mode);
    return true;
}

bool og_sdl_RenderFillRects(SDL_Renderer *r, const SDL_FRect *rects, int count) {
    if (!r || !rects) return false;
    for (int i = 0; i < count; i++) og_sdl_RenderFillRect(r, &rects[i]);
    return true;
}

bool og_sdl_RenderLines(SDL_Renderer *r, const SDL_FPoint *pts, int count) {
    if (!r || !pts || count < 2) return true;
    if (gpu) {
        SDL_FColor color={r->dr/255.f,r->dg/255.f,r->db/255.f,r->da/255.f};
        int vx=r->has_vp?r->vp.x:0, vy=r->has_vp?r->vp.y:0;
        SDL_Vertex run[128];
        for (int offset=0;offset<count-1;) {
            int n=count-offset<128?count-offset:128;
            for (int i=0;i<n;i++) run[i]=(SDL_Vertex){{vx+pts[offset+i].x*r->scale_x,vy+pts[offset+i].y*r->scale_y},color,{0,0}};
            gpu->geometry(0,r->draw_blend,gpu_clip(r),run,n,NULL,0,true,(SDL_FRect){0,0,1,1},false);
            offset+=n-1;
        }
        return true;
    }
    int tw, th, cx0, cy0, cx1, cy1;
    get_target_bounds(r, &tw, &th, &cx0, &cy0, &cx1, &cy1);
    int vx = r->has_vp ? r->vp.x : 0;
    int vy = r->has_vp ? r->vp.y : 0;
    for (int i = 0; i < count - 1; i++) {
        int x0 = vx + (int)lroundf(pts[i].x * r->scale_x);
        int y0 = vy + (int)lroundf(pts[i].y * r->scale_y);
        int x1 = vx + (int)lroundf(pts[i + 1].x * r->scale_x);
        int y1 = vy + (int)lroundf(pts[i + 1].y * r->scale_y);
        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            if (x0 >= cx0 && x0 < cx1 && y0 >= cy0 && y0 < cy1)
                put_pixel_target(r, x0, y0, r->dr, r->dg, r->db, r->da, r->draw_blend);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    return true;
}

/* --- Texture Creation & 16-bit Compression --- */
SDL_Texture *og_sdl_CreateTexture(SDL_Renderer *r, SDL_PixelFormat fmt, int access, int w, int h) {
    (void)r; (void)fmt;
    if (w <= 0 || h <= 0) return NULL;
    SDL_Texture *t = (SDL_Texture *)calloc(1, sizeof(SDL_Texture));
    if (!t) return NULL;
    t->w = w; t->h = h; t->access = access;
    /* SDL3 defaults: alpha textures default to BLEND, scale mode defaults to LINEAR */
    t->blend = SDL_BLENDMODE_BLEND;
    t->scale_mode = 1; /* SDL_SCALEMODE_LINEAR */
    t->cr = t->cg = t->cb = t->ca = 255;
    if (access == SDL_TEXTUREACCESS_TARGET) {
        /* RGB565 render targets (e.g. frame_tex): 50% RAM + unlocks 100% of fast RGB565 blitters! */
        t->pix_fmt = PIX_RGB565;
        t->pitch = w * 2;
        t->data = calloc((size_t)w * (size_t)h, 2);
        if (!t->data) { free(t); return NULL; }
    } else if (access != SDL_TEXTUREACCESS_STATIC) {
        t->pix_fmt = PIX_RGBA8888;
        t->pitch = w * 4;
        t->data = calloc((size_t)w * (size_t)h, 4);
        if (!t->data) { free(t); return NULL; }
    }
    return t;
}

SDL_Texture *og_sdl_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s) {
    if (!s || !s->pixels) return NULL;
    SDL_Texture *t = og_sdl_CreateTexture(r, s->format, SDL_TEXTUREACCESS_STATIC, s->w, s->h);
    if (!t) return NULL;
    og_sdl_UpdateTexture(t, NULL, s->pixels, s->pitch);
    return t;
}

static void (*sgx_forget)(unsigned);
static unsigned sgx_generation = 1;
static void gpu_changed(SDL_Texture *t,SDL_Rect r) {
    SDL_Rect old=t->gpu.update;
    if (t->gpu.dirty && old.w>0 && old.h>0) {
        int x=old.x<r.x?old.x:r.x, y=old.y<r.y?old.y:r.y;
        int right=old.x+old.w>r.x+r.w?old.x+old.w:r.x+r.w;
        int bottom=old.y+old.h>r.y+r.h?old.y+old.h:r.y+r.h;
        r=(SDL_Rect){x,y,right-x,bottom-y};
    }
    t->gpu.dirty=true;
    t->gpu.update=r;
}

static void invalidate_sgx_texture(SDL_Texture *t) {
    if (t && t->sgx_id) {
        if (sgx_forget && t->sgx_generation == sgx_generation) sgx_forget(t->sgx_id);
        t->sgx_id = 0;
    }
}

void og_sdl_DestroyTexture(SDL_Texture *t) {
    if (!t) return;
    if (gpu) gpu->destroy(&t->gpu);
    invalidate_sgx_texture(t);
    g_linear565_pixels -= t->linear565_capacity;
    free(t->linear565);
    free(t->data);
    free(t);
}

bool og_sdl_GetTextureSize(SDL_Texture *t, float *w, float *h) {
    if (!t) return false;
    if (w) *w = (float)t->w;
    if (h) *h = (float)t->h;
    return true;
}

bool og_sdl_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch) {
    if (!t || !pixels) return false;
    if (t->gpu_only && t->gpu.id) return false;
    int rx = rect ? rect->x : 0;
    int ry = rect ? rect->y : 0;
    int rw = rect ? rect->w : t->w;
    int rh = rect ? rect->h : t->h;
    if (rx < 0 || ry < 0 || rw <= 0 || rh <= 0 || rx + rw > t->w || ry + rh > t->h) return false;
    if (gpu && t->gpu.image && gpu->map(&t->gpu,t->w,t->h)) {
        uint8_t *dst=(uint8_t *)t->gpu.shared.pixels+(size_t)ry*t->gpu.shared.pitch+rx*4;
        for (int y=0;y<rh;y++)
            memcpy(dst+(size_t)y*t->gpu.shared.pitch,(const uint8_t *)pixels+(size_t)y*pitch,(size_t)rw*4);
        return true;
    }
    if (gpu) gpu_changed(t,(SDL_Rect){rx,ry,rw,rh});
    invalidate_sgx_texture(t);
    t->linear565_key.w = 0;

    /* Compress full-size STATIC textures to 16-bit RGB565 or ARGB1555 (50% RAM savings!) */
    if (!t->data && t->access == SDL_TEXTUREACCESS_STATIC && rx == 0 && ry == 0 && rw == t->w && rh == t->h) {
        bool all_opaque = true, binary_alpha = true;
        for (int y = 0; y < rh && (all_opaque || binary_alpha); y++) {
            const uint8_t *row = (const uint8_t *)pixels + (size_t)y * (size_t)pitch;
            for (int x = 0; x < rw; x++) {
                uint8_t a = row[x * 4 + 3];
                if (a != 255) {
                    all_opaque = false;
                    if (a != 0) { binary_alpha = false; break; }
                }
            }
        }
        if (all_opaque) {
            t->pix_fmt = PIX_RGB565;
            t->pitch = t->w * 2;
            t->data = malloc((size_t)t->w * (size_t)t->h * 2);
            if (!t->data) return false;
            for (int y = 0; y < rh; y++) {
                const uint8_t *src = (const uint8_t *)pixels + (size_t)y * (size_t)pitch;
                uint16_t *dst = (uint16_t *)((uint8_t *)t->data + (size_t)y * (size_t)t->pitch);
                int x = 0;
#if defined(__ARM_NEON)
                for (; x + 8 <= rw; x += 8) {
                    uint8x8x4_t c = vld4_u8(src + x * 4);
                    uint16x8_t r5 = vshlq_n_u16(vmovl_u8(vshr_n_u8(c.val[0], 3)), 11);
                    uint16x8_t g6 = vshlq_n_u16(vmovl_u8(vshr_n_u8(c.val[1], 2)), 5);
                    uint16x8_t b5 = vmovl_u8(vshr_n_u8(c.val[2], 3));
                    vst1q_u16(dst + x, vorrq_u16(vorrq_u16(r5, g6), b5));
                }
#endif
                for (; x < rw; x++) dst[x] = pack565(src[x * 4 + 0], src[x * 4 + 1], src[x * 4 + 2]);
            }
            return true;
        }
        if (binary_alpha) {
            t->pix_fmt = PIX_ARGB1555;
            t->pitch = t->w * 2;
            t->data = malloc((size_t)t->w * (size_t)t->h * 2);
            if (!t->data) return false;
            for (int y = 0; y < rh; y++) {
                const uint8_t *src = (const uint8_t *)pixels + (size_t)y * (size_t)pitch;
                uint16_t *dst = (uint16_t *)((uint8_t *)t->data + (size_t)y * (size_t)t->pitch);
                for (int x = 0; x < rw; x++)
                    dst[x] = pack1555(src[x * 4 + 0], src[x * 4 + 1], src[x * 4 + 2], src[x * 4 + 3]);
            }
            return true;
        }
        t->pix_fmt = PIX_RGBA8888;
        t->pitch = t->w * 4;
        t->data = malloc((size_t)t->w * (size_t)t->h * 4);
        if (!t->data) return false;
    } else if (!t->data) {
        t->pix_fmt = PIX_RGBA8888;
        t->pitch = t->w * 4;
        t->data = calloc((size_t)t->w * (size_t)t->h, 4);
        if (!t->data) return false;
    }

    if (t->pix_fmt == PIX_RGBA8888) {
        for (int y = 0; y < rh; y++) {
            const uint8_t *src = (const uint8_t *)pixels + (size_t)y * (size_t)pitch;
            uint8_t *dst = (uint8_t *)t->data + (size_t)(ry + y) * (size_t)t->pitch + (size_t)rx * 4;
            memcpy(dst, src, (size_t)rw * 4);
        }
    } else if (t->pix_fmt == PIX_RGB565) {
        for (int y = 0; y < rh; y++) {
            const uint8_t *src = (const uint8_t *)pixels + (size_t)y * (size_t)pitch;
            uint16_t *dst = (uint16_t *)((uint8_t *)t->data + (size_t)(ry + y) * (size_t)t->pitch) + rx;
            for (int x = 0; x < rw; x++) dst[x] = pack565(src[x * 4 + 0], src[x * 4 + 1], src[x * 4 + 2]);
        }
    } else if (t->pix_fmt == PIX_ARGB1555) {
        for (int y = 0; y < rh; y++) {
            const uint8_t *src = (const uint8_t *)pixels + (size_t)y * (size_t)pitch;
            uint16_t *dst = (uint16_t *)((uint8_t *)t->data + (size_t)(ry + y) * (size_t)t->pitch) + rx;
            for (int x = 0; x < rw; x++) dst[x] = pack1555(src[x * 4 + 0], src[x * 4 + 1], src[x * 4 + 2], src[x * 4 + 3]);
        }
    }
    return true;
}

bool og_sdl_LockTexture(SDL_Texture *t, const SDL_Rect *rect, void **pixels, int *pitch) {
    if (!t || !t->data || t->pix_fmt != PIX_RGBA8888) return false;
    SDL_Rect area=rect?*rect:(SDL_Rect){0,0,t->w,t->h};
    if (area.x<0 || area.y<0 || area.w<=0 || area.h<=0 ||
        area.x>t->w-area.w || area.y>t->h-area.h) return false;
    bool shared=gpu && !gpu_async_requested && t->access==SDL_TEXTUREACCESS_STREAMING &&
        gpu->map(&t->gpu,t->w,t->h);
    int stride=shared?t->gpu.shared.pitch:t->pitch;
    uint8_t *data=shared?t->gpu.shared.pixels:t->data;
    if (gpu) t->gpu_locked=area;
    if (pixels) *pixels = data+(size_t)area.y*stride+(size_t)area.x*4;
    if (pitch) *pitch = stride;
    return true;
}
void og_sdl_UnlockTexture(SDL_Texture *t) {
    if (gpu && t && !t->gpu.image) gpu_changed(t,t->gpu_locked);
    invalidate_sgx_texture(t);
}

bool og_sdl_SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode mode) { if (t) t->blend = mode; return t != NULL; }
bool og_sdl_GetTextureBlendMode(SDL_Texture *t, SDL_BlendMode *mode) { if (t && mode) *mode = t->blend; return t != NULL; }
bool og_sdl_SetTextureScaleMode(SDL_Texture *t, int mode) { if (t && t->scale_mode != mode) { invalidate_sgx_texture(t); t->scale_mode = mode; } return t != NULL; }
bool og_sdl_SetTextureColorMod(SDL_Texture *t, uint8_t cr, uint8_t cg, uint8_t cb) {
    if (!t) return false;
    t->cr = cr; t->cg = cg; t->cb = cb;
    return true;
}
bool og_sdl_GetTextureColorMod(SDL_Texture *t, uint8_t *cr, uint8_t *cg, uint8_t *cb) {
    if (!t) return false;
    if (cr) *cr = t->cr; if (cg) *cg = t->cg; if (cb) *cb = t->cb;
    return true;
}
bool og_sdl_SetTextureColorModFloat(SDL_Texture *t, float cr, float cg, float cb) {
    return og_sdl_SetTextureColorMod(t, clamp_u8f(cr), clamp_u8f(cg), clamp_u8f(cb));
}
bool og_sdl_SetTextureAlphaMod(SDL_Texture *t, uint8_t alpha) { if (t) t->ca = alpha; return t != NULL; }
bool og_sdl_SetTextureAlphaModFloat(SDL_Texture *t, float alpha) { return og_sdl_SetTextureAlphaMod(t, clamp_u8f(alpha)); }
bool og_sdl_GetTextureAlphaModFloat(SDL_Texture *t, float *alpha) {
    if (!t) return false;
    if (alpha) *alpha = (float)t->ca / 255.0f;
    return true;
}
SDL_BlendMode og_sdl_ComposeCustomBlendMode(int sc, int dc, int cop, int sa, int da, int aop) {
    (void)cop; (void)sa; (void)da; (void)aop;
    if (sc == SDL_BLENDFACTOR_ONE && dc == SDL_BLENDFACTOR_ONE) return OG_BLEND_ADD_ONE;
    if (sc == SDL_BLENDFACTOR_ZERO && dc == SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR) return OG_BLEND_DARKEN;
    if (sc == SDL_BLENDFACTOR_ONE && dc == SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR) return OG_BLEND_SCREEN;
    if (sc == SDL_BLENDFACTOR_DST_COLOR && dc == SDL_BLENDFACTOR_ZERO) return OG_BLEND_MULTIPLY;
    return SDL_BLENDMODE_BLEND;
}

/* --- Texture Sampling & Blitting --- */
static inline void sample_tex(const SDL_Texture *t, int sx, int sy, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    if (sx < 0) sx = 0; else if (sx >= t->w) sx = t->w - 1;
    if (sy < 0) sy = 0; else if (sy >= t->h) sy = t->h - 1;
    if (t->pix_fmt == PIX_RGB565) {
        uint16_t p = ((const uint16_t *)((const uint8_t *)t->data + (size_t)sy * (size_t)t->pitch))[sx];
        unpack565(p, r, g, b);
        *a = 255;
    } else if (t->pix_fmt == PIX_ARGB1555) {
        uint16_t p = ((const uint16_t *)((const uint8_t *)t->data + (size_t)sy * (size_t)t->pitch))[sx];
        *a = (p & 0x8000u) ? 255 : 0;
        uint8_t r5 = (uint8_t)((p >> 10) & 31);
        uint8_t g5 = (uint8_t)((p >> 5) & 31);
        uint8_t b5 = (uint8_t)(p & 31);
        *r = (uint8_t)((r5 << 3) | (r5 >> 2));
        *g = (uint8_t)((g5 << 3) | (g5 >> 2));
        *b = (uint8_t)((b5 << 3) | (b5 >> 2));
    } else {
        const uint8_t *p = (const uint8_t *)t->data + (size_t)sy * (size_t)t->pitch + (size_t)sx * 4;
        *r = p[0]; *g = p[1]; *b = p[2]; *a = p[3];
    }
}

static inline uint16_t sample565_bilerp(const uint16_t *srow0, const uint16_t *srow1, int tw_s, int32_t u_lin, uint32_t fy) {
    int sx0 = u_lin >> 16;
    uint32_t fx = ((uint32_t)u_lin >> 11) & 31u;
    int sx1 = sx0 + 1;
    if (sx0 < 0) { sx0 = 0; sx1 = 0; }
    else if (sx1 >= tw_s) { sx0 = tw_s - 1; sx1 = tw_s - 1; }
    uint16_t p00 = srow0[sx0], p10 = srow0[sx1];
    uint16_t p01 = srow1[sx0], p11 = srow1[sx1];
    if ((p00 | p10 | p01 | p11) == 0) return 0;
    return bilerp565_swar(p00, p10, p01, p11, fx, fy);
}

#if defined(__ARM_NEON)
static const uint16_t *linear565_quad(SDL_Texture *t, Linear565Key key) {
    const Linear565Key old = t->linear565_key;
    if (old.w == key.w && old.h == key.h && old.u == key.u && old.v == key.v &&
        old.du == key.du && old.dv == key.dv) return t->linear565;

    size_t n = (size_t)key.w * key.h;
    if (n > t->linear565_capacity) {
        // shortcut: cap the sample cache at 2 MiB, raise it only with a measured RAM budget.
        if (n - t->linear565_capacity > (2u * 1024 * 1024 / sizeof(uint16_t)) - g_linear565_pixels)
            return NULL;
        uint16_t *pixels = realloc(t->linear565, n * sizeof(*pixels));
        if (!pixels) return NULL;
        g_linear565_pixels += n - t->linear565_capacity;
        t->linear565 = pixels;
        t->linear565_capacity = n;
    }

    // Filter each source row horizontally once, then reuse it for the vertical pass.
    uint16_t left[LOG_W], right[LOG_W], fx[LOG_W], rows[2][LOG_W];
    int32_t u = key.u;
    for (int x = 0; x < key.w; x++, u += key.du) {
        int ix = u >> 16;
        left[x] = ix < 0 ? 0 : (ix < t->w ? ix : t->w - 1);
        right[x] = ix < 0 ? 0 : (ix < t->w - 1 ? ix + 1 : t->w - 1);
        fx[x] = ((uint32_t)u >> 11) & 31u;
    }
    int cached_y[2] = {-1, -1};
    int32_t v = key.v;
    for (int y = 0; y < key.h; y++, v += key.dv) {
        int iy = v >> 16;
        int sy[2] = { iy < 0 ? 0 : (iy < t->h ? iy : t->h - 1),
                      iy < 0 ? 0 : (iy < t->h - 1 ? iy + 1 : t->h - 1) };
        for (int i = 0; i < 2; i++) {
            int slot = sy[i] & 1;
            if (cached_y[slot] == sy[i]) continue;
            cached_y[slot] = sy[i];
            const uint16_t *src = (const uint16_t *)((const uint8_t *)t->data + (size_t)sy[i] * t->pitch);
            for (int x = 0; x < key.w; x++)
                rows[slot][x] = lerp565_swar(src[left[x]], src[right[x]], fx[x]);
        }
        const uint16_t *a = rows[sy[0] & 1], *b = rows[sy[1] & 1];
        uint16_t *dst = t->linear565 + (size_t)y * key.w;
        uint32_t fy = ((uint32_t)v >> 11) & 31u;
        if (a == b || fy == 0) {
            memcpy(dst, a, (size_t)key.w * sizeof(*dst));
            continue;
        }
        int x = 0;
        for (; x + 8 <= key.w; x += 8)
            vst1q_u16(dst + x, lerp565x8(vld1q_u16(a + x), vld1q_u16(b + x), vdupq_n_u16(fy)));
        for (; x < key.w; x++) dst[x] = lerp565_swar(a[x], b[x], fy);
    }
    t->linear565_key = key;
    return t->linear565;
}

static inline uint16x8_t composite_channel_x8(uint16x8_t d, uint16x8_t s, uint32_t scale,
                                             uint32_t max, uint32_t mode) {
    if (scale != 256) s = vshrq_n_u16(vmulq_n_u16(s, scale), 8);
    const uint16x8_t limit = vdupq_n_u16(max);
    if (mode == OG_BLEND_ADD_ONE) return vminq_u16(vaddq_u16(d, s), limit);
    uint16x8_t p = vmlaq_u16(vdupq_n_u16(max / 2), d, vsubq_u16(limit, s));
    p = max == 63 ? vshrq_n_u16(p, 6) : vshrq_n_u16(p, 5);
    return mode == OG_BLEND_SCREEN ? vaddq_u16(s, p) : p;
}

static void composite565_row(uint16_t *dst, const uint16_t *src, int w, uint32_t mode,
                             uint32_t mr, uint32_t mg, uint32_t mb) {
    int x = 0;
    for (; x + 8 <= w; x += 8) {
        uint64_t a, b;
        memcpy(&a, src + x, sizeof(a));
        memcpy(&b, src + x + 4, sizeof(b));
        if (!(a | b)) continue;
        uint16x8_t s = vld1q_u16(src + x);
        uint16x8_t d = vld1q_u16(dst + x);
        uint16x8_t p = pack565x8(composite_channel_x8(red565x8(d), red565x8(s), mr, 31, mode),
                                 composite_channel_x8(green565x8(d), green565x8(s), mg, 63, mode),
                                 composite_channel_x8(blue565x8(d), blue565x8(s), mb, 31, mode));
        vst1q_u16(dst + x, vbslq_u16(vceqq_u16(s, vdupq_n_u16(0)), d, p));
    }
    for (; x < w; x++) {
        uint16_t s = src[x], d = dst[x];
        if (!s) continue;
        uint32_t r = (s >> 11) * mr >> 8;
        uint32_t g = ((s >> 5) & 63u) * mg >> 8;
        uint32_t b = (s & 31u) * mb >> 8;
        uint32_t dr = d >> 11, dg = (d >> 5) & 63u, db = d & 31u;
        if (mode == OG_BLEND_ADD_ONE) {
            r += dr; if (r > 31) r = 31;
            g += dg; if (g > 63) g = 63;
            b += db; if (b > 31) b = 31;
        } else {
            dr = (dr * (31 - r) + 15) >> 5;
            dg = (dg * (63 - g) + 31) >> 6;
            db = (db * (31 - b) + 15) >> 5;
            r = mode == OG_BLEND_SCREEN ? r + dr : dr;
            g = mode == OG_BLEND_SCREEN ? g + dg : dg;
            b = mode == OG_BLEND_SCREEN ? b + db : db;
        }
        dst[x] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}
#endif

static void blend_rgba565_row(uint16_t *dst, const uint8_t *src, int count) {
    int x = 0;
#if defined(__ARM_NEON)
    for (; x + 8 <= count; x += 8) {
        const uint8x8x4_t rgba = vld4_u8(src + x * 4);
        const uint64_t alpha = vget_lane_u64(vreinterpret_u64_u8(rgba.val[3]), 0);
        if (alpha == 0) continue;

        uint16x8_t red = vmovl_u8(vshr_n_u8(rgba.val[0], 3));
        uint16x8_t green = vmovl_u8(vshr_n_u8(rgba.val[1], 2));
        uint16x8_t blue = vmovl_u8(vshr_n_u8(rgba.val[2], 3));
        if (alpha != UINT64_MAX) {
            /* Same five-bit alpha and truncation as blend565_rgba. */
            const uint16x8_t a = vmovl_u8(vrshr_n_u8(rgba.val[3], 3));
            const uint16x8_t ia = vsubq_u16(vdupq_n_u16(32), a);
            const uint16x8_t d = vld1q_u16(dst + x);
            red = vshrq_n_u16(vmlaq_u16(vmulq_u16(red, a), red565x8(d), ia), 5);
            green = vshrq_n_u16(vmlaq_u16(vmulq_u16(green, a), green565x8(d), ia), 5);
            blue = vshrq_n_u16(vmlaq_u16(vmulq_u16(blue, a), blue565x8(d), ia), 5);
        }
        vst1q_u16(dst + x, pack565x8(red, green, blue));
    }
#endif
    for (; x < count; x++) {
        const uint8_t *p = src + x * 4;
        if (p[3] == 0) continue;
        dst[x] = p[3] == 255 ? pack565(p[0], p[1], p[2])
                             : blend565_rgba(dst[x], p[0], p[1], p[2], p[3], SDL_BLENDMODE_BLEND);
    }
}

bool og_sdl_RenderTexture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *srcrect, const SDL_FRect *dstrect) {
    if (!r || !t || (!t->data && !(gpu && t->gpu.id))) return false;
    int tw, th, cx0, cy0, cx1, cy1;
    get_target_bounds(r, &tw, &th, &cx0, &cy0, &cx1, &cy1);
    int vx = r->has_vp ? r->vp.x : 0;
    int vy = r->has_vp ? r->vp.y : 0;

    float sx0 = srcrect ? srcrect->x : 0.0f;
    float sy0 = srcrect ? srcrect->y : 0.0f;
    float sw  = srcrect ? srcrect->w : (float)t->w;
    float sh  = srcrect ? srcrect->h : (float)t->h;
    float dx0 = dstrect ? (float)vx + dstrect->x * r->scale_x : (float)cx0;
    float dy0 = dstrect ? (float)vy + dstrect->y * r->scale_y : (float)cy0;
    float dw  = dstrect ? dstrect->w * r->scale_x : (float)(cx1 - cx0);
    float dh  = dstrect ? dstrect->h * r->scale_y : (float)(cy1 - cy0);
    if (dw <= 0.0f || dh <= 0.0f || sw <= 0.0f || sh <= 0.0f) return true;
    if (gpu) return gpu_blit(r,t,(SDL_FRect){sx0,sy0,sw,sh},(SDL_FRect){dx0,dy0,dw,dh},0,NULL,0);


    int ix0 = (int)floorf(dx0), iy0 = (int)floorf(dy0);
    int ix1 = (int)ceilf(dx0 + dw), iy1 = (int)ceilf(dy0 + dh);
    if (ix0 < cx0) ix0 = cx0; if (iy0 < cy0) iy0 = cy0;
    if (ix1 > cx1) ix1 = cx1; if (iy1 > cy1) iy1 = cy1;
    if (ix0 >= ix1 || iy0 >= iy1) return true;

    bool mod_c = (t->cr != 255 || t->cg != 255 || t->cb != 255);
    bool mod_a = (t->ca != 255);
    bool is_1to1 = (fabsf(sw - dw) < 0.01f && fabsf(sh - dh) < 0.01f);
    int dstride = 0;
    uint16_t *dbuf = get_dst565(r, &dstride);

    /* Fast path 0: Zero-copy 640x480 frame_tex -> backbuf pointer swap (0.0 ms!) */
    if (!r->target && t->access == SDL_TEXTUREACCESS_TARGET && t->pix_fmt == PIX_RGB565 &&
        t->w == LOG_W && t->h == LOG_H && !mod_c && !mod_a &&
        ix0 == 0 && iy0 == 0 && ix1 == LOG_W && iy1 == LOG_H && is_1to1 &&
        (t->blend == SDL_BLENDMODE_NONE || t->blend == SDL_BLENDMODE_BLEND)) {
        uint16_t *tmp = r->backbuf;
        r->backbuf = (uint16_t *)t->data;
        g_fb_stage[g_fb_back] = r->backbuf;
        t->data = tmp;
        return true;
    }

    /* Fast path 1: 1:1 opaque RGB565 -> RGB565 target/backbuf memcpy (backgrounds!) */
    if (dbuf && is_1to1 && t->pix_fmt == PIX_RGB565 && !mod_c && !mod_a &&
        (t->blend == SDL_BLENDMODE_NONE || t->blend == SDL_BLENDMODE_BLEND)) {
        int srx0 = (int)lroundf(sx0) + (ix0 - (int)lroundf(dx0));
        int sry0 = (int)lroundf(sy0) + (iy0 - (int)lroundf(dy0));
        int copy_w = ix1 - ix0;
        if (srx0 >= 0 && srx0 + copy_w <= t->w) {
            for (int y = iy0; y < iy1; y++) {
                int sy = sry0 + (y - iy0);
                if (sy < 0 || sy >= t->h) continue;
                const uint16_t *src = (const uint16_t *)((const uint8_t *)t->data + (size_t)sy * (size_t)t->pitch) + srx0;
                memcpy(dbuf + y * dstride + ix0, src, (size_t)copy_w * 2);
            }
            return true;
        }
    }

    /* Fast path 2: 1:1 ARGB1555 -> RGB565 target/backbuf colorkey copy (UI & sprites!) */
    if (dbuf && is_1to1 && t->pix_fmt == PIX_ARGB1555 && !mod_c && !mod_a && t->blend == SDL_BLENDMODE_BLEND) {
        int srx0 = (int)lroundf(sx0) + (ix0 - (int)lroundf(dx0));
        int sry0 = (int)lroundf(sy0) + (iy0 - (int)lroundf(dy0));
        int copy_w = ix1 - ix0;
        if (srx0 >= 0 && srx0 + copy_w <= t->w) {
            for (int y = iy0; y < iy1; y++) {
                int sy = sry0 + (y - iy0);
                if (sy < 0 || sy >= t->h) continue;
                const uint16_t *src = (const uint16_t *)((const uint8_t *)t->data + (size_t)sy * (size_t)t->pitch) + srx0;
                uint16_t *dst = dbuf + y * dstride + ix0;
                int x = 0;
                for (; x + 4 <= copy_w; x += 4) {
                    if (((src[x] | src[x + 1] | src[x + 2] | src[x + 3]) & 0x8000u) == 0) continue;
                    for (int k = 0; k < 4; k++) {
                        uint16_t p = src[x + k];
                        if (p & 0x8000u) dst[x + k] = (uint16_t)(((p & 0x7FE0u) << 1) | (p & 0x001Fu));
                    }
                }
                for (; x < copy_w; x++) {
                    uint16_t p = src[x];
                    if (p & 0x8000u) dst[x] = (uint16_t)(((p & 0x7FE0u) << 1) | (p & 0x001Fu));
                }
            }
            return true;
        }
    }

    /* Unscaled RGBA8888, including the original movies' doubled scanlines. */
    bool double_rows = !srcrect && sw == dw && sh * 2 == dh &&
        dx0 == floorf(dx0) && dy0 == floorf(dy0) && t->blend == SDL_BLENDMODE_BLEND;
    if (dbuf && (is_1to1 || double_rows) && t->pix_fmt == PIX_RGBA8888 && !mod_c && !mod_a) {
        int srx0 = (int)lroundf(sx0) + (ix0 - (int)lroundf(dx0));
        int sry0 = (int)lroundf(sy0) + (iy0 - (int)lroundf(dy0));
        int copy_w = ix1 - ix0;
        if (srx0 >= 0 && srx0 + copy_w <= t->w) {
            uint32_t mode = t->blend;
            for (int y = iy0; y < iy1; y++) {
                int sy = double_rows ? (y - (int)dy0) / 2 : sry0 + (y - iy0);
                if (sy < 0 || sy >= t->h) continue;
                const uint8_t *src = (const uint8_t *)t->data + (size_t)sy * (size_t)t->pitch + (size_t)srx0 * 4;
                uint16_t *dst = dbuf + y * dstride + ix0;
                if (mode == SDL_BLENDMODE_NONE) {
                    int x = 0;
#if defined(__ARM_NEON)
                    for (; x + 8 <= copy_w; x += 8) {
                        uint8x8x4_t c = vld4_u8(src + x * 4);
                        uint16x8_t r5 = vshlq_n_u16(vmovl_u8(vshr_n_u8(c.val[0], 3)), 11);
                        uint16x8_t g6 = vshlq_n_u16(vmovl_u8(vshr_n_u8(c.val[1], 2)), 5);
                        uint16x8_t b5 = vmovl_u8(vshr_n_u8(c.val[2], 3));
                        vst1q_u16(dst + x, vorrq_u16(vorrq_u16(r5, g6), b5));
                    }
#endif
                    for (; x < copy_w; x++) dst[x] = pack565(src[x * 4 + 0], src[x * 4 + 1], src[x * 4 + 2]);
                } else if (mode == SDL_BLENDMODE_BLEND) {
                    blend_rgba565_row(dst, src, copy_w);
                } else {
                    const uint32_t *s32 = (const uint32_t *)src;
                    bool skip_zero = (mode == OG_BLEND_ADD_ONE || mode == SDL_BLENDMODE_ADD ||
                                      mode == OG_BLEND_SCREEN || mode == OG_BLEND_DARKEN);
                    int x = 0;
                    if (skip_zero) {
                        for (; x + 4 <= copy_w; x += 4) {
                            if (((s32[x] | s32[x + 1] | s32[x + 2] | s32[x + 3]) & 0x00FFFFFFu) == 0) continue;
                            for (int k = 0; k < 4; k++) {
                                uint32_t px = s32[x + k];
                                if ((px & 0x00FFFFFFu) == 0) continue;
                                dst[x + k] = blend565_rgba(dst[x + k], (uint8_t)(px & 0xFFu), (uint8_t)((px >> 8) & 0xFFu),
                                                           (uint8_t)((px >> 16) & 0xFFu), (uint8_t)(px >> 24), mode);
                            }
                        }
                    }
                    for (; x < copy_w; x++) {
                        uint32_t px = s32[x];
                        if (skip_zero && (px & 0x00FFFFFFu) == 0) continue;
                        dst[x] = blend565_rgba(dst[x], (uint8_t)(px & 0xFFu), (uint8_t)((px >> 8) & 0xFFu),
                                               (uint8_t)((px >> 16) & 0xFFu), (uint8_t)(px >> 24), mode);
                    }
                }
            }
            return true;
        }
    }

    /* Fast path 4: 2x upscale RGBA8888 (320x240 half-res 3D actor/glow/screen layer -> 640x480 RGB565!) */
    if (dbuf && t->pix_fmt == PIX_RGBA8888 && !mod_c && !mod_a &&
        fabsf(dw - sw * 2.0f) < 0.01f && fabsf(dh - sh * 2.0f) < 0.01f &&
        ix0 == 0 && iy0 == 0 && ix1 == t->w * 2 && iy1 == t->h * 2) {
        uint32_t mode = t->blend;
        int tw_s = t->w, th_s = t->h;
        bool is_blend = (mode == SDL_BLENDMODE_BLEND);
        bool skip_zero = (mode == OG_BLEND_ADD_ONE || mode == SDL_BLENDMODE_ADD ||
                          mode == OG_BLEND_SCREEN || mode == OG_BLEND_DARKEN);
        for (int sy = 0; sy < th_s; sy++) {
            const uint32_t *s32 = (const uint32_t *)((const uint8_t *)t->data + (size_t)sy * (size_t)t->pitch);
            uint16_t *d0 = dbuf + (sy * 2) * dstride;
            uint16_t *d1 = d0 + dstride;
            int sx = 0;
            if (is_blend) {
                for (; sx + 4 <= tw_s; sx += 4) {
                    if (((s32[sx] | s32[sx + 1] | s32[sx + 2] | s32[sx + 3]) & 0xFF000000u) == 0) continue;
                    for (int k = 0; k < 4; k++) {
                        uint32_t px = s32[sx + k];
                        uint8_t a = (uint8_t)(px >> 24);
                        if (a == 0) continue;
                        uint8_t sr = (uint8_t)(px & 0xFFu), sg = (uint8_t)((px >> 8) & 0xFFu), sb = (uint8_t)((px >> 16) & 0xFFu);
                        int dx = (sx + k) * 2;
                        if (a == 255) {
                            uint16_t c = pack565(sr, sg, sb);
                            d0[dx] = c; d0[dx + 1] = c; d1[dx] = c; d1[dx + 1] = c;
                        } else {
                            d0[dx]     = blend565_rgba(d0[dx],     sr, sg, sb, a, mode);
                            d0[dx + 1] = blend565_rgba(d0[dx + 1], sr, sg, sb, a, mode);
                            d1[dx]     = blend565_rgba(d1[dx],     sr, sg, sb, a, mode);
                            d1[dx + 1] = blend565_rgba(d1[dx + 1], sr, sg, sb, a, mode);
                        }
                    }
                }
            } else if (skip_zero) {
                for (; sx + 4 <= tw_s; sx += 4) {
                    if (((s32[sx] | s32[sx + 1] | s32[sx + 2] | s32[sx + 3]) & 0x00FFFFFFu) == 0) continue;
                    for (int k = 0; k < 4; k++) {
                        uint32_t px = s32[sx + k];
                        if ((px & 0x00FFFFFFu) == 0) continue;
                        uint8_t sr = (uint8_t)(px & 0xFFu), sg = (uint8_t)((px >> 8) & 0xFFu), sb = (uint8_t)((px >> 16) & 0xFFu), sa = (uint8_t)(px >> 24);
                        int dx = (sx + k) * 2;
                        d0[dx]     = blend565_rgba(d0[dx],     sr, sg, sb, sa, mode);
                        d0[dx + 1] = blend565_rgba(d0[dx + 1], sr, sg, sb, sa, mode);
                        d1[dx]     = blend565_rgba(d1[dx],     sr, sg, sb, sa, mode);
                        d1[dx + 1] = blend565_rgba(d1[dx + 1], sr, sg, sb, sa, mode);
                    }
                }
            }
            for (; sx < tw_s; sx++) {
                uint32_t px = s32[sx];
                uint8_t a = (uint8_t)(px >> 24);
                if (is_blend && a == 0) continue;
                if (skip_zero && (px & 0x00FFFFFFu) == 0) continue;
                uint8_t sr = (uint8_t)(px & 0xFFu), sg = (uint8_t)((px >> 8) & 0xFFu), sb = (uint8_t)((px >> 16) & 0xFFu);
                int dx = sx * 2;
                d0[dx]     = blend565_rgba(d0[dx],     sr, sg, sb, a, mode);
                d0[dx + 1] = blend565_rgba(d0[dx + 1], sr, sg, sb, a, mode);
                d1[dx]     = blend565_rgba(d1[dx],     sr, sg, sb, a, mode);
                d1[dx + 1] = blend565_rgba(d1[dx + 1], sr, sg, sb, a, mode);
            }
        }
        return true;
    }

    /* General scaled / modulated / offscreen-target blitter (16.16 fixed-point, direct RGB565 fast path) */
    int32_t du = (int32_t)((sw / dw) * 65536.0f);
    int32_t dv = (int32_t)((sh / dh) * 65536.0f);
    int32_t u0 = (int32_t)((sx0 + ((float)ix0 + 0.5f - dx0) * (sw / dw)) * 65536.0f);
    int32_t v  = (int32_t)((sy0 + ((float)iy0 + 0.5f - dy0) * (sh / dh)) * 65536.0f);

    /* Fast path 5: Scaled / color-modulated PIX_RGB565 -> RGB565 with ADD / DARKEN / SCREEN (main menu lights, logos, flares!) */
    if (dbuf && t->pix_fmt == PIX_RGB565 && !mod_a &&
        (t->blend == OG_BLEND_ADD_ONE || t->blend == OG_BLEND_DARKEN || t->blend == OG_BLEND_SCREEN)) {
        uint32_t mode = t->blend;
        int tw_s = t->w, th_s = t->h, tpitch = t->pitch;
        const uint8_t *tdata = (const uint8_t *)t->data;
        uint32_t mr = (uint32_t)t->cr + 1u, mg = (uint32_t)t->cg + 1u, mb = (uint32_t)t->cb + 1u;
        bool use_linear = (t->scale_mode == 1 && !is_1to1);
        int32_t u0_l = u0 - 32768;
        int32_t v_l  = v - 32768;
#if defined(__ARM_NEON)
        const int width = ix1 - ix0, height = iy1 - iy0;
        if (use_linear && t->access == SDL_TEXTUREACCESS_STATIC &&
            width <= LOG_W && height <= LOG_H && width * height >= 4096) {
            const uint16_t *src = linear565_quad(t, (Linear565Key){ width, height, u0_l, v_l, du, dv });
            if (src) {
                for (int y = iy0; y < iy1; y++, src += width)
                    composite565_row(dbuf + (size_t)y * dstride + ix0, src, width, mode, mr, mg, mb);
                return true;
            }
        }
#endif
        for (int y = iy0; y < iy1; y++, v += dv, v_l += dv) {
            const uint16_t *srow0, *srow1;
            uint32_t fy = 0;
            if (use_linear) {
                int sy0 = v_l >> 16;
                fy = ((uint32_t)v_l >> 11) & 31u;
                int sy1 = sy0 + 1;
                if (sy0 < 0) { sy0 = 0; sy1 = 0; }
                else if (sy1 >= th_s) { sy0 = th_s - 1; sy1 = th_s - 1; }
                srow0 = (const uint16_t *)(tdata + (size_t)sy0 * (size_t)tpitch);
                srow1 = (const uint16_t *)(tdata + (size_t)sy1 * (size_t)tpitch);
            } else {
                int sy = v >> 16;
                if ((unsigned)sy >= (unsigned)th_s) sy = (sy < 0) ? 0 : th_s - 1;
                srow0 = (const uint16_t *)(tdata + (size_t)sy * (size_t)tpitch);
                srow1 = srow0;
            }
            uint16_t *drow = dbuf + y * dstride;
            int32_t u = use_linear ? u0_l : u0;
            if (mode == OG_BLEND_ADD_ONE) {
                if (!mod_c) {
                    for (int x = ix0; x < ix1; x++, u += du) {
                        uint16_t sp;
                        if (use_linear) {
                            sp = sample565_bilerp(srow0, srow1, tw_s, u, fy);
                        } else {
                            int sx = u >> 16;
                            if ((unsigned)sx >= (unsigned)tw_s) sx = (sx < 0) ? 0 : tw_s - 1;
                            sp = srow0[sx];
                        }
                        if (sp == 0) continue;
                        uint16_t dp = drow[x];
                        uint32_t rr = (dp >> 11) + (sp >> 11); if (rr > 31u) rr = 31u;
                        uint32_t gg = ((dp >> 5) & 63u) + ((sp >> 5) & 63u); if (gg > 63u) gg = 63u;
                        uint32_t bb = (dp & 31u) + (sp & 31u); if (bb > 31u) bb = 31u;
                        drow[x] = (uint16_t)((rr << 11) | (gg << 5) | bb);
                    }
                } else {
                    for (int x = ix0; x < ix1; x++, u += du) {
                        uint16_t sp;
                        if (use_linear) {
                            sp = sample565_bilerp(srow0, srow1, tw_s, u, fy);
                        } else {
                            int sx = u >> 16;
                            if ((unsigned)sx >= (unsigned)tw_s) sx = (sx < 0) ? 0 : tw_s - 1;
                            sp = srow0[sx];
                        }
                        if (sp == 0) continue;
                        uint16_t dp = drow[x];
                        uint32_t rr = (dp >> 11) + (((sp >> 11) * mr) >> 8); if (rr > 31u) rr = 31u;
                        uint32_t gg = ((dp >> 5) & 63u) + ((((sp >> 5) & 63u) * mg) >> 8); if (gg > 63u) gg = 63u;
                        uint32_t bb = (dp & 31u) + (((sp & 31u) * mb) >> 8); if (bb > 31u) bb = 31u;
                        drow[x] = (uint16_t)((rr << 11) | (gg << 5) | bb);
                    }
                }
            } else if (mode == OG_BLEND_DARKEN) {
                for (int x = ix0; x < ix1; x++, u += du) {
                    uint16_t sp;
                    if (use_linear) {
                        sp = sample565_bilerp(srow0, srow1, tw_s, u, fy);
                    } else {
                        int sx = u >> 16;
                        if ((unsigned)sx >= (unsigned)tw_s) sx = (sx < 0) ? 0 : tw_s - 1;
                        sp = srow0[sx];
                    }
                    if (sp == 0) continue;
                    uint32_t sr = sp >> 11, sg = (sp >> 5) & 63u, sb = sp & 31u;
                    if (mod_c) { sr = (sr * mr) >> 8; sg = (sg * mg) >> 8; sb = (sb * mb) >> 8; }
                    uint16_t dp = drow[x];
                    uint32_t rr = ((dp >> 11) * (31u - sr) + 15u) >> 5;
                    uint32_t gg = (((dp >> 5) & 63u) * (63u - sg) + 31u) >> 6;
                    uint32_t bb = ((dp & 31u) * (31u - sb) + 15u) >> 5;
                    drow[x] = (uint16_t)((rr << 11) | (gg << 5) | bb);
                }
            } else { /* OG_BLEND_SCREEN */
                for (int x = ix0; x < ix1; x++, u += du) {
                    uint16_t sp;
                    if (use_linear) {
                        sp = sample565_bilerp(srow0, srow1, tw_s, u, fy);
                    } else {
                        int sx = u >> 16;
                        if ((unsigned)sx >= (unsigned)tw_s) sx = (sx < 0) ? 0 : tw_s - 1;
                        sp = srow0[sx];
                    }
                    if (sp == 0) continue;
                    uint32_t sr = sp >> 11, sg = (sp >> 5) & 63u, sb = sp & 31u;
                    if (mod_c) { sr = (sr * mr) >> 8; sg = (sg * mg) >> 8; sb = (sb * mb) >> 8; }
                    uint16_t dp = drow[x];
                    uint32_t rr = sr + (((dp >> 11) * (31u - sr) + 15u) >> 5); if (rr > 31u) rr = 31u;
                    uint32_t gg = sg + ((((dp >> 5) & 63u) * (63u - sg) + 31u) >> 6); if (gg > 63u) gg = 63u;
                    uint32_t bb = sb + (((dp & 31u) * (31u - sb) + 15u) >> 5); if (bb > 31u) bb = 31u;
                    drow[x] = (uint16_t)((rr << 11) | (gg << 5) | bb);
                }
            }
        }
        return true;
    }

    if (dbuf) {
        uint32_t mode = t->blend;
        uint8_t cr = t->cr, cg = t->cg, cb = t->cb, ca = t->ca;
        int tw_s = t->w, th_s = t->h, tpitch = t->pitch, pfmt = t->pix_fmt;
        const uint8_t *tdata = (const uint8_t *)t->data;
        bool skip_zero = (mode == OG_BLEND_ADD_ONE || mode == SDL_BLENDMODE_ADD ||
                          mode == OG_BLEND_SCREEN || mode == OG_BLEND_DARKEN);
        for (int y = iy0; y < iy1; y++, v += dv) {
            int sy = v >> 16;
            if (sy < 0) sy = 0; else if (sy >= th_s) sy = th_s - 1;
            const uint8_t *srow = tdata + (size_t)sy * (size_t)tpitch;
            uint16_t *drow = dbuf + y * dstride;
            int32_t u = u0;
            for (int x = ix0; x < ix1; x++, u += du) {
                int sx = u >> 16;
                if (sx < 0) sx = 0; else if (sx >= tw_s) sx = tw_s - 1;
                uint8_t sr, sg, sb, sa;
                if (pfmt == PIX_RGB565) {
                    uint16_t p = ((const uint16_t *)srow)[sx];
                    if (skip_zero && p == 0) continue;
                    unpack565(p, &sr, &sg, &sb);
                    sa = 255;
                } else if (pfmt == PIX_ARGB1555) {
                    uint16_t p = ((const uint16_t *)srow)[sx];
                    sa = (p & 0x8000u) ? 255 : 0;
                    if (sa == 0 && mode == SDL_BLENDMODE_BLEND) continue;
                    uint8_t r5 = (uint8_t)((p >> 10) & 31), g5 = (uint8_t)((p >> 5) & 31), b5 = (uint8_t)(p & 31);
                    sr = (uint8_t)((r5 << 3) | (r5 >> 2));
                    sg = (uint8_t)((g5 << 3) | (g5 >> 2));
                    sb = (uint8_t)((b5 << 3) | (b5 >> 2));
                } else {
                    const uint8_t *p = srow + (size_t)sx * 4;
                    sr = p[0]; sg = p[1]; sb = p[2]; sa = p[3];
                    if (sa == 0 && mode == SDL_BLENDMODE_BLEND) continue;
                }
                if (mod_a) {
                    sa = (uint8_t)((sa * ca + 127u) >> 8);
                    if (sa == 0 && mode == SDL_BLENDMODE_BLEND) continue;
                }
                if (mod_c) {
                    sr = (uint8_t)((sr * cr + 127u) >> 8);
                    sg = (uint8_t)((sg * cg + 127u) >> 8);
                    sb = (uint8_t)((sb * cb + 127u) >> 8);
                }
                if (skip_zero && (sr | sg | sb) == 0) continue;
                drow[x] = blend565_rgba(drow[x], sr, sg, sb, sa, mode);
            }
        }
        return true;
    }
    for (int y = iy0; y < iy1; y++, v += dv) {
        int sy = v >> 16;
        int32_t u = u0;
        for (int x = ix0; x < ix1; x++, u += du) {
            int sx = u >> 16;
            uint8_t sr, sg, sb, sa;
            sample_tex(t, sx, sy, &sr, &sg, &sb, &sa);
            if (mod_a) sa = (uint8_t)((sa * t->ca + 127u) >> 8);
            if (sa == 0 && t->blend == SDL_BLENDMODE_BLEND) continue;
            if (mod_c) {
                sr = (uint8_t)((sr * t->cr + 127u) >> 8);
                sg = (uint8_t)((sg * t->cg + 127u) >> 8);
                sb = (uint8_t)((sb * t->cb + 127u) >> 8);
            }
            put_pixel_target(r, x, y, sr, sg, sb, sa, t->blend);
        }
    }
    return true;
}

bool og_sdl_RenderTextureRotated(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *srcrect, const SDL_FRect *dstrect,
                                 double angle, const SDL_FPoint *center, int flip) {
    if (!r || !t || (!t->data && !(gpu && t->gpu.id))) return false;
    if (fabs(angle) < 0.01 && flip == 0) return og_sdl_RenderTexture(r, t, srcrect, dstrect);
    int tw, th, cx0, cy0, cx1, cy1;
    get_target_bounds(r, &tw, &th, &cx0, &cy0, &cx1, &cy1);
    int vx = r->has_vp ? r->vp.x : 0;
    int vy = r->has_vp ? r->vp.y : 0;

    float sx0 = srcrect ? srcrect->x : 0.0f;
    float sy0 = srcrect ? srcrect->y : 0.0f;
    float sw  = srcrect ? srcrect->w : (float)t->w;
    float sh  = srcrect ? srcrect->h : (float)t->h;
    float dx0 = dstrect ? (float)vx + dstrect->x * r->scale_x : (float)cx0;
    float dy0 = dstrect ? (float)vy + dstrect->y * r->scale_y : (float)cy0;
    float dw  = dstrect ? dstrect->w * r->scale_x : (float)(cx1 - cx0);
    float dh  = dstrect ? dstrect->h * r->scale_y : (float)(cy1 - cy0);
    if (dw <= 0.0f || dh <= 0.0f) return true;
    if (gpu) return gpu_blit(r,t,(SDL_FRect){sx0,sy0,sw,sh},(SDL_FRect){dx0,dy0,dw,dh},angle,center,flip);


    /* Fast path: angle == 0 with horizontal/vertical flip (main menu chrome, flipped sprites) */
    int dstride = 0;
    uint16_t *dbuf = get_dst565(r, &dstride);
    if (dbuf && fabs(angle) < 0.01) {
        int ix0 = (int)floorf(dx0), iy0 = (int)floorf(dy0);
        int ix1 = (int)ceilf(dx0 + dw), iy1 = (int)ceilf(dy0 + dh);
        if (ix0 < cx0) ix0 = cx0; if (iy0 < cy0) iy0 = cy0;
        if (ix1 > cx1) ix1 = cx1; if (iy1 > cy1) iy1 = cy1;
        if (ix0 >= ix1 || iy0 >= iy1) return true;
        bool mod_c = (t->cr != 255 || t->cg != 255 || t->cb != 255);
        bool mod_a = (t->ca != 255);
        uint32_t mode = t->blend;
        bool skip_zero = (mode == OG_BLEND_ADD_ONE || mode == SDL_BLENDMODE_ADD ||
                          mode == OG_BLEND_SCREEN || mode == OG_BLEND_DARKEN);
        int32_t du = (int32_t)((sw / dw) * 65536.0f);
        int32_t dv = (int32_t)((sh / dh) * 65536.0f);
        int32_t u0, v0;
        if (flip & SDL_FLIP_HORIZONTAL) {
            u0 = (int32_t)((sx0 + sw - ((float)ix0 + 0.5f - dx0) * (sw / dw)) * 65536.0f);
            du = -du;
        } else {
            u0 = (int32_t)((sx0 + ((float)ix0 + 0.5f - dx0) * (sw / dw)) * 65536.0f);
        }
        if (flip & 2) { /* SDL_FLIP_VERTICAL */
            v0 = (int32_t)((sy0 + sh - ((float)iy0 + 0.5f - dy0) * (sh / dh)) * 65536.0f);
            dv = -dv;
        } else {
            v0 = (int32_t)((sy0 + ((float)iy0 + 0.5f - dy0) * (sh / dh)) * 65536.0f);
        }
        int tw_s = t->w, th_s = t->h, tpitch = t->pitch, pfmt = t->pix_fmt;
        const uint8_t *tdata = (const uint8_t *)t->data;
        int32_t v = v0;
        for (int y = iy0; y < iy1; y++, v += dv) {
            int sy = v >> 16;
            if (sy < 0) sy = 0; else if (sy >= th_s) sy = th_s - 1;
            const uint8_t *srow = tdata + (size_t)sy * (size_t)tpitch;
            uint16_t *drow = dbuf + y * dstride;
            int32_t u = u0;
            for (int x = ix0; x < ix1; x++, u += du) {
                int sx = u >> 16;
                if (sx < 0) sx = 0; else if (sx >= tw_s) sx = tw_s - 1;
                uint8_t sr, sg, sb, sa;
                if (pfmt == PIX_RGB565) {
                    uint16_t p = ((const uint16_t *)srow)[sx];
                    if (skip_zero && p == 0) continue;
                    unpack565(p, &sr, &sg, &sb);
                    sa = 255;
                } else if (pfmt == PIX_ARGB1555) {
                    uint16_t p = ((const uint16_t *)srow)[sx];
                    sa = (p & 0x8000u) ? 255 : 0;
                    if (sa == 0 && mode == SDL_BLENDMODE_BLEND) continue;
                    uint8_t r5 = (uint8_t)((p >> 10) & 31), g5 = (uint8_t)((p >> 5) & 31), b5 = (uint8_t)(p & 31);
                    sr = (uint8_t)((r5 << 3) | (r5 >> 2));
                    sg = (uint8_t)((g5 << 3) | (g5 >> 2));
                    sb = (uint8_t)((b5 << 3) | (b5 >> 2));
                } else {
                    const uint8_t *p = srow + (size_t)sx * 4;
                    sr = p[0]; sg = p[1]; sb = p[2]; sa = p[3];
                    if (sa == 0 && mode == SDL_BLENDMODE_BLEND) continue;
                }
                if (mod_a) { sa = (uint8_t)((sa * t->ca + 127u) >> 8); if (sa == 0 && mode == SDL_BLENDMODE_BLEND) continue; }
                if (mod_c) { sr = (uint8_t)((sr * t->cr + 127u) >> 8); sg = (uint8_t)((sg * t->cg + 127u) >> 8); sb = (uint8_t)((sb * t->cb + 127u) >> 8); }
                if (skip_zero && (sr | sg | sb) == 0) continue;
                drow[x] = blend565_rgba(drow[x], sr, sg, sb, sa, mode);
            }
        }
        return true;
    }

    float pcx = dx0 + (center ? center->x * r->scale_x : dw * 0.5f);
    float pcy = dy0 + (center ? center->y * r->scale_y : dh * 0.5f);
    float rad = (float)(angle * 3.14159265358979323846 / 180.0);
    float ca = cosf(rad), sa_ang = sinf(rad);

    /* Bounding box of rotated quad */
    float xs[4] = { dx0 - pcx, dx0 + dw - pcx, dx0 + dw - pcx, dx0 - pcx };
    float ys[4] = { dy0 - pcy, dy0 - pcy, dy0 + dh - pcy, dy0 + dh - pcy };
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < 4; i++) {
        float rx = pcx + xs[i] * ca - ys[i] * sa_ang;
        float ry = pcy + xs[i] * sa_ang + ys[i] * ca;
        if (rx < minx) minx = rx; if (rx > maxx) maxx = rx;
        if (ry < miny) miny = ry; if (ry > maxy) maxy = ry;
    }
    int ix0 = (int)floorf(minx), iy0 = (int)floorf(miny);
    int ix1 = (int)ceilf(maxx),  iy1 = (int)ceilf(maxy);
    if (ix0 < cx0) ix0 = cx0; if (iy0 < cy0) iy0 = cy0;
    if (ix1 > cx1) ix1 = cx1; if (iy1 > cy1) iy1 = cy1;
    bool mod_c = (t->cr != 255 || t->cg != 255 || t->cb != 255);
    bool mod_a = (t->ca != 255);

    for (int y = iy0; y < iy1; y++) {
        float py = (float)y + 0.5f - pcy;
        for (int x = ix0; x < ix1; x++) {
            float px = (float)x + 0.5f - pcx;
            float ux = (px * ca + py * sa_ang) + (pcx - dx0);
            float uy = (-px * sa_ang + py * ca) + (pcy - dy0);
            if (ux < 0.0f || ux >= dw || uy < 0.0f || uy >= dh) continue;
            float fx = ux / dw;
            if (flip & SDL_FLIP_HORIZONTAL) fx = 1.0f - fx;
            int sx = (int)floorf(sx0 + fx * sw);
            int sy = (int)floorf(sy0 + (uy / dh) * sh);
            uint8_t sr, sg, sb, sa;
            sample_tex(t, sx, sy, &sr, &sg, &sb, &sa);
            if (mod_a) sa = (uint8_t)((sa * t->ca + 127u) / 255u);
            if (sa == 0 && t->blend == SDL_BLENDMODE_BLEND) continue;
            if (mod_c) {
                sr = (uint8_t)((sr * t->cr + 127u) / 255u);
                sg = (uint8_t)((sg * t->cg + 127u) / 255u);
                sb = (uint8_t)((sb * t->cb + 127u) / 255u);
            }
            put_pixel_target(r, x, y, sr, sg, sb, sa, t->blend);
        }
    }
    return true;
}

bool og_sdl_RenderTexture9Grid(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *srcrect,
                               float lw, float rw, float th, float bh, float scale, const SDL_FRect *dstrect) {
    (void)lw; (void)rw; (void)th; (void)bh; (void)scale;
    return og_sdl_RenderTexture(r, t, srcrect, dstrect);
}

/* Fast scanline span rasterizer for SDL_RenderGeometry:
 * - Untextured flat-color triangles (battle movement tiles/borders): NEON 8-pixel RGB565 blend (~50x faster!)
 * - Textured flat-color triangles (main menu wavy multiply backdrop, battle grid): 16.16 fixed-point scanlines (~20x faster!)
 * - Fallback for vertex-color gradients / RGBA8888 targets */
static void geom_span_textured_565(uint16_t *drow, int xl, int xr, int32_t u, int32_t v, int32_t du, int32_t dv,
                                   const SDL_Texture *t, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca, uint32_t mode) {
    int tw = t->w, th = t->h, tpitch = t->pitch, pfmt = t->pix_fmt;
    const uint8_t *tdata = (const uint8_t *)t->data;
    bool mod_c = (cr != 255 || cg != 255 || cb != 255);
    bool mod_a = (ca != 255);

    /* Ultra-fast path: Main menu monster multiply (PIX_RGB565 * PIX_RGB565, no color mod) */
    if (mode == OG_BLEND_MULTIPLY && pfmt == PIX_RGB565 && !mod_c) {
        if (t->scale_mode == 1) {
            int32_t ul = u - 32768, vl = v - 32768;
            int x = xl;
#if defined(__ARM_NEON)
            int64_t u_end = ul + (int64_t)(xr - xl - 1) * du;
            int64_t v_end = vl + (int64_t)(xr - xl - 1) * dv;
            if (x + 8 <= xr && (unsigned)(ul >> 16) < (unsigned)(tw - 1) &&
                (unsigned)(vl >> 16) < (unsigned)(th - 1) &&
                (uint64_t)(u_end >> 16) < (unsigned)(tw - 1) &&
                (uint64_t)(v_end >> 16) < (unsigned)(th - 1)) {
                const uint16_t *pixels = (const uint16_t *)tdata;
                const int stride = tpitch / 2;
                for (; x + 8 <= xr; x += 8) {
                    uint16_t top[16], bottom[16], fx[8], fy[8];
                    for (int i = 0; i < 8; i++, ul += du, vl += dv) {
                        const uint16_t *src = pixels + (vl >> 16) * stride + (ul >> 16);
                        memcpy(top + 2 * i, src, 4);
                        memcpy(bottom + 2 * i, src + stride, 4);
                        fx[i] = ((uint32_t)ul >> 11) & 31u;
                        fy[i] = ((uint32_t)vl >> 11) & 31u;
                    }
                    uint16x8x2_t a = vld2q_u16(top), b = vld2q_u16(bottom);
                    uint16x8_t wx = vld1q_u16(fx), wy = vld1q_u16(fy);
                    vst1q_u16(drow + x, bilerp_multiply565x8(vld1q_u16(drow + x),
                                a.val[0], a.val[1], b.val[0], b.val[1], wx, wy));
                }
            }
#endif
            for (; x < xr; x++, ul += du, vl += dv) {
                int sy0 = vl >> 16;
                uint32_t fy = ((uint32_t)vl >> 11) & 31u;
                int sy1 = sy0 + 1;
                if (sy0 < 0) { sy0 = 0; sy1 = 0; }
                else if (sy1 >= th) { sy0 = th - 1; sy1 = th - 1; }
                const uint16_t *srow0 = (const uint16_t *)(tdata + (size_t)sy0 * (size_t)tpitch);
                const uint16_t *srow1 = (const uint16_t *)(tdata + (size_t)sy1 * (size_t)tpitch);
                uint16_t sp = sample565_bilerp(srow0, srow1, tw, ul, fy);
                drow[x] = multiply565(drow[x], sp);
            }
            return;
        }
        int32_t u_end = u + (xr - xl - 1) * du;
        int32_t v_end = v + (xr - xl - 1) * dv;
        bool in_bounds = ((unsigned)(u >> 16) < (unsigned)tw) && ((unsigned)(u_end >> 16) < (unsigned)tw) &&
                         ((unsigned)(v >> 16) < (unsigned)th) && ((unsigned)(v_end >> 16) < (unsigned)th);
        if (in_bounds) {
            int tstride = tpitch >> 1;
            const uint16_t *t16 = (const uint16_t *)tdata;
            for (int x = xl; x < xr; x++, u += du, v += dv) {
                uint16_t sp = t16[(v >> 16) * tstride + (u >> 16)];
                uint16_t dp = drow[x];
                uint32_t rr = ((dp >> 11) & 31u) * ((sp >> 11) & 31u);
                uint32_t gg = ((dp >> 5) & 63u) * ((sp >> 5) & 63u);
                uint32_t bb = (dp & 31u) * (sp & 31u);
                rr = (rr + 1u + (rr >> 5)) >> 5;
                gg = (gg + 1u + (gg >> 6)) >> 6;
                bb = (bb + 1u + (bb >> 5)) >> 5;
                drow[x] = (uint16_t)((rr << 11) | (gg << 5) | bb);
            }
            return;
        }
        for (int x = xl; x < xr; x++, u += du, v += dv) {
            int sx = u >> 16, sy = v >> 16;
            if ((unsigned)sx >= (unsigned)tw) sx = (sx < 0) ? 0 : tw - 1;
            if ((unsigned)sy >= (unsigned)th) sy = (sy < 0) ? 0 : th - 1;
            uint16_t sp = ((const uint16_t *)(tdata + (size_t)sy * (size_t)tpitch))[sx];
            uint16_t dp = drow[x];
            uint32_t rr = ((dp >> 11) & 31u) * ((sp >> 11) & 31u);
            uint32_t gg = ((dp >> 5) & 63u) * ((sp >> 5) & 63u);
            uint32_t bb = (dp & 31u) * (sp & 31u);
            rr = (rr + 1u + (rr >> 5)) >> 5;
            gg = (gg + 1u + (gg >> 6)) >> 6;
            bb = (bb + 1u + (bb >> 5)) >> 5;
            drow[x] = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
        return;
    }

    bool skip_zero = (mode == OG_BLEND_ADD_ONE || mode == SDL_BLENDMODE_ADD ||
                      mode == OG_BLEND_SCREEN || mode == OG_BLEND_DARKEN);
    for (int x = xl; x < xr; x++, u += du, v += dv) {
        int sx = u >> 16, sy = v >> 16;
        if ((unsigned)sx >= (unsigned)tw) sx = (sx < 0) ? 0 : tw - 1;
        if ((unsigned)sy >= (unsigned)th) sy = (sy < 0) ? 0 : th - 1;
        uint8_t sr, sg, sb, sa;
        if (pfmt == PIX_RGB565) {
            uint16_t p = ((const uint16_t *)(tdata + (size_t)sy * (size_t)tpitch))[sx];
            if (skip_zero && p == 0) continue;
            unpack565(p, &sr, &sg, &sb);
            sa = 255;
        } else if (pfmt == PIX_ARGB1555) {
            uint16_t p = ((const uint16_t *)(tdata + (size_t)sy * (size_t)tpitch))[sx];
            sa = (p & 0x8000u) ? 255 : 0;
            if (sa == 0 && mode == SDL_BLENDMODE_BLEND) continue;
            uint8_t r5 = (uint8_t)((p >> 10) & 31), g5 = (uint8_t)((p >> 5) & 31), b5 = (uint8_t)(p & 31);
            sr = (uint8_t)((r5 << 3) | (r5 >> 2));
            sg = (uint8_t)((g5 << 3) | (g5 >> 2));
            sb = (uint8_t)((b5 << 3) | (b5 >> 2));
        } else {
            const uint8_t *p = tdata + (size_t)sy * (size_t)tpitch + (size_t)sx * 4;
            sr = p[0]; sg = p[1]; sb = p[2]; sa = p[3];
            if (sa == 0 && mode == SDL_BLENDMODE_BLEND) continue;
        }
        if (mod_a) { sa = (uint8_t)((sa * ca + 127u) >> 8); if (sa == 0 && mode == SDL_BLENDMODE_BLEND) continue; }
        if (mod_c) { sr = (uint8_t)((sr * cr + 127u) >> 8); sg = (uint8_t)((sg * cg + 127u) >> 8); sb = (uint8_t)((sb * cb + 127u) >> 8); }
        if (skip_zero && (sr | sg | sb) == 0) continue;
        drow[x] = blend565_rgba(drow[x], sr, sg, sb, sa, mode);
    }
}

static bool render_geometry_band(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *verts, int num_verts,
                                 const int *indices, int num_indices, int ylo, int yhi) {
    if (!r || !verts || num_verts < 3) return true;
    int tw, th, cx0, cy0, cx1, cy1;
    get_target_bounds(r, &tw, &th, &cx0, &cy0, &cx1, &cy1);
    if (cy0 < ylo) cy0 = ylo;
    if (cy1 > yhi) cy1 = yhi;
    int vx = r->has_vp ? r->vp.x : 0;
    int vy = r->has_vp ? r->vp.y : 0;
    int n_tris = indices ? (num_indices / 3) : (num_verts / 3);
    uint32_t mode = t ? t->blend : r->draw_blend;
    int dstride = 0;
    uint16_t *dbuf = get_dst565(r, &dstride);

    for (int ti = 0; ti < n_tris; ti++) {
        int i0 = indices ? indices[ti * 3 + 0] : ti * 3 + 0;
        int i1 = indices ? indices[ti * 3 + 1] : ti * 3 + 1;
        int i2 = indices ? indices[ti * 3 + 2] : ti * 3 + 2;
        if (i0 < 0 || i0 >= num_verts || i1 < 0 || i1 >= num_verts || i2 < 0 || i2 >= num_verts) continue;
        const SDL_Vertex *v0 = &verts[i0], *v1 = &verts[i1], *v2 = &verts[i2];
        float x0 = (float)vx + v0->position.x * r->scale_x, y0 = (float)vy + v0->position.y * r->scale_y;
        float x1 = (float)vx + v1->position.x * r->scale_x, y1 = (float)vy + v1->position.y * r->scale_y;
        float x2 = (float)vx + v2->position.x * r->scale_x, y2 = (float)vy + v2->position.y * r->scale_y;
        if ((x0 < cx0 && x1 < cx0 && x2 < cx0) || (x0 >= cx1 && x1 >= cx1 && x2 >= cx1) ||
            (y0 < cy0 && y1 < cy0 && y2 < cy0) || (y0 >= cy1 && y1 >= cy1 && y2 >= cy1)) continue;
        float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (fabsf(area) < 1e-4f) continue;

        /* Fast scanline path for RGB565 target with flat vertex color (99.9% of all battle tiles & menu waves!) */
        uint8_t r0 = clamp_u8f(v0->color.r), g0 = clamp_u8f(v0->color.g), b0 = clamp_u8f(v0->color.b), a0 = clamp_u8f(v0->color.a);
        uint8_t r1 = clamp_u8f(v1->color.r), g1 = clamp_u8f(v1->color.g), b1 = clamp_u8f(v1->color.b), a1 = clamp_u8f(v1->color.a);
        uint8_t r2 = clamp_u8f(v2->color.r), g2 = clamp_u8f(v2->color.g), b2 = clamp_u8f(v2->color.b), a2 = clamp_u8f(v2->color.a);
        bool flat = (r0 == r1 && r0 == r2 && g0 == g1 && g0 == g2 && b0 == b1 && b0 == b2 && a0 == a1 && a0 == a2);

        if (dbuf && flat) {
            /* Sort 3 vertices by Y ascending: p0.y <= p1.y <= p2.y */
            float px[3] = { x0, x1, x2 }, py[3] = { y0, y1, y2 };
            float pu[3] = { 0, 0, 0 }, pv[3] = { 0, 0, 0 };
            if (t && t->data) {
                float fw = (float)t->w, fh = (float)t->h;
                pu[0] = v0->tex_coord.x * fw; pv[0] = v0->tex_coord.y * fh;
                pu[1] = v1->tex_coord.x * fw; pv[1] = v1->tex_coord.y * fh;
                pu[2] = v2->tex_coord.x * fw; pv[2] = v2->tex_coord.y * fh;
            }
            if (py[0] > py[1]) { float tmp; tmp=px[0];px[0]=px[1];px[1]=tmp; tmp=py[0];py[0]=py[1];py[1]=tmp; tmp=pu[0];pu[0]=pu[1];pu[1]=tmp; tmp=pv[0];pv[0]=pv[1];pv[1]=tmp; }
            if (py[1] > py[2]) { float tmp; tmp=px[1];px[1]=px[2];px[2]=tmp; tmp=py[1];py[1]=py[2];py[2]=tmp; tmp=pu[1];pu[1]=pu[2];pu[2]=tmp; tmp=pv[1];pv[1]=pv[2];pv[2]=tmp; }
            if (py[0] > py[1]) { float tmp; tmp=px[0];px[0]=px[1];px[1]=tmp; tmp=py[0];py[0]=py[1];py[1]=tmp; tmp=pu[0];pu[0]=pu[1];pu[1]=tmp; tmp=pv[0];pv[0]=pv[1];pv[1]=tmp; }

            float dy02 = py[2] - py[0];
            if (dy02 < 1e-4f) continue;
            float inv02 = 1.0f / dy02;

            /* Constant UV gradients across X for textured triangles */
            int32_t du_dx = 0, dv_dx = 0;
            if (t && t->data) {
                float sarea = (px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]);
                if (fabsf(sarea) < 1e-4f) continue;
                float isarea = 1.0f / sarea;
                float dudx = ((pu[1] - pu[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (pu[2] - pu[0])) * isarea;
                float dvdx = ((pv[1] - pv[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (pv[2] - pv[0])) * isarea;
                du_dx = (int32_t)(dudx * 65536.0f);
                dv_dx = (int32_t)(dvdx * 65536.0f);
            }

            int y_start = (int)ceilf(py[0] - 0.5f);
            int y_end   = (int)ceilf(py[2] - 0.5f);
            if (y_start < cy0) y_start = cy0;
            if (y_end   > cy1) y_end   = cy1;

            float dy01 = py[1] - py[0];
            float dy12 = py[2] - py[1];
            float inv01 = (dy01 > 1e-4f) ? (1.0f / dy01) : 0.0f;
            float inv12 = (dy12 > 1e-4f) ? (1.0f / dy12) : 0.0f;

            for (int y = y_start; y < y_end; y++) {
                float fy = (float)y + 0.5f;
                float t_long = (fy - py[0]) * inv02;
                float xa = px[0] + (px[2] - px[0]) * t_long;
                float xb;
                float ua = 0, va = 0, ub = 0, vb = 0;
                if (t && t->data) {
                    ua = pu[0] + (pu[2] - pu[0]) * t_long;
                    va = pv[0] + (pv[2] - pv[0]) * t_long;
                }
                if (fy < py[1]) {
                    if (inv01 == 0.0f) continue;
                    float t_s = (fy - py[0]) * inv01;
                    xb = px[0] + (px[1] - px[0]) * t_s;
                    if (t && t->data) {
                        ub = pu[0] + (pu[1] - pu[0]) * t_s;
                        vb = pv[0] + (pv[1] - pv[0]) * t_s;
                    }
                } else {
                    if (inv12 == 0.0f) continue;
                    float t_s = (fy - py[1]) * inv12;
                    xb = px[1] + (px[2] - px[1]) * t_s;
                    if (t && t->data) {
                        ub = pu[1] + (pu[2] - pu[1]) * t_s;
                        vb = pv[1] + (pv[2] - pv[1]) * t_s;
                    }
                }
                float xf_l = xa, xf_r = xb, u_l = ua, v_l = va;
                if (xf_l > xf_r) { xf_l = xb; xf_r = xa; u_l = ub; v_l = vb; }
                int xl = (int)ceilf(xf_l - 0.5f);
                int xr = (int)ceilf(xf_r - 0.5f);
                if (xl < cx0) xl = cx0;
                if (xr > cx1) xr = cx1;
                if (xl >= xr) continue;

                uint16_t *drow = dbuf + y * dstride;
                if (!t || !t->data) {
                    blend_span565_const(drow + xl, xr - xl, r0, g0, b0, a0, mode);
                } else {
                    float dx_sub = ((float)xl + 0.5f) - xf_l;
                    int32_t u_fp = (int32_t)(u_l * 65536.0f) + (int32_t)(dx_sub * (float)du_dx);
                    int32_t v_fp = (int32_t)(v_l * 65536.0f) + (int32_t)(dx_sub * (float)dv_dx);
                    geom_span_textured_565(drow, xl, xr, u_fp, v_fp, du_dx, dv_dx, t, r0, g0, b0, a0, mode);
                }
            }
            continue;
        }

        /* General fallback (vertex-color gradients or RGBA8888 offscreen targets) */
        float inv_area = 1.0f / area;
        int bx0 = (int)floorf(fminf(x0, fminf(x1, x2)));
        int by0 = (int)floorf(fminf(y0, fminf(y1, y2)));
        int bx1 = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
        int by1 = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
        if (bx0 < cx0) bx0 = cx0; if (by0 < cy0) by0 = cy0;
        if (bx1 > cx1) bx1 = cx1; if (by1 > cy1) by1 = cy1;

        for (int y = by0; y < by1; y++) {
            float py = (float)y + 0.5f;
            for (int x = bx0; x < bx1; x++) {
                float px = (float)x + 0.5f;
                float w0 = ((x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)) * inv_area;
                float w1 = ((x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)) * inv_area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                float cr = w0 * v0->color.r + w1 * v1->color.r + w2 * v2->color.r;
                float cg = w0 * v0->color.g + w1 * v1->color.g + w2 * v2->color.g;
                float cb = w0 * v0->color.b + w1 * v1->color.b + w2 * v2->color.b;
                float ca = w0 * v0->color.a + w1 * v1->color.a + w2 * v2->color.a;
                uint8_t sr = clamp_u8f(cr), sg = clamp_u8f(cg), sb = clamp_u8f(cb), sa = clamp_u8f(ca);
                if (t && t->data) {
                    float u = w0 * v0->tex_coord.x + w1 * v1->tex_coord.x + w2 * v2->tex_coord.x;
                    float v = w0 * v0->tex_coord.y + w1 * v1->tex_coord.y + w2 * v2->tex_coord.y;
                    uint8_t tr, tg, tb, ta;
                    sample_tex(t, (int)floorf(u * (float)t->w), (int)floorf(v * (float)t->h), &tr, &tg, &tb, &ta);
                    sr = (uint8_t)((sr * tr + 127u) >> 8);
                    sg = (uint8_t)((sg * tg + 127u) >> 8);
                    sb = (uint8_t)((sb * tb + 127u) >> 8);
                    sa = (uint8_t)((sa * ta + 127u) >> 8);
                }
                put_pixel_target(r, x, y, sr, sg, sb, sa, mode);
            }
        }
    }
    return true;
}

typedef struct {
    SDL_Texture *texture;
    const SDL_Vertex *vertices;
    const int *indices;
    int num_vertices, num_indices;
} MenuGeometryDraw;

static SDL_Renderer *menu_renderer;
static MenuGeometryDraw menu_draws[8];
static void *sgx_menu_lib;
static OgSgxDraw *sgx_draw;
static ScenePage *scene_page;
static void (*scene_begin)(void);
static SceneShadow *scene_shadow;
static void (*sgx_close)(void);
static bool sgx_checked;
static unsigned sgx_frames;

static void close_sgx_menu(bool reopen) {
    sgx_checked = !reopen;
    ++sgx_generation;
    if (sgx_close) sgx_close();
    if (sgx_menu_lib) dlclose(sgx_menu_lib);
    scene_begin = NULL; scene_page = NULL; scene_shadow = NULL;
    sgx_menu_lib = NULL; sgx_draw = NULL; sgx_close = NULL; sgx_forget = NULL;
}

bool og_scene_enabled(void) {
    if (gpu) return false;
    if (model_trace_enabled==1) model_trace_note('S',__builtin_return_address(0),now_ns());
    if (!sgx_checked) {
        sgx_checked = true;
        if (getenv("XGB35_SGX") && !strcmp(getenv("XGB35_SGX"),"1")) {
            sgx_menu_lib = dlopen("opengorky-sgx.so",RTLD_NOW|RTLD_LOCAL);
            if (sgx_menu_lib) {
                scene_begin=(void (*)(void))dlsym(sgx_menu_lib,"og_sgx_scene_begin");
                scene_page=(ScenePage *)dlsym(sgx_menu_lib,"og_sgx_scene_page");
                scene_shadow=(SceneShadow *)dlsym(sgx_menu_lib,"og_sgx_scene_shadow");
                sgx_draw = (OgSgxDraw *)dlsym(sgx_menu_lib,"og_sgx_draw");
                sgx_close = (void (*)(void))dlsym(sgx_menu_lib,"og_sgx_close");
                sgx_forget = (void (*)(unsigned))dlsym(sgx_menu_lib,"og_sgx_forget");
                if (!sgx_close || !sgx_forget || !sgx_draw) close_sgx_menu(false);
            } else fprintf(stderr,"OpenGorky SGX: %s\n",dlerror());
        }
    }
    if (!scene_begin || !scene_page || !scene_shadow) return false;
    scene_begin();
    return true;
}

bool og_model_probe_requested(void) {
    static bool sampled;
    const char *frame=getenv("OG_MODEL_PROBE");
    static unsigned eligible;
    if (sampled || !frame || ++eligible<(unsigned)atoi(frame)) return false;
    sampled = true;
    return true;
}

void og_model_probe(const SceneTriangle *tris,const float *z,int n,int w,int h) {
    void (*probe)(const SceneTriangle *,const float *,int,int,int) =
        (void (*)(const SceneTriangle *,const float *,int,int,int))dlsym(sgx_menu_lib,"og_sgx_probe_models");
    if (probe) probe(tris,z,n,w,h);
}

int og_scene_page(const SceneTriangle *tris,int count,int w,int h,SceneSlot *slots,
                  const uint8_t **out,int *stride,bool *bgra) {
    static bool reported;
    int n = scene_page ? scene_page(tris,count,w,h,slots,out,stride,bgra) : 0;
    if (n && !reported) {
        fprintf(stderr,"OpenGorky SGX: world geometry active (%d triangles in first batch, %dx%d)\n",n,w,h);
        reported=true;
    }
    return n;
}
bool og_scene_shadow(uint8_t *mask,int size,const float *tris,int count) {
    static bool reported;
    bool ok = gpu ? (gpu_shadow && gpu_shadow(mask,size,tris,count)) :
        (og_scene_enabled() && scene_shadow(mask,size,tris,count));
    if (ok && !reported) { fprintf(stderr,"OpenGorky SGX: world shadows active (%d triangles)\n",count); reported=true; }
    return ok;
}

bool og_scene_direct_models_enabled(void) {
    return gpu && gpu_models && !gpu_async_requested && getenv("OG_SGX_GPU_MODELS");
}

bool og_scene_direct_models(const SceneTriangle *tris,const float *z,int n,int w,int h,uint8_t *rgba,float *depth,uint8_t *glow) {
    static bool reported;
    if (!gpu_models || !gpu_models(tris,z,n,w,h,rgba,depth,glow,NULL)) return false;
    if (!reported) {
        fprintf(stderr,"OpenGorky SGX: opaque models with hardware depth (%d triangles)\n",n);
        reported=true;
    }
    return true;
}

bool og_scene_resident_enabled(SDL_Renderer *r,int w,int h) {
    if (!gpu || !gpu_models || !r || !getenv("OG_SGX_RESIDENT") ||
        r->scale_x!=1 || r->scale_y!=1 || (r->has_vp && (r->vp.x || r->vp.y))) return false;
    SDL_Rect clip=gpu_clip(r);
    return w==LOG_W && h==LOG_H && !clip.x && !clip.y && clip.w==w && clip.h==h;
}

bool og_scene_resident(SDL_Renderer *r,const SceneTriangle *tris,const float *z,int n,int w,int h,
                       float *depth,const SceneResident *settings) {
    static bool reported;
    if (!og_scene_resident_enabled(r,w,h) || !gpu_models(tris,z,n,w,h,NULL,depth,NULL,settings)) return false;
    if (!reported) {
        fprintf(stderr,"OpenGorky SGX: resident models and shadows (%d + %d triangles, 512x512 mask)\n",
                n,settings->mask_count);
        reported=true;
    }
    return true;
}

static bool draw_sgx_menu(SDL_Renderer *r, size_t count) {
    if (sgx_checked && !sgx_draw) return false;
    if (!r || !count || r->scale_x != 1 || r->scale_y != 1) return false;
    SDL_Texture *dst = r->target;
    int w,h,x0,y0,x1,y1;
    get_target_bounds(r,&w,&h,&x0,&y0,&x1,&y1);
    if (!dst || dst->pix_fmt != PIX_RGB565 || dst->pitch != w*2 ||
        x0 || y0 || x1 != w || y1 != h || (r->has_vp && (r->vp.x || r->vp.y))) return false;
    if (!sgx_checked) og_scene_enabled();
    if (!sgx_draw) return false;
    OgSgxMesh meshes[sizeof(menu_draws)/sizeof(menu_draws[0])];
    for (size_t i=0; i<count; i++) {
        const MenuGeometryDraw *d=&menu_draws[i];
        SDL_Texture *t=d->texture;
        if (!t || !t->data || t->access != SDL_TEXTUREACCESS_STATIC || t->blend != OG_BLEND_MULTIPLY || !d->indices || d->num_indices <= 0 ||
            (t->pix_fmt != PIX_RGB565 && t->pix_fmt != PIX_RGBA8888) ||
            t->pitch != t->w*(t->pix_fmt == PIX_RGB565 ? 2 : 4)) return false;
        if (t->sgx_generation != sgx_generation) {
            t->sgx_id = 0;
            t->sgx_generation = sgx_generation;
        }
        meshes[i]=(OgSgxMesh){d->vertices,d->indices,d->num_vertices,d->num_indices,
                              t->data,&t->sgx_id,t->w,t->h,t->pix_fmt == PIX_RGB565,t->scale_mode == 1};
    }
    if (!sgx_draw(dst->data,w,h,meshes,(int)count)) {
        fprintf(stderr,"OpenGorky SGX: hardware draw failed, using software geometry\n");
        close_sgx_menu(false);
        return false;
    }
    if (++sgx_frames == 1 || sgx_frames == 60)
        fprintf(stderr,"OpenGorky SGX: PowerVR SGX 540, menu meshes=%zu frames=%u\n",count,sgx_frames);
    return true;
}
static size_t menu_draw_count;
static int menu_split_y;
static pthread_t menu_thread;
static pthread_mutex_t menu_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t menu_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t menu_done = PTHREAD_COND_INITIALIZER;
static bool menu_running, menu_pending, menu_stopping;

static void render_menu_band(int ylo, int yhi) {
    for (size_t i = 0; i < menu_draw_count; i++) {
        const MenuGeometryDraw *d = &menu_draws[i];
        render_geometry_band(menu_renderer, d->texture, d->vertices, d->num_vertices,
                             d->indices, d->num_indices, ylo, yhi);
    }
}

static void *menu_worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&menu_mutex);
    while (!menu_stopping) {
        if (!menu_pending) {
            pthread_cond_wait(&menu_work, &menu_mutex);
            continue;
        }
        pthread_mutex_unlock(&menu_mutex);
        render_menu_band(menu_split_y, INT_MAX);
        pthread_mutex_lock(&menu_mutex);
        menu_pending = false;
        pthread_cond_signal(&menu_done);
    }
    pthread_mutex_unlock(&menu_mutex);
    return NULL;
}

static void stop_menu_worker(void) {
    if (!menu_running) return;
    pthread_mutex_lock(&menu_mutex);
    menu_stopping = true;
    pthread_cond_signal(&menu_work);
    pthread_mutex_unlock(&menu_mutex);
    pthread_join(menu_thread, NULL);
    menu_running = false;
    menu_stopping = false;
}

void og_xgb35_BeginMenuGeometry(SDL_Renderer *r) {
    if (gpu) return;
    if (r && r->target && r->target->pix_fmt == PIX_RGB565) menu_renderer = r;
}

void og_xgb35_EndMenuGeometry(void) {
    static int profile = -1;
    static unsigned samples, gpu_samples;
    static uint64_t wall_start, geometry_ns;
    if (profile < 0) profile = getenv("OG_MENU_BENCH") != NULL;
    bool measure = profile && menu_draw_count;
    uint64_t start = measure ? now_ns() : 0;
    SDL_Texture *dest = measure && samples == 3 && menu_renderer ? menu_renderer->target : NULL;
    uint16_t *reference = NULL;
    if (dest && getenv("OG_MENU_CHECK")) {
        size_t bytes = (size_t)dest->pitch * dest->h;
        reference = malloc(bytes);
        if (reference) memcpy(reference, dest->data, bytes);
    }
    bool gpu = draw_sgx_menu(menu_renderer,menu_draw_count);
    if (reference && gpu) {
        uint16_t *actual = dest->data;
        dest->data = reference;
        render_menu_band(0,INT_MAX);
        dest->data = actual;
        unsigned different = 0, large = 0;
        for (int i=0; i<dest->w*dest->h; i++) {
            uint16_t a=actual[i], b=reference[i];
            different += a != b;
            large += abs((a>>11)-(b>>11)) > 2 || abs(((a>>5)&63)-((b>>5)&63)) > 4 ||
                     abs((a&31)-(b&31)) > 2;
        }
        fprintf(stderr,"OpenGorky menu check: pixels=%d differing=%u above_tolerance=%u\n",
                dest->w*dest->h,different,large);
    }
    free(reference);
    if (gpu) menu_draw_count = 0;
    if (menu_draw_count > 1) {
        if (!menu_running) menu_running = pthread_create(&menu_thread, NULL, menu_worker, NULL) == 0;
        if (menu_running) {
            // Each band traverses the original draw order and touches disjoint rows.
            menu_split_y = menu_renderer->target->h / 2;
            pthread_mutex_lock(&menu_mutex);
            menu_pending = true;
            pthread_cond_signal(&menu_work);
            pthread_mutex_unlock(&menu_mutex);
            render_menu_band(0, menu_split_y);
            pthread_mutex_lock(&menu_mutex);
            while (menu_pending) pthread_cond_wait(&menu_done, &menu_mutex);
            pthread_mutex_unlock(&menu_mutex);
            menu_draw_count = 0;
        }
    }
    if (menu_draw_count) render_menu_band(0, INT_MAX);
    menu_draw_count = 0;
    if (measure) {
        ++samples;
        if (samples == 5) wall_start = now_ns();
        if (samples > 5 && samples <= 35) {
            geometry_ns += now_ns() - start;
            gpu_samples += gpu;
            if (samples == 35)
                fprintf(stderr,"OpenGorky menu benchmark: frames=30 gpu_frames=%u geometry_ms=%.2f menu_fps=%.2f\n",
                        gpu_samples, geometry_ns/30e6, 30e9/(now_ns()-wall_start));
        }
    }
    menu_renderer = NULL;
}

bool og_sdl_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *verts, int num_verts,
                           const int *indices, int num_indices) {
    if (gpu) {
        if (!r || !verts || num_verts<3) return true;
        if (t && !gpu_texture(t)) return false;
        int vx=r->has_vp?r->vp.x:0, vy=r->has_vp?r->vp.y:0;
        bool flip=t && t->access==SDL_TEXTUREACCESS_TARGET;
        gpu->geometry(t?t->gpu.id:0,t?t->blend:r->draw_blend,
                      gpu_clip(r),verts,num_verts,indices,num_indices,false,
                      (SDL_FRect){vx,vy,r->scale_x,r->scale_y},flip);
        return true;
    }
    if (menu_renderer && r == menu_renderer && menu_draw_count < sizeof(menu_draws) / sizeof(*menu_draws)) {
        menu_draws[menu_draw_count++] = (MenuGeometryDraw){ t, verts, indices, num_verts, num_indices };
        return true;
    }
    if (menu_renderer) og_xgb35_EndMenuGeometry();
    return render_geometry_band(r, t, verts, num_verts, indices, num_indices, 0, INT_MAX);
}

/* Minimal 5x7 uppercase/ASCII bitmap font for loading screen & debug text */
static const uint8_t g_font5x7[][5] = {
    [' ' - 32] = {0x00,0x00,0x00,0x00,0x00}, ['-'-32] = {0x08,0x08,0x08,0x08,0x08},
    ['.' - 32] = {0x00,0x60,0x60,0x00,0x00}, ['/'-32] = {0x20,0x10,0x08,0x04,0x02},
    ['0' - 32] = {0x3E,0x51,0x49,0x45,0x3E}, ['1'-32] = {0x00,0x42,0x7F,0x40,0x00},
    ['2' - 32] = {0x42,0x61,0x51,0x49,0x46}, ['3'-32] = {0x21,0x41,0x45,0x4B,0x31},
    ['4' - 32] = {0x18,0x14,0x12,0x7F,0x10}, ['5'-32] = {0x27,0x45,0x45,0x45,0x39},
    ['6' - 32] = {0x3C,0x4A,0x49,0x49,0x30}, ['7'-32] = {0x01,0x71,0x09,0x05,0x03},
    ['8' - 32] = {0x36,0x49,0x49,0x49,0x36}, ['9'-32] = {0x06,0x49,0x49,0x29,0x1E},
    ['A' - 32] = {0x7E,0x11,0x11,0x11,0x7E}, ['B'-32] = {0x7F,0x49,0x49,0x49,0x36},
    ['C' - 32] = {0x3E,0x41,0x41,0x41,0x22}, ['D'-32] = {0x7F,0x41,0x41,0x22,0x1C},
    ['E' - 32] = {0x7F,0x49,0x49,0x49,0x41}, ['F'-32] = {0x7F,0x09,0x09,0x09,0x01},
    ['G' - 32] = {0x3E,0x41,0x49,0x49,0x7A}, ['H'-32] = {0x7F,0x08,0x08,0x08,0x7F},
    ['I' - 32] = {0x00,0x41,0x7F,0x41,0x00}, ['J'-32] = {0x20,0x40,0x41,0x3F,0x01},
    ['K' - 32] = {0x7F,0x08,0x14,0x22,0x41}, ['L'-32] = {0x7F,0x40,0x40,0x40,0x40},
    ['M' - 32] = {0x7F,0x02,0x0C,0x02,0x7F}, ['N'-32] = {0x7F,0x04,0x08,0x10,0x7F},
    ['O' - 32] = {0x3E,0x41,0x41,0x41,0x3E}, ['P'-32] = {0x7F,0x09,0x09,0x09,0x06},
    ['Q' - 32] = {0x3E,0x41,0x51,0x21,0x5E}, ['R'-32] = {0x7F,0x09,0x19,0x29,0x46},
    ['S' - 32] = {0x46,0x49,0x49,0x49,0x31}, ['T'-32] = {0x01,0x01,0x7F,0x01,0x01},
    ['U' - 32] = {0x3F,0x40,0x40,0x40,0x3F}, ['V'-32] = {0x1F,0x20,0x40,0x20,0x1F},
    ['W' - 32] = {0x3F,0x40,0x38,0x40,0x3F}, ['X'-32] = {0x63,0x14,0x08,0x14,0x63},
    ['Y' - 32] = {0x07,0x08,0x70,0x08,0x07}, ['Z'-32] = {0x61,0x51,0x49,0x45,0x43},
};

bool og_sdl_RenderDebugText(SDL_Renderer *r, float x, float y, const char *str) {
    if (!r || !str) return true;
    int tw, th, cx0, cy0, cx1, cy1;
    get_target_bounds(r, &tw, &th, &cx0, &cy0, &cx1, &cy1);
    int px = (int)x, py = (int)y;
    for (const char *p = str; *p; p++, px += 12) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
        if (ch < 32 || ch > 'Z') continue;
        const uint8_t *glyph = g_font5x7[ch - 32];
        for (int col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; row++) {
                if (!(bits & (1u << row))) continue;
                if (gpu) {
                    gpu_solid((SDL_Rect){cx0,cy0,cx1-cx0,cy1-cy0},
                              (SDL_FRect){px+col*2,py+row*2,2,2},r->dr,r->dg,r->db,r->da,SDL_BLENDMODE_NONE);
                    continue;
                }
                for (int sy = 0; sy < 2; sy++) {
                    for (int sx = 0; sx < 2; sx++) {
                        int gx = px + col * 2 + sx, gy = py + row * 2 + sy;
                        if (gx >= cx0 && gx < cx1 && gy >= cy0 && gy < cy1)
                            put_pixel_target(r, gx, gy, r->dr, r->dg, r->db, r->da, SDL_BLENDMODE_NONE);
                    }
                }
            }
        }
    }
    return true;
}

static void draw_fps(SDL_Renderer *r) {
    uint64_t now = now_ns();
    if (!r->fps_start) {
        r->fps_start = now;
        memcpy(r->fps_text, "FPS -", sizeof("FPS -"));
    } else {
        uint64_t elapsed = now - r->fps_start;
        ++r->fps_frames;
        if (elapsed >= 500000000) {
            unsigned fps = (unsigned)((r->fps_frames * 1000000000ull + elapsed / 2) / elapsed);
            snprintf(r->fps_text, sizeof(r->fps_text), "FPS %u", fps);
            r->fps_start = now;
            r->fps_frames = 0;
        }
    }

    int right = LOG_W, top = 0;
    if (!r->win->use_x11 && g_handheld_1to1) {
        fb0_crop_origin((int)g_mouse_x, (int)g_mouse_y, &right, &top);
        right += 480;
    }
    char label[32];
    int len = snprintf(label, sizeof(label), "%s", r->fps_text);
    if (hidden_passes) {
        label[len++] = ' ';
        label[len++] = '-';
        for (unsigned i = 0; i < 5; i++)
            if (hidden_passes & (1u << i)) label[len++] = '5' + i;
        label[len] = 0;
    }
    int width = len * 12 + 6;
    SDL_FRect bg = { right - width - 6, top + 6, width, 20 };
    SDL_Renderer overlay = { .backbuf = r->backbuf, .da = 255, .scale_x = 1, .scale_y = 1 };
    og_sdl_RenderFillRect(&overlay, &bg);
    overlay.dr = overlay.dg = overlay.db = 255;
    og_sdl_RenderDebugText(&overlay, bg.x + 4, bg.y + 3, label);
}

/* --- Present: DE_ML0 HDMI / X11 XShm / /dev/fb0 ST7796S --- */
bool og_sdl_RenderPresent(SDL_Renderer *r) {
    if (!r || !r->win) return false;
    SDL_Window *w = r->win;
    if (gpu) {
        draw_fps(r);
        if (!gpu->present()) return false;
        // FBIOPAN_DISPLAY can restore the framebuffer's unscaled geometry.
        if (gpu_hdmi_scaled) {
            int h=gpu_display_h,w=h*LOG_W/LOG_H;
            if (w>gpu_display_w) { w=gpu_display_w; h=w*LOG_H/LOG_W; }
            de_apply((gpu_display_w-w)/2,(gpu_display_h-h)/2,w,h);
        }
        return true;
    }
    if (r == g_ren) draw_fps(r);

    if (w->use_x11) {
        /* Fast path: Fullscreen HDMI /dev/fb1 + ATM7051H DE_ML0 hardware scaler (0.0 ms CPU!) */
        if (w->fullscreen && w->dfb_ptr && w->dfb_de_ok && w->w == w->dfb_w && w->h == w->dfb_h) {
            int oh = w->dfb_h, ow = (oh * LOG_W) / LOG_H;
            if (ow > w->dfb_w) { ow = w->dfb_w; oh = (ow * LOG_H) / LOG_W; }
            int ox = (w->dfb_w - ow) / 2, oy = (w->dfb_h - oh) / 2;
            for (int y = 0; y < LOG_H; y++) {
                memcpy(w->dfb_ptr + (size_t)y * w->dfb_pitch, r->backbuf + y * LOG_W, LOG_W * 2);
            }
            de_apply(ox, oy, ow, oh);
            return true;
        }
        de_restore();

        /* Windowed / non-DE X11 path: 4:3 fit via XShm or XPutImage */
        int ww = w->w > 0 ? w->w : LOG_W;
        int wh = w->h > 0 ? w->h : LOG_H;
        int vh = wh, vw = (vh * LOG_W) / LOG_H;
        if (vw > ww) { vw = ww; vh = (vw * LOG_H) / LOG_W; }
        int ox = (ww - vw) / 2, oy = (wh - vh) / 2;

        x11_ensure_img(w, vw, vh);
        if (!w->ximg || !w->xbuf) return true;

        if (vw == LOG_W && vh == LOG_H) {
            if (w->xbuf_bpp == 2) {
                memcpy(w->xbuf, r->backbuf, LOG_W * LOG_H * 2);
            } else {
                for (int y = 0; y < LOG_H; y++)
                    rgb565_row_to_bgrx32(r->backbuf + y * LOG_W, (uint32_t *)w->xbuf + y * LOG_W, LOG_W);
            }
        } else {
            uint16_t row565[1920];
            int cw = vw > 1920 ? 1920 : vw;
            for (int y = 0; y < vh; y++) {
                int sy = (y * LOG_H) / vh;
                const uint16_t *srow = r->backbuf + sy * LOG_W;
                if (w->xbuf_bpp == 2) {
                    uint16_t *drow = (uint16_t *)(w->xbuf + (size_t)y * (size_t)w->ximg->bytes_per_line);
                    for (int x = 0; x < cw; x++) drow[x] = srow[(x * LOG_W) / vw];
                } else {
                    for (int x = 0; x < cw; x++) row565[x] = srow[(x * LOG_W) / vw];
                    uint32_t *drow = (uint32_t *)(w->xbuf + (size_t)y * (size_t)w->ximg->bytes_per_line);
                    rgb565_row_to_bgrx32(row565, drow, cw);
                }
            }
        }
        if (w->use_shm && w->x11.ShmPutImage) {
            w->x11.ShmPutImage(w->dpy, w->win, w->gc, w->ximg, 0, 0, ox, oy, (unsigned)vw, (unsigned)vh, False);
        } else {
            w->x11.PutImage(w->dpy, w->win, w->gc, w->ximg, 0, 0, ox, oy, (unsigned)vw, (unsigned)vh);
        }
        w->x11.Flush(w->dpy);
        return true;
    }

    /* Handheld 3.5" LCD (/dev/fb0 480x320 ST7796S) — zero-copy dual-core pipelined on Core 1! */
    if (!w->fb_raw) return true;
    if (g_fb_thread_started) {
        pthread_mutex_lock(&g_fb_mtx);
        g_fb_stage[g_fb_back] = r->backbuf;
        int tmp = g_fb_ready; g_fb_ready = g_fb_back; g_fb_back = tmp;
        r->backbuf = g_fb_stage[g_fb_back];
        g_fb_job_1to1 = g_handheld_1to1;
        g_fb_job_mx = (int)g_mouse_x;
        g_fb_job_my = (int)g_mouse_y;
        g_fb_has_job = true;
        pthread_cond_signal(&g_fb_cv);
        pthread_mutex_unlock(&g_fb_mtx);
        return true;
    }
    fb0_draw(w, r->backbuf, g_handheld_1to1, (int)g_mouse_x, (int)g_mouse_y);
    return true;
}

static struct {
    SDL_Renderer renderer;
    SDL_Texture *const *frames;
    size_t count;
    unsigned hz, first;
    pthread_t worker;
    bool started, stop;
} g_loading;
static pthread_mutex_t g_loading_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_loading_cv = PTHREAD_COND_INITIALIZER;

static void draw_loading_frame(size_t index) {
    SDL_Renderer *r = &g_loading.renderer;
    SDL_Texture *t = g_loading.frames[index];
    memset(r->backbuf, 0, LOG_W * LOG_H * sizeof(*r->backbuf));
    SDL_FRect dst = { floorf((LOG_W - t->w) * 0.5f), floorf((LOG_H - t->h) * 0.5f), t->w, t->h };
    og_sdl_RenderTexture(r, t, NULL, &dst);
    if (r->win->use_x11) og_sdl_RenderPresent(r);
    else fb0_draw(r->win, r->backbuf, false, LOG_W / 2, LOG_H / 2);
}

static void *loading_worker(void *unused) {
    (void)unused;
    const uint64_t start = now_ns(), interval = 1000000000u / g_loading.hz;
    pthread_mutex_lock(&g_loading_mtx);
    while (!g_loading.stop) {
        pthread_mutex_unlock(&g_loading_mtx);
        const uint64_t tick = (now_ns() - start) / interval;
        draw_loading_frame((g_loading.first + tick) % g_loading.count);
        const uint64_t next = start + (tick + 1) * interval, now = now_ns();
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        uint64_t ns = deadline.tv_nsec + (next > now ? next - now : 0);
        deadline.tv_sec += ns / 1000000000u;
        deadline.tv_nsec = ns % 1000000000u;
        pthread_mutex_lock(&g_loading_mtx);
        if (!g_loading.stop) pthread_cond_timedwait(&g_loading_cv, &g_loading_mtx, &deadline);
    }
    pthread_mutex_unlock(&g_loading_mtx);
    return NULL;
}

void og_xgb35_BeginLoading(SDL_Renderer *r, SDL_Texture *const *frames, size_t count,
                           unsigned hz, unsigned first) {
    if (gpu) return;
    if (g_loading.started || !r || !r->win || !frames || count == 0 || hz == 0 ||
        hz > 1000000000u || (!r->win->use_x11 && !r->win->fb_raw)) return;
    g_loading.renderer = (SDL_Renderer){ .win = r->win, .scale_x = 1, .scale_y = 1 };
    g_loading.renderer.backbuf = malloc(LOG_W * LOG_H * sizeof(uint16_t));
    if (!g_loading.renderer.backbuf) return;
    g_loading.frames = frames;
    g_loading.count = count;
    g_loading.hz = hz;
    g_loading.first = first;
    g_loading.stop = false;
    // Remove a queued frame from the scene that preceded loading.
    pthread_mutex_lock(&g_fb_mtx);
    g_fb_has_job = false;
    pthread_mutex_unlock(&g_fb_mtx);
    if ((!r->win->use_x11 || g_x11_threaded) &&
        pthread_create(&g_loading.worker, NULL, loading_worker, NULL) == 0) {
        g_loading.started = true;
    } else {
        draw_loading_frame(first % count);
        free(g_loading.renderer.backbuf);
    }
}

void og_xgb35_EndLoading(void) {
    if (!g_loading.started) return;
    pthread_mutex_lock(&g_loading_mtx);
    g_loading.stop = true;
    pthread_cond_signal(&g_loading_cv);
    pthread_mutex_unlock(&g_loading_mtx);
    pthread_join(g_loading.worker, NULL);
    g_loading.started = false;
    free(g_loading.renderer.backbuf);
}

SDL_Surface *og_sdl_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect) {
    if (gpu) return NULL;
    if (!r) return NULL;
    const SDL_Texture *target = r->target;
    const int width = target ? target->w : LOG_W;
    const int height = target ? target->h : LOG_H;
    int stride = 0;
    const uint16_t *pixels565 = get_dst565(r, &stride);
    const uint8_t *rgba = target && target->pix_fmt == PIX_RGBA8888 ? target->data : NULL;
    if (!pixels565 && !rgba) return NULL;
    int rx = rect ? rect->x : 0, ry = rect ? rect->y : 0;
    int rw = rect ? rect->w : width, rh = rect ? rect->h : height;
    if (rw <= 0 || rh <= 0) return NULL;
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (!s) return NULL;
    s->w = rw; s->h = rh; s->pitch = rw * 4; s->format = SDL_PIXELFORMAT_ABGR8888;
    s->pixels = malloc((size_t)rw * (size_t)rh * 4);
    if (!s->pixels) { free(s); return NULL; }
    for (int y = 0; y < rh; y++) {
        uint8_t *dst = (uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch;
        int sy = ry + y;
        for (int x = 0; x < rw; x++) {
            int sx = rx + x;
            uint8_t cr = 0, cg = 0, cb = 0;
            if (sx >= 0 && sx < width && sy >= 0 && sy < height) {
                if (rgba) {
                    memcpy(dst + x * 4, rgba + (size_t)sy * target->pitch + (size_t)sx * 4, 4);
                    continue;
                }
                unpack565(pixels565[(size_t)sy * stride + sx], &cr, &cg, &cb);
            }
            dst[x * 4 + 0] = cr; dst[x * 4 + 1] = cg; dst[x * 4 + 2] = cb; dst[x * 4 + 3] = 255;
        }
    }
    return s;
}

bool og_sdl_RenderCoordinatesToWindow(SDL_Renderer *r, float x, float y, float *wx, float *wy) {
    (void)r;
    if (wx) *wx = x;
    if (wy) *wy = y;
    return true;
}
bool og_sdl_ConvertEventToRenderCoordinates(SDL_Renderer *r, SDL_Event *ev) {
    (void)r; (void)ev;
    return true; /* events are already in 640x480 logical coordinates */
}

/* --- Surfaces --- */
SDL_Surface *og_sdl_CreateSurfaceFrom(int w, int h, SDL_PixelFormat fmt, void *pixels, int pitch) {
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (!s) return NULL;
    s->flags = 1u; /* borrowed pixels */
    s->format = fmt; s->w = w; s->h = h; s->pitch = pitch; s->pixels = pixels;
    return s;
}
SDL_Surface *og_sdl_ConvertSurface(SDL_Surface *s, SDL_PixelFormat fmt) {
    if (!s) return NULL;
    SDL_Surface *out = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (!out) return NULL;
    out->format = fmt; out->w = s->w; out->h = s->h; out->pitch = s->w * 4;
    out->pixels = malloc((size_t)out->pitch * (size_t)out->h);
    if (!out->pixels) { free(out); return NULL; }
    for (int y = 0; y < s->h; y++)
        memcpy((uint8_t *)out->pixels + (size_t)y * (size_t)out->pitch,
               (const uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch, (size_t)out->pitch);
    return out;
}
void og_sdl_DestroySurface(SDL_Surface *s) {
    if (!s) return;
    if (!(s->flags & 1u)) free(s->pixels);
    free(s);
}

/* --- Event Polling --- */
static void pump_all_inputs(void) {
    if (g_win) {
        if (g_win->use_x11) x11_pump(g_win);
        ev_poll(&g_win->evdev, g_win->use_x11);
    }
    if (g_quit_req) {
        g_quit_req = 0;
        SDL_Event ev; memset(&ev, 0, sizeof(ev));
        ev.type = SDL_EVENT_QUIT;
        evq_push(&ev);
    }
}

bool og_sdl_PollEvent(SDL_Event *ev) {
    pump_all_inputs();
    if (g_ev_head == g_ev_tail) return false;
    if (ev) {
        *ev = g_evq[g_ev_head];
        if (ev->type == SDL_EVENT_TEXT_INPUT) ev->text.text = ev->text.buf;
    }
    g_ev_head = (g_ev_head + 1) % EVENT_Q_CAP;
    return true;
}

bool og_sdl_WaitEventTimeout(SDL_Event *ev, int32_t timeout_ms) {
    if (og_sdl_PollEvent(ev)) return true;
    uint64_t end = og_sdl_GetTicks() + (timeout_ms > 0 ? (uint64_t)timeout_ms : 0ull);
    while (og_sdl_GetTicks() < end) {
        usleep(2000);
        if (og_sdl_PollEvent(ev)) return true;
    }
    return false;
}

bool og_sdl_PushEvent(SDL_Event *ev) { return ev ? evq_push(ev) : false; }
const bool *og_sdl_GetKeyboardState(int *numkeys) { if (numkeys) *numkeys = 512; return g_key_state; }
SDL_Keymod og_sdl_GetModState(void) { return g_key_mod; }
SDL_Keycode og_sdl_GetKeyFromName(const char *name) {
    if (!name || !name[0]) return 0;
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '5' && name[1] <= '9' && !name[2])
        return OG_F5 + name[1] - '5';
    if (name[1] == 0) {
        char ch = name[0];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        return (SDL_Keycode)(unsigned char)ch;
    }
    if (strcasecmp(name, "Escape") == 0) return SDLK_ESCAPE;
    if (strcasecmp(name, "Return") == 0) return SDLK_RETURN;
    if (strcasecmp(name, "Space") == 0)  return SDLK_SPACE;
    if (strcasecmp(name, "Tab") == 0)    return SDLK_TAB;
    if (strcasecmp(name, "Up") == 0)     return SDLK_UP;
    if (strcasecmp(name, "Down") == 0)   return SDLK_DOWN;
    if (strcasecmp(name, "Left") == 0)   return SDLK_LEFT;
    if (strcasecmp(name, "Right") == 0)  return SDLK_RIGHT;
    return 0;
}
bool og_sdl_StartTextInput(SDL_Window *w) { (void)w; g_text_input = true; return true; }
bool og_sdl_StopTextInput(SDL_Window *w)  { (void)w; g_text_input = false; return true; }
bool og_sdl_TextInputActive(SDL_Window *w) { (void)w; return g_text_input; }
bool og_sdl_HideCursor(void) { g_cursor_visible = false; return true; }
bool og_sdl_CursorVisible(void) { return g_cursor_visible; }

/* --- Linux 3.10 POSIX stat helpers (avoids statx ENOSYS in Zig 0.17 std.Io.Threaded) --- */
typedef struct {
    uint64_t ino;
    uint64_t nlink;
    uint64_t size;
    uint32_t mode;
    uint32_t blksize;
    int64_t atime_sec, atime_nsec;
    int64_t mtime_sec, mtime_nsec;
    int64_t ctime_sec, ctime_nsec;
} OgStat310;

static void fill_stat310(const struct stat *st, OgStat310 *out) {
    out->ino = (uint64_t)st->st_ino;
    out->nlink = (uint64_t)st->st_nlink;
    out->size = (uint64_t)st->st_size;
    out->mode = (uint32_t)st->st_mode;
    out->blksize = (uint32_t)(st->st_blksize > 0 ? st->st_blksize : 4096);
    out->atime_sec = (int64_t)st->st_atim.tv_sec;
    out->atime_nsec = (int64_t)st->st_atim.tv_nsec;
    out->mtime_sec = (int64_t)st->st_mtim.tv_sec;
    out->mtime_nsec = (int64_t)st->st_mtim.tv_nsec;
    out->ctime_sec = (int64_t)st->st_ctim.tv_sec;
    out->ctime_nsec = (int64_t)st->st_ctim.tv_nsec;
}

int og_linux310_fstat(int fd, OgStat310 *out) {
    struct stat st;
    if (fstat(fd, &st) != 0) return -errno;
    fill_stat310(&st, out);
    return 0;
}

int og_linux310_fstatat(int dirfd, const char *path, int follow_symlinks, OgStat310 *out) {
    struct stat st;
    int flags = follow_symlinks ? 0 : AT_SYMLINK_NOFOLLOW;
    if (fstatat(dirfd, path, &st, flags) != 0) return -errno;
    fill_stat310(&st, out);
    return 0;
}

int og_linux310_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    if (renameat(olddirfd, oldpath, newdirfd, newpath) != 0) return -errno;
    return 0;
}

ssize_t og_linux310_pread(int fd, void *buf, size_t count, int64_t offset) {
    ssize_t n = pread(fd, buf, count, (off_t)offset);
    return (n < 0) ? -errno : n;
}

ssize_t og_linux310_pwrite(int fd, const void *buf, size_t count, int64_t offset) {
    ssize_t n = pwrite(fd, buf, count, (off_t)offset);
    return (n < 0) ? -errno : n;
}

int og_linux310_faccessat(int dirfd, const char *path, int mode) {
    /* flags=0 forces musl to use Linux 2.6 SYS_faccessat instead of Linux 5.8 SYS_faccessat2 */
    if (faccessat(dirfd, path, mode, 0) != 0) return -errno;
    return 0;
}

/* Intercept libc statx() and getrandom() for Linux 3.10.52 (which lacks both syscalls) */
int statx(int dirfd, const char *__restrict pathname, int flags, unsigned int mask, struct statx *__restrict stx) {
    (void)mask;
    struct stat st;
    int rc;
    if ((flags & 0x1000 /* AT_EMPTY_PATH */) && (!pathname || pathname[0] == '\0')) {
        rc = fstat(dirfd, &st);
    } else {
        int f = (flags & AT_SYMLINK_NOFOLLOW) ? AT_SYMLINK_NOFOLLOW : 0;
        rc = fstatat(dirfd, pathname, &st, f);
    }
    if (rc != 0) return -1;
    memset(stx, 0, sizeof(*stx));
    stx->stx_mask = 0x000007FFu; /* STATX_BASIC_STATS */
    stx->stx_blksize = (uint32_t)(st.st_blksize > 0 ? st.st_blksize : 4096);
    stx->stx_nlink = (uint32_t)st.st_nlink;
    stx->stx_uid = (uint32_t)st.st_uid;
    stx->stx_gid = (uint32_t)st.st_gid;
    stx->stx_mode = (uint16_t)st.st_mode;
    stx->stx_ino = (uint64_t)st.st_ino;
    stx->stx_size = (uint64_t)st.st_size;
    stx->stx_blocks = (uint64_t)st.st_blocks;
    stx->stx_atime.tv_sec = (int64_t)st.st_atim.tv_sec; stx->stx_atime.tv_nsec = (uint32_t)st.st_atim.tv_nsec;
    stx->stx_mtime.tv_sec = (int64_t)st.st_mtim.tv_sec; stx->stx_mtime.tv_nsec = (uint32_t)st.st_mtim.tv_nsec;
    stx->stx_ctime.tv_sec = (int64_t)st.st_ctim.tv_sec; stx->stx_ctime.tv_nsec = (uint32_t)st.st_ctim.tv_nsec;
    stx->stx_btime = stx->stx_ctime;
    return 0;
}

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    static int ufd = -1;
    if (ufd < 0) ufd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (ufd < 0) { errno = ENOSYS; return -1; }
    return read(ufd, buf, buflen);
}


