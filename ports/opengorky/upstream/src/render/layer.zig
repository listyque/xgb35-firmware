//! Draws 3D actors over an orthographic level background: a software rasterizer
//! into a 640×480 layer with a depth test against the background height map (`.zbf`).

const std = @import("std");
const vec = @import("../vec.zig");
const num = @import("../num.zig");
const Allocator = std.mem.Allocator;
const Image = @import("../image.zig").Image;
const sprite = @import("../formats.zig").sprite;
const backdrop = @import("../core.zig").map;
const skin = @import("../actor.zig").skin;
const camera = @import("../actor.zig").camera;
const model_cache = @import("../actor.zig").model_cache;
const Model = model_cache.Model;
const raster = @import("raster.zig");
const Textures = @import("textures.zig").Textures;

/// Where the background surface depth for actor occlusion comes from.
pub const DepthSource = union(enum) {
    none,
    /// Orthographic levels: `.zbf` is a height map.
    heights: struct { z: backdrop.Heights, cfg: backdrop.OrthoCfg },
    /// Perspective levels: `.zbf` is a W-buffer, depth `far − v·tl/65536` (level.PerspCfg).
    /// `tol`: the background counts as this much farther (our camera's error against
    /// the rendered background, see depth_fit.fitPerspPoints).
    persp: struct { z: backdrop.Heights, far: f32, tl: f32, tol: f32 = 0 },
};

/// Level light (`.lts`): ambient, directional (direction (1, 0.3, 0.7) hard-coded
/// in the exe) and point lamps; defaults 0.3 and 0.5 (0x47f5d0).
const Lighting = struct {
    ambient: [3]f32 = .{ 0.3, 0.3, 0.3 },
    directional: [3]f32 = .{ 0.5, 0.5, 0.5 },
    omni: []const backdrop.Omni = &.{},

    /// Vertex color (0x47e1f0): ambient + lamps (color · cosθ · (1 − d²/range²)) +
    /// directional · max(0, N·D); channels capped at 1. `n` is a unit normal.
    fn color(self: Lighting, pos: [3]f32, n: [3]f32) [3]f32 {
        var c = self.ambient;
        for (self.omni) |o| {
            const l = [3]f32{ o.pos[0] - pos[0], o.pos[1] - pos[1], o.pos[2] - pos[2] };
            const dot = n[0] * l[0] + n[1] * l[1] + n[2] * l[2];
            if (dot <= 0) continue;
            const d2 = l[0] * l[0] + l[1] * l[1] + l[2] * l[2];
            const range2 = o.range * o.range;
            if (d2 > range2) continue;
            const att = 1 - d2 / range2;
            if (att <= 0) continue;
            const k = att * dot / @sqrt(d2);
            for (&c, o.color) |*ch, oc| ch.* += oc * k;
        }
        const dir = comptime vec.normalize(.{ 1, 0.3, 0.7 });
        const nd = n[0] * dir[0] + n[1] * dir[1] + n[2] * dir[2];
        if (nd > 0) for (&c, self.directional) |*ch, dc| {
            ch.* += dc * nd;
        };
        for (&c) |*ch| ch.* = @min(ch.*, 1);
        return c;
    }
};

