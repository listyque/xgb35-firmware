#pragma once
// XGB35 Hardware Platform Layer: X11 Desktop (MIT-SHM) + 480x320 /dev/fb0 (ST7796S) + ALSA Audio + Evdev.

#include "core.h"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <mutex>
#include <sound/asound.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#ifdef None
#undef None
#endif
#ifdef Status
#undef Status
#endif

namespace jf {

inline uint16_t rgba_to_rgb565(uint32_t p) {
    return uint16_t((((p & 0xFFu) >> 3) << 11) | ((((p >> 8) & 0xFFu) >> 2) << 5) | (((p >> 16) & 0xFFu) >> 3));
}

inline void rgba_row_to_rgb565(const uint32_t* srow, uint16_t* out, int count) {
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

// --- ATM7051H + ATC2603C Safe V/F Performance Guard (1.20V DCDC1 before 1200 MHz COREPLL) ---

struct CpuBoostGuard {
    static constexpr const char* kRegDbg = "/sys/devices/b0188000.i2c/i2c-2/2-0065/reg_dbg";
    std::string old_gov, old_min, old_max;
    uint32_t old_pll = 0;
    bool active = false;
    bool pll_boosted = false;

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
    static bool set_dcdc1_mv(int mv) {
        int sel = std::max(12, std::min(20, (mv - 700) / 25));
        uint16_t reg11 = uint16_t(0xE04Fu | (uint16_t(sel) << 7));
        char cmd[24];
        int n = std::snprintf(cmd, sizeof(cmd), "11=%04x\n", reg11);
        int fd = ::open(kRegDbg, O_WRONLY);
        if (fd < 0) return false;
        ssize_t w = ::write(fd, cmd, size_t(n));
        ::close(fd);
        return w == n;
    }
    static bool rw_corepll(bool write, uint32_t* val) {
        int fd = ::open("/dev/mem", write ? (O_RDWR | O_SYNC) : (O_RDONLY | O_SYNC));
        if (fd < 0) return false;
        void* m = mmap(nullptr, 4096, write ? (PROT_READ | PROT_WRITE) : PROT_READ, MAP_SHARED, fd, 0xb0150000u);
        ::close(fd);
        if (m == MAP_FAILED) return false;
        volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(m);
        uint32_t cur = *reg;
        bool ok = ((cur & 0xFFFFFF00u) == 0x00000300u);
        if (ok) {
            if (write) *reg = *val;
            else *val = cur;
        }
        munmap(m, 4096);
        return ok;
    }

    void engage() {
        if (active) return;
        active = true;
        old_gov = read_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
        old_min = read_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq");
        old_max = read_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq");
        write_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "performance\n");
        write_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq", "1200000\n");
        write_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq", "1200000\n");
        if (rw_corepll(false, &old_pll) && (old_pll & 0xFFu) < 0x64u && set_dcdc1_mv(1200)) {
            usleep(8000);
            uint32_t boost = (old_pll & ~0xFFu) | 0x64u;
            pll_boosted = rw_corepll(true, &boost);
        }
    }

    void release() {
        if (!active) return;
        active = false;
        if (pll_boosted) {
            rw_corepll(true, &old_pll);
            usleep(5000);
            set_dcdc1_mv(1150);
            pll_boosted = false;
        }
        if (!old_min.empty()) write_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq", (old_min + "\n").c_str());
        if (!old_max.empty()) write_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq", (old_max + "\n").c_str());
        if (!old_gov.empty()) write_sys("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", (old_gov + "\n").c_str());
    }

    ~CpuBoostGuard() { release(); }
};

// --- Direct ALSA S32_LE Stereo Ring-Buffered Audio Sink ---

class AlsaAudioSink {
    static constexpr size_t kRingFrames = 65536; // ~1.48s stereo buffer at 44100 Hz
    std::vector<int16_t> ring_;
    std::atomic<uint64_t> rd_frames_{0};
    std::atomic<uint64_t> wr_frames_{0};
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> sw_vol_pct_{100};
    std::thread worker_;
    int pcm_fd_ = -1;
    unsigned sample_rate_ = 44100;
    bool speaker_amp_ = false;

public:
    AlsaAudioSink() : ring_(kRingFrames * 2, 0) {}
    ~AlsaAudioSink() { shutdown(); }

    unsigned sample_rate() const { return sample_rate_; }
    bool is_open() const { return pcm_fd_ >= 0; }

