/* fbterm: minimal framebuffer terminal for the ATM7051H RAM Linux (no fbcon in the stock kernel).
 * - shell on a pty, drawn with the VGA 8x16 font on /dev/fb0 (RGB565)
 * - kernel log (/proc/kmsg) interleaved in yellow
 * - input: anything written to the FIFO /tmp/fbterm.in goes to the shell (e.g. from adb)
 * - the panel is a portrait 320x480 ST7796S mounted as 480x320 landscape: -r 1 (default) rotates
 * - -x PREFIX (repeatable) hides kernel log lines whose text starts with PREFIX
 * Build: python -m ziglang cc -target arm-linux-musleabi -Os -static -s fbterm.c -o fbterm
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "font8x16.h"

#define FW 8
#define FH 16
#define MAXC 160
#define MAXR 80

static uint16_t *fb;
static int stride16, xres, yres, rot = 1;
static int W, H, COLS, ROWS;            /* logical (rotated) screen */
static unsigned char ch[MAXR][MAXC];
static unsigned char at[MAXR][MAXC];    /* fg | bg << 4 */
static int cx, cy, sx, sy, attr = 7, cursor_on = 1;

static const uint16_t pal[16] = {
    0x0000, 0xA800, 0x0540, 0xAAA0, 0x0015, 0xA815, 0x02B5, 0xAD55,
    0x52AA, 0xFAAA, 0x57EA, 0xFFEA, 0x52BF, 0xFABF, 0x57FF, 0xFFFF,
};

static inline void px(int x, int y, uint16_t c)
{
    if (rot) fb[x * stride16 + (xres - 1 - y)] = c;   /* logical (x,y) -> buffer (xres-1-y, x) */
    else fb[y * stride16 + x] = c;
}

static void cell(int r, int c, int invert)
{
    const unsigned char *g = font8x16 + ch[r][c] * FH;
    int a = at[r][c], fg = a & 15, bg = a >> 4;
    if (invert) { int t = fg; fg = bg; bg = t; }
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++)
            px(c * FW + x, r * FH + y, pal[(g[y] & (0x80 >> x)) ? fg : bg]);
}

static void redraw(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) cell(r, c, 0);
}

static void clear_cells(int r, int c0, int c1)
{
    for (int c = c0; c < c1 && c < COLS; c++) { ch[r][c] = ' '; at[r][c] = (attr & 0xF0) | 7; cell(r, c, 0); }
}

static void scroll_up(void)
{
    memmove(ch[0], ch[1], sizeof(ch[0]) * (ROWS - 1));
    memmove(at[0], at[1], sizeof(at[0]) * (ROWS - 1));
    memset(ch[ROWS - 1], ' ', MAXC);
    memset(at[ROWS - 1], 7, MAXC);
    redraw();
}

static void newline(void) { if (++cy >= ROWS) { cy = ROWS - 1; scroll_up(); } }

static void put(unsigned char c)
{
    if (cx >= COLS) { cx = 0; newline(); }
    ch[cy][cx] = c; at[cy][cx] = attr; cell(cy, cx, 0); cx++;
}

/* --- VT100 subset --- */
static int state, np, par[16], priv;

static void sgr(void)
{
    if (np == 0) np = 1, par[0] = 0;
    for (int i = 0; i < np; i++) {
        int p = par[i];
        if (p == 0) attr = 7;
        else if (p == 1) attr |= 8;
        else if (p == 22) attr &= ~8;
        else if (p == 7) attr = (attr & 0x0F) << 4 | attr >> 4;
        else if (p >= 30 && p <= 37) attr = (attr & 0xF8) | (p - 30);
        else if (p == 39) attr = (attr & 0xF8) | 7;
        else if (p >= 90 && p <= 97) attr = (attr & 0xF0) | 8 | (p - 90);
        else if (p >= 40 && p <= 47) attr = (attr & 0x0F) | (p - 40) << 4;
        else if (p == 49) attr &= 0x0F;
    }
}

static int P(int i, int d) { return i < np && par[i] ? par[i] : d; }

