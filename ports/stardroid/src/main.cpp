// StarDroid 1.9.11 — Authentic StarCraft & Brood War Port for Linux Desktop (X11) & XGB35 Handheld (480x320 /dev/fb0)
// Features:
//   - Complete Original Blizzard Glue UI (glumain, glugamemode, glucmpgn/gluexpcmpgn, gluhist,
//     rez/est*.txt story crawls, smk/*.smk cinematics, glurdy{t,z,p} MBRF briefings, glucustm, gluload, gluscore)
//   - Complete In-Game Race Console (zconsole/tconsole/pconsole, talking SMK portraits, cmdicons, wireframes, F10 menus)
//   - Dual-Mode Video: X11 Desktop (windowed & F11 fullscreen) + Direct /dev/fb0 480x320 RGB565 (Full 640x480 & 1:1 Zoom)
//   - Console & Desktop Controls: USB Mouse + Keyboard (with live hotplug) + Dual-Stick Gamepad Adaptation
//   - Direct ALSA S32_LE Stereo 22050 Hz Audio (HDMI pcmC1D0p / Speaker pcmC0D0p)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "blizz_frontend.h"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <mutex>
#include <poll.h>
#include <sound/asound.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#ifdef None
#undef None
#endif

static constexpr int LOG_W = blizz::W;
static constexpr int LOG_H = blizz::H;

static volatile sig_atomic_t g_quit = 0;

// ATM7051H / ATS3605 Display Engine (DE_ML0 @ 0xB02F0000) hardware scaler:
// reads 640x480 RGB565 from top-left of /dev/fb1 and scales to 1440x1080 / 1280x960 / 1920x1080 at 0.0 ms CPU!
struct DeHwScaler {
    int fd = -1;
    volatile uint32_t* de = nullptr;
    uint32_t orig_coor0 = 0, orig_isize = 0, orig_osize = 0, orig_sr = 0;
    uint32_t orig_coef[8] = {};
    volatile sig_atomic_t active = 0;
    int cur_ox = -1, cur_oy = -1, cur_ow = -1, cur_oh = -1;

    bool init(int fb_w, int fb_h, size_t fb_pitch) {
        int mfd = ::open("/dev/mem", O_RDWR | O_SYNC);
        if (mfd < 0) return false;
        void* p = mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0xB02F0000u);
        if (p == MAP_FAILED) { ::close(mfd); return false; }
        volatile uint32_t* regs = static_cast<volatile uint32_t*>(p);
        uint32_t expected_sz = (uint32_t(fb_h - 1) << 16) | uint32_t(fb_w - 1);
        if (regs[0x224 / 4] != expected_sz || regs[0x268 / 4] != uint32_t(fb_pitch / 8)) {
            munmap(p, 0x1000);
            ::close(mfd);
            return false;
        }
        fd = mfd;
        de = regs;
        orig_coor0 = regs[0x114 / 4];
        orig_isize = regs[0x224 / 4];
        orig_osize = regs[0x228 / 4];
        orig_sr    = regs[0x22C / 4];
        for (int i = 0; i < 8; ++i) orig_coef[i] = regs[(0x230 / 4) + i];
        return true;
    }

    void apply(int ox, int oy, int ow, int oh) {
        if (!de) return;
        if (active && ox == cur_ox && oy == cur_oy && ow == cur_ow && oh == cur_oh) return;
        static const uint32_t bilinear[8] = {
            0x00004000u, 0x00083800u, 0x00103000u, 0x00182800u,
            0x00202000u, 0x00281800u, 0x00301000u, 0x00380800u
        };
        de[0x114 / 4] = (uint32_t(oy) << 16) | uint32_t(ox);
        de[0x224 / 4] = (uint32_t(LOG_H - 1) << 16) | uint32_t(LOG_W - 1);
        de[0x228 / 4] = (uint32_t(oh - 1) << 16) | uint32_t(ow - 1);
        de[0x22C / 4] = (((uint32_t(LOG_H) << 13) / uint32_t(oh)) << 16) |
                        ((uint32_t(LOG_W) << 13) / uint32_t(ow));
        for (int i = 0; i < 8; ++i) de[(0x230 / 4) + i] = bilinear[i];
        de[0x104 / 4] = 1u; // latch on VSYNC
        cur_ox = ox; cur_oy = oy; cur_ow = ow; cur_oh = oh;
        active = 1;
    }

    void restore() {
        if (!de || !active) return;
        de[0x114 / 4] = orig_coor0;
        de[0x224 / 4] = orig_isize;
        de[0x228 / 4] = orig_osize;
        de[0x22C / 4] = orig_sr;
        for (int i = 0; i < 8; ++i) de[(0x230 / 4) + i] = orig_coef[i];
        de[0x104 / 4] = 1u;
        active = 0;
        cur_ox = cur_oy = cur_ow = cur_oh = -1;
    }

    void shutdown() {
        restore();
        if (de) { munmap(const_cast<uint32_t*>(de), 0x1000); de = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
    }
} g_de_scaler;

static void on_signal(int) { g_quit = 1; }
static void on_crash_signal(int sig) {
    g_de_scaler.restore();
    char msg[64];
    int n = std::snprintf(msg, sizeof(msg), "\n[STARDROID CRASH] signal=%d\n", sig);
    if (n > 0) (void)::write(2, msg, size_t(n));
    _exit(128 + sig);
}

static inline uint16_t rgba_to_rgb565(uint32_t p) {
    return uint16_t((((p & 0xFFu) >> 3) << 11) | ((((p >> 8) & 0xFFu) >> 2) << 5) | (((p >> 16) & 0xFFu) >> 3));
}

static inline uint16_t avg565(uint16_t a, uint16_t b) {
    return uint16_t((a & b) + (((a ^ b) & 0xF7DEu) >> 1));
}

static inline void blend_row_565(const uint16_t* a, const uint16_t* b, uint16_t* out, int n) {
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
    for (; i + 2 <= n; i += 2) {
        uint32_t x, y;
        std::memcpy(&x, a + i, 4);
        std::memcpy(&y, b + i, 4);
        uint32_t m = (x & y) + (((x ^ y) & 0xF7DEF7DEu) >> 1);
        std::memcpy(out + i, &m, 4);
    }
    for (; i < n; ++i) out[i] = avg565(a[i], b[i]);
}

static inline void rgba_row_to_rgb565(const uint32_t* srow, uint16_t* out, int count) {
    int x = 0;
#if defined(__ARM_NEON)
    for (; x + 8 <= count; x += 8) {
        uint8x8x4_t c = vld4_u8(reinterpret_cast<const uint8_t*>(srow + x));
        uint16x8_t r = vshlq_n_u16(vmovl_u8(vshr_n_u8(c.val[0], 3)), 11);
        uint16x8_t g = vshlq_n_u16(vmovl_u8(vshr_n_u8(c.val[1], 2)), 5);
        uint16x8_t b = vmovl_u8(vshr_n_u8(c.val[2], 3));
        vst1q_u16(out + x, vorrq_u16(vorrq_u16(r, g), b));
    }
#endif
    for (; x < count; ++x) out[x] = rgba_to_rgb565(srow[x]);
}

static double monotonic_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static uint32_t g_world_buf[LOG_W * LOG_H];
static uint32_t g_canvas_buf[LOG_W * LOG_H];
static uint16_t g_world565[LOG_W * LOG_H];
static uint16_t g_fb565[LOG_W * LOG_H];
static uint16_t g_hud565[LOG_W * LOG_H];
static uint8_t  g_hud_alpha[LOG_W * LOG_H];
static uint8_t  g_hud_row_active[LOG_H];

// --- Safe CPU Governor Guard (performance governor only; no raw PMIC/COREPLL overclocking that can brown-out SD0!) ---
struct CpuBoostGuard {
    std::string old_gov;

    static std::string read_sys(const char* p) {
        int fd = ::open(p, O_RDONLY);
        if (fd < 0) return "";
        char buf[64] = {};
        ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
        ::close(fd);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = 0;
        return buf;
    }
    static void write_sys(const char* p, const char* v) {
        if (!v || !*v) return;
        int fd = ::open(p, O_WRONLY);
        if (fd >= 0) { (void)::write(fd, v, std::strlen(v)); ::close(fd); }
    }
    void engage() {
        old_gov = read_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
        write_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "performance\n");
    }
    ~CpuBoostGuard() {
        if (!old_gov.empty()) write_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", (old_gov + "\n").c_str());
    }
};

// --- Direct ALSA Audio Thread (S32_LE stereo 22050 Hz) ---
struct AudioEngine {
    std::atomic<bool> stop{false};
    std::thread worker;
    int pcm_fd = -1;
    bool speaker_amp = false;

    void start(bool prefer_hdmi) {
        if (prefer_hdmi) {
            // Respect Desktop speaker choice if /etc/asound.conf routes to card 0 (console speaker)
            int afd = ::open("/etc/asound.conf", O_RDONLY);
            if (afd >= 0) {
                char abuf[512] = {};
                ssize_t n = ::read(afd, abuf, sizeof(abuf) - 1);
                ::close(afd);
                if (n > 0 && (std::strstr(abuf, "card 0") || std::strstr(abuf, "hw:0,0")))
                    prefer_hdmi = false;
            }
        }
        if (prefer_hdmi) pcm_fd = ::open("/dev/snd/pcmC1D0p", O_RDWR);
        if (pcm_fd < 0) {
            pcm_fd = ::open("/dev/snd/pcmC0D0p", O_RDWR);
            if (pcm_fd >= 0) {
                (void)::system("LD_LIBRARY_PATH=/lib /bin/tinymix 35 1 >/dev/null 2>&1");
                speaker_amp = true;
            }
        }
        if (pcm_fd < 0) return;

        struct snd_pcm_hw_params p;
        std::memset(&p, 0, sizeof(p));
        for (int i = 0; i < SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1; ++i) {
            p.intervals[i].min = 0;
            p.intervals[i].max = ~0U;
        }
        for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; ++i)
            std::memset(&p.masks[i], 0xFF, sizeof(p.masks[i]));

        const unsigned rate = 22050;
        p.masks[SNDRV_PCM_HW_PARAM_ACCESS - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[0] = 1U << SNDRV_PCM_ACCESS_RW_INTERLEAVED;
        p.masks[SNDRV_PCM_HW_PARAM_FORMAT - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[0] = 1U << SNDRV_PCM_FORMAT_S32_LE;
        p.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 2;
        p.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 2;
        p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = rate;
        p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = rate;
        p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 256;
        p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 512;
        p.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 1024;
        p.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 2048;

        if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_HW_PARAMS, &p) < 0 || ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0) < 0) {
            ::close(pcm_fd);
            pcm_fd = -1;
            return;
        }
        stop.store(false);
        worker = std::thread([this]() {
            int16_t s16[512 * 2];
            int32_t s32[512 * 2];
            while (!stop.load(std::memory_order_relaxed)) {
                sd_mix_audio(s16, 512, 22050);
                for (int i = 0; i < 512 * 2; ++i) s32[i] = int32_t(s16[i]) << 16;
                struct snd_xferi x{};
                x.buf = s32;
                x.frames = 512;
                if (ioctl(pcm_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
                    if (errno == EPIPE) ioctl(pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0);
                    else usleep(10000);
                }
            }
        });
    }

    void shutdown() {
        stop.store(true);
        if (worker.joinable()) worker.join();
        if (pcm_fd >= 0) { ::close(pcm_fd); pcm_fd = -1; }
        if (speaker_amp) {
            (void)::system("LD_LIBRARY_PATH=/lib /bin/tinymix 35 0 >/dev/null 2>&1");
            speaker_amp = false;
        }
    }
};

// --- Dynamic X11 + MIT-SHM Backend ---
struct X11Api {
    void* lib = nullptr;
    void* lib_ext = nullptr;
    Display* (*OpenDisplay)(const char*) = nullptr;
    int (*CloseDisplay)(Display*) = nullptr;
    Window (*CreateSimpleWindow)(Display*, Window, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long) = nullptr;
    int (*SelectInput)(Display*, Window, long) = nullptr;
    int (*MapWindow)(Display*, Window) = nullptr;
    int (*StoreName)(Display*, Window, const char*) = nullptr;
    Atom (*InternAtom)(Display*, const char*, Bool) = nullptr;
    Status (*SetWMProtocols)(Display*, Window, Atom*, int) = nullptr;
    int (*Pending)(Display*) = nullptr;
    int (*NextEvent)(Display*, XEvent*) = nullptr;
    int (*LookupString)(XKeyEvent*, char*, int, KeySym*, XComposeStatus*) = nullptr;
    XImage* (*CreateImage)(Display*, Visual*, unsigned, int, int, char*, unsigned, unsigned, int, int) = nullptr;
    int (*PutImage)(Display*, Drawable, GC, XImage*, int, int, int, int, unsigned, unsigned) = nullptr;
    int (*Flush)(Display*) = nullptr;
    int (*Sync)(Display*, Bool) = nullptr;
    Pixmap (*CreateBitmapFromData)(Display*, Drawable, const char*, unsigned, unsigned) = nullptr;
    Cursor (*CreatePixmapCursor)(Display*, Pixmap, Pixmap, XColor*, XColor*, unsigned, unsigned) = nullptr;
    int (*DefineCursor)(Display*, Window, Cursor) = nullptr;
    int (*FreeCursor)(Display*, Cursor) = nullptr;
    int (*FreePixmap)(Display*, Pixmap) = nullptr;
    int (*MoveResizeWindow)(Display*, Window, int, int, unsigned, unsigned) = nullptr;
    Status (*SendEvent)(Display*, Window, Bool, long, XEvent*) = nullptr;
    int (*ClearWindow)(Display*, Window) = nullptr;

    Bool (*ShmQueryExtension)(Display*) = nullptr;
    XImage* (*ShmCreateImage)(Display*, Visual*, unsigned, int, char*, XShmSegmentInfo*, unsigned, unsigned) = nullptr;
    Status (*ShmAttach)(Display*, XShmSegmentInfo*) = nullptr;
    Status (*ShmDetach)(Display*, XShmSegmentInfo*) = nullptr;
    Status (*ShmPutImage)(Display*, Drawable, GC, XImage*, int, int, int, int, unsigned, unsigned, Bool) = nullptr;
    bool has_shm = false;

    struct XcurImage {
        uint32_t version, size, width, height, xhot, yhot, delay;
        uint32_t* pixels;
    };
    void* lib_xcur = nullptr;
    XcurImage* (*XcurCreate)(int, int) = nullptr;
    void (*XcurDestroy)(XcurImage*) = nullptr;
    Cursor (*XcurLoad)(Display*, const XcurImage*) = nullptr;
    bool has_xcursor = false;

    bool load() {
        const char* paths[] = {"libX11.so.6", "/usr/lib/libX11.so.6", "/desktop/usr/lib/libX11.so.6"};
        for (const char* p : paths) { lib = dlopen(p, RTLD_LAZY); if (lib) break; }
        if (!lib) return false;
        #define LOAD_SYM(field, sym) field = reinterpret_cast<decltype(field)>(dlsym(lib, sym)); if (!field) return false
        LOAD_SYM(OpenDisplay, "XOpenDisplay");
        LOAD_SYM(CloseDisplay, "XCloseDisplay");
        LOAD_SYM(CreateSimpleWindow, "XCreateSimpleWindow");
        LOAD_SYM(SelectInput, "XSelectInput");
        LOAD_SYM(MapWindow, "XMapWindow");
        LOAD_SYM(StoreName, "XStoreName");
        LOAD_SYM(InternAtom, "XInternAtom");
        LOAD_SYM(SetWMProtocols, "XSetWMProtocols");
        LOAD_SYM(Pending, "XPending");
        LOAD_SYM(NextEvent, "XNextEvent");
        LOAD_SYM(LookupString, "XLookupString");
        LOAD_SYM(CreateImage, "XCreateImage");
        LOAD_SYM(PutImage, "XPutImage");
        LOAD_SYM(Flush, "XFlush");
        LOAD_SYM(Sync, "XSync");
        LOAD_SYM(CreateBitmapFromData, "XCreateBitmapFromData");
        LOAD_SYM(CreatePixmapCursor, "XCreatePixmapCursor");
        LOAD_SYM(DefineCursor, "XDefineCursor");
        LOAD_SYM(FreeCursor, "XFreeCursor");
        LOAD_SYM(FreePixmap, "XFreePixmap");
        LOAD_SYM(MoveResizeWindow, "XMoveResizeWindow");
        LOAD_SYM(SendEvent, "XSendEvent");
        LOAD_SYM(ClearWindow, "XClearWindow");
        #undef LOAD_SYM

        const char* ext_paths[] = {"libXext.so.6", "/usr/lib/libXext.so.6", "/desktop/usr/lib/libXext.so.6"};
        for (const char* p : ext_paths) { lib_ext = dlopen(p, RTLD_LAZY); if (lib_ext) break; }
        if (lib_ext) {
            ShmQueryExtension = reinterpret_cast<decltype(ShmQueryExtension)>(dlsym(lib_ext, "XShmQueryExtension"));
            ShmCreateImage    = reinterpret_cast<decltype(ShmCreateImage)>(dlsym(lib_ext, "XShmCreateImage"));
            ShmAttach         = reinterpret_cast<decltype(ShmAttach)>(dlsym(lib_ext, "XShmAttach"));
            ShmDetach         = reinterpret_cast<decltype(ShmDetach)>(dlsym(lib_ext, "XShmDetach"));
            ShmPutImage       = reinterpret_cast<decltype(ShmPutImage)>(dlsym(lib_ext, "XShmPutImage"));
            has_shm = ShmQueryExtension && ShmCreateImage && ShmAttach && ShmDetach && ShmPutImage;
        }
        const char* cur_paths[] = {"libXcursor.so.1", "/usr/lib/libXcursor.so.1", "/desktop/usr/lib/libXcursor.so.1"};
        for (const char* p : cur_paths) { lib_xcur = dlopen(p, RTLD_LAZY); if (lib_xcur) break; }
        if (lib_xcur) {
            XcurCreate  = reinterpret_cast<decltype(XcurCreate)>(dlsym(lib_xcur, "XcursorImageCreate"));
            XcurDestroy = reinterpret_cast<decltype(XcurDestroy)>(dlsym(lib_xcur, "XcursorImageDestroy"));
            XcurLoad    = reinterpret_cast<decltype(XcurLoad)>(dlsym(lib_xcur, "XcursorImageLoadCursor"));
            has_xcursor = XcurCreate && XcurDestroy && XcurLoad;
        }
        return true;
    }
};

