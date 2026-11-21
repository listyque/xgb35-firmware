/* xgbpowerd: Universal Power & Screen-Sleep daemon for XGB35 (ATM7051H + ATC2603C).
 * Listens to:
 *   1. Physical POWER key via ATC2603C PMU_SYS_CTL2[15] (ONOFF_PRESS) & /dev/input/event0 (KEY_POWER=116)
 *   2. Simultaneous VOL+ (115) & VOL- (114) chord on /dev/input/event1 (atc2603c-adckeypad)
 * Actions:
 *   - Short press (50..1150 ms): toggle LCD backlight sleep (bl_power 0 <-> 1)
 *   - Hold (>= 1200 ms): blank screen, sync filesystems, enter ATC2603C S4 deep powerdown (RB_POWER_OFF)
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BL_POWER "/sys/class/backlight/owl_backlight/bl_power"
#define HDMI_EN  "/sys/devices/b02c0000.hdmi/panel.1/owl_panel/hdmi/enable"
#define HDMI_CON "/sys/devices/b02c0000.hdmi/panel.1/owl_panel/hdmi/connected"
#define KEYTEST_PID "/tmp/.xgb_keytest_active"
#define LED1_SYS "/sys/class/custom/led_control"
#define LED2_SYS "/sys/class/custom/led2_control"
#define USB_EN   "/sys/class/android_usb/android0/enable"
#define PKEY_SYS "/sys/devices/b0188000.i2c/i2c-2/2-0065/atc2603c-onoff.0/input/input0/powerkey_status"
#define REG_DBG  "/sys/devices/b0188000.i2c/i2c-2/2-0065/reg_dbg"
#define HOLD_MS  1400

static volatile sig_atomic_t gov_stop = 0;
static void on_gov_sig(int sig) { (void)sig; gov_stop = 1; }

static void set_dcdc1_mv(int mv)
{
    int sel = (mv - 700) / 25;
    if (sel < 12) sel = 12; /* never below 1000 mV (+50mV margin over measured 950mV floor) */
    if (sel > 20) sel = 20; /* never above 1200 mV */
    uint16_t reg11 = (uint16_t)(0xE04FU | ((uint16_t)sel << 7));
    char cmd[24];
    int n = snprintf(cmd, sizeof(cmd), "11=%04x\n", reg11);
    int fd = open(REG_DBG, O_WRONLY);
    if (fd >= 0) { write(fd, cmd, (size_t)n); close(fd); }
}

