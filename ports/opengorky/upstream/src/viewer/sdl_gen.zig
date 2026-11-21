const __root = @This();
pub const __builtin = @import("std").zig.c_translation.builtins;
pub const __helpers = @import("std").zig.c_translation.helpers;
pub const __builtin_va_list = [*c]u8;
pub const __gnuc_va_list = __builtin_va_list;
pub const va_list = __gnuc_va_list;
pub extern fn __mingw_get_crt_info() [*c]const u8;
pub const rsize_t = usize;
pub const ptrdiff_t = c_longlong;
pub const wchar_t = c_ushort;
pub const wint_t = c_ushort;
pub const wctype_t = c_ushort;
pub const errno_t = c_int;
pub const __time32_t = c_long;
pub const __time64_t = c_longlong;
pub const time_t = __time64_t;
pub extern fn _invalid_parameter_noinfo() void;
pub extern fn _invalid_parameter_noinfo_noreturn() noreturn;
pub extern fn _invoke_watson(expression: [*c]const wchar_t, function_name: [*c]const wchar_t, file_name: [*c]const wchar_t, line_number: c_uint, reserved: c_ulonglong) noreturn;
pub const struct_threadlocaleinfostruct = opaque {};
pub const struct_threadmbcinfostruct = opaque {};
pub const pthreadlocinfo = ?*struct_threadlocaleinfostruct;
pub const pthreadmbcinfo = ?*struct_threadmbcinfostruct;
pub const struct___lc_time_data = opaque {};
pub const struct_localeinfo_struct = extern struct {
    locinfo: pthreadlocinfo = null,
    mbcinfo: pthreadmbcinfo = null,
};
pub const _locale_tstruct = struct_localeinfo_struct;
pub const _locale_t = [*c]struct_localeinfo_struct;
pub const struct_tagLC_ID = extern struct {
    wLanguage: c_ushort = 0,
    wCountry: c_ushort = 0,
    wCodePage: c_ushort = 0,
};
pub const LC_ID = struct_tagLC_ID;
pub const LPLC_ID = [*c]struct_tagLC_ID;
pub const max_align_t = extern struct {
    __aro_max_align_ll: c_longlong = 0,
    __aro_max_align_ld: c_longdouble = 0,
};
pub const int_least8_t = i8;
pub const uint_least8_t = u8;
pub const int_least16_t = c_short;
pub const uint_least16_t = c_ushort;
pub const int_least32_t = c_int;
pub const uint_least32_t = c_uint;
pub const int_least64_t = c_longlong;
pub const uint_least64_t = c_ulonglong;
pub const int_fast8_t = i8;
pub const uint_fast8_t = u8;
pub const int_fast16_t = c_short;
pub const uint_fast16_t = c_ushort;
pub const int_fast32_t = c_int;
pub const uint_fast32_t = c_uint;
pub const int_fast64_t = c_longlong;
pub const uint_fast64_t = c_ulonglong;
pub const intmax_t = c_longlong;
pub const uintmax_t = c_ulonglong;
pub const struct_SDL_Window = opaque {
    pub const og_sdl_DestroyWindow = __root.og_sdl_DestroyWindow;
    pub const og_sdl_GetWindowID = __root.og_sdl_GetWindowID;
    pub const og_sdl_GetWindowFlags = __root.og_sdl_GetWindowFlags;
    pub const og_sdl_GetWindowSize = __root.og_sdl_GetWindowSize;
    pub const og_sdl_GetWindowPixelDensity = __root.og_sdl_GetWindowPixelDensity;
    pub const og_sdl_SetWindowMinimumSize = __root.og_sdl_SetWindowMinimumSize;
    pub const og_sdl_SetWindowFullscreen = __root.og_sdl_SetWindowFullscreen;
    pub const og_sdl_SetWindowIcon = __root.og_sdl_SetWindowIcon;
    pub const og_sdl_StartTextInput = __root.og_sdl_StartTextInput;
    pub const og_sdl_StopTextInput = __root.og_sdl_StopTextInput;
    pub const og_sdl_TextInputActive = __root.og_sdl_TextInputActive;
    pub const DestroyWindow = __root.og_sdl_DestroyWindow;
    pub const GetWindowID = __root.og_sdl_GetWindowID;
    pub const GetWindowFlags = __root.og_sdl_GetWindowFlags;
    pub const GetWindowSize = __root.og_sdl_GetWindowSize;
    pub const GetWindowPixelDensity = __root.og_sdl_GetWindowPixelDensity;
    pub const SetWindowMinimumSize = __root.og_sdl_SetWindowMinimumSize;
    pub const SetWindowFullscreen = __root.og_sdl_SetWindowFullscreen;
    pub const SetWindowIcon = __root.og_sdl_SetWindowIcon;
    pub const StartTextInput = __root.og_sdl_StartTextInput;
    pub const StopTextInput = __root.og_sdl_StopTextInput;
    pub const TextInputActive = __root.og_sdl_TextInputActive;
};
pub const SDL_Window = struct_SDL_Window;
pub const struct_SDL_Renderer = opaque {
    pub const og_sdl_DestroyRenderer = __root.og_sdl_DestroyRenderer;
    pub const og_sdl_SetRenderVSync = __root.og_sdl_SetRenderVSync;
    pub const og_sdl_GetRenderOutputSize = __root.og_sdl_GetRenderOutputSize;
    pub const og_sdl_SetRenderLogicalPresentation = __root.og_sdl_SetRenderLogicalPresentation;
    pub const og_sdl_SetRenderScale = __root.og_sdl_SetRenderScale;
    pub const og_sdl_SetRenderViewport = __root.og_sdl_SetRenderViewport;
    pub const og_sdl_SetRenderTarget = __root.og_sdl_SetRenderTarget;
    pub const og_sdl_GetRenderTarget = __root.og_sdl_GetRenderTarget;
    pub const og_sdl_SetRenderDrawBlendMode = __root.og_sdl_SetRenderDrawBlendMode;
    pub const og_sdl_SetRenderDrawColor = __root.og_sdl_SetRenderDrawColor;
    pub const og_sdl_SetRenderDrawColorFloat = __root.og_sdl_SetRenderDrawColorFloat;
    pub const og_sdl_RenderClear = __root.og_sdl_RenderClear;
    pub const og_sdl_RenderFillRect = __root.og_sdl_RenderFillRect;
    pub const og_sdl_RenderFillRects = __root.og_sdl_RenderFillRects;
    pub const og_sdl_SetRenderClipRect = __root.og_sdl_SetRenderClipRect;
    pub const og_sdl_RenderLines = __root.og_sdl_RenderLines;
    pub const og_sdl_RenderTexture = __root.og_sdl_RenderTexture;
    pub const og_sdl_RenderTextureRotated = __root.og_sdl_RenderTextureRotated;
    pub const og_sdl_RenderTexture9Grid = __root.og_sdl_RenderTexture9Grid;
    pub const og_sdl_RenderGeometry = __root.og_sdl_RenderGeometry;
    pub const og_sdl_RenderDebugText = __root.og_sdl_RenderDebugText;
    pub const og_sdl_RenderPresent = __root.og_sdl_RenderPresent;
    pub const og_sdl_RenderReadPixels = __root.og_sdl_RenderReadPixels;
    pub const og_sdl_RenderCoordinatesToWindow = __root.og_sdl_RenderCoordinatesToWindow;
    pub const og_sdl_ConvertEventToRenderCoordinates = __root.og_sdl_ConvertEventToRenderCoordinates;
    pub const og_sdl_CreateTexture = __root.og_sdl_CreateTexture;
    pub const og_sdl_CreateTextureFromSurface = __root.og_sdl_CreateTextureFromSurface;
    pub const DestroyRenderer = __root.og_sdl_DestroyRenderer;
    pub const SetRenderVSync = __root.og_sdl_SetRenderVSync;
    pub const GetRenderOutputSize = __root.og_sdl_GetRenderOutputSize;
    pub const SetRenderLogicalPresentation = __root.og_sdl_SetRenderLogicalPresentation;
    pub const SetRenderScale = __root.og_sdl_SetRenderScale;
    pub const SetRenderViewport = __root.og_sdl_SetRenderViewport;
    pub const SetRenderTarget = __root.og_sdl_SetRenderTarget;
    pub const GetRenderTarget = __root.og_sdl_GetRenderTarget;
    pub const SetRenderDrawBlendMode = __root.og_sdl_SetRenderDrawBlendMode;
    pub const SetRenderDrawColor = __root.og_sdl_SetRenderDrawColor;
    pub const SetRenderDrawColorFloat = __root.og_sdl_SetRenderDrawColorFloat;
    pub const RenderClear = __root.og_sdl_RenderClear;
    pub const RenderFillRect = __root.og_sdl_RenderFillRect;
    pub const RenderFillRects = __root.og_sdl_RenderFillRects;
    pub const SetRenderClipRect = __root.og_sdl_SetRenderClipRect;
    pub const RenderLines = __root.og_sdl_RenderLines;
    pub const RenderTexture = __root.og_sdl_RenderTexture;
    pub const RenderTextureRotated = __root.og_sdl_RenderTextureRotated;
    pub const RenderTexture9Grid = __root.og_sdl_RenderTexture9Grid;
    pub const RenderGeometry = __root.og_sdl_RenderGeometry;
    pub const RenderDebugText = __root.og_sdl_RenderDebugText;
    pub const RenderPresent = __root.og_sdl_RenderPresent;
    pub const RenderReadPixels = __root.og_sdl_RenderReadPixels;
    pub const RenderCoordinatesToWindow = __root.og_sdl_RenderCoordinatesToWindow;
    pub const ConvertEventToRenderCoordinates = __root.og_sdl_ConvertEventToRenderCoordinates;
    pub const CreateTexture = __root.og_sdl_CreateTexture;
    pub const CreateTextureFromSurface = __root.og_sdl_CreateTextureFromSurface;
};
pub const SDL_Renderer = struct_SDL_Renderer;
pub const struct_SDL_Texture = opaque {
    pub const og_sdl_DestroyTexture = __root.og_sdl_DestroyTexture;
    pub const og_sdl_GetTextureSize = __root.og_sdl_GetTextureSize;
    pub const og_sdl_UpdateTexture = __root.og_sdl_UpdateTexture;
    pub const og_sdl_LockTexture = __root.og_sdl_LockTexture;
    pub const og_sdl_UnlockTexture = __root.og_sdl_UnlockTexture;
    pub const og_sdl_SetTextureBlendMode = __root.og_sdl_SetTextureBlendMode;
    pub const og_sdl_GetTextureBlendMode = __root.og_sdl_GetTextureBlendMode;
    pub const og_sdl_SetTextureScaleMode = __root.og_sdl_SetTextureScaleMode;
    pub const og_sdl_SetTextureColorMod = __root.og_sdl_SetTextureColorMod;
    pub const og_sdl_GetTextureColorMod = __root.og_sdl_GetTextureColorMod;
    pub const og_sdl_SetTextureColorModFloat = __root.og_sdl_SetTextureColorModFloat;
    pub const og_sdl_SetTextureAlphaMod = __root.og_sdl_SetTextureAlphaMod;
    pub const og_sdl_SetTextureAlphaModFloat = __root.og_sdl_SetTextureAlphaModFloat;
    pub const og_sdl_GetTextureAlphaModFloat = __root.og_sdl_GetTextureAlphaModFloat;
    pub const DestroyTexture = __root.og_sdl_DestroyTexture;
    pub const GetTextureSize = __root.og_sdl_GetTextureSize;
    pub const UpdateTexture = __root.og_sdl_UpdateTexture;
    pub const LockTexture = __root.og_sdl_LockTexture;
    pub const UnlockTexture = __root.og_sdl_UnlockTexture;
    pub const SetTextureBlendMode = __root.og_sdl_SetTextureBlendMode;
    pub const GetTextureBlendMode = __root.og_sdl_GetTextureBlendMode;
    pub const SetTextureScaleMode = __root.og_sdl_SetTextureScaleMode;
    pub const SetTextureColorMod = __root.og_sdl_SetTextureColorMod;
    pub const GetTextureColorMod = __root.og_sdl_GetTextureColorMod;
    pub const SetTextureColorModFloat = __root.og_sdl_SetTextureColorModFloat;
    pub const SetTextureAlphaMod = __root.og_sdl_SetTextureAlphaMod;
    pub const SetTextureAlphaModFloat = __root.og_sdl_SetTextureAlphaModFloat;
    pub const GetTextureAlphaModFloat = __root.og_sdl_GetTextureAlphaModFloat;
};
pub const SDL_Texture = struct_SDL_Texture;
pub const struct_SDL_AudioStream = opaque {
    pub const og_sdl_ResumeAudioStreamDevice = __root.og_sdl_ResumeAudioStreamDevice;
    pub const og_sdl_PauseAudioStreamDevice = __root.og_sdl_PauseAudioStreamDevice;
    pub const og_sdl_GetAudioStreamQueued = __root.og_sdl_GetAudioStreamQueued;
    pub const og_sdl_PutAudioStreamData = __root.og_sdl_PutAudioStreamData;
    pub const og_sdl_DestroyAudioStream = __root.og_sdl_DestroyAudioStream;
    pub const ResumeAudioStreamDevice = __root.og_sdl_ResumeAudioStreamDevice;
    pub const PauseAudioStreamDevice = __root.og_sdl_PauseAudioStreamDevice;
    pub const GetAudioStreamQueued = __root.og_sdl_GetAudioStreamQueued;
    pub const PutAudioStreamData = __root.og_sdl_PutAudioStreamData;
    pub const DestroyAudioStream = __root.og_sdl_DestroyAudioStream;
};
pub const SDL_AudioStream = struct_SDL_AudioStream;
pub const SDL_Keycode = u32;
pub const SDL_Keymod = u16;
pub const SDL_BlendMode = u32;
pub const SDL_PixelFormat = u32;
pub const SDL_WindowFlags = u32;
pub const SDL_AudioDeviceID = u32;
pub const struct_SDL_Rect = extern struct {
    x: c_int = 0,
    y: c_int = 0,
    w: c_int = 0,
    h: c_int = 0,
    pub const og_sdl_GetRectIntersection = __root.og_sdl_GetRectIntersection;
    pub const GetRectIntersection = __root.og_sdl_GetRectIntersection;
};
pub const SDL_Rect = struct_SDL_Rect;
pub const struct_SDL_FRect = extern struct {
    x: f32 = 0,
    y: f32 = 0,
    w: f32 = 0,
    h: f32 = 0,
};
pub const SDL_FRect = struct_SDL_FRect;
pub const struct_SDL_FPoint = extern struct {
    x: f32 = 0,
    y: f32 = 0,
};
pub const SDL_FPoint = struct_SDL_FPoint;
pub const struct_SDL_FColor = extern struct {
    r: f32 = 0,
    g: f32 = 0,
    b: f32 = 0,
    a: f32 = 0,
};
pub const SDL_FColor = struct_SDL_FColor;
pub const struct_SDL_Vertex = extern struct {
    position: SDL_FPoint = @import("std").mem.zeroes(SDL_FPoint),
    color: SDL_FColor = @import("std").mem.zeroes(SDL_FColor),
    tex_coord: SDL_FPoint = @import("std").mem.zeroes(SDL_FPoint),
};
pub const SDL_Vertex = struct_SDL_Vertex;
pub const struct_SDL_Surface = extern struct {
    flags: u32 = 0,
    format: SDL_PixelFormat = 0,
    w: c_int = 0,
    h: c_int = 0,
    pitch: c_int = 0,
    pixels: ?*anyopaque = null,
    pub const og_sdl_ConvertSurface = __root.og_sdl_ConvertSurface;
    pub const og_sdl_DestroySurface = __root.og_sdl_DestroySurface;
    pub const ConvertSurface = __root.og_sdl_ConvertSurface;
    pub const DestroySurface = __root.og_sdl_DestroySurface;
};
pub const SDL_Surface = struct_SDL_Surface;
pub const struct_SDL_Locale = extern struct {
    language: [*c]const u8 = null,
    country: [*c]const u8 = null,
};
pub const SDL_Locale = struct_SDL_Locale;
pub const struct_SDL_AudioSpec = extern struct {
    format: u32 = 0,
    channels: c_int = 0,
    freq: c_int = 0,
};
pub const SDL_AudioSpec = struct_SDL_AudioSpec;
pub const SDL_DialogFileCallback = ?*const fn (userdata: ?*anyopaque, filelist: [*c]const [*c]const u8, filter: c_int) callconv(.c) void;
pub const struct_SDL_KeyboardEvent = extern struct {
    type: u32 = 0,
    windowID: u32 = 0,
    scancode: c_int = 0,
    key: SDL_Keycode = 0,
    mod: SDL_Keymod = 0,
    down: bool = false,
    repeat: bool = false,
};
pub const SDL_KeyboardEvent = struct_SDL_KeyboardEvent;
pub const struct_SDL_TextInputEvent = extern struct {
    type: u32 = 0,
    windowID: u32 = 0,
    text: [*c]const u8 = null,
    buf: [64]u8 = @import("std").mem.zeroes([64]u8),
};
pub const SDL_TextInputEvent = struct_SDL_TextInputEvent;
pub const struct_SDL_MouseMotionEvent = extern struct {
    type: u32 = 0,
    windowID: u32 = 0,
    x: f32 = 0,
    y: f32 = 0,
    xrel: f32 = 0,
    yrel: f32 = 0,
};
pub const SDL_MouseMotionEvent = struct_SDL_MouseMotionEvent;
pub const struct_SDL_MouseButtonEvent = extern struct {
    type: u32 = 0,
    windowID: u32 = 0,
    button: u8 = 0,
    down: bool = false,
    clicks: u8 = 0,
    x: f32 = 0,
    y: f32 = 0,
};
pub const SDL_MouseButtonEvent = struct_SDL_MouseButtonEvent;
pub const struct_SDL_MouseWheelEvent = extern struct {
    type: u32 = 0,
    windowID: u32 = 0,
    x: f32 = 0,
    y: f32 = 0,
    mouse_x: f32 = 0,
    mouse_y: f32 = 0,
};
pub const SDL_MouseWheelEvent = struct_SDL_MouseWheelEvent;
pub const struct_SDL_WindowEvent = extern struct {
    type: u32 = 0,
    windowID: u32 = 0,
    data1: i32 = 0,
    data2: i32 = 0,
};
pub const SDL_WindowEvent = struct_SDL_WindowEvent;
pub const union_SDL_Event = extern union {
    type: u32,
    window: SDL_WindowEvent,
    key: SDL_KeyboardEvent,
    text: SDL_TextInputEvent,
    motion: SDL_MouseMotionEvent,
    button: SDL_MouseButtonEvent,
    wheel: SDL_MouseWheelEvent,
    padding: [128]u8,
    pub const og_sdl_PollEvent = __root.og_sdl_PollEvent;
    pub const og_sdl_WaitEventTimeout = __root.og_sdl_WaitEventTimeout;
    pub const og_sdl_PushEvent = __root.og_sdl_PushEvent;
    pub const PollEvent = __root.og_sdl_PollEvent;
    pub const WaitEventTimeout = __root.og_sdl_WaitEventTimeout;
    pub const PushEvent = __root.og_sdl_PushEvent;
};
pub const SDL_Event = union_SDL_Event;
pub extern fn og_sdl_Init(flags: u32) bool;
pub extern fn og_sdl_InitSubSystem(flags: u32) bool;
pub extern fn og_sdl_QuitSubSystem(flags: u32) void;
pub extern fn og_sdl_Quit() void;
pub extern fn og_sdl_GetError() [*c]const u8;
pub extern fn og_sdl_free(mem: ?*anyopaque) void;
pub extern fn og_sdl_CreateWindowAndRenderer(title: [*c]const u8, w: c_int, h: c_int, flags: SDL_WindowFlags, window: [*c]?*SDL_Window, renderer: [*c]?*SDL_Renderer) bool;
pub extern fn og_sdl_DestroyWindow(w: ?*SDL_Window) void;
pub extern fn og_sdl_DestroyRenderer(r: ?*SDL_Renderer) void;
pub extern fn og_sdl_GetWindowID(w: ?*SDL_Window) u32;
pub extern fn og_sdl_GetWindowFlags(w: ?*SDL_Window) SDL_WindowFlags;
pub extern fn og_sdl_GetWindowSize(w: ?*SDL_Window, out_w: [*c]c_int, out_h: [*c]c_int) bool;
pub extern fn og_sdl_GetWindowPixelDensity(w: ?*SDL_Window) f32;
pub extern fn og_sdl_SetWindowMinimumSize(w: ?*SDL_Window, min_w: c_int, min_h: c_int) bool;
pub extern fn og_sdl_SetWindowFullscreen(w: ?*SDL_Window, fullscreen: bool) bool;
pub extern fn og_sdl_SetWindowIcon(w: ?*SDL_Window, icon: [*c]SDL_Surface) bool;
pub extern fn og_sdl_ShowSimpleMessageBox(flags: u32, title: [*c]const u8, msg: [*c]const u8, w: ?*SDL_Window) bool;
pub extern fn og_sdl_ShowOpenFolderDialog(cb: SDL_DialogFileCallback, userdata: ?*anyopaque, w: ?*SDL_Window, default_loc: [*c]const u8, allow_many: bool) void;
pub extern fn og_sdl_GetPreferredLocales(count: [*c]c_int) [*c][*c]SDL_Locale;
pub extern fn og_sdl_SetRenderVSync(r: ?*SDL_Renderer, vsync: c_int) bool;
pub extern fn og_sdl_GetRenderOutputSize(r: ?*SDL_Renderer, w: [*c]c_int, h: [*c]c_int) bool;
pub extern fn og_sdl_SetRenderLogicalPresentation(r: ?*SDL_Renderer, w: c_int, h: c_int, mode: c_int) bool;
pub extern fn og_sdl_SetRenderScale(r: ?*SDL_Renderer, sx: f32, sy: f32) bool;
pub extern fn og_sdl_SetRenderViewport(r: ?*SDL_Renderer, rect: [*c]const SDL_Rect) bool;
pub extern fn og_sdl_SetRenderTarget(r: ?*SDL_Renderer, t: ?*SDL_Texture) bool;
pub extern fn og_sdl_GetRenderTarget(r: ?*SDL_Renderer) ?*SDL_Texture;
pub extern fn og_sdl_SetRenderDrawBlendMode(r: ?*SDL_Renderer, mode: SDL_BlendMode) bool;
pub extern fn og_sdl_SetRenderDrawColor(r: ?*SDL_Renderer, cr: u8, cg: u8, cb: u8, ca: u8) bool;
pub extern fn og_sdl_SetRenderDrawColorFloat(r: ?*SDL_Renderer, cr: f32, cg: f32, cb: f32, ca: f32) bool;
pub extern fn og_sdl_RenderClear(r: ?*SDL_Renderer) bool;
pub extern fn og_sdl_RenderFillRect(r: ?*SDL_Renderer, rect: [*c]const SDL_FRect) bool;
pub extern fn og_sdl_RenderFillRects(r: ?*SDL_Renderer, rects: [*c]const SDL_FRect, count: c_int) bool;
pub extern fn og_sdl_SetRenderClipRect(r: ?*SDL_Renderer, rect: [*c]const SDL_Rect) bool;
pub extern fn og_sdl_GetRectIntersection(a: [*c]const SDL_Rect, b: [*c]const SDL_Rect, result: [*c]SDL_Rect) bool;
pub extern fn og_sdl_RenderLines(r: ?*SDL_Renderer, pts: [*c]const SDL_FPoint, count: c_int) bool;
pub extern fn og_sdl_RenderTexture(r: ?*SDL_Renderer, t: ?*SDL_Texture, srcrect: [*c]const SDL_FRect, dstrect: [*c]const SDL_FRect) bool;
pub extern fn og_sdl_RenderTextureRotated(r: ?*SDL_Renderer, t: ?*SDL_Texture, srcrect: [*c]const SDL_FRect, dstrect: [*c]const SDL_FRect, angle: f64, center: [*c]const SDL_FPoint, flip: c_int) bool;
pub extern fn og_sdl_RenderTexture9Grid(r: ?*SDL_Renderer, t: ?*SDL_Texture, srcrect: [*c]const SDL_FRect, lw: f32, rw: f32, th: f32, bh: f32, scale: f32, dstrect: [*c]const SDL_FRect) bool;
pub extern fn og_sdl_RenderGeometry(r: ?*SDL_Renderer, t: ?*SDL_Texture, verts: [*c]const SDL_Vertex, num_verts: c_int, indices: [*c]const c_int, num_indices: c_int) bool;
pub extern fn og_sdl_RenderDebugText(r: ?*SDL_Renderer, x: f32, y: f32, str: [*c]const u8) bool;
pub extern fn og_sdl_RenderPresent(r: ?*SDL_Renderer) bool;
pub extern fn og_sdl_RenderReadPixels(r: ?*SDL_Renderer, rect: [*c]const SDL_Rect) [*c]SDL_Surface;
pub extern fn og_sdl_RenderCoordinatesToWindow(r: ?*SDL_Renderer, x: f32, y: f32, wx: [*c]f32, wy: [*c]f32) bool;
pub extern fn og_sdl_ConvertEventToRenderCoordinates(r: ?*SDL_Renderer, ev: [*c]SDL_Event) bool;
pub extern fn og_sdl_CreateTexture(r: ?*SDL_Renderer, fmt: SDL_PixelFormat, access: c_int, w: c_int, h: c_int) ?*SDL_Texture;
pub extern fn og_sdl_CreateTextureFromSurface(r: ?*SDL_Renderer, s: [*c]SDL_Surface) ?*SDL_Texture;
pub extern fn og_sdl_DestroyTexture(t: ?*SDL_Texture) void;
pub extern fn og_sdl_GetTextureSize(t: ?*SDL_Texture, w: [*c]f32, h: [*c]f32) bool;
pub extern fn og_sdl_UpdateTexture(t: ?*SDL_Texture, rect: [*c]const SDL_Rect, pixels: ?*const anyopaque, pitch: c_int) bool;
pub extern fn og_sdl_LockTexture(t: ?*SDL_Texture, rect: [*c]const SDL_Rect, pixels: [*c]?*anyopaque, pitch: [*c]c_int) bool;
pub extern fn og_sdl_UnlockTexture(t: ?*SDL_Texture) void;
pub extern fn og_sdl_SetTextureBlendMode(t: ?*SDL_Texture, mode: SDL_BlendMode) bool;
pub extern fn og_sdl_GetTextureBlendMode(t: ?*SDL_Texture, mode: [*c]SDL_BlendMode) bool;
pub extern fn og_sdl_SetTextureScaleMode(t: ?*SDL_Texture, mode: c_int) bool;
pub extern fn og_sdl_SetTextureColorMod(t: ?*SDL_Texture, cr: u8, cg: u8, cb: u8) bool;
pub extern fn og_sdl_GetTextureColorMod(t: ?*SDL_Texture, cr: [*c]u8, cg: [*c]u8, cb: [*c]u8) bool;
pub extern fn og_sdl_SetTextureColorModFloat(t: ?*SDL_Texture, cr: f32, cg: f32, cb: f32) bool;
pub extern fn og_sdl_SetTextureAlphaMod(t: ?*SDL_Texture, alpha: u8) bool;
pub extern fn og_sdl_SetTextureAlphaModFloat(t: ?*SDL_Texture, alpha: f32) bool;
pub extern fn og_sdl_GetTextureAlphaModFloat(t: ?*SDL_Texture, alpha: [*c]f32) bool;
pub extern fn og_sdl_ComposeCustomBlendMode(srcColor: c_int, dstColor: c_int, colorOp: c_int, srcAlpha: c_int, dstAlpha: c_int, alphaOp: c_int) SDL_BlendMode;
pub extern fn og_sdl_CreateSurfaceFrom(w: c_int, h: c_int, fmt: SDL_PixelFormat, pixels: ?*anyopaque, pitch: c_int) [*c]SDL_Surface;
pub extern fn og_sdl_ConvertSurface(s: [*c]SDL_Surface, fmt: SDL_PixelFormat) [*c]SDL_Surface;
pub extern fn og_sdl_DestroySurface(s: [*c]SDL_Surface) void;
pub extern fn og_sdl_PollEvent(ev: [*c]SDL_Event) bool;
pub extern fn og_sdl_WaitEventTimeout(ev: [*c]SDL_Event, timeout_ms: i32) bool;
pub extern fn og_sdl_PushEvent(ev: [*c]SDL_Event) bool;
pub extern fn og_sdl_GetKeyboardState(numkeys: [*c]c_int) [*c]const bool;
pub extern fn og_sdl_GetModState() SDL_Keymod;
pub extern fn og_sdl_GetKeyFromName(name: [*c]const u8) SDL_Keycode;
pub extern fn og_sdl_StartTextInput(w: ?*SDL_Window) bool;
pub extern fn og_sdl_StopTextInput(w: ?*SDL_Window) bool;
pub extern fn og_sdl_TextInputActive(w: ?*SDL_Window) bool;
pub extern fn og_sdl_HideCursor() bool;
pub extern fn og_sdl_CursorVisible() bool;
pub extern fn og_sdl_GetTicks() u64;
pub extern fn og_sdl_GetTicksNS() u64;
pub extern fn og_sdl_GetPerformanceCounter() u64;
pub extern fn og_sdl_GetPerformanceFrequency() u64;
pub extern fn og_sdl_DelayPrecise(ns: u64) void;
pub extern fn og_sdl_OpenAudioDeviceStream(devid: SDL_AudioDeviceID, spec: [*c]const SDL_AudioSpec, cb: ?*anyopaque, userdata: ?*anyopaque) ?*SDL_AudioStream;
pub extern fn og_sdl_ResumeAudioStreamDevice(s: ?*SDL_AudioStream) bool;
pub extern fn og_sdl_PauseAudioStreamDevice(s: ?*SDL_AudioStream) bool;
pub extern fn og_sdl_GetAudioStreamQueued(s: ?*SDL_AudioStream) c_int;
pub extern fn og_sdl_PutAudioStreamData(s: ?*SDL_AudioStream, buf: ?*const anyopaque, len: c_int) bool;
pub extern fn og_sdl_DestroyAudioStream(s: ?*SDL_AudioStream) void;

