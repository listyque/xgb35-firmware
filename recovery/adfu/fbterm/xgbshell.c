/* xgbshell: 480x320 graphical Dual-Boot menu (--bootmenu) and desktop/game shell for ATM7051H / XGB35.
 * Renders into a 480x320 RGB565 landscape backbuffer and blits rotated 90 deg onto portrait 320x480 /dev/fb0.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "font8x16.h"

#define W 480
#define H 320
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#define C_BG       RGB(16, 20, 28)
#define C_TOP      RGB(26, 33, 48)
#define C_CARD     RGB(28, 36, 52)
#define C_SEL      RGB(42, 98, 188)
#define C_ACCENT   RGB(82, 182, 255)
#define C_GREEN    RGB(76, 209, 115)
#define C_WARN     RGB(245, 178, 66)
#define C_TEXT     RGB(238, 242, 248)
#define C_DIM      RGB(142, 156, 178)

static uint16_t back[W * H];
static uint16_t *fb_mem;
static int fb_fd = -1, fb_xres = 320, fb_yres = 480, fb_stride16 = 320;
static int evfds[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int vol_cur = 28, bri_cur = 675; /* max vol=40, max bri=900 */

static void fill_rect(int x, int y, int w, int h, uint16_t col)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    for (int r = y; r < y + h; r++) {
        uint16_t *row = back + r * W + x;
        for (int c = 0; c < w; c++) row[c] = col;
    }
}

static void draw_str(int x, int y, const char *s, uint16_t fg, int scale)
{
    for (; *s; s++, x += 8 * scale) {
        unsigned char ch = (unsigned char)*s;
        if (ch >= 128) ch = '?';
        const unsigned char *g = font8x16 + ch * 16;
        for (int gy = 0; gy < 16; gy++) {
            unsigned char bits = g[gy];
            if (!bits) continue;
            for (int gx = 0; gx < 8; gx++) {
                if (bits & (0x80 >> gx)) {
                    int px = x + gx * scale, py = y + gy * scale;
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            if ((unsigned)(px + sx) < W && (unsigned)(py + sy) < H)
                                back[(py + sy) * W + (px + sx)] = fg;
                }
            }
        }
    }
}

static void present(void)
{
    /* Rotate 480x320 landscape backbuffer -> 320x480 portrait /dev/fb0: (X,Y) -> (319-Y, X) */
    for (int X = 0; X < W; X++) {
        uint16_t *dst = fb_mem + X * fb_stride16;
        const uint16_t *col = back + X;
        for (int Y = 0; Y < H; Y++)
            dst[fb_xres - 1 - Y] = col[Y * W];
    }
}

static void fb_reinit(void)
{
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &v); ioctl(fb_fd, FBIOGET_FSCREENINFO, &f);
    ioctl(fb_fd, FBIOBLANK, FB_BLANK_UNBLANK);
    v.xoffset = 0; v.yoffset = v.yres; ioctl(fb_fd, FBIOPAN_DISPLAY, &v);
    v.yoffset = 0; ioctl(fb_fd, FBIOPAN_DISPLAY, &v);
}

static void open_inputs(int grab)
{
    /* Skip event0 (atc260x_onoff power button) so xgbpowerd always receives KEY_POWER (116). */
    for (int i = 1; i < 8; i++) {
        if (evfds[i] >= 0) continue;
        char p[32]; snprintf(p, sizeof(p), "/dev/input/event%d", i);
        evfds[i] = open(p, O_RDONLY | O_NONBLOCK);
        (void)grab;
    }
}

static void close_inputs(void)
{
    for (int i = 0; i < 8; i++) {
        if (evfds[i] >= 0) { ioctl(evfds[i], EVIOCGRAB, 0); close(evfds[i]); evfds[i] = -1; }
    }
}

static int read_int_file(const char *path, int dflt)
{
    FILE *f = fopen(path, "r");
    if (!f) return dflt;
    int v = dflt;
    if (fscanf(f, "%d", &v) != 1) v = dflt;
    fclose(f);
    return v;
}

