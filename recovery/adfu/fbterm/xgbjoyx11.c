/* xgbjoyx11: XGB35 built-in sticks/gamepad + hotplug USB Mouse/Keyboard -> X11 (XTest).
 * - Polls /dev/input/event1..16 non-exclusively (auto-creating /dev/input/event* nodes via mknod)
 * - Reads smooth 10-bit analog sticks (0..1023) from ATC2603C debug_adckey for BOTH Left & Right sticks (X + Y)
 * - Zero kernel dmesg spam (writes key_mode=1 once at startup)
 */
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
#include <unistd.h>
#include "x11_dl.h"

#define MAX_SLOTS 16 /* /dev/input/event1 .. event16 (skip event0 = atc260x_onoff) */
#define ADC_SYS "/sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-adckeypad.0/input/input1/debug_adckey"
#define KEYMODE_SYS "/sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-adckeypad.0/input/input1/key_mode"

struct dev_slot {
    int fd;
    int is_builtin;
    uint8_t key_held[256];
    uint8_t btn_held[10];
};

struct adc_axis {
    int center;
    int raw;
    int ready;
};

static KeyCode ev_to_xkc[256];

static void init_keymap(Display *dpy)
{
    static const struct { unsigned short ev; KeySym sym; } map[] = {
        { 1, XK_Escape }, { 2, XK_1 }, { 3, XK_2 }, { 4, XK_3 }, { 5, XK_4 },
        { 6, XK_5 }, { 7, XK_6 }, { 8, XK_7 }, { 9, XK_8 }, { 10, XK_9 }, { 11, XK_0 },
        { 12, XK_minus }, { 13, XK_equal }, { 14, XK_BackSpace }, { 15, XK_Tab },
        { 16, XK_q }, { 17, XK_w }, { 18, XK_e }, { 19, XK_r }, { 20, XK_t },
        { 21, XK_y }, { 22, XK_u }, { 23, XK_i }, { 24, XK_o }, { 25, XK_p },
        { 26, XK_bracketleft }, { 27, XK_bracketright }, { 28, XK_Return }, { 29, XK_Control_L },
        { 30, XK_a }, { 31, XK_s }, { 32, XK_d }, { 33, XK_f }, { 34, XK_g },
        { 35, XK_h }, { 36, XK_j }, { 37, XK_k }, { 38, XK_l }, { 39, XK_semicolon },
        { 40, XK_apostrophe }, { 41, XK_grave }, { 42, XK_Shift_L }, { 43, XK_backslash },
        { 44, XK_z }, { 45, XK_x }, { 46, XK_c }, { 47, XK_v }, { 48, XK_b },
        { 49, XK_n }, { 50, XK_m }, { 51, XK_comma }, { 52, XK_period }, { 53, XK_slash },
        { 54, XK_Shift_R }, { 55, XK_KP_Multiply }, { 56, XK_Alt_L }, { 57, XK_space },
        { 58, XK_Caps_Lock },
        { 59, XK_F1 }, { 60, XK_F2 }, { 61, XK_F3 }, { 62, XK_F4 }, { 63, XK_F5 },
        { 64, XK_F6 }, { 65, XK_F7 }, { 66, XK_F8 }, { 67, XK_F9 }, { 68, XK_F10 },
        { 69, XK_Num_Lock }, { 70, XK_Scroll_Lock },
        { 71, XK_KP_7 }, { 72, XK_KP_8 }, { 73, XK_KP_9 }, { 74, XK_KP_Subtract },
        { 75, XK_KP_4 }, { 76, XK_KP_5 }, { 77, XK_KP_6 }, { 78, XK_KP_Add },
        { 79, XK_KP_1 }, { 80, XK_KP_2 }, { 81, XK_KP_3 }, { 82, XK_KP_0 }, { 83, XK_KP_Decimal },
        { 87, XK_F11 }, { 88, XK_F12 },
        { 96, XK_KP_Enter }, { 97, XK_Control_R }, { 98, XK_KP_Divide }, { 99, XK_Print },
        { 100, XK_Alt_R }, { 102, XK_Home }, { 103, XK_Up }, { 104, XK_Page_Up },
        { 105, XK_Left }, { 106, XK_Right }, { 107, XK_End }, { 108, XK_Down },
        { 109, XK_Page_Down }, { 110, XK_Insert }, { 111, XK_Delete },
        { 119, XK_Pause }, { 125, XK_Super_L }, { 126, XK_Super_R }, { 127, XK_Menu },
    };
    for (int c = 1; c < 248; c++)
        ev_to_xkc[c] = (KeyCode)(c + 8);
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        KeyCode kc = XKeysymToKeycode(dpy, map[i].sym);
        if (kc) ev_to_xkc[map[i].ev] = kc;
    }
}

