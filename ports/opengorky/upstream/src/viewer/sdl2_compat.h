#ifndef OG_SDL2_COMPAT_H
#define OG_SDL2_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_AudioStream SDL_AudioStream;

typedef uint32_t SDL_Keycode;
typedef uint16_t SDL_Keymod;
typedef uint32_t SDL_BlendMode;
typedef uint32_t SDL_PixelFormat;
typedef uint32_t SDL_WindowFlags;
typedef uint32_t SDL_AudioDeviceID;

typedef struct SDL_Rect {
    int x, y, w, h;
} SDL_Rect;

typedef struct SDL_FRect {
    float x, y, w, h;
} SDL_FRect;

typedef struct SDL_FPoint {
    float x, y;
} SDL_FPoint;

typedef struct SDL_FColor {
    float r, g, b, a;
} SDL_FColor;

typedef struct SDL_Vertex {
    SDL_FPoint position;
    SDL_FColor color;
    SDL_FPoint tex_coord;
} SDL_Vertex;

typedef struct SDL_Surface {
    uint32_t flags;
    SDL_PixelFormat format;
    int w;
    int h;
    int pitch;
    void *pixels;
} SDL_Surface;

typedef struct SDL_Locale {
    const char *language;
    const char *country;
} SDL_Locale;

typedef struct SDL_AudioSpec {
    uint32_t format;
    int channels;
    int freq;
} SDL_AudioSpec;

typedef void (*SDL_DialogFileCallback)(void *userdata, const char *const *filelist, int filter);

/* Init flags */
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_VIDEO 0x00000020u

/* Window flags */
#define SDL_WINDOW_FULLSCREEN         0x00000001u
#define SDL_WINDOW_HIDDEN             0x00000008u
#define SDL_WINDOW_RESIZABLE          0x00000020u
#define SDL_WINDOW_MINIMIZED          0x00000040u
#define SDL_WINDOW_INPUT_FOCUS        0x00000200u
#define SDL_WINDOW_HIGH_PIXEL_DENSITY 0x00002000u
#define SDL_WINDOW_OCCLUDED           0x00004000u

/* Pixel format */
#define SDL_PIXELFORMAT_ABGR8888 0x16762004u

/* Texture access */
#define SDL_TEXTUREACCESS_STATIC    0
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET    2

/* Scale modes */
#define SDL_SCALEMODE_NEAREST  0
#define SDL_SCALEMODE_LINEAR   1
#define SDL_SCALEMODE_PIXELART 0

/* Flip modes */
#define SDL_FLIP_NONE       0
#define SDL_FLIP_HORIZONTAL 1

/* Logical presentation */
#define SDL_LOGICAL_PRESENTATION_DISABLED      0
#define SDL_LOGICAL_PRESENTATION_STRETCH       1
#define SDL_LOGICAL_PRESENTATION_LETTERBOX     2
#define SDL_LOGICAL_PRESENTATION_INTEGER_SCALE 4

/* Blend modes & factors (SDL2 compatible numeric values) */
#define SDL_BLENDMODE_NONE  0x00000000u
#define SDL_BLENDMODE_BLEND 0x00000001u
#define SDL_BLENDMODE_ADD   0x00000002u

#define SDL_BLENDOPERATION_ADD 0x1
#define SDL_BLENDFACTOR_ZERO                0x1
#define SDL_BLENDFACTOR_ONE                 0x2
#define SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR 0x4
#define SDL_BLENDFACTOR_DST_COLOR           0x9

/* Audio */
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0xFFFFFFFFu
#define SDL_AUDIO_F32                     0x8120u

/* Message box */
#define SDL_MESSAGEBOX_ERROR 0x00000010u

/* Mouse buttons */
#define SDL_BUTTON_LEFT  1
#define SDL_BUTTON_RIGHT 3

/* Keymods */
#define SDL_KMOD_LSHIFT 0x0001u
#define SDL_KMOD_RSHIFT 0x0002u
#define SDL_KMOD_SHIFT  (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT)
#define SDL_KMOD_LCTRL  0x0040u
#define SDL_KMOD_RCTRL  0x0080u
#define SDL_KMOD_CTRL   (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL)
#define SDL_KMOD_LALT   0x0100u
#define SDL_KMOD_RALT   0x0200u
#define SDL_KMOD_ALT    (SDL_KMOD_LALT | SDL_KMOD_RALT)
#define SDL_KMOD_LGUI   0x0400u
#define SDL_KMOD_RGUI   0x0800u
#define SDL_KMOD_GUI    (SDL_KMOD_LGUI | SDL_KMOD_RGUI)

