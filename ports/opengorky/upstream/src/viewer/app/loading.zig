//! XGB35 presents the original saw while the viewer prepares the next frame.
const c = @import("sdl");
const gfx = @import("../media/gfx.zig");
const Viewer = @import("viewer.zig").Viewer;
const tick_hz = @import("opengorky").state.world.tick_hz;

pub fn begin(r: *c.SDL_Renderer, frames: gfx.Frames, first: u32) void {
    if (@hasDecl(c, "og_xgb35_BeginLoading") and frames.len() > 0) {
        c.og_xgb35_BeginLoading(r, @ptrCast(frames.textures.items.ptr), frames.len(), tick_hz, first);
    }
}

pub fn show(v: *Viewer) void {
    if (v.shot_run) return;
    if (v.gui) |g| begin(v.renderer, g.spinner, if (v.transition) |tr| tr.sawTick() else 0);
}

pub fn end() void {
    if (@hasDecl(c, "og_xgb35_EndLoading")) c.og_xgb35_EndLoading();
}

pub fn levelPending(v: *const Viewer) bool {
    return v.first_level_frame and v.level != null and v.video == null and (v.menu == null or !v.menu.?.open);
}

pub fn creditsPending(v: *const Viewer) bool {
    const m = v.menu orelse return false;
    return v.video == null and m.open and m.screen == .credits and m.credits_start_ms == null;
}