/// Actor layer for a `w×h` window with top-left `origin` (background pixels).
pub const Layer = struct {
    pub var hardware_shadow: ?*const fn ([]u8, usize, []const [3][2]f32) bool = null;
    canvas: raster.Canvas,
    /// Model textures (the host's; none: untextured).
    textures: ?*Textures = null,
    /// Debug: shadow pixels rejected by the depth test are drawn red.
    shadow_debug: bool = false,
    origin: [2]f32 = .{ 0, 0 },
    /// Actor lighting from `.lts` (0x47f070, 0x47e1f0).
    light: Lighting = .{},
    /// 512×512 shadow mask over the whole layer (0x56f2e8, buffer 0x40d570): 1 — shadowed.
    shadow: []u8,
    /// 8×8 layer tiles (0x54b200): floor height of the first actor whose shadow touched
    /// them (its floor plane is the depth of the tile quad's four corners, 0x419e44).
    shadow_tiles: [shadow_grid * shadow_grid]?f32 = @splat(null),
    /// Mask triangles cast this frame (castShadow, in its arena): compositeShadow
    /// fills them into `shadow` in row bands on the pool (ones only, so order is free).
    shadow_tris: std.ArrayList([3][2]f32) = .empty,
    /// Additive passes (ice, shock, materialization glow): drawn over the layer
    /// additively; `glow_used` — whether anything was drawn.
    glow: Image,
    glow_used: bool = false,
    /// Screen-blended billboards (ONE / INVSRCCOLOR), drawn over the layer with that blend.
    screen: Image,
    screen_used: bool = false,
    /// Screen bounds of the last drawn model (left, top, right, bottom; layer
    /// coordinates) — for the selected-hero frame (+0x64..+0x70, 0x45b920).
    bbox: [4]f32 = .{ 0, 0, 0, 0 },
    /// Canvas pixels per 640×480 screen pixel (port: models at window resolution;
    /// 1 in the exe). External coordinates (`origin`, `bbox`, depth) are screen pixels.
    ss: u32 = 1,
    /// Background depth per screen pixel (with `ss` > 1; the canvas reads it through NaN).
    base_depth: []f32 = &.{},
    /// The canvas is fully cleared (from now on `begin` clears only what was drawn).
    fresh: bool = false,
    /// Background depth without object z-frames for `base_key` — with the same camera
    /// and background `begin` copies it instead of recomputing.
    base_cache: []f32 = &.{},
    base_key: ?BaseKey = null,
    base_cache_valid: bool = false,
    overlay_dirty: raster.Canvas.Rect = .empty,
    /// Last shadow composite (viewer command `shadowstat`): canvas pixels with
    /// coverage and, of those, the ones cut by the background depth test (only
    /// with `ss` > 1, where background depth is separate from the models).
    shadow_stat: struct { lit: u32 = 0, cut_bg: u32 = 0 } = .{},
    /// Shadow texture sampling per canvas column and row (compositeShadow): fixed by the
    /// layer's size, so computed once instead of per pixel.
    shadow_x: []ShadowSample = &.{},
    shadow_y: []ShadowSample = &.{},

    const BaseKey = struct { x: i32, y: i32, src: DepthSource };

    /// PLATFORM: keep full 640×480 crisp 3D characters; use ARM32 fast paths
    /// (off-screen actor culling, area-weighted normals, 256×256 soft shadow mask, nearest texels) without pixelating actors.
    const is_arm = @import("builtin").cpu.arch == .arm;
    const half_res = false;
    const div: u32 = if (half_res) 2 else 1;
    const mask_size = if (is_arm) 256 else 512;
    /// Side of the layer tile grid for the shadow plane (8×8, 0x54b200).
    const shadow_grid = 8;

    inline fn scaleF(self: *const Layer) f32 {
        return if (half_res) 0.5 else @floatFromInt(self.ss);
    }

    /// Where canvas column (row) `i` samples the shadow texture: its center in
    /// screen pixels, the two texels and the weight of the second (bilinear, 0x406da6).
    const ShadowSample = struct { pos: f32, frac: f32, lo: u32, hi: u32 };

    /// The samples of an axis of `screen` screen pixels at `ss` canvas pixels each.
    fn shadowAxis(gpa: Allocator, screen: u32, ss: u32) ![]ShadowSample {
        const cw = (@as(usize, screen) * ss) / div;
        const out = try gpa.alloc(ShadowSample, cw);
        const tn: f32 = @floatFromInt(mask_size / 2);
        const span: f32 = @floatFromInt(mask_size - 2);
        const fs: f32 = @floatFromInt(screen);
        const ssf: f32 = if (half_res) 0.5 else @floatFromInt(ss);
        for (out, 0..) |*o, i| {
            const e = (num.toF32(i) + 0.5) / ssf;
            const v = (1 + e * span / fs) / 2 - 0.5;
            const iv = @floor(v);
            o.* = .{ .pos = e, .frac = v - iv, .lo = @intFromFloat(std.math.clamp(iv, 0, tn - 1)), .hi = @intFromFloat(std.math.clamp(iv + 1, 0, tn - 1)) };
        }
        return out;
    }

    /// Layer of `w×h` screen pixels with a canvas `ss` times finer.
    pub fn initScaled(gpa: Allocator, w: u32, h: u32, ss: u32) !Layer {
        const cw = (w * ss) / div;
        const ch = (h * ss) / div;
        const bw = w / div;
        const bh = h / div;
        var canvas = try raster.Canvas.init(gpa, cw, ch, .{ 0, 0, 0, 0 });
        errdefer canvas.deinit(gpa);
        var glow = try Image.init(gpa, cw, ch);
        errdefer glow.deinit(gpa);
        @memset(glow.pixels, 0);
        var screen = try Image.init(gpa, cw, ch);
        errdefer screen.deinit(gpa);
        @memset(screen.pixels, 0);
        const shadow = try gpa.alloc(u8, mask_size * mask_size);
        errdefer gpa.free(shadow);
        const base: []f32 = if (!half_res and ss > 1) try gpa.alloc(f32, @as(usize, bw) * bh) else &.{};
        errdefer if (!half_res and ss > 1) gpa.free(base);
        const cache = try gpa.alloc(f32, @as(usize, bw) * bh);
        errdefer gpa.free(cache);
        const shadow_x = try shadowAxis(gpa, w, ss);
        errdefer gpa.free(shadow_x);
        const shadow_y = try shadowAxis(gpa, h, ss);
        if (!half_res and ss > 1) canvas.base = .{ .depth = base, .w = bw, .ss = ss };
        return .{ .canvas = canvas, .glow = glow, .screen = screen, .shadow = shadow, .ss = if (half_res) 1 else ss, .base_depth = base, .base_cache = cache, .shadow_x = shadow_x, .shadow_y = shadow_y };
    }

    pub fn deinit(self: *Layer, gpa: Allocator) void {
        if (self.base_depth.len > 0) gpa.free(self.base_depth);
        gpa.free(self.base_cache);
        gpa.free(self.shadow);
        gpa.free(self.shadow_x);
        gpa.free(self.shadow_y);
        self.glow.deinit(gpa);
        self.screen.deinit(gpa);
        self.canvas.deinit(gpa);
    }

    /// Actor shadow (0x419970, 0x41a8c0): triangles of the mesh's main part
    /// (all of them for rura and fang) are projected from light point `light` onto the
    /// actor's floor plane (z of its position) into the 512×512 mask without a depth test;
    /// 80×60 tiles touched by the shadow remember the floor height (the first actor wins).
    /// Returns the posed vertices when skinning was needed, for the matching model draw.
    pub fn castShadow(self: *Layer, arena: Allocator, cam: camera.Camera, model: *const Model, p: Placement, light: [3]f32, all_parts: bool) !?SkinPoints {
        // Zeroed planes (`.2s`): no triangle faces the light (0x41aa3a, s ≤ 1e-4).
        if (model.two_sided) return null;
        const zg = p.pos[2];
        if (light[2] <= zg + 1) return null;
        // PLATFORM: cull off-screen actors before expensive bone skinning.
        if (is_arm) {
            const q0 = cam.project(p.pos);
            const lx0 = q0.x - self.origin[0];
            const ly0 = q0.y - self.origin[1];
            const sw_f: f32 = @floatFromInt(self.canvas.img.width * div);
            const sh_f: f32 = @floatFromInt(self.canvas.img.height * div);
            if (lx0 < -240 or lx0 > sw_f + 240 or ly0 < -280 or ly0 > sh_f + 240) return null;
        }
        const frame = @min(p.frame, model.frames.frames -| 1);
        const pts = try skin.pose(arena, model.mesh, model.frames, frame, .row);
        var main: usize = 0;
        for (model.mesh.parts, 0..) |part, i| {
            if (part.triangles.len > model.mesh.parts[main].triangles.len) main = i;
        }
        const s = p.scale * model.unitScale();
        const ca = @cos(p.angle);
        const sa = @sin(p.angle);
        const w: f32 = @floatFromInt(self.canvas.img.width);
        const h: f32 = @floatFromInt(self.canvas.img.height);
        const ssf: f32 = self.scaleF();
        // Screen → mask (0x40d810): bx = half + (half-1)·ndc.x.
        const half_m: f32 = @floatFromInt(mask_size / 2);
        const kx = 2 * (half_m - 1) / w;
        const ky = 2 * (half_m - 1) / h;
        var lo = [2]f32{ std.math.inf(f32), std.math.inf(f32) };
        var hi = [2]f32{ -std.math.inf(f32), -std.math.inf(f32) };
        for (model.mesh.parts, pts, 0..) |part, ppts, pi| {
            if (!all_parts and pi != main) continue;
            const proj = try arena.alloc([2]f32, ppts.len);
            for (ppts, proj) |v, *o| {
                var wv = model_cache.toWorld(model, v, s, p.pos, ca, sa);
                // Above the light — no shadow (the ray never reaches the floor).
                if (wv[2] >= light[2] - 0.5) wv[2] = light[2] - 0.5;
                const t = (zg - light[2]) / (wv[2] - light[2]);
                const g = [3]f32{ light[0] + (wv[0] - light[0]) * t, light[1] + (wv[1] - light[1]) * t, zg };
                const q = cam.project(g);
                const sx = (q.x - self.origin[0]) * ssf;
                const sy = (q.y - self.origin[1]) * ssf;
                lo = .{ @min(lo[0], sx), @min(lo[1], sy) };
                hi = .{ @max(hi[0], sx), @max(hi[1], sy) };
                o.* = .{ 1 + sx * kx, 1 + sy * ky };
            }
            for (part.triangles) |tri| try self.shadow_tris.append(arena, .{ proj[tri[0]], proj[tri[1]], proj[tri[2]] });
        }
        if (hi[0] < 0 or hi[1] < 0 or lo[0] >= w or lo[1] >= h) return pts;
        const g = shadow_grid;
        const tw = w / g;
        const th = h / g;
        const c0: usize = @intFromFloat(std.math.clamp(@floor(lo[0] / tw), 0, g - 1));
        const c1: usize = @intFromFloat(std.math.clamp(@floor(hi[0] / tw), 0, g - 1));
        const r0: usize = @intFromFloat(std.math.clamp(@floor(lo[1] / th), 0, g - 1));
        const r1: usize = @intFromFloat(std.math.clamp(@floor(hi[1] / th), 0, g - 1));
        for (r0..r1 + 1) |r| for (c0..c1 + 1) |col| {
            if (self.shadow_tiles[r * g + col] == null) self.shadow_tiles[r * g + col] = zg;
        };
        return pts;
    }

    /// Fills a mask triangle with ones (0x43e100): pixel centers, mask rows [y_lo, y_hi).
    fn shadowTri(self: *Layer, v: [3][2]f32, y_lo: i32, y_hi: i32) void {
        const n: i32 = mask_size;
        // In float before the cast: a point behind a perspective camera goes beyond i32.
        const x0: i32 = @intFromFloat(std.math.clamp(@floor(@min(v[0][0], v[1][0], v[2][0])), 0, mask_size));
        const x1: i32 = @intFromFloat(std.math.clamp(@ceil(@max(v[0][0], v[1][0], v[2][0])), -1, mask_size - 1));
        const y0: i32 = @max(y_lo, @as(i32, @intFromFloat(std.math.clamp(@floor(@min(v[0][1], v[1][1], v[2][1])), 0, mask_size))));
        const y1: i32 = @min(y_hi - 1, @as(i32, @intFromFloat(std.math.clamp(@ceil(@max(v[0][1], v[1][1], v[2][1])), -1, mask_size - 1))));
        if (x0 > x1 or y0 > y1) return;
        const area = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[2][0] - v[0][0]) * (v[1][1] - v[0][1]);
        if (@abs(area) < 1e-6) return;
        const inv_area = 1.0 / area;
        var y = y0;
        while (y <= y1) : (y += 1) {
            const py = num.toF32(y) + 0.5;
            var x = x0;
            while (x <= x1) : (x += 1) {
                const px = num.toF32(x) + 0.5;
                const w0 = ((v[1][0] - px) * (v[2][1] - py) - (v[2][0] - px) * (v[1][1] - py)) * inv_area;
                const w1 = ((v[2][0] - px) * (v[0][1] - py) - (v[0][0] - px) * (v[2][1] - py)) * inv_area;
                if (w0 < 0 or w1 < 0 or 1 - w0 - w1 < 0) continue;
                self.shadow[@intCast(y * n + x)] = 1;
            }
        }
    }

    /// Shadow composite after the actors (0x40d150, called at 0x4209c6 after all
    /// models and the battle grid): the mask shrinks into a 256×256 texture (texel is
    /// a 2×2 sum, 0…4) and is drawn as the 80×60 tile quads the shadow touched, with
    /// bilinear sampling (0x406da6). Color is `dst·(1 − I·coverage)`
    /// (mode 3 at 0x408ad0: ZERO, INVSRCCOLOR; texture × vertex color I). The depth
    /// test is LESSEQUAL without writes (0x408930, 0x408a40): the quad depth is the
    /// floor plane of the first actor touching the tile, moved toward the camera by
    /// 0.002 of the depth range (0x419910, constant 0x508560) — `bias` in layer depth
    /// buffer units. Empty layer pixels (background) get black with
    /// alpha I·coverage.
    pub fn compositeShadow(self: *Layer, cam: camera.Camera, intensity: f32, bias: f32, pool: ?*raster.Canvas.Pool) void {
        if (std.mem.allEqual(?f32, &self.shadow_tiles, null)) {
            self.shadow_tris = .empty;
            self.shadow_stat = .{};
            return;
        }
        const on_gpu = if (hardware_shadow) |draw_mask| draw_mask(self.shadow, mask_size, self.shadow_tris.items) else false;
        if (!on_gpu) {
            var fill: MaskJob = .{ .layer = self };
            if (pool) |p| p.run(&fill) else fill.run();
        }
        self.shadow_tris = .empty;
        const n = mask_size;
        const tn = n / 2;
        var cov: [tn * tn]u8 = undefined;
        for (0..tn) |ty| for (0..tn) |tx| {
            const m = self.shadow[2 * ty * n + 2 * tx ..];
            cov[ty * tn + tx] = m[0] + m[1] + m[n] + m[n + 1];
        };
        const ss = self.ss;
        const w = self.canvas.img.width / ss;
        const h = self.canvas.img.height / ss;
        var job: ShadowJob = .{ .layer = self, .cam = cam, .intensity = intensity, .bias = bias, .cov = &cov, .chunks = (h + ShadowJob.rows - 1) / ShadowJob.rows, .lo = @splat(.{ w, h }), .hi = @splat(.{ 0, 0 }) };
        if (pool) |p| p.run(&job) else job.run();
        self.shadow_stat = .{ .lit = job.lit.raw, .cut_bg = job.cut_bg.raw };
        // Darkened screen pixels go into the canvas "drawn" area per tile (shadows of
        // different actors do not merge into one bounding box).
        for (job.lo, job.hi) |l, hh| if (l[0] < hh[0]) {
            self.canvas.beginRegion();
            self.canvas.addDirty(.{ .x0 = @intCast(l[0] * ss), .y0 = @intCast(l[1] * ss), .x1 = @intCast(hh[0] * ss), .y1 = @intCast(hh[1] * ss) });
        };
    }

    /// Fills `shadow_tris` into the mask in bands of rows.
    const MaskJob = struct {
        layer: *Layer,
        next: std.atomic.Value(i32) = .init(0),

        const rows = 32;

        pub fn run(job: *MaskJob) void {
            while (true) {
                const lo = job.next.fetchAdd(rows, .monotonic);
                if (lo >= mask_size) return;
                const flo: f32 = @floatFromInt(lo);
                for (job.layer.shadow_tris.items) |v| {
                    if (@max(v[0][1], v[1][1], v[2][1]) < flo - 1 or @min(v[0][1], v[1][1], v[2][1]) > flo + rows + 1) continue;
                    job.layer.shadowTri(v, lo, lo + rows);
                }
            }
        }
    };

    /// compositeShadow in bands of screen rows on the raster pool: every pixel is
    /// computed on its own, so the result equals one thread's; tile bounds and the
    /// counters merge with atomics.
    const ShadowJob = struct {
        layer: *Layer,
        cam: camera.Camera,
        intensity: f32,
        bias: f32,
        cov: *const [mask_size / 2 * (mask_size / 2)]u8,
        chunks: usize,
        next: std.atomic.Value(usize) = .init(0),
        lit: std.atomic.Value(u32) = .init(0),
        cut_bg: std.atomic.Value(u32) = .init(0),
        lo: [shadow_grid * shadow_grid][2]usize,
        hi: [shadow_grid * shadow_grid][2]usize,

        /// Screen rows per band.
        const rows = 8;

        pub fn run(job: *ShadowJob) void {
            const self = job.layer;
            const ss = self.ss;
            const cw = self.canvas.img.width;
            // Tiles are in 640×480 screen pixels; coverage (bilinear sampling of the
            // 256×256 texture), the floor plane and the depth test are evaluated at the
            // center of each fine canvas pixel, like the exe's quad drawn at this
            // resolution. With K = 1 this is the same as screen pixels.
            const w = cw / ss;
            const h = self.canvas.img.height / ss;
            const px = self.canvas.img.pixels;
            const tn = mask_size / 2;
            const g = shadow_grid;
            const tw = @max(1, w / g);
            const th = @max(1, h / g);
            const xs = self.shadow_x;
            const ys = self.shadow_y;
            const cov = job.cov;
            const scanline_depth = ss == 1 and switch (job.cam) {
                .ortho => true,
                .persp => |pc| pc.right[1] == 0,
            };
            var alpha: [5]f32 = undefined;
            for (&alpha, 0..) |*a, i| a.* = std.math.clamp(job.intensity * num.toF32(i) / 4, 0, 1);
            var lit: u32 = 0;
            var cut_bg: u32 = 0;
            var lo: [g * g][2]usize = @splat(.{ w, h });
            var hi: [g * g][2]usize = @splat(.{ 0, 0 });
            while (true) {
                const ci = job.next.fetchAdd(1, .monotonic);
                if (ci >= job.chunks) break;
                for (ci * rows..@min(h, ci * rows + rows)) |y| {
                    // Only tiles the shadow touched (the last one reaches the layer edge).
                    const tr: usize = @min(y / th, g - 1);
                    if (std.mem.allEqual(?f32, self.shadow_tiles[tr * g ..][0..g], null)) continue;
                    for (0..g) |tc| {
                        const zg = self.shadow_tiles[tr * g + tc] orelse continue;
                        // With no horizontal ray slope, this tile's plane depth is constant across the scanline.
                        const row_plane: ?f32 = if (scanline_depth)
                            -job.cam.planeDepth(0, ys[y].pos + self.origin[1], zg) - job.bias
                        else
                            null;
                        for (tc * tw..if (tc == g - 1) w else (tc + 1) * tw) |x| {
                            var any = false;
                            for (y * ss..y * ss + ss) |hy| {
                                // Canvas pixel center in screen pixels and its texels (shadowAxis).
                                const sy = ys[hy];
                                const ey = sy.pos;
                                const fy = sy.frac;
                                const y0: usize = sy.lo;
                                const y1: usize = sy.hi;
                                for (x * ss..x * ss + ss) |hx| {
                                    const sx = xs[hx];
                                    const ex = sx.pos;
                                    const fx = sx.frac;
                                    const x0: usize = sx.lo;
                                    const x1: usize = sx.hi;
                                    const a: f32 = if (is_arm) blk: {
                                        const b00 = cov[y0 * tn + x0];
                                        if (b00 == 0) continue;
                                        break :blk alpha[b00];
                                    } else blk: {
                                        const b00 = cov[y0 * tn + x0];
                                        const b10 = cov[y0 * tn + x1];
                                        const b01 = cov[y1 * tn + x0];
                                        const b11 = cov[y1 * tn + x1];
                                        if ((b00 | b10 | b01 | b11) == 0) continue;
                                        const c00: f32 = @floatFromInt(b00);
                                        const c10: f32 = @floatFromInt(b10);
                                        const c01: f32 = @floatFromInt(b01);
                                        const c11: f32 = @floatFromInt(b11);
                                        const sum = (c00 * (1 - fx) + c10 * fx) * (1 - fy) + (c01 * (1 - fx) + c11 * fx) * fy;
                                        if (sum <= 0) continue;
                                        break :blk std.math.clamp(job.intensity * sum / 4, 0, 1);
                                    };
                                    any = true;
                                    lit += 1;
                                    const plane = row_plane orelse (-job.cam.planeDepth(ex + self.origin[0], ey + self.origin[1], zg) - job.bias);
                                    const i = (hy * cw + hx) * 4;
                                    if (plane > self.canvas.depthAt(hy * cw + hx, hx, hy)) {
                                        if (ss > 1 and std.math.isNan(self.canvas.depth[hy * cw + hx])) cut_bg += 1;
                                        if (self.shadow_debug) px[i..][0..4].* = .{ 255, 0, 0, 255 };
                                        continue;
                                    }
                                    if (px[i + 3] == 0) {
                                        px[i..][0..4].* = .{ 0, 0, 0, @intFromFloat(a * 255) };
                                    } else for (px[i..][0..3]) |*ch| {
                                        ch.* = @intFromFloat(num.toF32(ch.*) * (1 - a));
                                    }
                                }
                            }
                            if (!any) continue;
                            const ti = tr * g + tc;
                            lo[ti] = .{ @min(lo[ti][0], x), @min(lo[ti][1], y) };
                            hi[ti] = .{ @max(hi[ti][0], x + 1), @max(hi[ti][1], y + 1) };
                        }
                    }
                }
            }
            _ = job.lit.fetchAdd(lit, .monotonic);
            _ = job.cut_bg.fetchAdd(cut_bg, .monotonic);
            for (lo, hi, 0..) |l, hh, ti| if (l[0] < hh[0]) for (0..2) |k| {
                _ = @atomicRmw(usize, &job.lo[ti][k], .Min, l[k], .monotonic);
                _ = @atomicRmw(usize, &job.hi[ti][k], .Max, hh[k], .monotonic);
            };
        }
    };

    const DepthFillJob = struct {
        out: []f32,
        src: DepthSource,
        sw: usize,
        sh_rows: usize,
        ox32: i32,
        oy32: i32,
        next: std.atomic.Value(usize) = .init(0),
        const rows = 60;

        pub fn run(job: *DepthFillJob) void {
            const inv_k: f32 = 2.0 / camera.k;
            const sw = job.sw;
            const ox32 = job.ox32;
            const oy32 = job.oy32;
            while (true) {
                const y0 = job.next.fetchAdd(rows, .monotonic);
                if (y0 >= job.sh_rows) return;
                const y1 = @min(job.sh_rows, y0 + rows);
                switch (job.src) {
                    .none => @memset(job.out[y0 * sw .. y1 * sw], std.math.inf(f32)),
                    .heights => |hm| {
                        const scale = (hm.cfg.gscale * inv_k) / (hm.cfg.g0 - hm.cfg.g1);
                        const g0_s = hm.cfg.g0 * scale;
                        const zw: i32 = @intCast(hm.z.width);
                        const zh: i32 = @intCast(hm.z.height);
                        const zs: usize = if (hm.z.stride != 0) hm.z.stride else hm.z.width;
                        const sh = hm.z.shift;
                        const in_x = ox32 >= 0 and ox32 + @as(i32, @intCast(sw * div)) <= zw;
                        for (y0..y1) |yy| {
                            const by: i32 = oy32 + @as(i32, @intCast(yy * div));
                            const byf: f32 = @floatFromInt(by);
                            const row = job.out[yy * sw ..][0..sw];
                            if (by >= 0 and by < zh) {
                                const row_base = -(byf + g0_s);
                                const zrow = hm.z.depth[(@as(usize, @intCast(by)) >> sh) * zs ..];
                                if (in_x) {
                                    const x0: usize = @intCast(ox32);
                                    if (sh == 1 and div == 1 and (x0 & 1) == 0) {
                                        const zr = zrow[x0 >> 1 ..];
                                        var xx: usize = 0;
                                        while (xx + 1 < sw) : (xx += 2) {
                                            const d = row_base + @as(f32, @floatFromInt(zr[xx >> 1])) * scale;
                                            row[xx] = d;
                                            row[xx + 1] = d;
                                        }
                                        if (xx < sw) row[xx] = row_base + @as(f32, @floatFromInt(zr[xx >> 1])) * scale;
                                    } else {
                                        for (row, 0..) |*d, xx| {
                                            d.* = row_base + @as(f32, @floatFromInt(zrow[(x0 + xx * div) >> sh])) * scale;
                                        }
                                    }
                                } else {
                                    for (row, 0..) |*d, xx| {
                                        const bx = ox32 + @as(i32, @intCast(xx * div));
                                        d.* = if (bx >= 0 and bx < zw)
                                            row_base + @as(f32, @floatFromInt(zrow[@as(usize, @intCast(bx)) >> sh])) * scale
                                        else
                                            -byf;
                                    }
                                }
                            } else {
                                @memset(row, -byf);
                            }
                        }
                    },
                    .persp => |pz| {
                        const zw: i32 = @intCast(pz.z.width);
                        const zh: i32 = @intCast(pz.z.height);
                        const zs: usize = if (pz.z.stride != 0) pz.z.stride else pz.z.width;
                        const sh = pz.z.shift;
                        const pbase = pz.far + pz.tol;
                        const pscale = pz.tl * (1.0 / 65536.0);
                        for (y0..y1) |yy| {
                            const by: i32 = oy32 + @as(i32, @intCast(yy * div));
                            const row = job.out[yy * sw ..][0..sw];
                            if (by >= 0 and by < zh) {
                                const zrow = pz.z.depth[(@as(usize, @intCast(by)) >> sh) * zs ..];
                                for (row, 0..) |*d, xx| {
                                    const bx = ox32 + @as(i32, @intCast(xx * div));
                                    if (bx >= 0 and bx < zw) {
                                        const v = zrow[@as(usize, @intCast(bx)) >> sh];
                                        d.* = if (v == 0) std.math.inf(f32) else pbase - @as(f32, @floatFromInt(v)) * pscale;
                                    } else {
                                        d.* = std.math.inf(f32);
                                    }
                                }
                            } else {
                                @memset(row, std.math.inf(f32));
                            }
                        }
                    },
                }
            }
        }
    };

    /// Clears the layer and fills the depth buffer with the background surface: pixel
    /// (x, y) with height H lies at depth y + 2H/k (see camera.worldToScreen).
    pub fn begin(self: *Layer, origin: [2]f32, src: DepthSource) void {
        self.beginWithPool(origin, src, null);
    }

    fn clearRect(pixels: []u8, w: usize, rect: raster.Canvas.Rect, value: u8) void {
        if (rect.isEmpty()) return;
        const stride = w * 4;
        if (rect.x0 == 0 and rect.x1 == w) {
            @memset(pixels[rect.y0 * stride .. rect.y1 * stride], value);
        } else {
            for (rect.y0..rect.y1) |y| {
                const row = pixels[y * stride ..];
                @memset(row[rect.x0 * 4 .. rect.x1 * 4], value);
            }
        }
    }

    fn restoreDepth(out: []f32, saved: []const f32, w: usize, rect: raster.Canvas.Rect) void {
        if (rect.isEmpty()) return;
        for (rect.y0..rect.y1) |y| {
            const start = y * w + rect.x0;
            const end = y * w + rect.x1;
            @memcpy(out[start..end], saved[start..end]);
        }
    }

    pub fn beginWithPool(self: *Layer, origin: [2]f32, src: DepthSource, pool: ?*raster.Canvas.Pool) void {
        self.origin = origin;
        const ss = self.ss;
        const w = self.canvas.img.width;
        const h = self.canvas.img.height;
        const prev_dirty = self.canvas.dirtyRegions();
        const prev_overlay = self.overlay_dirty;
        const was_fresh = self.fresh;
        // Only what was drawn last frame is cleared (the rest is already empty); the
        // fine depth buffer becomes NaN (0xff bytes): background depth comes from the coarse one.
        if (!self.fresh) {
            @memset(self.canvas.img.pixels, 0);
            if (ss > 1) @memset(std.mem.sliceAsBytes(self.canvas.depth), 0xff);
            self.fresh = true;
        } else for (prev_dirty) |rect| {
            clearRect(self.canvas.img.pixels, w, rect, 0);
            if (ss > 1) clearRect(std.mem.sliceAsBytes(self.canvas.depth), w, rect, 0xff);
        }
        self.canvas.dirty = .empty;
        self.canvas.n_regions = 0;
        self.overlay_dirty = .empty;
        const ad = self.canvas.add_dirty;
        if (self.glow_used) clearRect(self.glow.pixels, w, ad, 0);
        if (self.screen_used) clearRect(self.screen.pixels, w, ad, 0);
        self.canvas.add_dirty = .empty;
        self.canvas.glow_px = null;
        self.glow_used = false;
        self.screen_used = false;
        @memset(self.shadow, 0);
        self.shadow_tiles = @splat(null);
        self.shadow_tris = .empty;
        // Background depth by screen rows: the source kind is resolved once per frame.
        const sw = w / ss;
        const out = if (ss == 1) self.canvas.depth else self.base_depth;
        const key: BaseKey = .{ .x = @intFromFloat(origin[0]), .y = @intFromFloat(origin[1]), .src = src };
        const same_key = if (self.base_key) |k| std.meta.eql(k, key) else false;
        if (same_key and self.base_cache_valid) {
            if (was_fresh) {
                if (ss == 1) for (prev_dirty) |rect| restoreDepth(out, self.base_cache, sw, rect);
                restoreDepth(out, self.base_cache, sw, prev_overlay);
            } else {
                @memcpy(out[0..self.base_cache.len], self.base_cache);
            }
            return;
        }
        var job: DepthFillJob = .{
            .out = out,
            .src = src,
            .sw = sw,
            .sh_rows = h / ss,
            .ox32 = key.x,
            .oy32 = key.y,
        };
        if (pool) |p| p.run(&job) else job.run();
        if (same_key) {
            // Camera held steady for 2 ticks: cache base_depth now (avoids 1.2 MB copy while scrolling)
            @memcpy(self.base_cache, out[0..self.base_cache.len]);
            self.base_cache_valid = true;
        } else {
            self.base_key = key;
            self.base_cache_valid = false;
        }
    }

    /// Background pixel depth (by is the background row) for z-buffer value `v` in
    /// `.zbf` space (as in begin).
    fn depthOf(src: DepthSource, by: i64, v: u16) f32 {
        return switch (src) {
            .none => std.math.inf(f32),
            .heights => |hm| -(num.toF32(by) + 2 * hm.cfg.height(v) / camera.k),
            .persp => |pz| backdrop.perspDepth(pz.far, pz.tl, v) + pz.tol,
        };
    }

    /// Object z-frame (`.spz`, raw spr1 with `.zbf` values) over the background depth
    /// at background point (x0, y0) (z pass 0x4b3ff0: a copy without a depth test).
    pub fn overlayDepth(self: *Layer, frame: sprite.Frame, x0: i64, y0: i64, src: DepthSource) void {
        if (src == .none or !frame.raw) return;
        const fw: usize = frame.width;
        if (frame.data.len < 4 + fw * frame.height * 2) return;
        const w: i64 = @intCast(self.canvas.img.width / self.ss);
        const h: i64 = @intCast(self.canvas.img.height / self.ss);
        const ox: i64 = @intFromFloat(self.origin[0]);
        const oy: i64 = @intFromFloat(self.origin[1]);
        const dx = @divFloor(x0 - ox, div);
        const dy = @divFloor(y0 - oy, div);
        const left = @max(dx, 0);
        const top = @max(dy, 0);
        const right = @min(dx + @divFloor(@as(i64, @intCast(fw)) + div - 1, div), w);
        const bottom = @min(dy + @divFloor(@as(i64, frame.height) + div - 1, div), h);
        if (left >= right or top >= bottom) return;
        self.overlay_dirty = self.overlay_dirty.join(.{ .x0 = @intCast(left), .x1 = @intCast(right), .y0 = @intCast(top), .y1 = @intCast(bottom) });
        const out = if (self.ss == 1) self.canvas.depth else self.base_depth;
        for (@as(usize, @intCast(top))..@as(usize, @intCast(bottom))) |ly| {
            const yy: usize = @intCast((@as(i64, @intCast(ly)) - dy) * div);
            const by = y0 + @as(i64, @intCast(yy));
            const xx: usize = @intCast((left - dx) * div);
            const input = frame.data[4 + (yy * fw + xx) * 2 ..];
            const row = out[ly * @as(usize, @intCast(w)) + @as(usize, @intCast(left)) ..][0..@intCast(right - left)];
            for (row, 0..) |*d, x| {
                const v = std.mem.readInt(u16, input[x * div * 2 ..][0..2], .little);
                d.* = depthOf(src, by, v);
            }
        }
    }

    /// Weapon in hand: model and attachment point on the actor model (0x42b300).
    pub const Attachment = struct {
        model: *const Model,
        link: @import("../core.zig").model.HoldMode,
        /// Command 0x20 (0x42ac30): offset from the point (exe units — mesh × actor
        /// model scale), rotation (for a row vector) and mesh scale.
        offset: [3]f32 = .{ 0, 0, 0 },
        rot: [9]f32 = .{ 1, 0, 0, 0, 1, 0, 0, 0, 1 },
        scale: f32 = 1,
        /// Model frame at the point (Headnail's saws spin, "obrot", 0x48d760).
        frame: u32 = 0,
    };

    pub const Placement = struct {
        pos: [3]f32,
        angle: f32,
        scale: f32,
        frame: u32,
        attach: ?Attachment = null,
        /// More attached meshes (ActivateLink: Headnail's saws, the bat).
        extra: []const Attachment = &.{},
        /// Alpha (+0x311c, 0x42a620: alpha v/255) and depth write (off during the
        /// transformation).
        alpha: f32 = 1,
        depth_write: bool = true,
        /// Shift toward the camera as a fraction of depth, in the depth test only
        /// (items on surfaces — bed, table: the background depth fit is accurate to
        /// ~2%, otherwise the item sinks). Draw such models before the ones they
        /// must sort against: their true depth is written.
        depth_bias: f32 = 0,
        /// The model itself is not drawn (materialization at v < 0.6), only the passes.
        hidden: bool = false,
        /// Materialization glow (0x42a620): additive color without a texture.
        tint: ?[3]f32 = null,
        /// Additive passes over the model: ice on a frozen fighter (0x4dc700),
        /// "current" on a shocked one.
        overlays: [2]?Overlay = .{ null, null },
    };

    fn enableGlow(self: *Layer) void {
        self.glow_used = true;
        self.canvas.glow_px = self.glow.pixels;
    }

    const Overlay = struct {
        tex: Image,
        uv_scale: f32 = 1,
        uv_offset: [2]f32 = .{ 0, 0 },
        wrap: bool = false,
    };

    pub const SkinPoints = []const []const [3]f32;

    pub fn draw(self: *Layer, arena: Allocator, cam: camera.Camera, model: *const Model, p: Placement) !void {
        return self.drawWithPose(arena, cam, model, p, null);
    }

    /// `pose` must come from the same model and animation frame in this arena.
    pub fn drawWithPose(self: *Layer, arena: Allocator, cam: camera.Camera, model: *const Model, p: Placement, pose: ?SkinPoints) !void {
        self.bbox = .{ std.math.inf(f32), std.math.inf(f32), -std.math.inf(f32), -std.math.inf(f32) };
        // PLATFORM: cull off-screen actors/items before expensive bone skinning & lighting.
        if (is_arm) {
            const q0 = cam.project(p.pos);
            const lx0 = q0.x - self.origin[0];
            const ly0 = q0.y - self.origin[1];
            const sw_f: f32 = @floatFromInt(self.canvas.img.width * div);
            const sh_f: f32 = @floatFromInt(self.canvas.img.height * div);
            if (lx0 < -240 or lx0 > sw_f + 240 or ly0 < -280 or ly0 > sh_f + 240) return;
        }
        self.canvas.beginRegion();
        const frame = @min(p.frame, model.frames.frames -| 1);
        const pts = pose orelse try skin.pose(arena, model.mesh, model.frames, frame, .row);
        const s = p.scale * model.unitScale();
        try self.drawParts(arena, cam, model, pts, s, p, null);
        if (p.attach) |at| try self.drawAttached(arena, cam, model, frame, s, p, at);
        for (p.extra) |at| try self.drawAttached(arena, cam, model, frame, s, p, at);
    }

    /// Effect mesh at an actor attachment point (0x4294f0, type 1): additive, with a
    /// depth test and no depth write (ZWRITE off at 0x408a40(0), default D3D z-test) —
    /// whatever is closer to the camera (the shooter, the target, neighbours) covers the
    /// nitro stream and muzzle flashes, as in the exe.
    pub fn drawEffect(self: *Layer, arena: Allocator, cam: camera.Camera, owner: *const Model, p: Placement, at: Attachment, tex: Image) !void {
        const frame = @min(p.frame, owner.frames.frames -| 1);
        const pts = try attachedPoints(arena, owner, frame, at) orelse return;
        const s = p.scale * owner.unitScale();
        const ca = @cos(p.angle);
        const sa = @sin(p.angle);
        const aspect = cam.aspect();
        const ssf: f32 = self.scaleF();
        for (at.model.mesh.parts, pts) |part, ppts| {
            for (part.triangles) |t| {
                var sp: [3][3]f32 = undefined;
                var uv: [3][2]f32 = undefined;
                for (0..3) |i| {
                    var wv = model_cache.toWorld(owner, ppts[t[i]], s, p.pos, ca, sa);
                    wv[2] = p.pos[2] + (wv[2] - p.pos[2]) * aspect;
                    const q = cam.project(wv);
                    sp[i] = .{ (q.x - self.origin[0]) * ssf, (q.y - self.origin[1]) * ssf, -q.depth };
                    uv[i] = part.vertices[t[i]].uv;
                }
                self.canvas.triangleShaded(sp, uv, tex, raster.gray(1), .{ .add = &self.glow });
                self.enableGlow();
            }
        }
    }

    /// Billboard of the particle batch (0x45e150, flushed at 0x45d8d0 after the models
    /// and shadows): depth-tested against the scene, no depth write, blended per `.dsc`.
    /// The flush turns ZWRITE off and ZENABLE off only off combat levels
    /// ([0x696ea0] == 0), so this is the combat-level path. `corners` are world
    /// points, clockwise from top-left. Vertex colour (0x45e150): alpha mode (r, g, b, a),
    /// otherwise (a·r, a·g, a·b).
    /// `program`, `k`: a material's CPU program (raster `Shade.program`, strength `pk`; .none: the exe's).
    pub fn billboard(self: *Layer, cam: camera.Camera, corners: [4][3]f32, tex: Image, alpha: f32, rgb: [3]f32, blend: BillboardBlend, program: raster.Canvas.Program, pk: f32) void {
        const ssf: f32 = self.scaleF();
        var sp: [4][3]f32 = undefined;
        for (corners, &sp) |p, *s| {
            const q = cam.project(p);
            s.* = .{ (q.x - self.origin[0]) * ssf, (q.y - self.origin[1]) * ssf, -q.depth };
        }
        const uv = [4][2]f32{ .{ 0, 0 }, .{ 1, 0 }, .{ 1, 1 }, .{ 0, 1 } };
        const k = std.math.clamp(alpha, 0, 1);
        const lit: [3]f32 = if (blend == .alpha) rgb else .{ rgb[0] * k, rgb[1] * k, rgb[2] * k };
        const col = [3][3]f32{ lit, lit, lit };
        var sh: raster.Canvas.Shade = switch (blend) {
            .alpha => .{ .alpha = alpha, .depth_write = false, .tex_alpha = true },
            .add => .{ .add = &self.glow, .depth_write = false },
            .screen => .{ .add = &self.screen, .screen = true, .depth_write = false },
        };
        sh.program = program;
        sh.k = pk;
        switch (blend) {
            .alpha => {},
            .add => self.enableGlow(),
            .screen => self.screen_used = true,
        }
        self.canvas.triangleShaded(.{ sp[0], sp[1], sp[2] }, .{ uv[0], uv[1], uv[2] }, tex, col, sh);
        self.canvas.triangleShaded(.{ sp[0], sp[2], sp[3] }, .{ uv[0], uv[2], uv[3] }, tex, col, sh);
    }

    /// Batch billboard blend (0x408ad0 modes 2, 1, 4): a material's set.
    pub const BillboardBlend = @import("../effects.zig").material.Blend;

    fn drawAttached(self: *Layer, arena: Allocator, cam: camera.Camera, model: *const Model, frame: u32, s: f32, p: Placement, at: Attachment) !void {
        const wpts = try attachedPoints(arena, model, frame, at) orelse return;
        // Points are already in actor mesh units: drawParts applies the actor rotation and shift.
        try self.drawParts(arena, cam, at.model, wpts, s, .{ .pos = p.pos, .angle = p.angle, .scale = p.scale, .frame = 0, .alpha = p.alpha, .depth_write = p.depth_write, .hidden = p.hidden }, model);
    }

    /// Back face skipped as the exe does (0x447460, 0x446f20): plane n = (P0−P1)×(P2−P1)
    /// with n·E + d > 0 for the eye E (n·dir for an orthographic view) — the same as
    /// the projected triangle's winding (screen y down; checked on both cameras: the
    /// other sign turns closed models inside out).
    fn backFacing(sp: [3][3]f32) bool {
        return (sp[1][0] - sp[0][0]) * (sp[2][1] - sp[0][1]) - (sp[1][1] - sp[0][1]) * (sp[2][0] - sp[0][0]) > 0;
    }

    /// Model triangles from points `pts` (mesh units). `frame_of` — whose local
    /// transform (rotation and shift, 0x44c060) applies: the actor's for weapons.
    /// Lighting (0x4c6bc0, 0x47e1f0): the vertex normal is minus the sum of face normals
    /// (A − B) × (C − B) over welded vertices, in mesh units; the color comes from the
    /// level light in world coordinates; triangles use vertex colors.
    fn drawParts(self: *Layer, arena: Allocator, cam: camera.Camera, model: *const Model, pts: []const []const [3]f32, s: f32, p: Placement, frame_of: ?*const Model) !void {
        const owner = frame_of orelse model;
        const ca = @cos(p.angle);
        const sa = @sin(p.angle);
        const aspect = cam.aspect();
        const ssf: f32 = self.scaleF();
        for (model.mesh.parts, pts, 0..) |part, ppts, pi| {
            const mips: []const Image = if (self.textures) |t| t.partMips(model, pi) else &.{};
            const tex: ?Image = if (mips.len > 0) mips[0] else null;
            const weld = if (pi < model.weld.len) model.weld[pi] else &.{};
            const acc = try arena.alloc([3]f32, ppts.len);
            @memset(acc, .{ 0, 0, 0 });
            for (part.triangles) |t| {
                const cr = vec.cross(vec.sub(ppts[t[0]], ppts[t[1]]), vec.sub(ppts[t[2]], ppts[t[1]]));
                const nf = if (is_arm) cr else vec.normalize(cr);
                for (t) |vi| {
                    const k = if (vi < weld.len) weld[vi] else vi;
                    acc[k] = .{ acc[k][0] + nf[0], acc[k][1] + nf[1], acc[k][2] + nf[2] };
                }
            }
            const proj = try arena.alloc([3]f32, ppts.len);
            const cols = try arena.alloc([3]f32, ppts.len);
            for (ppts, proj, cols, 0..) |pt, *pv, *cv, i| {
                var wv = model_cache.toWorld(owner, pt, s, p.pos, ca, sa);
                wv[2] = p.pos[2] + (wv[2] - p.pos[2]) * aspect;
                const k = if (i < weld.len) weld[i] else i;
                const nm = vec.normalize(acc[k]);
                // Outward normal (−sum), into world axes like a point in toWorld, without scale.
                const n1 = camera.mulVec(owner.local, .{ -nm[0], -nm[1], -nm[2] });
                const nw = [3]f32{ n1[0] * ca - n1[2] * sa, n1[0] * sa + n1[2] * ca, n1[1] };
                cv.* = self.light.color(wv, nw);
                const q = cam.project(wv);
                const lx = q.x - self.origin[0];
                const ly = q.y - self.origin[1];
                pv.* = .{ lx * ssf, ly * ssf, -q.depth };
                if (frame_of == null) self.bbox = .{ @min(self.bbox[0], lx), @min(self.bbox[1], ly), @max(self.bbox[2], lx), @max(self.bbox[3], ly) };
            }
            for (part.triangles) |t| {
                const sp = [3][3]f32{ proj[t[0]], proj[t[1]], proj[t[2]] };
                if (!model.two_sided and backFacing(sp)) continue;
                const uv = [3][2]f32{ part.vertices[t[0]].uv, part.vertices[t[1]].uv, part.vertices[t[2]].uv };
                const col = [3][3]f32{ cols[t[0]], cols[t[1]], cols[t[2]] };
                if (!p.hidden) self.canvas.triangleShaded(sp, uv, tex, col, .{ .alpha = p.alpha, .depth_write = p.depth_write, .mips = mips, .test_bias = p.depth_bias });
                if (frame_of != null) continue;
                for (p.overlays) |ov| if (ov) |o| {
                    self.canvas.triangleShaded(sp, uv, o.tex, raster.gray(1), .{ .add = &self.glow, .uv_scale = o.uv_scale, .uv_offset = o.uv_offset, .wrap = o.wrap });
                    self.enableGlow();
                };
                if (p.tint) |tc| {
                    self.canvas.triangleShaded(sp, uv, null, .{ tc, tc, tc }, .{ .add = &self.glow });
                    self.enableGlow();
                }
            }
        }
    }
};