/* Scancodes (SDL2 standard USB HID values) */
#define SDL_SCANCODE_RIGHT 79
#define SDL_SCANCODE_LEFT  80
#define SDL_SCANCODE_DOWN  81
#define SDL_SCANCODE_UP    82

/* Keycodes (SDL2 values with SDL3 uppercase names) */
#define SDLK_BACKSPACE    8
#define SDLK_TAB          9
#define SDLK_RETURN       13
#define SDLK_ESCAPE       27
#define SDLK_SPACE        32
#define SDLK_MINUS        '-'
#define SDLK_EQUALS       '='
#define SDLK_1            '1'
#define SDLK_2            '2'
#define SDLK_3            '3'
#define SDLK_4            '4'
#define SDLK_5            '5'
#define SDLK_6            '6'
#define SDLK_LEFTBRACKET  '['
#define SDLK_RIGHTBRACKET ']'
#define SDLK_GRAVE        '`'
#define SDLK_A            'a'
#define SDLK_D            'd'
#define SDLK_E            'e'
#define SDLK_F            'f'
#define SDLK_G            'g'
#define SDLK_H            'h'
#define SDLK_I            'i'
#define SDLK_K            'k'
#define SDLK_L            'l'
#define SDLK_M            'm'
#define SDLK_N            'n'
#define SDLK_O            'o'
#define SDLK_P            'p'
#define SDLK_Q            'q'
#define SDLK_R            'r'
#define SDLK_S            's'
#define SDLK_W            'w'
#define SDLK_X            'x'
#define SDLK_Y            'y'
#define SDLK_F1           (0x40000000u | 58u)
#define SDLK_F2           (0x40000000u | 59u)
#define SDLK_F10          (0x40000000u | 67u)
#define SDLK_F11          (0x40000000u | 68u)
#define SDLK_HOME         (0x40000000u | 74u)
#define SDLK_PAGEUP       (0x40000000u | 75u)
#define SDLK_END          (0x40000000u | 77u)
#define SDLK_PAGEDOWN     (0x40000000u | 78u)
#define SDLK_RIGHT        (0x40000000u | 79u)
#define SDLK_LEFT         (0x40000000u | 80u)
#define SDLK_DOWN         (0x40000000u | 81u)
#define SDLK_UP           (0x40000000u | 82u)
#define SDLK_KP_ENTER     (0x40000000u | 88u)

/* Events */
#define SDL_EVENT_QUIT                   0x100u
#define SDL_EVENT_WINDOW_RESIZED         0x206u
#define SDL_EVENT_WINDOW_MOUSE_LEAVE     0x20Du
#define SDL_EVENT_WINDOW_CLOSE_REQUESTED 0x210u
#define SDL_EVENT_KEY_DOWN               0x300u
#define SDL_EVENT_KEY_UP                 0x301u
#define SDL_EVENT_TEXT_INPUT             0x303u
#define SDL_EVENT_MOUSE_MOTION           0x400u
#define SDL_EVENT_MOUSE_BUTTON_DOWN      0x401u
#define SDL_EVENT_MOUSE_BUTTON_UP        0x402u
#define SDL_EVENT_MOUSE_WHEEL            0x403u

