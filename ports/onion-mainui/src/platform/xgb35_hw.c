/* SPDX-License-Identifier: GPL-3.0-only
 * XGB35 hardware backend for Open MainUI:
 * - Anti-aliased 640x480 XRGB8888 -> 480x320 landscape -> rotated 90 deg 320x480 RGB565 /dev/fb0
 * - Direct Linux evdev reader (/dev/input/event1..7 + auto-zeroing ADC joystick) -> SDL events
 * - ATC2603C battery %, USB charging detect, PWM backlight (90..900), and ALSA speaker volume
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "platform/xgb35_hw.h"
#include "platform/menu_button.h"
#include "shmvar/shmvar.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <unistd.h>

#define LAND_W 480
#define LAND_H 320
#define CONTENT_W 427                       /* 640 * 320 / 480 */
#define CONTENT_X0 ((LAND_W - CONTENT_W) / 2)

static int fb_fd = -1;
static uint16_t *fb_mem = NULL;
static size_t fb_map_len = 0;
static struct fb_var_screeninfo fb_var;
static int fb_xres = 320, fb_yres = 480, fb_stride16 = 320;
static int fb_page = 0, fb_pages = 1;
static volatile uint32_t *de_regs = NULL;

static uint16_t x_i0[LAND_W], x_i1[LAND_W];
static uint8_t  x_w1[LAND_W];
static uint16_t y_i0[LAND_H], y_i1[LAND_H];
static uint8_t  y_w1[LAND_H];
static bool tables_ready = false;

static pthread_t input_tid;
static bool input_running = false;
static int wake_pipe[2] = { -1, -1 };

static int shm_slots[MONITOR_VALUE_COUNT] = {
    [MONITOR_VOLUME] = 14,
    [MONITOR_BRIGHTNESS] = 7,
    [MONITOR_BGM_VOLUME] = 8,
    [MONITOR_HIBERNATE_DELAY] = 0,
    [MONITOR_LUMINATION] = 7,
    [MONITOR_HUE] = 10,
    [MONITOR_SATURATION] = 10,
    [MONITOR_CONTRAST] = 10,
    [MONITOR_AUDIOFIX] = 1,
    [MONITOR_MUTE] = 0,
    [MONITOR_VOLUME_CHANGED] = 0,
};

static void apply_hw_volume(int vol_0_20, int mute)
{
    if (vol_0_20 < 0) vol_0_20 = 0;
    if (vol_0_20 > 20) vol_0_20 = 20;
    int hw = mute ? 0 : (vol_0_20 * 2); /* ATC2603C tinymix 15 range: 0..40 */
    char cmd[128];
    snprintf(cmd, sizeof cmd, "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1", hw);
    int rc = system(cmd);
    (void)rc;
}

int InitKeyShm(KeyShmInfo *info)
{
    if (!info) return -1;
    info->id = 1;
    info->addr = shm_slots;
    return 0;
}

int SetKeyShm(KeyShmInfo *info, MonitorValue key, int value)
{
    if (!info || !info->addr || key < 0 || key >= MONITOR_VALUE_COUNT) return -1;
    info->addr[key] = value;
    if (key == MONITOR_VOLUME || key == MONITOR_MUTE) {
        apply_hw_volume(info->addr[MONITOR_VOLUME], info->addr[MONITOR_MUTE]);
    } else if (key == MONITOR_BRIGHTNESS) {
        xgb35_set_brightness_step(value);
    }
    return 0;
}

int GetKeyShm(KeyShmInfo *info, MonitorValue key)
{
    if (!info || !info->addr || key < 0 || key >= MONITOR_VALUE_COUNT) return 0;
    return info->addr[key];
}

