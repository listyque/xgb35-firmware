/* hwtest: hardware exerciser for ATM7051H / XGB35 Linux.
 *   hwtest status          - summary of display, HDMI, audio, input, USB, storage
 *   hwtest sound [0|1]     - 32-bit stereo chime on card 0 (speaker/earphone) or 1 (HDMI)
 *   hwtest hdmi            - 1280x720 test bars on /dev/fb1 + DE pan kick
 *   hwtest buttons [sec]   - live button & analog stick monitor (hold SELECT+START to exit)
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static const char *key_name(int c)
{
    switch (c) {
    case 103: return "UP";     case 108: return "DOWN";
    case 105: return "LEFT";   case 106: return "RIGHT";
    case 352: return "A(OK)";  case 158: return "B(BACK)";
    case 139: return "X(MENU)"; case 308: return "Y";
    case 314: return "SELECT"; case 315: return "START";
    case 312: return "L2";     case 313: return "R2";
    case 412: return "L1(PREV)"; case 407: return "R1(NEXT)";
    case 317: return "L3";     case 318: return "R3";
    case 115: return "VOL+";   case 114: return "VOL-";
    case 174: return "EXIT";   case 431: return "FN(431)";
    case 116: return "POWER";
    default:  return NULL;
    }
}

static int cmd_sound(int card)
{
    const int rate = 44100, frames = rate * 9 / 20; /* 450 ms, 3 notes */
    const int freqs[3] = { 523, 659, 784 };
    FILE *f = fopen("/tmp/chime.wav", "wb");
    if (!f) { perror("/tmp/chime.wav"); return 1; }
    uint32_t data_bytes = (uint32_t)frames * 8, riff_bytes = 36 + data_bytes;
    uint16_t fmt = 1, ch = 2, align = 8, bits = 32;
    uint32_t sr = rate, br = rate * 8, fmt_len = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff_bytes, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt_len, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);
    for (int i = 0; i < frames; i++) {
        int hz = freqs[i * 3 / frames];
        int32_t s = (int32_t)(450000000.0 * sin(6.283185307 * hz * i / rate));
        int32_t lr[2] = { s, s };
        fwrite(lr, 8, 1, f);
    }
    fclose(f);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "LD_LIBRARY_PATH=/lib /bin/tinyplay /tmp/chime.wav -D %d -d 0", card);
    return system(cmd);
}

static int cmd_hdmi_set(int w, int h, int realloc_mode);

static int cmd_hdmi(void)
{
    int fd = open("/dev/fb1", O_RDWR);
    if (fd < 0) { perror("/dev/fb1"); return 1; }
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) { perror("fb1 ioctl"); close(fd); return 1; }
    uint8_t *mem = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) { perror("mmap fb1"); close(fd); return 1; }
    static const uint16_t bars16[8] = { 0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000 };
    static const uint32_t bars32[8] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };
    for (uint32_t y = 0; y < v.yres; y++) {
        for (uint32_t x = 0; x < v.xres; x++) {
            int border = x < 4 || y < 4 || x >= v.xres - 4 || y >= v.yres - 4;
            int idx = border ? 0 : (x * 8 / v.xres);
            if (v.bits_per_pixel == 32)
                *(uint32_t *)(mem + y * f.line_length + x * 4) = bars32[idx];
            else
                *(uint16_t *)(mem + y * f.line_length + x * 2) = bars16[idx];
        }
    }
    munmap(mem, f.smem_len);
    close(fd);
    cmd_hdmi_set(0, 0, 0);
    printf("HDMI fb1 active: %ux%u %ubpp (stride %u)\n", v.xres, v.yres, v.bits_per_pixel, f.line_length);
    return 0;
}

static int cmd_hdmi_set(int w, int h, int realloc_mode)
{
    int fd = open("/dev/fb1", O_RDWR);
    if (fd < 0) { perror("/dev/fb1"); return 1; }
    struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v)) { perror("FBIOGET_VSCREENINFO"); close(fd); return 1; }
    if (realloc_mode) {
        v.xres = (uint32_t)w; v.yres = (uint32_t)h;
        v.xres_virtual = (uint32_t)w; v.yres_virtual = (uint32_t)(h * 2);
        v.bits_per_pixel = 16; v.xoffset = 0; v.yoffset = 0;
        if (ioctl(fd, FBIOPUT_VSCREENINFO, &v)) { perror("FBIOPUT_VSCREENINFO"); close(fd); return 1; }
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) { perror("fb1 ioctl"); close(fd); return 1; }
    uint8_t *mem = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem != MAP_FAILED) {
        if (realloc_mode)
            memset(mem, 0, f.smem_len);
        else if (f.smem_len >= v.yres * f.line_length * 2)
            memset(mem + v.yres * f.line_length, 0, v.yres * f.line_length);
        munmap(mem, f.smem_len);
    }
    ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
    /* owl_fb_set_par reallocates DMA without owl_de_path_apply; yoffset bounce forces DE base sync */
    v.xoffset = 0;
    v.yoffset = (v.yres_virtual >= v.yres * 2) ? v.yres : 0;
    ioctl(fd, FBIOPAN_DISPLAY, &v);
    v.yoffset = 0;
    ioctl(fd, FBIOPAN_DISPLAY, &v);
    close(fd);

    /* Ensure DE OVL0 uses 1:1 native v.xres x v.yres input size (not 320x480 DTB clone crop) */
    int mfd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mfd >= 0) {
        volatile uint32_t *de = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0xB02F0000);
        if (de != MAP_FAILED) {
            uint32_t sz = ((v.yres - 1) << 16) | (v.xres - 1);
            de[0x110 / 4] = sz;
            de[0x114 / 4] = 0;
            de[0x220 / 4] = 0x04080770;
            de[0x250 / 4] = (uint32_t)f.smem_start;
            de[0x268 / 4] = f.line_length / 8;
            de[0x224 / 4] = sz;
            de[0x228 / 4] = sz;
            de[0x22C / 4] = 0x20002000;
            de[0x230 / 4] = 0x00004000;
            de[0x234 / 4] = 0x00083800;
            de[0x238 / 4] = 0x00103000;
            de[0x23C / 4] = 0x00182800;
            de[0x240 / 4] = 0x00202000;
            de[0x244 / 4] = 0x00281800;
            de[0x248 / 4] = 0x00301000;
            de[0x24C / 4] = 0x00380800;
            /* Clear bit 9 (0x200 = INTERLACE!) so progressive 720p/1080p renders full height with FCR0=0 */
            de[0x100 / 4] = (de[0x100 / 4] & ~0x2FEU) | 0x01U;
            de[0x104 / 4] = 1;
            usleep(25000);
            if (de[0x104 / 4] & 1U) {
                de[0x108 / 4] = 0;
                usleep(25000);
                de[0x402C / 4] = 0x1FU;
                de[0x108 / 4] = 1;
                de[0x104 / 4] = 1;
            }
            munmap((void *)de, 0x1000);
        }
        close(mfd);
    }
    return 0;
}