    void set_paused(bool p) { paused_.store(p, std::memory_order_relaxed); }
    void set_sw_volume(int pct) { sw_vol_pct_.store(std::max(0, std::min(150, pct)), std::memory_order_relaxed); }

    void flush() {
        uint64_t w = wr_frames_.load(std::memory_order_acquire);
        rd_frames_.store(w, std::memory_order_release);
    }

    size_t queued_frames() const {
        uint64_t w = wr_frames_.load(std::memory_order_acquire);
        uint64_t r = rd_frames_.load(std::memory_order_acquire);
        return (w > r) ? size_t(w - r) : 0;
    }

    size_t free_frames() const {
        size_t q = queued_frames();
        return (q < kRingFrames - 512) ? (kRingFrames - 512 - q) : 0;
    }

    size_t push_s16_stereo(const int16_t* frames, size_t count) {
        uint64_t w = wr_frames_.load(std::memory_order_relaxed);
        uint64_t r = rd_frames_.load(std::memory_order_acquire);
        size_t used = (w >= r) ? size_t(w - r) : 0;
        size_t avail = (used < kRingFrames - 256) ? (kRingFrames - 256 - used) : 0;
        size_t to_write = std::min(count, avail);
        for (size_t i = 0; i < to_write; ++i) {
            size_t slot = size_t((w + i) % kRingFrames) * 2;
            ring_[slot + 0] = frames[i * 2 + 0];
            ring_[slot + 1] = frames[i * 2 + 1];
        }
        wr_frames_.store(w + to_write, std::memory_order_release);
        return to_write;
    }

    // audio_mode: 0 = Auto (HDMI if X11 else Speaker, with fallback), 1 = HDMI, 2 = Speaker
    void start(int audio_mode, bool x11_active) {
        shutdown();
        bool try_hdmi_first = (audio_mode == 1) || (audio_mode == 0 && x11_active);
        if (try_hdmi_first) pcm_fd_ = ::open("/dev/snd/pcmC1D0p", O_RDWR);
        if (pcm_fd_ < 0 && audio_mode != 1) {
            pcm_fd_ = ::open("/dev/snd/pcmC0D0p", O_RDWR);
            if (pcm_fd_ >= 0) {
                (void)::system("LD_LIBRARY_PATH=/lib /bin/tinymix 35 1 >/dev/null 2>&1");
                speaker_amp_ = true;
            }
        }
        if (pcm_fd_ < 0 && !try_hdmi_first)
            pcm_fd_ = ::open("/dev/snd/pcmC1D0p", O_RDWR);
        if (pcm_fd_ < 0) return;

        for (unsigned cand_rate : {44100u, 48000u, 22050u}) {
            struct snd_pcm_hw_params p;
            std::memset(&p, 0, sizeof(p));
            for (int i = 0; i < SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1; ++i) {
                p.intervals[i].min = 0;
                p.intervals[i].max = ~0U;
            }
            for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; ++i)
                std::memset(&p.masks[i], 0xFF, sizeof(p.masks[i]));

            p.masks[SNDRV_PCM_HW_PARAM_ACCESS - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[0] = 1U << SNDRV_PCM_ACCESS_RW_INTERLEAVED;
            p.masks[SNDRV_PCM_HW_PARAM_FORMAT - SNDRV_PCM_HW_PARAM_FIRST_MASK].bits[0] = 1U << SNDRV_PCM_FORMAT_S32_LE;
            p.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 2;
            p.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 2;
            p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = cand_rate;
            p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = cand_rate;
            p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 512;
            p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 1024;
            p.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].min = 2048;
            p.intervals[SNDRV_PCM_HW_PARAM_BUFFER_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL].max = 4096;

            if (ioctl(pcm_fd_, SNDRV_PCM_IOCTL_HW_PARAMS, &p) == 0 &&
                ioctl(pcm_fd_, SNDRV_PCM_IOCTL_PREPARE, 0) == 0) {
                sample_rate_ = cand_rate;
                break;
            }
            if (cand_rate == 22050u) {
                ::close(pcm_fd_);
                pcm_fd_ = -1;
                return;
            }
        }