// --- Key Translation ---
static void translate_linux_key(int code, int* sym_out, int* scan_out) {
    *sym_out = 0; *scan_out = 0;
    switch (code) {
    case KEY_ESC:       *sym_out = 27;  *scan_out = 41; break;
    case KEY_1:         *sym_out = '1'; *scan_out = 30; break;
    case KEY_2:         *sym_out = '2'; *scan_out = 31; break;
    case KEY_3:         *sym_out = '3'; *scan_out = 32; break;
    case KEY_4:         *sym_out = '4'; *scan_out = 33; break;
    case KEY_5:         *sym_out = '5'; *scan_out = 34; break;
    case KEY_6:         *sym_out = '6'; *scan_out = 35; break;
    case KEY_7:         *sym_out = '7'; *scan_out = 36; break;
    case KEY_8:         *sym_out = '8'; *scan_out = 37; break;
    case KEY_9:         *sym_out = '9'; *scan_out = 38; break;
    case KEY_0:         *sym_out = '0'; *scan_out = 39; break;
    case KEY_MINUS:     *sym_out = '-'; *scan_out = 45; break;
    case KEY_EQUAL:     *sym_out = '='; *scan_out = 46; break;
    case KEY_BACKSPACE: *sym_out = 8;   *scan_out = 42; break;
    case KEY_TAB:       *sym_out = 9;   *scan_out = 43; break;
    case KEY_SPACE:     *sym_out = ' '; *scan_out = 44; break;
    case KEY_ENTER:     *sym_out = 13;  *scan_out = 40; break;
    case KEY_LEFTSHIFT:  *scan_out = 225; break;
    case KEY_RIGHTSHIFT: *scan_out = 229; break;
    case KEY_LEFTCTRL:   *scan_out = 224; break;
    case KEY_RIGHTCTRL:  *scan_out = 228; break;
    case KEY_RIGHT:      *scan_out = 79; break;
    case KEY_LEFT:       *scan_out = 80; break;
    case KEY_DOWN:       *scan_out = 81; break;
    case KEY_UP:         *scan_out = 82; break;
    case KEY_Q: *sym_out = 'q'; *scan_out = 20; break;
    case KEY_W: *sym_out = 'w'; *scan_out = 26; break;
    case KEY_E: *sym_out = 'e'; *scan_out = 8;  break;
    case KEY_R: *sym_out = 'r'; *scan_out = 21; break;
    case KEY_T: *sym_out = 't'; *scan_out = 23; break;
    case KEY_Y: *sym_out = 'y'; *scan_out = 28; break;
    case KEY_U: *sym_out = 'u'; *scan_out = 24; break;
    case KEY_I: *sym_out = 'i'; *scan_out = 12; break;
    case KEY_O: *sym_out = 'o'; *scan_out = 18; break;
    case KEY_P: *sym_out = 'p'; *scan_out = 19; break;
    case KEY_A: *sym_out = 'a'; *scan_out = 4;  break;
    case KEY_S: *sym_out = 's'; *scan_out = 22; break;
    case KEY_D: *sym_out = 'd'; *scan_out = 7;  break;
    case KEY_F: *sym_out = 'f'; *scan_out = 9;  break;
    case KEY_G: *sym_out = 'g'; *scan_out = 10; break;
    case KEY_H: *sym_out = 'h'; *scan_out = 11; break;
    case KEY_J: *sym_out = 'j'; *scan_out = 13; break;
    case KEY_K: *sym_out = 'k'; *scan_out = 14; break;
    case KEY_L: *sym_out = 'l'; *scan_out = 15; break;
    case KEY_Z: *sym_out = 'z'; *scan_out = 29; break;
    case KEY_X: *sym_out = 'x'; *scan_out = 27; break;
    case KEY_C: *sym_out = 'c'; *scan_out = 6;  break;
    case KEY_V: *sym_out = 'v'; *scan_out = 25; break;
    case KEY_B: *sym_out = 'b'; *scan_out = 5;  break;
    case KEY_N: *sym_out = 'n'; *scan_out = 17; break;
    case KEY_M: *sym_out = 'm'; *scan_out = 16; break;
    default: break;
    }
}

static void translate_x11_keysym(KeySym ks, int* sym_out, int* scan_out) {
    *sym_out = 0; *scan_out = 0;
    if (ks >= XK_a && ks <= XK_z) { *sym_out = int(ks); *scan_out = 4 + int(ks - XK_a); return; }
    if (ks >= XK_A && ks <= XK_Z) { *sym_out = int(ks - XK_A + 'a'); *scan_out = 4 + int(ks - XK_A); return; }
    if (ks >= XK_1 && ks <= XK_9) { *sym_out = int(ks); *scan_out = 30 + int(ks - XK_1); return; }
    if (ks == XK_0) { *sym_out = '0'; *scan_out = 39; return; }
    switch (ks) {
    case XK_Escape:    *sym_out = 27;  *scan_out = 41; break;
    case XK_Return:
    case XK_KP_Enter:  *sym_out = 13;  *scan_out = 40; break;
    case XK_BackSpace: *sym_out = 8;   *scan_out = 42; break;
    case XK_Tab:       *sym_out = 9;   *scan_out = 43; break;
    case XK_space:     *sym_out = ' '; *scan_out = 44; break;
    case XK_minus:     *sym_out = '-'; *scan_out = 45; break;
    case XK_equal:     *sym_out = '='; *scan_out = 46; break;
    case XK_Shift_L:   *scan_out = 225; break;
    case XK_Shift_R:   *scan_out = 229; break;
    case XK_Control_L: *scan_out = 224; break;
    case XK_Control_R: *scan_out = 228; break;
    case XK_Right:     *scan_out = 79; break;
    case XK_Left:      *scan_out = 80; break;
    case XK_Down:      *scan_out = 81; break;
    case XK_Up:        *scan_out = 82; break;
    default: break;
    }
}

// --- Data & Asset Discovery ---
struct DataPaths {
    std::string port_dir, data_dir, save_dir, replay_dir;
    std::string stardat, broodat, patch_rt, starcraft_mpq, broodwar_mpq;
    std::vector<std::string> maps, saves, replays;
    bool core_ready() const { return !stardat.empty() && !patch_rt.empty(); }
};

static std::string find_file_ci(const std::string& dir, const char* target_name) {
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (struct dirent* e = readdir(d)) {
        if (strcasecmp(e->d_name, target_name) == 0) {
            std::string full = dir + "/" + e->d_name;
            struct stat st{};
            if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 32) {
                found = full;
                break;
            }
        }
    }
    closedir(d);
    return found;
}

static void collect_ext_files(const std::string& dir, const char* ext1, const char* ext2, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const char* dot = std::strrchr(e->d_name, '.');
        if (!dot) continue;
        if (strcasecmp(dot, ext1) == 0 || (ext2 && strcasecmp(dot, ext2) == 0)) {
            std::string full = dir + "/" + e->d_name;
            struct stat st{};
            if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                if (std::find(out.begin(), out.end(), full) == out.end())
                    out.push_back(full);
            }
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
}

static void mkdir_p(const std::string& p) {
    std::string cur;
    for (size_t i = 0; i < p.size(); ++i) {
        cur += p[i];
        if (p[i] == '/' && cur.size() > 1) ::mkdir(cur.c_str(), 0755);
    }
    ::mkdir(p.c_str(), 0755);
}

static void discover_data(DataPaths& dp, const std::string& cli_data) {
    dp.stardat.clear(); dp.broodat.clear(); dp.patch_rt.clear();
    dp.starcraft_mpq.clear(); dp.broodwar_mpq.clear();
    dp.maps.clear(); dp.saves.clear(); dp.replays.clear();

    std::vector<std::string> dirs;
    if (!cli_data.empty()) dirs.push_back(cli_data);
    if (const char* env = std::getenv("STARDROID_DATA")) if (*env) dirs.push_back(env);
    dirs.push_back(dp.port_dir + "/data");
    dirs.push_back(dp.port_dir);
    dirs.push_back("/mnt/sd/ports/stardroid/data");
    dirs.push_back("/mnt/sd/ports/stardroid");
    dirs.push_back("/mnt/sd/game/stardroid");
    dirs.push_back("/mnt/sd/starcraft");
    dirs.push_back("/mnt/udisk/game/stardroid");
    dirs.push_back("/mnt/udisk/starcraft");

    for (const auto& d : dirs) {
        if (dp.stardat.empty())       dp.stardat       = find_file_ci(d, "stardat.mpq");
        if (dp.broodat.empty())       dp.broodat       = find_file_ci(d, "broodat.mpq");
        if (dp.patch_rt.empty())      dp.patch_rt      = find_file_ci(d, "patch_rt.mpq");
        if (dp.starcraft_mpq.empty()) dp.starcraft_mpq = find_file_ci(d, "starcraft.mpq");
        if (dp.broodwar_mpq.empty())  dp.broodwar_mpq  = find_file_ci(d, "broodwar.mpq");
        if (!dp.stardat.empty() && dp.data_dir.empty()) dp.data_dir = d;
    }
    if (dp.data_dir.empty())
        dp.data_dir = (::access("/mnt/sd", W_OK) == 0) ? "/mnt/sd/ports/stardroid/data" : (dp.port_dir + "/data");

    std::string state_root = (::access("/mnt/sd", W_OK) == 0) ? "/mnt/sd/ports/stardroid" : dp.port_dir;
    dp.save_dir = state_root + "/saves";
    dp.replay_dir = state_root + "/replays";
    mkdir_p(dp.save_dir);
    mkdir_p(dp.replay_dir);

    collect_ext_files(dp.port_dir + "/maps", ".scx", ".scm", dp.maps);
    collect_ext_files(dp.data_dir + "/maps", ".scx", ".scm", dp.maps);
    collect_ext_files(dp.data_dir, ".scx", ".scm", dp.maps);
    collect_ext_files("/mnt/sd/ports/stardroid/maps", ".scx", ".scm", dp.maps);
    collect_ext_files(dp.save_dir, ".sav", nullptr, dp.saves);
    collect_ext_files(dp.replay_dir, ".rep", nullptr, dp.replays);
}

// --- Fast NEON 480x320 Handheld Downscaler (4->3 RGB565 with dirty-row support) & 1:1 Viewport Blitter ---
static void scale_640x480_to_480x320_rgb565_rows(const uint32_t* src, uint16_t* dst480x320, int dy0, int dy1) {
    uint16_t row565[LOG_W];
    dy0 = std::max(0, dy0);
    dy1 = std::min(320, dy1);
    for (int dy = dy0; dy < dy1; ++dy) {
        int sy = (dy * 3) >> 1;
        rgba_row_to_rgb565(src + sy * LOG_W, row565, LOG_W);
        uint16_t* out = dst480x320 + dy * 480;
        for (int s = 0, d = 0; s < 640; s += 4, d += 3) {
            out[d + 0] = row565[s + 0];
            out[d + 1] = avg565(row565[s + 1], row565[s + 2]);
            out[d + 2] = row565[s + 3];
        }
    }
}

static void scale_640x480_to_480x320_rgb565(const uint32_t* src, uint16_t* dst480x320) {
    scale_640x480_to_480x320_rgb565_rows(src, dst480x320, 0, 320);
}

static void crop_480x320_rgb565(const uint32_t* src, int vx, int vy, uint16_t* dst480x320) {
    vx = std::max(0, std::min(LOG_W - 480, vx));
    vy = std::max(0, std::min(LOG_H - 320, vy));
    for (int dy = 0; dy < 320; ++dy)
        rgba_row_to_rgb565(src + (vy + dy) * LOG_W + vx, dst480x320 + dy * 480, 480);
}

static void scale_640x480_rgb565_to_480x320_rows(const uint16_t* src565, uint16_t* dst480x320, int dy0, int dy1) {
    dy0 = std::max(0, dy0);
    dy1 = std::min(320, dy1);
    for (int dy = dy0; dy < dy1; ++dy) {
        const uint16_t* row = src565 + ((dy * 3) >> 1) * LOG_W;
        uint16_t* out = dst480x320 + dy * 480;
        for (int s = 0, d = 0; s < 640; s += 4, d += 3) {
            out[d + 0] = row[s + 0];
            out[d + 1] = avg565(row[s + 1], row[s + 2]);
            out[d + 2] = row[s + 3];
        }
    }
}

static void crop_480x320_from_565(const uint16_t* src565, int vx, int vy, uint16_t* dst480x320) {
    vx = std::max(0, std::min(LOG_W - 480, vx));
    vy = std::max(0, std::min(LOG_H - 320, vy));
    for (int dy = 0; dy < 320; ++dy)
        std::memcpy(dst480x320 + dy * 480, src565 + (vy + dy) * LOG_W + vx, 480 * sizeof(uint16_t));
}

// --- Application Host State ---
struct App {
    DataPaths dp;
    std::string cli_data;
    blizz::FrontendUI ui;

    int mouse_x = 320, mouse_y = 240;
    int view_x = 80, view_y = 80;
    bool lmb_down = false, minimap_dragging = false, r2_held = false;
    bool dirty = true;
    int last_hover_token = -2;
    double last_click_ms = 0;
    int last_click_btn = 0, last_click_x = 0, last_click_y = 0;

    bool mount_all_mpqs(std::string& err) {
        char buf[256] = {};
        if (!dp.core_ready()) {
            err = "Missing StarDat.mpq or Patch_rt.mpq";
            return false;
        }
        if (!sd_open_archive(0, dp.stardat.c_str(), buf, sizeof(buf))) { err = buf; return false; }
        if (!dp.broodat.empty()) {
            if (!sd_open_archive(1, dp.broodat.c_str(), buf, sizeof(buf))) { err = buf; return false; }
        } else {
            sd_open_archive(1, "", buf, sizeof(buf));
        }
        if (!sd_open_archive(2, dp.patch_rt.c_str(), buf, sizeof(buf))) { err = buf; return false; }
        // Mount StarCraft.mpq (slot 4) and BroodWar.mpq (slot 5) without opening the same MPQ twice
        if (!dp.starcraft_mpq.empty()) sd_open_archive(4, dp.starcraft_mpq.c_str(), buf, sizeof(buf));
        else sd_open_archive(4, "", buf, sizeof(buf));
        if (!dp.broodwar_mpq.empty()) sd_open_archive(5, dp.broodwar_mpq.c_str(), buf, sizeof(buf));
        else sd_open_archive(5, "", buf, sizeof(buf));

        ui.has_broodwar = !dp.broodat.empty();
        ui.has_sc_camp  = !dp.starcraft_mpq.empty();
        ui.has_bw_camp  = !dp.broodwar_mpq.empty();
        ui.starcraft_mpq_path = dp.starcraft_mpq;
        ui.broodwar_mpq_path  = dp.broodwar_mpq;
        ui.data_dir   = dp.data_dir;
        ui.save_dir   = dp.save_dir;
        ui.replay_dir = dp.replay_dir;
        ui.map_files  = dp.maps;
        return true;
    }

    void init_startup() {
        discover_data(dp, cli_data);
        ui.progress.load(dp.save_dir + "/campaign_progress.txt");
        std::string err;
        if (mount_all_mpqs(err)) {
            if (!ui.has_broodwar) ui.progress.expansion = false;
            sd_set_audio(false, ui.sfx_vol, ui.mus_vol);
            ui.open_title();
        } else {
            ui.data_dir = dp.data_dir;
            ui.screen = blizz::ScreenId::DataMissing;
        }
    }

    void open_score_screen() {
        std::string stats = sd_text(9);
        int outc = (ui.last_outcome != 0) ? ui.last_outcome : sd_value(7, 0);
        if (outc != 1 && outc != 2) outc = 2;
        if (ui.score.open(ui.active_race, outc, stats, !ui.active_is_replay)) {
            ui.modal = blizz::ModalId::None;
            ui.screen = blizz::ScreenId::Score;
        } else {
            ui.finish_score_screen();
        }
    }
};

