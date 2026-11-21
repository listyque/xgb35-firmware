/* xgbdisplay: Multi-screen controller & blitter for ATM7051H / XGB35
 * Modes:
 *   display-mode single            - external HDMI only (3.5" LCD & PATH1 off)
 *   display-mode mirror-hdmi       - duplicate big HDMI screen onto 3.5" LCD (rotated 90 deg)
 *   display-mode mirror-lcd        - duplicate 3.5" 480x320 LCD onto big HDMI via DE hardware scaler (auto-hotplug!)
 *   display-mode off               - cleanly power off HDMI output & DE_PATH0 (zero green stripes)
 *   display-mode extend [x_off]    - copy+rotate (x_off..x_off+479, 0..319) of /dev/fb1 onto 3.5" LCD
 *   display-mode status            - print active multi-screen mode
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define HDMI_SYS "/sys/devices/b02c0000.hdmi/panel.1/owl_panel/hdmi"

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }

static void write_str(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { (void)write(fd, val, strlen(val)); close(fd); }
}

static int read_str(const char *path, char *buf, size_t sz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { buf[0] = '\0'; return -1; }
    ssize_t n = read(fd, buf, sz - 1);
    close(fd);
    if (n <= 0) { buf[0] = '\0'; return -1; }
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return 0;
}

static volatile uint32_t *map_phys(uint32_t base, size_t len, int *mfd_out)
{
    int mfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mfd < 0) return NULL;
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, base);
    if (p == MAP_FAILED) { close(mfd); return NULL; }
    *mfd_out = mfd;
    return (volatile uint32_t *)p;
}

static void kill_other_workers(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "for p in $(pidof xgbdisplay display-mode 2>/dev/null); do "
             "[ \"$p\" != \"%d\" ] && kill \"$p\" 2>/dev/null; done",
             (int)getpid());
    (void)system(cmd);
    usleep(40000);
}

static void lcd_power(int on)
{
    int mfd = -1;
    volatile uint32_t *de = map_phys(0xB02F0000, 0x5000, &mfd);
    if (on) {
        if (de) { de[0x148 / 4] = 1; de[0x144 / 4] = 1; }
        write_str("/sys/class/graphics/fb0/blank", "0\n");
        if (access("/bin/st7796s-init", X_OK) == 0)
            (void)system("/bin/st7796s-init >/dev/null 2>&1; /bin/devmem 0xb01c0044 32 0x60600000 2>/dev/null; /bin/devmem 0xb01c0050 32 0x01788100 2>/dev/null");
        write_str("/sys/class/backlight/owl_backlight/bl_power", "0\n");
    } else {
        write_str("/sys/class/backlight/owl_backlight/bl_power", "4\n");
        if (de) de[0x148 / 4] = 0;
    }
    if (de) { munmap((void *)de, 0x5000); close(mfd); }
}

static void save_state(const char *m)
{
    FILE *f = fopen("/tmp/display_mode", "w");
    if (f) { fprintf(f, "%s\n", m); fclose(f); }
}

static int daemonize_worker(const char *mode_tag)
{
    kill_other_workers();
    save_state(mode_tag);
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid > 0) return 0;
    setsid();
    int nullfd = open("/dev/null", O_RDWR);
    if (nullfd >= 0) { dup2(nullfd, 0); dup2(nullfd, 1); dup2(nullfd, 2); if (nullfd > 2) close(nullfd); }
    signal(SIGTERM, on_sig);
    signal(SIGINT, on_sig);
    return -1; /* child continues */
}