static void csi(unsigned char f)
{
    int n = P(0, 1);
    if (priv) return;
    switch (f) {
    case 'A': cy = cy - n < 0 ? 0 : cy - n; break;
    case 'B': case 'e': cy = cy + n >= ROWS ? ROWS - 1 : cy + n; break;
    case 'C': case 'a': cx = cx + n >= COLS ? COLS - 1 : cx + n; break;
    case 'D': cx = cx - n < 0 ? 0 : cx - n; break;
    case 'G': case '`': cx = n - 1 < COLS ? n - 1 : COLS - 1; break;
    case 'd': cy = n - 1 < ROWS ? n - 1 : ROWS - 1; break;
    case 'H': case 'f':
        cy = P(0, 1) - 1; cx = P(1, 1) - 1;
        if (cy >= ROWS) cy = ROWS - 1;
        if (cx >= COLS) cx = COLS - 1;
        break;
    case 'J':
        if (P(0, 0) == 2) { for (int r = 0; r < ROWS; r++) clear_cells(r, 0, COLS); }
        else if (P(0, 0) == 0) { clear_cells(cy, cx, COLS); for (int r = cy + 1; r < ROWS; r++) clear_cells(r, 0, COLS); }
        else { for (int r = 0; r < cy; r++) clear_cells(r, 0, COLS); clear_cells(cy, 0, cx + 1); }
        break;
    case 'K':
        if (P(0, 0) == 0) clear_cells(cy, cx, COLS);
        else if (P(0, 0) == 1) clear_cells(cy, 0, cx + 1);
        else clear_cells(cy, 0, COLS);
        break;
    case 'P':
        for (int c = cx; c < COLS; c++) { int s = c + n; ch[cy][c] = s < COLS ? ch[cy][s] : ' '; at[cy][c] = s < COLS ? at[cy][s] : 7; cell(cy, c, 0); }
        break;
    case '@':
        for (int c = COLS - 1; c >= cx; c--) { int s = c - n; ch[cy][c] = s >= cx ? ch[cy][s] : ' '; at[cy][c] = s >= cx ? at[cy][s] : 7; cell(cy, c, 0); }
        break;
    case 'm': sgr(); break;
    case 's': sx = cx; sy = cy; break;
    case 'u': cx = sx; cy = sy; break;
    }
}

static void feed(const unsigned char *s, int n)
{
    static unsigned utf_left;
    cell(cy, cx < COLS ? cx : COLS - 1, 0);          /* hide cursor */
    for (int i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (state == 1) {                             /* after ESC */
            if (c == '[') { state = 2; np = 0; priv = 0; memset(par, 0, sizeof(par)); }
            else if (c == ']') state = 3;
            else if (c == 'c') { attr = 7; for (int r = 0; r < ROWS; r++) clear_cells(r, 0, COLS); cx = cy = 0; state = 0; }
            else if (c == '(' || c == ')') state = 4;
            else state = 0;
            continue;
        }
        if (state == 2) {                             /* CSI */
            if (c >= '0' && c <= '9') { if (np == 0) np = 1; par[np - 1] = par[np - 1] * 10 + c - '0'; }
            else if (c == ';') { if (np == 0) np = 1; if (np < 16) np++; }
            else if (c == '?' || c == '>' || c == '=') priv = 1;
            else if (c >= 0x40 && c <= 0x7E) { csi(c); state = 0; }
            continue;
        }
        if (state == 3) { if (c == 7 || c == '\\') state = 0; continue; }   /* OSC: skip */
        if (state == 4) { state = 0; continue; }
        if (utf_left) { if (--utf_left == 0) put('?'); continue; }
        switch (c) {
        case 27: state = 1; break;
        case '\r': cx = 0; break;
        case '\n': case 11: case 12: newline(); break;
        case '\b': if (cx > 0) cx--; break;
        case '\t': do put(' '); while (cx % 8 && cx < COLS); break;
        case 7: case 0: break;
        default:
            if (c >= 0xC0) utf_left = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            else if (c >= 0x80) break;
            else if (c >= 32) put(c);
        }
    }
    if (cursor_on) cell(cy, cx < COLS ? cx : COLS - 1, 1);
}