/// Weapon `at` vertices in actor mesh units (before its chunk 14 and 11 rotation and shift).
/// The attachment point (0x4290f0) is the bone joint with an offset, like a vertex. For a
/// column vector: w' = Rlinkᵀ·B·X·(Rw·w + shift_w), X mirrors the X axis (so the
/// chaingun.msh flash, extended along +X, sits at the muzzle with the exe offset and the
/// weapon lies in the hand; checked by rendering the rifle, pistol and NATO captain).
pub fn attachedPoints(arena: Allocator, model: *const Model, frame: u32, at: Layer.Attachment) !?[][][3]f32 {
    if (model.frames.bones == 0 or at.link.bone >= model.frames.bones) return null;
    const b = model.frames.matrix(frame, at.link.bone);
    const j = model.mesh.joints[at.link.bone];
    const lp = skin.transform(b, .{ j[0] + at.link.params[0], j[1] + at.link.params[1], j[2] + at.link.params[2] }, .row);
    const lr = camera.rotationXYZ(at.link.params[3..6].*);
    const wm = at.model;
    const wpts = try skin.pose(arena, wm.mesh, wm.frames, at.frame, .row);
    const k = wm.unitScale() / model.unitScale() * at.scale;
    // Point matrix (0x4290f0) M = Bᵀ·R: R = Rx·Ry·Rz from the point angles (+0x2c…+0x34,
    // 0x4293c0/0x429380/0x429340, product 0x47cd30 — A·B), Bᵀ is the bone matrix
    // transposed by 0x47c930. Applied to a column (0x42955f: offset M·o;
    // mesh M·X·v, X the flash rotation): point rotation in weapon axes first, then the
    // bone. Heroes have zero angles; spetsnaz and NATO two_hands (3, 3.5, 0.5) —
    // with the rotation after the bone the rifle pointed backwards.
    const off = camera.mulVecT(b[0..9].*, camera.mulVec(lr, at.offset));
    const ha = model.unitScale();
    const base = [3]f32{ lp[0] + off[0] / ha, lp[1] + off[1] / ha, lp[2] + off[2] / ha };
    for (wpts) |part| for (part) |*v| {
        // Flash rotation (command 0x20 matrix, 0x482010 = Rx·Ry·Rz via 0x47cd30)
        // — R·v in mesh axes: pistol, uzi and rifle flashes along the barrel at the
        // shot (with v·R they pointed up or backwards).
        const wl = camera.mulVec(at.rot, vec.add(camera.mulVec(wm.local, v.*), wm.mesh.center));
        const f = camera.mulVecT(b[0..9].*, camera.mulVec(lr, wl));
        v.* = .{ base[0] + f[0] * k, base[1] + f[1] * k, base[2] + f[2] * k };
    };
    return wpts;
}

