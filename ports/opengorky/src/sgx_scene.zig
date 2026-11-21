const std = @import("std");
const og = @import("opengorky");
const Canvas = og.render.raster.Canvas;

pub fn install() void {
    Canvas.hardware_draw = draw;
    og.render.layer.Layer.hardware_shadow = shadow;
}

const Vertex = extern struct { x: f32, y: f32, u: f32, v: f32, r: f32, g: f32, b: f32 };
const Triangle = extern struct {
    v: [3]Vertex, pixels: ?[*]const u8, tw: c_int, th: c_int,
    x0: c_int, y0: c_int, x1: c_int, y1: c_int, flags: c_int, k: f32,
};
const Slot = extern struct { x: c_int, y: c_int };
extern fn og_scene_enabled() bool;
extern fn og_scene_page([*]const Triangle,c_int,c_int,c_int,[*]Slot,*?[*]const u8,*c_int,*bool) c_int;
extern fn og_scene_shadow([*]u8,c_int,[*]const f32,c_int) bool;
pub fn shadow(mask: []u8, size: usize, tris: []const [3][2]f32) bool {
    if (tris.len == 0) return false;
    return og_scene_shadow(mask.ptr,@intCast(size),@ptrCast(tris.ptr),@intCast(tris.len));
}

fn describe(t: Canvas.Tri) Triangle {
    const sh = t.sh;
    var out: Triangle = .{
        .v = undefined, .pixels = if (t.tex) |im| im.pixels.ptr else null,
        .tw = if (t.tex) |im| @intCast(im.width) else 0, .th = if (t.tex) |im| @intCast(im.height) else 0,
        .x0 = @intCast(t.bounds.x0), .y0 = @intCast(t.bounds.y0),
        .x1 = @intCast(t.bounds.x1), .y1 = @intCast(t.bounds.y1),
        .flags = @as(c_int,@intFromBool(sh.wrap)) + 2*@as(c_int,@intFromBool(sh.tex_alpha and sh.add == null)) +
                 4*@as(c_int,@intFromBool(t.tex != null and sh.program == .heat)) +
                 8*@as(c_int,@intFromBool(t.tex != null and sh.program == .tint)), .k = sh.k,
    };
    const grey: f32 = if (t.tex == null and sh.add == null) 200.0/255.0 else 1;
    for (0..3) |i| out.v[i] = .{
        .x=t.p[i][0], .y=t.p[i][1], .u=t.uv[i][0]*sh.uv_scale+sh.uv_offset[0],
        .v=t.uv[i][1]*sh.uv_scale+sh.uv_offset[1], .r=t.col[i][0]*grey, .g=t.col[i][1]*grey, .b=t.col[i][2]*grey,
    };
    return out;
}

fn mixFragment(canvas: *Canvas, t: Canvas.Tri, idx: usize, x: usize, y: usize, z: f32, rgb: [3]u8, alpha: u8) void {
    const sh = t.sh;
    const dz = canvas.depthAt(idx,x,y);
    const zt = z-sh.test_bias*@abs(z);
    if (sh.add != null) {
        if (zt > dz + @abs(dz)*1e-4+1e-3) return;
    } else if (zt >= dz) return;
    if (sh.program == .dissolve and (sh.add != null or sh.alpha < 1 or sh.tex_alpha)) {
        const noise: f32 = @floatFromInt(std.hash.Murmur2_32.hashUint64((@as(u64,x)<<32)|y)%1024);
        if (noise/1024 < sh.k) return;
    }
    if (sh.add) |image| {
        const d=image.pixels[idx*4..][0..4];
        for (0..3) |k| d[k]=if (sh.screen) @intCast(rgb[k]+@as(u32,d[k])*(255-@as(u32,rgb[k]))/255) else d[k]+|rgb[k];
        d[3]=255;
        return;
    }
    const blend = sh.tex_alpha or sh.alpha < 1;
    const a=if (sh.tex_alpha) sh.alpha*@as(f32,@floatFromInt(alpha))/255 else sh.alpha;
    if (sh.tex_alpha and a<=0) return;
    if (sh.depth_write) canvas.depth[idx]=z;
    if (canvas.glow_px) |glow| {
        const g=glow[idx*4..][0..4];
        if (g[3]!=0) {
            if (blend) { for (0..3) |k| g[k]=@intFromFloat(@as(f32,@floatFromInt(g[k]))*(1-a)); }
            else g.*=.{0,0,0,0};
        }
    }
    const d=canvas.img.pixels[idx*4..][0..4];
    if (!blend) { d.*=.{rgb[0],rgb[1],rgb[2],255}; return; }
    const old=@as(f32,@floatFromInt(d[3]))/255*(1-a);
    const out=a+old;
    if (out<=0) return;
    for (0..3) |k| d[k]=@intFromFloat(std.math.clamp((@as(f32,@floatFromInt(rgb[k]))*a+@as(f32,@floatFromInt(d[k]))*old)/out,0,255));
    d[3]=@intFromFloat(std.math.clamp(out*255,0,255));
}

