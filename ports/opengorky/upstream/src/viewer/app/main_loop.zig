//! Viewer main loop: input script, events, background pause, world tick,
//! drawing, gamma, present and frame cap.

const std = @import("std");
const loading = @import("loading.zig");
const viewer = @import("viewer.zig");
const og = @import("opengorky");
const c = @import("sdl");
const Viewer = viewer.Viewer;
const events = @import("events.zig");
const input_ctx = @import("input_ctx.zig");
const frame = @import("frame.zig");
const update = @import("update.zig");
const pause = @import("pause.zig");
const render = @import("render.zig");
const perf = @import("perf.zig");
const InputScript = @import("input_script.zig").InputScript;
const AudioOut = @import("../media/audio.zig").AudioOut;

pub fn run(v: *Viewer, ctx: *input_ctx.Ctx, script: *?InputScript, audio_out: *?AudioOut, quit_at: ?u64, logos_only: bool) !void {
    var video_pacer: VideoPacer = .{};
    while (!logos_only or v.video != null) {
        const frame_start = c.SDL_GetTicksNS();
        if (quit_at) |t| if (c.SDL_GetTicks() >= t) return;
        if (script.*) |*sc| if (try sc.step(v, ctx.window, ctx.renderer) == .quit) return;
        var ev: c.SDL_Event = undefined;
        var had_event = false;
        while (!loading.levelPending(v) and !(v.pending_new_game and v.video == null) and (!logos_only or v.video != null) and c.SDL_PollEvent(&ev)) {
            had_event = true;
            if (try events.handle(v, ctx, ev) == .quit) {
                v.quit = true;
                return;
            }
        }
        if (v.quit or (logos_only and v.video == null)) return;
        // Save name entry uses SDL text input (the console enables its own).
        if (v.menu) |m| if (!v.console.open) {
            const want = m.open and m.editing != null;
            if (want != c.SDL_TextInputActive(ctx.window)) {
                _ = if (want) c.SDL_StartTextInput(ctx.window) else c.SDL_StopTextInput(ctx.window);
            }
        };
        // Videos play even with the menu open (intro videos before the main menu).
        // Unfocused: `fps_background` cap (no frame if also minimised/occluded), and
        // with `pause_background` the world and audio stop (update.hold re-anchors
        // the world clock); focused: `fps_limit`. --input,
        // --frames and shots run uncapped and unpaused.
        const flags = c.SDL_GetWindowFlags(ctx.window);
        const free_run = v.input_mode or ctx.opts.frames > 0 or ctx.opts.shot != null;
        const always_on = free_run or ctx.opts.active;
        const unfocused = !always_on and flags & c.SDL_WINDOW_INPUT_FOCUS == 0;
        const bg = pause.state(v.cfg.pause_background, !unfocused, flags & (c.SDL_WINDOW_MINIMIZED | c.SDL_WINDOW_OCCLUDED | c.SDL_WINDOW_HIDDEN) != 0);
        const unseen = bg.unseen;
        const paused = bg.paused;
        if (paused and v.pause_start == null) {
            v.pause_start = c.SDL_GetTicks();
            if (audio_out.*) |*a| a.setPaused(true);
        } else if (!paused) if (v.pause_start) |ps| {
            // Video audio was paused with the device, so its clock is too.
            if (v.video) |vd| if (vd.started) {
                vd.start_ms +%= c.SDL_GetTicks() -% ps;
            };
            v.pause_start = null;
            if (audio_out.*) |*a| a.setPaused(false);
        };
        const pt_update = perf.now();
        if (!paused and v.first_level_frame) update.queueWorldVideos(v);
        if (!paused and !loading.levelPending(v) and (v.video != null or v.menu == null or !v.menu.?.open)) update.update(v) else update.hold(v);
        if (logos_only and v.video == null) return;
        if (v.pending_new_game and v.video == null) {
            try @import("flow.zig").enterNewGame(v);
            update.queueWorldVideos(v);
        }
        if (!paused) {
            if (loading.creditsPending(v)) loading.show(v);
            update.menuMusic(v);
        }
        perf.add(.update, pt_update);
        const fps_cap: u32 = if (free_run) 0 else if (unseen) og.state.world.tick_hz else if (unfocused and v.cfg.fps_background > 0) v.cfg.fps_background else v.cfg.fps_limit;
        if (!paused) if (audio_out.*) |*a| a.pump(ctx.mixer, if (fps_cap > 0) 1000 / fps_cap else 0);
        if (!paused) if (ctx.mix_out) |o| ctx.mixer.mix(try o.addManyAsSlice(v.gpa, 2 * @import("../media/audio.zig").rate / og.state.world.tick_hz));
        if (unseen) {
            video_pacer = .{};
            loading.end();
            limitFrame(frame_start, fps_cap);
            continue;
        }
        // Same frame as on screen (no events, no world tick, camera and mouse
        // still, no animated windows): skip drawing and present; at 60 fps and a
        // 30 Hz world that is every other frame.
        if (!loading.levelPending(v) and !free_run and !had_event and !(if (script.*) |sc| sc.shot != null else false) and !v.show_perf and frame.frameSame(v)) {
            limitFrame(frame_start, if (fps_cap > 0) fps_cap else 120);
            continue;
        }
        // Hide the system cursor (macOS restores it on focus and fullscreen
        // changes); we draw our own.
        if (c.SDL_CursorVisible()) _ = c.SDL_HideCursor();
        const pt_render = perf.now();
        const first_level = loading.levelPending(v);
        if (first_level) loading.show(v);
        frame.beginFrame(v);
        try render.render(v);
        if (first_level) {
            v.level.?.resumeSound();
            v.level.?.updateSound(v.scroll) catch |err| std.log.warn("level sound: {t}", .{err});
            v.level.?.last_tick_ms = 0;
            v.first_level_frame = false;
        }
        frame.applyGamma(v);
        if (v.show_perf) {
            var pb: [256]u8 = undefined;
            viewer.debugText(v.renderer, 2, 2, perf.line(&pb)) catch {};
        }
        perf.add(.rest, pt_render);
        if (script.*) |*sc| if (sc.shot) |path| {
            sc.shot = null;
            v.screenshot(path) catch |err| std.log.warn("shot {s}: {t}", .{ path, err });
        };
        if (v.record_dir) |dir| {
            var pb: [600]u8 = undefined;
            const path = try std.fmt.bufPrint(&pb, "{s}/{d:0>5}.png", .{ dir, v.record_n });
            v.screenshot(path) catch |err| std.log.warn("record {s}: {t}", .{ path, err });
            v.record_n += 1;
        }
        loading.end();
        const pt_present = perf.now();
        try frame.presentFrame(v);
        if (!paused) if (v.video) |vd| vd.start(if (v.sound) |snd| snd.mixer else null);
        if (loading.creditsPending(v)) v.menu.?.credits_start_ms = c.SDL_GetTicks();
        perf.add(.present, pt_present);
        perf.endFrame();
        frame.adaptDetail(v);
        if (@hasDecl(c, "og_xgb35_BeginLoading") and v.video != null and fps_cap > 0 and !paused) {
            const now = c.SDL_GetTicksNS();
            const deadline = video_pacer.next(frame_start, now, fps_cap);
            if (deadline > now) c.SDL_DelayPrecise(deadline - now);
        } else {
            video_pacer = .{};
            limitFrame(frame_start, fps_cap);
        }
    }
}