static int free_ram_mb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[128]; int free_kb = 0, cached_kb = 0, total_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        sscanf(line, "MemTotal: %d", &total_kb);
        sscanf(line, "MemFree: %d", &free_kb);
        sscanf(line, "Cached: %d", &cached_kb);
    }
    fclose(f);
    return (free_kb + cached_kb) / 1024;
}

static int total_ram_mb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;
    char line[128]; int total_kb = 0;
    while (fgets(line, sizeof(line), f)) if (sscanf(line, "MemTotal: %d", &total_kb) == 1) break;
    fclose(f);
    return total_kb / 1024;
}

static void set_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 40) v = 40;
    vol_cur = v;
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1", vol_cur);
    system(cmd);
}

static void set_brightness(int b)
{
    if (b < 90) b = 90;
    if (b > 900) b = 900;
    bri_cur = b;
    FILE *f = fopen("/sys/class/backlight/owl_backlight/brightness", "w");
    if (f) { fprintf(f, "%d\n", bri_cur); fclose(f); }
}

static int led_cur = 0;
static void set_led(int on)
{
    led_cur = on ? 1 : 0;
    FILE *f = fopen("/sys/class/custom/led_control", "w");
    if (f) { fprintf(f, "%d\n", led_cur); fclose(f); }
}

static const char *const cpu_modes[6] = { "stock", "smart", "eco", "balanced", "turbo", "max" };
static const char *const cpu_labels[6] = {
    "STOCK 900M (1.15V)", "SMART 240M-1.26G (UV)", "ECO 240M (1.00V)",
    "BAL 600M (1.03V)", "TURBO 1.20G (OC)", "MAX 1.27G (+41%)"
};
static int cpu_idx = 0;

static void sync_cpu_idx(void)
{
    FILE *f = fopen("/tmp/cpu_profile", "r");
    if (!f) f = fopen("/mnt/sd/linux/cpu_profile", "r");
    if (!f) return;
    char buf[32] = "";
    if (fscanf(f, "%31s", buf) == 1) {
        for (int i = 0; i < 6; i++)
            if (!strcmp(buf, cpu_modes[i])) { cpu_idx = i; break; }
    }
    fclose(f);
}

static void set_cpu_mode(int idx)
{
    cpu_idx = (idx + 6) % 6;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "/bin/perf-mode %s >/dev/null 2>&1", cpu_modes[cpu_idx]);
    system(cmd);
}

enum { EV_NONE = 0, EV_UP, EV_DOWN, EV_LEFT, EV_RIGHT, EV_A, EV_B, EV_TAB_PREV, EV_TAB_NEXT, EV_EXIT };

static int poll_ui_event(int timeout_ms)
{
    open_inputs(1);
    struct pollfd p[8]; int nfd = 0, map[8];
    for (int i = 0; i < 8; i++) if (evfds[i] >= 0) { p[nfd].fd = evfds[i]; p[nfd].events = POLLIN; map[nfd++] = i; }
    if (!nfd || poll(p, nfd, timeout_ms) <= 0) return EV_NONE;

    static int stick_x, stick_y, base_x = -1, base_y = -1;
    for (int k = 0; k < nfd; k++) {
        if (!p[k].revents) continue;
        struct input_event ev[16];
        int n = read(p[k].fd, ev, sizeof(ev));
        if (n <= 0) { if (errno != EAGAIN) { close(evfds[map[k]]); evfds[map[k]] = -1; } continue; }
        for (int j = 0; j < n / (int)sizeof(ev[0]); j++) {
            if (ev[j].type == EV_KEY && ev[j].value >= 1) {
                switch (ev[j].code) {
                case 103: return EV_UP;
                case 108: return EV_DOWN;
                case 105: return EV_LEFT;
                case 106: return EV_RIGHT;
                case 158: case 28: case 315: return EV_A; /* A (Bottom), Enter, or START */
                case 352: case 1:   return EV_B;          /* B (Right) or Esc */
                case 412: case 312: return EV_TAB_PREV;   /* L1 / L2 */
                case 407: case 313: return EV_TAB_NEXT;   /* R1 / R2 */
                case 115: set_volume(vol_cur + 4); return EV_NONE;
                case 114: set_volume(vol_cur - 4); return EV_NONE;
                case 174: return EV_EXIT;
                }
            } else if (ev[j].type == EV_ABS) {
                if (ev[j].code == 0) {
                    if (base_x < 0) base_x = ev[j].value;
                    int d = ev[j].value - base_x;
                    int s = d < -300 ? -1 : d > 300 ? 1 : 0;
                    if (s != stick_x) { stick_x = s; if (s < 0) return EV_LEFT; if (s > 0) return EV_RIGHT; }
                } else if (ev[j].code == 1) {
                    if (base_y < 0) base_y = ev[j].value;
                    int d = ev[j].value - base_y;
                    int s = d < -300 ? -1 : d > 300 ? 1 : 0;
                    if (s != stick_y) { stick_y = s; if (s < 0) return EV_UP; if (s > 0) return EV_DOWN; }
                }
            }
        }
    }
    return EV_NONE;
}