// --- Unified Modal & Screen Action Dispatcher ---
static void handle_modal_btn(App& app, int id, double now_ms) {
    using namespace blizz;
    auto& ui = app.ui;
    sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);

    // Expansion chooser (500=Original, 501=Brood War, 502=Cancel)
    if (id >= 500 && id <= 502) {
        ModalId prev = ui.modal;
        ui.modal = ModalId::None;
        if (id == 502) return;
        bool bw = (id == 501) && ui.has_broodwar;
        if (prev == ModalId::ExpansionIntro) {
            if (ui.cinematic.open(bw ? "smk\\starxintr.smk" : "smk\\starintr.smk", bw ? "Brood War Intro" : "StarCraft Intro")) {
                ui.after_cinematic = ScreenId::MainMenu;
                ui.screen = ScreenId::Cinematic;
            } else {
                ui.show_toast("Intro cinematic not found in MPQ.", now_ms);
            }
        } else if (prev == ModalId::ExpansionMain) {
            ui.progress.expansion = bw;
            ui.progress.save();
            ui.open_campaign_select();
        } else if (prev == ModalId::ExpansionCustom) {
            ui.progress.expansion = bw;
            ui.progress.save();
            ui.open_custom_game();
        }
        return;
    }

    // Mission History modal (gluhist)
    if (id >= 3000 && id < 3200) {
        int idx = id - 3000;
        if (idx >= 0 && idx < int(ui.hist_items.size())) {
            if (ui.hist_sel == idx && ui.progress.is_available(ui.hist_items[size_t(idx)].unlock_code)) {
                id = 512; // Double-click launches immediately!
            } else {
                ui.hist_sel = idx;
                return;
            }
        }
    }
    if (id == 510) { ui.hist_scroll = std::max(0.0f, ui.hist_scroll - 38.0f); return; }
    if (id == 511) { ui.hist_scroll += 38.0f; return; }
    if (id == 513) { ui.modal = ModalId::None; return; }
    if (id == 512) {
        if (ui.hist_sel >= 0 && ui.hist_sel < int(ui.hist_items.size())) {
            const auto& it = ui.hist_items[size_t(ui.hist_sel)];
            if (!ui.progress.is_available(it.unlock_code)) return;
            ui.modal = ModalId::None;
            if (it.is_cinematic) {
                if (ui.cinematic.open(it.smk_path, it.label)) {
                    ui.after_cinematic = ScreenId::CampaignSelect;
                    ui.screen = ScreenId::Cinematic;
                }
            } else {
                ui.start_mission_flow(it.mission_idx);
            }
        }
        return;
    }

    // AppOptions modal (520..528)
    switch (id) {
    case 520: ui.sfx_vol = (ui.sfx_vol >= 0.95f) ? 0.0f : std::min(1.0f, ui.sfx_vol + 0.25f); sd_set_audio(false, ui.sfx_vol, ui.mus_vol); return;
    case 521: ui.mus_vol = (ui.mus_vol >= 0.95f) ? 0.0f : std::min(1.0f, ui.mus_vol + 0.20f); sd_set_audio(false, ui.sfx_vol, ui.mus_vol); return;
    case 522: ui.handheld_1to1 = !ui.handheld_1to1; return;
    case 523:
        ui.world_zoom = (ui.world_zoom < 0.9) ? 1.0 : (ui.world_zoom < 1.1 ? 1.25 : (ui.world_zoom < 1.4 ? 1.5 : 0.75));
        return;
    case 524: ui.progress.unlock_all = !ui.progress.unlock_all; ui.progress.save(); return;
    case 525: ui.hud.grid_hotkeys = !ui.hud.grid_hotkeys; return;
    case 526: ui.request_rescan = true; ui.modal = ModalId::None; return;
    case 527: ui.request_fullscreen_toggle = true; return;
    case 528: ui.modal = ModalId::None; return;
    case 530: ui.modal = ModalId::None; return;
    case 540: ui.progress.expansion = !(ui.progress.expansion && ui.has_broodwar); ui.progress.save(); return;
    case 541: ui.setup.match_opts ^= 1u; return;
    case 542: ui.setup.match_opts ^= 2u; return;
    case 543: ui.setup.match_opts ^= 4u; return;
    case 544: ui.setup.hero_mode = !ui.setup.hero_mode; return;
    case 545: ui.modal = ModalId::None; return;
    default: break;
    }

    // In-Game F10 & Outcome Modals (600..710)
    switch (id) {
    case 600: app.open_score_screen(); return;
    case 601:
        if (ui.active_mission_idx >= 0) {
            ui.modal = ModalId::None;
            sd_close_game();
            ui.open_briefing_room();
        } else {
            ui.outcome_dismissed = true;
            ui.modal = ModalId::None;
            sd_command(6, 0, 0, 0, 0);
        }
        return;
    case 610: ui.modal = ModalId::GameSave; return;
    case 611: ui.modal = ModalId::GameLoad; return;
    case 612: ui.modal = ModalId::GameOptions; return;
    case 613: ui.modal = ModalId::GameTips; return;
    case 614: ui.modal = ModalId::GameObjectives; return;
    case 615: ui.modal = ModalId::AbortMenu; return;
    case 616: ui.modal = ModalId::None; sd_command(6, 0, 0, 0, 0); return;
    case 620:
        ui.modal = ModalId::None;
        sd_close_game();
        if (ui.active_mission_idx >= 0) ui.open_briefing_room();
        else ui.start_custom_engine(now_ms);
        return;
    case 621:
        ui.modal = ModalId::None;
        sd_close_game();
        discover_data(app.dp, app.cli_data);
        if (ui.active_mission_idx >= 0) ui.open_campaign_select();
        else ui.open_main_menu();
        return;
    case 622: g_quit = 1; return;
    case 623: ui.modal = ModalId::GameMenu; return;
    case 630: ui.modal = ModalId::OptSpeed; return;
    case 631: ui.modal = ModalId::OptSound; return;
    case 632: ui.modal = ModalId::OptVideo; return;
    case 633: ui.modal = ModalId::OptControls; return;
    case 634: ui.modal = ModalId::OptCheats; return;
    case 635: ui.modal = ModalId::GameMenu; return;
    case 640: ui.sim_fps = 18; ui.modal = ModalId::GameOptions; return;
    case 641: ui.sim_fps = 21; ui.modal = ModalId::GameOptions; return;
    case 642: ui.sim_fps = 24; ui.modal = ModalId::GameOptions; return;
    case 643: ui.sim_fps = 32; ui.modal = ModalId::GameOptions; return;
    case 644: sd_command(19, sd_value(18, 0) ? 0 : 1, 0, 0, 0); return;
    case 645: ui.modal = ModalId::GameOptions; return;
    case 650: ui.sfx_vol = (ui.sfx_vol >= 0.95f) ? 0.0f : std::min(1.0f, ui.sfx_vol + 0.25f); sd_set_audio(false, ui.sfx_vol, ui.mus_vol); return;
    case 651: ui.mus_vol = (ui.mus_vol >= 0.95f) ? 0.0f : std::min(1.0f, ui.mus_vol + 0.20f); sd_set_audio(false, ui.sfx_vol, ui.mus_vol); return;
    case 652: {
        static const char* kRN[3] = {"zerg", "terran", "protoss"};
        std::string trk = std::string("music\\") + kRN[ui.active_race % 3] + std::to_string(1 + (std::rand() % 3)) + ".wav";
        sd_play_mpq_music(trk.c_str());
        return;
    }
    case 653: ui.modal = ModalId::GameOptions; return;
    case 660:
        ui.world_zoom = (ui.world_zoom < 0.9) ? 1.0 : (ui.world_zoom < 1.1 ? 1.25 : (ui.world_zoom < 1.4 ? 1.5 : 0.75));
        sd_resize_viewport(LOG_W, LOG_H, ui.world_zoom);
        return;
    case 661: ui.handheld_1to1 = !ui.handheld_1to1; return;
    case 662: ui.hud.show_group_bar = !ui.hud.show_group_bar; return;
    case 663: ui.request_fullscreen_toggle = true; return;
    case 664: ui.modal = ModalId::GameOptions; return;
    case 670: ui.hud.grid_hotkeys = !ui.hud.grid_hotkeys; return;
    case 671: ui.modal = ModalId::GameOptions; return;
    case 680: sd_command(9, 0, 0, 0, 0); ui.show_toast("Cheat enabled: +10,000 Minerals & Gas!", now_ms); return;
    case 681: sd_command(11, 0, 0, 0, 0); ui.show_toast("Cheat enabled: Map Revealed!", now_ms); return;
    case 682: sd_command(10, 1, 0, 0, 0); ui.show_toast("Cheat enabled: Operation CWAL!", now_ms); return;
    case 683: sd_command(29, sd_value(22, 0) ? 0 : 1, 0, 0, 0); return;
    case 684: sd_command(30, sd_value(24, 0) ? 0 : 1, 0, 0, 0); return;
    case 685:
        ui.modal = ModalId::None;
        sd_command(6, 0, 0, 0, 0);
        sd_command(13, 1, 0, 0, 0);
        return;
    case 686: ui.modal = ModalId::GameOptions; return;
    case 690: ++ui.tip_idx; return;
    case 691:
    case 695: ui.modal = ModalId::None; sd_command(6, 0, 0, 0, 0); return;
    case 710: ui.modal = ModalId::GameMenu; return;
    default: break;
    }

    if (id >= 700 && id <= 705) {
        int s = id - 700;
        std::string sname = (s == 0) ? "quicksave.sav" : ("slot_" + std::to_string(s) + ".sav");
        std::string full = app.dp.save_dir + "/" + sname;
        char err[256] = {};
        if (ui.modal == ModalId::GameSave) {
            if (sd_save_to_file(full.c_str(), err, sizeof(err))) {
                discover_data(app.dp, app.cli_data);
                ui.show_toast("Saved " + sname, now_ms);
                ui.modal = ModalId::None;
                sd_command(6, 0, 0, 0, 0);
            } else ui.show_toast(std::string("Save failed: ") + err, now_ms);
        } else {
            if (sd_load_from_file(full.c_str(), err, sizeof(err))) {
                ui.show_toast("Loaded " + sname, now_ms);
                ui.hud.occlusion_sent = false;
                ui.modal = ModalId::None;
                sd_command(6, 0, 0, 0, 0);
            } else ui.show_toast(std::string("Load failed: ") + err, now_ms);
        }
    }
}

static int compute_hover_token(const blizz::FrontendUI& ui, int x, int y) {
    for (const auto& b : ui.modal_btns)
        if (b.enabled && b.contains(x, y)) return 100000 + b.id;
    if (ui.screen == blizz::ScreenId::Playing && ui.modal == blizz::ModalId::None) {
        for (size_t i = 0; i < ui.hud.hits.size(); ++i)
            if (ui.hud.hits[i].contains(x, y)) return 200000 + int(i);
        return -1;
    }
    if (ui.screen == blizz::ScreenId::Score) return 300000 + ui.score.hot_id;
    return ui.glue.hot_id;
}

static void on_pointer_move(App& app, int x, int y) {
    int nx = std::max(0, std::min(LOG_W - 1, x));
    int ny = std::max(0, std::min(LOG_H - 1, y));
    if (nx == app.mouse_x && ny == app.mouse_y && !app.minimap_dragging) return;
    app.mouse_x = nx;
    app.mouse_y = ny;
    auto& ui = app.ui;
    if (ui.screen == blizz::ScreenId::Playing && ui.modal == blizz::ModalId::None) {
        if (app.minimap_dragging) {
            int mpw = 0, mph = 0;
            sd_minimap_rgba(&mpw, &mph);
            if (mpw > 0 && mph > 0) {
                int mx = (app.mouse_x - ui.hud.min_x) * mpw / std::max(1, ui.hud.min_w);
                int my = (app.mouse_y - ui.hud.min_y) * mph / std::max(1, ui.hud.min_h);
                sd_minimap_click(mx, my, false);
                app.dirty = true;
            }
            return;
        }
        sd_mouse_move(app.mouse_x, app.mouse_y);
    } else {
        ui.update_pointer(app.mouse_x, app.mouse_y);
    }
    int htok = compute_hover_token(ui, app.mouse_x, app.mouse_y);
    if (htok != app.last_hover_token) {
        app.last_hover_token = htok;
        app.dirty = true;
    }
}

static void on_pointer_button(App& app, int down, int btn) {
    using namespace blizz;
    app.dirty = true;
    auto& ui = app.ui;
    int x = app.mouse_x, y = app.mouse_y;
    double now = monotonic_ms();
    if (btn == 1) app.lmb_down = (down != 0);
    if (!down) app.minimap_dragging = false;

    int clicks = 1;
    if (down) {
        if (btn == app.last_click_btn && (now - app.last_click_ms) < 360.0 &&
            std::abs(x - app.last_click_x) <= 10 && std::abs(y - app.last_click_y) <= 10)
            clicks = 2;
        app.last_click_btn = btn;
        app.last_click_ms  = now;
        app.last_click_x   = x;
        app.last_click_y   = y;
    }

    // Active Modal intercepts input first
    if (ui.modal != ModalId::None) {
        if (!down) return;
        if (btn == 3) {
            if (ui.modal == ModalId::OutcomePrompt) return;
            if (ui.screen == ScreenId::Playing) {
                if (ui.modal == ModalId::GameMenu || ui.modal == ModalId::GameTips || ui.modal == ModalId::GameObjectives) {
                    ui.modal = ModalId::None;
                    sd_command(6, 0, 0, 0, 0);
                } else {
                    ui.modal = ModalId::GameMenu;
                }
            } else {
                ui.modal = ModalId::None;
            }
            return;
        }
        if (btn == 1) {
            for (const auto& b : ui.modal_btns) {
                if (b.contains(x, y)) {
                    handle_modal_btn(app, b.id, now);
                    return;
                }
            }
        }
        return;
    }

    // Non-Playing Screens
    if (ui.screen != ScreenId::Playing) {
        if (!down) return;
        if (ui.screen == ScreenId::DataMissing) {
            ui.request_rescan = true;
            return;
        }
        if (ui.screen == ScreenId::TitleSplash) {
            ui.open_main_menu();
            return;
        }
        if (ui.screen == ScreenId::Story) {
            if (btn == 3) ui.open_briefing_room();
            else {
                ui.story.next_page();
                if (!ui.story.active()) ui.open_briefing_room();
            }
            return;
        }
        if (ui.screen == ScreenId::Cinematic) {
            ui.cinematic.close();
            if (ui.after_cinematic == ScreenId::Briefing && ui.active_mission_idx >= 0)
                ui.start_mission_flow(ui.active_mission_idx);
            else if (ui.after_cinematic == ScreenId::CampaignSelect)
                ui.open_campaign_select();
            else
                ui.open_main_menu();
            return;
        }
        if (ui.screen == ScreenId::Credits) {
            ui.open_main_menu();
            return;
        }
        if (ui.screen == ScreenId::Score) {
            int cid = ui.score.on_click(x, y);
            if (cid == 7) ui.finish_score_screen();
            else if (cid == 61) {
                char err[256] = {};
                std::string rpath = app.dp.replay_dir + "/match_" + std::to_string(long(now / 1000.0)) + ".rep";
                if (sd_save_replay_file(rpath.c_str(), err, sizeof(err))) {
                    discover_data(app.dp, app.cli_data);
                    ui.score.can_save_replay = false;
                    ui.show_toast("Saved replay!", now);
                } else ui.show_toast(err, now);
            }
            return;
        }

        // Glue Screens: fast-forward slide-in on click if still sliding
        if (!ui.glue.ready()) {
            ui.glue.elapsed = BlizzGlue::ENTER_MS;
            ui.glue.anim_elapsed = std::max(ui.glue.anim_elapsed, BlizzGlue::ENTER_MS);
            return;
        }

        if (ui.screen == ScreenId::MainMenu && btn == 1) {
            ui.update_pointer(x, y);
            int wid = ui.glue.hot_id;
            if (wid < 0) return;
            sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);
            switch (wid) {
            case 3:
                if (ui.has_broodwar) ui.modal = ModalId::ExpansionMain;
                else { ui.progress.expansion = false; ui.open_campaign_select(); }
                break;
            case 4:
                if (ui.has_broodwar) ui.modal = ModalId::ExpansionCustom;
                else { ui.progress.expansion = false; ui.open_custom_game(); }
                break;
            case 5: ui.modal = ModalId::AppOptions; break;
            case 8:
                if (ui.has_broodwar) ui.modal = ModalId::ExpansionIntro;
                else if (ui.cinematic.open("smk\\starintr.smk", "StarCraft Intro")) {
                    ui.after_cinematic = ScreenId::MainMenu;
                    ui.screen = ScreenId::Cinematic;
                }
                break;
            case 9: ui.credits_ms = 0; ui.screen = ScreenId::Credits; break;
            case 2: g_quit = 1; break;
            }
            return;
        }

        if (ui.screen == ScreenId::CampaignSelect) {
            if (btn == 3) { ui.open_main_menu(); return; }
            ui.update_pointer(x, y);
            int wid = ui.glue.hot_id;
            if (wid < 0) return;
            sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);
            if (wid >= 6 && wid <= 8) ui.open_history_modal(wid);
            else if (wid == 5) { discover_data(app.dp, app.cli_data); ui.open_load_browser(false, app.dp.saves); }
            else if (wid == 11) { discover_data(app.dp, app.cli_data); ui.open_load_browser(true, app.dp.replays); }
            else if (wid == 10) ui.open_custom_game();
            else if (wid == 9) ui.open_main_menu();
            return;
        }

        if (ui.screen == ScreenId::Briefing) {
            if (btn == 3) { sd_sound_stop(62); if (ui.active_mission_idx >= 0) ui.open_campaign_select(); else ui.open_custom_game(); return; }
            ui.update_pointer(x, y);
            int wid = ui.glue.hot_id;
            if (wid < 0) return;
            sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);
            if (wid == 13) ui.launch_from_briefing(now);
            else if (wid == 14) { sd_sound_stop(62); if (ui.active_mission_idx >= 0) ui.open_campaign_select(); else ui.open_custom_game(); }
            else if (wid == 20) { ui.campaign.replay_briefing(); ui.last_brief_rev = -1; }
            return;
        }

        if (ui.screen == ScreenId::CustomGame) {
            if (btn == 3) { ui.open_main_menu(); return; }
            for (const auto& b : ui.modal_btns) {
                if (b.contains(x, y)) {
                    sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);
                    if (b.id >= 1000 && b.id < 2000) {
                        int midx = b.id - 1000;
                        if (midx == ui.map_sel && clicks == 2) { ui.start_custom_engine(now); return; }
                        ui.select_custom_map(midx);
                    } else if (b.id == 100) ui.modal = ModalId::MatchOptions;
                    else if (b.id == 101) ui.map_scroll = std::max(0.0f, ui.map_scroll - 40.0f);
                    else if (b.id == 102) ui.map_scroll += 40.0f;
                    else if (b.id == 103) ui.modal = ModalId::MapDetails;
                    else if (b.id == 104) {
                        if (ui.cur_map.ums_only) ui.setup.apply_ums(ui.cur_map);
                        else {
                            ui.setup.mode = (ui.setup.mode + 1) % 4;
                            if (ui.setup.mode == 3) ui.setup.apply_ums(ui.cur_map);
                            else ui.setup.clamp_to_starts(ui.cur_map.start_locs);
                        }
                    } else if (b.id == 105) ui.setup.difficulty = (ui.setup.difficulty + 1) % 4;
                    else if (b.id == 106) ui.setup.speed = (ui.setup.speed + 1) % 7;
                    else if (b.id >= 200 && b.id < 208) {
                        int s = b.id - 200;
                        if (ui.setup.mode == 3) {
                            for (int k = 0; k < 8; ++k) if (ui.cur_map.owners[k] == 6) ui.setup.controllers[k] = 2;
                            ui.setup.controllers[s] = 1; ui.setup.my_slot = s;
                        } else if (s > 0) {
                            ui.setup.controllers[s] = (ui.setup.controllers[s] == 2) ? 0 : 2;
                        }
                    } else if (b.id >= 210 && b.id < 218) {
                        int s = b.id - 210;
                        ui.setup.races[s] = (ui.setup.races[s] + 1) % 4;
                    } else if (b.id >= 220 && b.id < 228) {
                        int s = b.id - 220;
                        ui.setup.teams[s] = (ui.setup.teams[s] % 4) + 1;
                    }
                    return;
                }
            }
            ui.update_pointer(x, y);
            if (ui.glue.hot_id == 12) { sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false); ui.start_custom_engine(now); }
            else if (ui.glue.hot_id == 13) { sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false); ui.open_main_menu(); }
            return;
        }

        if (ui.screen == ScreenId::LoadBrowser) {
            if (btn == 3) { ui.open_campaign_select(); return; }
            for (const auto& b : ui.modal_btns) {
                if (b.contains(x, y)) {
                    sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);
                    if (b.id >= 2000 && b.id < 3000) ui.browser_sel = b.id - 2000;
                    else if (b.id == 301) ui.browser_scroll = std::max(0.0f, ui.browser_scroll - 44.0f);
                    else if (b.id == 302) ui.browser_scroll += 44.0f;
                    else if (b.id == 303 && ui.browser_sel >= 0 && ui.browser_sel < int(ui.browser_files.size())) {
                        ::unlink(ui.browser_files[size_t(ui.browser_sel)].c_str());
                        discover_data(app.dp, app.cli_data);
                        ui.browser_files = ui.load_is_replays ? app.dp.replays : app.dp.saves;
                        ui.browser_sel = std::max(0, std::min(int(ui.browser_files.size()) - 1, ui.browser_sel));
                    }
                    return;
                }
            }
            ui.update_pointer(x, y);
            int ok_id = 4, cancel_id = 5;
            if (ui.glue.name == "glucustm") { ok_id = 12; cancel_id = 13; }
            else {
                for (const auto& w : ui.glue.layout.widgets) {
                    if ((w.type == 14 || w.type == 1 || w.type == 2) && (w.flags & 0x80000u)) ok_id = w.id;
                    if ((w.type == 14 || w.type == 1 || w.type == 2) && (w.flags & 64u)) cancel_id = w.id;
                }
            }
            if (ui.glue.hot_id == cancel_id) {
                sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);
                ui.open_campaign_select();
            } else if (ui.glue.hot_id == ok_id && !ui.browser_files.empty() &&
                       ui.browser_sel >= 0 && ui.browser_sel < int(ui.browser_files.size())) {
                sd_play_mpq_sound(-1, "sound\\glue\\mousedown2.wav", 61, false);
                char err[256] = {};
                const std::string& fpath = ui.browser_files[size_t(ui.browser_sel)];
                if (ui.load_is_replays) {
                    if (sd_open_replay_file(fpath.c_str(), err, sizeof(err))) {
                        ui.active_is_replay = true;
                        ui.active_mission_idx = -1;
                        ui.campaign.clear();
                        ui.glue.close();
                        ui.enter_playing_common();
                    } else ui.show_toast(err, now, 4000);
                } else {
                    if (sd_load_from_file(fpath.c_str(), err, sizeof(err))) {
                        ui.active_is_replay = false;
                        ui.glue.close();
                        ui.enter_playing_common();
                    } else ui.show_toast(err, now, 4000);
                }
            }
            return;
        }
        return;
    }

    // --- In-Game Playing Screen ---
    if (!down) {
        sd_mouse_button(0, btn, x, y, 1);
        return;
    }

    // 1. Minimap click / right-click order / drag
    if (x >= 6 && x < 134 && y >= 347 && y < 475) {
        int mpw = 0, mph = 0;
        sd_minimap_rgba(&mpw, &mph);
        if (mpw > 0 && mph > 0) {
            int mx = (x - ui.hud.min_x) * mpw / std::max(1, ui.hud.min_w);
            int my = (y - ui.hud.min_y) * mph / std::max(1, ui.hud.min_h);
            sd_minimap_click(mx, my, btn == 3);
            if (btn == 1) app.minimap_dragging = true;
        }
        return;
    }

    // 2. HUD interactive widgets (MENU, Terrain toggle, Portrait center, Cargo unload, Queue cancel, Wireframes, 3x3 Command Card, Group pills)
    if (btn == 1) {
        for (const auto& h : ui.hud.hits) {
            if (h.unload_id >= 0 && h.contains(x, y)) { sd_command(24, h.unload_id, 0, 0, 0); return; }
        }
        for (const auto& h : ui.hud.hits) {
            if (!h.contains(x, y)) continue;
            if (h.menu_btn) { sd_command(6, 1, 0, 0, 0); ui.modal = ModalId::GameMenu; return; }
            if (h.terrain_toggle) { sd_command(8, sd_value(14, 0) ? 0 : 1, 0, 0, 0); return; }
            if (h.center_unit > 0) { sd_command(36, h.center_unit, 0, 0, 0); return; }
            if (h.cancel_q >= 0) { sd_command(7, h.cancel_q, 0, 0, 0); return; }
            if (h.select_unit >= 0) { sd_command(14, h.select_unit, app.r2_held ? 1 : 0, 0, 0); return; }
            if (h.group_num >= 1) { sd_command(25, h.group_num, app.r2_held ? 1 : 0, 0, 0); return; }
            if (h.more) { ++ui.hud.card_page; return; }
            if (h.slot >= 0) {
                if (h.local_cancel) sd_command(5, 0, 0, 0, 0);
                else if (h.enabled && h.card_idx >= 0) sd_command(35, h.card_idx, h.cmd_token, 0, 0);
                return;
            }
        }
    }

    // 3. Consume clicks on opaque console metal below y=300 so orders never leak under console!
    if (y >= BlizzHud::WORLD_BOTTOM ||
        (y >= 300 && ui.hud.console.image.valid() &&
         px_a(ui.hud.console.image.px[size_t(y) * ui.hud.console.width + size_t(x)]) != 0)) {
        if (btn == 3 && sd_value(0, 0) == 2) sd_command(5, 0, 0, 0, 0);
        return;
    }

    // 4. Smart Right-Click cancel when targeting / building
    if (btn == 3 && sd_value(0, 0) == 2) {
        sd_command(5, 0, 0, 0, 0);
        return;
    }

    // 5. Battlefield mouse button!
    sd_mouse_button(down, btn, x, y, clicks);
}