static void init_scale_tables(void)
{
    if (tables_ready) return;
    /* Uniform 2/3 scale keeps Onion's 4:3 aspect (427x320) while extending edge background to 480x320. */
    for (int x = 0; x < LAND_W; x++) {
        if (x < CONTENT_X0) {
            x_i0[x] = 2; x_i1[x] = 2; x_w1[x] = 0;
        } else if (x >= CONTENT_X0 + CONTENT_W) {
            x_i0[x] = 637; x_i1[x] = 637; x_w1[x] = 0;
        } else {
            int fp = ((x - CONTENT_X0) * 640 * 256 + 128) / CONTENT_W;
            int i0 = fp >> 8;
            if (i0 >= 639) { x_i0[x] = 639; x_i1[x] = 639; x_w1[x] = 0; }
            else { x_i0[x] = (uint16_t)i0; x_i1[x] = (uint16_t)(i0 + 1); x_w1[x] = (uint8_t)(fp & 255); }
        }
    }
    for (int y = 0; y < LAND_H; y++) {
        int fp = (y * 480 * 256 + 128) / LAND_H;
        int i0 = fp >> 8;
        if (i0 >= 479) { i0 = 479; y_i0[y] = 479; y_i1[y] = 479; y_w1[y] = 0; }
        else { y_i0[y] = (uint16_t)i0; y_i1[y] = (uint16_t)(i0 + 1); y_w1[y] = (uint8_t)(fp & 255); }
    }
    tables_ready = true;
}

static void restore_keypad_mfp(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) return;
    uint32_t *gpio = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0xB01C0000);
    close(fd);
    if (gpio != MAP_FAILED) {
        gpio[0x44 / 4] = 0x60600000u;
        gpio[0x50 / 4] = 0x01F88100u;
        munmap(gpio, 0x1000);
    }
}

void xgb35_pre_sdl_init(bool real_device)
{
    if (!real_device) return;
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_NOMOUSE", "1", 1);
    init_scale_tables();

    fb_fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fb_fd < 0) return;
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &v) == 0 && ioctl(fb_fd, FBIOGET_FSCREENINFO, &f) == 0) {
        ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
        fb_var = v;
        fb_xres = (int)v.xres;
        fb_yres = (int)v.yres;
        fb_stride16 = (int)(f.line_length / 2);
        fb_pages = (v.yres_virtual >= v.yres * 3) ? 3 : (v.yres_virtual >= v.yres * 2) ? 2 : 1;
        fb_page = (fb_pages > 1) ? 1 : 0;
        fb_map_len = f.smem_len ? f.smem_len : (size_t)(f.line_length * v.yres * (unsigned)fb_pages);
        void *m = mmap(NULL, fb_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
        if (m != MAP_FAILED) {
            fb_mem = (uint16_t *)m;
            memset(fb_mem, 0, (size_t)fb_pages * (size_t)fb_yres * (size_t)fb_stride16 * 2u);
        }
        fb_var.xoffset = 0;
        fb_var.yoffset = 0;
        ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var);
        int mfd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
        if (mfd >= 0) {
            void *dm = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0xB02F0000);
            close(mfd);
            if (dm != MAP_FAILED) de_regs = (volatile uint32_t *)dm;
        }
    }
    restore_keypad_mfp();
}

static void push_sdl_key(SDLKey sym, bool down)
{
    if (sym == SDLK_UNKNOWN) return;
    SDL_Event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.keysym.sym = sym;
    SDL_PushEvent(&ev);
}

