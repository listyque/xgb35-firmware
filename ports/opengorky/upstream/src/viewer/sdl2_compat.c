#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Matching struct layouts from sdl2_compat.h (without macro collisions) */
typedef struct OG_Rect   { int x, y, w, h; } OG_Rect;
typedef struct OG_FRect  { float x, y, w, h; } OG_FRect;
typedef struct OG_FPoint { float x, y; } OG_FPoint;
typedef struct OG_FColor { float r, g, b, a; } OG_FColor;
typedef struct OG_Vertex {
    OG_FPoint position;
    OG_FColor color;
    OG_FPoint tex_coord;
} OG_Vertex;

typedef struct OG_Surface {
    uint32_t flags;
    uint32_t format;
    int w;
    int h;
    int pitch;
    void *pixels;
} OG_Surface;

typedef struct OG_Locale {
    const char *language;
    const char *country;
} OG_Locale;

typedef struct OG_AudioSpec {
    uint32_t format;
    int channels;
    int freq;
} OG_AudioSpec;

typedef struct OG_AudioStream {
    SDL_AudioDeviceID dev;
} OG_AudioStream;

/* SGX540-safe Texture wrapper: static textures > 512px are tiled into 512x512 RGB565 chunks */
#define OG_TILE_SZ 512
typedef struct OG_Tex {
    SDL_Renderer *ren;
    int w, h;
    int access;
    bool tiled;
    bool half_res;
    uint32_t fmt;
    int cols, rows;
    SDL_Texture *single;
    SDL_Texture **tiles;
    SDL_BlendMode blend;
    SDL_ScaleMode scale_mode;
    uint8_t cr, cg, cb, ca;
} OG_Tex;

static OG_Tex *g_cur_target = NULL;

typedef struct OG_KeyboardEvent {
    uint32_t type;
    uint32_t windowID;
    int scancode;
    uint32_t key;
    uint16_t mod;
    bool down;
    bool repeat;
} OG_KeyboardEvent;

typedef struct OG_TextInputEvent {
    uint32_t type;
    uint32_t windowID;
    const char *text;
    char buf[64];
} OG_TextInputEvent;

typedef struct OG_MouseMotionEvent {
    uint32_t type;
    uint32_t windowID;
    float x, y;
    float xrel, yrel;
} OG_MouseMotionEvent;

typedef struct OG_MouseButtonEvent {
    uint32_t type;
    uint32_t windowID;
    uint8_t button;
    bool down;
    uint8_t clicks;
    float x, y;
} OG_MouseButtonEvent;

typedef struct OG_MouseWheelEvent {
    uint32_t type;
    uint32_t windowID;
    float x, y;
    float mouse_x, mouse_y;
} OG_MouseWheelEvent;

typedef struct OG_WindowEvent {
    uint32_t type;
    uint32_t windowID;
    int32_t data1;
    int32_t data2;
} OG_WindowEvent;

typedef union OG_Event {
    uint32_t type;
    OG_WindowEvent window;
    OG_KeyboardEvent key;
    OG_TextInputEvent text;
    OG_MouseMotionEvent motion;
    OG_MouseButtonEvent button;
    OG_MouseWheelEvent wheel;
    uint8_t padding[128];
} OG_Event;

typedef void (*OG_DialogFileCallback)(void *userdata, const char *const *filelist, int filter);

#define OG_EVENT_QUIT                   0x100u
#define OG_EVENT_WINDOW_RESIZED         0x206u
#define OG_EVENT_WINDOW_MOUSE_LEAVE     0x20Du
#define OG_EVENT_WINDOW_CLOSE_REQUESTED 0x210u
#define OG_EVENT_KEY_DOWN               0x300u
#define OG_EVENT_KEY_UP                 0x301u
#define OG_EVENT_TEXT_INPUT             0x303u
#define OG_EVENT_MOUSE_MOTION           0x400u
#define OG_EVENT_MOUSE_BUTTON_DOWN      0x401u
#define OG_EVENT_MOUSE_BUTTON_UP        0x402u
#define OG_EVENT_MOUSE_WHEEL            0x403u

static uint8_t clamp_u8(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

/* Synthetic event queue for SDL_PushEvent (used by input_script.zig) */
#define SYNTH_Q_CAP 256
static OG_Event g_synth_q[SYNTH_Q_CAP];
static int g_synth_head = 0, g_synth_tail = 0;
static float g_last_mx = 0.0f, g_last_my = 0.0f;
static bool g_key_bools[SDL_NUM_SCANCODES];

bool og_sdl_Init(uint32_t flags) {
    uint32_t f2 = 0;
    if (flags & 0x10u) f2 |= SDL_INIT_AUDIO;
    if (flags & 0x20u) f2 |= SDL_INIT_VIDEO;
    return SDL_Init(f2) == 0;
}
bool og_sdl_InitSubSystem(uint32_t flags) {
    uint32_t f2 = 0;
    if (flags & 0x10u) f2 |= SDL_INIT_AUDIO;
    if (flags & 0x20u) f2 |= SDL_INIT_VIDEO;
    return SDL_InitSubSystem(f2) == 0;
}
void og_sdl_QuitSubSystem(uint32_t flags) {
    uint32_t f2 = 0;
    if (flags & 0x10u) f2 |= SDL_INIT_AUDIO;
    if (flags & 0x20u) f2 |= SDL_INIT_VIDEO;
    SDL_QuitSubSystem(f2);
}
void og_sdl_Quit(void) { SDL_Quit(); }
const char *og_sdl_GetError(void) { return SDL_GetError(); }
void og_sdl_free(void *mem) { free(mem); }

bool og_sdl_CreateWindowAndRenderer(const char *title, int w, int h, uint32_t flags, SDL_Window **window, SDL_Renderer **renderer) {
    uint32_t wf = 0;
    if (flags & 0x01u) wf |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (flags & 0x08u) wf |= SDL_WINDOW_HIDDEN;
    if (flags & 0x20u) wf |= SDL_WINDOW_RESIZABLE;
    if (flags & 0x2000u) wf |= SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window *win = SDL_CreateWindow(title ? title : "OpenGorky", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, wf);
    if (!win) return false;
    uint32_t rf = SDL_RENDERER_TARGETTEXTURE;
    const char *sw_env = getenv("OG_SW_RENDER");
    if (sw_env && sw_env[0] == '1') {
        rf |= SDL_RENDERER_SOFTWARE;
    } else {
        rf |= SDL_RENDERER_ACCELERATED;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, rf);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!ren) { SDL_DestroyWindow(win); return false; }
    *window = win;
    *renderer = ren;
    return true;
}