static void run_external(const char *cmd)
{
    close_inputs();
    fill_rect(0, 0, W, H, 0);
    present();
    system(cmd);
    fb_reinit();
    open_inputs(1);
}

/* --- Mode 1: Multi-Boot Selector (--bootmenu) --- */
static int run_bootmenu(void)
{
    static const char *titles[8] = {
        "1. Onion OS (Open MainUI v1.1)",
        "2. SuperX CFW (RetroArch + SimplerMenu+)",
        "3. HDMI Linux Desktop (X11 + Browser)",
        "4. Alpine Linux + XGB Graphical Shell",
        "5. Stock XGB35 Console (Native 480x320)",
        "6. USB Disks -> Windows PC (SD + NAND)",
        "7. Reboot to ADFU USB Mode",
        "8. Power Off Console (Full Shutdown)"
    };
    static const char *subs[8] = {
        "Official Onion UI + RU/EN + Recents/Favorites",
        "Powkiddy ATM7051 CFW + HW Upscaler + 40+ Cores",
        "720p/1080p JWM + Dillo + xterm + 512MB Swap (LCD Off)",
        "musl ARMv7 + Terminal + htop/nano + CPU OC",
        "Original manufacturer menu & emulators (update.zip)",
        "Open MicroSD + 8GB NAND in Windows Explorer",
        "Enter BootROM ADFU for PC flashing / RAM staging",
        "Deep S4 poweroff (or hold VOL+ & VOL- anytime)"
    };
    static const int codes[8] = { 5, 4, 7, 0, 1, 2, 3, 6 };
    int sel = 0, countdown_ms = 5000;
    int bat = read_int_file("/sys/class/power_supply/battery/capacity", -1);
    int ram_tot = total_ram_mb();

    for (;;) {
        fill_rect(0, 0, W, H, C_BG);
        fill_rect(0, 0, W, 26, C_TOP);
        draw_str(12, 5, "XGB35 MULTI-BOOT LOADER", C_ACCENT, 1);
        char hdr[48];
        if (bat >= 0) snprintf(hdr, sizeof(hdr), "Cortex-A5 | %dMB | %d%%", ram_tot, bat);
        else          snprintf(hdr, sizeof(hdr), "Cortex-A5 | %dMB RAM", ram_tot);
        draw_str(W - (int)strlen(hdr) * 8 - 10, 5, hdr, C_GREEN, 1);

        for (int i = 0; i < 8; i++) {
            int y = 28 + i * 32;
            fill_rect(12, y, W - 24, 30, i == sel ? C_SEL : C_CARD);
            if (i == sel) fill_rect(12, y, 4, 30, C_ACCENT);
            draw_str(22, y + 1, titles[i], C_TEXT, 1);
            draw_str(22, y + 15, subs[i], i == sel ? C_TEXT : C_DIM, 1);
        }

        fill_rect(0, H - 26, W, 26, C_TOP);
        if (countdown_ms > 0) {
            int bar_w = (W - 24) * countdown_ms / 5000;
            fill_rect(12, H - 30, bar_w, 3, C_ACCENT);
            char msg[64];
            snprintf(msg, sizeof(msg), "Auto-boot in %d.%ds (VOL+ & VOL- = Sleep/Off)",
                     countdown_ms / 1000, (countdown_ms % 1000) / 100);
            draw_str(12, H - 21, msg, C_WARN, 1);
        } else {
            draw_str(12, H - 21, "[Up/Down] Select   [A] Boot   [VOL+ & VOL-] Off", C_DIM, 1);
        }
        present();

        int ev = poll_ui_event(100);
        if (ev == EV_UP)   { countdown_ms = -1; sel = (sel + 7) % 8; }
        if (ev == EV_DOWN) { countdown_ms = -1; sel = (sel + 1) % 8; }
        if (ev == EV_A) {
            close_inputs();
            if (codes[sel] == 6) system("echo 4 > /sys/class/backlight/owl_backlight/bl_power; sync; poweroff -f");
            return codes[sel];
        }
        if (countdown_ms > 0) {
            countdown_ms -= 100;
            if (countdown_ms <= 0) { close_inputs(); return codes[sel]; }
        }
    }
}