typedef struct SDL_KeyboardEvent {
    uint32_t type;
    uint32_t windowID;
    int scancode;
    SDL_Keycode key;
    SDL_Keymod mod;
    bool down;
    bool repeat;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent {
    uint32_t type;
    uint32_t windowID;
    const char *text;
    char buf[64];
} SDL_TextInputEvent;

typedef struct SDL_MouseMotionEvent {
    uint32_t type;
    uint32_t windowID;
    float x, y;
    float xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    uint32_t type;
    uint32_t windowID;
    uint8_t button;
    bool down;
    uint8_t clicks;
    float x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent {
    uint32_t type;
    uint32_t windowID;
    float x, y;
    float mouse_x, mouse_y;
} SDL_MouseWheelEvent;

typedef struct SDL_WindowEvent {
    uint32_t type;
    uint32_t windowID;
    int32_t data1;
    int32_t data2;
} SDL_WindowEvent;

typedef union SDL_Event {
    uint32_t type;
    SDL_WindowEvent window;
    SDL_KeyboardEvent key;
    SDL_TextInputEvent text;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    uint8_t padding[128];
} SDL_Event;

/* Functions (prefixed og_sdl_* to never collide with libSDL2 symbols) */
bool og_sdl_Init(uint32_t flags);
bool og_sdl_InitSubSystem(uint32_t flags);
void og_sdl_QuitSubSystem(uint32_t flags);
void og_sdl_Quit(void);
const char *og_sdl_GetError(void);
void og_sdl_free(void *mem);

bool og_sdl_CreateWindowAndRenderer(const char *title, int w, int h, SDL_WindowFlags flags, SDL_Window **window, SDL_Renderer **renderer);
void og_sdl_DestroyWindow(SDL_Window *w);
void og_sdl_DestroyRenderer(SDL_Renderer *r);
uint32_t og_sdl_GetWindowID(SDL_Window *w);
SDL_WindowFlags og_sdl_GetWindowFlags(SDL_Window *w);
bool og_sdl_GetWindowSize(SDL_Window *w, int *out_w, int *out_h);
float og_sdl_GetWindowPixelDensity(SDL_Window *w);
bool og_sdl_SetWindowMinimumSize(SDL_Window *w, int min_w, int min_h);
bool og_sdl_SetWindowFullscreen(SDL_Window *w, bool fullscreen);
bool og_sdl_SetWindowIcon(SDL_Window *w, SDL_Surface *icon);
bool og_sdl_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *msg, SDL_Window *w);
void og_sdl_ShowOpenFolderDialog(SDL_DialogFileCallback cb, void *userdata, SDL_Window *w, const char *default_loc, bool allow_many);
SDL_Locale **og_sdl_GetPreferredLocales(int *count);

bool og_sdl_SetRenderVSync(SDL_Renderer *r, int vsync);
bool og_sdl_GetRenderOutputSize(SDL_Renderer *r, int *w, int *h);
bool og_sdl_SetRenderLogicalPresentation(SDL_Renderer *r, int w, int h, int mode);
bool og_sdl_SetRenderScale(SDL_Renderer *r, float sx, float sy);
bool og_sdl_SetRenderViewport(SDL_Renderer *r, const SDL_Rect *rect);
bool og_sdl_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t);
SDL_Texture *og_sdl_GetRenderTarget(SDL_Renderer *r);
bool og_sdl_SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode mode);
bool og_sdl_SetRenderDrawColor(SDL_Renderer *r, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca);
bool og_sdl_SetRenderDrawColorFloat(SDL_Renderer *r, float cr, float cg, float cb, float ca);
bool og_sdl_RenderClear(SDL_Renderer *r);
bool og_sdl_RenderFillRect(SDL_Renderer *r, const SDL_FRect *rect);
bool og_sdl_RenderFillRects(SDL_Renderer *r, const SDL_FRect *rects, int count);
bool og_sdl_SetRenderClipRect(SDL_Renderer *r, const SDL_Rect *rect);
bool og_sdl_GetRectIntersection(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *result);
bool og_sdl_RenderLines(SDL_Renderer *r, const SDL_FPoint *pts, int count);
bool og_sdl_RenderTexture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *srcrect, const SDL_FRect *dstrect);
bool og_sdl_RenderTextureRotated(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *srcrect, const SDL_FRect *dstrect, double angle, const SDL_FPoint *center, int flip);
bool og_sdl_RenderTexture9Grid(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *srcrect, float lw, float rw, float th, float bh, float scale, const SDL_FRect *dstrect);
bool og_sdl_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *verts, int num_verts, const int *indices, int num_indices);
bool og_sdl_RenderDebugText(SDL_Renderer *r, float x, float y, const char *str);
bool og_sdl_RenderPresent(SDL_Renderer *r);
SDL_Surface *og_sdl_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect);
bool og_sdl_RenderCoordinatesToWindow(SDL_Renderer *r, float x, float y, float *wx, float *wy);
bool og_sdl_ConvertEventToRenderCoordinates(SDL_Renderer *r, SDL_Event *ev);

SDL_Texture *og_sdl_CreateTexture(SDL_Renderer *r, SDL_PixelFormat fmt, int access, int w, int h);
SDL_Texture *og_sdl_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s);
void og_sdl_DestroyTexture(SDL_Texture *t);
bool og_sdl_GetTextureSize(SDL_Texture *t, float *w, float *h);
bool og_sdl_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch);
bool og_sdl_LockTexture(SDL_Texture *t, const SDL_Rect *rect, void **pixels, int *pitch);
void og_sdl_UnlockTexture(SDL_Texture *t);
bool og_sdl_SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode mode);
bool og_sdl_GetTextureBlendMode(SDL_Texture *t, SDL_BlendMode *mode);
bool og_sdl_SetTextureScaleMode(SDL_Texture *t, int mode);
bool og_sdl_SetTextureColorMod(SDL_Texture *t, uint8_t cr, uint8_t cg, uint8_t cb);
bool og_sdl_GetTextureColorMod(SDL_Texture *t, uint8_t *cr, uint8_t *cg, uint8_t *cb);
bool og_sdl_SetTextureColorModFloat(SDL_Texture *t, float cr, float cg, float cb);
bool og_sdl_SetTextureAlphaMod(SDL_Texture *t, uint8_t alpha);
bool og_sdl_SetTextureAlphaModFloat(SDL_Texture *t, float alpha);
bool og_sdl_GetTextureAlphaModFloat(SDL_Texture *t, float *alpha);
SDL_BlendMode og_sdl_ComposeCustomBlendMode(int srcColor, int dstColor, int colorOp, int srcAlpha, int dstAlpha, int alphaOp);