fn composite(canvas: *Canvas, t: Canvas.Tri, slot: Slot, pixels: [*]const u8, stride: usize, bgra: bool) void {
    const b=t.bounds;
    const p=t.p;
    const dx=((p[1][2]-p[0][2])*(p[2][1]-p[0][1])-(p[2][2]-p[0][2])*(p[1][1]-p[0][1]))*t.inv_area;
    const dy=((p[1][0]-p[0][0])*(p[2][2]-p[0][2])-(p[2][0]-p[0][0])*(p[1][2]-p[0][2]))*t.inv_area;
    const xrel=@as(f32,@floatFromInt(b.x0))+0.5-p[0][0];
    for (b.y0..b.y1) |y| {
        var off=(@as(usize,@intCast(slot.y))+y-b.y0)*stride+@as(usize,@intCast(slot.x))*4;
        const z0=p[0][2]+dx*xrel+dy*(@as(f32,@floatFromInt(y))+0.5-p[0][1]);
        for (b.x0..b.x1) |x| {
            const c=pixels[off..][0..4];
            if (c[3]!=0) mixFragment(canvas,t,y*canvas.img.width+x,x,y,z0+@as(f32,@floatFromInt(x-b.x0))*dx,
                .{c[if (bgra) @as(usize,2) else 0],c[1],c[if (bgra) @as(usize,0) else 2]},c[3]);
            off+=4;
        }
    }
}

extern fn og_model_probe_requested() bool;
extern fn og_model_probe([*]const Triangle, [*]const f32, c_int, c_int, c_int) void;

noinline fn probeModels(canvas: *Canvas, tris: []const Canvas.Tri) void {
    const a = std.heap.c_allocator;
    const forms = a.alloc(Triangle, tris.len) catch return;
    defer a.free(forms);
    const z = a.alloc(f32, tris.len * 3) catch return;
    defer a.free(z);
    var n: usize = 0;
    for (tris) |tri| {
        if (tri.sh.add != null or tri.sh.tex_alpha or tri.sh.alpha < 1 or !tri.sh.depth_write) continue;
        forms[n] = describe(tri);
        for (0..3) |j| z[n*3+j] = tri.p[j][2];
        n += 1;
    }
    if (n > 0) og_model_probe(forms.ptr, z.ptr, @intCast(n), @intCast(canvas.img.width), @intCast(canvas.img.height));
}

extern fn og_scene_direct_models_enabled() bool;
extern fn og_scene_direct_models([*]const Triangle, [*]const f32, c_int, c_int, c_int, [*]u8, [*]f32, ?[*]u8) bool;
extern fn og_scene_resident_enabled(*anyopaque,c_int,c_int) bool;
extern fn og_scene_resident(*anyopaque,[*]const Triangle,[*]const f32,c_int,c_int,c_int,[*]f32,*const Resident) bool;
const ShadowVertex = extern struct { x:f32, y:f32, plane:f32 };
const Resident = extern struct {
    mask: [*]const f32, tiles: [*]const ShadowVertex,
    mask_count: c_int, tile_count: c_int,
    intensity: f32, bias: f32, perspective: bool,
};

pub fn residentAvailable(r: *anyopaque, layer: *const og.render.layer.Layer) bool {
    return !layer.shadow_debug and layer.ss == 1 and
        og_scene_resident_enabled(r,@intCast(layer.canvas.img.width),@intCast(layer.canvas.img.height));
}