test "shadow sampling tables are the per-pixel formula, bit for bit, at every scale" {
    const gpa = std.testing.allocator;
    const tn: f32 = Layer.mask_size / 2;
    for ([_]u32{ 1, 2, 3, 4 }) |ss| for ([_]u32{ 640, 480, 853 }) |screen| {
        const axis = try Layer.shadowAxis(gpa, screen, ss);
        defer gpa.free(axis);
        try std.testing.expectEqual(@as(usize, screen) * ss, axis.len);
        const fs: f32 = @floatFromInt(screen);
        const ssf: f32 = @floatFromInt(ss);
        for (axis, 0..) |a, i| {
            // The formula compositeShadow evaluated for each pixel before the tables.
            const e = (num.toF32(i) + 0.5) / ssf;
            const v = (1 + e * 2 * 255 / fs) / 2 - 0.5;
            const iv = @floor(v);
            try std.testing.expectEqual(@as(u32, @bitCast(e)), @as(u32, @bitCast(a.pos)));
            try std.testing.expectEqual(@as(u32, @bitCast(v - iv)), @as(u32, @bitCast(a.frac)));
            try std.testing.expectEqual(@as(u32, @intFromFloat(std.math.clamp(iv, 0, tn - 1))), a.lo);
            try std.testing.expectEqual(@as(u32, @intFromFloat(std.math.clamp(iv + 1, 0, tn - 1))), a.hi);
        }
    };
}

