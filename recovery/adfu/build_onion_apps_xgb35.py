"""Build official Miyoo Mini Plus OnionUI/Onion C apps natively for XGB35 (glibc 2.25 + /dev/fb0 480x320 CW)."""
import os, pathlib, shutil, subprocess, sys

ROOT = pathlib.Path(r'd:\grav\asm_ex\build\firmware-inspection\recovery-research')
UP = ROOT / 'onion-upstream'
ONION = UP / 'Onion'
COMMON = ONION / 'src' / 'common'
LIBS_DIR = UP / 'xgb35-libs'
OUT_DIR = UP / 'xgb35-built'
OUT_DIR.mkdir(parents=True, exist_ok=True)

# Materialize .so symlinks in xgb35-libs for zig ld
SYMLINKS = {
    'libSDL.so': 'libSDL-1.2.so.0.11.5',
    'libSDL-1.2.so.0': 'libSDL-1.2.so.0.11.5',
    'libSDL_image.so': 'libSDL_image-1.2.so.0.8.4',
    'libSDL_ttf.so': 'libSDL_ttf-2.0.so.0.10.2',
    'libSDL_gfx.so': 'libSDL_gfx.so.16.9.1',
    'libSDL_mixer.so': 'libSDL_mixer-1.2.so.0.12.1',
    'libpng.so': 'libpng16.so.16.55.0',
    'libpng16.so': 'libpng16.so.16.55.0',
}
for dst, src in SYMLINKS.items():
    d = LIBS_DIR / dst
    if not d.exists() and (LIBS_DIR / src).exists():
        shutil.copy2(LIBS_DIR / src, d)
# Also copy libsqlite3.so and libSDL_rotozoom.so from Onion/lib
for extra in ('libsqlite3.so', 'libsqlite3.so.0', 'libSDL_rotozoom.so'):
    src = ONION / 'lib' / extra
    dst = LIBS_DIR / extra
    if src.exists() and not dst.exists():
        shutil.copy2(src, dst)

# Create clean XGB35 override include directory (searched BEFORE Onion/src/common!)
OV = UP / 'xgb35-override'
(OV / 'system').mkdir(parents=True, exist_ok=True)
(OV / 'utils').mkdir(parents=True, exist_ok=True)

# 1. keymap_hw.h: exact XGB35 evdev codes
(OV / 'system' / 'keymap_hw.h').write_text('''#ifndef KEYMAP_HW_H__
#define KEYMAP_HW_H__
#include <linux/input.h>
#define HW_BTN_UP 103
#define HW_BTN_DOWN 108
#define HW_BTN_LEFT 105
#define HW_BTN_RIGHT 106
#define HW_BTN_A 352
#define HW_BTN_B 158
#define HW_BTN_X 308
#define HW_BTN_Y 139
#define HW_BTN_L1 412
#define HW_BTN_R1 407
#define HW_BTN_L2 312
#define HW_BTN_R2 313
#define HW_BTN_SELECT 314
#define HW_BTN_START 315
#define HW_BTN_MENU 174
#define HW_BTN_POWER 116
#define HW_BTN_VOLUME_UP 115
#define HW_BTN_VOLUME_DOWN 114
#endif
''', encoding='utf-8')