SDL_Surface *og_sdl_CreateSurfaceFrom(int w, int h, SDL_PixelFormat fmt, void *pixels, int pitch);
SDL_Surface *og_sdl_ConvertSurface(SDL_Surface *s, SDL_PixelFormat fmt);
void og_sdl_DestroySurface(SDL_Surface *s);

bool og_sdl_PollEvent(SDL_Event *ev);
bool og_sdl_WaitEventTimeout(SDL_Event *ev, int32_t timeout_ms);
bool og_sdl_PushEvent(SDL_Event *ev);
const bool *og_sdl_GetKeyboardState(int *numkeys);
SDL_Keymod og_sdl_GetModState(void);
SDL_Keycode og_sdl_GetKeyFromName(const char *name);
bool og_sdl_StartTextInput(SDL_Window *w);
bool og_sdl_StopTextInput(SDL_Window *w);
bool og_sdl_TextInputActive(SDL_Window *w);
bool og_sdl_HideCursor(void);
bool og_sdl_CursorVisible(void);

uint64_t og_sdl_GetTicks(void);
uint64_t og_sdl_GetTicksNS(void);
uint64_t og_sdl_GetPerformanceCounter(void);
uint64_t og_sdl_GetPerformanceFrequency(void);
void og_sdl_DelayPrecise(uint64_t ns);

SDL_AudioStream *og_sdl_OpenAudioDeviceStream(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec, void *cb, void *userdata);
bool og_sdl_ResumeAudioStreamDevice(SDL_AudioStream *s);
bool og_sdl_PauseAudioStreamDevice(SDL_AudioStream *s);
int og_sdl_GetAudioStreamQueued(SDL_AudioStream *s);
bool og_sdl_PutAudioStreamData(SDL_AudioStream *s, const void *buf, int len);
void og_sdl_DestroyAudioStream(SDL_AudioStream *s);