test "cached depth restores clipped depth sprites and actor pixels" {
    const gpa = std.testing.allocator;
    var heights: [8 * 8]u16 = @splat(5000);
    const src: DepthSource = .{ .persp = .{ .z = .{ .width = 8, .height = 8, .depth = &heights }, .far = 500, .tl = 1000 } };
    const z_frame: sprite.Frame = .{
        .width = 3,
        .height = 2,
        .raw = true,
        .data = &.{ 0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0 },
    };
    for ([_]u32{ 1, 2 }) |ss| {
        var layer = try Layer.initScaled(gpa, 8, 8, ss);
        defer layer.deinit(gpa);
        var fresh = try Layer.initScaled(gpa, 8, 8, ss);
        defer fresh.deinit(gpa);
        layer.begin(.{ 0.25, 0.25 }, src);
        layer.begin(.{ 0.5, 0.5 }, src);
        try std.testing.expect(layer.base_cache_valid);
        layer.overlayDepth(z_frame, -1, 0, src);
        const out = if (ss == 1) layer.canvas.depth else layer.base_depth;
        try std.testing.expectEqual(Layer.depthOf(src, 0, 1), out[0]);
        try std.testing.expectEqual(Layer.depthOf(src, 1, 5), out[9]);
        layer.canvas.triangle(.{ .{ 4, 4, 0 }, .{ 7, 4, 0 }, .{ 4, 7, 0 } }, @splat(@splat(0)), null, raster.gray(1));
        try std.testing.expect(!layer.canvas.dirty.isEmpty());
        layer.begin(.{ 0.75, 0.75 }, src);
        fresh.begin(.{ 0.75, 0.75 }, src);
        try std.testing.expectEqualSlices(u8, fresh.canvas.img.pixels, layer.canvas.img.pixels);
        try std.testing.expectEqualSlices(u8, std.mem.sliceAsBytes(fresh.canvas.depth), std.mem.sliceAsBytes(layer.canvas.depth));
        try std.testing.expectEqualSlices(f32, fresh.base_depth, layer.base_depth);
    }
}