static void send_key(Display *dpy, KeySym sym, int press)
{
    KeyCode kc = XKeysymToKeycode(dpy, sym);
    if (kc) {
        XTestFakeKeyEvent(dpy, kc, press ? True : False, CurrentTime);
        XFlush(dpy);
    }
}

static void send_btn(Display *dpy, unsigned int btn, int press)
{
    XTestFakeButtonEvent(dpy, btn, press ? True : False, CurrentTime);
    XFlush(dpy);
}

static void ensure_adc_key_mode_once(void)
{
    char cur[8] = "";
    int rfd = open(KEYMODE_SYS, O_RDONLY);
    if (rfd >= 0) { (void)read(rfd, cur, sizeof(cur) - 1); close(rfd); }
    if (cur[0] != '1') {
        int wfd = open(KEYMODE_SYS, O_WRONLY);
        if (wfd >= 0) { (void)write(wfd, "1\n", 2); close(wfd); }
    }
}

static void poll_adc_sticks(struct adc_axis ax[4])
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
    for (int i = 0; i < 4; i++) {
        if (!ax[i].ready) {
            ax[i].center = (v[i] > 250 && v[i] < 780) ? v[i] : 515;
            ax[i].ready = 1;
        }
        ax[i].raw = v[i];
    }
}

static float adc_vel(const struct adc_axis *a, float max_px_per_tick)
{
    if (!a->ready) return 0.0f;
    float n = (float)(a->center - a->raw) / 360.0f;
    if (n > 1.0f) n = 1.0f;
    if (n < -1.0f) n = -1.0f;
    const float dz = 0.11f;
    float mag = fabsf(n);
    if (mag <= dz) return 0.0f;
    float u = (mag - dz) / (1.0f - dz);
    float curve = 0.30f * u + 0.70f * u * u;
    return (n < 0.0f ? -curve : curve) * max_px_per_tick;
}

static void ensure_evnode(int ev_idx)
{
    (void)ev_idx;
}

static void close_slot(Display *dpy, struct dev_slot *s)
{
    if (s->fd < 0) return;
    for (int c = 1; c < 256; c++) {
        if (s->key_held[c] && ev_to_xkc[c]) {
            XTestFakeKeyEvent(dpy, ev_to_xkc[c], False, CurrentTime);
            s->key_held[c] = 0;
        }
    }
    for (unsigned int b = 1; b < 10; b++) {
        if (s->btn_held[b]) {
            XTestFakeButtonEvent(dpy, b, False, CurrentTime);
            s->btn_held[b] = 0;
        }
    }
    XFlush(dpy);
    close(s->fd);
    s->fd = -1;
}

static void rescan_devices(Display *dpy, struct dev_slot *slots)
{
    for (int i = 0; i < MAX_SLOTS; i++) {
        int ev_idx = i + 1;
        if (slots[i].fd >= 0) {
            int ver = 0;
            if (ioctl(slots[i].fd, EVIOCGVERSION, &ver) < 0)
                close_slot(dpy, &slots[i]);
            else
                continue;
        }
        ensure_evnode(ev_idx);
        char path[32], name[80] = "";
        snprintf(path, sizeof(path), "/dev/input/event%d", ev_idx);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (strstr(name, "onoff")) {
            close(fd);
            continue;
        }
        int builtin = (strstr(name, "adckeypad") || strstr(name, "gpio") ||
                       strstr(name, "joystick") || strstr(name, "matrix")) ? 1 : 0;
        memset(slots[i].key_held, 0, sizeof(slots[i].key_held));
        memset(slots[i].btn_held, 0, sizeof(slots[i].btn_held));
        slots[i].is_builtin = builtin;
        slots[i].fd = fd;
    }
}