#define SDL_Init                          og_sdl_Init
#define SDL_InitSubSystem                 og_sdl_InitSubSystem
#define SDL_QuitSubSystem                 og_sdl_QuitSubSystem
#define SDL_Quit                          og_sdl_Quit
#define SDL_GetError                      og_sdl_GetError
#define SDL_free                          og_sdl_free
#define SDL_CreateWindowAndRenderer       og_sdl_CreateWindowAndRenderer
#define SDL_DestroyWindow                 og_sdl_DestroyWindow
#define SDL_DestroyRenderer               og_sdl_DestroyRenderer
#define SDL_GetWindowID                   og_sdl_GetWindowID
#define SDL_GetWindowFlags                og_sdl_GetWindowFlags
#define SDL_GetWindowSize                 og_sdl_GetWindowSize
#define SDL_GetWindowPixelDensity         og_sdl_GetWindowPixelDensity
#define SDL_SetWindowMinimumSize          og_sdl_SetWindowMinimumSize
#define SDL_SetWindowFullscreen           og_sdl_SetWindowFullscreen
#define SDL_SetWindowIcon                 og_sdl_SetWindowIcon
#define SDL_ShowSimpleMessageBox          og_sdl_ShowSimpleMessageBox
#define SDL_ShowOpenFolderDialog          og_sdl_ShowOpenFolderDialog
#define SDL_GetPreferredLocales           og_sdl_GetPreferredLocales
#define SDL_SetRenderVSync                og_sdl_SetRenderVSync
#define SDL_GetRenderOutputSize           og_sdl_GetRenderOutputSize
#define SDL_SetRenderLogicalPresentation  og_sdl_SetRenderLogicalPresentation
#define SDL_SetRenderScale                og_sdl_SetRenderScale
#define SDL_SetRenderViewport             og_sdl_SetRenderViewport
#define SDL_SetRenderTarget               og_sdl_SetRenderTarget
#define SDL_GetRenderTarget               og_sdl_GetRenderTarget
#define SDL_SetRenderDrawBlendMode        og_sdl_SetRenderDrawBlendMode
#define SDL_SetRenderDrawColor            og_sdl_SetRenderDrawColor
#define SDL_SetRenderDrawColorFloat       og_sdl_SetRenderDrawColorFloat
#define SDL_RenderClear                   og_sdl_RenderClear
#define SDL_RenderFillRect                og_sdl_RenderFillRect
#define SDL_RenderFillRects               og_sdl_RenderFillRects
#define SDL_SetRenderClipRect             og_sdl_SetRenderClipRect
#define SDL_GetRectIntersection           og_sdl_GetRectIntersection
#define SDL_RenderLines                   og_sdl_RenderLines
#define SDL_RenderTexture                 og_sdl_RenderTexture
#define SDL_RenderTextureRotated          og_sdl_RenderTextureRotated
#define SDL_RenderTexture9Grid            og_sdl_RenderTexture9Grid
#define SDL_RenderGeometry                og_sdl_RenderGeometry
#define SDL_RenderDebugText               og_sdl_RenderDebugText
#define SDL_RenderPresent                 og_sdl_RenderPresent
#define SDL_RenderReadPixels              og_sdl_RenderReadPixels
#define SDL_RenderCoordinatesToWindow     og_sdl_RenderCoordinatesToWindow
#define SDL_ConvertEventToRenderCoordinates og_sdl_ConvertEventToRenderCoordinates
#define SDL_CreateTexture                 og_sdl_CreateTexture
#define SDL_CreateTextureFromSurface      og_sdl_CreateTextureFromSurface
#define SDL_DestroyTexture                og_sdl_DestroyTexture
#define SDL_GetTextureSize                og_sdl_GetTextureSize
#define SDL_UpdateTexture                 og_sdl_UpdateTexture
#define SDL_LockTexture                   og_sdl_LockTexture
#define SDL_UnlockTexture                 og_sdl_UnlockTexture
#define SDL_SetTextureBlendMode           og_sdl_SetTextureBlendMode
#define SDL_GetTextureBlendMode           og_sdl_GetTextureBlendMode
#define SDL_SetTextureScaleMode           og_sdl_SetTextureScaleMode
#define SDL_SetTextureColorMod            og_sdl_SetTextureColorMod
#define SDL_GetTextureColorMod            og_sdl_GetTextureColorMod
#define SDL_SetTextureColorModFloat       og_sdl_SetTextureColorModFloat
#define SDL_SetTextureAlphaMod            og_sdl_SetTextureAlphaMod
#define SDL_SetTextureAlphaModFloat       og_sdl_SetTextureAlphaModFloat
#define SDL_GetTextureAlphaModFloat       og_sdl_GetTextureAlphaModFloat
#define SDL_ComposeCustomBlendMode        og_sdl_ComposeCustomBlendMode
#define SDL_CreateSurfaceFrom             og_sdl_CreateSurfaceFrom
#define SDL_ConvertSurface                og_sdl_ConvertSurface
#define SDL_DestroySurface                og_sdl_DestroySurface
#define SDL_PollEvent                     og_sdl_PollEvent
#define SDL_WaitEventTimeout              og_sdl_WaitEventTimeout
#define SDL_PushEvent                     og_sdl_PushEvent
#define SDL_GetKeyboardState              og_sdl_GetKeyboardState
#define SDL_GetModState                   og_sdl_GetModState
#define SDL_GetKeyFromName                og_sdl_GetKeyFromName
#define SDL_StartTextInput                og_sdl_StartTextInput
#define SDL_StopTextInput                 og_sdl_StopTextInput
#define SDL_TextInputActive               og_sdl_TextInputActive
#define SDL_HideCursor                    og_sdl_HideCursor
#define SDL_CursorVisible                 og_sdl_CursorVisible
#define SDL_GetTicks                      og_sdl_GetTicks
#define SDL_GetTicksNS                    og_sdl_GetTicksNS
#define SDL_GetPerformanceCounter         og_sdl_GetPerformanceCounter
#define SDL_GetPerformanceFrequency       og_sdl_GetPerformanceFrequency
#define SDL_DelayPrecise                  og_sdl_DelayPrecise
#define SDL_OpenAudioDeviceStream         og_sdl_OpenAudioDeviceStream
#define SDL_ResumeAudioStreamDevice       og_sdl_ResumeAudioStreamDevice
#define SDL_PauseAudioStreamDevice        og_sdl_PauseAudioStreamDevice
#define SDL_GetAudioStreamQueued          og_sdl_GetAudioStreamQueued
#define SDL_PutAudioStreamData            og_sdl_PutAudioStreamData
#define SDL_DestroyAudioStream            og_sdl_DestroyAudioStream

#endif