void og_sdl_DestroyWindow(SDL_Window *w) { if (w) SDL_DestroyWindow(w); }
void og_sdl_DestroyRenderer(SDL_Renderer *r) { if (r) SDL_DestroyRenderer(r); }
uint32_t og_sdl_GetWindowID(SDL_Window *w) { return w ? SDL_GetWindowID(w) : 0; }
uint32_t og_sdl_GetWindowFlags(SDL_Window *w) {
    if (!w) return 0;
    uint32_t f2 = SDL_GetWindowFlags(w), out = 0;
    if (f2 & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) out |= 0x01u;
    if (f2 & SDL_WINDOW_HIDDEN)      out |= 0x08u;
    if (f2 & SDL_WINDOW_RESIZABLE)   out |= 0x20u;
    if (f2 & SDL_WINDOW_MINIMIZED)   out |= 0x40u;
    if (f2 & SDL_WINDOW_INPUT_FOCUS) out |= 0x200u;
    return out;
}
bool og_sdl_GetWindowSize(SDL_Window *w, int *out_w, int *out_h) {
    if (!w) return false;
    SDL_GetWindowSize(w, out_w, out_h);
    return true;
}
float og_sdl_GetWindowPixelDensity(SDL_Window *w) {
    if (!w) return 1.0f;
    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(w, &ww, &wh);
    SDL_Renderer *r = SDL_GetRenderer(w);
    if (r && SDL_GetRendererOutputSize(r, &dw, &dh) == 0 && ww > 0) return (float)dw / (float)ww;
    return 1.0f;
}
bool og_sdl_SetWindowMinimumSize(SDL_Window *w, int min_w, int min_h) {
    if (!w) return false;
    SDL_SetWindowMinimumSize(w, min_w, min_h);
    return true;
}
bool og_sdl_SetWindowFullscreen(SDL_Window *w, bool fullscreen) {
    if (!w) return false;
    return SDL_SetWindowFullscreen(w, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0;
}
bool og_sdl_SetWindowIcon(SDL_Window *w, OG_Surface *icon) {
    if (!w || !icon || !icon->pixels) return false;
    SDL_Surface *s2 = SDL_CreateRGBSurfaceWithFormatFrom(icon->pixels, icon->w, icon->h, 32, icon->pitch, SDL_PIXELFORMAT_ABGR8888);
    if (!s2) return false;
    SDL_SetWindowIcon(w, s2);
    SDL_FreeSurface(s2);
    return true;
}
bool og_sdl_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *msg, SDL_Window *w) {
    (void)flags;
    return SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title ? title : "Error", msg ? msg : "", w) == 0;
}
void og_sdl_ShowOpenFolderDialog(OG_DialogFileCallback cb, void *userdata, SDL_Window *w, const char *default_loc, bool allow_many) {
    (void)w; (void)default_loc; (void)allow_many;
    if (cb) cb(userdata, NULL, 0);
}
OG_Locale **og_sdl_GetPreferredLocales(int *count) {
    if (count) *count = 0;
    return NULL;
}