static SDLKey map_xgb35_evkey(int code)
{
    /* Maps XGB35 Linux EV_KEY codes to raw Miyoo SDLKey values expected by mainui_input_device_event(). */
    switch (code) {
    case 103: return SDLK_UP;
    case 108: return SDLK_DOWN;
    case 105: return SDLK_LEFT;
    case 106: return SDLK_RIGHT;
    case 158: case 28: return SDLK_SPACE;      /* A (Bottom) -> SDLK_RETURN */
    case 352: case 1:  return SDLK_LCTRL;      /* B (Right)  -> SDLK_ESCAPE */
    case 139:          return SDLK_x;          /* X (Left)   -> SDLK_LSHIFT */
    case 308:          return SDLK_y;          /* Y (Top)    -> SDLK_LALT */
    case 314:          return SDLK_s;          /* SELECT -> SDLK_RCTRL */
    case 315:          return SDLK_RETURN;     /* START -> SDLK_F2 */
    case 412:          return SDLK_e;          /* L1 -> SDLK_PAGEUP */
    case 407:          return SDLK_t;          /* R1 -> SDLK_PAGEDOWN */
    case 312:          return SDLK_TAB;        /* L2 -> SDLK_HOME */
    case 313:          return SDLK_BACKSPACE;  /* R2 -> SDLK_END */
    case 174:          return SDLK_ESCAPE;     /* EXIT/MENU -> SDLK_F1 */
    default:           return SDLK_UNKNOWN;
    }
}