# 2. display.h: patch a clean copy of Onion's display.h with XGB35 640x480 -> 480x320 RGB565 CW blitter + owl_backlight
disp_src = (COMMON / 'system' / 'display.h').read_text(encoding='utf-8').replace('\r\n', '\n')
disp_src = disp_src.replace('#include "system.h"', '#include "system/system.h"')
disp_src = disp_src.replace(
    '#ifdef PLATFORM_MIYOOMINI\n#define DEFAULT_WIDTH 640\n#define DEFAULT_HEIGHT 480',
    '#if defined(PLATFORM_MIYOOMINI) || defined(PLATFORM_XGB35)\n#define DEFAULT_WIDTH 640\n#define DEFAULT_HEIGHT 480'
)
disp_src = disp_src.replace(
    '    ioctl(fb_fd, FBIOPUT_VSCREENINFO, &g_display.vinfo);',
    '    g_display.vinfo.xoffset = 0; g_display.vinfo.yoffset = 0; ioctl(fb_fd, FBIOPAN_DISPLAY, &g_display.vinfo);'
)
disp_src = disp_src.replace(
    '''void display_getRenderResolution()
{
    if (fb_fd < 0)
        fb_fd = open("/dev/fb0", O_RDWR);
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &g_display.vinfo) == 0) {
        g_display.width = g_display.vinfo.xres;
        g_display.height = g_display.vinfo.yres;
    }
    printf_debug("Render resolution: %dx%d\\n", g_display.width, g_display.height);
}''',
    '''void display_getRenderResolution()
{
    g_display.width = 640;
    g_display.height = 480;
    DISPLAY_WIDTH = 640;
    DISPLAY_HEIGHT = 480;
}'''
)
disp_src = disp_src.replace(
    '''void display_readOrWriteBuffer(int index, display_t *display, uint32_t *pixels, rect_t rect, bool rotate, bool mask, bool write)
{
    int bufferPos = index * display->vinfo.yres;''',
    '''void display_readOrWriteBuffer(int index, display_t *display, uint32_t *pixels, rect_t rect, bool rotate, bool mask, bool write)
{
    (void)index; (void)rotate; (void)mask;
    if (!write || !display || !display->fb_addr || !pixels || rect.w <= 0 || rect.h <= 0)
        return;
    uint16_t *fb16 = (uint16_t *)display->fb_addr;
    int stride16 = (int)(display->finfo.line_length / 2);
    int xres = (int)display->vinfo.xres;
    if (stride16 <= 0) stride16 = 320;
    if (xres <= 0) xres = 320;
    for (int Y = 0; Y < 320; Y++) {
        int sy = (Y * rect.h) / 320;
        const uint32_t *src = pixels + sy * rect.w;
        int fb_x = xres - 1 - Y;
        for (int X = 0; X < 480; X++) {
            int sx = (X * rect.w) / 480;
            uint32_t p = src[sx];
            fb16[X * stride16 + fb_x] = (uint16_t)(((p >> 8) & 0xF800U) | ((p >> 5) & 0x07E0U) | ((p >> 3) & 0x001FU));
        }
    }
    return;
    int bufferPos = index * display->vinfo.yres;'''
)
disp_src = disp_src.replace(
    '    file_put_sync(fp, PWM_DIR "pwm0/duty_cycle", "%u", value);',
    '    file_put_sync(fp, "/sys/class/backlight/owl_backlight/brightness", "%u", value > 255 ? 255 : value);'
)
(OV / 'system' / 'display.h').write_text(disp_src, encoding='utf-8')

# 3. battery.h: real XGB35 /sys/class/power_supply/battery
(OV / 'system' / 'battery.h').write_text('''#ifndef BATTERY_H__
#define BATTERY_H__
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "utils/file.h"
static time_t battery_last_modified = 0;
static bool battery_is_charging = false;
static inline int battery_getPercentage(void) {
    int cap = 80;
    FILE *f = fopen("/sys/class/power_supply/battery/capacity", "r");
    if (f) { if (fscanf(f, "%d", &cap) != 1) cap = 80; fclose(f); }
    if (cap < 0) cap = 0;
    if (cap > 100) cap = 100;
    return cap;
}
static inline bool battery_isCharging(void) {
    char st[32] = "";
    FILE *f = fopen("/sys/class/power_supply/battery/status", "r");
    if (f) { (void)fgets(st, sizeof(st), f); fclose(f); }
    return strncmp(st, "Charging", 8) == 0;
}
static inline bool battery_hasChanged(int ticks, int *out_percentage) {
    (void)ticks;
    int p = battery_isCharging() ? 500 : battery_getPercentage();
    if (p != *out_percentage) { *out_percentage = p; return true; }
    return false;
}
#endif
''', encoding='utf-8')