static int is_keytest_running(void)
{
    char buf[24];
    int fd = open("/tmp/.xgb_keytest_active", O_RDONLY);
    if (fd < 0) return 0;
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    int pid = (int)strtol(buf, NULL, 10);
    return (pid > 1 && kill(pid, 0) == 0);
}

int main(void)
{
    if (!xgb_load_x11(1)) return 2;
    Display *dpy = NULL;
    for (int tries = 0; tries < 60 && !dpy; tries++) {
        dpy = XOpenDisplay(NULL);
        if (!dpy) usleep(100000);
    }
    if (!dpy) return 1;
    /* Stay immune to JWM/Openbox XGrabServer() during outline window move/resize (prevents 100% XTest deadlock!) */
    if (XTestGrabControl) { XTestGrabControl(dpy, True); XFlush(dpy); }
    init_keymap(dpy);
    ensure_adc_key_mode_once();

    struct dev_slot slots[MAX_SLOTS];
    for (int i = 0; i < MAX_SLOTS; i++) slots[i].fd = -1;
    struct adc_axis ax[4] = { {515,515,0}, {515,515,0}, {515,515,0}, {515,515,0} };

    poll_adc_sticks(ax);
    rescan_devices(dpy, slots);

    float rem_x = 0.0f, rem_y = 0.0f;
    int sel = 0, st = 0, scan_ticks = 0, adc_ticks = 0, keytest_on = 0;

    for (;;) {
        if (++scan_ticks >= 15) {
            scan_ticks = 0;
            rescan_devices(dpy, slots);
            keytest_on = is_keytest_running();
        }
        if (++adc_ticks >= 2) {
            adc_ticks = 0;
            poll_adc_sticks(ax);
        }

        struct pollfd pfds[MAX_SLOTS];
        int map_idx[MAX_SLOTS];
        int nfd = 0;
        for (int i = 0; i < MAX_SLOTS; i++) {
            if (slots[i].fd >= 0) {
                pfds[nfd].fd = slots[i].fd;
                pfds[nfd].events = POLLIN;
                pfds[nfd].revents = 0;
                map_idx[nfd++] = i;
            }
        }

        int r = nfd ? poll(pfds, (nfds_t)nfd, 12) : (usleep(12000), 0);
        int usb_dx = 0, usb_dy = 0, need_flush = 0;

        if (r > 0) {
            for (int k = 0; k < nfd; k++) {
                struct dev_slot *s = &slots[map_idx[k]];
                if (pfds[k].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    close_slot(dpy, s);
                    continue;
                }
                if (!(pfds[k].revents & POLLIN)) continue;

                struct input_event ev[32];
                ssize_t n;
                while ((n = read(s->fd, ev, sizeof(ev))) > 0) {
                    int cnt = (int)(n / (ssize_t)sizeof(ev[0]));
                    for (int j = 0; j < cnt; j++) {
                        unsigned short t = ev[j].type, code = ev[j].code;
                        int val = ev[j].value;

                        if (s->is_builtin) {
                            if (t == EV_KEY && val != 2) {
                                int dn = (val == 1);
                                if (keytest_on) {
                                    /* In key tester: Bottom button A (158) = Left Click so UI buttons work */
                                    if (code == 158) send_btn(dpy, 1, dn);
                                    continue;
                                }
                                /* Clockwise diamond: Left=X(139), Top=Y(308), Right=B(352), Bottom=A(158) */
                                switch (code) {
                                case 314: sel = dn; break;
                                case 315: st = dn; break;
                                case 158: send_btn(dpy, 1, dn); break; /* A (bottom) = Left Click */
                                case 352: send_btn(dpy, 3, dn); break; /* B (right)  = Right Click */
                                case 139: send_key(dpy, XK_Return, dn); break; /* X (left) = Enter */
                                case 308: if (dn) { int rc = system("uxterm -fa 'DejaVu Sans Mono' -fs 11 -bg '#10151c' -fg '#e6edf3' &"); (void)rc; } break; /* Y (top) = Terminal */
                                case 174: send_key(dpy, XK_Escape, dn); break; /* ESC button */
                                case 412: if (dn) { send_btn(dpy, 4, 1); send_btn(dpy, 4, 0); } break;
                                case 407: if (dn) { send_btn(dpy, 5, 1); send_btn(dpy, 5, 0); } break;
                                case 312: send_btn(dpy, 2, dn); break;
                                case 313: send_key(dpy, XK_BackSpace, dn); break;
                                case 103: send_key(dpy, XK_Up, dn); break;
                                case 108: send_key(dpy, XK_Down, dn); break;
                                case 105: send_key(dpy, XK_Left, dn); break;
                                case 106: send_key(dpy, XK_Right, dn); break;
                                }
                                if (sel && st) {
                                    for (int m = 0; m < MAX_SLOTS; m++) close_slot(dpy, &slots[m]);
                                    XCloseDisplay(dpy);
                                    int rc = system("killall jwm Xorg 2>/dev/null");
                                    (void)rc;
                                    return 0;
                                }
                            }
                        } else {
                            /* External hotplugged USB Mouse / Keyboard / Receiver */
                            if (t == EV_REL) {
                                if (code == REL_X) usb_dx += val;
                                else if (code == REL_Y) usb_dy += val;
                                else if (code == REL_WHEEL && val != 0) {
                                    unsigned int b = (val > 0) ? 4 : 5;
                                    int steps = val > 0 ? val : -val;
                                    if (steps > 8) steps = 8;
                                    for (int w = 0; w < steps; w++) {
                                        XTestFakeButtonEvent(dpy, b, True, CurrentTime);
                                        XTestFakeButtonEvent(dpy, b, False, CurrentTime);
                                    }
                                    need_flush = 1;
                                } else if (code == REL_HWHEEL && val != 0) {
                                    unsigned int b = (val < 0) ? 6 : 7;
                                    XTestFakeButtonEvent(dpy, b, True, CurrentTime);
                                    XTestFakeButtonEvent(dpy, b, False, CurrentTime);
                                    need_flush = 1;
                                }
                            } else if (t == EV_KEY && val != 2) {
                                int dn = (val == 1);
                                unsigned int mbtn = 0;
                                if (code == BTN_LEFT) mbtn = 1;
                                else if (code == BTN_MIDDLE) mbtn = 2;
                                else if (code == BTN_RIGHT) mbtn = 3;
                                else if (code == BTN_SIDE) mbtn = 8;
                                else if (code == BTN_EXTRA) mbtn = 9;

                                if (mbtn) {
                                    s->btn_held[mbtn] = (uint8_t)dn;
                                    XTestFakeButtonEvent(dpy, mbtn, dn ? True : False, CurrentTime);
                                    need_flush = 1;
                                } else if (code > 0 && code < 248 && ev_to_xkc[code]) {
                                    s->key_held[code] = (uint8_t)dn;
                                    XTestFakeKeyEvent(dpy, ev_to_xkc[code], dn ? True : False, CurrentTime);
                                    need_flush = 1;
                                }
                            }
                        }
                    }
                }
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                    close_slot(dpy, s);
            }
        }

        if (usb_dx != 0 || usb_dy != 0) {
            XTestFakeRelativeMotionEvent(dpy, usb_dx, usb_dy, CurrentTime);
            need_flush = 1;
        }

        /* Both Left Stick (ax[2..3], 5px) and Right Stick (ax[0..1], precision 2.2px) control cursor X & Y */
        rem_x += adc_vel(&ax[2], 5.0f) + adc_vel(&ax[0], 2.2f);
        rem_y += adc_vel(&ax[3], 5.0f) + adc_vel(&ax[1], 2.2f);
        int dx = (int)rem_x, dy = (int)rem_y;
        if (dx != 0 || dy != 0) {
            rem_x -= (float)dx;
            rem_y -= (float)dy;
            XTestFakeRelativeMotionEvent(dpy, dx, dy, CurrentTime);
            need_flush = 1;
        }

        if (need_flush)
            XFlush(dpy);
    }
}