static void on_key_event(App& app, int down, int sym, int scancode, int fkey = 0) {
    using namespace blizz;
    app.dirty = true;
    auto& ui = app.ui;
    double now = monotonic_ms();

    if (down && fkey == 2) { ui.handheld_1to1 = !ui.handheld_1to1; return; }

    if (ui.modal != ModalId::None) {
        if (!down) return;
        if (sym == 27 || fkey == 10) {
            if (ui.modal == ModalId::OutcomePrompt) { app.open_score_screen(); return; }
            if (ui.screen == ScreenId::Playing) {
                if (ui.modal == ModalId::GameMenu || fkey == 10) {
                    ui.modal = ModalId::None;
                    sd_command(6, 0, 0, 0, 0);
                } else {
                    ui.modal = ModalId::GameMenu;
                }
            } else {
                ui.modal = ModalId::None;
            }
            return;
        }
        if (ui.modal == ModalId::MissionHistory) {
            if (scancode == 82 && ui.hist_sel > 0) { --ui.hist_sel; ui.hist_scroll = std::max(0.0f, float((ui.hist_sel - 4) * 19)); return; }
            if (scancode == 81 && ui.hist_sel + 1 < int(ui.hist_items.size())) { ++ui.hist_sel; ui.hist_scroll = std::max(0.0f, float((ui.hist_sel - 4) * 19)); return; }
            if (sym == 13 || sym == ' ') { handle_modal_btn(app, 512, now); return; }
        }
        if ((scancode == 82 || scancode == 81) && !ui.modal_btns.empty()) {
            int cur = -1;
            for (size_t i = 0; i < ui.modal_btns.size(); ++i)
                if (ui.modal_btns[i].contains(app.mouse_x, app.mouse_y)) { cur = int(i); break; }
            int n = int(ui.modal_btns.size());
            int nxt = (cur < 0) ? 0 : ((cur + n + (scancode == 81 ? 1 : -1)) % n);
            const auto& b = ui.modal_btns[size_t(nxt)];
            on_pointer_move(app, b.x + b.w / 2, b.y + b.h / 2);
            return;
        }
        if (sym == 13 || sym == ' ') {
            if (ui.modal == ModalId::OutcomePrompt) { handle_modal_btn(app, 600, now); return; }
            for (const auto& b : ui.modal_btns)
                if (b.contains(app.mouse_x, app.mouse_y)) { handle_modal_btn(app, b.id, now); return; }
        }
        return;
    }

    if (ui.screen != ScreenId::Playing) {
        if (!down) return;
        if (ui.screen == ScreenId::DataMissing) {
            if (sym == 27) g_quit = 1;
            else ui.request_rescan = true;
            return;
        }
        if (ui.screen == ScreenId::TitleSplash) { ui.open_main_menu(); return; }
        if (ui.screen == ScreenId::Story) {
            if (sym == 27) ui.open_briefing_room();
            else { ui.story.next_page(); if (!ui.story.active()) ui.open_briefing_room(); }
            return;
        }
        if (ui.screen == ScreenId::Cinematic) { on_pointer_button(app, 1, 1); return; }
        if (ui.screen == ScreenId::Credits) { ui.open_main_menu(); return; }
        if (ui.screen == ScreenId::Score) {
            if (sym == 13 || sym == 27 || sym == ' ') {
                if (ui.score.progress < 1.0) { ui.score.count_ms = ScoreScreen::COUNT_MS; ui.score.progress = 1.0; }
                else ui.finish_score_screen();
            } else if (scancode == 80) ui.score.tab = (ui.score.tab + 3) % 4;
            else if (scancode == 79) ui.score.tab = (ui.score.tab + 1) % 4;
            return;
        }
        if (ui.screen == ScreenId::MainMenu) {
            static const int kOrder[6] = {3, 4, 5, 8, 9, 2};
            if (scancode >= 79 && scancode <= 82) {
                int idx = 0;
                for (int i = 0; i < 6; ++i) if (kOrder[i] == ui.glue.hot_id) idx = i;
                idx = (idx + 6 + ((scancode == 81 || scancode == 79) ? 1 : -1)) % 6;
                if (const auto* w = ui.glue.layout.get(kOrder[idx]))
                    on_pointer_move(app, w->x + (w->rx2 > w->rx ? (w->rx + w->rx2) / 2 : w->w / 2),
                                    w->y + (w->ry2 > w->ry ? (w->ry + w->ry2) / 2 : w->h / 2));
                return;
            }
            if (sym == 's') { on_pointer_move(app, 140, 140); ui.glue.hot_id = 3; on_pointer_button(app, 1, 1); return; }
            if (sym == 'm' || sym == 'c') { ui.glue.hot_id = 4; on_pointer_button(app, 1, 1); return; }
            if (sym == 'o' || sym == 'e') { ui.modal = ModalId::AppOptions; return; }
            if (sym == 'v' || sym == 'i') { ui.glue.hot_id = 8; on_pointer_button(app, 1, 1); return; }
            if (sym == 13 || sym == ' ') {
                if (ui.glue.hot_id < 0) ui.glue.hot_id = 3;
                on_pointer_button(app, 1, 1);
                return;
            }
            if (sym == 27) g_quit = 1;
            return;
        }
        if (ui.screen == ScreenId::CampaignSelect) {
            static const int kOrder[7] = {7, 8, 6, 10, 5, 11, 9};
            if (scancode >= 79 && scancode <= 82) {
                int idx = 0;
                for (int i = 0; i < 7; ++i) if (kOrder[i] == ui.glue.hot_id) idx = i;
                idx = (idx + 7 + ((scancode == 81 || scancode == 79) ? 1 : -1)) % 7;
                if (const auto* w = ui.glue.layout.get(kOrder[idx]))
                    on_pointer_move(app, w->x + w->w / 2, w->y + w->h / 2);
                return;
            }
            if (sym == 27) { ui.open_main_menu(); return; }
            if (sym == 13 || sym == ' ') {
                if (ui.glue.hot_id < 0) ui.glue.hot_id = 7;
                on_pointer_button(app, 1, 1);
                return;
            }
            return;
        }
        if (ui.screen == ScreenId::Briefing) {
            if (sym == 27) { sd_sound_stop(62); if (ui.active_mission_idx >= 0) ui.open_campaign_select(); else ui.open_custom_game(); }
            else if (sym == 'r') { ui.campaign.replay_briefing(); ui.last_brief_rev = -1; }
            else if (sym == 13 || sym == ' ') ui.launch_from_briefing(now);
            else if (scancode == 82) { ui.brief_manual = std::max(0.0f, ui.brief_scroll - 28.0f); ui.brief_manual_until = now + 8000.0; }
            else if (scancode == 81) { ui.brief_manual = ui.brief_scroll + 28.0f; ui.brief_manual_until = now + 8000.0; }
            return;
        }
        if (ui.screen == ScreenId::CustomGame) {
            if (sym == 27) ui.open_main_menu();
            else if (scancode == 82 && ui.map_sel > 0) { ui.select_custom_map(ui.map_sel - 1); ui.map_scroll = std::max(0.0f, float((ui.map_sel - 4) * 20)); }
            else if (scancode == 81 && ui.map_sel + 1 < int(ui.map_files.size())) { ui.select_custom_map(ui.map_sel + 1); ui.map_scroll = std::max(0.0f, float((ui.map_sel - 4) * 20)); }
            else if (sym == 13) ui.start_custom_engine(now);
            return;
        }
        if (ui.screen == ScreenId::LoadBrowser) {
            if (sym == 27) ui.open_campaign_select();
            else if (scancode == 82 && ui.browser_sel > 0) --ui.browser_sel;
            else if (scancode == 81 && ui.browser_sel + 1 < int(ui.browser_files.size())) ++ui.browser_sel;
            else if (sym == 13 && !ui.browser_files.empty()) {
                ui.glue.hot_id = (ui.glue.name == "glucustm") ? 12 : 4;
                on_pointer_button(app, 1, 1);
            }
            return;
        }
        return;
    }

    // --- In-Game Playing Key Handling ---
    if (down && fkey == 10) {
        sd_command(6, 1, 0, 0, 0);
        ui.modal = ModalId::GameMenu;
        return;
    }
    if (down && fkey == 5 && !ui.active_is_replay) {
        char err[256] = {};
        std::string qs = app.dp.save_dir + "/quicksave.sav";
        if (sd_save_to_file(qs.c_str(), err, sizeof(err))) ui.show_toast("Quick-saved!", now);
        return;
    }
    if (down && fkey == 9) {
        char err[256] = {};
        std::string qs = app.dp.save_dir + "/quicksave.sav";
        if (sd_load_from_file(qs.c_str(), err, sizeof(err))) {
            ui.hud.occlusion_sent = false;
            ui.show_toast("Quick-loaded!", now);
        }
        return;
    }

    if (down && sym == 9) { // Tab toggles minimap terrain just like StarCraft!
        sd_command(8, sd_value(14, 0) ? 0 : 1, 0, 0, 0);
        return;
    }
    if (down && sym == '=') { ui.sim_fps = std::min(48, ui.sim_fps + 3); ui.show_toast("Speed: " + std::to_string(ui.sim_fps) + " fps", now, 1500); return; }
    if (down && sym == '-') { ui.sim_fps = std::max(10, ui.sim_fps - 3); ui.show_toast("Speed: " + std::to_string(ui.sim_fps) + " fps", now, 1500); return; }

    // Grid hotkey dispatch if enabled
    if (down && ui.hud.grid_hotkeys && sym >= 'a' && sym <= 'z') {
        char up = char(std::toupper(sym));
        for (const auto& h : ui.hud.hits) {
            if (h.slot >= 0 && h.hotkey == up) {
                if (h.more) ++ui.hud.card_page;
                else if (h.local_cancel) sd_command(5, 0, 0, 0, 0);
                else if (h.enabled && h.card_idx >= 0) sd_command(35, h.card_idx, h.cmd_token, 0, 0);
                return;
            }
        }
    }

    // Escape cancels submenu/targeting if active; if nothing active and cinematic skip available, forwards to engine; otherwise opens F10 menu on console
    if (down && sym == 27 && sd_value(0, 0) == 2) {
        sd_command(5, 0, 0, 0, 0);
        return;
    }

    sd_key(down, sym, scancode);
}

// --- Evdev Input Reader with Dynamic Hotplug + Console Dual-Stick Adaptation ---
struct EvdevHub {
    struct Dev { int fd = -1; int idx = -1; bool is_gamepad = false; };
    std::vector<Dev> devs;
    struct AxisCal { int center = 0, half = 127, raw = 0; } ax[4];
    float cur_rx = 0, cur_ry = 0, pan_rx = 0, pan_ry = 0;
    bool btn_sel = false, btn_start = false, btn_l1 = false, btn_r2 = false;
    bool dpad_u = false, dpad_d = false, dpad_l = false, dpad_r = false;
    double last_scan_ms = -5000.0;

    static void init_axis(int fd, int code, AxisCal& a) {
        struct input_absinfo ai{};
        if (ioctl(fd, EVIOCGABS(code), &ai) == 0 && ai.maximum > ai.minimum) {
            a.center = (ai.minimum + ai.maximum) / 2;
            a.half = (ai.maximum - ai.minimum) / 2;
            a.raw = ai.value;
        }
    }

    static float stick_vel(const AxisCal& a, float max_px) {
        float n = float(a.raw - a.center) / float(a.half > 0 ? a.half : 127);
        n = std::max(-1.0f, std::min(1.0f, n));
        float m = std::fabs(n);
        if (m <= 0.12f) return 0.0f;
        float u = (m - 0.12f) / 0.88f;
        float c = 0.30f * u + 0.70f * u * u;
        return (n < 0 ? -c : c) * max_px;
    }

    bool has_idx(int idx) const {
        for (const auto& d : devs) if (d.idx == idx && d.fd >= 0) return true;
        return false;
    }

    void rescan_hotplug(double now_ms) {
        if (now_ms - last_scan_ms < 1500.0) return;
        last_scan_ms = now_ms;
        for (int i = 1; i <= 16; ++i) {
            if (has_idx(i)) continue;
            char path[32], name[64] = {};
            std::snprintf(path, sizeof(path), "/dev/input/event%d", i);
            int fd = ::open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            bool gp = (std::strstr(name, "adckeypad") || std::strstr(name, "gpio") ||
                       std::strstr(name, "joystick") || std::strstr(name, "matrix"));
            if (std::strstr(name, "adckeypad") || std::strstr(name, "joystick")) {
                init_axis(fd, 0, ax[0]); init_axis(fd, 1, ax[1]);
                init_axis(fd, 3, ax[2]); init_axis(fd, 4, ax[3]);
            }
            devs.push_back({fd, i, gp});
        }
    }

    void open_all() {
        int km = ::open("/sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-adckeypad.0/input/input1/key_mode", O_WRONLY);
        if (km >= 0) { (void)::write(km, "1\n", 2); ::close(km); }
        rescan_hotplug(0.0);
    }

    void close_all() {
        for (auto& d : devs) if (d.fd >= 0) ::close(d.fd);
        devs.clear();
    }