# 4. volume.h: XGB35 tinymix 15 / 35
(OV / 'system' / 'volume.h').write_text('''#ifndef VOLUME_H__
#define VOLUME_H__
#include <stdio.h>
#include <stdlib.h>
#define MAX_VOLUME 20
static inline int setVolumeRaw(int value, int add) { (void)add; return value; }
static inline int setVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 20) volume = 20;
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "LD_LIBRARY_PATH=/lib /bin/tinymix 15 %d >/dev/null 2>&1; "
             "LD_LIBRARY_PATH=/lib /bin/tinymix 35 %d >/dev/null 2>&1",
             volume * 2, volume > 0 ? 1 : 0);
    int rc = system(cmd); (void)rc;
    return volume;
}
#endif
''', encoding='utf-8')

# 5. sdl_direct_fb.h: use /dev/input/event1, no EVIOCGRAB, single-buffer write
dfb_src = (COMMON / 'utils' / 'sdl_direct_fb.h').read_text(encoding='utf-8').replace('\r\n', '\n')
dfb_src = dfb_src.replace('#ifdef PLATFORM_MIYOOMINI', '#if defined(PLATFORM_MIYOOMINI) || defined(PLATFORM_XGB35)')
dfb_src = dfb_src.replace('"/dev/input/event0"', '"/dev/input/event1"')
dfb_src = dfb_src.replace('ioctl(_input_fd, EVIOCGRAB, 1)', '0')
dfb_src = dfb_src.replace('ioctl(_input_fd, EVIOCGRAB, 0)', '0')
dfb_src = dfb_src.replace(
    '''        int numBuffers = g_display.vinfo.yres_virtual / g_display.vinfo.yres;
        for (int b = 0; b < numBuffers; b++) {
            display_writeBuffer(b, &g_display, (uint32_t *)screen->pixels, (rect_t){0, 0, screen->w, screen->h}, true, false);
        }''',
    '        display_writeBuffer(0, &g_display, (uint32_t *)screen->pixels, (rect_t){0, 0, screen->w, screen->h}, false, false);'
)
(OV / 'utils' / 'sdl_direct_fb.h').write_text(dfb_src, encoding='utf-8')

# 6. sdl_init.h: software 640x480 surface + direct rotated /dev/fb0 flip!
(OV / 'utils' / 'sdl_init.h').write_text('''#ifndef UTILS_SDL_INIT_H__
#define UTILS_SDL_INIT_H__
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#ifdef HAS_AUDIO
#include <SDL/SDL_mixer.h>
#endif
#include "system/display.h"

static SDL_Surface *video;
static SDL_Surface *screen;

static inline int xgb_sdl_flip(SDL_Surface *surf)
{
    if (surf && surf->pixels) {
        if (!g_display.init_done) display_init(true);
        display_writeBuffer(0, &g_display, (uint32_t *)surf->pixels,
                            (rect_t){0, 0, surf->w, surf->h}, false, false);
    }
    return 0;
}
#define SDL_Flip(s) xgb_sdl_flip(s)

static inline bool SDL_InitDefault(void)
{
    display_init(true);
    display_getRenderResolution();
    IMG_Init(IMG_INIT_PNG);
    TTF_Init();
    video = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 480, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    screen = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 480, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    return video != NULL && screen != NULL;
}
#endif
''', encoding='utf-8')