static char *ignore[16];
static int nignore;

static void kmsg_line(const char *s, int n)
{
    /* "<6>[    1.234] text" -> yellow text without the priority */
    if (n > 2 && s[0] == '<') { const char *e = memchr(s, '>', n < 5 ? n : 5); if (e) { n -= e + 1 - s; s = e + 1; } }
    const char *text = memchr(s, ']', n < 16 ? n : 16);
    text = text ? text + 2 : s;
    for (int i = 0; i < nignore; i++)   /* -x PREFIX: drop periodic driver debug lines */
        if (text < s + n && !strncmp(text, ignore[i], strlen(ignore[i]))) return;
    int saved = attr;
    attr = 3 | 8;
    if (cx) feed((const unsigned char *)"\r\n", 2);
    feed((const unsigned char *)s, n);
    feed((const unsigned char *)"\r\n", 2);
    attr = saved;
}

static pid_t spawn(int *master, char **argv)
{
    struct winsize ws = { .ws_row = ROWS, .ws_col = COLS };
    pid_t pid = forkpty(master, NULL, NULL, &ws);
    if (pid == 0) {
        setenv("TERM", "vt100", 1);
        setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
        execvp(argv[0], argv);
        _exit(127);
    }
    return pid;
}

/* USB keyboard + console button input via /dev/input/event0..7 (hotplug-aware) */
static int evfd[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int shift_down, ctrl_down, select_down, start_down;
static int once_mode = 0;
static const char kmap[64] =
    "\0\0331234567890-=\177\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0* \0";
static const char smap[64] =
    "\0\033!@#$%^&*()_+\177\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0* \0";

static void scan_evdevs(void)
{
    char path[32];
    for (int k = 0; k < 8; k++) {
        if (evfd[k] >= 0) continue;
        snprintf(path, sizeof(path), "/dev/input/event%d", k);
        evfd[k] = open(path, O_RDONLY | O_NONBLOCK);
    }
}

static void handle_key(int master, int code, int val)
{
    if (code == 314) select_down = val != 0;
    if (code == 315) start_down = val != 0;
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) { shift_down = val != 0; return; }
    if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) { ctrl_down = val != 0; return; }
    if (val == 0) return;   /* act on press (1) and repeat (2) */
    const char *seq = NULL; char ch1;
    switch (code) {
    case KEY_UP:    seq = "\033[A"; break;
    case KEY_DOWN:  seq = "\033[B"; break;
    case KEY_RIGHT: seq = "\033[C"; break;
    case KEY_LEFT:  seq = "\033[D"; break;
    case KEY_HOME:  seq = "\033[H"; break;
    case KEY_END:   seq = "\033[F"; break;
    case KEY_DELETE: seq = "\033[3~"; break;
    case 352:       seq = "\r"; break;       /* handheld A (KEY_OK) -> Enter */
    case 158:       seq = "\177"; break;     /* handheld B (KEY_BACK) -> Backspace */
    case 314:       seq = "\t"; break;       /* handheld SELECT -> Tab */
    case 174:       seq = "\003"; break;     /* handheld EXIT -> Ctrl+C */
    default:
        if (code > 0 && code < 60 && kmap[code]) {
            ch1 = shift_down ? smap[code] : kmap[code];
            if (ctrl_down && (ch1 | 32) >= 'a' && (ch1 | 32) <= 'z') ch1 = (ch1 | 32) - 96;
            seq = &ch1; write(master, seq, 1); return;
        }
    }
    if (seq) write(master, seq, strlen(seq));
}