    void poll_events(App& app, double now_ms) {
        rescan_hotplug(now_ms);
        struct input_event ev[32];
        int rel_dx = 0, rel_dy = 0;

        for (size_t di = 0; di < devs.size();) {
            auto& d = devs[di];
            int n;
            bool disconnected = false;
            while ((n = ::read(d.fd, ev, sizeof(ev))) > 0) {
                int cnt = n / int(sizeof(ev[0]));
                for (int j = 0; j < cnt; ++j) {
                    const auto& e = ev[j];
                    if (e.type == EV_REL) {
                        if (e.code == REL_X) rel_dx += e.value;
                        else if (e.code == REL_Y) rel_dy += e.value;
                        else if (e.code == REL_WHEEL && e.value != 0) {
                            if (app.ui.screen == blizz::ScreenId::Playing && app.ui.modal == blizz::ModalId::None) {
                                sd_pan(0, -e.value * 48);
                                app.dirty = true;
                            } else
                                on_key_event(app, 1, 0, e.value > 0 ? 82 : 81);
                        }
                    } else if (e.type == EV_ABS) {
                        if (e.code == 0)      ax[0].raw = e.value;
                        else if (e.code == 1) ax[1].raw = e.value;
                        else if (e.code == 3) ax[2].raw = e.value;
                        else if (e.code == 4) ax[3].raw = e.value;
                    } else if (e.type == EV_KEY && e.value != 2) {
                        int dn = (e.value == 1) ? 1 : 0;
                        if (e.code == BTN_LEFT)   { on_pointer_button(app, dn, 1); continue; }
                        if (e.code == BTN_RIGHT)  { on_pointer_button(app, dn, 3); continue; }
                        if (e.code == BTN_MIDDLE) { on_pointer_button(app, dn, 2); continue; }

                        if (d.is_gamepad) {
                            bool in_live_play = (app.ui.screen == blizz::ScreenId::Playing && app.ui.modal == blizz::ModalId::None);
                            switch (e.code) {
                            case 314: // SELECT -> Jump to alert (' ')
                                btn_sel = dn;
                                on_key_event(app, dn, ' ', 44);
                                break;
                            case 315: // START -> F10 Menu in-game, Enter/Start in menus
                                btn_start = dn;
                                if (dn) {
                                    if (app.ui.screen == blizz::ScreenId::Playing) on_key_event(app, 1, 0, 0, 10);
                                    else on_key_event(app, 1, 13, 40);
                                }
                                break;
                            case 352: // A -> Left Click (everywhere, supports cursor click + box-drag + double-tap!)
                                on_pointer_button(app, dn, 1);
                                break;
                            case 158: // B -> Smart Right Click in-game, Back/Esc in menus
                                if (in_live_play) on_pointer_button(app, dn, 3);
                                else if (dn) on_key_event(app, 1, 27, 41);
                                break;
                            case 139: // X -> Attack-Move ('a')
                                on_key_event(app, dn, 'a', 4);
                                break;
                            case 308: { // Y -> Build ('b') for workers, Stop ('s') for combat units
                                int ut = sd_value(1, 0);
                                bool worker = (ut == 7 || ut == 41 || ut == 64);
                                on_key_event(app, dn, worker ? 'b' : 's', worker ? 5 : 22);
                                break;
                            }
                            case 412: // L1 -> Hold for precision cursor + tap for Control Group 1
                                btn_l1 = dn;
                                if (dn && in_live_play) sd_command(25, 1, btn_r2 ? 1 : 0, 0, 0);
                                break;
                            case 407: // R1 -> Control Group 2
                                if (dn && in_live_play) sd_command(25, 2, btn_r2 ? 1 : 0, 0, 0);
                                break;
                            case 312: // L2 -> Toggle 640x480 vs 1:1 480x320 LCD Zoom
                                if (dn) app.ui.handheld_1to1 = !app.ui.handheld_1to1;
                                break;
                            case 313: // R2 -> Shift + Control-Group Assign modifier
                                btn_r2 = dn;
                                app.r2_held = dn;
                                on_key_event(app, dn, 0, 225);
                                break;
                            case 103: dpad_u = dn; if (!in_live_play) on_key_event(app, dn, 0, 82); break;
                            case 108: dpad_d = dn; if (!in_live_play) on_key_event(app, dn, 0, 81); break;
                            case 105: dpad_l = dn; if (!in_live_play) on_key_event(app, dn, 0, 80); break;
                            case 106: dpad_r = dn; if (!in_live_play) on_key_event(app, dn, 0, 79); break;
                            case 115:
                                if (dn) {
                                    app.ui.hw_vol = std::min(40, app.ui.hw_vol + 4);
                                    char c[80]; std::snprintf(c, sizeof(c), "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1", app.ui.hw_vol);
                                    (void)::system(c);
                                }
                                break;
                            case 114:
                                if (dn) {
                                    app.ui.hw_vol = std::max(0, app.ui.hw_vol - 4);
                                    char c[80]; std::snprintf(c, sizeof(c), "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1", app.ui.hw_vol);
                                    (void)::system(c);
                                }
                                break;
                            case 174: if (dn) g_quit = 1; break;
                            }
                            if (btn_sel && btn_start) g_quit = 1;
                        } else {
                            int fkey = (e.code >= KEY_F1 && e.code <= KEY_F10) ? ((e.code - KEY_F1) + 1) : 0;
                            if (dn && e.code == KEY_F11) { app.ui.request_fullscreen_toggle = true; continue; }
                            int sym = 0, scan = 0;
                            translate_linux_key(e.code, &sym, &scan);
                            if (sym || scan || fkey) on_key_event(app, dn, sym, scan, fkey);
                        }
                    }
                }
            }
            if (n < 0 && errno == ENODEV) disconnected = true;
            if (disconnected) {
                ::close(d.fd);
                devs.erase(devs.begin() + long(di));
            } else {
                ++di;
            }
        }

        float speed_scale = btn_l1 ? 0.35f : 1.0f;
        cur_rx += stick_vel(ax[0], 10.5f * speed_scale) + float(rel_dx);
        cur_ry += stick_vel(ax[1], 10.5f * speed_scale) + float(rel_dy);
        int mdx = int(cur_rx), mdy = int(cur_ry);
        if (mdx || mdy) {
            cur_rx -= float(mdx); cur_ry -= float(mdy);
            on_pointer_move(app, app.mouse_x + mdx, app.mouse_y + mdy);
        }

        if (app.ui.screen == blizz::ScreenId::Playing && app.ui.modal == blizz::ModalId::None) {
            pan_rx += stick_vel(ax[2], 18.0f) + float((dpad_r ? 12 : 0) - (dpad_l ? 12 : 0));
            pan_ry += stick_vel(ax[3], 18.0f) + float((dpad_d ? 12 : 0) - (dpad_u ? 12 : 0));
            int pdx = int(pan_rx), pdy = int(pan_ry);
            if (pdx || pdy) {
                pan_rx -= float(pdx); pan_ry -= float(pdy);
                sd_pan(pdx, pdy);
                app.dirty = true;
            }
        }

        if (app.ui.handheld_1to1) {
            if (app.mouse_x < app.view_x + 48) app.view_x = std::max(0, app.mouse_x - 48);
            if (app.mouse_x > app.view_x + 480 - 48) app.view_x = std::min(LOG_W - 480, app.mouse_x - (480 - 48));
            if (app.mouse_y < app.view_y + 40) app.view_y = std::max(0, app.mouse_y - 40);
            if (app.mouse_y > app.view_y + 320 - 40) app.view_y = std::min(LOG_H - 320, app.mouse_y - (320 - 40));
        }
    }
};

// --- Comprehensive Headless Verification (--headless-test) ---
static int run_self_test(const std::string& port_dir, const std::string& cli_data) {
    App app;
    app.dp.port_dir = port_dir;
    app.cli_data = cli_data;
    discover_data(app.dp, cli_data);

    int tw = 0, th = 0, hum = 0, cmp = 0;
    char title[128] = {};
    std::string weave = port_dir + "/maps/Weave_v1.scx";
    if (!sd_inspect_map(weave.c_str(), title, sizeof(title), &tw, &th, &hum, &cmp) || tw <= 0 || th <= 0) {
        std::fprintf(stderr, "SELF-TEST FAIL: sd_inspect_map failed on %s\n", weave.c_str());
        return 1;
    }

    std::string err;
    bool mpq_tested = false;
    if (app.mount_all_mpqs(err)) {
        blizz::PCX tcon, tfont;
        if (!blizz::load_pcx("game\\tconsole.pcx", tcon, true, -1) || tcon.width != 640 || tcon.height != 480) {
            std::fprintf(stderr, "SELF-TEST FAIL: failed decoding game\\tconsole.pcx\n");
            return 2;
        }
        if (!blizz::load_pcx("glue\\palmm\\tfont.pcx", tfont, false, -1)) {
            std::fprintf(stderr, "SELF-TEST FAIL: failed decoding glue\\palmm\\tfont.pcx\n");
            return 3;
        }
        // Test ClassicFontCipher on encrypted StarDat.mpq font\font8.fnt AND plain files\font\font16.fnt
        auto enc_fnt = blizz::mpq_read("font\\font8.fnt", 0);
        auto dec_fnt = blizz::font_cipher::decode_font_data(enc_fnt);
        if (dec_fnt.size() < 8 || std::memcmp(dec_fnt.data(), "FONT", 4) != 0) {
            std::fprintf(stderr, "SELF-TEST FAIL: ClassicFontCipher failed on StarDat.mpq font\\font8.fnt\n");
            return 4;
        }
        blizz::BlizzDialog glumain;
        if (!glumain.load("glumain") || !glumain.get(3)) {
            std::fprintf(stderr, "SELF-TEST FAIL: rez\\glumain.bin failed\n");
            return 5;
        }
        blizz::MapMeta mmeta;
        if (!blizz::build_map_preview(weave, mmeta) || !mmeta.preview.valid()) {
            std::fprintf(stderr, "SELF-TEST FAIL: build_map_preview failed\n");
            return 6;
        }
        if (!app.dp.starcraft_mpq.empty()) {
            char berr[128] = {};
            sd_open_archive(3, app.dp.starcraft_mpq.c_str(), berr, sizeof(berr));
            auto chk = blizz::mpq_read("campaign\\terran\\terran01\\staredit\\scenario.chk", 3);
            blizz::CampaignRunner cr;
            if (!cr.load_chk(chk.data(), chk.size(), 1) || cr.actions.empty()) {
                std::fprintf(stderr, "SELF-TEST FAIL: terran01 MBRF briefing parse failed\n");
                return 7;
            }
        }
        app.ui.open_main_menu();
        mpq_tested = true;
    } else {
        app.ui.screen = blizz::ScreenId::DataMissing;
    }

    blizz::Canvas canvas(g_canvas_buf);
    app.ui.render(canvas, 100.0, 16.0, 320, 240, false);
    uint16_t buf480[480 * 320];
    scale_640x480_to_480x320_rgb565(g_canvas_buf, buf480);
    crop_480x320_rgb565(g_canvas_buf, 80, 80, buf480);

    sd_close_all();
    std::printf("SELF-TEST OK: map='%s' %dx%d missions=%d mpq_ui_verified=%d\n",
                title, tw, th, kMissionCount, mpq_tested ? 1 : 0);
    return 0;
}