bool og_sdl_SetRenderVSync(SDL_Renderer *r, int vsync) {
    return SDL_RenderSetVSync(r, vsync) == 0;
}
bool og_sdl_GetRenderOutputSize(SDL_Renderer *r, int *w, int *h) {
    return SDL_GetRendererOutputSize(r, w, h) == 0;
}
bool og_sdl_SetRenderLogicalPresentation(SDL_Renderer *r, int w, int h, int mode) {
    if (mode == 0) { /* DISABLED */
        SDL_RenderSetIntegerScale(r, SDL_FALSE);
        return SDL_RenderSetLogicalSize(r, 0, 0) == 0;
    }
    SDL_RenderSetIntegerScale(r, (mode == 4) ? SDL_TRUE : SDL_FALSE);
    return SDL_RenderSetLogicalSize(r, w, h) == 0;
}
bool og_sdl_SetRenderScale(SDL_Renderer *r, float sx, float sy) {
    return SDL_RenderSetScale(r, sx, sy) == 0;
}
bool og_sdl_SetRenderViewport(SDL_Renderer *r, const OG_Rect *rect) {
    if (!rect) return SDL_RenderSetViewport(r, NULL) == 0;
    SDL_Rect rc = { rect->x, rect->y, rect->w, rect->h };
    return SDL_RenderSetViewport(r, &rc) == 0;
}
bool og_sdl_SetRenderTarget(SDL_Renderer *r, OG_Tex *t) {
    g_cur_target = t;
    return SDL_SetRenderTarget(r, t ? t->single : NULL) == 0;
}
OG_Tex *og_sdl_GetRenderTarget(SDL_Renderer *r) {
    (void)r;
    return g_cur_target;
}
bool og_sdl_SetRenderDrawBlendMode(SDL_Renderer *r, uint32_t mode) {
    return SDL_SetRenderDrawBlendMode(r, (SDL_BlendMode)mode) == 0;
}
bool og_sdl_SetRenderDrawColor(SDL_Renderer *r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    return SDL_SetRenderDrawColor(r, cr, cg, cb, ca) == 0;
}
bool og_sdl_SetRenderDrawColorFloat(SDL_Renderer *r, float cr, float cg, float cb, float ca) {
    return SDL_SetRenderDrawColor(r, clamp_u8(cr), clamp_u8(cg), clamp_u8(cb), clamp_u8(ca)) == 0;
}
bool og_sdl_RenderClear(SDL_Renderer *r) { return SDL_RenderClear(r) == 0; }
bool og_sdl_RenderFillRect(SDL_Renderer *r, const OG_FRect *rect) {
    if (!rect) return SDL_RenderFillRectF(r, NULL) == 0;
    SDL_FRect rc = { rect->x, rect->y, rect->w, rect->h };
    return SDL_RenderFillRectF(r, &rc) == 0;
}
bool og_sdl_RenderFillRects(SDL_Renderer *r, const OG_FRect *rects, int count) {
    if (!rects || count <= 0) return true;
    return SDL_RenderFillRectsF(r, (const SDL_FRect *)rects, count) == 0;
}
bool og_sdl_SetRenderClipRect(SDL_Renderer *r, const OG_Rect *rect) {
    if (!rect) return SDL_RenderSetClipRect(r, NULL) == 0;
    SDL_Rect rc = { rect->x, rect->y, rect->w, rect->h };
    return SDL_RenderSetClipRect(r, &rc) == 0;
}
bool og_sdl_GetRectIntersection(const OG_Rect *a, const OG_Rect *b, OG_Rect *result) {
    if (!a || !b || !result) return false;
    SDL_Rect ra = { a->x, a->y, a->w, a->h }, rb = { b->x, b->y, b->w, b->h }, rr = {0};
    SDL_bool ok = SDL_IntersectRect(&ra, &rb, &rr);
    result->x = rr.x; result->y = rr.y; result->w = rr.w; result->h = rr.h;
    return ok == SDL_TRUE;
}
bool og_sdl_RenderLines(SDL_Renderer *r, const OG_FPoint *pts, int count) {
    if (!pts || count <= 0) return true;
    return SDL_RenderDrawLinesF(r, (const SDL_FPoint *)pts, count) == 0;
}
bool og_sdl_RenderTexture(SDL_Renderer *r, OG_Tex *t, const OG_FRect *srcrect, const OG_FRect *dstrect) {
    if (!t) return false;
    if (t->tiled) {
        float sx0 = srcrect ? srcrect->x : 0.0f;
        float sy0 = srcrect ? srcrect->y : 0.0f;
        float sw  = srcrect ? srcrect->w : (float)t->w;
        float sh  = srcrect ? srcrect->h : (float)t->h;
        float dx0 = dstrect ? dstrect->x : 0.0f;
        float dy0 = dstrect ? dstrect->y : 0.0f;
        float dw  = dstrect ? dstrect->w : sw;
        float dh  = dstrect ? dstrect->h : sh;
        if (sw <= 0.0f || sh <= 0.0f) return true;
        float kx = dw / sw, ky = dh / sh;
        int tile_log = t->half_res ? (OG_TILE_SZ * 2) : OG_TILE_SZ;
        for (int ty = 0; ty < t->rows; ty++) {
            float ty0 = (float)(ty * tile_log);
            float ty1 = ty0 + (float)tile_log;
            if (ty1 > (float)t->h) ty1 = (float)t->h;
            float iy0 = ty0 > sy0 ? ty0 : sy0;
            float iy1 = ty1 < (sy0 + sh) ? ty1 : (sy0 + sh);
            if (iy1 <= iy0) continue;
            for (int tx = 0; tx < t->cols; tx++) {
                float tx0 = (float)(tx * tile_log);
                float tx1 = tx0 + (float)tile_log;
                if (tx1 > (float)t->w) tx1 = (float)t->w;
                float ix0 = tx0 > sx0 ? tx0 : sx0;
                float ix1 = tx1 < (sx0 + sw) ? tx1 : (sx0 + sw);
                if (ix1 <= ix0) continue;
                SDL_Texture *tt = t->tiles[ty * t->cols + tx];
                if (!tt) continue;
                float div = t->half_res ? 0.5f : 1.0f;
                SDL_Rect s = {
                    (int)floorf((ix0 - tx0) * div),
                    (int)floorf((iy0 - ty0) * div),
                    (int)ceilf((ix1 - ix0) * div),
                    (int)ceilf((iy1 - iy0) * div)
                };
                if (s.w < 1) s.w = 1;
                if (s.h < 1) s.h = 1;
                SDL_FRect d = {
                    dx0 + (ix0 - sx0) * kx,
                    dy0 + (iy0 - sy0) * ky,
                    (ix1 - ix0) * kx,
                    (iy1 - iy0) * ky
                };
                SDL_RenderCopyF(r, tt, &s, &d);
            }
        }
        return true;
    }
    SDL_Rect src, *psrc = NULL;
    SDL_FRect dst, *pdst = NULL;
    if (srcrect) {
        src.x = (int)floorf(srcrect->x); src.y = (int)floorf(srcrect->y);
        src.w = (int)ceilf(srcrect->w);  src.h = (int)ceilf(srcrect->h);
        psrc = &src;
    }
    if (dstrect) {
        dst.x = dstrect->x; dst.y = dstrect->y; dst.w = dstrect->w; dst.h = dstrect->h;
        pdst = &dst;
    }
    return SDL_RenderCopyF(r, t->single, psrc, pdst) == 0;
}
bool og_sdl_RenderTextureRotated(SDL_Renderer *r, OG_Tex *t, const OG_FRect *srcrect, const OG_FRect *dstrect,
                                 double angle, const OG_FPoint *center, int flip) {
    if (!t || !t->single) return false;
    SDL_Rect src, *psrc = NULL;
    SDL_FRect dst, *pdst = NULL;
    SDL_FPoint ctr, *pctr = NULL;
    if (srcrect) {
        src.x = (int)floorf(srcrect->x); src.y = (int)floorf(srcrect->y);
        src.w = (int)ceilf(srcrect->w);  src.h = (int)ceilf(srcrect->h);
        psrc = &src;
    }
    if (dstrect) {
        dst.x = dstrect->x; dst.y = dstrect->y; dst.w = dstrect->w; dst.h = dstrect->h;
        pdst = &dst;
    }
    if (center) { ctr.x = center->x; ctr.y = center->y; pctr = &ctr; }
    SDL_RendererFlip f2 = (flip & 1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
    return SDL_RenderCopyExF(r, t->single, psrc, pdst, angle, pctr, f2) == 0;
}
bool og_sdl_RenderTexture9Grid(SDL_Renderer *r, OG_Tex *t, const OG_FRect *srcrect,
                               float lw, float rw, float th, float bh, float scale, const OG_FRect *dstrect) {
    (void)lw; (void)rw; (void)th; (void)bh; (void)scale;
    return og_sdl_RenderTexture(r, t, srcrect, dstrect);
}
bool og_sdl_RenderGeometry(SDL_Renderer *r, OG_Tex *t, const OG_Vertex *verts, int num_verts,
                           const int *indices, int num_indices) {
    if (!verts || num_verts <= 0) return true;
    SDL_Vertex stack_v[256];
    SDL_Vertex *v2 = (num_verts <= 256) ? stack_v : (SDL_Vertex *)malloc((size_t)num_verts * sizeof(SDL_Vertex));
    if (!v2) return false;
    for (int i = 0; i < num_verts; i++) {
        v2[i].position.x = verts[i].position.x;
        v2[i].position.y = verts[i].position.y;
        v2[i].color.r    = clamp_u8(verts[i].color.r);
        v2[i].color.g    = clamp_u8(verts[i].color.g);
        v2[i].color.b    = clamp_u8(verts[i].color.b);
        v2[i].color.a    = clamp_u8(verts[i].color.a);
        v2[i].tex_coord.x = verts[i].tex_coord.x;
        v2[i].tex_coord.y = verts[i].tex_coord.y;
    }
    int rc = SDL_RenderGeometry(r, t ? t->single : NULL, v2, num_verts, indices, num_indices);
    if (v2 != stack_v) free(v2);
    return rc == 0;
}
bool og_sdl_RenderDebugText(SDL_Renderer *r, float x, float y, const char *str) {
    (void)r; (void)x; (void)y; (void)str;
    return true;
}
bool og_sdl_RenderPresent(SDL_Renderer *r) { SDL_RenderPresent(r); return true; }

OG_Surface *og_sdl_RenderReadPixels(SDL_Renderer *r, const OG_Rect *rect) {
    int w = 0, h = 0;
    SDL_Rect rc;
    if (rect) {
        rc.x = rect->x; rc.y = rect->y; rc.w = rect->w; rc.h = rect->h;
        w = rc.w; h = rc.h;
    } else {
        SDL_Texture *tgt = SDL_GetRenderTarget(r);
        if (tgt) SDL_QueryTexture(tgt, NULL, NULL, &w, &h);
        else     SDL_GetRendererOutputSize(r, &w, &h);
        rc.x = 0; rc.y = 0; rc.w = w; rc.h = h;
    }
    if (w <= 0 || h <= 0) return NULL;
    OG_Surface *s = (OG_Surface *)calloc(1, sizeof(OG_Surface));
    if (!s) return NULL;
    s->w = w; s->h = h; s->pitch = w * 4; s->format = SDL_PIXELFORMAT_ABGR8888;
    s->pixels = malloc((size_t)s->pitch * (size_t)h);
    if (!s->pixels) { free(s); return NULL; }
    if (SDL_RenderReadPixels(r, &rc, SDL_PIXELFORMAT_ABGR8888, s->pixels, s->pitch) != 0) {
        free(s->pixels); free(s); return NULL;
    }
    return s;
}

bool og_sdl_RenderCoordinatesToWindow(SDL_Renderer *r, float x, float y, float *wx, float *wy) {
    int ix = 0, iy = 0;
    SDL_RenderLogicalToWindow(r, x, y, &ix, &iy);
    if (wx) *wx = (float)ix;
    if (wy) *wy = (float)iy;
    return true;
}
bool og_sdl_ConvertEventToRenderCoordinates(SDL_Renderer *r, OG_Event *ev) {
    if (!r || !ev) return false;
    float lx = 0.0f, ly = 0.0f;
    if (ev->type == OG_EVENT_MOUSE_MOTION) {
        SDL_RenderWindowToLogical(r, (int)ev->motion.x, (int)ev->motion.y, &lx, &ly);
        ev->motion.x = lx; ev->motion.y = ly;
    } else if (ev->type == OG_EVENT_MOUSE_BUTTON_DOWN || ev->type == OG_EVENT_MOUSE_BUTTON_UP) {
        SDL_RenderWindowToLogical(r, (int)ev->button.x, (int)ev->button.y, &lx, &ly);
        ev->button.x = lx; ev->button.y = ly;
    } else if (ev->type == OG_EVENT_MOUSE_WHEEL) {
        SDL_RenderWindowToLogical(r, (int)ev->wheel.mouse_x, (int)ev->wheel.mouse_y, &lx, &ly);
        ev->wheel.mouse_x = lx; ev->wheel.mouse_y = ly;
    }
    return true;
}

static uint16_t rgba_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static uint16_t rgba_to_argb1555(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint16_t)((a >= 128 ? 0x8000u : 0u) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

OG_Tex *og_sdl_CreateTexture(SDL_Renderer *r, uint32_t fmt, int access, int w, int h) {
    (void)fmt;
    OG_Tex *t = (OG_Tex *)calloc(1, sizeof(OG_Tex));
    if (!t) return NULL;
    t->ren = r; t->w = w; t->h = h; t->access = access;
    t->blend = SDL_BLENDMODE_NONE;
    t->scale_mode = SDL_ScaleModeNearest;
    t->cr = t->cg = t->cb = t->ca = 255;
    /* Only tile large opaque level backgrounds (> 640x480); 640x480 UI overlays need alpha */
    if (access == SDL_TEXTUREACCESS_STATIC && (w > 640 || h > 480)) {
        const char *hb = getenv("OG_HALF_BG");
        t->tiled = true;
        t->half_res = (hb && hb[0] == '1');
        int tile_log = t->half_res ? (OG_TILE_SZ * 2) : OG_TILE_SZ;
        t->cols = (w + tile_log - 1) / tile_log;
        t->rows = (h + tile_log - 1) / tile_log;
        t->tiles = (SDL_Texture **)calloc((size_t)(t->cols * t->rows), sizeof(SDL_Texture *));
        if (!t->tiles) { free(t); return NULL; }
        for (int ty = 0; ty < t->rows; ty++) {
            int rem_h = h - ty * tile_log;
            int th = rem_h < tile_log ? rem_h : tile_log;
            int pth = t->half_res ? ((th + 1) / 2) : th;
            if (pth < 1) pth = 1;
            for (int tx = 0; tx < t->cols; tx++) {
                int rem_w = w - tx * tile_log;
                int tw = rem_w < tile_log ? rem_w : tile_log;
                int ptw = t->half_res ? ((tw + 1) / 2) : tw;
                if (ptw < 1) ptw = 1;
                t->tiles[ty * t->cols + tx] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC, ptw, pth);
            }
        }
        return t;
    }
    /* PLATFORM: defer STATIC texture creation to UpdateTexture so opaque/1-bit-alpha sprites use 16-bit RGB565/ARGB1555 (50% RAM/VRAM) */
    if (access == SDL_TEXTUREACCESS_STATIC) {
        return t;
    }
    t->fmt = SDL_PIXELFORMAT_ABGR8888;
    t->single = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, access, w, h);
    if (!t->single) { free(t); return NULL; }
    return t;
}
bool og_sdl_UpdateTexture(OG_Tex *t, const OG_Rect *rect, const void *pixels, int pitch);