int main(int argc, char **argv)
{
    int i = 1;
    while (i < argc) {
        if (!strcmp(argv[i], "-1")) { once_mode = 1; i++; }
        else if (i + 1 < argc && !strcmp(argv[i], "-r")) { rot = atoi(argv[i + 1]); i += 2; }
        else if (i + 1 < argc && !strcmp(argv[i], "-x") && nignore < 16) { ignore[nignore++] = argv[i + 1]; i += 2; }
        else break;
    }
    if (i < argc && !strcmp(argv[i], "--")) i++;
    static char *dflt[] = { "/bin/sh", "-i", NULL };
    char **cmd = i < argc ? argv + i : dflt;

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { perror("fb0"); return 1; }
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) { perror("fb ioctl"); return 1; }
    if (v.bits_per_pixel != 16) { fprintf(stderr, "need 16 bpp, got %u\n", v.bits_per_pixel); return 1; }
    fb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }
    xres = v.xres; yres = v.yres; stride16 = f.line_length / 2;
    W = rot ? yres : xres; H = rot ? xres : yres;
    COLS = W / FW < MAXC ? W / FW : MAXC; ROWS = H / FH < MAXR ? H / FH : MAXR;
    ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
    /* owl_fb only programs the DE layer when the pan offset changes */
    v.xoffset = 0; v.yoffset = v.yres; ioctl(fd, FBIOPAN_DISPLAY, &v);
    v.yoffset = 0; ioctl(fd, FBIOPAN_DISPLAY, &v);

    memset(ch, ' ', sizeof(ch)); memset(at, 7, sizeof(at));
    redraw();
    char banner[128];
    int bn = snprintf(banner, sizeof(banner), "\033[1;32mATM7051H Linux - fbterm %dx%d%s\033[0m\r\n",
                      COLS, ROWS, once_mode ? " (SELECT+START: back)" : "");
    feed((unsigned char *)banner, bn);

    signal(SIGPIPE, SIG_IGN);
    mkfifo("/tmp/fbterm.in", 0600);
    int in = open("/tmp/fbterm.in", O_RDWR | O_NONBLOCK);
    int km = open("/proc/kmsg", O_RDONLY | O_NONBLOCK);
    int master; pid_t child = spawn(&master, cmd);
    char kbuf[4096]; int klen = 0;

    for (int tick = 0;; tick++) {
        if ((tick & 1) == 0) scan_evdevs();
        struct pollfd p[11] = { { master, POLLIN }, { km, POLLIN }, { in, POLLIN } };
        for (int k = 0; k < 8; k++) { p[3 + k].fd = evfd[k]; p[3 + k].events = POLLIN; }
        if (poll(p, 11, 1000) < 0 && errno != EINTR) break;
        unsigned char buf[4096];
        if (p[0].revents) {
            int n = read(master, buf, sizeof(buf));
            if (n > 0) feed(buf, n);
            else {
                close(master); waitpid(child, NULL, 0);
                if (once_mode) return 0;
                feed((unsigned char *)"\r\n[shell exited, restarting]\r\n", 30);
                sleep(1); child = spawn(&master, cmd);
            }
        }
        if (km >= 0 && p[1].revents & POLLIN) {
            int n = read(km, kbuf + klen, sizeof(kbuf) - klen);
            if (n > 0) {
                klen += n;
                char *s = kbuf, *e;
                while ((e = memchr(s, '\n', kbuf + klen - s))) { kmsg_line(s, e - s); s = e + 1; }
                klen -= s - kbuf; memmove(kbuf, s, klen);
                if (klen == sizeof(kbuf)) { kmsg_line(kbuf, klen); klen = 0; }
            }
        }
        if (in >= 0 && p[2].revents & POLLIN) {
            int n = read(in, buf, sizeof(buf));
            if (n > 0) write(master, buf, n);
        }
        for (int k = 0; k < 8; k++) {
            if (evfd[k] < 0 || !p[3 + k].revents) continue;
            struct input_event ev[16];
            int n = read(evfd[k], ev, sizeof(ev));
            if (n <= 0) { if (errno != EAGAIN) { close(evfd[k]); evfd[k] = -1; } continue; }
            for (int j = 0; j < n / (int)sizeof(ev[0]); j++) {
                if (ev[j].type == EV_KEY) {
                    handle_key(master, ev[j].code, ev[j].value);
                    if (once_mode && select_down && start_down) {
                        kill(child, SIGTERM); close(master); waitpid(child, NULL, WNOHANG); return 0;
                    }
                }
            }
        }
    }
    return 0;
}