static void apply_mirror_lcd_hw(uint32_t fb1_phys, uint32_t fb1_line_bytes)
{
    char mode_str[64] = "";
    read_str(HDMI_SYS "/mode", mode_str, sizeof(mode_str));
    int is_1080 = (strstr(mode_str, "1920x1080") != NULL);
    uint32_t ow = is_1080 ? 1920U : 1280U;
    uint32_t oh = is_1080 ? 1080U : 720U;

    int cfd = -1, dfd = -1, hfd = -1;
    volatile uint32_t *cmu  = map_phys(0xB0150000, 0x1000, &cfd);
    volatile uint32_t *de   = map_phys(0xB02F0000, 0x5000, &dfd);
    volatile uint32_t *hdmi = map_phys(0xB02C0000, 0x1000, &hfd);

    write_str(HDMI_SYS "/need_edid", "1\n");
    char en_str[16] = "";
    read_str(HDMI_SYS "/enable", en_str, sizeof(en_str));
    if (en_str[0] != '1') {
        write_str(HDMI_SYS "/enable", "1\n");
        usleep(120000);
        read_str(HDMI_SYS "/mode", mode_str, sizeof(mode_str));
        is_1080 = (strstr(mode_str, "1920x1080") != NULL);
        ow = is_1080 ? 1920U : 1280U;
        oh = is_1080 ? 1080U : 720U;
    }

    /* Always enforce 300MHz DECLK (from DEVPLL) + factory 48MHz DISPLAYPLL / 12MHz LCDCLK AFTER HDMI enable! */
    if (cmu) {
        cmu[0x30 / 4] = 0x00001111U;
        cmu[0x10 / 4] = 0x00000108U;
        cmu[0x24 / 4] = 0x00000003U;
        munmap((void *)cmu, 0x1000);
        close(cfd);
    }

    if (de) {
        static const uint32_t coef[8] = {
            0x00004000U, 0x00083800U, 0x00103000U, 0x00182800U,
            0x00202000U, 0x00281800U, 0x00301000U, 0x00380800U
        };
        uint32_t out_sz = ((oh - 1U) << 16) | (ow - 1U);
        de[0x110 / 4] = out_sz;
        de[0x220 / 4] = 0x04080770U; /* RGB565 + RGB->YUV BT.709 CSC enabled! */
        de[0x250 / 4] = fb1_phys;
        de[0x268 / 4] = fb1_line_bytes / 8U;
        de[0x224 / 4] = (319U << 16) | 479U;
        de[0x228 / 4] = out_sz;
        de[0x22C / 4] = (((320U << 13) / oh) << 16) | ((480U << 13) / ow);
        for (int i = 0; i < 8; i++) de[(0x230 / 4) + i] = coef[i];
        de[0x100 / 4] = (de[0x100 / 4] & ~0x2FEU) | 0x01U; /* progressive + OVL0 on */
        de[0x108 / 4] = 1U;
        de[0x104 / 4] = 1U;
        usleep(25000);
        if (de[0x104 / 4] != 0) {
            de[0x108 / 4] = 0U;
            usleep(20000);
            de[0x108 / 4] = 1U;
            de[0x104 / 4] = 1U;
        }
        munmap((void *)de, 0x5000);
        close(dfd);
    }

    if (hdmi) {
        /* Write YCbCr 4:4:4 AVI InfoFrame so monitor decodes true sRGB colors (never green/magenta!) */
        hdmi[0x0F8 / 4] = 0x000D0282U;
        hdmi[0x0FC / 4] = is_1080 ? 0x006840B7U : 0x006840C3U;
        hdmi[0x100 / 4] = is_1080 ? 0x00000010U : 0x00000004U;
        hdmi[0x0F4 / 4] = 0x00001107U;
        hdmi[0x0E8 / 4] = 0x80010105U;
        hdmi[0x058 / 4] = 0x08000021U;
        munmap((void *)hdmi, 0x1000);
        close(hfd);
    }
}

static void hdmi_power_off(void)
{
    int cfd = -1, dfd = -1;
    volatile uint32_t *de = map_phys(0xB02F0000, 0x5000, &dfd);
    if (de) {
        de[0x100 / 4] = 0U;
        de[0x104 / 4] = 1U;
        de[0x108 / 4] = 0U;
        munmap((void *)de, 0x5000);
        close(dfd);
    }
    write_str(HDMI_SYS "/enable", "0\n");
    volatile uint32_t *cmu = map_phys(0xB0150000, 0x1000, &cfd);
    if (cmu) {
        cmu[0x10 / 4] = 0x00000108U;
        cmu[0x24 / 4] = 0x00000003U;
        munmap((void *)cmu, 0x1000);
        close(cfd);
    }
}