// Schedule repeated displays of a movie frame without accumulating sleep overshoot.
const VideoPacer = struct {
    deadline: u64 = 0,
    fps: u32 = 0,

    fn next(self: *VideoPacer, start: u64, now: u64, fps: u32) u64 {
        const period = 1_000_000_000 / @as(u64, fps);
        if (self.fps != fps) {
            self.fps = fps;
            self.deadline = start;
        }
        self.deadline += period;
        if (now -| self.deadline >= period) self.deadline = now;
        return self.deadline;
    }
};

test "video pacing absorbs a late frame and reanchors after a long stall" {
    var pacer: VideoPacer = .{};
    const period = 1_000_000_000 / 60;
    try std.testing.expectEqual(period, pacer.next(0, 5_000_000, 60));
    try std.testing.expectEqual(2 * period, pacer.next(period + 800_000, 2 * period + 900_000, 60));
    try std.testing.expectEqual(3 * period, pacer.next(2 * period + 910_000, 2 * period + 4_000_000, 60));
    try std.testing.expectEqual(500_000_000, pacer.next(3 * period, 500_000_000, 60));
}

/// Frame cap: sleeps the rest of the frame since `start` (SDL ns) when `fps` > 0.
fn limitFrame(start: u64, fps: u32) void {
    if (fps == 0) return;
    const period: u64 = 1_000_000_000 / fps;
    const spent = c.SDL_GetTicksNS() -% start;
    if (spent < period) c.SDL_DelayPrecise(period - spent);
}