OG_Tex *og_sdl_CreateTextureFromSurface(SDL_Renderer *r, OG_Surface *s) {
    if (!s || !s->pixels) return NULL;
    OG_Tex *t = og_sdl_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, s->w, s->h);
    if (t) og_sdl_UpdateTexture(t, NULL, s->pixels, s->pitch);
    return t;
}
void og_sdl_DestroyTexture(OG_Tex *t) {
    if (!t) return;
    if (g_cur_target == t) g_cur_target = NULL;
    if (t->tiled && t->tiles) {
        for (int i = 0; i < t->cols * t->rows; i++) {
            if (t->tiles[i]) SDL_DestroyTexture(t->tiles[i]);
        }
        free(t->tiles);
    }
    if (t->single) SDL_DestroyTexture(t->single);
    free(t);
}
bool og_sdl_GetTextureSize(OG_Tex *t, float *w, float *h) {
    if (!t) return false;
    if (w) *w = (float)t->w;
    if (h) *h = (float)t->h;
    return true;
}
bool og_sdl_UpdateTexture(OG_Tex *t, const OG_Rect *rect, const void *pixels, int pitch) {
    if (!t || !pixels) return false;
    if (t->tiled) {
        const uint8_t *src = (const uint8_t *)pixels;
        int tile_log = t->half_res ? (OG_TILE_SZ * 2) : OG_TILE_SZ;
        uint16_t *tmp = (uint16_t *)malloc((size_t)OG_TILE_SZ * (size_t)OG_TILE_SZ * sizeof(uint16_t));
        if (!tmp) return false;
        for (int ty = 0; ty < t->rows; ty++) {
            int y0 = ty * tile_log;
            int th = (t->h - y0) < tile_log ? (t->h - y0) : tile_log;
            int pth = t->half_res ? ((th + 1) / 2) : th;
            for (int tx = 0; tx < t->cols; tx++) {
                int x0 = tx * tile_log;
                int tw = (t->w - x0) < tile_log ? (t->w - x0) : tile_log;
                int ptw = t->half_res ? ((tw + 1) / 2) : tw;
                SDL_Texture *tt = t->tiles[ty * t->cols + tx];
                if (!tt) continue;
                int step = t->half_res ? 2 : 1;
                for (int py = 0; py < pth; py++) {
                    int sy = y0 + py * step;
                    if (sy >= t->h) sy = t->h - 1;
                    const uint8_t *row = src + (size_t)sy * (size_t)pitch;
                    uint16_t *dst = tmp + (size_t)py * (size_t)ptw;
                    for (int px = 0; px < ptw; px++) {
                        int sx = x0 + px * step;
                        if (sx >= t->w) sx = t->w - 1;
                        const uint8_t *p = row + sx * 4;
                        dst[px] = rgba_to_rgb565(p[0], p[1], p[2]);
                    }
                }
                SDL_UpdateTexture(tt, NULL, tmp, ptw * 2);
            }
        }
        free(tmp);
        return true;
    }
    /* Deferred 16-bit/32-bit selection for STATIC textures */
    if (!t->single) {
        const uint8_t *src = (const uint8_t *)pixels;
        bool has_zero_a = false, has_mid_a = false;
        if (!rect) {
            for (int y = 0; y < t->h && !has_mid_a; y++) {
                const uint8_t *row = src + (size_t)y * (size_t)pitch;
                for (int x = 0; x < t->w; x++) {
                    uint8_t a = row[x * 4 + 3];
                    if (a == 0) has_zero_a = true;
                    else if (a != 255) { has_mid_a = true; break; }
                }
            }
        } else {
            has_mid_a = true;
        }
        t->fmt = has_mid_a ? SDL_PIXELFORMAT_ABGR8888 : (has_zero_a ? SDL_PIXELFORMAT_ARGB1555 : SDL_PIXELFORMAT_RGB565);
        t->single = SDL_CreateTexture(t->ren, t->fmt, t->access, t->w, t->h);
        if (!t->single) return false;
        SDL_SetTextureBlendMode(t->single, t->blend);
        SDL_SetTextureScaleMode(t->single, t->scale_mode);
        SDL_SetTextureColorMod(t->single, t->cr, t->cg, t->cb);
        SDL_SetTextureAlphaMod(t->single, t->ca);
    }
    if (!rect && (t->fmt == SDL_PIXELFORMAT_RGB565 || t->fmt == SDL_PIXELFORMAT_ARGB1555)) {
        const uint8_t *src = (const uint8_t *)pixels;
        uint16_t *tmp = (uint16_t *)malloc((size_t)t->w * (size_t)t->h * sizeof(uint16_t));
        if (!tmp) return false;
        if (t->fmt == SDL_PIXELFORMAT_RGB565) {
            for (int y = 0; y < t->h; y++) {
                const uint8_t *row = src + (size_t)y * (size_t)pitch;
                uint16_t *dst = tmp + (size_t)y * (size_t)t->w;
                for (int x = 0; x < t->w; x++) {
                    const uint8_t *p = row + x * 4;
                    dst[x] = rgba_to_rgb565(p[0], p[1], p[2]);
                }
            }
        } else {
            for (int y = 0; y < t->h; y++) {
                const uint8_t *row = src + (size_t)y * (size_t)pitch;
                uint16_t *dst = tmp + (size_t)y * (size_t)t->w;
                for (int x = 0; x < t->w; x++) {
                    const uint8_t *p = row + x * 4;
                    dst[x] = rgba_to_argb1555(p[0], p[1], p[2], p[3]);
                }
            }
        }
        int rc = SDL_UpdateTexture(t->single, NULL, tmp, t->w * 2);
        free(tmp);
        return rc == 0;
    }
    if (!rect) return SDL_UpdateTexture(t->single, NULL, pixels, pitch) == 0;
    SDL_Rect rc = { rect->x, rect->y, rect->w, rect->h };
    return SDL_UpdateTexture(t->single, &rc, pixels, pitch) == 0;
}
bool og_sdl_LockTexture(OG_Tex *t, const OG_Rect *rect, void **pixels, int *pitch) {
    if (!t || !t->single) return false;
    if (!rect) return SDL_LockTexture(t->single, NULL, pixels, pitch) == 0;
    SDL_Rect rc = { rect->x, rect->y, rect->w, rect->h };
    return SDL_LockTexture(t->single, &rc, pixels, pitch) == 0;
}
void og_sdl_UnlockTexture(OG_Tex *t) { if (t && t->single) SDL_UnlockTexture(t->single); }
bool og_sdl_SetTextureBlendMode(OG_Tex *t, uint32_t mode) {
    if (!t) return false;
    t->blend = (SDL_BlendMode)mode;
    if (t->tiled) {
        for (int i = 0; i < t->cols * t->rows; i++)
            if (t->tiles[i]) SDL_SetTextureBlendMode(t->tiles[i], t->blend);
        return true;
    }
    if (!t->single) return true;
    return SDL_SetTextureBlendMode(t->single, t->blend) == 0;
}
bool og_sdl_GetTextureBlendMode(OG_Tex *t, uint32_t *mode) {
    if (!t) return false;
    if (mode) *mode = (uint32_t)t->blend;
    return true;
}
bool og_sdl_SetTextureScaleMode(OG_Tex *t, int mode) {
    if (!t) return false;
    SDL_ScaleMode sm = (mode == 0) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
    t->scale_mode = sm;
    if (t->tiled) {
        for (int i = 0; i < t->cols * t->rows; i++)
            if (t->tiles[i]) SDL_SetTextureScaleMode(t->tiles[i], sm);
        return true;
    }
    if (!t->single) return true;
    return SDL_SetTextureScaleMode(t->single, sm) == 0;
}
bool og_sdl_SetTextureColorMod(OG_Tex *t, uint8_t cr, uint8_t cg, uint8_t cb) {
    if (!t) return false;
    t->cr = cr; t->cg = cg; t->cb = cb;
    if (t->tiled) {
        for (int i = 0; i < t->cols * t->rows; i++)
            if (t->tiles[i]) SDL_SetTextureColorMod(t->tiles[i], cr, cg, cb);
        return true;
    }
    if (!t->single) return true;
    return SDL_SetTextureColorMod(t->single, cr, cg, cb) == 0;
}
bool og_sdl_GetTextureColorMod(OG_Tex *t, uint8_t *cr, uint8_t *cg, uint8_t *cb) {
    if (!t) return false;
    if (cr) *cr = t->cr; if (cg) *cg = t->cg; if (cb) *cb = t->cb;
    return true;
}
bool og_sdl_SetTextureColorModFloat(OG_Tex *t, float cr, float cg, float cb) {
    return og_sdl_SetTextureColorMod(t, clamp_u8(cr), clamp_u8(cg), clamp_u8(cb));
}
bool og_sdl_SetTextureAlphaMod(OG_Tex *t, uint8_t alpha) {
    if (!t) return false;
    t->ca = alpha;
    if (t->tiled) {
        for (int i = 0; i < t->cols * t->rows; i++)
            if (t->tiles[i]) SDL_SetTextureAlphaMod(t->tiles[i], alpha);
        return true;
    }
    if (!t->single) return true;
    return SDL_SetTextureAlphaMod(t->single, alpha) == 0;
}
bool og_sdl_SetTextureAlphaModFloat(OG_Tex *t, float alpha) { return og_sdl_SetTextureAlphaMod(t, clamp_u8(alpha)); }
bool og_sdl_GetTextureAlphaModFloat(OG_Tex *t, float *alpha) {
    if (!t) return false;
    if (alpha) *alpha = (float)t->ca / 255.0f;
    return true;
}
uint32_t og_sdl_ComposeCustomBlendMode(int srcColor, int dstColor, int colorOp, int srcAlpha, int dstAlpha, int alphaOp) {
    return (uint32_t)SDL_ComposeCustomBlendMode(
        (SDL_BlendFactor)srcColor, (SDL_BlendFactor)dstColor, (SDL_BlendOperation)colorOp,
        (SDL_BlendFactor)srcAlpha, (SDL_BlendFactor)dstAlpha, (SDL_BlendOperation)alphaOp);
}

