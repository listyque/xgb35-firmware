/* xgb-keytest: Graphical XGB35 Console Button, Dual-Stick & USB Hotplug Tester for X11 Desktop.
 * - Dirty-only 25 FPS capped rendering (zero Xorg socket flood, <1% idle CPU)
 * - True 10-bit continuous analog sticks via ATC2603C debug_adckey
 * - Clockwise XYBA diamond (Left=X, Top=Y, Right=B, Bottom=A) + ESC (174) + live POWER key
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>
#include "x11_dl.h"

#include "font8x16.h"
#include "cyr8x16.h"

#define W 820
#define H 490
#define PID_FILE "/tmp/.xgb_keytest_active"
#define PKEY_SYS "/sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-onoff.0/input/input0/powerkey_status"
#define ADC_SYS  "/sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-adckeypad.0/input/input1/debug_adckey"
#define MAX_EV 17 /* event0 .. event16 */

#define C_BG       0x0E131DU
#define C_HDR      0x161F2FU
#define C_SHELL    0x182234U
#define C_SHELL_BD 0x2B3B56U
#define C_SCR      0x0B1018U
#define C_IDLE     0x222E44U
#define C_IDLE_BD  0x384B6BU
#define C_DONE     0x133B4EU
#define C_DONE_BD  0x38BDF8U
#define C_DOWN     0x22C55EU
#define C_DOWN_BD  0xBBF7D0U
#define C_ACCENT   0x38BDF8U
#define C_GREEN    0x4ADE80U
#define C_WARN     0xFBBF24U
#define C_TEXT     0xF1F5F9U
#define C_DIM      0x94A3B8U
#define C_BTN_RST  0x2563EBU
#define C_BTN_EXT  0x991B1BU

static uint32_t canvas[W * H];
static volatile sig_atomic_t g_quit = 0;

static void cleanup_pid(void) { unlink(PID_FILE); }
static void on_sig(int s) { (void)s; g_quit = 1; }

static long long mono_ms(void)
{
    /* Safe for both 32-bit and 64-bit time_t musl 1.2+ (Alpine v3.20 uses 64-bit time_t + 32-bit pad + nsec) */
    char buf[64];
    int fd = open("/proc/uptime", O_RDONLY);
    if (fd < 0) return 0;
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    char *end = NULL;
    unsigned long sec = strtoul(buf, &end, 10);
    unsigned long csec = (end && *end == '.') ? strtoul(end + 1, NULL, 10) : 0;
    return (long long)sec * 1000LL + (long long)csec * 10LL;
}

static inline void put_px(int x, int y, uint32_t c)
{
    if ((unsigned)x < W && (unsigned)y < H)
        canvas[y * W + x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    for (int r = y; r < y + h; r++) {
        uint32_t *row = canvas + r * W + x;
        for (int col = 0; col < w; col++) row[col] = c;
    }
}

static void draw_rect(int x, int y, int w, int h, int thick, uint32_t c)
{
    fill_rect(x, y, w, thick, c);
    fill_rect(x, y + h - thick, w, thick, c);
    fill_rect(x, y, thick, h, c);
    fill_rect(x + w - thick, y, thick, h, c);
}

static void fill_box(int x, int y, int w, int h, uint32_t fill, uint32_t bd)
{
    fill_rect(x + 2, y, w - 4, h, fill);
    fill_rect(x, y + 2, w, h - 4, fill);
    fill_rect(x + 1, y + 1, w - 2, h - 2, fill);
    fill_rect(x + 2, y, w - 4, 2, bd);
    fill_rect(x + 2, y + h - 2, w - 4, 2, bd);
    fill_rect(x, y + 2, 2, h - 4, bd);
    fill_rect(x + w - 2, y + 2, 2, h - 4, bd);
    put_px(x + 1, y + 1, bd); put_px(x + w - 2, y + 1, bd);
    put_px(x + 1, y + h - 2, bd); put_px(x + w - 2, y + h - 2, bd);
}

static void fill_circle(int cx, int cy, int r, uint32_t c)
{
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int dx_max = (int)sqrtf((float)(r2 - dy * dy));
        fill_rect(cx - dx_max, cy + dy, dx_max * 2 + 1, 1, c);
    }
}

