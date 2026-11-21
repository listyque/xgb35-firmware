//! Entry into battle (original Gorky-18 executable, 0x41f1a9–0x41f4ea).
const std = @import("std");
const c = @import("sdl");
const og = @import("opengorky");
const check = @import("../media/gfx.zig").check;

const width = 640;
const height = 480;
const zoom_ticks = 8;
const tick_ms = 1000 / og.state.world.tick_hz;

const Row = struct {
    left: i16,
    right: i16,
    left_delay: u1,
    right_delay: u1,

    fn tear(self: *Row, tick: u32) void {
        if (tick > self.left_delay) self.left -= speed(self.left);
        if (tick > self.right_delay) self.right += speed(width - self.right);
    }

    fn speed(distance: i16) i16 {
        if (distance < 64) return 128;
        if (distance < 256) return 64;
        if (distance < 304) return 32;
        return 16;
    }
};

pub const Transition = struct {
    snap: ?*c.SDL_Texture = null,
    ticks: u32 = 0,
    next_ms: u64,
    rows: [height]Row,

    pub fn init(now: u64) Transition {
        var t: Transition = .{ .next_ms = now + tick_ms, .rows = undefined };
        // Visual jitter must not consume the world's RNG.
        var prng = std.Random.DefaultPrng.init(now);
        const random = prng.random();
        for (&t.rows) |*row| {
            const delay = random.int(u1);
            const split = 308 + 4 * @as(i16, random.int(u3));
            row.* = .{ .left = split, .right = split, .left_delay = delay, .right_delay = random.int(u1) };
        }
        return t;
    }

    pub fn sawTick(self: *const Transition) u32 {
        return self.ticks -| zoom_ticks;
    }

    pub fn advance(self: *Transition, now: u64) bool {
        if (now < self.next_ms) return false;
        self.next_ms = @max(self.next_ms + tick_ms, now + 1);
        self.ticks += 1;
        const saw = self.sawTick();
        if (saw > 0 and saw <= 10) for (&self.rows) |*row| row.tear(saw);
        return true;
    }

    pub fn draw(self: *const Transition, r: *c.SDL_Renderer, fw: f32, fh: f32) !void {
        const snap = self.snap.?;
        var tw: f32 = 0;
        var th: f32 = 0;
        try check(c.SDL_GetTextureSize(snap, &tw, &th));
        _ = c.SDL_SetTextureScaleMode(snap, c.SDL_SCALEMODE_NEAREST);
        const crop_w = tw * og.num.toF32(@max(4, width -| (self.ticks + 1) * 16)) / width;
        const crop_x = (tw - crop_w) / 2;
        if (self.sawTick() == 0) return check(c.SDL_RenderTexture(r, snap,
            &.{ .x = crop_x, .y = 0, .w = crop_w, .h = th },
            &.{ .x = 0, .y = 0, .w = fw, .h = fh }));
        const dx = fw / width;
        const dy = fh / height;
        const sx = crop_w / width;
        const sy = th / height;
        for (&self.rows, 0..) |*row, y| {
            const src_y = og.num.toF32(y) * sy;
            const dst_y = og.num.toF32(y) * dy;
            const left = og.num.toF32(@max(0, row.left));
            const right = og.num.toF32(@min(width, row.right));
            if (left > 1) try check(c.SDL_RenderTexture(r, snap,
                &.{ .x = crop_x, .y = src_y, .w = left * sx, .h = sy },
                &.{ .x = 0, .y = dst_y, .w = left * dx, .h = dy }));
            if (right < width) try check(c.SDL_RenderTexture(r, snap,
                &.{ .x = crop_x + right * sx, .y = src_y, .w = (width - right) * sx, .h = sy },
                &.{ .x = right * dx, .y = dst_y, .w = (width - right) * dx, .h = dy }));
        }
    }
};

test "tearing starts near the center, staggers rows, and clears both edges" {
    var t = Transition.init(0);
    try std.testing.expect(!t.advance(tick_ms - 1));
    for (1..zoom_ticks + 1) |tick| try std.testing.expect(t.advance(tick * tick_ms));
    try std.testing.expectEqual(0, t.sawTick());
    for (&t.rows) |*row| {
        try std.testing.expect(row.left >= 308 and row.left <= 336);
        try std.testing.expectEqual(row.left, row.right);
    }
    var row: Row = .{ .left = 320, .right = 320, .left_delay = 0, .right_delay = 1 };
    row.tear(1);
    try std.testing.expectEqual(304, row.left);
    try std.testing.expectEqual(320, row.right);
    for (zoom_ticks + 1..zoom_ticks + 11) |tick| try std.testing.expect(t.advance(tick * tick_ms));
    for (&t.rows) |*r| try std.testing.expect(r.left <= 0 and r.right >= width);
}