/* Smart load-scaling V/F governor using empirically verified stable undervolt + OC points (+50mV guardband). */
static int run_cpugov(void)
{
    static const struct { int max_load, mhz, mv; } opp[6] = {
        {  15,  240, 1000 },
        {  35,  600, 1000 },
        {  55,  900, 1025 },
        {  75, 1056, 1075 },
        {  90, 1200, 1150 },
        { 101, 1200, 1200 }, /* was 1260 MHz: above the 1.2 V rail it hung the SoC */
    };
    int mfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mfd < 0) return 1;
    void *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0xB0150000UL);
    if (map == MAP_FAILED) { close(mfd); return 1; }
    volatile uint32_t *corepll = (volatile uint32_t *)map;

    signal(SIGTERM, on_gov_sig);
    signal(SIGINT, on_gov_sig);

    int cur = 2, down_hold = 0;
    uint64_t prev_tot = 0, prev_idle = 0;
    set_dcdc1_mv(1150);
    *corepll = 0x300U | (900U / 12U);

    while (!gov_stop) {
        FILE *f = fopen("/proc/stat", "r");
        if (f) {
            unsigned long long u = 0, n = 0, s = 0, id = 0, io = 0, ir = 0, si = 0, st = 0;
            if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &id, &io, &ir, &si, &st) >= 4) {
                uint64_t idle = id + io;
                uint64_t tot = u + n + s + idle + ir + si + st;
                uint64_t dt = tot - prev_tot, di = idle - prev_idle;
                prev_tot = tot; prev_idle = idle;
                if (dt > 0) {
                    int load = (int)((dt > di ? (dt - di) * 100ULL : 0ULL) / dt);
                    int tgt = 5;
                    for (int i = 0; i < 6; i++) {
                        if (load < opp[i].max_load) { tgt = i; break; }
                    }
                    if (tgt > cur) {
                        down_hold = 0;
                        if (opp[tgt].mv > opp[cur].mv) { set_dcdc1_mv(opp[tgt].mv); usleep(8000); }
                        *corepll = 0x300U | (uint32_t)(opp[tgt].mhz / 12);
                        if (opp[tgt].mv < opp[cur].mv) { usleep(5000); set_dcdc1_mv(opp[tgt].mv); }
                        cur = tgt;
                    } else if (tgt < cur && ++down_hold >= 3) {
                        down_hold = 0;
                        *corepll = 0x300U | (uint32_t)(opp[tgt].mhz / 12);
                        usleep(5000);
                        set_dcdc1_mv(opp[tgt].mv);
                        cur = tgt;
                    } else if (tgt == cur) {
                        down_hold = 0;
                    }
                    FILE *sf = fopen("/tmp/cpugov_status", "w");
                    if (sf) { fprintf(sf, "%d MHz @ %d mV (load %d%%)\n", opp[cur].mhz, opp[cur].mv, load); fclose(sf); }
                }
            }
            fclose(f);
        }
        usleep(100000);
    }

    /* Always restore stock 1150 mV BEFORE 900 MHz on exit */
    set_dcdc1_mv(1150);
    usleep(8000);
    *corepll = 0x300U | (900U / 12U);
    unlink("/tmp/cpugov_status");
    munmap(map, 4096);
    close(mfd);
    return 0;
}

static void write_str(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, val, strlen(val));
        close(fd);
    }
}