static void *xgb35_input_loop(void *unused)
{
    (void)unused;
    int evfds[7];
    for (int i = 0; i < 7; i++) evfds[i] = -1;
    int stick_x = 0, stick_y = 0, base_x = -1, base_y = -1;
    bool select_held = false;

    for (;;) {
        struct pollfd p[8];
        int map[8], nfd = 0;
        p[nfd].fd = wake_pipe[0];
        p[nfd].events = POLLIN;
        map[nfd++] = -1;

        for (int i = 0; i < 7; i++) {
            if (evfds[i] < 0) {
                char path[32];
                /* Skip event0 so xgbpowerd handles physical POWER sleep & S4 shutdown */
                snprintf(path, sizeof path, "/dev/input/event%d", i + 1);
                evfds[i] = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            }
            if (evfds[i] >= 0) {
                p[nfd].fd = evfds[i];
                p[nfd].events = POLLIN;
                map[nfd++] = i;
            }
        }

        int pr = poll(p, (nfds_t)nfd, 250);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (p[0].revents) break;

        for (int k = 1; k < nfd; k++) {
            if (!(p[k].revents & (POLLIN | POLLERR | POLLHUP))) continue;
            struct input_event ev[16];
            ssize_t n = read(p[k].fd, ev, sizeof ev);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
                int idx = map[k];
                ioctl(evfds[idx], EVIOCGRAB, 0);
                close(evfds[idx]);
                evfds[idx] = -1;
                continue;
            }
            for (size_t j = 0; j < (size_t)n / sizeof ev[0]; j++) {
                if (ev[j].type == EV_KEY) {
                    if (ev[j].value == 2) continue; /* SDL key repeat handles held keys */
                    bool down = ev[j].value != 0;
                    if (ev[j].code == 314) select_held = down;

                    if (ev[j].code == 115 || ev[j].code == 114) {
                        if (down) {
                            int delta = ev[j].code == 115 ? 1 : -1;
                            if (select_held) {
                                int b = shm_slots[MONITOR_BRIGHTNESS] + delta;
                                if (b < 0) b = 0;
                                if (b > 10) b = 10;
                                shm_slots[MONITOR_BRIGHTNESS] = b;
                                xgb35_set_brightness_step(b);
                            } else {
                                int v = shm_slots[MONITOR_VOLUME] + delta * 2;
                                if (v < 0) v = 0;
                                if (v > 20) v = 20;
                                shm_slots[MONITOR_VOLUME] = v;
                                apply_hw_volume(v, shm_slots[MONITOR_MUTE]);
                            }
                        }
                        continue;
                    }

                    if (ev[j].code == 174) {
                        mainui_menu_button_record(ev[j].value);
                        if (!down) {
                            SDL_Event uev;
                            memset(&uev, 0, sizeof uev);
                            uev.type = SDL_USEREVENT;
                            uev.user.code = MAINUI_MENU_RELEASE_CODE;
                            SDL_PushEvent(&uev);
                        }
                    }
                    push_sdl_key(map_xgb35_evkey(ev[j].code), down);
                } else if (ev[j].type == EV_ABS) {
                    if (ev[j].code == 0) {
                        if (base_x < 0) base_x = ev[j].value;
                        int d = ev[j].value - base_x;
                        int s = d < -300 ? -1 : d > 300 ? 1 : 0;
                        if (s != stick_x) {
                            if (stick_x < 0) push_sdl_key(SDLK_LEFT, false);
                            if (stick_x > 0) push_sdl_key(SDLK_RIGHT, false);
                            stick_x = s;
                            if (stick_x < 0) push_sdl_key(SDLK_LEFT, true);
                            if (stick_x > 0) push_sdl_key(SDLK_RIGHT, true);
                        }
                    } else if (ev[j].code == 1) {
                        if (base_y < 0) base_y = ev[j].value;
                        int d = ev[j].value - base_y;
                        int s = d < -300 ? -1 : d > 300 ? 1 : 0;
                        if (s != stick_y) {
                            if (stick_y < 0) push_sdl_key(SDLK_UP, false);
                            if (stick_y > 0) push_sdl_key(SDLK_DOWN, false);
                            stick_y = s;
                            if (stick_y < 0) push_sdl_key(SDLK_UP, true);
                            if (stick_y > 0) push_sdl_key(SDLK_DOWN, true);
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < 7; i++) {
        if (evfds[i] >= 0) {
            ioctl(evfds[i], EVIOCGRAB, 0);
            close(evfds[i]);
        }
    }
    return NULL;
}

void xgb35_post_sdl_init(bool real_device)
{
    if (!real_device || input_running) return;
    if (pipe(wake_pipe) != 0) return;
    if (pthread_create(&input_tid, NULL, xgb35_input_loop, NULL) == 0) {
        input_running = true;
    } else {
        close(wake_pipe[0]);
        close(wake_pipe[1]);
        wake_pipe[0] = wake_pipe[1] = -1;
    }
}

void xgb35_hw_quit(void)
{
    if (input_running) {
        ssize_t w = write(wake_pipe[1], "q", 1);
        (void)w;
        pthread_join(input_tid, NULL);
        input_running = false;
        close(wake_pipe[0]);
        close(wake_pipe[1]);
        wake_pipe[0] = wake_pipe[1] = -1;
    }
    if (de_regs) {
        munmap((void *)de_regs, 0x1000);
        de_regs = NULL;
    }
    if (fb_mem) {
        munmap(fb_mem, fb_map_len);
        fb_mem = NULL;
    }
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
}

/* Downscale 640x480 32bpp surface -> 480x320 landscape -> rotated 90 deg 320x480 RGB565 /dev/fb0 */
void xgb35_fb_present(const SDL_Surface *screen)
{
    if (!fb_mem || !screen || !screen->pixels || screen->w < 640 || screen->h < 480) return;
    init_scale_tables();
    /* Wait for previous DE_PATH1_FCR VSync latch if rendering faster than 60Hz (prevents lapping active scanout) */
    if (fb_pages > 1 && de_regs) {
        for (int tries = 0; tries < 40 && (de_regs[0x144 / 4] & 1u); tries++)
            usleep(500);
    }
    uint16_t *dst_fb = fb_mem + (size_t)fb_page * (size_t)fb_yres * (size_t)fb_stride16;
    const uint8_t *base = (const uint8_t *)screen->pixels;
    int pitch = screen->pitch;
    int rshift = screen->format->Rshift;
    int gshift = screen->format->Gshift;
    int bshift = screen->format->Bshift;

    for (int Y = 0; Y < LAND_H; Y++) {
        const uint32_t *row0 = (const uint32_t *)(base + y_i0[Y] * pitch);
        const uint32_t *row1 = (const uint32_t *)(base + y_i1[Y] * pitch);
        unsigned wy = y_w1[Y], iwy = 256u - wy;
        int fb_col = fb_xres - 1 - Y;
        if (fb_col < 0 || fb_col >= fb_xres) continue;

        for (int X = 0; X < LAND_W && X < fb_yres; X++) {
            unsigned wx = x_w1[X], iwx = 256u - wx;
            uint32_t p00 = row0[x_i0[X]], p01 = row0[x_i1[X]];
            uint32_t p10 = row1[x_i0[X]], p11 = row1[x_i1[X]];

            unsigned w00 = (iwx * iwy) >> 8;
            unsigned w01 = (wx  * iwy) >> 8;
            unsigned w10 = (iwx * wy)  >> 8;
            unsigned w11 = 256u - w00 - w01 - w10;

            unsigned r = (((p00 >> rshift) & 0xFFu) * w00 + ((p01 >> rshift) & 0xFFu) * w01 +
                          ((p10 >> rshift) & 0xFFu) * w10 + ((p11 >> rshift) & 0xFFu) * w11) >> 8;
            unsigned g = (((p00 >> gshift) & 0xFFu) * w00 + ((p01 >> gshift) & 0xFFu) * w01 +
                          ((p10 >> gshift) & 0xFFu) * w10 + ((p11 >> gshift) & 0xFFu) * w11) >> 8;
            unsigned b = (((p00 >> bshift) & 0xFFu) * w00 + ((p01 >> bshift) & 0xFFu) * w01 +
                          ((p10 >> bshift) & 0xFFu) * w10 + ((p11 >> bshift) & 0xFFu) * w11) >> 8;

            dst_fb[X * fb_stride16 + fb_col] =
                (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
        }
    }
    if (fb_pages > 1) {
        fb_var.xoffset = 0;
        fb_var.yoffset = (uint32_t)(fb_page * fb_yres);
        ioctl(fb_fd, FBIOPAN_DISPLAY, &fb_var);
        fb_page = (fb_page + 1) % fb_pages;
    }
}

static int read_sysfs_int(const char *path, int fallback)
{
    FILE *f = fopen(path, "r");
    if (!f) return fallback;
    int v = fallback;
    if (fscanf(f, "%d", &v) != 1) v = fallback;
    fclose(f);
    return v;
}

int xgb35_read_battery_percent(void)
{
    int cap = read_sysfs_int("/sys/class/power_supply/battery/capacity", -1);
    if (cap < 0) cap = read_sysfs_int("/sys/class/power_supply/atc260x-battery/capacity", -1);
    if (cap >= 0 && cap <= 100) return cap;
    return 80;
}

bool xgb35_read_charging(void)
{
    int usb = read_sysfs_int("/sys/class/power_supply/atc260x-usb/online", 0);
    int wall = read_sysfs_int("/sys/class/power_supply/atc260x-wall/online", 0);
    return usb > 0 || wall > 0;
}

#define XGB35_BAT "/sys/class/power_supply/battery/"

int xgb35_read_battery_mv(void)
{
    int uv = read_sysfs_int(XGB35_BAT "voltage_now", -1);
    return uv > 0 ? uv / 1000 : -1;
}

static void read_sysfs_line(const char *path, char *out, size_t size)
{
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)size, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
}

void xgb35_read_dashboard(XGB35Dashboard *d)
{
    snprintf(d->battery, sizeof d->battery, "Unknown");
    snprintf(d->voltage, sizeof d->voltage, "Unknown");
    snprintf(d->current, sizeof d->current, "Unknown");
    snprintf(d->health, sizeof d->health, "Unknown");
    snprintf(d->temperature, sizeof d->temperature, "Unknown");
    snprintf(d->charger, sizeof d->charger, "Unknown");
    snprintf(d->uptime, sizeof d->uptime, "Unknown");
    snprintf(d->kernel, sizeof d->kernel, "Unknown");

    char status[32], health[32];
    read_sysfs_line(XGB35_BAT "status", status, sizeof status);
    int cap = read_sysfs_int(XGB35_BAT "capacity", -1);
    if (cap >= 0 && cap <= 100)
        snprintf(d->battery, sizeof d->battery, "%d%%%s%s", cap, *status ? " " : "", status);

    int mv = xgb35_read_battery_mv();
    if (mv > 0) snprintf(d->voltage, sizeof d->voltage, "%d.%02d V", mv / 1000, (mv % 1000) / 10);

    /* current_now is in uA; the ATC2603C gauge reports 0 when idle on a charger */
    int ua = read_sysfs_int(XGB35_BAT "current_now", INT32_MIN);
    if (ua != INT32_MIN) snprintf(d->current, sizeof d->current, "%d mA", ua / 1000);

    read_sysfs_line(XGB35_BAT "health", health, sizeof health);
    if (*health) snprintf(d->health, sizeof d->health, "%s", health);

    /* No battery NTC on this board (power_supply temp reads 0): show the ATC2603C die
     * temperature from its AUXADC debug table ("10 ICTEMP 48068 mCel"). */
    int temp = read_sysfs_int(XGB35_BAT "temp", 0);
    if (temp != 0) {
        snprintf(d->temperature, sizeof d->temperature, "%d.%d C", temp / 10, abs(temp % 10));
    }
    else {
        FILE *f = fopen("/sys/devices/b0188000.i2c/i2c-2/2-0065/auxadc_dbg", "r");
        char line[96];
        while (f && fgets(line, sizeof line, f)) {
            int idx, mc;
            char name[24];
            if (sscanf(line, "%d %23s %d", &idx, name, &mc) == 3 && !strcmp(name, "ICTEMP")) {
                snprintf(d->temperature, sizeof d->temperature, "PMIC %d.%d C", mc / 1000,
                         (mc % 1000) / 100);
                break;
            }
        }
        if (f) fclose(f);
    }

    int usb = read_sysfs_int("/sys/class/power_supply/atc260x-usb/online", 0);
    int wall = read_sysfs_int("/sys/class/power_supply/atc260x-wall/online", 0);
    int vbus = read_sysfs_int("/sys/class/power_supply/atc260x-usb/voltage_now", -1);
    if (!usb && !wall)
        snprintf(d->charger, sizeof d->charger, "Not connected");
    else if (vbus > 0)
        snprintf(d->charger, sizeof d->charger, "%s %d.%02d V", wall ? "Wall" : "USB", vbus / 1000,
                 (vbus % 1000) / 10);
    else
        snprintf(d->charger, sizeof d->charger, "%s", wall ? "Wall" : "USB");

    char up[64];
    read_sysfs_line("/proc/uptime", up, sizeof up);
    long secs = strtol(up, NULL, 10);
    if (secs > 0)
        snprintf(d->uptime, sizeof d->uptime, "%ldh %02ldm", secs / 3600, (secs / 60) % 60);

    char ver[160];
    read_sysfs_line("/proc/version", ver, sizeof ver);
    char release[48];
    if (sscanf(ver, "Linux version %47s", release) == 1)
        snprintf(d->kernel, sizeof d->kernel, "Linux %s", release);
}

bool xgb35_set_brightness_step(int step)
{
    static const int pwm_curve[11] = { 90, 140, 200, 270, 350, 440, 540, 650, 760, 830, 900 };
    if (step < 0) step = 0;
    if (step > 10) step = 10;
    FILE *f = fopen("/sys/class/backlight/owl_backlight/brightness", "w");
    if (!f) return false;
    bool ok = fprintf(f, "%d\n", pwm_curve[step]) > 0;
    return fclose(f) == 0 && ok;
}

bool xgb35_power_off(void)
{
    FILE *f = fopen("/sys/class/backlight/owl_backlight/bl_power", "w");
    if (f) { fprintf(f, "1\n"); fclose(f); }
    sync();
    usleep(150000);
    sync();
    reboot(RB_POWER_OFF);
    return true;
}