pub const __VERSION__ = "Aro aro-zig";
pub const __Aro__ = "";
pub const __STDC__ = @as(c_int, 1);
pub const __STDC_HOSTED__ = @as(c_int, 1);
pub const __STDC_UTF_16__ = @as(c_int, 1);
pub const __STDC_UTF_32__ = @as(c_int, 1);
pub const __STDC_EMBED_NOT_FOUND__ = @as(c_int, 0);
pub const __STDC_EMBED_FOUND__ = @as(c_int, 1);
pub const __STDC_EMBED_EMPTY__ = @as(c_int, 2);
pub const __STDC_VERSION__ = @as(c_long, 201710);
pub const __GNUC__ = @as(c_int, 7);
pub const __GNUC_MINOR__ = @as(c_int, 1);
pub const __GNUC_PATCHLEVEL__ = @as(c_int, 0);
pub const __ARO_EMULATE_NO__ = @as(c_int, 0);
pub const __ARO_EMULATE_CLANG__ = @as(c_int, 1);
pub const __ARO_EMULATE_GCC__ = @as(c_int, 2);
pub const __ARO_EMULATE_MSVC__ = @as(c_int, 3);
pub const __ARO_EMULATE__ = __ARO_EMULATE_GCC__;
pub inline fn __building_module(x: anytype) @TypeOf(@as(c_int, 0)) {
    _ = &x;
    return @as(c_int, 0);
}
pub const _WIN32 = @as(c_int, 1);
pub const _WIN64 = @as(c_int, 1);
pub const WIN32 = @as(c_int, 1);
pub const __WIN32 = @as(c_int, 1);
pub const __WIN32__ = @as(c_int, 1);
pub const WINNT = @as(c_int, 1);
pub const __WINNT = @as(c_int, 1);
pub const __WINNT__ = @as(c_int, 1);
pub const WIN64 = @as(c_int, 1);
pub const __WIN64 = @as(c_int, 1);
pub const __WIN64__ = @as(c_int, 1);
pub const __MINGW64__ = @as(c_int, 1);
pub const __MSVCRT__ = @as(c_int, 1);
pub const __MINGW32__ = @as(c_int, 1);
pub const __declspec = @compileError("unable to translate C expr: unexpected token '__attribute__'");
// <builtin>:34:9
pub const _cdecl = @compileError("unable to translate macro: undefined identifier `__cdecl__`");
// <builtin>:35:9
pub const __cdecl = @compileError("unable to translate macro: undefined identifier `__cdecl__`");
// <builtin>:36:9
pub const _stdcall = @compileError("unable to translate macro: undefined identifier `__stdcall__`");
// <builtin>:37:9
pub const __stdcall = @compileError("unable to translate macro: undefined identifier `__stdcall__`");
// <builtin>:38:9
pub const _fastcall = @compileError("unable to translate macro: undefined identifier `__fastcall__`");
// <builtin>:39:9
pub const __fastcall = @compileError("unable to translate macro: undefined identifier `__fastcall__`");
// <builtin>:40:9
pub const _thiscall = @compileError("unable to translate macro: undefined identifier `__thiscall__`");
// <builtin>:41:9
pub const __thiscall = @compileError("unable to translate macro: undefined identifier `__thiscall__`");
// <builtin>:42:9
pub const __code_model_small__ = @as(c_int, 1);
pub const __amd64__ = @as(c_int, 1);
pub const __amd64 = @as(c_int, 1);
pub const __x86_64__ = @as(c_int, 1);
pub const __x86_64 = @as(c_int, 1);
pub const __SEG_GS = @as(c_int, 1);
pub const __SEG_FS = @as(c_int, 1);
pub const __seg_gs = @compileError("unable to translate macro: undefined identifier `address_space`");
// <builtin>:50:9
pub const __seg_fs = @compileError("unable to translate macro: undefined identifier `address_space`");
// <builtin>:51:9
pub const __LAHF_SAHF__ = @as(c_int, 1);
pub const __AES__ = @as(c_int, 1);
pub const __VAES__ = @as(c_int, 1);
pub const __PCLMUL__ = @as(c_int, 1);
pub const __VPCLMULQDQ__ = @as(c_int, 1);
pub const __LZCNT__ = @as(c_int, 1);
pub const __RDRND__ = @as(c_int, 1);
pub const __FSGSBASE__ = @as(c_int, 1);
pub const __BMI__ = @as(c_int, 1);
pub const __BMI2__ = @as(c_int, 1);
pub const __POPCNT__ = @as(c_int, 1);
pub const __PRFCHW__ = @as(c_int, 1);
pub const __RDSEED__ = @as(c_int, 1);
pub const __ADX__ = @as(c_int, 1);
pub const __MOVBE__ = @as(c_int, 1);
pub const __FMA__ = @as(c_int, 1);
pub const __F16C__ = @as(c_int, 1);
pub const __GFNI__ = @as(c_int, 1);
pub const __SHA__ = @as(c_int, 1);
pub const __SHA512__ = @as(c_int, 1);
pub const __FXSR__ = @as(c_int, 1);
pub const __XSAVE__ = @as(c_int, 1);
pub const __XSAVEOPT__ = @as(c_int, 1);
pub const __XSAVEC__ = @as(c_int, 1);
pub const __XSAVES__ = @as(c_int, 1);
pub const __CLFLUSHOPT__ = @as(c_int, 1);
pub const __CLWB__ = @as(c_int, 1);
pub const __SHSTK__ = @as(c_int, 1);
pub const __SM3__ = @as(c_int, 1);
pub const __SM4__ = @as(c_int, 1);
pub const __RDPID__ = @as(c_int, 1);
pub const __WAITPKG__ = @as(c_int, 1);
pub const __MOVDIRI__ = @as(c_int, 1);
pub const __MOVDIR64B__ = @as(c_int, 1);
pub const __PTWRITE__ = @as(c_int, 1);
pub const __INVPCID__ = @as(c_int, 1);
pub const __HRESET__ = @as(c_int, 1);
pub const __CMPCCXADD__ = @as(c_int, 1);
pub const __AVXIFMA__ = @as(c_int, 1);
pub const __AVXNECONVERT__ = @as(c_int, 1);
pub const __AVXVNNI__ = @as(c_int, 1);
pub const __AVXVNNIINT16__ = @as(c_int, 1);
pub const __AVXVNNIINT8__ = @as(c_int, 1);
pub const __SERIALIZE__ = @as(c_int, 1);
pub const __CRC32__ = @as(c_int, 1);
pub const __AVX2__ = @as(c_int, 1);
pub const __AVX__ = @as(c_int, 1);
pub const __SSE4_2__ = @as(c_int, 1);
pub const __SSE4_1__ = @as(c_int, 1);
pub const __SSSE3__ = @as(c_int, 1);
pub const __SSE3__ = @as(c_int, 1);
pub const __SSE2__ = @as(c_int, 1);
pub const __SSE__ = @as(c_int, 1);
pub const __SSE_MATH__ = @as(c_int, 1);
pub const __MMX__ = @as(c_int, 1);
pub const __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 = @as(c_int, 1);
pub const __ORDER_LITTLE_ENDIAN__ = @as(c_int, 1234);
pub const __ORDER_BIG_ENDIAN__ = @as(c_int, 4321);
pub const __ORDER_PDP_ENDIAN__ = @as(c_int, 3412);
pub const __BYTE_ORDER__ = __ORDER_LITTLE_ENDIAN__;
pub const __LITTLE_ENDIAN__ = @as(c_int, 1);
pub const __ATOMIC_RELAXED = @as(c_int, 0);
pub const __ATOMIC_CONSUME = @as(c_int, 1);
pub const __ATOMIC_ACQUIRE = @as(c_int, 2);
pub const __ATOMIC_RELEASE = @as(c_int, 3);
pub const __ATOMIC_ACQ_REL = @as(c_int, 4);
pub const __ATOMIC_SEQ_CST = @as(c_int, 5);
pub const __ATOMIC_BOOL_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_CHAR_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_CHAR16_T_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_CHAR32_T_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_WCHAR_T_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_WINT_T_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_SHORT_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_INT_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_LONG_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_LLONG_LOCK_FREE = @as(c_int, 1);
pub const __ATOMIC_POINTER_LOCK_FREE = @as(c_int, 1);
pub const __WCHAR_UNSIGNED__ = @as(c_int, 1);
pub const __WINT_UNSIGNED__ = @as(c_int, 1);
pub const __CHAR_BIT__ = @as(c_int, 8);
pub const __BOOL_WIDTH__ = @as(c_int, 8);
pub const __SCHAR_MAX__ = @as(c_int, 127);
pub const __SCHAR_WIDTH__ = @as(c_int, 8);
pub const __SHRT_MAX__ = @as(c_int, 32767);
pub const __SHRT_WIDTH__ = @as(c_int, 16);
pub const __INT_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __INT_WIDTH__ = @as(c_int, 32);
pub const __LONG_MAX__ = @as(c_long, 2147483647);
pub const __LONG_WIDTH__ = @as(c_int, 32);
pub const __LONG_LONG_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __LONG_LONG_WIDTH__ = @as(c_int, 64);
pub const __WCHAR_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const __WCHAR_WIDTH__ = @as(c_int, 16);
pub const __WINT_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const __WINT_WIDTH__ = @as(c_int, 16);
pub const __INTMAX_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INTMAX_WIDTH__ = @as(c_int, 64);
pub const __SIZE_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const __SIZE_WIDTH__ = @as(c_int, 64);
pub const __UINTMAX_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const __UINTMAX_WIDTH__ = @as(c_int, 64);
pub const __PTRDIFF_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __PTRDIFF_WIDTH__ = @as(c_int, 64);
pub const __INTPTR_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INTPTR_WIDTH__ = @as(c_int, 64);
pub const __UINTPTR_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const __UINTPTR_WIDTH__ = @as(c_int, 64);
pub const __SIG_ATOMIC_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __SIG_ATOMIC_WIDTH__ = @as(c_int, 32);
pub const __BITINT_MAXWIDTH__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const __SIZEOF_FLOAT__ = @as(c_int, 4);
pub const __SIZEOF_DOUBLE__ = @as(c_int, 8);
pub const __SIZEOF_LONG_DOUBLE__ = @as(c_int, 10);
pub const __SIZEOF_SHORT__ = @as(c_int, 2);
pub const __SIZEOF_INT__ = @as(c_int, 4);
pub const __SIZEOF_LONG__ = @as(c_int, 4);
pub const __SIZEOF_LONG_LONG__ = @as(c_int, 8);
pub const __SIZEOF_POINTER__ = @as(c_int, 8);
pub const __SIZEOF_PTRDIFF_T__ = @as(c_int, 8);
pub const __SIZEOF_SIZE_T__ = @as(c_int, 8);
pub const __SIZEOF_WCHAR_T__ = @as(c_int, 2);
pub const __SIZEOF_WINT_T__ = @as(c_int, 2);
pub const __SIZEOF_INT128__ = @as(c_int, 16);
pub const __INTPTR_TYPE__ = c_longlong;
pub const __UINTPTR_TYPE__ = c_ulonglong;
pub const __INTMAX_TYPE__ = c_longlong;
pub const __INTMAX_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `LL`");
// <builtin>:180:9
pub const __INTMAX_C = __helpers.LL_SUFFIX;
pub const __UINTMAX_TYPE__ = c_ulonglong;
pub const __UINTMAX_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `ULL`");
// <builtin>:183:9
pub const __UINTMAX_C = __helpers.ULL_SUFFIX;
pub const __PTRDIFF_TYPE__ = c_longlong;
pub const __SIZE_TYPE__ = c_ulonglong;
pub const __WCHAR_TYPE__ = c_ushort;
pub const __WINT_TYPE__ = c_ushort;
pub const __CHAR16_TYPE__ = c_ushort;
pub const __CHAR32_TYPE__ = c_uint;
pub const __INT8_TYPE__ = i8;
pub const __INT8_FMTd__ = "hhd";
pub const __INT8_FMTi__ = "hhi";
pub const __INT8_C_SUFFIX__ = "";
pub inline fn __INT8_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __INT16_TYPE__ = c_short;
pub const __INT16_FMTd__ = "hd";
pub const __INT16_FMTi__ = "hi";
pub const __INT16_C_SUFFIX__ = "";
pub inline fn __INT16_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __INT32_TYPE__ = c_int;
pub const __INT32_FMTd__ = "d";
pub const __INT32_FMTi__ = "i";
pub const __INT32_C_SUFFIX__ = "";
pub inline fn __INT32_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __INT64_TYPE__ = c_longlong;
pub const __INT64_FMTd__ = "lld";
pub const __INT64_FMTi__ = "lli";
pub const __INT64_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `LL`");
// <builtin>:209:9
pub const __INT64_C = __helpers.LL_SUFFIX;
pub const __UINT8_TYPE__ = u8;
pub const __UINT8_FMTo__ = "hho";
pub const __UINT8_FMTu__ = "hhu";
pub const __UINT8_FMTx__ = "hhx";
pub const __UINT8_FMTX__ = "hhX";
pub const __UINT8_C_SUFFIX__ = "";
pub inline fn __UINT8_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __UINT8_MAX__ = @as(c_int, 255);
pub const __INT8_MAX__ = @as(c_int, 127);
pub const __UINT16_TYPE__ = c_ushort;
pub const __UINT16_FMTo__ = "ho";
pub const __UINT16_FMTu__ = "hu";
pub const __UINT16_FMTx__ = "hx";
pub const __UINT16_FMTX__ = "hX";
pub const __UINT16_C_SUFFIX__ = "";
pub inline fn __UINT16_C(c: anytype) @TypeOf(c) {
    _ = &c;
    return c;
}
pub const __UINT16_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const __INT16_MAX__ = @as(c_int, 32767);
pub const __UINT32_TYPE__ = c_uint;
pub const __UINT32_FMTo__ = "o";
pub const __UINT32_FMTu__ = "u";
pub const __UINT32_FMTx__ = "x";
pub const __UINT32_FMTX__ = "X";
pub const __UINT32_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `U`");
// <builtin>:234:9
pub const __UINT32_C = __helpers.U_SUFFIX;
pub const __UINT32_MAX__ = __helpers.promoteIntLiteral(c_uint, 4294967295, .decimal);
pub const __INT32_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __UINT64_TYPE__ = c_ulonglong;
pub const __UINT64_FMTo__ = "llo";
pub const __UINT64_FMTu__ = "llu";
pub const __UINT64_FMTx__ = "llx";
pub const __UINT64_FMTX__ = "llX";
pub const __UINT64_C_SUFFIX__ = @compileError("unable to translate macro: undefined identifier `ULL`");
// <builtin>:243:9
pub const __UINT64_C = __helpers.ULL_SUFFIX;
pub const __UINT64_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const __INT64_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INT_LEAST8_TYPE__ = i8;
pub const __INT_LEAST8_MAX__ = @as(c_int, 127);
pub const __INT_LEAST8_WIDTH__ = @as(c_int, 8);
pub const INT_LEAST8_FMTd__ = "hhd";
pub const INT_LEAST8_FMTi__ = "hhi";
pub const __UINT_LEAST8_TYPE__ = u8;
pub const __UINT_LEAST8_MAX__ = @as(c_int, 255);
pub const UINT_LEAST8_FMTo__ = "hho";
pub const UINT_LEAST8_FMTu__ = "hhu";
pub const UINT_LEAST8_FMTx__ = "hhx";
pub const UINT_LEAST8_FMTX__ = "hhX";
pub const __INT_FAST8_TYPE__ = i8;
pub const __INT_FAST8_MAX__ = @as(c_int, 127);
pub const __INT_FAST8_WIDTH__ = @as(c_int, 8);
pub const INT_FAST8_FMTd__ = "hhd";
pub const INT_FAST8_FMTi__ = "hhi";
pub const __UINT_FAST8_TYPE__ = u8;
pub const __UINT_FAST8_MAX__ = @as(c_int, 255);
pub const UINT_FAST8_FMTo__ = "hho";
pub const UINT_FAST8_FMTu__ = "hhu";
pub const UINT_FAST8_FMTx__ = "hhx";
pub const UINT_FAST8_FMTX__ = "hhX";
pub const __INT_LEAST16_TYPE__ = c_short;
pub const __INT_LEAST16_MAX__ = @as(c_int, 32767);
pub const __INT_LEAST16_WIDTH__ = @as(c_int, 16);
pub const INT_LEAST16_FMTd__ = "hd";
pub const INT_LEAST16_FMTi__ = "hi";
pub const __UINT_LEAST16_TYPE__ = c_ushort;
pub const __UINT_LEAST16_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const UINT_LEAST16_FMTo__ = "ho";
pub const UINT_LEAST16_FMTu__ = "hu";
pub const UINT_LEAST16_FMTx__ = "hx";
pub const UINT_LEAST16_FMTX__ = "hX";
pub const __INT_FAST16_TYPE__ = c_short;
pub const __INT_FAST16_MAX__ = @as(c_int, 32767);
pub const __INT_FAST16_WIDTH__ = @as(c_int, 16);
pub const INT_FAST16_FMTd__ = "hd";
pub const INT_FAST16_FMTi__ = "hi";
pub const __UINT_FAST16_TYPE__ = c_ushort;
pub const __UINT_FAST16_MAX__ = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const UINT_FAST16_FMTo__ = "ho";
pub const UINT_FAST16_FMTu__ = "hu";
pub const UINT_FAST16_FMTx__ = "hx";
pub const UINT_FAST16_FMTX__ = "hX";
pub const __INT_LEAST32_TYPE__ = c_int;
pub const __INT_LEAST32_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __INT_LEAST32_WIDTH__ = @as(c_int, 32);
pub const INT_LEAST32_FMTd__ = "d";
pub const INT_LEAST32_FMTi__ = "i";
pub const __UINT_LEAST32_TYPE__ = c_uint;
pub const __UINT_LEAST32_MAX__ = __helpers.promoteIntLiteral(c_uint, 4294967295, .decimal);
pub const UINT_LEAST32_FMTo__ = "o";
pub const UINT_LEAST32_FMTu__ = "u";
pub const UINT_LEAST32_FMTx__ = "x";
pub const UINT_LEAST32_FMTX__ = "X";
pub const __INT_FAST32_TYPE__ = c_int;
pub const __INT_FAST32_MAX__ = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const __INT_FAST32_WIDTH__ = @as(c_int, 32);
pub const INT_FAST32_FMTd__ = "d";
pub const INT_FAST32_FMTi__ = "i";
pub const __UINT_FAST32_TYPE__ = c_uint;
pub const __UINT_FAST32_MAX__ = __helpers.promoteIntLiteral(c_uint, 4294967295, .decimal);
pub const UINT_FAST32_FMTo__ = "o";
pub const UINT_FAST32_FMTu__ = "u";
pub const UINT_FAST32_FMTx__ = "x";
pub const UINT_FAST32_FMTX__ = "X";
pub const __INT_LEAST64_TYPE__ = c_longlong;
pub const __INT_LEAST64_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INT_LEAST64_WIDTH__ = @as(c_int, 64);
pub const INT_LEAST64_FMTd__ = "lld";
pub const INT_LEAST64_FMTi__ = "lli";
pub const __UINT_LEAST64_TYPE__ = c_ulonglong;
pub const __UINT_LEAST64_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const UINT_LEAST64_FMTo__ = "llo";
pub const UINT_LEAST64_FMTu__ = "llu";
pub const UINT_LEAST64_FMTx__ = "llx";
pub const UINT_LEAST64_FMTX__ = "llX";
pub const __INT_FAST64_TYPE__ = c_longlong;
pub const __INT_FAST64_MAX__ = @as(c_longlong, 9223372036854775807);
pub const __INT_FAST64_WIDTH__ = @as(c_int, 64);
pub const INT_FAST64_FMTd__ = "lld";
pub const INT_FAST64_FMTi__ = "lli";
pub const __UINT_FAST64_TYPE__ = c_ulonglong;
pub const __UINT_FAST64_MAX__ = @as(c_ulonglong, 18446744073709551615);
pub const UINT_FAST64_FMTo__ = "llo";
pub const UINT_FAST64_FMTu__ = "llu";
pub const UINT_FAST64_FMTx__ = "llx";
pub const UINT_FAST64_FMTX__ = "llX";
pub const __FLT16_DENORM_MIN__ = @as(f16, 5.9604644775390625e-8);
pub const __FLT16_HAS_DENORM__ = "";
pub const __FLT16_DIG__ = @as(c_int, 3);
pub const __FLT16_DECIMAL_DIG__ = @as(c_int, 5);
pub const __FLT16_EPSILON__ = @as(f16, 9.765625e-4);
pub const __FLT16_HAS_INFINITY__ = "";
pub const __FLT16_HAS_QUIET_NAN__ = "";
pub const __FLT16_MANT_DIG__ = @as(c_int, 11);
pub const __FLT16_MAX_10_EXP__ = @as(c_int, 4);
pub const __FLT16_MAX_EXP__ = @as(c_int, 16);
pub const __FLT16_MAX__ = @as(f16, 6.5504e+4);
pub const __FLT16_MIN_10_EXP__ = -@as(c_int, 4);
pub const __FLT16_MIN_EXP__ = -@as(c_int, 13);
pub const __FLT16_MIN__ = @as(f16, 6.103515625e-5);
pub const __FLT_DENORM_MIN__ = @as(f32, 1.40129846e-45);
pub const __FLT_HAS_DENORM__ = "";
pub const __FLT_DIG__ = @as(c_int, 6);
pub const __FLT_DECIMAL_DIG__ = @as(c_int, 9);
pub const __FLT_EPSILON__ = @as(f32, 1.19209290e-7);
pub const __FLT_HAS_INFINITY__ = "";
pub const __FLT_HAS_QUIET_NAN__ = "";
pub const __FLT_MANT_DIG__ = @as(c_int, 24);
pub const __FLT_MAX_10_EXP__ = @as(c_int, 38);
pub const __FLT_MAX_EXP__ = @as(c_int, 128);
pub const __FLT_MAX__ = @as(f32, 3.40282347e+38);
pub const __FLT_MIN_10_EXP__ = -@as(c_int, 37);
pub const __FLT_MIN_EXP__ = -@as(c_int, 125);
pub const __FLT_MIN__ = @as(f32, 1.17549435e-38);
pub const __DBL_DENORM_MIN__ = @as(f64, 4.9406564584124654e-324);
pub const __DBL_HAS_DENORM__ = "";
pub const __DBL_DIG__ = @as(c_int, 15);
pub const __DBL_DECIMAL_DIG__ = @as(c_int, 17);
pub const __DBL_EPSILON__ = @as(f64, 2.2204460492503131e-16);
pub const __DBL_HAS_INFINITY__ = "";
pub const __DBL_HAS_QUIET_NAN__ = "";
pub const __DBL_MANT_DIG__ = @as(c_int, 53);
pub const __DBL_MAX_10_EXP__ = @as(c_int, 308);
pub const __DBL_MAX_EXP__ = @as(c_int, 1024);
pub const __DBL_MAX__ = @as(f64, 1.7976931348623157e+308);
pub const __DBL_MIN_10_EXP__ = -@as(c_int, 307);
pub const __DBL_MIN_EXP__ = -@as(c_int, 1021);
pub const __DBL_MIN__ = @as(f64, 2.2250738585072014e-308);
pub const __LDBL_DENORM_MIN__ = @as(c_longdouble, 3.64519953188247460253e-4951);
pub const __LDBL_HAS_DENORM__ = "";
pub const __LDBL_DIG__ = @as(c_int, 18);
pub const __LDBL_DECIMAL_DIG__ = @as(c_int, 21);
pub const __LDBL_EPSILON__ = @as(c_longdouble, 1.08420217248550443401e-19);
pub const __LDBL_HAS_INFINITY__ = "";
pub const __LDBL_HAS_QUIET_NAN__ = "";
pub const __LDBL_MANT_DIG__ = @as(c_int, 64);
pub const __LDBL_MAX_10_EXP__ = @as(c_int, 4932);
pub const __LDBL_MAX_EXP__ = @as(c_int, 16384);
pub const __LDBL_MAX__ = @as(c_longdouble, 1.18973149535723176502e+4932);
pub const __LDBL_MIN_10_EXP__ = -@as(c_int, 4931);
pub const __LDBL_MIN_EXP__ = -@as(c_int, 16381);
pub const __LDBL_MIN__ = @as(c_longdouble, 3.36210314311209350626e-4932);
pub const __FLT_EVAL_METHOD__ = @as(c_int, 0);
pub const __FLT_RADIX__ = @as(c_int, 2);
pub const __DECIMAL_DIG__ = __LDBL_DECIMAL_DIG__;
pub const __pic__ = @as(c_int, 2);
pub const __PIC__ = @as(c_int, 2);
pub const __MSVCRT_VERSION__ = @as(c_int, 0xE00);
pub const _WIN32_WINNT = @as(c_int, 0x0a00);
pub const __CRT__NO_INLINE = @as(c_int, 1);
pub const OG_SDL2_COMPAT_H = "";
pub const __CLANG_STDINT_H = "";
pub const _STDINT_H = "";
pub const _INC_CRTDEFS = "";
pub const _INC_CORECRT = "";
pub const _INC__MINGW_H = "";
pub const _INC_CRTDEFS_MACRO = "";
pub const __MINGW64_PASTE2 = @compileError("unable to translate C expr: unexpected token '##'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:10:9
pub inline fn __MINGW64_PASTE(x: anytype, y: anytype) @TypeOf(__MINGW64_PASTE2(x, y)) {
    _ = &x;
    _ = &y;
    return __MINGW64_PASTE2(x, y);
}
pub const __STRINGIFY = @compileError("unable to translate C expr: unexpected token ''");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:13:9
pub inline fn __MINGW64_STRINGIFY(x: anytype) @TypeOf(__STRINGIFY(x)) {
    _ = &x;
    return __STRINGIFY(x);
}
pub const __MINGW64_VERSION_MAJOR = @as(c_int, 15);
pub const __MINGW64_VERSION_MINOR = @as(c_int, 0);
pub const __MINGW64_VERSION_BUGFIX = @as(c_int, 0);
pub const __MINGW64_VERSION_RC = @as(c_int, 0);
pub const __MINGW64_VERSION_STR = __MINGW64_STRINGIFY(__MINGW64_VERSION_MAJOR) ++ "." ++ __MINGW64_STRINGIFY(__MINGW64_VERSION_MINOR) ++ "." ++ __MINGW64_STRINGIFY(__MINGW64_VERSION_BUGFIX);
pub const __MINGW64_VERSION_STATE = "alpha";
pub const __MINGW32_MAJOR_VERSION = @as(c_int, 3);
pub const __MINGW32_MINOR_VERSION = @as(c_int, 11);
pub const _M_AMD64 = @as(c_int, 100);
pub const _M_X64 = @as(c_int, 100);
pub const __MINGW_USE_UNDERSCORE_PREFIX = @as(c_int, 0);
pub const __MINGW_IMP_SYMBOL = @compileError("unable to translate macro: undefined identifier `__imp_`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:129:11
pub const __MINGW_IMP_LSYMBOL = @compileError("unable to translate macro: undefined identifier `__imp_`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:130:11
pub inline fn __MINGW_USYMBOL(sym: anytype) @TypeOf(sym) {
    _ = &sym;
    return sym;
}
pub const __MINGW_LSYMBOL = @compileError("unable to translate macro: undefined identifier `_`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:132:11
pub const __MINGW_ASM_CALL = @compileError("unable to translate C expr: unexpected token '__asm__'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:140:9
pub const __MINGW_ASM_CRT_CALL = @compileError("unable to translate C expr: unexpected token '__asm__'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:141:9
pub const __MINGW_EXTENSION = @compileError("unable to translate C expr: unexpected token '__extension__'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:173:13
pub const __C89_NAMELESS = __MINGW_EXTENSION;
pub const __C89_NAMELESSSTRUCTNAME = "";
pub const __C89_NAMELESSSTRUCTNAME1 = "";
pub const __C89_NAMELESSSTRUCTNAME2 = "";
pub const __C89_NAMELESSSTRUCTNAME3 = "";
pub const __C89_NAMELESSSTRUCTNAME4 = "";
pub const __C89_NAMELESSSTRUCTNAME5 = "";
pub const __C89_NAMELESSUNIONNAME = "";
pub const __C89_NAMELESSUNIONNAME1 = "";
pub const __C89_NAMELESSUNIONNAME2 = "";
pub const __C89_NAMELESSUNIONNAME3 = "";
pub const __C89_NAMELESSUNIONNAME4 = "";
pub const __C89_NAMELESSUNIONNAME5 = "";
pub const __C89_NAMELESSUNIONNAME6 = "";
pub const __C89_NAMELESSUNIONNAME7 = "";
pub const __C89_NAMELESSUNIONNAME8 = "";
pub const __GNU_EXTENSION = __MINGW_EXTENSION;
pub const __MINGW_HAVE_ANSI_C99_PRINTF = @as(c_int, 1);
pub const __MINGW_HAVE_WIDE_C99_PRINTF = @as(c_int, 1);
pub const __MINGW_HAVE_ANSI_C99_SCANF = @as(c_int, 1);
pub const __MINGW_HAVE_WIDE_C99_SCANF = @as(c_int, 1);
pub const __MINGW_POISON_NAME = @compileError("unable to translate macro: undefined identifier `_layout_has_not_been_verified_and_its_declaration_is_most_likely_incorrect`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:213:11
pub const __MSABI_LONG = __helpers.L_SUFFIX;
pub const __MINGW_GCC_VERSION = ((__GNUC__ * @as(c_int, 10000)) + (__GNUC_MINOR__ * @as(c_int, 100))) + __GNUC_PATCHLEVEL__;
pub inline fn __MINGW_GNUC_PREREQ(major: anytype, minor: anytype) @TypeOf((__GNUC__ > major) or ((__GNUC__ == major) and (__GNUC_MINOR__ >= minor))) {
    _ = &major;
    _ = &minor;
    return (__GNUC__ > major) or ((__GNUC__ == major) and (__GNUC_MINOR__ >= minor));
}
pub inline fn __MINGW_MSC_PREREQ(major: anytype, minor: anytype) @TypeOf(@as(c_int, 0)) {
    _ = &major;
    _ = &minor;
    return @as(c_int, 0);
}
pub inline fn __MINGW_ATTRIB_DEPRECATED_STR(X: anytype) void {
    _ = &X;
    return;
}
pub const __MINGW_SEC_WARN_STR = "This function or variable may be unsafe, use _CRT_SECURE_NO_WARNINGS to disable deprecation";
pub const __MINGW_MSVC2005_DEPREC_STR = "This POSIX function is deprecated beginning in Visual C++ 2005, use _CRT_NONSTDC_NO_DEPRECATE to disable deprecation";
pub const __MINGW_ATTRIB_DEPRECATED_MSVC2005 = __MINGW_ATTRIB_DEPRECATED_STR(__MINGW_MSVC2005_DEPREC_STR);
pub const __MINGW_ATTRIB_DEPRECATED_SEC_WARN = __MINGW_ATTRIB_DEPRECATED_STR(__MINGW_SEC_WARN_STR);
pub const __MINGW_MS_PRINTF = @compileError("unable to translate macro: undefined identifier `__format__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:293:9
pub const __MINGW_MS_SCANF = @compileError("unable to translate macro: undefined identifier `__format__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:296:9
pub const __MINGW_GNU_PRINTF = @compileError("unable to translate macro: undefined identifier `__format__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:299:9
pub const __MINGW_GNU_SCANF = @compileError("unable to translate macro: undefined identifier `__format__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:302:9
pub const __mingw_ovr = @compileError("unable to translate macro: undefined identifier `__unused__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:311:11
pub const __mingw_attribute_artificial = @compileError("unable to translate macro: undefined identifier `__artificial__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:318:11
pub const __MINGW_SELECTANY = @compileError("unable to translate macro: undefined identifier `__selectany__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_mac.h:324:9
pub const __MINGW_FORTIFY_LEVEL = @as(c_int, 0);
pub const __mingw_bos_ovr = __mingw_ovr;
pub const __MINGW_FORTIFY_VA_ARG = @as(c_int, 0);
pub const _INC_MINGW_SECAPI = "";
pub const _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES = @as(c_int, 0);
pub const _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES_MEMORY = @as(c_int, 0);
pub const _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES = @as(c_int, 0);
pub const _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT = @as(c_int, 0);
pub const _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_MEMORY = @as(c_int, 0);
pub const __MINGW_CRT_NAME_CONCAT2 = @compileError("unable to translate macro: undefined identifier `_s`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_secapi.h:41:9
pub const __CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_MEMORY_0_3_ = @compileError("unable to translate C expr: unexpected token '__cdecl'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw_secapi.h:69:9
pub const __LONG32 = c_long;
pub const __MINGW_IMPORT = @compileError("unable to translate macro: undefined identifier `__dllimport__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:44:12
pub const __USE_CRTIMP = @as(c_int, 1);
pub const _CRTIMP = @compileError("unable to translate macro: undefined identifier `__dllimport__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:52:15
pub const __DECLSPEC_SUPPORTED = "";
pub const USE___UUIDOF = @as(c_int, 0);
pub const _inline = @compileError("unable to translate C expr: unexpected token '__inline'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:74:9
pub const __CRT_INLINE = @compileError("unable to translate macro: undefined identifier `__gnu_inline__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:83:11
pub const __MINGW_INTRIN_INLINE = @compileError("unable to translate macro: undefined identifier `__always_inline__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:90:9
pub const __MINGW_CXX11_CONSTEXPR = "";
pub const __MINGW_CXX14_CONSTEXPR = "";
pub const __UNUSED_PARAM = @compileError("unable to translate macro: undefined identifier `__unused__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:118:11
pub const __restrict_arr = @compileError("unable to translate C expr: unexpected token '__restrict'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:133:10
pub const __MINGW_ATTRIB_NORETURN = @compileError("unable to translate macro: undefined identifier `__noreturn__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:149:9
pub const __MINGW_ATTRIB_CONST = @compileError("unable to translate C expr: unexpected token '__attribute__'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:150:9
pub const __MINGW_ATTRIB_MALLOC = @compileError("unable to translate macro: undefined identifier `__malloc__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:160:9
pub const __MINGW_ATTRIB_PURE = @compileError("unable to translate macro: undefined identifier `__pure__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:161:9
pub const __MINGW_ATTRIB_NONNULL = @compileError("unable to translate macro: undefined identifier `__nonnull__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:174:9
pub const __MINGW_ATTRIB_UNUSED = @compileError("unable to translate macro: undefined identifier `__unused__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:180:9
pub const __MINGW_ATTRIB_USED = @compileError("unable to translate macro: undefined identifier `__used__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:186:9
pub const __MINGW_ATTRIB_DEPRECATED = @compileError("unable to translate macro: undefined identifier `__deprecated__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:187:9
pub const __MINGW_ATTRIB_DEPRECATED_MSG = @compileError("unable to translate macro: undefined identifier `__deprecated__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:189:9
pub const __MINGW_NOTHROW = @compileError("unable to translate macro: undefined identifier `__nothrow__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:204:9
pub const __MINGW_ATTRIB_NO_OPTIMIZE = @compileError("unable to translate macro: undefined identifier `__optimize__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:212:9
pub const __MINGW_PRAGMA_PARAM = @compileError("unable to translate macro: undefined identifier `_Pragma`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:218:9
pub const __MINGW_BROKEN_INTERFACE = @compileError("unable to translate macro: undefined identifier `message`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:225:9
pub const _UCRT = "";
pub inline fn __MINGW_UCRT_ASM_CALL(func: anytype) @TypeOf(__MINGW_ASM_CALL(func)) {
    _ = &func;
    return __MINGW_ASM_CALL(func);
}
pub const _INT128_DEFINED = "";
pub const __int8 = u8;
pub const __int16 = c_short;
pub const __int32 = c_int;
pub const __int64 = c_longlong;
pub const __ptr32 = "";
pub const __ptr64 = "";
pub const __unaligned = "";
pub const __w64 = "";
pub const __forceinline = @compileError("unable to translate macro: undefined identifier `__always_inline__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:290:9
pub const __nothrow = "";
pub const _INC_VADEFS = "";
pub const __GNUC_VA_LIST = "";
pub const _VA_LIST_DEFINED = "";
pub inline fn _ADDRESSOF(v: anytype) @TypeOf(&v) {
    _ = &v;
    return &v;
}
pub const _crt_va_start = @compileError("unable to translate macro: undefined identifier `__builtin_va_start`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\vadefs.h:48:9
pub const _crt_va_arg = @compileError("unable to translate macro: undefined identifier `__builtin_va_arg`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\vadefs.h:49:9
pub const _crt_va_end = @compileError("unable to translate macro: undefined identifier `__builtin_va_end`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\vadefs.h:50:9
pub const _crt_va_copy = @compileError("unable to translate macro: undefined identifier `__builtin_va_copy`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\vadefs.h:51:9
pub const __CRT_STRINGIZE = @compileError("unable to translate C expr: unexpected token ''");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:309:9
pub inline fn _CRT_STRINGIZE(_Value: anytype) @TypeOf(__CRT_STRINGIZE(_Value)) {
    _ = &_Value;
    return __CRT_STRINGIZE(_Value);
}
pub const __CRT_WIDE = @compileError("unable to translate macro: undefined identifier `L`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:314:9
pub inline fn _CRT_WIDE(_String: anytype) @TypeOf(__CRT_WIDE(_String)) {
    _ = &_String;
    return __CRT_WIDE(_String);
}
pub const _W64 = "";
pub const _CRTIMP_NOIA64 = _CRTIMP;
pub const _CRTIMP2 = _CRTIMP;
pub const _CRTIMP_ALTERNATIVE = _CRTIMP;
pub const _CRT_ALTERNATIVE_IMPORTED = "";
pub const _MRTIMP2 = _CRTIMP;
pub const _DLL = "";
pub const _MT = "";
pub const _MCRTIMP = _CRTIMP;
pub const _CRTIMP_PURE = _CRTIMP;
pub const _PGLOBAL = "";
pub const _AGLOBAL = "";
pub const _SECURECRT_FILL_BUFFER_PATTERN = @as(c_int, 0xFD);
pub const _CRT_DEPRECATE_TEXT = @compileError("unable to translate macro: undefined identifier `deprecated`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:373:9
pub inline fn _CRT_INSECURE_DEPRECATE_MEMORY(_Replacement: anytype) void {
    _ = &_Replacement;
    return;
}
pub inline fn _CRT_INSECURE_DEPRECATE_GLOBALS(_Replacement: anytype) void {
    _ = &_Replacement;
    return;
}
pub const _CRT_MANAGED_HEAP_DEPRECATE = "";
pub inline fn _CRT_OBSOLETE(_NewItem: anytype) void {
    _ = &_NewItem;
    return;
}
pub const _CONST_RETURN = "";
pub const UNALIGNED = "";
pub const _CRT_ALIGN = @compileError("unable to translate macro: undefined identifier `__aligned__`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:415:9
pub const __CRTDECL = @compileError("unable to translate C expr: unexpected token '__cdecl'");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:422:9
pub const _ARGMAX = @as(c_int, 100);
pub const _TRUNCATE = __helpers.cast(usize, -@as(c_int, 1));
pub inline fn _CRT_UNUSED(x: anytype) anyopaque {
    _ = &x;
    return __helpers.cast(anyopaque, x);
}
pub const __USE_MINGW_ANSI_STDIO = @as(c_int, 0);
pub const _CRT_glob = @compileError("unable to translate macro: undefined identifier `_dowildcard`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:479:9
pub const __ANONYMOUS_DEFINED = "";
pub const _ANONYMOUS_UNION = __MINGW_EXTENSION;
pub const _ANONYMOUS_STRUCT = __MINGW_EXTENSION;
pub inline fn _UNION_NAME(x: anytype) void {
    _ = &x;
    return;
}
pub inline fn _STRUCT_NAME(x: anytype) void {
    _ = &x;
    return;
}
pub const DUMMYUNIONNAME = "";
pub const DUMMYUNIONNAME1 = "";
pub const DUMMYUNIONNAME2 = "";
pub const DUMMYUNIONNAME3 = "";
pub const DUMMYUNIONNAME4 = "";
pub const DUMMYUNIONNAME5 = "";
pub const DUMMYUNIONNAME6 = "";
pub const DUMMYUNIONNAME7 = "";
pub const DUMMYUNIONNAME8 = "";
pub const DUMMYUNIONNAME9 = "";
pub const DUMMYSTRUCTNAME = "";
pub const DUMMYSTRUCTNAME1 = "";
pub const DUMMYSTRUCTNAME2 = "";
pub const DUMMYSTRUCTNAME3 = "";
pub const DUMMYSTRUCTNAME4 = "";
pub const DUMMYSTRUCTNAME5 = "";
pub inline fn __CRT_UUID_DECL(@"type": anytype, l: anytype, w1: anytype, w2: anytype, b1: anytype, b2: anytype, b3: anytype, b4: anytype, b5: anytype, b6: anytype, b7: anytype, b8: anytype) void {
    _ = &@"type";
    _ = &l;
    _ = &w1;
    _ = &w2;
    _ = &b1;
    _ = &b2;
    _ = &b3;
    _ = &b4;
    _ = &b5;
    _ = &b6;
    _ = &b7;
    _ = &b8;
    return;
}
pub const __MINGW_DEBUGBREAK_IMPL = @compileError("unable to translate macro: undefined identifier `__debugbreak`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:599:9
pub const __MINGW_FASTFAIL_IMPL = @compileError("unable to translate macro: undefined identifier `__fastfail`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:620:9
pub const __MINGW_PREFETCH_IMPL = @compileError("unable to translate macro: undefined identifier `__prefetch`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\_mingw.h:644:9
pub const __STDC_SECURE_LIB__ = @as(c_long, 200411);
pub const __GOT_SECURE_LIB__ = __STDC_SECURE_LIB__;
pub const _CRT_PACKING = @as(c_int, 8);
pub const _CRTNOALIAS = "";
pub const _CRTRESTRICT = "";
pub const _SIZE_T_DEFINED = "";
pub const _SSIZE_T_DEFINED = "";
pub const _RSIZE_T_DEFINED = "";
pub const _INTPTR_T_DEFINED = "";
pub const __intptr_t_defined = "";
pub const _UINTPTR_T_DEFINED = "";
pub const __uintptr_t_defined = "";
pub const _PTRDIFF_T_DEFINED = "";
pub const _PTRDIFF_T_ = "";
pub const _WCHAR_T_DEFINED = "";
pub const _WCTYPE_T_DEFINED = "";
pub const _WINT_T = "";
pub const _ERRCODE_DEFINED = "";
pub const _TIME32_T_DEFINED = "";
pub const _TIME64_T_DEFINED = "";
pub const _TIME_T_DEFINED = "";
pub const _CRT_SECURE_CPP_NOTHROW = @compileError("unable to translate macro: undefined identifier `throw`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:143:9
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_0(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_1(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_2(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_3(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype, __type3: anytype, __arg3: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    _ = &__type3;
    _ = &__arg3;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_4(__ret: anytype, __func: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype, __type3: anytype, __arg3: anytype, __type4: anytype, __arg4: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    _ = &__type3;
    _ = &__arg3;
    _ = &__type4;
    _ = &__arg4;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_1_1(__ret: anytype, __func: anytype, __type0: anytype, __arg0: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__type0;
    _ = &__arg0;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_1_2(__ret: anytype, __func: anytype, __type0: anytype, __arg0: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__type0;
    _ = &__arg0;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_1_3(__ret: anytype, __func: anytype, __type0: anytype, __arg0: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype, __type3: anytype, __arg3: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__type0;
    _ = &__arg0;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    _ = &__type3;
    _ = &__arg3;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_2_0(__ret: anytype, __func: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype, __dsttype: anytype, __dst: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    _ = &__dsttype;
    _ = &__dst;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_1_ARGLIST(__ret: anytype, __func: anytype, __vfunc: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__vfunc;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_0_2_ARGLIST(__ret: anytype, __func: anytype, __vfunc: anytype, __dsttype: anytype, __dst: anytype, __type1: anytype, __arg1: anytype, __type2: anytype, __arg2: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__vfunc;
    _ = &__dsttype;
    _ = &__dst;
    _ = &__type1;
    _ = &__arg1;
    _ = &__type2;
    _ = &__arg2;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_SECURE_FUNC_SPLITPATH(__ret: anytype, __func: anytype, __dsttype: anytype, __src: anytype) void {
    _ = &__ret;
    _ = &__func;
    _ = &__dsttype;
    _ = &__src;
    return;
}
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_0 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:284:9
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_1 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:286:9
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_2 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:288:9
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_3 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:290:9
pub const __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_4 = @compileError("unable to translate macro: undefined identifier `__func_name`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\libc\include\any-windows-any\corecrt.h:292:9
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_0_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_1_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype, __arg1_type: anytype, __arg1: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    _ = &__arg1_type;
    _ = &__arg1;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_2_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype, __arg1_type: anytype, __arg1: anytype, __arg2_type: anytype, __arg2: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    _ = &__arg1_type;
    _ = &__arg1;
    _ = &__arg2_type;
    _ = &__arg2;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_3_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype, __arg1_type: anytype, __arg1: anytype, __arg2_type: anytype, __arg2: anytype, __arg3_type: anytype, __arg3: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    _ = &__arg1_type;
    _ = &__arg1;
    _ = &__arg2_type;
    _ = &__arg2;
    _ = &__arg3_type;
    _ = &__arg3;
    return;
}
pub inline fn __DEFINE_CPP_OVERLOAD_STANDARD_FUNC_0_4_EX(__ret_type: anytype, __ret_policy: anytype, __decl_spec: anytype, __name: anytype, __sec_name: anytype, __dst_attr: anytype, __dst_type: anytype, __dst: anytype, __arg1_type: anytype, __arg1: anytype, __arg2_type: anytype, __arg2: anytype, __arg3_type: anytype, __arg3: anytype, __arg4_type: anytype, __arg4: anytype) void {
    _ = &__ret_type;
    _ = &__ret_policy;
    _ = &__decl_spec;
    _ = &__name;
    _ = &__sec_name;
    _ = &__dst_attr;
    _ = &__dst_type;
    _ = &__dst;
    _ = &__arg1_type;
    _ = &__arg1;
    _ = &__arg2_type;
    _ = &__arg2;
    _ = &__arg3_type;
    _ = &__arg3;
    _ = &__arg4_type;
    _ = &__arg4;
    return;
}
pub const _TAGLC_ID_DEFINED = "";
pub inline fn __crt_typefix(ctype: anytype) void {
    _ = &ctype;
    return;
}
pub const _CRT_USE_WINAPI_FAMILY_DESKTOP_APP = "";
pub const __need_wint_t = "";
pub const __need_wchar_t = "";
pub const __STDC_VERSION_STDDEF_H__ = @as(c_long, 202311);
pub const NULL = __helpers.cast(?*anyopaque, @as(c_int, 0));
pub const offsetof = @compileError("unable to translate macro: undefined identifier `__builtin_offsetof`");
// D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\lib\compiler\aro\include\stddef.h:18:9
pub const INT8_MIN = -@as(c_int, 128);
pub const INT16_MIN = -__helpers.promoteIntLiteral(c_int, 32768, .decimal);
pub const INT32_MIN = -__helpers.promoteIntLiteral(c_int, 2147483647, .decimal) - @as(c_int, 1);
pub const INT64_MIN = -@as(c_longlong, 9223372036854775807) - @as(c_int, 1);
pub const INT8_MAX = @as(c_int, 127);
pub const INT16_MAX = @as(c_int, 32767);
pub const INT32_MAX = __helpers.promoteIntLiteral(c_int, 2147483647, .decimal);
pub const INT64_MAX = @as(c_longlong, 9223372036854775807);
pub const UINT8_MAX = @as(c_int, 255);
pub const UINT16_MAX = __helpers.promoteIntLiteral(c_int, 65535, .decimal);
pub const UINT32_MAX = __helpers.promoteIntLiteral(c_uint, 0xffffffff, .hex);
pub const UINT64_MAX = @as(c_ulonglong, 0xffffffffffffffff);
pub const INT_LEAST8_MIN = INT8_MIN;
pub const INT_LEAST16_MIN = INT16_MIN;
pub const INT_LEAST32_MIN = INT32_MIN;
pub const INT_LEAST64_MIN = INT64_MIN;
pub const INT_LEAST8_MAX = INT8_MAX;
pub const INT_LEAST16_MAX = INT16_MAX;
pub const INT_LEAST32_MAX = INT32_MAX;
pub const INT_LEAST64_MAX = INT64_MAX;
pub const UINT_LEAST8_MAX = UINT8_MAX;
pub const UINT_LEAST16_MAX = UINT16_MAX;
pub const UINT_LEAST32_MAX = UINT32_MAX;
pub const UINT_LEAST64_MAX = UINT64_MAX;
pub const INT_FAST8_MIN = INT8_MIN;
pub const INT_FAST16_MIN = INT16_MIN;
pub const INT_FAST32_MIN = INT32_MIN;
pub const INT_FAST64_MIN = INT64_MIN;
pub const INT_FAST8_MAX = INT8_MAX;
pub const INT_FAST16_MAX = INT16_MAX;
pub const INT_FAST32_MAX = INT32_MAX;
pub const INT_FAST64_MAX = INT64_MAX;
pub const UINT_FAST8_MAX = UINT8_MAX;
pub const UINT_FAST16_MAX = UINT16_MAX;
pub const UINT_FAST32_MAX = UINT32_MAX;
pub const UINT_FAST64_MAX = UINT64_MAX;
pub const INTPTR_MIN = INT64_MIN;
pub const INTPTR_MAX = INT64_MAX;
pub const UINTPTR_MAX = UINT64_MAX;
pub const INTMAX_MIN = INT64_MIN;
pub const INTMAX_MAX = INT64_MAX;
pub const UINTMAX_MAX = UINT64_MAX;
pub const PTRDIFF_MIN = INT64_MIN;
pub const PTRDIFF_MAX = INT64_MAX;
pub const SIG_ATOMIC_MIN = INT32_MIN;
pub const SIG_ATOMIC_MAX = INT32_MAX;
pub const SIZE_MAX = UINT64_MAX;
pub const WCHAR_MIN = @as(c_uint, 0);
pub const WCHAR_MAX = @as(c_uint, 0xffff);
pub const WINT_MIN = @as(c_uint, 0);
pub const WINT_MAX = @as(c_uint, 0xffff);
pub inline fn INT8_C(val: anytype) @TypeOf((INT_LEAST8_MAX - INT_LEAST8_MAX) + val) {
    _ = &val;
    return (INT_LEAST8_MAX - INT_LEAST8_MAX) + val;
}
pub inline fn INT16_C(val: anytype) @TypeOf((INT_LEAST16_MAX - INT_LEAST16_MAX) + val) {
    _ = &val;
    return (INT_LEAST16_MAX - INT_LEAST16_MAX) + val;
}
pub inline fn INT32_C(val: anytype) @TypeOf((INT_LEAST32_MAX - INT_LEAST32_MAX) + val) {
    _ = &val;
    return (INT_LEAST32_MAX - INT_LEAST32_MAX) + val;
}
pub const INT64_C = __helpers.LL_SUFFIX;
pub inline fn UINT8_C(val: anytype) @TypeOf(val) {
    _ = &val;
    return val;
}
pub inline fn UINT16_C(val: anytype) @TypeOf(val) {
    _ = &val;
    return val;
}
pub const UINT32_C = __helpers.U_SUFFIX;
pub const UINT64_C = __helpers.ULL_SUFFIX;
pub const INTMAX_C = __helpers.LL_SUFFIX;
pub const UINTMAX_C = __helpers.ULL_SUFFIX;
pub const @"bool" = bool;
pub const @"true" = @as(c_int, 1);
pub const @"false" = @as(c_int, 0);
pub const __bool_true_false_are_defined = @as(c_int, 1);
pub const SDL_INIT_AUDIO = @as(c_uint, 0x00000010);
pub const SDL_INIT_VIDEO = @as(c_uint, 0x00000020);
pub const SDL_WINDOW_FULLSCREEN = @as(c_uint, 0x00000001);
pub const SDL_WINDOW_HIDDEN = @as(c_uint, 0x00000008);
pub const SDL_WINDOW_RESIZABLE = @as(c_uint, 0x00000020);
pub const SDL_WINDOW_MINIMIZED = @as(c_uint, 0x00000040);
pub const SDL_WINDOW_INPUT_FOCUS = @as(c_uint, 0x00000200);
pub const SDL_WINDOW_HIGH_PIXEL_DENSITY = @as(c_uint, 0x00002000);
pub const SDL_WINDOW_OCCLUDED = @as(c_uint, 0x00004000);
pub const SDL_PIXELFORMAT_ABGR8888 = __helpers.promoteIntLiteral(c_uint, 0x16762004, .hex);
pub const SDL_TEXTUREACCESS_STATIC = @as(c_int, 0);
pub const SDL_TEXTUREACCESS_STREAMING = @as(c_int, 1);
pub const SDL_TEXTUREACCESS_TARGET = @as(c_int, 2);
pub const SDL_SCALEMODE_NEAREST = @as(c_int, 0);
pub const SDL_SCALEMODE_LINEAR = @as(c_int, 1);
pub const SDL_SCALEMODE_PIXELART = @as(c_int, 0);
pub const SDL_FLIP_NONE = @as(c_int, 0);
pub const SDL_FLIP_HORIZONTAL = @as(c_int, 1);
pub const SDL_LOGICAL_PRESENTATION_DISABLED = @as(c_int, 0);
pub const SDL_LOGICAL_PRESENTATION_STRETCH = @as(c_int, 1);
pub const SDL_LOGICAL_PRESENTATION_LETTERBOX = @as(c_int, 2);
pub const SDL_LOGICAL_PRESENTATION_INTEGER_SCALE = @as(c_int, 4);
pub const SDL_BLENDMODE_NONE = @as(c_uint, 0x00000000);
pub const SDL_BLENDMODE_BLEND = @as(c_uint, 0x00000001);
pub const SDL_BLENDMODE_ADD = @as(c_uint, 0x00000002);
pub const SDL_BLENDOPERATION_ADD = @as(c_int, 0x1);
pub const SDL_BLENDFACTOR_ZERO = @as(c_int, 0x1);
pub const SDL_BLENDFACTOR_ONE = @as(c_int, 0x2);
pub const SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR = @as(c_int, 0x4);
pub const SDL_BLENDFACTOR_DST_COLOR = @as(c_int, 0x9);
pub const SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK = __helpers.promoteIntLiteral(c_uint, 0xFFFFFFFF, .hex);
pub const SDL_AUDIO_F32 = @as(c_uint, 0x8120);
pub const SDL_MESSAGEBOX_ERROR = @as(c_uint, 0x00000010);
pub const SDL_BUTTON_LEFT = @as(c_int, 1);
pub const SDL_BUTTON_RIGHT = @as(c_int, 3);
pub const SDL_KMOD_LSHIFT = @as(c_uint, 0x0001);
pub const SDL_KMOD_RSHIFT = @as(c_uint, 0x0002);
pub const SDL_KMOD_SHIFT = SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT;
pub const SDL_KMOD_LCTRL = @as(c_uint, 0x0040);
pub const SDL_KMOD_RCTRL = @as(c_uint, 0x0080);
pub const SDL_KMOD_CTRL = SDL_KMOD_LCTRL | SDL_KMOD_RCTRL;
pub const SDL_KMOD_LALT = @as(c_uint, 0x0100);
pub const SDL_KMOD_RALT = @as(c_uint, 0x0200);
pub const SDL_KMOD_ALT = SDL_KMOD_LALT | SDL_KMOD_RALT;
pub const SDL_KMOD_LGUI = @as(c_uint, 0x0400);
pub const SDL_KMOD_RGUI = @as(c_uint, 0x0800);
pub const SDL_KMOD_GUI = SDL_KMOD_LGUI | SDL_KMOD_RGUI;
pub const SDL_SCANCODE_RIGHT = @as(c_int, 79);
pub const SDL_SCANCODE_LEFT = @as(c_int, 80);
pub const SDL_SCANCODE_DOWN = @as(c_int, 81);
pub const SDL_SCANCODE_UP = @as(c_int, 82);
pub const SDLK_BACKSPACE = @as(c_int, 8);
pub const SDLK_TAB = @as(c_int, 9);
pub const SDLK_RETURN = @as(c_int, 13);
pub const SDLK_ESCAPE = @as(c_int, 27);
pub const SDLK_SPACE = @as(c_int, 32);
pub const SDLK_MINUS = '-';
pub const SDLK_EQUALS = '=';
pub const SDLK_1 = '1';
pub const SDLK_2 = '2';
pub const SDLK_3 = '3';
pub const SDLK_4 = '4';
pub const SDLK_5 = '5';
pub const SDLK_6 = '6';
pub const SDLK_LEFTBRACKET = '[';
pub const SDLK_RIGHTBRACKET = ']';
pub const SDLK_GRAVE = '`';
pub const SDLK_A = 'a';
pub const SDLK_D = 'd';
pub const SDLK_E = 'e';
pub const SDLK_F = 'f';
pub const SDLK_G = 'g';
pub const SDLK_H = 'h';
pub const SDLK_I = 'i';
pub const SDLK_K = 'k';
pub const SDLK_L = 'l';
pub const SDLK_M = 'm';
pub const SDLK_N = 'n';
pub const SDLK_O = 'o';
pub const SDLK_P = 'p';
pub const SDLK_Q = 'q';
pub const SDLK_R = 'r';
pub const SDLK_S = 's';
pub const SDLK_W = 'w';
pub const SDLK_X = 'x';
pub const SDLK_Y = 'y';
pub const SDLK_F1 = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 58);
pub const SDLK_F2 = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 59);
pub const SDLK_F10 = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 67);
pub const SDLK_F11 = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 68);
pub const SDLK_HOME = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 74);
pub const SDLK_PAGEUP = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 75);
pub const SDLK_END = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 77);
pub const SDLK_PAGEDOWN = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 78);
pub const SDLK_RIGHT = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 79);
pub const SDLK_LEFT = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 80);
pub const SDLK_DOWN = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 81);
pub const SDLK_UP = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 82);
pub const SDLK_KP_ENTER = __helpers.promoteIntLiteral(c_uint, 0x40000000, .hex) | @as(c_uint, 88);
pub const SDL_EVENT_QUIT = @as(c_uint, 0x100);
pub const SDL_EVENT_WINDOW_RESIZED = @as(c_uint, 0x206);
pub const SDL_EVENT_WINDOW_MOUSE_LEAVE = @as(c_uint, 0x20D);
pub const SDL_EVENT_WINDOW_CLOSE_REQUESTED = @as(c_uint, 0x210);
pub const SDL_EVENT_KEY_DOWN = @as(c_uint, 0x300);
pub const SDL_EVENT_KEY_UP = @as(c_uint, 0x301);
pub const SDL_EVENT_TEXT_INPUT = @as(c_uint, 0x303);
pub const SDL_EVENT_MOUSE_MOTION = @as(c_uint, 0x400);
pub const SDL_EVENT_MOUSE_BUTTON_DOWN = @as(c_uint, 0x401);
pub const SDL_EVENT_MOUSE_BUTTON_UP = @as(c_uint, 0x402);
pub const SDL_EVENT_MOUSE_WHEEL = @as(c_uint, 0x403);
pub const SDL_Init = og_sdl_Init;
pub const SDL_InitSubSystem = og_sdl_InitSubSystem;
pub const SDL_QuitSubSystem = og_sdl_QuitSubSystem;
pub const SDL_Quit = og_sdl_Quit;
pub const SDL_GetError = og_sdl_GetError;
pub const SDL_free = og_sdl_free;
pub const SDL_CreateWindowAndRenderer = og_sdl_CreateWindowAndRenderer;
pub const SDL_DestroyWindow = og_sdl_DestroyWindow;
pub const SDL_DestroyRenderer = og_sdl_DestroyRenderer;
pub const SDL_GetWindowID = og_sdl_GetWindowID;
pub const SDL_GetWindowFlags = og_sdl_GetWindowFlags;
pub const SDL_GetWindowSize = og_sdl_GetWindowSize;
pub const SDL_GetWindowPixelDensity = og_sdl_GetWindowPixelDensity;
pub const SDL_SetWindowMinimumSize = og_sdl_SetWindowMinimumSize;
pub const SDL_SetWindowFullscreen = og_sdl_SetWindowFullscreen;
pub const SDL_SetWindowIcon = og_sdl_SetWindowIcon;
pub const SDL_ShowSimpleMessageBox = og_sdl_ShowSimpleMessageBox;
pub const SDL_ShowOpenFolderDialog = og_sdl_ShowOpenFolderDialog;
pub const SDL_GetPreferredLocales = og_sdl_GetPreferredLocales;
pub const SDL_SetRenderVSync = og_sdl_SetRenderVSync;
pub const SDL_GetRenderOutputSize = og_sdl_GetRenderOutputSize;
pub const SDL_SetRenderLogicalPresentation = og_sdl_SetRenderLogicalPresentation;
pub const SDL_SetRenderScale = og_sdl_SetRenderScale;
pub const SDL_SetRenderViewport = og_sdl_SetRenderViewport;
pub const SDL_SetRenderTarget = og_sdl_SetRenderTarget;
pub const SDL_GetRenderTarget = og_sdl_GetRenderTarget;
pub const SDL_SetRenderDrawBlendMode = og_sdl_SetRenderDrawBlendMode;
pub const SDL_SetRenderDrawColor = og_sdl_SetRenderDrawColor;
pub const SDL_SetRenderDrawColorFloat = og_sdl_SetRenderDrawColorFloat;
pub const SDL_RenderClear = og_sdl_RenderClear;
pub const SDL_RenderFillRect = og_sdl_RenderFillRect;
pub const SDL_RenderFillRects = og_sdl_RenderFillRects;
pub const SDL_SetRenderClipRect = og_sdl_SetRenderClipRect;
pub const SDL_GetRectIntersection = og_sdl_GetRectIntersection;
pub const SDL_RenderLines = og_sdl_RenderLines;
pub const SDL_RenderTexture = og_sdl_RenderTexture;
pub const SDL_RenderTextureRotated = og_sdl_RenderTextureRotated;
pub const SDL_RenderTexture9Grid = og_sdl_RenderTexture9Grid;
pub const SDL_RenderGeometry = og_sdl_RenderGeometry;
pub const SDL_RenderDebugText = og_sdl_RenderDebugText;
pub const SDL_RenderPresent = og_sdl_RenderPresent;
pub const SDL_RenderReadPixels = og_sdl_RenderReadPixels;
pub const SDL_RenderCoordinatesToWindow = og_sdl_RenderCoordinatesToWindow;
pub const SDL_ConvertEventToRenderCoordinates = og_sdl_ConvertEventToRenderCoordinates;
pub const SDL_CreateTexture = og_sdl_CreateTexture;
pub const SDL_CreateTextureFromSurface = og_sdl_CreateTextureFromSurface;
pub const SDL_DestroyTexture = og_sdl_DestroyTexture;
pub const SDL_GetTextureSize = og_sdl_GetTextureSize;
pub const SDL_UpdateTexture = og_sdl_UpdateTexture;
pub const SDL_LockTexture = og_sdl_LockTexture;
pub const SDL_UnlockTexture = og_sdl_UnlockTexture;
pub const SDL_SetTextureBlendMode = og_sdl_SetTextureBlendMode;
pub const SDL_GetTextureBlendMode = og_sdl_GetTextureBlendMode;
pub const SDL_SetTextureScaleMode = og_sdl_SetTextureScaleMode;
pub const SDL_SetTextureColorMod = og_sdl_SetTextureColorMod;
pub const SDL_GetTextureColorMod = og_sdl_GetTextureColorMod;
pub const SDL_SetTextureColorModFloat = og_sdl_SetTextureColorModFloat;
pub const SDL_SetTextureAlphaMod = og_sdl_SetTextureAlphaMod;
pub const SDL_SetTextureAlphaModFloat = og_sdl_SetTextureAlphaModFloat;
pub const SDL_GetTextureAlphaModFloat = og_sdl_GetTextureAlphaModFloat;
pub const SDL_ComposeCustomBlendMode = og_sdl_ComposeCustomBlendMode;
pub const SDL_CreateSurfaceFrom = og_sdl_CreateSurfaceFrom;
pub const SDL_ConvertSurface = og_sdl_ConvertSurface;
pub const SDL_DestroySurface = og_sdl_DestroySurface;
pub const SDL_PollEvent = og_sdl_PollEvent;
pub const SDL_WaitEventTimeout = og_sdl_WaitEventTimeout;
pub const SDL_PushEvent = og_sdl_PushEvent;
pub const SDL_GetKeyboardState = og_sdl_GetKeyboardState;
pub const SDL_GetModState = og_sdl_GetModState;
pub const SDL_GetKeyFromName = og_sdl_GetKeyFromName;
pub const SDL_StartTextInput = og_sdl_StartTextInput;
pub const SDL_StopTextInput = og_sdl_StopTextInput;
pub const SDL_TextInputActive = og_sdl_TextInputActive;
pub const SDL_HideCursor = og_sdl_HideCursor;
pub const SDL_CursorVisible = og_sdl_CursorVisible;
pub const SDL_GetTicks = og_sdl_GetTicks;
pub const SDL_GetTicksNS = og_sdl_GetTicksNS;
pub const SDL_GetPerformanceCounter = og_sdl_GetPerformanceCounter;
pub const SDL_GetPerformanceFrequency = og_sdl_GetPerformanceFrequency;
pub const SDL_DelayPrecise = og_sdl_DelayPrecise;
pub const SDL_OpenAudioDeviceStream = og_sdl_OpenAudioDeviceStream;
pub const SDL_ResumeAudioStreamDevice = og_sdl_ResumeAudioStreamDevice;
pub const SDL_PauseAudioStreamDevice = og_sdl_PauseAudioStreamDevice;
pub const SDL_GetAudioStreamQueued = og_sdl_GetAudioStreamQueued;
pub const SDL_PutAudioStreamData = og_sdl_PutAudioStreamData;
pub const SDL_DestroyAudioStream = og_sdl_DestroyAudioStream;
pub const threadlocaleinfostruct = struct_threadlocaleinfostruct;
pub const threadmbcinfostruct = struct_threadmbcinfostruct;
pub const __lc_time_data = struct___lc_time_data;
pub const localeinfo_struct = struct_localeinfo_struct;
pub const tagLC_ID = struct_tagLC_ID;