static void set_leds(int on)
{
    const char *v = on ? "1\n" : "0\n";
    write_str(LED1_SYS, v);
    write_str(LED2_SYS, v);
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int is_keytest_open(void)
{
    char buf[24];
    int fd = open(KEYTEST_PID, O_RDONLY);
    if (fd < 0) return 0;
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    int pid = (int)strtol(buf, NULL, 10);
    return (pid > 1 && kill(pid, 0) == 0);
}

static void toggle_backlight(void)
{
    if (is_keytest_open())
        return; /* Let user test short POWER press inside xgb-keytest without blanking screen */

    char hcon = '0';
    int cfd = open(HDMI_CON, O_RDONLY);
    if (cfd >= 0) { (void)read(cfd, &hcon, 1); close(cfd); }

    if (hcon == '1') {
        char hen = '1';
        int hfd = open(HDMI_EN, O_RDWR);
        if (hfd >= 0) {
            if (read(hfd, &hen, 1) > 0) {
                int sleep_hdmi = (hen == '1');
                lseek(hfd, 0, SEEK_SET);
                write(hfd, sleep_hdmi ? "0\n" : "1\n", 2);
                set_leds(!sleep_hdmi);
                close(hfd);
                if (!sleep_hdmi) {
                    usleep(120000);
                    int rc = system("/bin/hwtest hdmi-kick >/dev/null 2>&1");
                    (void)rc;
                }
                return;
            }
            close(hfd);
        }
    }

    char ch = '0';
    int fd = open(BL_POWER, O_RDWR);
    if (fd < 0) return;
    if (read(fd, &ch, 1) > 0) {
        int sleep_now = (ch == '0');
        lseek(fd, 0, SEEK_SET);
        write(fd, sleep_now ? "1\n" : "0\n", 2);
        set_leds(!sleep_now);
    }
    close(fd);
}

static int read_sysfs_pkey(void)
{
    char buf[8] = {0};
    int fd = open(PKEY_SYS, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    return (n > 0 && buf[0] == '1') ? 1 : 0;
}

static void do_poweroff(void)
{
    /* Turn off backlight + board GPIO LEDs immediately for instant visual feedback */
    write_str(BL_POWER, "1\n");
    set_leds(0);
    write_str(USB_EN, "0\n");
    /* Wait until physical Power button is released so ATC2603C S4 does not immediately wake back up */
    for (int waited = 0; waited < 5000 && read_sysfs_pkey() == 1; waited += 25)
        usleep(25000);
    usleep(120000);
    sync();
    usleep(80000);
    sync();
    reboot(RB_POWER_OFF);
}

/* ==================== GLOBAL LOSSLESS PNG SCREENSHOT ==================== */

static uint32_t crc32_upd(uint32_t c, const uint8_t *buf, size_t len)
{
    c = ~c;
    for (size_t i = 0; i < len; i++) {
        c ^= buf[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320U & (-(c & 1U)));
    }
    return ~c;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_png_chunk(FILE *fp, const char tag[4], const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, tag, 4);
    if (fwrite(hdr, 1, 8, fp) != 8) return -1;
    if (len > 0 && fwrite(data, 1, len, fp) != len) return -1;
    uint32_t crc = crc32_upd(0, (const uint8_t *)tag, 4);
    if (len > 0) crc = crc32_upd(crc, data, len);
    uint8_t cbuf[4];
    put_be32(cbuf, crc);
    return fwrite(cbuf, 1, 4, fp) == 4 ? 0 : -1;
}

static int save_rgb24_png(const char *path, const uint8_t *rgb_rows, uint32_t w, uint32_t h)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (fwrite(sig, 1, 8, fp) != 8) { fclose(fp); return -1; }

    uint8_t ihdr[13] = {0};
    put_be32(ihdr, w);
    put_be32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; /* 8-bit RGB */
    if (write_png_chunk(fp, "IHDR", ihdr, 13) != 0) { fclose(fp); return -1; }

    /* Pack filtered scanlines (filter byte 0 + 3*w bytes per row) into stored DEFLATE blocks */
    size_t raw_len = (size_t)h * (1U + (size_t)w * 3U);
    size_t nblocks = (raw_len + 65534U) / 65535U;
    size_t zlen = 2U + nblocks * 5U + raw_len + 4U;
    uint8_t *zbuf = malloc(zlen);
    if (!zbuf) { fclose(fp); return -1; }

    size_t zp = 0, rp = 0;
    zbuf[zp++] = 0x78; zbuf[zp++] = 0x01; /* zlib header */
    uint32_t s1 = 1, s2 = 0;
    while (rp < raw_len) {
        size_t chunk = raw_len - rp;
        if (chunk > 65535U) chunk = 65535U;
        int last = (rp + chunk >= raw_len) ? 1 : 0;
        zbuf[zp++] = (uint8_t)last;
        zbuf[zp++] = (uint8_t)(chunk & 0xFFU);
        zbuf[zp++] = (uint8_t)(chunk >> 8);
        uint16_t nlen = (uint16_t)~chunk;
        zbuf[zp++] = (uint8_t)(nlen & 0xFFU);
        zbuf[zp++] = (uint8_t)(nlen >> 8);
        memcpy(zbuf + zp, rgb_rows + rp, chunk);
        for (size_t i = 0; i < chunk; i++) {
            s1 += rgb_rows[rp + i];
            if (s1 >= 65521U) s1 -= 65521U;
            s2 += s1;
            if (s2 >= 65521U) s2 -= 65521U;
        }
        zp += chunk;
        rp += chunk;
    }
    put_be32(zbuf + zp, (s2 << 16) | s1);
    zp += 4;

    int rc = write_png_chunk(fp, "IDAT", zbuf, (uint32_t)zp);
    free(zbuf);
    if (rc == 0) rc = write_png_chunk(fp, "IEND", NULL, 0);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    return rc;
}

static int take_screenshot(char *out_path, size_t out_sz)
{
    int use_fb1 = (access("/tmp/.X11-unix/X0", F_OK) == 0);
    const char *fb_dev = use_fb1 ? "/dev/fb1" : "/dev/fb0";
    int fd = open(fb_dev, O_RDWR);
    if (fd < 0) return -1;

    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) {
        close(fd);
        return -1;
    }
    uint16_t *fb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { close(fd); return -1; }

    uint32_t stride_px = f.line_length / 2U;
    uint32_t pan_px = (v.yoffset < v.yres_virtual) ? (v.yoffset * stride_px) : 0U;
    if ((pan_px + v.yres * stride_px) * 2U > f.smem_len) pan_px = 0U;
    uint16_t *src = fb + pan_px;

    uint32_t w = use_fb1 ? v.xres : 480U;
    uint32_t h = use_fb1 ? v.yres : 320U;
    size_t row_bytes = 1U + (size_t)w * 3U;
    uint8_t *rows = malloc((size_t)h * row_bytes);
    if (!rows) { munmap(fb, f.smem_len); close(fd); return -1; }

    for (uint32_t y = 0; y < h; y++) {
        uint8_t *dst = rows + (size_t)y * row_bytes;
        *dst++ = 0; /* PNG filter None */
        if (use_fb1) {
            const uint16_t *srow = src + y * stride_px;
            for (uint32_t x = 0; x < w; x++) {
                uint16_t px = srow[x];
                *dst++ = (uint8_t)(((px >> 11) & 31U) * 255U / 31U);
                *dst++ = (uint8_t)(((px >> 5) & 63U) * 255U / 63U);
                *dst++ = (uint8_t)((px & 31U) * 255U / 31U);
            }
        } else {
            uint32_t sy = 319U - y;
            for (uint32_t x = 0; x < 480U; x++) {
                uint16_t px = src[x * stride_px + sy];
                *dst++ = (uint8_t)(((px >> 11) & 31U) * 255U / 31U);
                *dst++ = (uint8_t)(((px >> 5) & 63U) * 255U / 63U);
                *dst++ = (uint8_t)((px & 31U) * 255U / 31U);
            }
        }
    }

    /* Brief 100ms shutter flash bar at top of screen + LED blink (after pixels are already copied!) */
    set_leds(1);
    size_t flash_px = use_fb1 ? (size_t)w * 6U : 320U * 6U;
    uint16_t *saved_bar = malloc(flash_px * 2U);
    if (saved_bar) {
        memcpy(saved_bar, src, flash_px * 2U);
        for (size_t i = 0; i < flash_px; i++) src[i] = 0xFFFFU;
    }

    const char *dir = "/mnt/sd/screenshots";
    if (access("/mnt/sd", W_OK) != 0) {
        (void)system("/bin/mount-games >/dev/null 2>&1");
    }
    if (access("/mnt/sd", W_OK) != 0) dir = "/tmp/screenshots";
    mkdir(dir, 0777);

    char path[256] = "";
    for (int idx = 1; idx <= 9999; idx++) {
        snprintf(path, sizeof(path), "%s/shot_%04d.png", dir, idx);
        if (access(path, F_OK) != 0) break;
    }
    int rc = save_rgb24_png(path, rows, w, h);
    free(rows);

    usleep(90000);
    if (saved_bar) {
        memcpy(src, saved_bar, flash_px * 2U);
        free(saved_bar);
    }
    set_leds(0);
    munmap(fb, f.smem_len);
    close(fd);

    if (rc == 0 && out_path) snprintf(out_path, out_sz, "%s", path);
    return rc;
}