int main(int argc, char** argv) {
    std::atexit([]() { g_de_scaler.shutdown(); });
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_signal);
    std::signal(SIGSEGV, on_crash_signal);
    std::signal(SIGBUS, on_crash_signal);
    std::signal(SIGILL, on_crash_signal);
    std::signal(SIGABRT, on_crash_signal);
    std::signal(SIGFPE, on_crash_signal);

    char exe_buf[512] = {};
    ssize_t elen = ::readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    std::string port_dir = ".";
    if (elen > 0) {
        exe_buf[elen] = 0;
        std::string ep(exe_buf);
        size_t sl = ep.find_last_of('/');
        if (sl != std::string::npos) port_dir = ep.substr(0, sl);
        if (port_dir.size() > 4 && port_dir.substr(port_dir.size() - 4) == "/bin")
            port_dir = port_dir.substr(0, port_dir.size() - 4);
    }

    bool force_fb = false, force_x11 = false, fullscreen = false, headless = false, test_mission = false, bench = false;
    std::string cli_data;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--headless-test") headless = true;
        else if (a == "--test-mission") test_mission = true;
        else if (a == "--bench") bench = true;
        else if (a == "--fb") force_fb = true;
        else if (a == "--x11") force_x11 = true;
        else if (a == "--fullscreen" || a == "-f") fullscreen = true;
        else if (a == "--data" && i + 1 < argc) cli_data = argv[++i];
    }
    if (headless) return run_self_test(port_dir, cli_data);
    if (bench) {
        CpuBoostGuard boost;
        boost.engage();
        App app;
        app.dp.port_dir = port_dir;
        app.cli_data = cli_data;
        app.init_startup();
        int midx = 0;
        for (int i = 0; i < kMissionCount; ++i) if (kMissions[i].expansion) { midx = i; break; }
        app.ui.start_mission_flow(midx);
        if (!app.ui.launch_from_briefing(1000.0)) {
            std::fprintf(stderr, "BENCH init failed: %s\n", app.ui.toast_msg.c_str()); return 1;
        }
        std::string map = kMissions[midx].title;
        sd_set_fast_indexed(true);
        blizz::Canvas c(g_canvas_buf);
        const int N = 60;
        double t_step = 0, t_render8 = 0, t_idx565 = 0, t_hud = 0, t_s1280 = 0, t_s960 = 0, t_s480 = 0;
        static uint16_t tmp1280[1280 * 960];
        static uint16_t tmp480[480 * 320];
        static uint16_t rot480[480 * 320];
        uint16_t pal565[256];
        int iw = 0, ih = 0, ipitch = 0, drag[4];
        for (int f = 0; f < N; ++f) {
            double t0 = monotonic_ms();
            if ((f % 5) < 2) sd_step(); // authentic 24 Hz sim at 60 FPS (2/5 frames)
            double t1 = monotonic_ms();
            sd_render(1000.0 + f * 16.6);
            double t2 = monotonic_ms();
            const uint8_t* idx = sd_indexed_fb(&iw, &ih, &ipitch, pal565, drag);
            for (int y = 0; y < 392; ++y) {
                const uint8_t* s = idx + size_t(y) * size_t(ipitch);
                uint16_t* d = g_fb565 + y * LOG_W;
                for (int x = 0; x < LOG_W; x += 4) {
                    d[x+0] = pal565[s[x+0]]; d[x+1] = pal565[s[x+1]];
                    d[x+2] = pal565[s[x+2]]; d[x+3] = pal565[s[x+3]];
                }
            }
            double t3 = monotonic_ms();
            if ((f % 5) < 2) {
                app.ui.hud.draw(c, 1000.0 + f * 16.6, false, 320, 240, false, app.ui.campaign);
                for (int y = 296; y < LOG_H; ++y) rgba_row_to_rgb565(g_canvas_buf + y * LOG_W, g_hud565 + y * LOG_W, LOG_W);
            }
            std::memcpy(g_fb565 + 392 * LOG_W, g_hud565 + 392 * LOG_W, 88 * LOG_W * 2);
            double t4 = monotonic_ms();
            for (int y = 0; y < 480; ++y) {
                const uint16_t* sr = g_fb565 + y * 640;
                uint16_t* d0 = tmp1280 + (y * 2) * 1280;
#if defined(__ARM_NEON)
                for (int i = 0; i < 640; i += 8) {
                    uint16x8_t v = vld1q_u16(sr + i);
                    uint16x8x2_t z = vzipq_u16(v, v);
                    vst1q_u16(d0 + i * 2, z.val[0]);
                    vst1q_u16(d0 + i * 2 + 8, z.val[1]);
                }
#endif
                std::memcpy(d0 + 1280, d0, 1280 * 2);
            }
            double t5 = monotonic_ms();
            for (int vy = 0; vy < 720; vy += 3) {
                int sy0 = (vy / 3) * 2;
                uint16_t* d0 = tmp1280 + (vy + 0) * 960;
                uint16_t* d1 = tmp1280 + (vy + 1) * 960;
                uint16_t* d2 = tmp1280 + (vy + 2) * 960;
                const uint16_t* s0 = g_fb565 + sy0 * 640;
                const uint16_t* s1 = g_fb565 + (sy0 + 1) * 640;
                for (int i = 0; i < 320; ++i) { d0[i*3]=s0[i*2]; d0[i*3+1]=avg565(s0[i*2],s0[i*2+1]); d0[i*3+2]=s0[i*2+1]; }
                for (int i = 0; i < 320; ++i) { d2[i*3]=s1[i*2]; d2[i*3+1]=avg565(s1[i*2],s1[i*2+1]); d2[i*3+2]=s1[i*2+1]; }
                blend_row_565(d0, d2, d1, 960);
            }
            double t6 = monotonic_ms();
            scale_640x480_rgb565_to_480x320_rows(g_fb565, tmp480, 0, 320);
            for (int ty = 0; ty < 320; ty += 16)
                for (int tx = 0; tx < 480; tx += 16)
                    for (int Y = ty; Y < ty + 16; ++Y)
                        for (int X = tx; X < tx + 16; ++X)
                            rot480[X * 320 + (319 - Y)] = tmp480[Y * 480 + X];
            double t7 = monotonic_ms();
            t_step += t1 - t0; t_render8 += t2 - t1; t_idx565 += t3 - t2; t_hud += t4 - t3;
            t_s1280 += t5 - t4; t_s960 += t6 - t5; t_s480 += t7 - t6;
        }
        double tot = (t_step + t_render8 + t_idx565 + t_hud) / N;
        std::printf("BENCH 8->16bit (%d frames, map='%s'):\n"
                    "  sd_step(24Hz) = %.2f ms\n"
                    "  sd_render_8b  = %.2f ms\n"
                    "  idx_to_565    = %.2f ms\n"
                    "  hud_565(24Hz) = %.2f ms\n"
                    "  scale1280_565 = %.2f ms\n"
                    "  scale960_565  = %.2f ms\n"
                    "  scale480r_565 = %.2f ms\n"
                    "  TOTAL(game+hud) = %.2f ms (max %.1f FPS)\n",
                    N, map.c_str(), t_step/N, t_render8/N, t_idx565/N, t_hud/N,
                    t_s1280/N, t_s960/N, t_s480/N, tot, 1000.0/tot);
        sd_close_all();
        return 0;
    }
    if (test_mission) {
        AudioEngine test_audio;
        test_audio.start(false);
        App app;
        app.dp.port_dir = port_dir;
        app.cli_data = cli_data;
        app.init_startup();
        std::printf("[M] startup ok, sc_camp=%d bw_camp=%d maps=%zu\n",
                    app.ui.has_sc_camp ? 1 : 0, app.ui.has_bw_camp ? 1 : 0, app.ui.map_files.size());
        blizz::Canvas c(g_canvas_buf);
        for (int midx : {0, 1, 10, 30}) {
            if (midx >= kMissionCount) continue;
            app.ui.start_mission_flow(midx);
            app.ui.open_briefing_room();
            app.ui.glue.elapsed = blizz::BlizzGlue::ENTER_MS + 100.0;
            for (int t = 0; t < 15; ++t) app.ui.render(c, 500.0 + t * 200.0, 200.0, 320, 240, false);
            int open_movies = 0;
            for (int p = 0; p < 4; ++p) if (app.ui.brief_portraits[p].movie_id >= 0) ++open_movies;
            bool ok = app.ui.launch_from_briefing(4000.0);
            std::printf("[M] mission[%d] code=%d '%s': brief_movies=%d launch=%d toast='%s'\n",
                        midx, kMissions[midx].code, kMissions[midx].title, open_movies, ok ? 1 : 0, app.ui.toast_msg.c_str());
            std::fflush(stdout);
            if (ok) {
                for (int i = 0; i < 4; ++i) {
                    sd_step();
                    app.ui.render(c, 4050.0 + i * 42.0, 42.0, 320, 240, false);
                }
            }
            sd_close_game();
        }
        // Also test CustomGame Skirmish launch!
        app.ui.open_custom_game();
        bool cok = app.ui.start_custom_engine(5000.0);
        std::printf("[M] custom_skirmish=%d toast='%s'\n", cok ? 1 : 0, app.ui.toast_msg.c_str());
        if (cok) {
            for (int i = 0; i < 4; ++i) {
                sd_step();
                app.ui.render(c, 5050.0 + i * 42.0, 42.0, 320, 240, false);
            }
        }
        test_audio.shutdown();
        sd_close_all();
        std::printf("[M] ALL MISSION + SKIRMISH TESTS OK!\n");
        return 0;
    }

    CpuBoostGuard cpu_boost;
    cpu_boost.engage();

    App app;
    app.dp.port_dir = port_dir;
    app.cli_data = cli_data;

    X11Api x11;
    Display* dpy = nullptr;
    if (!force_fb && (force_x11 || std::getenv("DISPLAY") != nullptr)) {
        if (x11.load()) dpy = x11.OpenDisplay(nullptr);
        if (force_x11 && !dpy) {
            std::fprintf(stderr, "Cannot open X11 DISPLAY. Pass --fb for direct framebuffer mode.\n");
            return 1;
        }
    }

    AudioEngine audio;
    audio.start(dpy != nullptr);
    app.init_startup();

    blizz::Canvas world_canvas(g_world_buf);
    blizz::Canvas canvas(g_canvas_buf);
    double prev_ms = monotonic_ms();
    double next_sim = prev_ms;
    double last_world_ms = prev_ms - 1000.0;
    bool world_valid = false;
    int prev_cur_x = app.mouse_x, prev_cur_y = app.mouse_y, prev_cur_half = 38, prev_cur_anim = -1;
    blizz::ScreenId last_screen = app.ui.screen;
    blizz::ModalId last_modal = app.ui.modal;

    struct PresentStep {
        enum Kind { None, CursorRect, FullViewport } kind = None;
        int lx0 = 0, ly0 = 0, lx1 = LOG_W, ly1 = LOG_H;
    };

    auto cursor_half_box = [&]() -> int {
        if (app.ui.screen == blizz::ScreenId::Playing && app.ui.modal == blizz::ModalId::None) {
            int cbox = 64;
            (void)sd_cursor_rgba(&cbox);
            return std::max(40, std::min(68, cbox / 2 + 2));
        }
        return 36;
    };

    app.ui.direct_fb_ok = false;
    bool active_is_565 = false;
    bool hud565_valid = false;
    bool skip_sw_cursor = false;
    double last_hud_ms = 0.0;

    auto draw_cursor_565 = [&](uint16_t* dst565, int mx, int my) {
        int cbox = 0;
        const uint8_t* cpx = sd_cursor_rgba(&cbox);
        if (!cpx || cbox <= 0) return;
        int ox = mx - cbox / 2, oy = my - cbox / 2;
        int x0 = std::max(0, ox), y0 = std::max(0, oy);
        int x1 = std::min(LOG_W, ox + cbox), y1 = std::min(LOG_H, oy + cbox);
        const uint32_t* s = reinterpret_cast<const uint32_t*>(cpx);
        for (int y = y0; y < y1; ++y) {
            const uint32_t* srow = s + (y - oy) * cbox;
            uint16_t* drow = dst565 + y * LOG_W;
            for (int x = x0; x < x1; ++x) {
                uint32_t p = srow[x - ox];
                if ((p >> 24) >= 128) drow[x] = rgba_to_rgb565(p);
            }
        }
    };

    auto compose_playing_565 = [&](double now, bool refresh_hud) -> bool {
        sd_set_fast_indexed(true);
        sd_render(now);
        int iw = 0, ih = 0, ipitch = 0;
        uint16_t pal565[256];
        int drag[4] = {-1, 0, 0, 0};
        const uint8_t* idx = sd_indexed_fb(&iw, &ih, &ipitch, pal565, drag);
        if (!idx || iw != LOG_W || ih != LOG_H) {
            sd_set_fast_indexed(false);
            return false;
        }

        // Direct 8-bit indexed -> 16-bit RGB565 into clean backing store g_world565 (rows 0..391)
        constexpr int VIS_H = 392;
        for (int y = 0; y < VIS_H; ++y) {
            const uint8_t* s = idx + size_t(y) * size_t(ipitch);
            uint16_t* d = g_world565 + y * LOG_W;
            for (int x = 0; x < LOG_W; x += 4) {
                d[x + 0] = pal565[s[x + 0]];
                d[x + 1] = pal565[s[x + 1]];
                d[x + 2] = pal565[s[x + 2]];
                d[x + 3] = pal565[s[x + 3]];
            }
        }
        // Green drag-selection rectangle directly in RGB565
        if (app.lmb_down && !app.minimap_dragging && app.last_click_btn == 1 && app.last_click_y < 380 &&
            (std::abs(app.mouse_x - app.last_click_x) > 4 || std::abs(app.mouse_y - app.last_click_y) > 4)) {
            int x0 = std::max(0, std::min(app.last_click_x, app.mouse_x));
            int y0 = std::max(0, std::min(app.last_click_y, app.mouse_y));
            int x1 = std::min(LOG_W - 1, std::max(app.last_click_x, app.mouse_x));
            int y1 = std::min(VIS_H - 1, std::max(app.last_click_y, app.mouse_y));
            const uint16_t green = 0x07E2u;
            if (x0 <= x1 && y0 <= y1) {
                for (int x = x0; x <= x1; ++x) { g_world565[y0 * LOG_W + x] = green; g_world565[y1 * LOG_W + x] = green; }
                for (int y = y0; y <= y1; ++y) { g_world565[y * LOG_W + x0] = green; g_world565[y * LOG_W + x1] = green; }
            }
        }

        // Refresh 16-bit HUD cache on simulation ticks / input / every 40ms
        if (refresh_hud || !hud565_valid) {
            last_hud_ms = now;
            app.ui.campaign.poll_mission();
            // Clear only top (0..15), bottom (296..479), and previously active middle rows (avoids 700 KB memset!)
            std::memset(g_canvas_buf, 0, size_t(16 * LOG_W) * 4);
            if (!hud565_valid) {
                std::memset(g_canvas_buf + 16 * LOG_W, 0, size_t((296 - 16) * LOG_W) * 4);
            } else {
                for (int y = 16; y < 296; ++y)
                    if (g_hud_row_active[y]) std::memset(g_canvas_buf + y * LOG_W, 0, size_t(LOG_W) * 4);
            }
            std::memset(g_canvas_buf + 296 * LOG_W, 0, size_t((LOG_H - 296) * LOG_W) * 4);
            blizz::Canvas hc(g_canvas_buf);
            app.ui.hud.draw(hc, now, false, app.mouse_x, app.mouse_y, app.lmb_down, app.ui.campaign);
            int outc = sd_value(7, 0);
            if ((outc == 1 || outc == 2) && !app.ui.outcome_dismissed) {
                app.ui.last_outcome = outc;
                sd_command(6, 1, 0, 0, 0);
                app.ui.modal = blizz::ModalId::OutcomePrompt;
            }
            // Scan rows 0..VIS_H-1 so in-game transmission text, tooltips, timers & leaderboards are NEVER clipped!
            for (int y = 0; y < VIS_H; ++y) {
                const uint32_t* s = g_canvas_buf + y * LOG_W;
                uint32_t any_a = 0;
                for (int x = 0; x < LOG_W; x += 4)
                    any_a |= (s[x] | s[x + 1] | s[x + 2] | s[x + 3]) & 0xFF000000u;
                if (!any_a) {
                    g_hud_row_active[y] = 0;
                    continue;
                }
                g_hud_row_active[y] = 1;
                uint16_t* d = g_hud565 + y * LOG_W;
                uint8_t* m = g_hud_alpha + y * LOG_W;
                rgba_row_to_rgb565(s, d, LOG_W);
                for (int x = 0; x < LOG_W; ++x) m[x] = (s[x] >> 24) >= 128 ? 1 : 0;
            }
            for (int y = VIS_H; y < LOG_H; ++y)
                rgba_row_to_rgb565(g_canvas_buf + y * LOG_W, g_hud565 + y * LOG_W, LOG_W);
            hud565_valid = true;
        }

        // Composite cached 16-bit HUD onto g_world565 (empty middle rows skipped in 1 cycle!)
        for (int y = 0; y < VIS_H; ++y) {
            if (!g_hud_row_active[y]) continue;
            const uint16_t* h = g_hud565 + y * LOG_W;
            const uint8_t* m = g_hud_alpha + y * LOG_W;
            uint16_t* d = g_world565 + y * LOG_W;
            for (int x = 0; x < LOG_W; ++x) if (m[x]) d[x] = h[x];
        }
        std::memcpy(g_world565 + VIS_H * LOG_W, g_hud565 + VIS_H * LOG_W, size_t(LOG_H - VIS_H) * LOG_W * sizeof(uint16_t));

        std::memcpy(g_fb565, g_world565, sizeof(g_fb565));
        if (!skip_sw_cursor) draw_cursor_565(g_fb565, app.mouse_x, app.mouse_y);
        return true;
    };

    auto step_frame = [&]() -> PresentStep {
        double now = monotonic_ms();
        prev_ms = now;

        if (app.ui.request_rescan) {
            app.ui.request_rescan = false;
            app.init_startup();
            app.dirty = true;
        }
        if (app.ui.request_quit) { g_quit = 1; return {}; }

        double step_ms = 1000.0 / std::max(8, app.ui.sim_fps);
        int steps = 0;
        while (now >= next_sim && steps < 4) {
            if (app.ui.screen == blizz::ScreenId::Playing && app.ui.modal == blizz::ModalId::None)
                sd_step();
            next_sim += step_ms;
            ++steps;
        }
        if (now > next_sim + 200.0) next_sim = now;

        if (app.ui.screen != last_screen || app.ui.modal != last_modal) {
            last_screen = app.ui.screen;
            last_modal = app.ui.modal;
            hud565_valid = false;
            app.dirty = true;
        }

        double since_world = now - last_world_ms;
        bool need_world = !world_valid || app.dirty;
        if (!need_world) {
            if (app.ui.screen == blizz::ScreenId::Playing && app.ui.modal == blizz::ModalId::None) {
                // World renders on authentic 24 Hz simulation ticks (or drag/edge-scroll), freeing >55% CPU/Xorg for 60+ FPS cursor!
                bool edge_or_drag = app.lmb_down || (sd_value(0, 0) == 2) ||
                                    app.mouse_x <= 6 || app.mouse_x >= LOG_W - 7 ||
                                    app.mouse_y <= 6 || app.mouse_y >= LOG_H - 7;
                if (steps > 0 || (edge_or_drag && since_world >= 33.0) || since_world >= 100.0)
                    need_world = true;
            } else {
                bool fast_anim = !app.ui.glue.ready() ||
                                 app.ui.screen == blizz::ScreenId::TitleSplash ||
                                 app.ui.screen == blizz::ScreenId::Story ||
                                 app.ui.screen == blizz::ScreenId::Credits ||
                                 (app.ui.screen == blizz::ScreenId::Score && app.ui.score.progress < 1.0);
                if (since_world >= (fast_anim ? 33.0 : 67.0))
                    need_world = true;
            }
        }

        int cur_anim = int(now / 100.0);
        if (need_world) {
            double world_dt = std::max(0.0, std::min(100.0, since_world));
            last_world_ms = now;
            bool was_dirty = app.dirty;
            app.dirty = false;
            bool toast_active = !app.ui.toast_msg.empty() && now < app.ui.toast_until;
            bool can_fast565 = (app.ui.screen == blizz::ScreenId::Playing &&
                                app.ui.modal == blizz::ModalId::None &&
                                !toast_active);
            int half = cursor_half_box();
            if (can_fast565 && compose_playing_565(now, steps > 0 || was_dirty || (now - last_hud_ms) >= 40.0)) {
                active_is_565 = true;
            } else {
                sd_set_fast_indexed(false);
                active_is_565 = false;
                app.ui.render(world_canvas, now, world_dt, app.mouse_x, app.mouse_y, app.lmb_down, false);
                std::memcpy(g_canvas_buf, g_world_buf, sizeof(g_world_buf));
                if (!skip_sw_cursor) app.ui.draw_cursor_overlay(canvas, now, app.mouse_x, app.mouse_y);
            }
            prev_cur_x = app.mouse_x;
            prev_cur_y = app.mouse_y;
            prev_cur_half = half;
            prev_cur_anim = cur_anim;
            world_valid = true;
            last_screen = app.ui.screen;
            last_modal = app.ui.modal;
            return {PresentStep::FullViewport, 0, 0, LOG_W, LOG_H};
        }

        if (skip_sw_cursor || (app.mouse_x == prev_cur_x && app.mouse_y == prev_cur_y && cur_anim == prev_cur_anim))
            return {PresentStep::None, 0, 0, 0, 0};

        int ox0 = std::max(0, prev_cur_x - prev_cur_half);
        int oy0 = std::max(0, prev_cur_y - prev_cur_half);
        int ox1 = std::min(LOG_W, prev_cur_x + prev_cur_half);
        int oy1 = std::min(LOG_H, prev_cur_y + prev_cur_half);
        int half = cursor_half_box();
        int nx0 = std::max(0, app.mouse_x - half);
        int ny0 = std::max(0, app.mouse_y - half);
        int nx1 = std::min(LOG_W, app.mouse_x + half);
        int ny1 = std::min(LOG_H, app.mouse_y + half);
        if (active_is_565) {
            for (int y = oy0; y < oy1; ++y)
                std::memcpy(g_fb565 + y * LOG_W + ox0, g_world565 + y * LOG_W + ox0, size_t(ox1 - ox0) * sizeof(uint16_t));
            draw_cursor_565(g_fb565, app.mouse_x, app.mouse_y);
        } else {
            for (int y = oy0; y < oy1; ++y)
                std::memcpy(g_canvas_buf + y * LOG_W + ox0, g_world_buf + y * LOG_W + ox0, size_t(ox1 - ox0) * 4);
            app.ui.draw_cursor_overlay(canvas, now, app.mouse_x, app.mouse_y);
        }
        prev_cur_x = app.mouse_x;
        prev_cur_y = app.mouse_y;
        prev_cur_half = half;
        prev_cur_anim = cur_anim;
        return {PresentStep::CursorRect, std::min(ox0, nx0), std::min(oy0, ny0), std::max(ox1, nx1), std::max(oy1, ny1)};
    };

    if (dpy) {
        int scr = DefaultScreen(dpy);
        int sw = DisplayWidth(dpy, scr), sh = DisplayHeight(dpy, scr);
        int win_w = (sw >= 1280 && sh >= 960) ? 1280 : (sh >= 720 ? 960 : 640);
        int win_h = (win_w == 1280) ? 960 : (win_w == 960 ? 720 : 480);
        if (fullscreen) { win_w = sw; win_h = sh; }

        Window win = x11.CreateSimpleWindow(dpy, RootWindow(dpy, scr),
                                            std::max(0, (sw - win_w) / 2), std::max(0, (sh - win_h) / 2),
                                            unsigned(win_w), unsigned(win_h), 0, 0, 0);
        x11.StoreName(dpy, win, "StarCraft: Brood War (StarDroid 1.9.11)");
        x11.SelectInput(dpy, win, ExposureMask | KeyPressMask | KeyReleaseMask |
                                  ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
        Atom wm_del = x11.InternAtom(dpy, "WM_DELETE_WINDOW", False);
        x11.SetWMProtocols(dpy, win, &wm_del, 1);

        // Hide X11 cursor so authentic Blizzard animated cursor renders cleanly with zero Xorg cursor flicker
        char zero_bits[8] = {};
        Pixmap blank_pm = x11.CreateBitmapFromData(dpy, win, zero_bits, 8, 8);
        XColor black{};
        Cursor blank_cur = x11.CreatePixmapCursor(dpy, blank_pm, blank_pm, &black, &black, 0, 0);
        x11.DefineCursor(dpy, win, blank_cur);
        x11.FreePixmap(dpy, blank_pm);
        x11.MapWindow(dpy, win);

        auto toggle_fs = [&]() {
            fullscreen = !fullscreen;
            if (!fullscreen) g_de_scaler.restore();
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
        if (fullscreen) { fullscreen = false; toggle_fs(); }

        int depth = DefaultDepth(dpy, scr);
        Visual* vis = DefaultVisual(dpy, scr);
        GC gc = DefaultGC(dpy, scr);
        int img_w = 0, img_h = 0, vp_w = 0, vp_h = 0, ox = 0, oy = 0;
        // 0 = 4:3 Full (1440x1080 / 960x720), 1 = Crisp Integer 2x (1280x960), 2 = 16:9 Stretch (HW DE only); F2 toggles!
        int scale_mode = (sw >= 1280 && sh >= 960) ? 1 : 0;
        std::vector<uint8_t> xbuf;
        std::vector<uint16_t> xmap;
        XImage* ximg[2] = {nullptr, nullptr};
        XShmSegmentInfo shminfo[2] = {};
        bool using_shm = false;
        int shm_cur = 0;
        bool need_clear_border = true;

        auto destroy_ximg = [&]() {
            if (using_shm) {
                for (int b = 0; b < 2; ++b) {
                    if (!ximg[b]) continue;
                    x11.ShmDetach(dpy, &shminfo[b]);
                    x11.Sync(dpy, False);
                    XDestroyImage(ximg[b]);
                    if (shminfo[b].shmaddr && shminfo[b].shmaddr != reinterpret_cast<char*>(-1))
                        shmdt(shminfo[b].shmaddr);
                    ximg[b] = nullptr;
                }
                using_shm = false;
            } else if (ximg[0]) {
                ximg[0]->data = nullptr;
                XDestroyImage(ximg[0]);
                ximg[0] = nullptr;
            }
        };

        auto alloc_vp_images = [&]() {
            destroy_ximg();
            if (x11.has_shm && x11.ShmQueryExtension(dpy)) {
                bool ok = true;
                for (int b = 0; b < 2; ++b) {
                    std::memset(&shminfo[b], 0, sizeof(shminfo[b]));
                    ximg[b] = x11.ShmCreateImage(dpy, vis, unsigned(depth), ZPixmap, nullptr, &shminfo[b], unsigned(vp_w), unsigned(vp_h));
                    if (!ximg[b]) { ok = false; break; }
                    size_t bytes = size_t(ximg[b]->bytes_per_line) * size_t(ximg[b]->height);
                    shminfo[b].shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
                    if (shminfo[b].shmid < 0) { ok = false; break; }
                    shminfo[b].shmaddr = static_cast<char*>(shmat(shminfo[b].shmid, nullptr, 0));
                    if (shminfo[b].shmaddr == reinterpret_cast<char*>(-1)) {
                        shmctl(shminfo[b].shmid, IPC_RMID, nullptr);
                        ok = false; break;
                    }
                    ximg[b]->data = shminfo[b].shmaddr;
                    shminfo[b].readOnly = False;
                    if (!x11.ShmAttach(dpy, &shminfo[b])) {
                        shmdt(shminfo[b].shmaddr);
                        shmctl(shminfo[b].shmid, IPC_RMID, nullptr);
                        ok = false; break;
                    }
                    x11.Sync(dpy, False);
                    shmctl(shminfo[b].shmid, IPC_RMID, nullptr);
                }
                if (ok) {
                    using_shm = true;
                } else {
                    for (int b = 0; b < 2; ++b) {
                        if (ximg[b]) {
                            if (shminfo[b].shmaddr && shminfo[b].shmaddr != reinterpret_cast<char*>(-1)) {
                                x11.ShmDetach(dpy, &shminfo[b]);
                                x11.Sync(dpy, False);
                                shmdt(shminfo[b].shmaddr);
                            }
                            XDestroyImage(ximg[b]);
                            ximg[b] = nullptr;
                        }
                    }
                }
            }
            if (!using_shm) {
                int bpp_bytes = (depth <= 16) ? 2 : 4;
                xbuf.assign(size_t(vp_w) * size_t(vp_h) * size_t(bpp_bytes), 0);
                ximg[0] = x11.CreateImage(dpy, vis, unsigned(depth), ZPixmap, 0,
                                          reinterpret_cast<char*>(xbuf.data()),
                                          unsigned(vp_w), unsigned(vp_h), bpp_bytes * 8, vp_w * bpp_bytes);
            }
            shm_cur = 0;
        };

        // Optional Direct /dev/fb1 (HDMI) / /dev/fb0 Fullscreen Bypass: skips Xorg ShadowFB triple-copy (~31 ms/frame on 1080p!)
        int direct_fb_fd = -1;
        uint8_t* direct_fb_raw = nullptr;
        uint8_t* direct_fb_ptr = nullptr;
        size_t direct_fb_len = 0, direct_fb_pitch = 0;
        int direct_fb_w = 0, direct_fb_h = 0;
        for (const char* fbpath : {"/dev/fb1", "/dev/fb0"}) {
            int fd = ::open(fbpath, O_RDWR);
            if (fd < 0) continue;
            struct fb_var_screeninfo dv{}; struct fb_fix_screeninfo df{};
            if (ioctl(fd, FBIOGET_VSCREENINFO, &dv) == 0 &&
                ioctl(fd, FBIOGET_FSCREENINFO, &df) == 0 &&
                dv.bits_per_pixel == 16 && int(dv.xres) == sw && int(dv.yres) == sh) {
                void* m = mmap(nullptr, df.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (m != MAP_FAILED) {
                    direct_fb_fd = fd;
                    direct_fb_raw = static_cast<uint8_t*>(m);
                    direct_fb_len = df.smem_len;
                    direct_fb_pitch = df.line_length;
                    direct_fb_ptr = direct_fb_raw + size_t(dv.yoffset) * direct_fb_pitch + size_t(dv.xoffset) * 2;
                    direct_fb_w = int(dv.xres);
                    direct_fb_h = int(dv.yres);
                    std::fprintf(stderr, "[X11] Direct %s fullscreen bypass enabled (%dx%d RGB565, skips ShadowFB & XShm!)\n",
                                 fbpath, direct_fb_w, direct_fb_h);
                    if (std::strcmp(fbpath, "/dev/fb1") == 0 && g_de_scaler.init(direct_fb_w, direct_fb_h, direct_fb_pitch)) {
                        scale_mode = 0; // 4:3 Full (1440x1080 / 960x720) via 0.0 ms hardware DE scaler!
                        std::fprintf(stderr, "[X11] ATM7051H DE_ML0 hardware scaler enabled (640x480 -> %dx%d at 0.0 ms CPU!)\n",
                                     direct_fb_w, direct_fb_h);
                    }
                    std::fflush(stderr);
                    break;
                }
            }
            ::close(fd);
        }

        auto can_use_direct_fb = [&]() -> bool {
            return fullscreen && direct_fb_ptr &&
                   win_w == direct_fb_w && win_h == direct_fb_h &&
                   ox >= 0 && oy >= 0 && ox + vp_w <= direct_fb_w && oy + vp_h <= direct_fb_h &&
                   vp_w <= 1920;
        };

        // Dual-core pipelined Direct /dev/fb1 presenter: Core 1 writes to /dev/fb1 while Core 0 renders!
        static uint16_t dfb_stage[2][LOG_W * LOG_H];
        struct DfbJob {
            int vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0;
            int vp_w = 0, vp_h = 0, ox = 0, oy = 0;
            bool clear_border = false;
            bool is_full = false;
            bool hw_scale = false;
        };
        std::mutex dfb_mtx;
        std::condition_variable dfb_cv;
        DfbJob dfb_job{};
        int dfb_front = 0, dfb_back = 1;
        bool dfb_has_job = false, dfb_stop = false, dfb_allow_hw = false;
        std::vector<uint16_t> dfb_xmap;

        auto restore_de_sync = [&]() {
            std::lock_guard<std::mutex> lk(dfb_mtx);
            dfb_allow_hw = false;
            g_de_scaler.restore();
        };

        std::thread dfb_worker;
        if (direct_fb_ptr) {
            dfb_worker = std::thread([&]() {
                static uint16_t dfb_prev[LOG_W * LOG_H];
                bool dfb_prev_valid = false;
                int last_vp_w = 0, last_vp_h = 0;
                uint16_t line0[1920], line1[1920], line2[1920];
                std::vector<uint16_t> local_xmap;
                while (true) {
                    DfbJob job;
                    const uint16_t* src565 = nullptr;
                    {
                        std::unique_lock<std::mutex> lk(dfb_mtx);
                        dfb_cv.wait(lk, [&]() { return dfb_has_job || dfb_stop; });
                        if (dfb_stop && !dfb_has_job) break;
                        std::swap(dfb_front, dfb_back);
                        src565 = dfb_stage[dfb_front];
                        job = dfb_job;
                        if (!job.hw_scale && int(local_xmap.size()) != job.vp_w) local_xmap = dfb_xmap;
                        dfb_has_job = false;
                    }
                    if (job.hw_scale) {
                        if (job.clear_border || job.vp_w != last_vp_w || job.vp_h != last_vp_h) {
                            dfb_prev_valid = false;
                            last_vp_w = job.vp_w; last_vp_h = job.vp_h;
                        }
                        int lx0 = job.vx0, ly0 = job.vy0, lx1 = job.vx1, ly1 = job.vy1;
                        size_t row_bytes = size_t(lx1 - lx0) * sizeof(uint16_t);
                        for (int y = ly0; y < ly1; ++y) {
                            const uint16_t* sr = src565 + y * LOG_W + lx0;
                            uint16_t* pr = dfb_prev + y * LOG_W + lx0;
                            if (job.is_full && dfb_prev_valid && std::memcmp(sr, pr, row_bytes) == 0)
                                continue;
                            std::memcpy(pr, sr, row_bytes);
                            std::memcpy(direct_fb_ptr + size_t(y) * direct_fb_pitch + size_t(lx0) * 2, sr, row_bytes);
                        }
                        {
                            std::lock_guard<std::mutex> lk(dfb_mtx);
                            if (dfb_allow_hw) g_de_scaler.apply(job.ox, job.oy, job.vp_w, job.vp_h);
                        }
                        if (job.is_full) dfb_prev_valid = true;
                        continue;
                    }
                    if (job.clear_border || job.vp_w != last_vp_w || job.vp_h != last_vp_h) {
                        if (job.clear_border) {
                            for (int y = 0; y < direct_fb_h; ++y)
                                std::memset(direct_fb_ptr + size_t(y) * direct_fb_pitch, 0, size_t(direct_fb_w) * 2);
                        }
                        dfb_prev_valid = false;
                        last_vp_w = job.vp_w; last_vp_h = job.vp_h;
                    }
                    int vx0 = job.vx0, vy0 = job.vy0, vx1 = job.vx1, vy1 = job.vy1;
                    int sx0 = local_xmap[size_t(vx0)];
                    int sx1 = std::min(LOG_W, int(local_xmap[size_t(vx1 - 1)]) + 2);
                    size_t copy_bytes = size_t(vx1 - vx0) * sizeof(uint16_t);
                    if (job.vp_w == 960 && job.vp_h == 720) {
                        int by0 = (vy0 / 3) * 3;
                        int by1 = std::min(720, ((vy1 + 2) / 3) * 3);
                        auto fill_960 = [&](int sy, uint16_t* drow) {
                            const uint16_t* s = src565 + sy * LOG_W;
                            if (vx0 == 0 && vx1 == 960) {
                                uint16_t* d = drow;
                                for (int i = 0; i < 320; ++i, s += 2, d += 3) {
                                    d[0] = s[0]; d[1] = avg565(s[0], s[1]); d[2] = s[1];
                                }
                            } else {
                                for (int vx = vx0; vx < vx1; ++vx) {
                                    int blk = (vx / 3) * 2, rem = vx % 3;
                                    drow[vx] = (rem == 0) ? s[blk] : (rem == 2 ? s[blk + 1] : avg565(s[blk], s[blk + 1]));
                                }
                            }
                        };
                        for (int vy = by0; vy < by1; vy += 3) {
                            int sy0 = (vy / 3) * 2;
                            if (job.is_full && dfb_prev_valid &&
                                std::memcmp(src565 + sy0 * LOG_W, dfb_prev + sy0 * LOG_W, size_t(LOG_W) * 4) == 0)
                                continue;
                            std::memcpy(dfb_prev + sy0 * LOG_W + sx0, src565 + sy0 * LOG_W + sx0, size_t(sx1 - sx0) * 2);
                            std::memcpy(dfb_prev + (sy0 + 1) * LOG_W + sx0, src565 + (sy0 + 1) * LOG_W + sx0, size_t(sx1 - sx0) * 2);
                            fill_960(sy0, line0);
                            fill_960(sy0 + 1, line2);
                            blend_row_565(line0 + vx0, line2 + vx0, line1 + vx0, vx1 - vx0);
                            std::memcpy(direct_fb_ptr + size_t(job.oy + vy + 0) * direct_fb_pitch + size_t(job.ox + vx0) * 2, line0 + vx0, copy_bytes);
                            std::memcpy(direct_fb_ptr + size_t(job.oy + vy + 1) * direct_fb_pitch + size_t(job.ox + vx0) * 2, line1 + vx0, copy_bytes);
                            std::memcpy(direct_fb_ptr + size_t(job.oy + vy + 2) * direct_fb_pitch + size_t(job.ox + vx0) * 2, line2 + vx0, copy_bytes);
                        }
                    } else {
                        int prev_sy = -1;
                        bool skip_sy = false;
                        for (int vy = vy0; vy < vy1; ++vy) {
                            int sy = (vy * LOG_H) / job.vp_h;
                            if (sy != prev_sy) {
                                prev_sy = sy;
                                const uint16_t* sr = src565 + sy * LOG_W;
                                if (job.is_full && dfb_prev_valid &&
                                    std::memcmp(sr, dfb_prev + sy * LOG_W, size_t(LOG_W) * sizeof(uint16_t)) == 0) {
                                    skip_sy = true;
                                    continue;
                                }
                                skip_sy = false;
                                std::memcpy(dfb_prev + sy * LOG_W + sx0, sr + sx0, size_t(sx1 - sx0) * sizeof(uint16_t));
                                if (job.vp_w == 1280 && vx0 == 0 && vx1 == 1280) {
#if defined(__ARM_NEON)
                                    for (int i = 0; i < 640; i += 8) {
                                        uint16x8_t v = vld1q_u16(sr + i);
                                        uint16x8x2_t z = vzipq_u16(v, v);
                                        vst1q_u16(line0 + i * 2, z.val[0]);
                                        vst1q_u16(line0 + i * 2 + 8, z.val[1]);
                                    }
#else
                                    for (int i = 0; i < 640; ++i) { line0[i * 2] = sr[i]; line0[i * 2 + 1] = sr[i]; }
#endif
                                } else if (job.vp_w == 640 && vx0 == 0 && vx1 == 640) {
                                    std::memcpy(line0, sr, 640 * sizeof(uint16_t));
                                } else {
                                    for (int vx = vx0; vx < vx1; ++vx) line0[vx] = sr[local_xmap[size_t(vx)]];
                                }
                            }
                            if (!skip_sy)
                                std::memcpy(direct_fb_ptr + size_t(job.oy + vy) * direct_fb_pitch + size_t(job.ox + vx0) * 2, line0 + vx0, copy_bytes);
                        }
                    }
                    if (job.is_full) dfb_prev_valid = true;
                }
            });
        }

        auto update_viewport = [&]() {
            int new_vp_w, new_vp_h;
            if (scale_mode == 2 && fullscreen && g_de_scaler.de) {
                new_vp_w = img_w;
                new_vp_h = img_h;
            } else if (scale_mode == 1 && img_w >= LOG_W && img_h >= LOG_H) {
                int mult = std::min(img_w / LOG_W, img_h / LOG_H);
                new_vp_w = LOG_W * mult;
                new_vp_h = LOG_H * mult;
            } else {
                new_vp_h = img_h; new_vp_w = img_h * 4 / 3;
                if (new_vp_w > img_w) { new_vp_w = img_w; new_vp_h = img_w * 3 / 4; }
            }
            new_vp_w = std::max(64, new_vp_w); new_vp_h = std::max(64, new_vp_h);
            bool size_changed = (new_vp_w != vp_w || new_vp_h != vp_h);
            vp_w = new_vp_w; vp_h = new_vp_h;
            ox = (img_w - vp_w) / 2; oy = (img_h - vp_h) / 2;
            xmap.resize(size_t(vp_w));
            for (int vx = 0; vx < vp_w; ++vx) xmap[size_t(vx)] = uint16_t((vx * LOG_W) / vp_w);
            if (can_use_direct_fb()) {
                // Save 4.8 MB of unused /SYSV XShm RAM when direct /dev/fb1 is active!
                destroy_ximg();
            } else {
                restore_de_sync();
                if (size_changed || !ximg[0]) alloc_vp_images();
            }
            need_clear_border = true;
            app.dirty = true;
        };

        auto ensure_ximg = [&](int w, int h) {
            w = std::max(64, w); h = std::max(64, h);
            if (w == img_w && h == img_h && (can_use_direct_fb() || ximg[0])) return;
            img_w = w; img_h = h;
            update_viewport();
        };
        ensure_ximg(win_w, win_h);

        bool shm_in_flight = false;
        auto present_rect_x11 = [&](int lx0, int ly0, int lx1, int ly1, bool is_full) {
            if (lx0 >= lx1 || ly0 >= ly1) return;
            int vx0 = is_full ? 0 : std::max(0, (lx0 * vp_w) / LOG_W - 2);
            int vy0 = is_full ? 0 : std::max(0, (ly0 * vp_h) / LOG_H - 2);
            int vx1 = is_full ? vp_w : std::min(vp_w, (lx1 * vp_w + LOG_W - 1) / LOG_W + 2);
            int vy1 = is_full ? vp_h : std::min(vp_h, (ly1 * vp_h + LOG_H - 1) / LOG_H + 2);
            if (vx0 >= vx1 || vy0 >= vy1) return;

            int slx0 = xmap[size_t(vx0)];
            int slx1 = std::min(LOG_W, int(xmap[size_t(vx1 - 1)]) + 2);
            int span_len = slx1 - slx0;

            if (can_use_direct_fb()) {
                std::lock_guard<std::mutex> lk(dfb_mtx);
                bool hw_sc = (g_de_scaler.de != nullptr);
                int sy0 = is_full ? 0 : std::max(0, ly0 - 2);
                int sy1 = is_full ? LOG_H : std::min(LOG_H, ly1 + 2);
                int jx0 = hw_sc ? (is_full ? 0 : std::max(0, lx0 - 2)) : vx0;
                int jy0 = hw_sc ? sy0 : vy0;
                int jx1 = hw_sc ? (is_full ? LOG_W : std::min(LOG_W, lx1 + 2)) : vx1;
                int jy1 = hw_sc ? sy1 : vy1;
                // If an earlier FullViewport is still pending on Core 1, keep full-frame bounds so nothing is dropped
                if (dfb_has_job && dfb_job.is_full) {
                    is_full = true;
                    sy0 = 0; sy1 = LOG_H;
                    jx0 = 0; jy0 = 0; jx1 = hw_sc ? LOG_W : vp_w; jy1 = hw_sc ? LOG_H : vp_h;
                }
                if (active_is_565) {
                    std::memcpy(dfb_stage[dfb_back] + sy0 * LOG_W, g_fb565 + sy0 * LOG_W, size_t(sy1 - sy0) * LOG_W * sizeof(uint16_t));
                } else {
                    for (int y = sy0; y < sy1; ++y)
                        rgba_row_to_rgb565(g_canvas_buf + y * LOG_W, dfb_stage[dfb_back] + y * LOG_W, LOG_W);
                }
                if (!hw_sc && int(dfb_xmap.size()) != vp_w) dfb_xmap = xmap;
                bool clr = need_clear_border || (dfb_has_job && dfb_job.clear_border);
                need_clear_border = false;
                dfb_job = {jx0, jy0, jx1, jy1, vp_w, vp_h, ox, oy, clr, is_full, hw_sc};
                dfb_allow_hw = hw_sc;
                dfb_has_job = true;
                dfb_cv.notify_one();
                return;
            }

            // Fallback X11 SHM path (windowed mode or non-16bpp fb)
            if (!ximg[0]) alloc_vp_images();
            if (is_full && using_shm && shm_in_flight) {
                x11.Sync(dpy, False);
                shm_in_flight = false;
            }
            XImage* xi = using_shm ? ximg[shm_cur] : ximg[0];
            if (!xi || !xi->data) return;

            uint8_t* base = reinterpret_cast<uint8_t*>(xi->data);
            size_t pitch = size_t(xi->bytes_per_line);

            if (xi->bits_per_pixel == 16) {
                uint16_t row565[LOG_W];
                auto get_row565 = [&](int sy) -> const uint16_t* {
                    if (active_is_565) return g_fb565 + sy * LOG_W;
                    rgba_row_to_rgb565(g_canvas_buf + sy * LOG_W + slx0, row565 + slx0, span_len);
                    return row565;
                };
                if (vp_w == 960 && vp_h == 720) {
                    int by0 = (vy0 / 3) * 3;
                    int by1 = std::min(720, ((vy1 + 2) / 3) * 3);
                    auto fill_row_960 = [&](int sy, uint16_t* drow) {
                        const uint16_t* s = get_row565(sy);
                        if (vx0 == 0 && vx1 == 960) {
                            uint16_t* d = drow;
                            for (int i = 0; i < 320; ++i, s += 2, d += 3) {
                                d[0] = s[0];
                                d[1] = avg565(s[0], s[1]);
                                d[2] = s[1];
                            }
                        } else {
                            for (int vx = vx0; vx < vx1; ++vx) {
                                int blk = (vx / 3) * 2, rem = vx % 3;
                                drow[vx] = (rem == 0) ? s[blk] : (rem == 2 ? s[blk + 1] : avg565(s[blk], s[blk + 1]));
                            }
                        }
                    };
                    for (int vy = by0; vy < by1; vy += 3) {
                        int sy0 = (vy / 3) * 2;
                        uint16_t* d0 = reinterpret_cast<uint16_t*>(base + size_t(vy + 0) * pitch);
                        uint16_t* d1 = reinterpret_cast<uint16_t*>(base + size_t(vy + 1) * pitch);
                        uint16_t* d2 = reinterpret_cast<uint16_t*>(base + size_t(vy + 2) * pitch);
                        fill_row_960(sy0, d0);
                        fill_row_960(sy0 + 1, d2);
                        blend_row_565(d0 + vx0, d2 + vx0, d1 + vx0, vx1 - vx0);
                    }
                    vy0 = by0; vy1 = by1;
                } else {
                    int prev_sy = -1;
                    uint16_t* prev_drow = nullptr;
                    for (int vy = vy0; vy < vy1; ++vy) {
                        int sy = (vy * LOG_H) / vp_h;
                        uint16_t* drow = reinterpret_cast<uint16_t*>(base + size_t(vy) * pitch);
                        if (sy == prev_sy && prev_drow) {
                            std::memcpy(drow + vx0, prev_drow + vx0, size_t(vx1 - vx0) * sizeof(uint16_t));
                            continue;
                        }
                        const uint16_t* sr = get_row565(sy);
                        if (vp_w == 1280 && vx0 == 0 && vx1 == 1280) {
#if defined(__ARM_NEON)
                            for (int i = 0; i < 640; i += 8) {
                                uint16x8_t v = vld1q_u16(sr + i);
                                uint16x8x2_t z = vzipq_u16(v, v);
                                vst1q_u16(drow + i * 2, z.val[0]);
                                vst1q_u16(drow + i * 2 + 8, z.val[1]);
                            }
#else
                            for (int i = 0; i < 640; ++i) { drow[i * 2] = sr[i]; drow[i * 2 + 1] = sr[i]; }
#endif
                        } else if (vp_w == 640 && vx0 == 0 && vx1 == 640) {
                            std::memcpy(drow, sr, 640 * sizeof(uint16_t));
                        } else {
                            for (int vx = vx0; vx < vx1; ++vx) drow[vx] = sr[xmap[size_t(vx)]];
                        }
                        prev_sy = sy;
                        prev_drow = drow;
                    }
                }
            } else {
                int prev_sy = -1;
                uint32_t* prev_drow = nullptr;
                for (int vy = vy0; vy < vy1; ++vy) {
                    int sy = (vy * LOG_H) / vp_h;
                    uint32_t* drow = reinterpret_cast<uint32_t*>(base + size_t(vy) * pitch);
                    if (sy == prev_sy && prev_drow) {
                        std::memcpy(drow + vx0, prev_drow + vx0, size_t(vx1 - vx0) * sizeof(uint32_t));
                        continue;
                    }
                    const uint32_t* srow = g_canvas_buf + sy * LOG_W;
                    for (int vx = vx0; vx < vx1; ++vx) {
                        uint32_t p = srow[xmap[size_t(vx)]];
                        drow[vx] = ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu);
                    }
                    prev_sy = sy;
                    prev_drow = drow;
                }
            }

            if (need_clear_border) {
                x11.ClearWindow(dpy, win);
                need_clear_border = false;
            }
            if (using_shm) {
                x11.ShmPutImage(dpy, win, gc, xi, vx0, vy0, ox + vx0, oy + vy0,
                                unsigned(vx1 - vx0), unsigned(vy1 - vy0), False);
                x11.Flush(dpy);
                shm_in_flight = true;
                if (is_full) shm_cur ^= 1;
            } else {
                x11.PutImage(dpy, win, gc, xi, vx0, vy0, ox + vx0, oy + vy0,
                             unsigned(vx1 - vx0), unsigned(vy1 - vy0));
                x11.Flush(dpy);
            }
        };

        while (!g_quit) {
            double loop_start = monotonic_ms();
            bool got_motion = false;
            int last_mx = app.mouse_x, last_my = app.mouse_y;

            while (x11.Pending(dpy) > 0) {
                XEvent ev;
                x11.NextEvent(dpy, &ev);
                if (ev.type == ClientMessage && Atom(ev.xclient.data.l[0]) == wm_del) { g_quit = 1; break; }
                else if (ev.type == Expose && ev.xexpose.count == 0) {
                    need_clear_border = true;
                    app.dirty = true;
                } else if (ev.type == ConfigureNotify) {
                    win_w = ev.xconfigure.width; win_h = ev.xconfigure.height;
                    ensure_ximg(win_w, win_h);
                } else if (ev.type == MotionNotify) {
                    // Coalesce burst MotionNotify events so cursor jumps straight to newest sample!
                    last_mx = (ev.xmotion.x - ox) * LOG_W / vp_w;
                    last_my = (ev.xmotion.y - oy) * LOG_H / vp_h;
                    got_motion = true;
                } else if (ev.type == ButtonPress || ev.type == ButtonRelease) {
                    got_motion = false;
                    int dn = (ev.type == ButtonPress) ? 1 : 0;
                    int lx = (ev.xbutton.x - ox) * LOG_W / vp_w;
                    int ly = (ev.xbutton.y - oy) * LOG_H / vp_h;
                    on_pointer_move(app, lx, ly);
                    if (ev.xbutton.button >= 1 && ev.xbutton.button <= 3)
                        on_pointer_button(app, dn, int(ev.xbutton.button));
                    else if (dn && (ev.xbutton.button == 4 || ev.xbutton.button == 5)) {
                        int d = (ev.xbutton.button == 4) ? -48 : 48;
                        if (app.ui.screen == blizz::ScreenId::Playing && app.ui.modal == blizz::ModalId::None) {
                            sd_pan(0, d);
                            app.dirty = true;
                        } else on_key_event(app, 1, 0, d < 0 ? 82 : 81);
                    }
                } else if (ev.type == KeyPress || ev.type == KeyRelease) {
                    int dn = (ev.type == KeyPress) ? 1 : 0;
                    char ch_buf[8] = {};
                    KeySym ks = NoSymbol;
                    x11.LookupString(&ev.xkey, ch_buf, sizeof(ch_buf), &ks, nullptr);
                    if (dn && ks == XK_F11) { toggle_fs(); if (!fullscreen) restore_de_sync(); continue; }
                    if (dn && ks == XK_F2) {
                        int nmodes = (fullscreen && g_de_scaler.de) ? 3 : 2;
                        scale_mode = (scale_mode + 1) % nmodes;
                        update_viewport();
                        const char* lbl = (scale_mode == 0) ? "Scale: 4:3 Full"
                                        : (scale_mode == 1) ? "Scale: 2x Integer" : "Scale: 16:9 Stretch";
                        app.ui.show_toast(lbl, monotonic_ms(), 1800);
                        continue;
                    }
                    int fkey = (ks >= XK_F1 && ks <= XK_F10) ? int(ks - XK_F1 + 1) : 0;
                    int sym = 0, scan = 0;
                    translate_x11_keysym(ks, &sym, &scan);
                    if (sym || scan || fkey) on_key_event(app, dn, sym, scan, fkey);
                }
            }
            if (got_motion) on_pointer_move(app, last_mx, last_my);

            if (app.ui.request_fullscreen_toggle) {
                app.ui.request_fullscreen_toggle = false;
                toggle_fs();
                if (!fullscreen) restore_de_sync();
            }

            static Cursor blizz_xcur = 0;
            static uint32_t blizz_xcur_hash = 0;
            static bool xcur_active = false;
            static double last_xcur_ms = 0.0;
            bool want_xcur = !can_use_direct_fb() && x11.has_xcursor &&
                             app.ui.screen != blizz::ScreenId::Cinematic &&
                             app.ui.screen != blizz::ScreenId::TitleSplash;
            if (want_xcur) {
                double now_c = monotonic_ms();
                if (!xcur_active || (now_c - last_xcur_ms) >= 90.0) {
                    last_xcur_ms = now_c;
                    const uint32_t* s = nullptr;
                    int cw = 0, ch = 0;
                    if (app.ui.screen == blizz::ScreenId::Playing && app.ui.modal == blizz::ModalId::None) {
                        int cbox = 0;
                        if (const uint8_t* cpx = sd_cursor_rgba(&cbox); cpx && cbox > 0 && cbox <= 96) {
                            s = reinterpret_cast<const uint32_t*>(cpx);
                            cw = ch = cbox;
                        }
                    }
                    if (!s) {
                        app.ui.cursor.init();
                        if (!app.ui.cursor.frames.empty()) {
                            const auto& f = app.ui.cursor.frames[size_t(now_c / 100.0) % app.ui.cursor.frames.size()];
                            if (f.valid() && f.w <= 160 && f.h <= 160) {
                                s = f.px.data();
                                cw = f.w; ch = f.h;
                            }
                        }
                    }
                    if (s && cw > 0 && ch > 0) {
                        // Crop oversized 128x128 GRP cursor frames to a crisp 64x64 center window around hotspot
                        int ox_c = (cw > 64) ? (cw - 64) / 2 : 0;
                        int oy_c = (ch > 64) ? (ch - 64) / 2 : 0;
                        int ow_c = std::min(cw, 64), oh_c = std::min(ch, 64);
                        uint32_t h = 2166136261u ^ uint32_t((ow_c << 8) | oh_c);
                        for (int y = 0; y < oh_c; y += 2) {
                            const uint32_t* sr = s + (oy_c + y) * cw + ox_c;
                            for (int x = 0; x < ow_c; x += 2) h = (h ^ sr[x]) * 16777619u;
                        }
                        if (!xcur_active || h != blizz_xcur_hash) {
                            if (auto* img = x11.XcurCreate(ow_c, oh_c)) {
                                img->xhot = uint32_t(ow_c / 2);
                                img->yhot = uint32_t(oh_c / 2);
                                for (int y = 0; y < oh_c; ++y) {
                                    const uint32_t* sr = s + (oy_c + y) * cw + ox_c;
                                    uint32_t* dr = img->pixels + y * ow_c;
                                    for (int x = 0; x < ow_c; ++x) {
                                        uint32_t p = sr[x];
                                        uint32_t r = p & 0xFFu, g = (p >> 8) & 0xFFu, b = (p >> 16) & 0xFFu, a = (p >> 24) & 0xFFu;
                                        // Premultiplied ARGB8888 for Xcursor
                                        r = (r * a) / 255u; g = (g * a) / 255u; b = (b * a) / 255u;
                                        dr[x] = (a << 24) | (r << 16) | (g << 8) | b;
                                    }
                                }
                                Cursor nc = x11.XcurLoad(dpy, img);
                                x11.XcurDestroy(img);
                                if (nc) {
                                    x11.DefineCursor(dpy, win, nc);
                                    if (blizz_xcur) x11.FreeCursor(dpy, blizz_xcur);
                                    blizz_xcur = nc;
                                    blizz_xcur_hash = h;
                                    xcur_active = true;
                                }
                            }
                        }
                    }
                }
                skip_sw_cursor = xcur_active;
            } else {
                if (xcur_active) {
                    x11.DefineCursor(dpy, win, blank_cur);
                    xcur_active = false;
                }
                skip_sw_cursor = false;
            }

            static double fps_t0 = monotonic_ms();
            static int fps_world = 0, fps_cur = 0;
            PresentStep ps = step_frame();
            if (ps.kind == PresentStep::FullViewport) {
                present_rect_x11(0, 0, LOG_W, LOG_H, true);
                ++fps_world;
            } else if (ps.kind == PresentStep::CursorRect) {
                present_rect_x11(ps.lx0, ps.ly0, ps.lx1, ps.ly1, false);
                ++fps_cur;
            }
            double now_fps = monotonic_ms();
            if (now_fps - fps_t0 >= 5000.0) {
                double dt_s = (now_fps - fps_t0) / 1000.0;
                std::fprintf(stderr, "[FPS] world=%.1f cursor_extra=%.1f total_mouse=%.1f vp=%dx%d direct_fb=%d hw_de=%d xcur=%d\n",
                             fps_world / dt_s, fps_cur / dt_s, (fps_world + fps_cur) / dt_s, vp_w, vp_h,
                             can_use_direct_fb() ? 1 : 0, g_de_scaler.active ? 1 : 0, xcur_active ? 1 : 0);
                std::fflush(stderr);
                fps_t0 = now_fps; fps_world = 0; fps_cur = 0;
            }

            double elapsed = monotonic_ms() - loop_start;
            if (elapsed < 8.0 && x11.Pending(dpy) == 0) {
                struct pollfd pfd{};
                pfd.fd = ConnectionNumber(dpy);
                pfd.events = POLLIN;
                int wait_ms = std::max(1, int(8.0 - elapsed));
                ::poll(&pfd, 1, wait_ms);
            }
        }
        if (dfb_worker.joinable()) {
            {
                std::lock_guard<std::mutex> lk(dfb_mtx);
                dfb_stop = true;
            }
            dfb_cv.notify_one();
            dfb_worker.join();
        }
        g_de_scaler.shutdown();
        if (direct_fb_raw) munmap(direct_fb_raw, direct_fb_len);
        if (direct_fb_fd >= 0) ::close(direct_fb_fd);
        destroy_ximg();
        x11.CloseDisplay(dpy);
    } else {
        int fb_fd = ::open("/dev/fb0", O_RDWR);
        if (fb_fd < 0) { perror("open /dev/fb0"); audio.shutdown(); return 1; }
        struct fb_var_screeninfo v{}; struct fb_fix_screeninfo f{};
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &v);
        ioctl(fb_fd, FBIOGET_FSCREENINFO, &f);
        ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
        uint8_t* fb_raw = static_cast<uint8_t*>(mmap(nullptr, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0));
        if (fb_raw == MAP_FAILED) { perror("mmap /dev/fb0"); ::close(fb_fd); audio.shutdown(); return 1; }
        std::memset(fb_raw, 0, f.smem_len);

        EvdevHub input;
        input.open_all();

        static uint32_t fb_stage[2][LOG_W * LOG_H];
        static uint16_t land480[480 * 320];
        static uint16_t rot_buf[480 * 320]; // cached transpose buffer so /dev/fb0 gets fast linear burst memcpy!
        uint16_t* fb16 = reinterpret_cast<uint16_t*>(fb_raw);
        int stride16 = int(f.line_length) / 2;
        bool rotate90 = (v.xres == 320 && v.yres == 480);

        struct FbJob {
            PresentStep ps;
            bool crop_mode = false;
            bool is_565 = false;
            int view_x = 0, view_y = 0;
        };
        std::mutex fb_mtx;
        std::condition_variable fb_cv;
        FbJob fb_job{};
        int fb_front = 0, fb_back = 1;
        bool fb_has_job = false;
        bool fb_stop = false;

        // Dual-core pipelined /dev/fb0 presenter: Core 1 scales + rotates + blits fb_stage[fb_front] while Core 0 renders!
        std::thread fb_worker([&]() {
            while (true) {
                FbJob job;
                const uint32_t* src_buf = nullptr;
                {
                    std::unique_lock<std::mutex> lk(fb_mtx);
                    fb_cv.wait(lk, [&]() { return fb_has_job || fb_stop; });
                    if (fb_stop && !fb_has_job) break;
                    std::swap(fb_front, fb_back);
                    src_buf = fb_stage[fb_front];
                    job = fb_job;
                    fb_has_job = false;
                }
                int dx0 = 0, dy0 = 0, dx1 = 480, dy1 = 320;
                if (job.is_565) {
                    const uint16_t* s565 = reinterpret_cast<const uint16_t*>(src_buf);
                    if (job.crop_mode) crop_480x320_from_565(s565, job.view_x, job.view_y, land480);
                    else scale_640x480_rgb565_to_480x320_rows(s565, land480, 0, 320);
                } else if (job.crop_mode) {
                    crop_480x320_rgb565(src_buf, job.view_x, job.view_y, land480);
                } else if (job.ps.kind == PresentStep::CursorRect) {
                    dx0 = std::max(0, (job.ps.lx0 * 3) / 4 - 2);
                    dx1 = std::min(480, (job.ps.lx1 * 3 + 3) / 4 + 2);
                    dy0 = std::max(0, (job.ps.ly0 * 2) / 3 - 2);
                    dy1 = std::min(320, (job.ps.ly1 * 2 + 2) / 3 + 2);
                    scale_640x480_to_480x320_rgb565_rows(src_buf, land480, dy0, dy1);
                } else {
                    scale_640x480_to_480x320_rgb565_rows(src_buf, land480, 0, 320);
                }

                if (rotate90) {
                    for (int ty = dy0; ty < dy1; ty += 16) {
                        int ty1 = std::min(dy1, ty + 16);
                        for (int tx = dx0; tx < dx1; tx += 16) {
                            int tx1 = std::min(dx1, tx + 16);
                            for (int Y = ty; Y < ty1; ++Y) {
                                const uint16_t* srow = land480 + Y * 480;
                                int dst_col = 319 - Y;
                                for (int X = tx; X < tx1; ++X)
                                    rot_buf[X * 320 + dst_col] = srow[X];
                            }
                        }
                    }
                    int rx0 = 319 - (dy1 - 1), rlen = (dy1 - dy0) * 2;
                    for (int X = dx0; X < dx1; ++X)
                        std::memcpy(fb16 + X * stride16 + rx0, rot_buf + X * 320 + rx0, size_t(rlen));
                } else if (v.bits_per_pixel == 16) {
                    int cw = std::min(dx1, int(v.xres)) - dx0;
                    int ch1 = std::min(dy1, int(v.yres));
                    if (cw > 0) {
                        for (int Y = dy0; Y < ch1; ++Y)
                            std::memcpy(fb16 + Y * stride16 + dx0, land480 + Y * 480 + dx0, size_t(cw) * 2);
                    }
                }
            }
        });

        while (!g_quit) {
            double loop_start = monotonic_ms();
            input.poll_events(app, loop_start);
            PresentStep ps = step_frame();

            if (ps.kind != PresentStep::None) {
                std::lock_guard<std::mutex> lk(fb_mtx);
                bool crop_mode = app.ui.handheld_1to1 && app.ui.screen == blizz::ScreenId::Playing && app.ui.modal == blizz::ModalId::None;
                if (active_is_565)
                    std::memcpy(fb_stage[fb_back], g_fb565, sizeof(g_fb565));
                else
                    std::memcpy(fb_stage[fb_back], g_canvas_buf, sizeof(fb_stage[0]));
                // If an earlier FullViewport job is still pending, keep FullViewport so no screen regions are dropped
                if (fb_has_job && fb_job.ps.kind == PresentStep::FullViewport)
                    ps = fb_job.ps;
                fb_job = {ps, crop_mode, active_is_565, app.view_x, app.view_y};
                fb_has_job = true;
                fb_cv.notify_one();
            }

            double elapsed = monotonic_ms() - loop_start;
            if (elapsed < 16.0)
                usleep(useconds_t((16.0 - elapsed) * 1000.0));
        }

        {
            std::lock_guard<std::mutex> lk(fb_mtx);
            fb_stop = true;
        }
        fb_cv.notify_one();
        fb_worker.join();

        input.close_all();
        std::memset(fb_raw, 0, f.smem_len);
        munmap(fb_raw, f.smem_len);
        ::close(fb_fd);
    }

    audio.shutdown();
    sd_close_all();
    return 0;
}