OG_Surface *og_sdl_CreateSurfaceFrom(int w, int h, uint32_t fmt, void *pixels, int pitch) {
    OG_Surface *s = (OG_Surface *)calloc(1, sizeof(OG_Surface));
    if (!s) return NULL;
    s->flags = 1u; /* borrowed pixels */
    s->format = fmt;
    s->w = w;
    s->h = h;
    s->pitch = pitch;
    s->pixels = pixels;
    return s;
}
OG_Surface *og_sdl_ConvertSurface(OG_Surface *s, uint32_t fmt) {
    (void)fmt;
    if (!s || !s->pixels) return NULL;
    OG_Surface *c = (OG_Surface *)calloc(1, sizeof(OG_Surface));
    if (!c) return NULL;
    *c = *s;
    c->flags = 0u;
    size_t bytes = (size_t)s->pitch * (size_t)s->h;
    c->pixels = malloc(bytes);
    if (!c->pixels) { free(c); return NULL; }
    memcpy(c->pixels, s->pixels, bytes);
    return c;
}
void og_sdl_DestroySurface(OG_Surface *s) {
    if (!s) return;
    if ((s->flags & 1u) == 0u) free(s->pixels);
    free(s);
}

static bool translate_event(const SDL_Event *in, OG_Event *out) {
    memset(out, 0, sizeof(*out));
    switch (in->type) {
        case SDL_QUIT:
            out->type = OG_EVENT_QUIT;
            return true;
        case SDL_WINDOWEVENT:
            if (in->window.event == SDL_WINDOWEVENT_RESIZED || in->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                out->type = OG_EVENT_WINDOW_RESIZED;
                out->window.windowID = in->window.windowID;
                out->window.data1 = in->window.data1;
                out->window.data2 = in->window.data2;
                return true;
            }
            if (in->window.event == SDL_WINDOWEVENT_LEAVE) {
                out->type = OG_EVENT_WINDOW_MOUSE_LEAVE;
                out->window.windowID = in->window.windowID;
                return true;
            }
            if (in->window.event == SDL_WINDOWEVENT_CLOSE) {
                out->type = OG_EVENT_WINDOW_CLOSE_REQUESTED;
                out->window.windowID = in->window.windowID;
                return true;
            }
            return false;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            out->type = (in->type == SDL_KEYDOWN) ? OG_EVENT_KEY_DOWN : OG_EVENT_KEY_UP;
            out->key.windowID = in->key.windowID;
            out->key.scancode = (int)in->key.keysym.scancode;
            out->key.key      = (uint32_t)in->key.keysym.sym;
            out->key.mod      = (uint16_t)in->key.keysym.mod;
            out->key.down     = (in->type == SDL_KEYDOWN);
            out->key.repeat   = (in->key.repeat != 0);
            return true;
        case SDL_TEXTINPUT:
            out->type = OG_EVENT_TEXT_INPUT;
            out->text.windowID = in->text.windowID;
            strncpy(out->text.buf, in->text.text, sizeof(out->text.buf) - 1);
            out->text.text = out->text.buf;
            return true;
        case SDL_MOUSEMOTION:
            out->type = OG_EVENT_MOUSE_MOTION;
            out->motion.windowID = in->motion.windowID;
            out->motion.x = (float)in->motion.x;
            out->motion.y = (float)in->motion.y;
            out->motion.xrel = (float)in->motion.xrel;
            out->motion.yrel = (float)in->motion.yrel;
            g_last_mx = out->motion.x;
            g_last_my = out->motion.y;
            return true;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            out->type = (in->type == SDL_MOUSEBUTTONDOWN) ? OG_EVENT_MOUSE_BUTTON_DOWN : OG_EVENT_MOUSE_BUTTON_UP;
            out->button.windowID = in->button.windowID;
            out->button.button = in->button.button;
            out->button.down = (in->type == SDL_MOUSEBUTTONDOWN);
            out->button.clicks = in->button.clicks;
            out->button.x = (float)in->button.x;
            out->button.y = (float)in->button.y;
            g_last_mx = out->button.x;
            g_last_my = out->button.y;
            return true;
        case SDL_MOUSEWHEEL:
            out->type = OG_EVENT_MOUSE_WHEEL;
            out->wheel.windowID = in->wheel.windowID;
            out->wheel.x = (float)in->wheel.x;
            out->wheel.y = (float)in->wheel.y;
            out->wheel.mouse_x = g_last_mx;
            out->wheel.mouse_y = g_last_my;
            return true;
        default:
            return false;
    }
}