/* ==================== GLOBAL VOLUME CONTROL + OSD BAR ==================== */

static void draw_vol_osd_fb0(int vol)
{
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) return;
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) {
        close(fd); return;
    }
    if (v.bits_per_pixel != 16 || v.xres != 320 || v.yres != 480) { close(fd); return; }
    uint16_t *fb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { close(fd); return; }
    int stride16 = (int)(f.line_length / 2);
    /* Landscape 480x320 top-center pill: x=150..329 (w=180), y=10..31 (h=22) */
    int bx = 150, by = 10, bw = 180, bh = 22;
    int bar_x = bx + 26, bar_w = bw - 36;
    int fill_w = (vol * bar_w) / 40;
    uint16_t c_bg = 0x18E5, c_bd = 0x9CF3, c_tr = 0x31A6;
    uint16_t c_fg = vol == 0 ? 0xF800 : 0x2F33; /* red if muted, mint-green otherwise */
    for (int Y = by; Y < by + bh; Y++) {
        int fb_x = (int)v.xres - 1 - Y;
        for (int X = bx; X < bx + bw; X++) {
            int border = (X == bx || X == bx + bw - 1 || Y == by || Y == by + bh - 1);
            uint16_t c = border ? c_bd : c_bg;
            /* Tiny speaker icon at X=bx+8..bx+19, Y=by+5..by+16 */
            int lx = X - (bx + 8), ly = Y - (by + 5);
            if (lx >= 0 && lx < 10 && ly >= 0 && ly < 12) {
                if ((lx <= 3 && ly >= 3 && ly <= 8) ||
                    (lx >= 4 && lx <= 7 && ly >= (7 - lx) && ly <= (4 + lx)))
                    c = vol == 0 ? 0xF800 : 0xFFFF;
            }
            if (X >= bar_x && X < bar_x + bar_w && Y >= by + 7 && Y < by + 15) {
                c = (X - bar_x < fill_w) ? c_fg : c_tr;
            }
            fb[X * stride16 + fb_x] = c;
        }
    }
    munmap(fb, f.smem_len);
    close(fd);
}