static int cmd_buttons(int timeout_sec)
{
    struct pollfd p[8]; char name[64], path[32]; int nfd = 0;
    for (int i = 0; i < 8; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        strcpy(name, "?"); ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        ioctl(fd, EVIOCGRAB, 1);
        printf("Watching %s (%s)\n", path, name);
        p[nfd].fd = fd; p[nfd].events = POLLIN; nfd++;
    }
    if (!nfd) { printf("No /dev/input/event* devices\n"); return 1; }
    printf("Press buttons or move sticks (hold SELECT+START or wait %ds to exit)...\n", timeout_sec);
    fflush(stdout);
    int sel = 0, st = 0, last_abs[64] = { 0 };
    for (;;) {
        int r = poll(p, nfd, timeout_sec * 1000);
        if (r <= 0) { printf("Done (idle timeout).\n"); break; }
        for (int i = 0; i < nfd; i++) {
            if (!p[i].revents) continue;
            struct input_event ev[16];
            int n = read(p[i].fd, ev, sizeof(ev));
            for (int j = 0; j < n / (int)sizeof(ev[0]); j++) {
                if (ev[j].type == EV_KEY) {
                    if (ev[j].code == 314) sel = ev[j].value != 0;
                    if (ev[j].code == 315) st = ev[j].value != 0;
                    const char *kn = key_name(ev[j].code);
                    if (kn) printf("  KEY %-10s (%3d) %s\n", kn, ev[j].code, ev[j].value == 1 ? "DOWN" : ev[j].value ? "REP" : "UP");
                    else    printf("  KEY code=%-5d       %s\n", ev[j].code, ev[j].value == 1 ? "DOWN" : ev[j].value ? "REP" : "UP");
                    fflush(stdout);
                    if (sel && st) { printf("SELECT+START -> exit.\n"); goto out; }
                } else if (ev[j].type == EV_ABS && ev[j].code < 64) {
                    if (abs(ev[j].value - last_abs[ev[j].code]) >= 40) {
                        last_abs[ev[j].code] = ev[j].value;
                        const char *an = ev[j].code == 0 ? "LX" : ev[j].code == 1 ? "LY" :
                                         ev[j].code == 3 ? "RX" : ev[j].code == 4 ? "RY" : "ABS";
                        printf("  %-3s (%d) = %d\n", an, ev[j].code, ev[j].value);
                        fflush(stdout);
                    }
                }
            }
        }
    }
out:
    for (int i = 0; i < nfd; i++) { ioctl(p[i].fd, EVIOCGRAB, 0); close(p[i].fd); }
    return 0;
}

static int cmd_status(void)
{
    return system(
        "echo '=== DISPLAY / HDMI ==='; for f in /sys/class/graphics/fb*; do [ -e \"$f\" ] && echo \"$f: $(cat $f/name) $(cat $f/modes)\"; done;"
        "echo '=== AUDIO ==='; cat /proc/asound/cards;"
        "echo '=== INPUT ==='; grep -E '^[NH]:' /proc/bus/input/devices;"
        "echo '=== USB HOST ==='; ls /sys/bus/usb/devices;"
        "echo '=== PARTITIONS ==='; cat /proc/partitions");
}

int main(int argc, char **argv)
{
    const char *m = argc > 1 ? argv[1] : "status";
    if (!strcmp(m, "sound"))     return cmd_sound(argc > 2 ? atoi(argv[2]) : 0);
    if (!strcmp(m, "hdmi"))      return cmd_hdmi();
    if (!strcmp(m, "hdmi-set"))  return cmd_hdmi_set(argc > 2 ? atoi(argv[2]) : 1280, argc > 3 ? atoi(argv[3]) : 720, 1);
    if (!strcmp(m, "hdmi-kick")) return cmd_hdmi_set(0, 0, 0);
    if (!strcmp(m, "buttons"))   return cmd_buttons(argc > 2 ? atoi(argv[2]) : 20);
    return cmd_status();
}