bool og_sdl_PollEvent(OG_Event *ev) {
    if (g_synth_head != g_synth_tail) {
        if (ev) *ev = g_synth_q[g_synth_head];
        g_synth_head = (g_synth_head + 1) % SYNTH_Q_CAP;
        return true;
    }
    SDL_Event raw;
    while (SDL_PollEvent(&raw)) {
        OG_Event tmp;
        if (translate_event(&raw, &tmp)) {
            if (ev) *ev = tmp;
            return true;
        }
    }
    return false;
}
bool og_sdl_WaitEventTimeout(OG_Event *ev, int32_t timeout_ms) {
    if (og_sdl_PollEvent(ev)) return true;
    SDL_Event raw;
    if (SDL_WaitEventTimeout(&raw, timeout_ms)) {
        OG_Event tmp;
        if (translate_event(&raw, &tmp)) {
            if (ev) *ev = tmp;
            return true;
        }
    }
    return false;
}
bool og_sdl_PushEvent(OG_Event *ev) {
    if (!ev) return false;
    int next = (g_synth_tail + 1) % SYNTH_Q_CAP;
    if (next == g_synth_head) return false;
    g_synth_q[g_synth_tail] = *ev;
    g_synth_tail = next;
    return true;
}

const bool *og_sdl_GetKeyboardState(int *numkeys) {
    int n = 0;
    const uint8_t *ks = SDL_GetKeyboardState(&n);
    if (n > SDL_NUM_SCANCODES) n = SDL_NUM_SCANCODES;
    for (int i = 0; i < n; i++) g_key_bools[i] = (ks[i] != 0);
    if (numkeys) *numkeys = n;
    return g_key_bools;
}
uint16_t og_sdl_GetModState(void) { return (uint16_t)SDL_GetModState(); }
uint32_t og_sdl_GetKeyFromName(const char *name) { return (uint32_t)SDL_GetKeyFromName(name); }
bool og_sdl_StartTextInput(SDL_Window *w) { (void)w; SDL_StartTextInput(); return true; }
bool og_sdl_StopTextInput(SDL_Window *w) { (void)w; SDL_StopTextInput(); return true; }
bool og_sdl_TextInputActive(SDL_Window *w) { (void)w; return SDL_IsTextInputActive() == SDL_TRUE; }
bool og_sdl_HideCursor(void) { SDL_ShowCursor(SDL_DISABLE); return true; }
bool og_sdl_CursorVisible(void) { return SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE; }