        rd_frames_.store(0);
        wr_frames_.store(0);
        stop_.store(false);
        worker_ = std::thread([this]() {
            int32_t s32[512 * 2];
            while (!stop_.load(std::memory_order_relaxed)) {
                uint64_t r = rd_frames_.load(std::memory_order_relaxed);
                uint64_t w = wr_frames_.load(std::memory_order_acquire);
                bool paused = paused_.load(std::memory_order_relaxed);
                int vol = sw_vol_pct_.load(std::memory_order_relaxed);

                if (paused || r >= w) {
                    std::memset(s32, 0, sizeof(s32));
                } else {
                    size_t take = std::min<size_t>(512, size_t(w - r));
                    for (size_t i = 0; i < take; ++i) {
                        size_t slot = size_t((r + i) % kRingFrames) * 2;
                        int32_t l = (int32_t(ring_[slot + 0]) * vol) / 100;
                        int32_t rt = (int32_t(ring_[slot + 1]) * vol) / 100;
                        l = std::max(-32768, std::min(32767, l));
                        rt = std::max(-32768, std::min(32767, rt));
                        s32[i * 2 + 0] = l << 16;
                        s32[i * 2 + 1] = rt << 16;
                    }
                    if (take < 512)
                        std::memset(s32 + take * 2, 0, (512 - take) * 2 * sizeof(int32_t));
                    rd_frames_.store(r + take, std::memory_order_release);
                }

                struct snd_xferi x{};
                x.buf = s32;
                x.frames = 512;
                if (ioctl(pcm_fd_, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0) {
                    if (errno == EPIPE) ioctl(pcm_fd_, SNDRV_PCM_IOCTL_PREPARE, 0);
                    else usleep(8000);
                }
            }
        });
    }

    void shutdown() {
        stop_.store(true);
        if (worker_.joinable()) worker_.join();
        if (pcm_fd_ >= 0) {
            ::close(pcm_fd_);
            pcm_fd_ = -1;
        }
        if (speaker_amp_) {
            (void)::system("LD_LIBRARY_PATH=/lib /bin/tinymix 35 0 >/dev/null 2>&1");
            speaker_amp_ = false;
        }
    }
};

// --- Unified Input Events (Gamepad + Keyboard + Mouse, zero touch-gesture traps) ---

enum class NavAction {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Confirm,    // Enter / Gamepad A
    Back,       // Escape / Gamepad B
    ActionX,    // Gamepad X / Download / Delete shortcut
    ActionY,    // Gamepad Y / Search shortcut
    PrevTab,    // L1 / [
    NextTab,    // R1 / ]
    PageUp,     // L2 / PgUp
    PageDown,   // R2 / PgDn
    PlayPause,  // Space / START
    ToggleFs,   // F11
    Quit,
};

struct InputState {
    int mouse_x = 240;
    int mouse_y = 160;
    bool mouse_visible = false;
    bool lmb_clicked = false;
    bool rmb_clicked = false;
    int wheel_delta = 0;
    std::vector<NavAction> actions;
    std::string typed_utf8;
    bool backspace_pressed = false;

    void clear_frame() {
        lmb_clicked = false;
        rmb_clicked = false;
        wheel_delta = 0;
        actions.clear();
        typed_utf8.clear();
        backspace_pressed = false;
    }
};

// --- Dynamic X11 + MIT-SHM Loader ---

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
    int (*SetWMProtocols)(Display*, Window, Atom*, int) = nullptr;
    int (*Pending)(Display*) = nullptr;
    int (*NextEvent)(Display*, XEvent*) = nullptr;
    int (*LookupString)(XKeyEvent*, char*, int, KeySym*, XComposeStatus*) = nullptr;
    XImage* (*CreateImage)(Display*, Visual*, unsigned, int, int, char*, unsigned, unsigned, int, int) = nullptr;
    int (*PutImage)(Display*, Drawable, GC, XImage*, int, int, int, int, unsigned, unsigned) = nullptr;
    int (*Flush)(Display*) = nullptr;
    int (*Sync)(Display*, Bool) = nullptr;
    int (*MoveResizeWindow)(Display*, Window, int, int, unsigned, unsigned) = nullptr;
    int (*SendEvent)(Display*, Window, Bool, long, XEvent*) = nullptr;