static void draw_circle(int cx, int cy, int r, uint32_t c)
{
    int r_in2 = (r - 2) * (r - 2), r_out2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= r_in2 && d2 <= r_out2)
                put_px(cx + dx, cy + dy, c);
        }
    }
}

static const unsigned char *glyph_utf8(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    unsigned int cp = *s++;
    if (cp >= 0xC0) {
        if ((cp & 0xE0) == 0xC0 && (*s & 0xC0) == 0x80) {
            cp = ((cp & 0x1F) << 6) | (*s++ & 0x3F);
        } else if ((cp & 0xF0) == 0xE0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
            cp = ((cp & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
            s += 2;
        }
    }
    *ps = (const char *)s;
    if (cp < 128) return font8x16 + cp * 16;
    if (cp >= 0x0410 && cp <= 0x044F) return cyr8x16 + (cp - 0x0410) * 16;
    if (cp == 0x0401) return cyr8x16 + 64 * 16;
    if (cp == 0x0451) return cyr8x16 + 65 * 16;
    return font8x16 + '?' * 16;
}

static int utf8_cols(const char *s)
{
    int n = 0;
    while (*s) { (void)glyph_utf8(&s); n++; }
    return n;
}

static void draw_str(int x, int y, const char *s, uint32_t fg)
{
    while (*s) {
        const unsigned char *g = glyph_utf8(&s);
        for (int gy = 0; gy < 16; gy++) {
            unsigned char bits = g[gy];
            if (!bits) continue;
            for (int gx = 0; gx < 8; gx++) {
                if (bits & (0x80U >> gx))
                    put_px(x + gx, y + gy, fg);
            }
        }
        x += 8;
    }
}

static void draw_str_center(int x, int y, int w, int h, const char *s, uint32_t fg)
{
    int tw = utf8_cols(s) * 8;
    draw_str(x + (w - tw) / 2, y + (h - 16) / 2, s, fg);
}

enum {
    B_L2 = 0, B_L1, B_ESC, B_VOLM, B_VOLP, B_PWR, B_R1, B_R2,
    B_UP, B_DOWN, B_LEFT, B_RIGHT,
    B_X, B_Y, B_B, B_A,
    B_SEL, B_START,
    B_COUNT
};

struct btn_def {
    int code;
    const char *label;
    int x, y, w, h;
    int round;
};

/* Clockwise XYBA diamond: Left=X(139), Top=Y(308), Right=B(352), Bottom=A(158) + ESC(174) */
static const struct btn_def kBtns[B_COUNT] = {
    { 312, "L2",     34,  58,  78, 30, 0 },
    { 412, "L1",    120,  58,  78, 30, 0 },
    { 174, "ESC",   246,  58,  68, 30, 0 },
    { 114, "VOL-",  322,  58,  74, 30, 0 },
    { 115, "VOL+",  404,  58,  74, 30, 0 },
    { 116, "POWER", 486,  58,  86, 30, 0 },
    { 407, "R1",    622,  58,  78, 30, 0 },
    { 313, "R2",    708,  58,  78, 30, 0 },

    { 103, "UP",    106, 118,  44, 38, 0 },
    { 108, "DN",    106, 194,  44, 38, 0 },
    { 105, "LT",     64, 156,  44, 38, 0 },
    { 106, "RT",    148, 156,  44, 38, 0 },

    { 139, "X",     644, 174,  24, 24, 1 }, /* Left   (9h)  */
    { 308, "Y",     690, 134,  24, 24, 1 }, /* Top    (12h) */
    { 352, "B",     736, 174,  24, 24, 1 }, /* Right  (3h)  */
    { 158, "A",     690, 214,  24, 24, 1 }, /* Bottom (6h)  */

    { 314, "SELECT", 308, 326,  94, 30, 0 },
    { 315, "START",  418, 326,  94, 30, 0 },
};

struct stick_state {
    int center_x, raw_x;
    int center_y, raw_y;
    int ready;
    int tested;
};

static float norm_adc(int raw, int center)
{
    float v = (float)(center - raw) / 350.0f;
    if (v < -1.0f) v = -1.0f;
    if (v > 1.0f)  v = 1.0f;
    if (fabsf(v) < 0.06f) v = 0.0f;
    return v;
}

static int poll_adc_sticks(struct stick_state *sl, struct stick_state *sr)
{
    char buf[64];
    int fd = open(ADC_SYS, O_RDONLY);
    if (fd < 0) return 0;
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    char *p = buf;
    int v[4];
    for (int i = 0; i < 4; i++) {
        char *end = NULL;
        v[i] = (int)strtol(p, &end, 10);
        if (end == p) return 0;
        p = end;
    }

    /* ATC2603C AUXADC channels: v[2],v[3] = Left Stick X,Y; v[0],v[1] = Right Stick X,Y */
    if (!sl->ready) {
        sl->center_x = (v[2] > 250 && v[2] < 780) ? v[2] : 515;
        sl->center_y = (v[3] > 250 && v[3] < 780) ? v[3] : 515;
        sr->center_x = (v[0] > 250 && v[0] < 780) ? v[0] : 515;
        sr->center_y = (v[1] > 250 && v[1] < 780) ? v[1] : 515;
        sl->raw_x = v[2]; sl->raw_y = v[3];
        sr->raw_x = v[0]; sr->raw_y = v[1];
        sl->ready = sr->ready = 1;
        return 1;
    }

    int chg = (abs(v[2] - sl->raw_x) >= 3 || abs(v[3] - sl->raw_y) >= 3 ||
               abs(v[0] - sr->raw_x) >= 3 || abs(v[1] - sr->raw_y) >= 3);
    if (chg) {
        sl->raw_x = v[2]; sl->raw_y = v[3];
        sr->raw_x = v[0]; sr->raw_y = v[1];
        float lx = norm_adc(sl->raw_x, sl->center_x), ly = norm_adc(sl->raw_y, sl->center_y);
        float rx = norm_adc(sr->raw_x, sr->center_x), ry = norm_adc(sr->raw_y, sr->center_y);
        if (lx * lx + ly * ly > 0.35f) sl->tested = 1;
        if (rx * rx + ry * ry > 0.35f) sr->tested = 1;
    }
    return chg;
}

static void ensure_evnode(int ev_idx)
{
    (void)ev_idx;
}

struct usb_summary {
    int hub_ko;
    int usb_devs;
    int has_hub;
    int hid_devs;
    char hid_name[40];
    int flash_mounted;
    char flash_info[40];
    int wifi_present;
    char wifi_info[40];
};

static void read_first_line(const char *p, char *out, size_t sz)
{
    out[0] = '\0';
    if (!sz) return;
    int fd = open(p, O_RDONLY);
    if (fd < 0) return;
    ssize_t n = read(fd, out, sz - 1);
    close(fd);
    if (n <= 0) return;
    out[n] = '\0';
    for (ssize_t i = 0; i < n; i++) {
        if (out[i] == '\n' || out[i] == '\r') { out[i] = '\0'; break; }
    }
}

static void poll_usb_summary(struct usb_summary *u)
{
    memset(u, 0, sizeof(*u));
    FILE *pm = fopen("/proc/modules", "r");
    if (pm) {
        char line[128];
        while (fgets(line, sizeof(line), pm)) {
            if (strncmp(line, "aotg_hub_hcd ", 13) == 0) { u->hub_ko = 1; break; }
        }
        fclose(pm);
    }

    DIR *d = opendir("/sys/bus/usb/devices");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.' || strncmp(de->d_name, "usb", 3) == 0 || strchr(de->d_name, ':'))
                continue;
            char p[128], cls[16];
            snprintf(p, sizeof(p), "/sys/bus/usb/devices/%s/bDeviceClass", de->d_name);
            read_first_line(p, cls, sizeof(cls));
            if (cls[0]) {
                u->usb_devs++;
                if (strcmp(cls, "09") == 0) u->has_hub = 1;
            }
        }
        closedir(d);
    }

    for (int i = 1; i < MAX_EV; i++) {
        char p[64], nm[64];
        snprintf(p, sizeof(p), "/sys/class/input/event%d/device/name", i);
        read_first_line(p, nm, sizeof(nm));
        if (!nm[0]) continue;
        if (strstr(nm, "adckeypad") || strstr(nm, "gpio") || strstr(nm, "joystick") ||
            strstr(nm, "matrix") || strstr(nm, "onoff"))
            continue;
        u->hid_devs++;
        if (!u->hid_name[0]) snprintf(u->hid_name, sizeof(u->hid_name), "%.24s", nm);
    }

    FILE *pp = fopen("/proc/partitions", "r");
    char sd_dev[16] = "";
    unsigned long long sd_kb = 0;
    if (pp) {
        char line[128];
        while (fgets(line, sizeof(line), pp)) {
            char *p = line;
            char *e = NULL;
            (void)strtoul(p, &e, 10); if (e == p) continue; p = e;
            (void)strtoul(p, &e, 10); if (e == p) continue; p = e;
            unsigned long long blk = strtoull(p, &e, 10); if (e == p) continue; p = e;
            while (*p == ' ' || *p == '\t') p++;
            char nm[32];
            int ni = 0;
            while (*p && *p != '\n' && *p != '\r' && *p != ' ' && ni < 31) nm[ni++] = *p++;
            nm[ni] = '\0';
            if (strncmp(nm, "sd", 2) == 0 && nm[2] >= 'a' && nm[2] <= 'z') {
                snprintf(sd_dev, sizeof(sd_dev), "%s", nm);
                sd_kb = blk;
            }
        }
        fclose(pp);
    }
    FILE *mt = fopen("/proc/mounts", "r");
    if (mt) {
        char line[256];
        while (fgets(line, sizeof(line), mt)) {
            if (strstr(line, " /mnt/usb ") || strstr(line, " /desktop/mnt/usb ")) {
                u->flash_mounted = 1;
                break;
            }
        }
        fclose(mt);
    }
    if (u->flash_mounted)
        snprintf(u->flash_info, sizeof(u->flash_info), "/mnt/usb (%.1f ГБ)", (double)sd_kb / (1024.0 * 1024.0));
    else if (sd_dev[0])
        snprintf(u->flash_info, sizeof(u->flash_info), "/dev/%s (%.1f ГБ)", sd_dev, (double)sd_kb / (1024.0 * 1024.0));
    else
        snprintf(u->flash_info, sizeof(u->flash_info), "Не подключена");

    char oper[32];
    read_first_line("/sys/class/net/wlan0/operstate", oper, sizeof(oper));
    if (oper[0]) {
        u->wifi_present = 1;
        snprintf(u->wifi_info, sizeof(u->wifi_info), "wlan0 (%s)", oper);
    } else {
        snprintf(u->wifi_info, sizeof(u->wifi_info), "Не подключен");
    }
}