test "shadow is solid with soft edges and skips what is above the floor plane" {
    const gpa = std.testing.allocator;
    var layer = try Layer.initScaled(gpa, 640, 480, 1);
    defer layer.deinit(gpa);
    layer.begin(.{ 0, 0 }, .none);
    // Isometric floor at height 0: layer depth of pixel (x, y) is −y.
    for (0..480) |y| for (0..640) |x| {
        layer.canvas.depth[y * 640 + x] = -(num.toF32(y) + 0.5);
    };
    // An actor closer than the floor (a 20×20 pillar in the middle of the shadow).
    for (200..220) |y| for (300..320) |x| {
        layer.canvas.img.pixels[(y * 640 + x) * 4 ..][0..4].* = .{ 200, 200, 200, 255 };
        layer.canvas.depth[y * 640 + x] = -(num.toF32(y) + 0.5) - 40;
    };
    // The shadow is a 120×100 mask-texel square (≈150×94 layer pixels).
    const a = [2]f32{ 200, 180 };
    const b = [2]f32{ 320, 280 };
    layer.shadowTri(.{ a, .{ b[0], a[1] }, b }, 0, 512);
    layer.shadowTri(.{ a, b, .{ a[0], b[1] } }, 0, 512);
    for (&layer.shadow_tiles) |*t| t.* = 0;
    layer.compositeShadow(.ortho, 0.5, 2.8, null);
    // Inside without holes: every floor pixel is darkened by half.
    const x0: usize = @as(usize, @intFromFloat(@ceil((a[0] - 1) * 640 / 510))) + 2;
    const x1: usize = @as(usize, @intFromFloat(@floor((b[0] - 1) * 640 / 510))) - 2;
    const y0: usize = @as(usize, @intFromFloat(@ceil((a[1] - 1) * 480 / 510))) + 2;
    const y1: usize = @as(usize, @intFromFloat(@floor((b[1] - 1) * 480 / 510))) - 2;
    var holes: usize = 0;
    for (y0..y1) |y| for (x0..x1) |x| {
        if (y >= 200 and y < 220 and x >= 300 and x < 320) continue;
        if (layer.canvas.img.pixels[(y * 640 + x) * 4 + 3] != 127) holes += 1;
    };
    try std.testing.expectEqual(@as(usize, 0), holes);
    // An actor closer than the plane is not darkened.
    try std.testing.expectEqual(@as(u8, 200), layer.canvas.img.pixels[(210 * 640 + 310) * 4]);
    // The edge is soft: there is an intermediate alpha between empty and full.
    var soft = false;
    for (x0 - 6..x0) |x| {
        const al = layer.canvas.img.pixels[((y0 + y1) / 2 * 640 + x) * 4 + 3];
        if (al > 0 and al < 127) soft = true;
    }
    try std.testing.expect(soft);
}