    Bool (*ShmQueryExtension)(Display*) = nullptr;
    XImage* (*ShmCreateImage)(Display*, Visual*, unsigned, int, char*, XShmSegmentInfo*, unsigned, unsigned) = nullptr;
    int (*ShmAttach)(Display*, XShmSegmentInfo*) = nullptr;
    int (*ShmDetach)(Display*, XShmSegmentInfo*) = nullptr;
    int (*ShmPutImage)(Display*, Drawable, GC, XImage*, int, int, int, int, unsigned, unsigned, Bool) = nullptr;
    bool has_shm = false;

    bool load() {
        const char* paths[] = {"libX11.so.6", "/usr/lib/libX11.so.6", "/desktop/usr/lib/libX11.so.6"};
        for (const char* p : paths) { lib = dlopen(p, RTLD_LAZY); if (lib) break; }
        if (!lib) return false;
        #define JF_SYM(f, s) f = reinterpret_cast<decltype(f)>(dlsym(lib, s)); if (!f) return false
        JF_SYM(OpenDisplay, "XOpenDisplay");
        JF_SYM(CloseDisplay, "XCloseDisplay");
        JF_SYM(CreateSimpleWindow, "XCreateSimpleWindow");
        JF_SYM(SelectInput, "XSelectInput");
        JF_SYM(MapWindow, "XMapWindow");
        JF_SYM(StoreName, "XStoreName");
        JF_SYM(InternAtom, "XInternAtom");
        JF_SYM(SetWMProtocols, "XSetWMProtocols");
        JF_SYM(Pending, "XPending");
        JF_SYM(NextEvent, "XNextEvent");
        JF_SYM(LookupString, "XLookupString");
        JF_SYM(CreateImage, "XCreateImage");
        JF_SYM(PutImage, "XPutImage");
        JF_SYM(Flush, "XFlush");
        JF_SYM(Sync, "XSync");
        JF_SYM(MoveResizeWindow, "XMoveResizeWindow");
        JF_SYM(SendEvent, "XSendEvent");
        #undef JF_SYM

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
        return true;
    }
};

// --- Evdev Handheld Gamepad + USB Mouse/Keyboard Hub ---

struct EvdevHub {
    struct Dev { int fd; int idx; bool is_gamepad; };
    struct AxisCal { int min_v = 0, max_v = 4095, center = 2048, raw = 2048; };
    std::vector<Dev> devs;
    AxisCal ax[4];
    double last_scan_ms = -5000.0;
    float cur_rx = 0, cur_ry = 0;
    bool btn_sel = false, btn_start = false, shift_held = false;
    double next_stick_nav_ms = 0;

    static void init_axis(int fd, int code, AxisCal& a) {
        struct input_absinfo ai{};
        if (ioctl(fd, EVIOCGABS(code), &ai) == 0 && ai.maximum > ai.minimum) {
            a.min_v = ai.minimum;
            a.max_v = ai.maximum;
            a.center = (ai.minimum + ai.maximum) / 2;
            a.raw = (ai.value >= ai.minimum && ai.value <= ai.maximum) ? ai.value : a.center;
        }
    }

    static float axis_norm(const AxisCal& a) {
        float half = std::max(1.0f, float(a.max_v - a.min_v) * 0.5f);
        return std::max(-1.0f, std::min(1.0f, float(a.raw - a.center) / half));
    }

    bool has_idx(int idx) const {
        for (const auto& d : devs) if (d.idx == idx && d.fd >= 0) return true;
        return false;
    }