/* --- Mode 2: Graphical Desktop & Game Launcher --- */
struct entry { char label[64]; char path[256]; int is_dir; const char *core; };
static struct entry games[256];
static int ngames = 0;
static char cur_dir[256] = "";

static const char *core_for_ext(const char *name, off_t sz)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return NULL;
    if (!strcasecmp(dot, ".nes") || !strcasecmp(dot, ".fds")) return "fceumm_libretro.so";
    if (!strcasecmp(dot, ".gb") || !strcasecmp(dot, ".gbc")) return "gambatte_libretro.so";
    if (!strcasecmp(dot, ".gba")) return "gpsp_libretro.so";
    if (!strcasecmp(dot, ".sfc") || !strcasecmp(dot, ".smc")) return "snes9x2005_libretro.so";
    if (!strcasecmp(dot, ".pbp") || !strcasecmp(dot, ".cue") ||
        !strcasecmp(dot, ".iso") || !strcasecmp(dot, ".img") || !strcasecmp(dot, ".chd"))
        return "pcsx_rearmed_libretro.so";
    if (!strcasecmp(dot, ".bin"))
        return sz >= 8 * 1024 * 1024 ? "pcsx_rearmed_libretro.so" : "picodrive_libretro.so";
    if (!strcasecmp(dot, ".md") || !strcasecmp(dot, ".gen") || !strcasecmp(dot, ".smd"))
        return "picodrive_libretro.so";
    if (!strcasecmp(dot, ".zip")) return "fbalpha2012_libretro.so";
    return NULL;
}

static const char *find_core_path(const char *so_name, char *buf, size_t sz)
{
    static const char *dirs[] = { "/mnt/sd/onion/cores", "/alpine/usr/lib/libretro", "/usr/lib/libretro", "/lib/libretro" };
    for (int i = 0; i < 4; i++) {
        snprintf(buf, sz, "%s/%s", dirs[i], so_name);
        if (access(buf, R_OK) == 0) return buf;
    }
    return NULL;
}

static int nand_games_mounted(void)
{
    struct stat st;
    return stat("/mnt/sd/game", &st) == 0 || stat("/mnt/udisk/game", &st) == 0 || stat("/mnt/media/game", &st) == 0;
}

static void scan_games(void)
{
    ngames = 0;
    if (!cur_dir[0]) {
        strcpy(games[ngames].label, "* Play 2048 (Built-in Libretro)");
        strcpy(games[ngames].path, ":2048");
        games[ngames].core = "2048_libretro.so";
        ngames++;
        if (!nand_games_mounted()) {
            strcpy(games[ngames].label, "+ Mount MicroSD Card (/mnt/sd)");
            strcpy(games[ngames].path, ":mount_nand");
            ngames++;
        } else {
            static const char *roots[][2] = {
                { "[SD]    /mnt/sd/game",    "/mnt/sd/game" },
                { "[UDISK] /mnt/udisk/game", "/mnt/udisk/game" },
                { "[MEDIA] /mnt/media/game", "/mnt/media/game" },
            };
            for (int i = 0; i < 3; i++) {
                struct stat st;
                if (stat(roots[i][1], &st) == 0 && S_ISDIR(st.st_mode)) {
                    strcpy(games[ngames].label, roots[i][0]);
                    strcpy(games[ngames].path, roots[i][1]);
                    games[ngames].is_dir = 1;
                    ngames++;
                }
            }
        }
        return;
    }

    strcpy(games[ngames].label, "[..] Parent Directory");
    strcpy(games[ngames].path, ":up");
    games[ngames++].is_dir = 1;

    DIR *d = opendir(cur_dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && ngames < 255) {
        if (de->d_name[0] == '.') continue;
        char full[256];
        snprintf(full, sizeof(full), "%s/%s", cur_dir, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            snprintf(games[ngames].label, sizeof(games[ngames].label), "[DIR] %.56s", de->d_name);
            strncpy(games[ngames].path, full, sizeof(games[ngames].path) - 1);
            games[ngames++].is_dir = 1;
        } else {
            const char *core = core_for_ext(de->d_name, st.st_size);
            if (!core) continue;
            snprintf(games[ngames].label, sizeof(games[ngames].label), "      %.56s", de->d_name);
            strncpy(games[ngames].path, full, sizeof(games[ngames].path) - 1);
            games[ngames].is_dir = 0;
            games[ngames++].core = core;
        }
    }
    closedir(d);
}