# 7. keystate.h: read /dev/input/event1 directly with XGB35 evdev keycodes!
(OV / 'utils' / 'keystate.h').write_text('''#ifndef KEYSTATE_H__
#define KEYSTATE_H__
#include <SDL/SDL.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/poll.h>
#include <unistd.h>
#include "system/keymap_hw.h"
#include "system/keymap_sw.h"
#include "utils/msleep.h"

typedef enum { RELEASED, PRESSED, REPEATING } KeyState;
static SDL_Event keystate_event;
static int xgb_ev_fd = -1;

static inline SDLKey xgb_ev_to_sdl(int code) {
    switch (code) {
    case HW_BTN_UP: return SW_BTN_UP;
    case HW_BTN_DOWN: return SW_BTN_DOWN;
    case HW_BTN_LEFT: return SW_BTN_LEFT;
    case HW_BTN_RIGHT: return SW_BTN_RIGHT;
    case HW_BTN_A: return SW_BTN_A;
    case HW_BTN_B: return SW_BTN_B;
    case HW_BTN_X: return SW_BTN_X;
    case HW_BTN_Y: return SW_BTN_Y;
    case HW_BTN_L1: return SW_BTN_L1;
    case HW_BTN_R1: return SW_BTN_R1;
    case HW_BTN_L2: return SW_BTN_L2;
    case HW_BTN_R2: return SW_BTN_R2;
    case HW_BTN_SELECT: return SW_BTN_SELECT;
    case HW_BTN_START: return SW_BTN_START;
    case HW_BTN_MENU: return SW_BTN_MENU;
    case HW_BTN_POWER: return SW_BTN_POWER;
    default: return SDLK_UNKNOWN;
    }
}

static inline bool updateKeystate(KeyState keystate[320], bool *quit_flag, bool enabled, SDLKey *changed_key)
{
    (void)quit_flag;
    if (xgb_ev_fd < 0) xgb_ev_fd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    bool retval = false;
    if (xgb_ev_fd >= 0) {
        struct input_event ev[16];
        ssize_t n;
        while ((n = read(xgb_ev_fd, ev, sizeof(ev))) > 0) {
            for (int i = 0; i < (int)(n / (ssize_t)sizeof(ev[0])); i++) {
                if (!enabled || ev[i].type != EV_KEY) continue;
                SDLKey key = xgb_ev_to_sdl(ev[i].code);
                if (key == SDLK_UNKNOWN || (int)key >= 320) continue;
                if (ev[i].value == 1 || ev[i].value == 2) {
                    keystate[key] = (ev[i].value == 2 || keystate[key] != RELEASED) ? REPEATING : PRESSED;
                    if (changed_key) *changed_key = key;
                    retval = true;
                } else if (ev[i].value == 0) {
                    keystate[key] = RELEASED;
                    if (changed_key) *changed_key = key;
                    retval = true;
                }
            }
        }
    }
    msleep(8);
    return retval;
}
#endif
''', encoding='utf-8')