uint64_t og_sdl_GetTicks(void) { return SDL_GetTicks64(); }
uint64_t og_sdl_GetTicksNS(void) { return SDL_GetTicks64() * 1000000ull; }
uint64_t og_sdl_GetPerformanceCounter(void) { return SDL_GetPerformanceCounter(); }
uint64_t og_sdl_GetPerformanceFrequency(void) { return SDL_GetPerformanceFrequency(); }
void og_sdl_DelayPrecise(uint64_t ns) {
    uint32_t ms = (uint32_t)(ns / 1000000ull);
    if (ms > 0) SDL_Delay(ms);
}

OG_AudioStream *og_sdl_OpenAudioDeviceStream(uint32_t devid, const OG_AudioSpec *spec, void *cb, void *userdata) {
    (void)devid; (void)cb; (void)userdata;
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq     = spec ? spec->freq : 44100;
    want.format   = AUDIO_F32SYS;
    want.channels = spec ? (uint8_t)spec->channels : 2;
    want.samples  = 1024;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) return NULL;
    OG_AudioStream *s = (OG_AudioStream *)calloc(1, sizeof(OG_AudioStream));
    if (!s) { SDL_CloseAudioDevice(dev); return NULL; }
    s->dev = dev;
    return s;
}
bool og_sdl_ResumeAudioStreamDevice(OG_AudioStream *s) {
    if (!s) return false;
    SDL_PauseAudioDevice(s->dev, 0);
    return true;
}
bool og_sdl_PauseAudioStreamDevice(OG_AudioStream *s) {
    if (!s) return false;
    SDL_PauseAudioDevice(s->dev, 1);
    return true;
}
int og_sdl_GetAudioStreamQueued(OG_AudioStream *s) {
    if (!s) return -1;
    return (int)SDL_GetQueuedAudioSize(s->dev);
}
bool og_sdl_PutAudioStreamData(OG_AudioStream *s, const void *buf, int len) {
    if (!s || !buf || len <= 0) return false;
    return SDL_QueueAudio(s->dev, buf, (uint32_t)len) == 0;
}
void og_sdl_DestroyAudioStream(OG_AudioStream *s) {
    if (!s) return;
    SDL_CloseAudioDevice(s->dev);
    free(s);
}