static void launch_libretro(const char *core_so, const char *rom_path)
{
    char cpath[128], cmd[512];
    if (!find_core_path(core_so, cpath, sizeof(cpath))) {
        run_external("/bin/fbterm -x 't:' -- /bin/sh -c 'echo Core not unpacked yet - open Apps -> Unpack Alpine Bundle first; sleep 3'");
        return;
    }
    const char *runner = access("/alpine/bin/xgbretro", X_OK) == 0 ?
        "chroot /alpine /bin/xgbretro" : "LD_LIBRARY_PATH=/usr/lib:/alpine/usr/lib:/alpine/lib:/lib /bin/xgbretro";
    if (rom_path && rom_path[0])
        snprintf(cmd, sizeof(cmd), "LD_LIBRARY_PATH=/lib /bin/tinymix 35 1 >/dev/null 2>&1; %s '%s' \"%s\"; LD_LIBRARY_PATH=/lib /bin/tinymix 35 0 >/dev/null 2>&1", runner, cpath, rom_path);
    else
        snprintf(cmd, sizeof(cmd), "LD_LIBRARY_PATH=/lib /bin/tinymix 35 1 >/dev/null 2>&1; %s '%s'; LD_LIBRARY_PATH=/lib /bin/tinymix 35 0 >/dev/null 2>&1", runner, cpath);
    run_external(cmd);
}