# Copy ONLY SDL-1.2 headers from onionos-bundle/sysroot/usr/include/SDL into OV/SDL so zig's glibc headers stay pure
SYS_SDL = ROOT / 'onionos-bundle' / 'sysroot' / 'usr' / 'include' / 'SDL'
if SYS_SDL.exists():
    (OV / 'SDL').mkdir(parents=True, exist_ok=True)
    for h in SYS_SDL.glob('*.h'):
        dst_h = OV / 'SDL' / h.name
        if not dst_h.exists():
            shutil.copy2(h, dst_h)
    for h in (ONION / 'include' / 'SDL').glob('*.h'):
        dst_h = OV / 'SDL' / h.name
        if not dst_h.exists():
            shutil.copy2(h, dst_h)
    ttf_h = ROOT.parents[1] / 'ports' / 'onion-mainui' / 'vendor' / 'sdl_ttf' / 'SDL_ttf.h'
    if ttf_h.exists():
        t_txt = ttf_h.read_text(encoding='utf-8', errors='ignore')
        if 'XGB35_TTF_WRAP' not in t_txt:
            t_txt += '''
#ifdef PLATFORM_XGB35
#ifndef XGB35_TTF_WRAP
#define XGB35_TTF_WRAP
static inline TTF_Font *xgb35_open_font(const char *file, int ptsize) {
    TTF_Font *f = TTF_OpenFont(file, ptsize);
    if (!f) f = TTF_OpenFont("/mnt/SDCARD/Themes/Analogue by Aemiii91/font/font.ttf", ptsize);
    if (!f) f = TTF_OpenFont("/mnt/SDCARD/Themes/CyberOnion by Aemiii91/font/font.ttf", ptsize);
    if (!f) f = TTF_OpenFont("/mnt/SDCARD/.tmp_update/res/Exo-2-Bold-Italic.ttf", ptsize);
    if (!f) f = TTF_OpenFont("/superx/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", ptsize);
    if (!f) f = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", ptsize);
    return f;
}
#define TTF_OpenFont xgb35_open_font
#endif
#endif
'''
        (OV / 'SDL' / 'SDL_ttf.h').write_text(t_txt, encoding='utf-8')
    # Patch OV/SDL/SDL.h so apps that call SDL_SetVideoMode/SDL_Flip directly (like themeSwitcher) also blit to rotated /dev/fb0!
    sdl_h_path = OV / 'SDL' / 'SDL.h'
    if sdl_h_path.exists():
        s_txt = sdl_h_path.read_text(encoding='utf-8', errors='ignore')
        if 'XGB35_SDL_WRAP' not in s_txt:
            s_txt += '''
#ifdef PLATFORM_XGB35
#ifndef XGB35_SDL_WRAP
#define XGB35_SDL_WRAP
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
static inline SDL_Surface *xgb35_set_video_mode(int w, int h, int bpp, Uint32 flags) {
    (void)flags;
    if (w <= 0) w = 640;
    if (h <= 0) h = 480;
    if (bpp != 16 && bpp != 32) bpp = 32;
    return SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
}
static inline int xgb35_flip_direct(SDL_Surface *surf) {
    static int s_fd = -1;
    static uint16_t *s_fb = NULL;
    static int s_stride16 = 320, s_xres = 320;
    if (!surf || !surf->pixels) return 0;
    if (s_fd < 0) {
        s_fd = open("/dev/fb0", O_RDWR);
        if (s_fd >= 0) {
            struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
            if (ioctl(s_fd, FBIOGET_VSCREENINFO, &v) == 0 && ioctl(s_fd, FBIOGET_FSCREENINFO, &f) == 0) {
                s_xres = (int)v.xres; s_stride16 = (int)(f.line_length / 2);
                void *m = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, 0);
                if (m != MAP_FAILED) s_fb = (uint16_t *)m;
                v.xoffset = 0; v.yoffset = 0; ioctl(s_fd, FBIOPAN_DISPLAY, &v);
            }
        }
    }
    if (!s_fb) return 0;
    uint32_t *px = (uint32_t *)surf->pixels;
    int sw = surf->w, sh = surf->h;
    for (int Y = 0; Y < 320; Y++) {
        int sy = (Y * sh) / 320;
        const uint32_t *src = px + sy * sw;
        int fb_x = s_xres - 1 - Y;
        for (int X = 0; X < 480; X++) {
            int sx = (X * sw) / 480;
            uint32_t p = src[sx];
            s_fb[X * s_stride16 + fb_x] = (uint16_t)(((p >> 8) & 0xF800U) | ((p >> 5) & 0x07E0U) | ((p >> 3) & 0x001FU));
        }
    }
    return 0;
}
#define SDL_SetVideoMode xgb35_set_video_mode
#ifndef SDL_Flip
#define SDL_Flip xgb35_flip_direct
#endif
#endif
#endif
'''
            sdl_h_path.write_text(s_txt, encoding='utf-8')
    for zh in ('zlib.h', 'zconf.h'):
        zp = ROOT / 'onionos-bundle' / 'sysroot' / 'usr' / 'include' / zh
        if zp.exists():
            shutil.copy2(zp, OV / zh)

# Tiny stub for launch_keyboard (used by tweaks) so we don't depend on Miyoo's libkbinput.so
KB_STUB = OV / 'kb_stub.c'
KB_STUB.write_text('#include <stdlib.h>\n#include <string.h>\nchar *launch_keyboard(const char *iv, const char *title) { (void)title; return iv ? strdup(iv) : NULL; }\n', encoding='utf-8')

# Common C sources used across Onion apps
BASE_SRCS = [
    str(COMMON / 'utils' / 'file.c'),
    str(COMMON / 'utils' / 'log.c'),
    str(COMMON / 'utils' / 'str.c'),
    str(COMMON / 'utils' / 'retroarch_cmd.c'),
    str(COMMON / 'utils' / 'udp.c'),
    str(KB_STUB),
]
IMG_CACHE_SRC = str(COMMON / 'utils' / 'imageCache.c')
CJSON_SRC = str(ONION / 'include' / 'cjson' / 'cJSON.c')