static void apply_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 40) vol = 40;
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1; "
             "LD_LIBRARY_PATH=/lib /bin/tinymix 35 %d >/dev/null 2>&1",
             vol, vol > 0 ? 1 : 0);
    int rc = system(cmd);
    (void)rc;
    FILE *vf = fopen("/tmp/.xgb_vol", "w");
    if (vf) { fprintf(vf, "%d %lld\n", vol, now_ms()); fclose(vf); }
    draw_vol_osd_fb0(vol);
}

static void update_percbat(void)
{
    int cap = -1;
    FILE *f = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (f) { (void)fscanf(f, "%d", &cap); fclose(f); }
    if (cap < 0) return;
    if (cap > 100) cap = 100;
    char st[24] = "";
    FILE *sf = fopen("/sys/class/power_supply/battery/status", "r");
    if (sf) { (void)fgets(st, sizeof(st), sf); fclose(sf); }
    /* Onion MainUI treats 500 in /tmp/percBat as charging icon */
    int val = (strncmp(st, "Charging", 8) == 0) ? 500 : cap;
    FILE *of = fopen("/tmp/.percBat.tmp", "w");
    if (of) { fprintf(of, "%d\n", val); fclose(of); rename("/tmp/.percBat.tmp", "/tmp/percBat"); }
}