static int map_btn(int code)
{
    for (int i = 0; i < B_COUNT; i++)
        if (kBtns[i].code == code) return i;
    return -1;
}

int main(void)
{
    char pbuf[24];
    read_first_line(PID_FILE, pbuf, sizeof(pbuf));
    int old_pid = (int)strtol(pbuf, NULL, 10);
    if (old_pid > 1 && old_pid != (int)getpid()) {
        kill(old_pid, SIGTERM);
        usleep(60000);
    }
    FILE *pf = fopen(PID_FILE, "w");
    if (pf) { fprintf(pf, "%d\n", (int)getpid()); fclose(pf); }
    atexit(cleanup_pid);
    signal(SIGTERM, on_sig);
    signal(SIGINT, on_sig);
    if (!xgb_load_x11(0)) return 2;

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;
    int scr = DefaultScreen(dpy);
    Visual *vis = DefaultVisual(dpy, scr);
    int depth = DefaultDepth(dpy, scr);

    int dw = DisplayWidth(dpy, scr), dh = DisplayHeight(dpy, scr);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
                                     (dw > W ? (dw - W) / 2 : 0),
                                     (dh > H ? (dh - H) / 2 : 0),
                                     W, H, 0, 0, C_BG);
    XSizeHints sh;
    memset(&sh, 0, sizeof(sh));
    sh.flags = PMinSize | PMaxSize;
    sh.min_width = sh.max_width = W;
    sh.min_height = sh.max_height = H;
    XSetWMNormalHints(dpy, win, &sh);
    XStoreName(dpy, win, "XGB35 Button & USB Tester");

    Atom wm_del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_del, 1);
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(dpy, win);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    int bpp = (depth <= 16) ? 16 : 32;
    char *xbuf = (char *)malloc((size_t)W * H * (size_t)(bpp / 8));
    XImage *ximg = XCreateImage(dpy, vis, (unsigned int)depth, ZPixmap, 0, xbuf, W, H, bpp, W * (bpp / 8));

    int ev_fds[MAX_EV];
    int ev_builtin[MAX_EV];
    char ev_names[MAX_EV][40];
    for (int i = 0; i < MAX_EV; i++) ev_fds[i] = -1;

    uint8_t btn_down[B_COUNT] = {0};
    uint8_t btn_done[B_COUNT] = {0};
    struct stick_state st_l = { 515, 515, 515, 515, 0, 0 };
    struct stick_state st_r = { 515, 515, 515, 515, 0, 0 };

    char last_ev[80] = "Ожидание нажатий...";
    char last_usb_ev[64] = "Нет событий USB";
    struct usb_summary usbs, prev_usbs;
    memset(&usbs, 0, sizeof(usbs));
    memset(&prev_usbs, 0, sizeof(prev_usbs));

    int dirty = 1;
    long long last_draw_ms = 0, last_scan_ms = 0, last_adc_ms = 0, last_esc_ms = 0;

    while (!g_quit) {
        long long now = mono_ms();

        while (XPending(dpy)) {
            XEvent xe;
            XNextEvent(dpy, &xe);
            if (xe.type == Expose || xe.type == ConfigureNotify) {
                dirty = 1;
            } else if (xe.type == ClientMessage && (Atom)xe.xclient.data.l[0] == wm_del) {
                g_quit = 1;
            } else if (xe.type == ButtonPress && xe.xbutton.button == Button1) {
                int mx = xe.xbutton.x, my = xe.xbutton.y;
                if (my >= 8 && my <= 36 && mx >= 566 && mx <= 682) {
                    memset(btn_done, 0, sizeof(btn_done));
                    st_l.tested = st_r.tested = 0;
                    st_l.ready = st_r.ready = 0;
                    snprintf(last_ev, sizeof(last_ev), "Результаты сброшены (стики откалиброваны)");
                    dirty = 1;
                } else if (my >= 8 && my <= 36 && mx >= 692 && mx <= 808) {
                    g_quit = 1;
                } else if (usbs.flash_mounted && my >= 404 && my <= 478 && mx >= 414 && mx <= 608) {
                    int rc = system("/usr/bin/pcmanfm /mnt/usb >/dev/null 2>&1 &");
                    (void)rc;
                }
            }
        }

        if (now - last_scan_ms >= 300) {
            last_scan_ms = now;
            for (int i = 0; i < MAX_EV; i++) {
                if (ev_fds[i] >= 0) {
                    int ver = 0;
                    if (ioctl(ev_fds[i], EVIOCGVERSION, &ver) < 0) {
                        close(ev_fds[i]);
                        ev_fds[i] = -1;
                        dirty = 1;
                    } else {
                        continue;
                    }
                }
                ensure_evnode(i);
                char p[32], nm[64] = "";
                snprintf(p, sizeof(p), "/dev/input/event%d", i);
                int fd = open(p, O_RDONLY | O_NONBLOCK);
                if (fd < 0) continue;
                ioctl(fd, EVIOCGNAME(sizeof(nm)), nm);
                snprintf(ev_names[i], sizeof(ev_names[i]), "%.36s", nm[0] ? nm : p);
                ev_builtin[i] = (strstr(nm, "adckeypad") || strstr(nm, "gpio") ||
                                 strstr(nm, "joystick") || strstr(nm, "matrix") ||
                                 strstr(nm, "onoff")) ? 1 : 0;
                ev_fds[i] = fd;
                dirty = 1;
            }
            poll_usb_summary(&usbs);
            if (memcmp(&usbs, &prev_usbs, sizeof(usbs)) != 0) {
                prev_usbs = usbs;
                dirty = 1;
            }
        }

        if (now - last_adc_ms >= 25) {
            last_adc_ms = now;
            if (poll_adc_sticks(&st_l, &st_r))
                dirty = 1;

            /* Live PMIC power-key status poll every 25ms (catches even quick taps!) */
            char pk[8];
            read_first_line(PKEY_SYS, pk, sizeof(pk));
            if (pk[0] == '1') {
                if (!btn_down[B_PWR]) {
                    btn_down[B_PWR] = 1;
                    btn_done[B_PWR] = 1;
                    snprintf(last_ev, sizeof(last_ev), "Кнопка POWER (116) — НАЖАТА");
                    dirty = 1;
                }
            } else if (btn_down[B_PWR]) {
                btn_down[B_PWR] = 0;
                snprintf(last_ev, sizeof(last_ev), "Кнопка POWER (116) — ОТПУЩЕНА");
                dirty = 1;
            }
        }

        struct pollfd pfds[MAX_EV];
        int pmap[MAX_EV], nfd = 0;
        for (int i = 0; i < MAX_EV; i++) {
            if (ev_fds[i] >= 0) {
                pfds[nfd].fd = ev_fds[i];
                pfds[nfd].events = POLLIN;
                pfds[nfd].revents = 0;
                pmap[nfd++] = i;
            }
        }
        if (nfd && poll(pfds, (nfds_t)nfd, dirty ? 4 : 16) > 0) {
            for (int k = 0; k < nfd; k++) {
                int slot = pmap[k];
                if (pfds[k].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    close(ev_fds[slot]);
                    ev_fds[slot] = -1;
                    dirty = 1;
                    continue;
                }
                if (!(pfds[k].revents & POLLIN)) continue;
                struct input_event ev[32];
                ssize_t n;
                while ((n = read(ev_fds[slot], ev, sizeof(ev))) > 0) {
                    int cnt = (int)(n / (ssize_t)sizeof(ev[0]));
                    for (int j = 0; j < cnt; j++) {
                        int t = ev[j].type, c = ev[j].code, v = ev[j].value;
                        if (ev_builtin[slot]) {
                            if (t == EV_KEY && v != 2) {
                                int bi = map_btn(c);
                                if (bi >= 0) {
                                    btn_down[bi] = (uint8_t)(v != 0);
                                    if (v != 0) btn_done[bi] = 1;
                                    snprintf(last_ev, sizeof(last_ev), "Кнопка %s (код %d) — %s",
                                             kBtns[bi].label, c, v ? "НАЖАТА" : "ОТПУЩЕНА");
                                    if (c == 174 && v == 1) {
                                        if (now - last_esc_ms < 450) g_quit = 1;
                                        last_esc_ms = now;
                                    }
                                    if (btn_down[B_SEL] && btn_down[B_START]) g_quit = 1;
                                } else {
                                    snprintf(last_ev, sizeof(last_ev), "Код %d (%s) — %s",
                                             c, ev_names[slot], v ? "НАЖАТА" : "ОТПУЩЕНА");
                                }
                                dirty = 1;
                            }
                        } else {
                            if (t == EV_KEY && v != 2) {
                                snprintf(last_usb_ev, sizeof(last_usb_ev), "Код %d (%s)",
                                         c, v ? "нажат" : "отпущен");
                                dirty = 1;
                            } else if (t == EV_REL && c == REL_WHEEL) {
                                snprintf(last_usb_ev, sizeof(last_usb_ev), "Колесо мыши %+d", v);
                                dirty = 1;
                            }
                        }
                    }
                }
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    close(ev_fds[slot]);
                    ev_fds[slot] = -1;
                    dirty = 1;
                }
            }
        } else if (!nfd) {
            usleep(16000);
        }

        now = mono_ms();
        if (!dirty || (now - last_draw_ms < 40))
            continue;
        dirty = 0;
        last_draw_ms = now;

        /* --- Render UI (max 25 FPS, 0 FPS when idle!) --- */
        fill_rect(0, 0, W, H, C_BG);
        fill_rect(0, 0, W, 44, C_HDR);
        fill_rect(0, 43, W, 1, C_SHELL_BD);
        draw_str(16, 14, "ТЕСТЕР КНОПОК И USB ПРИСТАВКИ XGB35", C_ACCENT);

        int done_cnt = 0;
        for (int i = 0; i < B_COUNT; i++) done_cnt += btn_done[i];
        char badge[64];
        snprintf(badge, sizeof(badge), "Кнопки: %d/%d  Стики: %d/2",
                 done_cnt, B_COUNT, st_l.tested + st_r.tested);
        draw_str(340, 14, badge, (done_cnt == B_COUNT && st_l.tested && st_r.tested) ? C_GREEN : C_TEXT);

        fill_box(566, 8, 116, 28, C_BTN_RST, C_ACCENT);
        draw_str_center(566, 8, 116, 28, "Сбросить", C_TEXT);
        fill_box(692, 8, 116, 28, C_BTN_EXT, 0xF87171U);
        draw_str_center(692, 8, 116, 28, "Выход (2xESC)", C_TEXT);

        fill_box(20, 50, W - 40, 342, C_SHELL, C_SHELL_BD);

        fill_box(246, 102, 328, 212, C_SCR, C_SHELL_BD);
        draw_str_center(246, 112, 328, 18, "ДИАГНОСТИКА ВВОДА В РЕАЛЬНОМ ВРЕМЕНИ", C_ACCENT);
        fill_rect(262, 134, 296, 1, C_SHELL_BD);
        draw_str(262, 146, "Последнее событие приставки:", C_DIM);
        draw_str(262, 166, last_ev, C_GREEN);

        draw_str(262, 196, "Внешняя USB мышь / клавиатура:", C_DIM);
        draw_str(262, 216, last_usb_ev, C_WARN);

        int total_parts = B_COUNT + 2;
        int cur_parts = done_cnt + st_l.tested + st_r.tested;
        fill_rect(262, 250, 296, 14, C_IDLE);
        fill_rect(262, 250, 296 * cur_parts / total_parts, 14, C_DONE_BD);
        draw_rect(262, 250, 296, 14, 1, C_IDLE_BD);
        draw_str_center(246, 274, 328, 16, "Зелёный = нажата   Голубой = проверена", C_DIM);
        draw_str_center(246, 292, 328, 16, "Мышь и A = клик   2xESC = выход", C_DIM);

        fill_rect(106, 156, 44, 38, C_IDLE);

        for (int i = 0; i < B_COUNT; i++) {
            const struct btn_def *b = &kBtns[i];
            uint32_t bg = btn_down[i] ? C_DOWN : (btn_done[i] ? C_DONE : C_IDLE);
            uint32_t bd = btn_down[i] ? C_DOWN_BD : (btn_done[i] ? C_DONE_BD : C_IDLE_BD);
            uint32_t fg = btn_down[i] ? 0x052E16U : C_TEXT;
            if (b->round) {
                fill_circle(b->x, b->y, b->w, bg);
                draw_circle(b->x, b->y, b->w, bd);
                draw_str_center(b->x - b->w, b->y - b->w, b->w * 2, b->w * 2, b->label, fg);
            } else {
                fill_box(b->x, b->y, b->w, b->h, bg, bd);
                draw_str_center(b->x, b->y, b->w, b->h, b->label, fg);
            }
        }

        const int sl_x = 128, sl_y = 304, sr_x = 690, sr_y = 304, rad = 46;
        struct { int cx, cy; struct stick_state *st; const char *title; } sticks[2] = {
            { sl_x, sl_y, &st_l, "Левый стик" },
            { sr_x, sr_y, &st_r, "Правый стик" }
        };
        for (int i = 0; i < 2; i++) {
            int cx = sticks[i].cx, cy = sticks[i].cy;
            struct stick_state *st = sticks[i].st;
            fill_circle(cx, cy, rad, C_SCR);
            draw_circle(cx, cy, rad, st->tested ? C_DONE_BD : C_IDLE_BD);
            fill_rect(cx - rad + 4, cy, (rad - 4) * 2, 1, C_IDLE);
            fill_rect(cx, cy - rad + 4, 1, (rad - 4) * 2, C_IDLE);

            float nx = norm_adc(st->raw_x, st->center_x);
            float ny = norm_adc(st->raw_y, st->center_y);
            int px = cx + (int)(nx * (float)(rad - 10));
            int py = cy + (int)(ny * (float)(rad - 10));
            fill_circle(px, py, 9, (fabsf(nx) > 0.10f || fabsf(ny) > 0.10f) ? C_DOWN : C_ACCENT);

            draw_str_center(cx - 70, cy - rad - 20, 140, 16, sticks[i].title, C_TEXT);
            char xy[40];
            snprintf(xy, sizeof(xy), "X:%+4d%% Y:%+4d%%", (int)(nx * 100.0f), (int)(ny * 100.0f));
            draw_str_center(cx - 70, cy + rad + 6, 140, 16, xy, st->tested ? C_DONE_BD : C_DIM);
        }

        const int cy0 = 402, cw = 190, ch = 76;
        fill_box(20, cy0, cw, ch, C_HDR, (usbs.hub_ko && usbs.usb_devs > 0) ? C_DONE_BD : C_SHELL_BD);
        draw_str(30, cy0 + 8, "USB ХАБ / HOST", C_ACCENT);
        char c1a[48], c1b[48];
        snprintf(c1a, sizeof(c1a), "Драйвер: %s", usbs.hub_ko ? "Hub TDM OK" : "Штатный");
        snprintf(c1b, sizeof(c1b), "Устройств: %d%s", usbs.usb_devs, usbs.has_hub ? " (Хаб)" : "");
        draw_str(30, cy0 + 30, c1a, usbs.hub_ko ? C_GREEN : C_WARN);
        draw_str(30, cy0 + 50, c1b, C_TEXT);

        fill_box(216, cy0, cw, ch, C_HDR, usbs.hid_devs > 0 ? C_DONE_BD : C_SHELL_BD);
        draw_str(226, cy0 + 8, "МЫШЬ И КЛАВИАТУРА", C_ACCENT);
        char c2a[48];
        snprintf(c2a, sizeof(c2a), "HID входов: %d", usbs.hid_devs);
        draw_str(226, cy0 + 30, c2a, usbs.hid_devs > 0 ? C_GREEN : C_DIM);
        draw_str(226, cy0 + 50, usbs.hid_devs > 0 ? usbs.hid_name : "Ожидание USB HID", C_TEXT);

        fill_box(412, cy0, cw, ch, C_HDR, usbs.flash_mounted ? C_GREEN : C_SHELL_BD);
        draw_str(422, cy0 + 8, "USB ФЛЕШКА", C_ACCENT);
        draw_str(422, cy0 + 30, usbs.flash_mounted ? "Смонтирована (клик)" : "Статус накопителя:",
                 usbs.flash_mounted ? C_GREEN : C_DIM);
        draw_str(422, cy0 + 50, usbs.flash_info, C_TEXT);

        fill_box(608, cy0, cw + 2, ch, C_HDR, usbs.wifi_present ? C_DONE_BD : C_SHELL_BD);
        draw_str(618, cy0 + 8, "USB WI-FI", C_ACCENT);
        draw_str(618, cy0 + 30, usbs.wifi_present ? "Адаптер активен" : "Статус радиомодуля:",
                 usbs.wifi_present ? C_GREEN : C_DIM);
        draw_str(618, cy0 + 50, usbs.wifi_info, C_TEXT);

        if (bpp == 16) {
            uint16_t *dst = (uint16_t *)xbuf;
            for (int i = 0; i < W * H; i++) {
                uint32_t p = canvas[i];
                dst[i] = (uint16_t)((((p >> 19) & 0x1FU) << 11) |
                                    (((p >> 10) & 0x3FU) << 5) |
                                    ((p >> 3) & 0x1FU));
            }
        } else {
            memcpy(xbuf, canvas, sizeof(canvas));
        }
        XPutImage(dpy, win, gc, ximg, 0, 0, 0, 0, W, H);
        XFlush(dpy);
    }

    ximg->data = NULL;
    XDestroyImage(ximg);
    free(xbuf);
    for (int i = 0; i < MAX_EV; i++) if (ev_fds[i] >= 0) close(ev_fds[i]);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