# List of OnionUI/Onion apps to compile: (name, dir_rel, extra_libs, use_sdl)
TARGETS = [
    ('themeSwitcher',    'src/themeSwitcher',    ['-lSDL', '-lSDL_image', '-lSDL_ttf', '-lSDL_gfx', '-lpng', '-lm', '-lpthread'], True),
    ('infoPanel',        'src/infoPanel',        ['-lSDL', '-lSDL_image', '-lSDL_ttf', '-lSDL_gfx', '-lpng', '-lm', '-lpthread'], True),
    ('batteryMonitorUI', 'src/batteryMonitorUI', ['-lSDL', '-lSDL_image', '-lSDL_ttf', '-lSDL_gfx', '-lpng', '-lsqlite3', '-lm', '-lpthread'], True),
    ('playActivityUI',   'src/playActivityUI',   ['-lSDL', '-lSDL_image', '-lSDL_ttf', '-lSDL_gfx', '-lpng', '-lsqlite3', '-lm', '-lpthread'], True),
    ('playActivity',     'src/playActivity',     ['-lsqlite3', '-lm', '-lpthread'], False),
    ('tweaks',           'src/tweaks',           ['-lSDL', '-lSDL_image', '-lSDL_ttf', '-lSDL_gfx', '-lSDL_mixer', '-lpng', '-lsqlite3', '-lm', '-lpthread'], True),
    ('prompt',           'src/prompt',           ['-include', 'signal.h', '-lSDL', '-lSDL_image', '-lSDL_ttf', '-lSDL_gfx', '-lpng', '-lm', '-lpthread'], True),
    ('bootScreen',       'src/bootScreen',       ['-lSDL', '-lSDL_image', '-lSDL_ttf', '-lSDL_gfx', '-lpng', '-lm', '-lpthread'], True),
    ('gameSwitcher',     'src/gameSwitcher',     ['-lSDL', '-lSDL_image', '-lSDL_ttf', '-lSDL_gfx', '-lpng', '-lsqlite3', '-lm', '-lpthread'], True),
    ('packageManager',   'src/packageManager',   ['-lSDL', '-lSDL_image', '-lSDL_ttf', '-lSDL_gfx', '-lpng', '-lm', '-lpthread'], True),
    ('randomGamePicker', 'src/randomGamePicker', ['-lsqlite3', '-lm', '-lpthread'], False),
    ('tree',             'src/tree',             [], False),
    ('renameRom',        'src/renameRom',        ['-lsqlite3', '-lm', '-lpthread'], False),
    ('detectKey',        'src/detectKey',        [], False),
]

ok_list, fail_list = [], []
for name, dir_rel, libs, use_sdl in TARGETS:
    src_paths = sorted(str(p) for p in (ONION / dir_rel).glob('*.c'))
    if not src_paths:
        print(f'[SKIP] {name:18} (no .c files)')
        continue
    need_cjson = (CJSON_SRC if pathlib.Path(CJSON_SRC).exists() else None)
    cmd = [
        sys.executable, '-m', 'ziglang', 'cc',
        '-target', 'arm-linux-gnueabihf.2.25', '-mcpu=cortex_a5', '-O2', '-s',
        '-DPLATFORM_XGB35', '-DPLATFORM_MIYOOMINI',
        '-Wno-everything',
        f'-I{OV}',
        f'-I{COMMON}',
        f'-I{ONION / "include"}',
        f'-I{OV / "SDL"}',
        f'-I{ONION / "include" / "cjson"}',
        f'-I{ONION / "include" / "sqlite3"}',
        f'-I{ONION / "include" / "png"}',
        f'-L{LIBS_DIR}',
        '-Wl,--allow-shlib-undefined',
        *src_paths,
        *BASE_SRCS,
        *([IMG_CACHE_SRC] if use_sdl else []),
        *([need_cjson] if need_cjson else []),
        *libs,
        '-o', str(OUT_DIR / name),
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode == 0:
        sz = (OUT_DIR / name).stat().st_size
        print(f'[OK]   {name:18} ({sz:,} bytes)')
        ok_list.append(name)
    else:
        err_lines = [l for l in r.stderr.splitlines() if 'error:' in l][:3]
        print(f'[FAIL] {name:18} -> {" | ".join(err_lines) or r.stderr[:200]}')
        fail_list.append(name)

print(f'\nSummary: {len(ok_list)}/{len(TARGETS)} compiled cleanly: {ok_list}')