int main(int argc, char **argv)
{
    if (strstr(argv[0], "xgbcpud") || (argc > 1 && strcmp(argv[1], "--cpugov") == 0))
        return run_cpugov();
    if ((strstr(argv[0], "devmem") || (argc > 1 && strcmp(argv[1], "--devmem") == 0))) {
        int a = strstr(argv[0], "devmem") ? 1 : 2;
        if (argc <= a) return 1;
        unsigned long phys = strtoul(argv[a], NULL, 0);
        int mfd = open("/dev/mem", O_RDWR | O_SYNC);
        if (mfd < 0) return 1;
        void *map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, phys & ~0xFFFUL);
        if (map == MAP_FAILED) { close(mfd); return 1; }
        volatile uint32_t *reg = (volatile uint32_t *)((char *)map + (phys & 0xFFFUL));
        if (argc >= a + 3) *reg = (uint32_t)strtoul(argv[a + 2], NULL, 0);
        else printf("0x%08X\n", *reg);
        munmap(map, 4096);
        close(mfd);
        return 0;
    }
    if (strstr(argv[0], "screenshot") || (argc > 1 && strcmp(argv[1], "--screenshot") == 0)) {
        char saved[256] = "";
        if (take_screenshot(saved, sizeof(saved)) == 0) {
            printf("Saved screenshot: %s\n", saved);
            return 0;
        }
        fprintf(stderr, "Screenshot failed\n");
        return 1;
    }
    if (argc > 1 && strcmp(argv[1], "--off") == 0) {
        do_poweroff();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--wait-key") == 0) {
        int fd = open("/dev/input/event1", O_RDONLY);
        if (fd < 0) { sleep(5); return 0; }
        struct input_event ev;
        int saw_down = 0;
        while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type != EV_KEY || ev.code == 114 || ev.code == 115) continue;
            if (ev.value == 1) saw_down = 1;
            else if (ev.value == 0 && saw_down) break;
        }
        close(fd);
        return 0;
    }
    int fds[4] = { -1, -1, -1, -1 };
    int pkey_hw = 0, ev_pwr = 0, v_up = 0, v_dn = 0;
    int pressed = 0, triggered = 0, shot_latched = 0;
    int cur_vol = 28;
    long long down_at = 0, last_bat = 0;
    update_percbat();

    for (;;) {
        long long tnow = now_ms();
        if (tnow - last_bat >= 5000) { last_bat = tnow; update_percbat(); }
        for (int i = 0; i < 4; i++) {
            if (fds[i] < 0) {
                char devp[32];
                snprintf(devp, sizeof(devp), "/dev/input/event%d", i);
                fds[i] = open(devp, O_RDONLY | O_NONBLOCK);
            }
        }

        struct pollfd p[4] = {
            { fds[0], POLLIN, 0 },
            { fds[1], POLLIN, 0 },
            { fds[2], POLLIN, 0 },
            { fds[3], POLLIN, 0 },
        };
        poll(p, 4, pressed ? 40 : 80);

        int hw = read_sysfs_pkey();
        if (hw >= 0) pkey_hw = hw;

        int kb_shot = 0, vol_delta = 0;
        for (int i = 0; i < 4; i++) {
            if (fds[i] < 0 || !(p[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;
            struct input_event ev[16];
            int n = read(fds[i], ev, sizeof(ev));
            if (n <= 0) { close(fds[i]); fds[i] = -1; continue; }
            for (int j = 0; j < n / (int)sizeof(ev[0]); j++) {
                if (ev[j].type != EV_KEY) continue;
                int down = ev[j].value != 0;
                if (ev[j].code == 116 && ev[j].value != 2) ev_pwr = down;
                else if (ev[j].code == 115) {
                    if (ev[j].value != 2) v_up = down;
                    if (ev[j].value == 1 || ev[j].value == 2) vol_delta += 2;
                } else if (ev[j].code == 114) {
                    if (ev[j].value != 2) v_dn = down;
                    if (ev[j].value == 1 || ev[j].value == 2) vol_delta -= 2;
                } else if (ev[j].value == 1 && (ev[j].code == 99 || ev[j].code == 88)) {
                    kb_shot = 1; /* PrintScreen or F12 */
                }
            }
        }

        /* Global Screenshot Hotkey: POWER + VOL- (when VOL+ is not held) OR PrintScreen / F12 */
        int pwr_now = (pkey_hw || ev_pwr);
        if (kb_shot || (pwr_now && v_dn && !v_up && !shot_latched)) {
            shot_latched = 1;
            triggered = 1; /* suppress screen-sleep & poweroff for this POWER press! */
            (void)take_screenshot(NULL, 0);
        }
        if (!v_dn && !pwr_now) shot_latched = 0;

        /* Standalone VOL+ / VOL- adjusts DAC PA Volume (0..40) + draws Volume OSD */
        if (vol_delta != 0 && !pwr_now && !(v_up && v_dn)) {
            cur_vol += vol_delta;
            if (cur_vol < 0) cur_vol = 0;
            if (cur_vol > 40) cur_vol = 40;
            apply_volume(cur_vol);
        }

        int active = (pwr_now || (v_up && v_dn)) ? 1 : 0;
        if (active && !pressed) {
            pressed = 1;
            if (!shot_latched) triggered = 0;
            down_at = now_ms();
        } else if (!active && pressed) {
            if (!triggered && (now_ms() - down_at) >= 50)
                toggle_backlight();
            pressed = 0;
            triggered = 0;
        }

        if (pressed && !triggered && (now_ms() - down_at) >= HOLD_MS) {
            triggered = 1;
            do_poweroff();
        }
    }
}