static void run_shell_ui(void)
{
    static const char *tabs[3] = { "GAMES", "LINUX APPS", "SYSTEM" };
    static const char *apps[][2] = {
        { "HDMI Linux Desktop 720p@60 (Fast, LCD Off)", "/bin/boot-desktop 720p" },
        { "HDMI Linux Desktop 1080p@60 (FullHD Max)",   "/bin/boot-desktop 1080p" },
        { ">_ Open Terminal (Alpine ash / fbterm)",     ":term" },
        { "Top / Process Monitor (htop)",               ":htop" },
        { "Text Editor (nano / vi)",                    ":nano" },
        { "Button & Analog Stick Tester",               "/bin/fbterm -1 -x 't:' -- /bin/hwtest buttons 15" },
        { "Play Speaker Audio Test Chime",              "LD_LIBRARY_PATH=/lib /bin/tinymix 35 1 >/dev/null 2>&1; /bin/hwtest sound 0; LD_LIBRARY_PATH=/lib /bin/tinymix 35 0 >/dev/null 2>&1" },
    };
    int tab = 0, sel[3] = { 0, 0, 0 }, scroll[3] = { 0, 0, 0 };
    bri_cur = read_int_file("/sys/class/backlight/owl_backlight/brightness", 675);
    sync_cpu_idx();
    scan_games();

    for (;;) {
        fill_rect(0, 0, W, H, C_BG);
        fill_rect(0, 0, W, 30, C_TOP);

        for (int t = 0; t < 3; t++) {
            int tx = 8 + t * 98;
            if (t == tab) fill_rect(tx, 4, 92, 22, C_SEL);
            draw_str(tx + 8, 7, tabs[t], t == tab ? C_TEXT : C_DIM, 1);
        }
        int bat = read_int_file("/sys/class/power_supply/battery/capacity", -1);
        char stat_str[40];
        if (bat >= 0) snprintf(stat_str, sizeof(stat_str), "%dMB %d%% V%d", free_ram_mb(), bat, vol_cur);
        else          snprintf(stat_str, sizeof(stat_str), "%dMB free V%d", free_ram_mb(), vol_cur);
        draw_str(W - (int)strlen(stat_str) * 8 - 8, 7, stat_str, C_GREEN, 1);

        int count = tab == 0 ? ngames : (tab == 1 ? 7 : 9);
        if (sel[tab] >= count) sel[tab] = count > 0 ? count - 1 : 0;
        if (sel[tab] < scroll[tab]) scroll[tab] = sel[tab];
        if (sel[tab] >= scroll[tab] + 7) scroll[tab] = sel[tab] - 6;

        for (int row = 0; row < 7 && scroll[tab] + row < count; row++) {
            int idx = scroll[tab] + row;
            int y = 38 + row * 35;
            fill_rect(10, y, W - 20, 31, idx == sel[tab] ? C_SEL : C_CARD);
            if (idx == sel[tab]) fill_rect(10, y, 4, 31, C_ACCENT);
            char line[64];
            if (tab == 0) {
                snprintf(line, sizeof(line), "%.56s", games[idx].label);
            } else if (tab == 1) {
                snprintf(line, sizeof(line), "%s", apps[idx][0]);
            } else {
                if (idx == 0) snprintf(line, sizeof(line), "CPU Profile:     < %s >", cpu_labels[cpu_idx]);
                if (idx == 1) snprintf(line, sizeof(line), "Backlight Brightness:  < %d%% >", bri_cur * 100 / 900);
                if (idx == 2) snprintf(line, sizeof(line), "Speaker Volume:        < %d%% >", vol_cur * 100 / 40);
                if (idx == 3) snprintf(line, sizeof(line), "Blue Power LED:        < %s >", led_cur ? "ON" : "OFF");
                if (idx == 4) snprintf(line, sizeof(line), "Switch to SuperX CFW (RetroArch)");
                if (idx == 5) snprintf(line, sizeof(line), "Switch to Stock XGB35 OS (480x320)");
                if (idx == 6) snprintf(line, sizeof(line), "USB Disks -> Windows PC (SD + NAND)");
                if (idx == 7) snprintf(line, sizeof(line), "Reboot to ADFU USB Mode");
                if (idx == 8) snprintf(line, sizeof(line), "Power Off Console");
            }
            draw_str(22, y + 7, line, C_TEXT, 1);
        }

        fill_rect(0, H - 26, W, 26, C_TOP);
        draw_str(10, H - 21, "[L1/R1] Tab  [A] Open  [B] Back  [VOL+/-] Sound", C_DIM, 1);
        present();

        int ev = poll_ui_event(1000);
        if (ev == EV_TAB_PREV) tab = (tab + 2) % 3;
        else if (ev == EV_TAB_NEXT) tab = (tab + 1) % 3;
        else if (ev == EV_UP && count > 0) sel[tab] = (sel[tab] + count - 1) % count;
        else if (ev == EV_DOWN && count > 0) sel[tab] = (sel[tab] + 1) % count;
        else if (ev == EV_LEFT && tab == 2) {
            if (sel[2] == 0) set_cpu_mode(cpu_idx - 1);
            if (sel[2] == 1) set_brightness(bri_cur - 90);
            if (sel[2] == 2) set_volume(vol_cur - 4);
            if (sel[2] == 3) set_led(!led_cur);
        } else if (ev == EV_RIGHT && tab == 2) {
            if (sel[2] == 0) set_cpu_mode(cpu_idx + 1);
            if (sel[2] == 1) set_brightness(bri_cur + 90);
            if (sel[2] == 2) set_volume(vol_cur + 4);
            if (sel[2] == 3) set_led(!led_cur);
        } else if (ev == EV_LEFT) tab = (tab + 2) % 3;
        else if (ev == EV_RIGHT) tab = (tab + 1) % 3;
        else if (ev == EV_B && tab == 0 && cur_dir[0]) {
            char *slash = strrchr(cur_dir, '/');
            if (slash && strcmp(cur_dir, "/mnt/udisk/game") && strcmp(cur_dir, "/mnt/media/game") && strcmp(cur_dir, "/mnt/sd/game"))
                *slash = 0;
            else
                cur_dir[0] = 0;
            sel[0] = scroll[0] = 0;
            scan_games();
        } else if (ev == EV_A && count > 0) {
            if (tab == 0) {
                struct entry *g = &games[sel[0]];
                if (!strcmp(g->path, ":2048")) launch_libretro("2048_libretro.so", NULL);
                else if (!strcmp(g->path, ":mount_nand")) {
                    run_external("/bin/mount-games");
                    scan_games();
                } else if (!strcmp(g->path, ":up")) {
                    char *slash = strrchr(cur_dir, '/');
                    if (slash && strcmp(cur_dir, "/mnt/udisk/game") && strcmp(cur_dir, "/mnt/media/game") && strcmp(cur_dir, "/mnt/sd/game"))
                        *slash = 0;
                    else
                        cur_dir[0] = 0;
                    sel[0] = scroll[0] = 0;
                    scan_games();
                } else if (g->is_dir) {
                    strncpy(cur_dir, g->path, sizeof(cur_dir) - 1);
                    sel[0] = scroll[0] = 0;
                    scan_games();
                } else if (g->core) {
                    launch_libretro(g->core, g->path);
                }
            } else if (tab == 1) {
                const char *act = apps[sel[1]][1];
                if (!strcmp(act, ":term")) {
                    if (access("/alpine/bin/ash", X_OK) == 0)
                        run_external("/bin/fbterm -1 -x 't:' -x 'umon->state' -- chroot /alpine /bin/ash -l");
                    else
                        run_external("/bin/fbterm -1 -x 't:' -x 'umon->state' -- /bin/sh -i");
                } else if (!strcmp(act, ":htop")) {
                    if (access("/alpine/usr/bin/htop", X_OK) == 0)
                        run_external("/bin/fbterm -1 -x 't:' -- chroot /alpine /usr/bin/htop");
                    else
                        run_external("/bin/fbterm -1 -x 't:' -- /bin/busybox top");
                } else if (!strcmp(act, ":nano")) {
                    if (access("/alpine/usr/bin/nano", X_OK) == 0)
                        run_external("/bin/fbterm -1 -x 't:' -- chroot /alpine /usr/bin/nano /root/notes.txt");
                    else
                        run_external("/bin/fbterm -1 -x 't:' -- /bin/busybox vi /tmp/notes.txt");
                } else {
                    run_external(act);
                }
            } else if (tab == 2) {
                if (sel[2] == 0) set_cpu_mode(cpu_idx + 1);
                if (sel[2] == 1) set_brightness(bri_cur >= 900 ? 180 : bri_cur + 180);
                if (sel[2] == 2) set_volume(vol_cur >= 40 ? 8 : vol_cur + 8);
                if (sel[2] == 3) set_led(!led_cur);
                if (sel[2] == 4) { close_inputs(); execl("/bin/boot-superx", "boot-superx", NULL); open_inputs(1); }
                if (sel[2] == 5) { close_inputs(); execl("/bin/boot-stock", "boot-stock", NULL); open_inputs(1); }
                if (sel[2] == 6) { run_external("/bin/usb-mode pc-disks"); scan_games(); }
                if (sel[2] == 7) { close_inputs(); execl("/bin/reboot-adfu", "reboot-adfu", NULL); }
                if (sel[2] == 8) { close_inputs(); execl("/bin/xgbpowerd", "xgbpowerd", "--off", NULL); }
            }
        }
    }
}

int main(int argc, char **argv)
{
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) { perror("/dev/fb0"); return 1; }
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &v); ioctl(fb_fd, FBIOGET_FSCREENINFO, &f);
    fb_xres = v.xres; fb_yres = v.yres; fb_stride16 = f.line_length / 2;
    fb_mem = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) { perror("mmap fb0"); return 1; }
    fb_reinit();

    if (argc > 1 && !strcmp(argv[1], "--bootmenu"))
        return run_bootmenu();
    run_shell_ui();
    return 0;
}