int main(int argc, char **argv)
{
    const char *m = argc > 1 ? argv[1] : "status";
    if (!strcmp(m, "status")) {
        FILE *f = fopen("/tmp/display_mode", "r");
        char buf[64] = "single\n";
        if (f) { (void)fgets(buf, sizeof(buf), f); fclose(f); }
        printf("Display mode: %s", buf);
        return 0;
    }
    if (!strcmp(m, "off")) {
        kill_other_workers();
        hdmi_power_off();
        save_state("off");
        return 0;
    }
    if (!strcmp(m, "single")) {
        kill_other_workers();
        lcd_power(0);
        (void)system("/bin/hwtest hdmi-kick >/dev/null 2>&1");
        save_state("single");
        return 0;
    }

    int fd0 = open("/dev/fb0", O_RDWR);
    int fd1 = open("/dev/fb1", O_RDWR);
    if (fd0 < 0 || fd1 < 0) { perror("open fb0/fb1"); return 1; }
    struct fb_var_screeninfo v0, v1;
    struct fb_fix_screeninfo f0, f1;
    if (ioctl(fd0, FBIOGET_VSCREENINFO, &v0) || ioctl(fd0, FBIOGET_FSCREENINFO, &f0) ||
        ioctl(fd1, FBIOGET_VSCREENINFO, &v1) || ioctl(fd1, FBIOGET_FSCREENINFO, &f1)) {
        perror("fb ioctl"); return 1;
    }
    uint16_t *fb0 = mmap(NULL, f0.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd0, 0);
    uint16_t *fb1 = mmap(NULL, f1.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
    if (fb0 == MAP_FAILED || fb1 == MAP_FAILED) { perror("mmap fb"); return 1; }
    uint32_t s1 = f1.line_length / 2;

    if (!strcmp(m, "mirror-hdmi") || !strcmp(m, "extend-worker")) {
        int is_ext = !strcmp(m, "extend-worker");
        int x_off = (is_ext && argc > 2) ? atoi(argv[2]) : 0;
        lcd_power(1);
        int rc = daemonize_worker(is_ext ? "extend" : "mirror-hdmi");
        if (rc >= 0) return rc;

        uint32_t sx[480], sy[320];
        uint32_t src_w = is_ext ? 480 : v1.xres;
        uint32_t src_h = is_ext ? (v1.yres < 320 ? v1.yres : 320) : v1.yres;
        if ((uint32_t)x_off + src_w > v1.xres) x_off = v1.xres > 480 ? (int)(v1.xres - 480) : 0;
        for (int x = 0; x < 480; x++) sx[x] = (uint32_t)x_off + ((uint32_t)x * src_w / 480U);
        for (int y = 0; y < 320; y++) sy[y] = ((uint32_t)y * src_h / 320U) * s1;

        uint16_t tmp[320 * 480];
        while (!g_stop) {
            uint32_t pan_off = (v1.yoffset < v1.yres_virtual) ? (v1.yoffset * s1) : 0;
            for (int x = 0; x < 480; x++) {
                uint16_t *dst_col = tmp + x * 320;
                uint32_t col_x = sx[x];
                for (int y = 0; y < 320; y++)
                    dst_col[319 - y] = fb1[pan_off + sy[y] + col_x];
            }
            memcpy(fb0, tmp, sizeof(tmp));
            usleep(38000); /* ~26 FPS */
        }
        return 0;
    }

    if (!strcmp(m, "mirror-lcd")) {
        int rc = daemonize_worker("mirror-lcd");
        if (rc >= 0) return rc;

        static uint16_t cur_buf[320 * 480];
        int was_conn = -1, have_prev = 0, conn = 0, conn_tick = 0, idle_ticks = 0;
        uint32_t prev_hash = 0, prev_yoff = ~0U;

        while (!g_stop) {
            /* Check HDMI plug state every ~1s */
            if (was_conn < 0 || ++conn_tick >= 12) {
                conn_tick = 0;
                char conn_s[16] = "0";
                read_str(HDMI_SYS "/connected", conn_s, sizeof(conn_s));
                conn = (conn_s[0] == '1');
            }

            if (!conn) {
                if (was_conn != 0) {
                    hdmi_power_off();
                    was_conn = 0;
                    have_prev = 0;
                }
                usleep(500000); /* 0.0% CPU idle while no HDMI cable connected */
                continue;
            }

            int changed = 0;
            if (ioctl(fd0, FBIOGET_VSCREENINFO, &v0) == 0) {
                uint32_t off0 = (v0.yoffset * (f0.line_length / 2));
                if (off0 + 320U * 480U > f0.smem_len / 2) off0 = 0;
                /* Ultra-light 64-word sample hash (256 bytes vs 307 KB -> 0.0% CPU when static!) */
                const uint32_t *wsrc = (const uint32_t *)(fb0 + off0);
                uint32_t h = 2166136261U ^ v0.yoffset;
                for (int k = 0; k < 64; k++) {
                    h = (h ^ wsrc[k * 1193]) * 16777619U;
                }
                if (!have_prev || h != prev_hash || v0.yoffset != prev_yoff) {
                    changed = 1;
                    have_prev = 1;
                    prev_hash = h;
                    prev_yoff = v0.yoffset;
                    memcpy(cur_buf, fb0 + off0, sizeof(cur_buf));
                    for (int y0 = 0; y0 < 320; y0 += 16) {
                        for (int x0 = 0; x0 < 480; x0 += 16) {
                            for (int y = y0; y < y0 + 16; y++) {
                                uint16_t *row = fb1 + (uint32_t)y * s1 + x0;
                                int sy_idx = 319 - y;
                                for (int dx = 0; dx < 16; dx++)
                                    row[dx] = cur_buf[(x0 + dx) * 320 + sy_idx];
                            }
                        }
                    }
                }
            }

            if (was_conn != 1) {
                apply_mirror_lcd_hw((uint32_t)f1.smem_start, f1.line_length);
                was_conn = 1;
            }

            if (changed) {
                idle_ticks = 0;
                usleep(40000); /* ~25 FPS while animating */
            } else {
                if (idle_ticks < 20) idle_ticks++;
                usleep(idle_ticks > 4 ? 100000 : 50000); /* 10 Hz / 256-byte check = 0.0% CPU */
            }
        }
        return 0;
    }

    if (!strcmp(m, "extend")) {
        int ext_w = argc > 2 ? atoi(argv[2]) : (v1.xres >= 2200 ? 1920 : (v1.xres >= 1700 ? 1280 : 0));
        if (ext_w > 0) {
            int mfd = -1;
            volatile uint32_t *de = map_phys(0xB02F0000, 0x5000, &mfd);
            if (de) {
                uint32_t h = v1.yres;
                uint32_t sz = ((h - 1U) << 16) | ((uint32_t)ext_w - 1U);
                de[0x110 / 4] = sz;
                de[0x224 / 4] = sz;
                de[0x228 / 4] = sz;
                de[0x22C / 4] = 0x20002000;
                de[0x268 / 4] = f1.line_length / 8;
                de[0x104 / 4] = 1;
                munmap((void *)de, 0x5000);
                close(mfd);
            }
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "/bin/display-mode extend-worker %d", ext_w);
            return system(cmd);
        }
        write_str("/tmp/desktop_dualscreen", "extend\n");
        (void)system("killall lxsession openbox lxpanel pcmanfm jwm Xorg 2>/dev/null");
        return 0;
    }

    fprintf(stderr, "Usage: display-mode [single|mirror-hdmi|mirror-lcd|off|extend|status]\n");
    return 1;
}