test "vertices from the shadow pass render the same animated model" {
    const gpa = std.testing.allocator;
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const a = arena.allocator();
    var fresh = try Layer.initScaled(gpa, 64, 64, 1);
    defer fresh.deinit(gpa);
    var reused = try Layer.initScaled(gpa, 64, 64, 1);
    defer reused.deinit(gpa);
    fresh.begin(.{ 0, 0 }, .none);
    reused.begin(.{ 0, 0 }, .none);

    const model: Model = .{
        .mesh = .{ .height = 10, .parts = &.{.{
            .name = "",
            .vertices = &.{
                .{ .pos = .{ 0, 0, 0 }, .uv = .{ 0, 0 } },
                .{ .pos = .{ 0, 1, 0 }, .uv = .{ 0, 1 } },
                .{ .pos = .{ 1, 0, 0 }, .uv = .{ 1, 0 } },
            },
            .ranges = &.{ .{ 0, 1 }, .{ 0, 1 }, .{ 0, 1 } },
            .influences = &.{.{ .bone = 0, .weight = 1 }},
            .triangles = &.{.{ 0, 2, 1 }},
        }} },
        .anims = .{},
        .frames = .{ .frames = 2, .bones = 1, .matrices = &.{
            .{ 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
            .{ 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0 },
        }, .extra = &.{} },
        .local = camera.rotationXYZ(.{ 0, 0, 0 }),
    };
    const p: Layer.Placement = .{ .pos = .{ 50, 45, 0 }, .angle = 0, .scale = 1, .frame = 1 };
    try std.testing.expect(try reused.castShadow(a, .ortho, &model, p, .{ 0, 0, 0 }, false) == null);
    const pts = (try reused.castShadow(a, .ortho, &model, p, .{ 50, 45, 50 }, false)).?;
    try std.testing.expectEqual([3]f32{ 1, 0, 0 }, pts[0][0]);

    try fresh.draw(a, .ortho, &model, p);
    try reused.drawWithPose(a, .ortho, &model, p, pts);
    try std.testing.expect(!fresh.canvas.dirty.isEmpty());
    try std.testing.expectEqual(fresh.bbox, reused.bbox);
    try std.testing.expectEqualSlices(u8, fresh.canvas.img.pixels, reused.canvas.img.pixels);
}