    void rescan_hotplug(double now_ms) {
        if (now_ms - last_scan_ms < 1500.0) return;
        last_scan_ms = now_ms;
        for (int i = 0; i <= 16; ++i) {
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

    static char evdev_ascii(int code, bool shift) {
        switch (code) {
        case KEY_1: return shift ? '!' : '1';
        case KEY_2: return shift ? '@' : '2';
        case KEY_3: return shift ? '#' : '3';
        case KEY_4: return shift ? '$' : '4';
        case KEY_5: return shift ? '%' : '5';
        case KEY_6: return shift ? '^' : '6';
        case KEY_7: return shift ? '&' : '7';
        case KEY_8: return shift ? '*' : '8';
        case KEY_9: return shift ? '(' : '9';
        case KEY_0: return shift ? ')' : '0';
        case KEY_MINUS: return shift ? '_' : '-';
        case KEY_EQUAL: return shift ? '+' : '=';
        case KEY_DOT: return shift ? '>' : '.';
        case KEY_COMMA: return shift ? '<' : ',';
        case KEY_SLASH: return shift ? '?' : '/';
        case KEY_SEMICOLON: return shift ? ':' : ';';
        case KEY_SPACE: return ' ';
        case KEY_Q: return shift ? 'Q' : 'q';
        case KEY_W: return shift ? 'W' : 'w';
        case KEY_E: return shift ? 'E' : 'e';
        case KEY_R: return shift ? 'R' : 'r';
        case KEY_T: return shift ? 'T' : 't';
        case KEY_Y: return shift ? 'Y' : 'y';
        case KEY_U: return shift ? 'U' : 'u';
        case KEY_I: return shift ? 'I' : 'i';
        case KEY_O: return shift ? 'O' : 'o';
        case KEY_P: return shift ? 'P' : 'p';
        case KEY_A: return shift ? 'A' : 'a';
        case KEY_S: return shift ? 'S' : 's';
        case KEY_D: return shift ? 'D' : 'd';
        case KEY_F: return shift ? 'F' : 'f';
        case KEY_G: return shift ? 'G' : 'g';
        case KEY_H: return shift ? 'H' : 'h';
        case KEY_J: return shift ? 'J' : 'j';
        case KEY_K: return shift ? 'K' : 'k';
        case KEY_L: return shift ? 'L' : 'l';
        case KEY_Z: return shift ? 'Z' : 'z';
        case KEY_X: return shift ? 'X' : 'x';
        case KEY_C: return shift ? 'C' : 'c';
        case KEY_V: return shift ? 'V' : 'v';
        case KEY_B: return shift ? 'B' : 'b';
        case KEY_N: return shift ? 'N' : 'n';
        case KEY_M: return shift ? 'M' : 'm';
        default: return 0;
        }
    }

    void poll(InputState& inp, int screen_w, int screen_h, int& hw_vol, double now_ms) {
        rescan_hotplug(now_ms);
        struct input_event ev[32];
        int rel_dx = 0, rel_dy = 0;

        for (size_t di = 0; di < devs.size();) {
            auto& d = devs[di];
            int n;
            bool gone = false;
            while ((n = ::read(d.fd, ev, sizeof(ev))) > 0) {
                int cnt = n / int(sizeof(ev[0]));
                for (int j = 0; j < cnt; ++j) {
                    const auto& e = ev[j];
                    if (e.type == EV_REL) {
                        if (e.code == REL_X) rel_dx += e.value;
                        else if (e.code == REL_Y) rel_dy += e.value;
                        else if (e.code == REL_WHEEL) inp.wheel_delta += e.value;
                    } else if (e.type == EV_ABS) {
                        if (e.code == 0) ax[0].raw = e.value;
                        else if (e.code == 1) ax[1].raw = e.value;
                        else if (e.code == 3) ax[2].raw = e.value;
                        else if (e.code == 4) ax[3].raw = e.value;
                    } else if (e.type == EV_KEY) {
                        if (e.code == KEY_LEFTSHIFT || e.code == KEY_RIGHTSHIFT) {
                            shift_held = (e.value != 0);
                            continue;
                        }
                        if (e.value == 0) {
                            if (d.is_gamepad) {
                                if (e.code == 314) btn_sel = false;
                                if (e.code == 315) btn_start = false;
                            }
                            continue;
                        }
                        if (e.code == BTN_LEFT) { inp.mouse_visible = true; inp.lmb_clicked = true; continue; }
                        if (e.code == BTN_RIGHT) { inp.mouse_visible = true; inp.rmb_clicked = true; continue; }

                        if (d.is_gamepad) {
                            switch (e.code) {
                            case 103: inp.actions.push_back(NavAction::Up); break;
                            case 108: inp.actions.push_back(NavAction::Down); break;
                            case 105: inp.actions.push_back(NavAction::Left); break;
                            case 106: inp.actions.push_back(NavAction::Right); break;
                            case 352: // A
                                if (inp.mouse_visible) inp.lmb_clicked = true;
                                else inp.actions.push_back(NavAction::Confirm);
                                break;
                            case 158: // B
                                inp.mouse_visible = false;
                                inp.actions.push_back(NavAction::Back);
                                break;
                            case 139: inp.actions.push_back(NavAction::ActionX); break; // X
                            case 308: inp.actions.push_back(NavAction::ActionY); break; // Y
                            case 412: inp.actions.push_back(NavAction::PrevTab); break; // L1
                            case 407: inp.actions.push_back(NavAction::NextTab); break; // R1
                            case 312: inp.actions.push_back(NavAction::PageUp); break;  // L2
                            case 313: inp.actions.push_back(NavAction::PageDown); break;// R2
                            case 314: btn_sel = true; inp.mouse_visible = !inp.mouse_visible; break; // SELECT toggles pointer mode
                            case 315: btn_start = true; inp.actions.push_back(NavAction::PlayPause); break; // START
                            case 115: {
                                hw_vol = std::min(40, hw_vol + 4);
                                char c[80]; std::snprintf(c, sizeof(c), "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1", hw_vol);
                                (void)::system(c);
                                break;
                            }
                            case 114: {
                                hw_vol = std::max(0, hw_vol - 4);
                                char c[80]; std::snprintf(c, sizeof(c), "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1", hw_vol);
                                (void)::system(c);
                                break;
                            }
                            case 174: inp.actions.push_back(NavAction::Quit); break;
                            }
                            if (btn_sel && btn_start) inp.actions.push_back(NavAction::Quit);
                        } else {
                            // USB Keyboard
                            switch (e.code) {
                            case KEY_UP:        inp.actions.push_back(NavAction::Up); break;
                            case KEY_DOWN:      inp.actions.push_back(NavAction::Down); break;
                            case KEY_LEFT:      inp.actions.push_back(NavAction::Left); break;
                            case KEY_RIGHT:     inp.actions.push_back(NavAction::Right); break;
                            case KEY_ENTER:
                            case KEY_KPENTER:   inp.actions.push_back(NavAction::Confirm); break;
                            case KEY_ESC:       inp.actions.push_back(NavAction::Back); break;
                            case KEY_TAB:       inp.actions.push_back(shift_held ? NavAction::PrevTab : NavAction::NextTab); break;
                            case KEY_PAGEUP:    inp.actions.push_back(NavAction::PageUp); break;
                            case KEY_PAGEDOWN:  inp.actions.push_back(NavAction::PageDown); break;
                            case KEY_F11:       inp.actions.push_back(NavAction::ToggleFs); break;
                            case KEY_BACKSPACE: inp.backspace_pressed = true; break;
                            default: {
                                char ch = evdev_ascii(e.code, shift_held);
                                if (ch) {
                                    inp.typed_utf8.push_back(ch);
                                    if (ch == ' ') inp.actions.push_back(NavAction::PlayPause);
                                }
                                break;
                            }
                            }
                        }
                    }
                }
            }
            if (n < 0 && errno == ENODEV) gone = true;
            if (gone) { ::close(d.fd); devs.erase(devs.begin() + long(di)); }
            else ++di;
        }

        if (rel_dx != 0 || rel_dy != 0) {
            inp.mouse_visible = true;
            inp.mouse_x = std::max(0, std::min(screen_w - 1, inp.mouse_x + rel_dx));
            inp.mouse_y = std::max(0, std::min(screen_h - 1, inp.mouse_y + rel_dy));
        }

        // Left analog stick: discrete D-Pad navigation by default, or smooth pointer when SELECT toggled pointer mode
        float lx = axis_norm(ax[0]), ly = axis_norm(ax[1]);
        if (inp.mouse_visible) {
            if (std::fabs(lx) > 0.16f) cur_rx += lx * 7.0f;
            if (std::fabs(ly) > 0.16f) cur_ry += ly * 7.0f;
            int dx = int(cur_rx), dy = int(cur_ry);
            if (dx || dy) {
                cur_rx -= float(dx); cur_ry -= float(dy);
                inp.mouse_x = std::max(0, std::min(screen_w - 1, inp.mouse_x + dx));
                inp.mouse_y = std::max(0, std::min(screen_h - 1, inp.mouse_y + dy));
            }
        } else if (now_ms >= next_stick_nav_ms) {
            if (ly < -0.55f) { inp.actions.push_back(NavAction::Up); next_stick_nav_ms = now_ms + 190.0; }
            else if (ly > 0.55f) { inp.actions.push_back(NavAction::Down); next_stick_nav_ms = now_ms + 190.0; }
            else if (lx < -0.55f) { inp.actions.push_back(NavAction::Left); next_stick_nav_ms = now_ms + 190.0; }
            else if (lx > 0.55f) { inp.actions.push_back(NavAction::Right); next_stick_nav_ms = now_ms + 190.0; }
        }
    }
};

} // namespace jf