pub fn resident(r: *anyopaque, layer: *og.render.layer.Layer, cam: og.actor.camera.Camera, intensity: f32, bias: f32) bool {
    const canvas = &layer.canvas;
    const tris = canvas.recorded.items;
    if (tris.len == 0 or tris.len > 8192 or canvas.base != null or layer.shadow_tris.items.len > 8192) return false;
    for (tris) |t| {
        const sh = t.sh;
        if (sh.add != null or sh.tex_alpha or sh.alpha < 1 or !sh.depth_write or sh.program != .none or sh.test_bias != 0) return false;
    }
    const w: c_int = @intCast(canvas.img.width);
    const h: c_int = @intCast(canvas.img.height);
    const a = std.heap.c_allocator;
    const forms = a.alloc(Triangle, tris.len) catch return false;
    defer a.free(forms);
    const z = a.alloc(f32, tris.len*3) catch return false;
    defer a.free(z);
    for (tris, forms, 0..) |t,*out,i| {
        out.*=describe(t);
        for (0..3) |j| z[i*3+j]=t.p[j][2];
    }
    const perspective = cam == .persp;
    var tiles: [64*6]ShadowVertex = undefined;
    var n: usize = 0;
    if (intensity > 0 and layer.shadow_tris.items.len != 0) {
        var min: [2]f32 = .{ 256, 256 };
        var max: [2]f32 = .{ 0, 0 };
        for (layer.shadow_tris.items) |tri| for (tri) |p| {
            for (0..2) |j| {
                min[j] = @min(min[j],p[j]);
                max[j] = @max(max[j],p[j]);
            }
        };
        const fw: f32 = @floatFromInt(w);
        const fh: f32 = @floatFromInt(h);
        const left = @floor(@max(0,(min[0]-2)*fw/254));
        const top = @floor(@max(0,(min[1]-2)*fh/254));
        const right = @ceil(@min(fw,max[0]*fw/254));
        const bottom = @ceil(@min(fh,max[1]*fh/254));
        const tile_w: f32 = @floatFromInt(@divTrunc(w,8));
        const tile_h: f32 = @floatFromInt(@divTrunc(h,8));
        for (layer.shadow_tiles,0..) |height,i| {
            const plane = height orelse continue;
            const tx = @as(f32,@floatFromInt(i%8))*tile_w;
            const ty = @as(f32,@floatFromInt(i/8))*tile_h;
            const x0 = @max(left,tx);
            const y0 = @max(top,ty);
            const x1 = @min(right,if (i%8==7) fw else tx+tile_w);
            const y1 = @min(bottom,if (i/8==7) fh else ty+tile_h);
            if (x0>=x1 or y0>=y1) continue;
            for ([_][2]f32{.{x0,y0},.{x1,y0},.{x0,y1},.{x0,y1},.{x1,y0},.{x1,y1}}) |p| {
                const d = -cam.planeDepth(p[0]+layer.origin[0],p[1]+layer.origin[1],plane);
                if (perspective and (@abs(d) < 1e-8 or !std.math.isFinite(d))) return false;
                tiles[n] = .{ .x=p[0], .y=p[1], .plane=if (perspective) 1/d else d };
                n+=1;
            }
        }
    }
    const settings: Resident=.{
        .mask=@ptrCast(layer.shadow_tris.items.ptr), .tiles=&tiles,
        .mask_count=@intCast(layer.shadow_tris.items.len), .tile_count=@intCast(n),
        .intensity=intensity, .bias=bias, .perspective=perspective,
    };
    if (!og_scene_resident(r,forms.ptr,z.ptr,@intCast(tris.len),w,h,canvas.depth.ptr,&settings)) return false;
    canvas.rec_gpa=null;
    canvas.recorded.clearRetainingCapacity();
    layer.shadow_tris=.empty;
    return true;
}

fn drawDirect(canvas: *Canvas, tris: []const Canvas.Tri) usize {
    if (canvas.base != null or tris.len > 8192 or canvas.img.width > 640 or canvas.img.height > 480) return 0;
    var n: usize = 0;
    for (tris) |t| {
        const sh = t.sh;
        if (sh.add != null or sh.tex_alpha or sh.alpha < 1 or !sh.depth_write or sh.program != .none or sh.test_bias != 0) break;
        n += 1;
    }
    if (n == 0) return 0;
    const a = std.heap.c_allocator;
    const forms = a.alloc(Triangle, n) catch return 0;
    defer a.free(forms);
    const z = a.alloc(f32, n * 3) catch return 0;
    defer a.free(z);
    for (tris[0..n], forms, 0..) |t, *out, i| {
        out.* = describe(t);
        for (0..3) |j| z[i * 3 + j] = t.p[j][2];
    }
    if (!og_scene_direct_models(forms.ptr, z.ptr, @intCast(n), @intCast(canvas.img.width),
        @intCast(canvas.img.height), canvas.img.pixels.ptr, canvas.depth.ptr,
        if (canvas.glow_px) |p| p.ptr else null)) return 0;
    return n;
}

fn draw(canvas: *Canvas, tris: []const Canvas.Tri) usize {
    if (tris.len != 0 and og_scene_direct_models_enabled()) return drawDirect(canvas, tris);
    if (tris.len==0 or !og_scene_enabled()) return 0;
    if (tris.len >= 256 and og_model_probe_requested()) probeModels(canvas, tris);
    var completed: usize=0;
    while (completed<tris.len) {
        var list: [256]Triangle=undefined;
        var slots: [256]Slot=undefined;
        const n=@min(list.len,tris.len-completed);
        for (list[0..n],tris[completed..][0..n]) |*out,t| out.*=describe(t);
        var pixels: ?[*]const u8=null;
        var stride: c_int=0;
        var bgra=false;
        const got=og_scene_page(&list,@intCast(n),@intCast(canvas.img.width),@intCast(canvas.img.height),&slots,&pixels,&stride,&bgra);
        if (got<=0 or got>n or pixels==null) break;
        const used: usize=@intCast(got);
        for (tris[completed..][0..used],slots[0..used]) |t,slot| composite(canvas,t,slot,pixels.?,@intCast(stride),bgra);
        completed+=used;
    }
    return completed;
}
