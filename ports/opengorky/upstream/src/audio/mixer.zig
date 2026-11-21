//! Software mixer: voices with volume, pan and looping, stereo f32 output.
//! No output device here — the client's device only pulls ready samples.
//!
//! The original sends volume and pan to Miles `AIL_quick_set_volume`
//! (0..127, pan 0..126, center 63); here they are linear 0..1 and -1..1.
//! shortcut: Miles' volume curve (in the DLL, not the exe) is unknown, linear is
//! used; refine from a recording of the original if the difference is audible.

const std = @import("std");
const Allocator = std.mem.Allocator;
const Sound = @import("sound.zig").Sound;

pub const VoiceId = u32;

/// Volume group (settings menu: sfx / music / voice volume).
pub const Category = enum(u2) { sfx, music, voice };

const Voice = struct {
    id: VoiceId,
    sound: *const Sound,
    reader: ?*anyopaque = null,
    /// Position in source frames (fractional, for resampling).
    pos: f64 = 0,
    volume: f32 = 1,
    /// -1 left, 0 center, 1 right.
    pan: f32 = 0,
    loop: bool = false,
    category: Category = .sfx,
};

const Levels = struct {
    master: f32,
    gains: [3]f32,
    muted: [3]bool,
};

pub const Mixer = struct {
    rate: u32,
    voices: std.ArrayList(Voice) = .empty,
    next_id: VoiceId = 1,
    video_audio: ?VideoAudio = null,
    /// Master volume of effects and music.
    master: f32 = 1,
    /// Group volumes (settings), by `Category`.
    gains: [3]f32 = .{ 1, 1, 1 },
    /// Muted groups (viewer flag --no-sfx); not saved to the settings.
    muted: [3]bool = .{ false, false, false },

    io: ?std.Io = null,
    mutex: std.Io.Mutex = .init,
    output_levels: Levels = .{ .master = 1, .gains = @splat(1), .muted = @splat(false) },

    const VideoAudio = struct {
        handle: *anyopaque,
        mix: *const fn (*anyopaque, [*]f32, usize, f32) callconv(.c) void,
    };

    pub fn setVideoAudio(self: *Mixer, source: ?VideoAudio) void {
        self.lock();
        defer self.unlock();
        self.video_audio = source;
    }

    /// The render thread owns settings; publish them for the device thread.
    pub fn publishLevels(self: *Mixer) void {
        self.lock();
        defer self.unlock();
        self.output_levels = self.levels();
    }

    fn levels(self: *const Mixer) Levels {
        return .{ .master = self.master, .gains = self.gains, .muted = self.muted };
    }

    fn lock(self: *Mixer) void {
        if (self.io) |io| self.mutex.lockUncancelable(io);
    }

    fn unlock(self: *Mixer) void {
        if (self.io) |io| self.mutex.unlock(io);
    }

    pub fn init(rate: u32) Mixer {
        return .{ .rate = rate };
    }

    pub fn deinit(self: *Mixer, gpa: Allocator) void {
        while (self.voices.items.len > 0) self.removeVoice(self.voices.items.len - 1);
        self.voices.deinit(gpa);
    }

    /// `sound` must outlive the voice.
    /// Effect channels in the exe (0x63bd28): when all are busy a new sound does not play.
    const max_sfx = 16;

    pub fn play(self: *Mixer, gpa: Allocator, sound: *const Sound, volume: f32, pan: f32, loop: bool) Allocator.Error!VoiceId {
        return self.playAs(gpa, .sfx, sound, volume, pan, loop);
    }

    /// Whether an effect channel is free (0x4327f0).
    pub fn sfxFree(self: *Mixer) bool {
        self.lock();
        defer self.unlock();
        var n: usize = 0;
        for (self.voices.items) |v| {
            if (v.category == .sfx) n += 1;
        }
        return n < max_sfx;
    }

    pub fn playAs(self: *Mixer, gpa: Allocator, category: Category, sound: *const Sound, volume: f32, pan: f32, loop: bool) Allocator.Error!VoiceId {
        // Speech in the exe is one MP3 channel (0x40eed0 mutes it first, 0x40ec70): a new
        // line cuts the previous one, even one started before a level change.
        const reader = if (sound.stream) |s| s.api.open(s.fd, s.offset, s.size) else null;
        errdefer if (reader) |r| sound.stream.?.api.close(r);
        self.lock();
        defer self.unlock();
        const id = self.next_id;
        self.next_id +%= 1;
        if (sound.stream != null and reader == null) return id;
        try self.voices.ensureUnusedCapacity(gpa, 1);
        if (category == .voice) self.stopCategoryLocked(.voice);
        self.voices.appendAssumeCapacity(.{ .id = id, .sound = sound, .reader = reader, .volume = volume, .pan = pan, .loop = loop, .category = category });
        return id;
    }

    pub fn hasVoice(self: *Mixer, id: VoiceId) bool {
        self.lock();
        defer self.unlock();
        return self.find(id) != null;
    }

    pub fn setVoice(self: *Mixer, id: VoiceId, volume: f32, pan: ?f32, release_loop: bool) bool {
        self.lock();
        defer self.unlock();
        const v = self.find(id) orelse return false;
        v.volume = volume;
        if (pan) |p| v.pan = p;
        if (release_loop) v.loop = false;
        return true;
    }

    fn find(self: *Mixer, id: VoiceId) ?*Voice {
        for (self.voices.items) |*v| if (v.id == id) return v;
        return null;
    }

    /// Stop all voices of a category.
    pub fn stopCategory(self: *Mixer, category: Category) void {
        self.lock();
        defer self.unlock();
        self.stopCategoryLocked(category);
    }

    fn removeVoice(self: *Mixer, index: usize) void {
        const v = self.voices.swapRemove(index);
        if (v.reader) |r| v.sound.stream.?.api.close(r);
    }

    fn stopCategoryLocked(self: *Mixer, category: Category) void {
        var i: usize = 0;
        while (i < self.voices.items.len) {
            if (self.voices.items[i].category == category) self.removeVoice(i) else i += 1;
        }
    }

    pub fn stop(self: *Mixer, id: VoiceId) void {
        self.lock();
        defer self.unlock();
        for (self.voices.items, 0..) |v, i| if (v.id == id) {
            self.removeVoice(i);
            return;
        };
    }

    /// Mixes into `out` (stereo, interleaved L R) and advances the voices.
    pub fn mix(self: *Mixer, out: []f32) void {
        self.lock();
        defer self.unlock();
        const controls = if (self.io == null) self.levels() else self.output_levels;
        @memset(out, 0);
        const frames = out.len / 2;
        var i: usize = 0;
        while (i < self.voices.items.len) {
            const v = &self.voices.items[i];
            const c = @backingInt(v.category);
            const gain: f32 = if (controls.muted[c]) 0 else controls.master * controls.gains[c];
            const finished = mixVoice(v, out[0 .. frames * 2], self.rate, gain);
            if (finished) self.removeVoice(i) else i += 1;
        }
        if (self.video_audio) |source| {
            const music = @backingInt(Category.music);
            source.mix(source.handle, out.ptr, frames * 2, if (controls.muted[music]) 0 else controls.master * controls.gains[music]);
        }
        for (out) |*s| s.* = std.math.clamp(s.*, -1, 1);
    }

    /// Returns true when the voice has finished.
    fn mixVoice(v: *Voice, out: []f32, out_rate: u32, master: f32) bool {
        const snd = v.sound;
        // Linear pan law: center gives 1.0 to both channels, as in Miles.
        const gain = v.volume * master / 32768.0;
        const left = gain * @min(1, 1 - v.pan);
        const right = gain * @min(1, 1 + v.pan);
        if (v.reader) |r| return snd.stream.?.api.mix(r, out.ptr, out.len / 2, out_rate, left, right, @intFromBool(v.loop)) != 0;
        const total = snd.frames();
        if (total == 0) return true;
        const step = @as(f64, @floatFromInt(snd.rate)) / @as(f64, @floatFromInt(out_rate));
        var f: usize = 0;
        while (f < out.len / 2) : (f += 1) {
            if (v.pos >= @as(f64, @floatFromInt(total))) {
                if (!v.loop) return true;
                // The step may exceed the length (short sound, rate above the output).
                v.pos = @mod(v.pos, @as(f64, @floatFromInt(total)));
            }
            const idx: usize = @intFromFloat(v.pos);
            const frac: f32 = @floatCast(v.pos - @as(f64, @floatFromInt(idx)));
            const next = if (idx + 1 < total) idx + 1 else if (v.loop) 0 else idx;
            const l0, const r0 = frameAt(snd, idx);
            const l1, const r1 = frameAt(snd, next);
            out[f * 2] += (l0 + (l1 - l0) * frac) * left;
            out[f * 2 + 1] += (r0 + (r1 - r0) * frac) * right;
            v.pos += step;
        }
        return false;
    }

    fn frameAt(snd: *const Sound, idx: usize) struct { f32, f32 } {
        if (snd.channels == 1) {
            const s: f32 = @floatFromInt(snd.samples[idx]);
            return .{ s, s };
        }
        return .{ @floatFromInt(snd.samples[idx * 2]), @floatFromInt(snd.samples[idx * 2 + 1]) };
    }
};

test "video audio follows music gain and detaches before close" {
    const source = struct {
        fn mix(handle: *anyopaque, out: [*]f32, len: usize, gain: f32) callconv(.c) void {
            const count: *usize = @ptrCast(@alignCast(handle));
            count.* += len;
            for (out[0..len]) |*sample| sample.* += gain;
        }
    };
    var m: Mixer = .init(44100);
    var count: usize = 0;
    var out: [4]f32 = undefined;
    m.gains[@backingInt(Category.music)] = 0.5;
    m.setVideoAudio(.{ .handle = &count, .mix = source.mix });
    m.mix(&out);
    try std.testing.expectEqual(@as(f32, 0.5), out[1]);
    m.muted[@backingInt(Category.music)] = true;
    m.mix(&out);
    try std.testing.expectEqual(@as(f32, 0), out[1]);
    m.setVideoAudio(null);
    m.mix(&out);
    try std.testing.expectEqual(@as(usize, 8), count);
}

test "mix mono voice with pan and finish" {
    const gpa = std.testing.allocator;
    var samples = [_]i16{ 16384, 16384 };
    const snd: Sound = .{ .channels = 1, .rate = 100, .samples = &samples };
    var m: Mixer = .init(100);
    defer m.deinit(gpa);
    _ = try m.play(gpa, &snd, 1, 1, false); // full right
    var out: [8]f32 = undefined;
    m.mix(&out);
    try std.testing.expectEqual(@as(f32, 0), out[0]);
    try std.testing.expectEqual(@as(f32, 0.5), out[1]);
    try std.testing.expectEqual(@as(f32, 0.5), out[3]);
    try std.testing.expectEqual(@as(f32, 0), out[5]); // sound ended
    try std.testing.expectEqual(0, m.voices.items.len);
}

test "looping voice keeps playing; resampling halves step" {
    const gpa = std.testing.allocator;
    var samples = [_]i16{ 0, 32767 };
    const snd: Sound = .{ .channels = 1, .rate = 50, .samples = &samples };
    var m: Mixer = .init(100);
    defer m.deinit(gpa);
    const id = try m.play(gpa, &snd, 1, 0, true);
    var out: [16]f32 = undefined;
    m.mix(&out);
    try std.testing.expect(m.find(id) != null);
    // Frame 1 is halfway between 0 and 32767.
    try std.testing.expectApproxEqAbs(@as(f32, 0.5), out[2], 0.01);
    m.stop(id);
    try std.testing.expect(m.find(id) == null);
}

test "looping voice with step longer than the sound" {
    const gpa = std.testing.allocator;
    var samples = [_]i16{1000};
    const snd: Sound = .{ .channels = 1, .rate = 300, .samples = &samples };
    var m: Mixer = .init(100);
    defer m.deinit(gpa);
    _ = try m.play(gpa, &snd, 1, 0, true);
    var out: [8]f32 = undefined;
    m.mix(&out); // step 3 with length 1: without @mod this reads past the end
    try std.testing.expect(out[2] > 0);
}

test "new speech replaces the previous one" {
    const gpa = std.testing.allocator;
    var samples = [_]i16{ 1, 2, 3 };
    const snd: Sound = .{ .channels = 1, .rate = 100, .samples = &samples };
    var m: Mixer = .init(100);
    defer m.deinit(gpa);
    const a = try m.playAs(gpa, .voice, &snd, 1, 0, false);
    const b = try m.playAs(gpa, .voice, &snd, 1, 0, false);
    try std.testing.expect(m.find(a) == null);
    try std.testing.expect(m.find(b) != null);
}

test "device mixing uses published volume and safely releases looping voices" {
    const gpa = std.testing.allocator;
    var samples = [_]i16{16384};
    const snd: Sound = .{ .channels = 1, .rate = 100, .samples = &samples };
    var m: Mixer = .init(100);
    defer m.deinit(gpa);
    m.io = std.testing.io;
    m.publishLevels();
    const id = try m.play(gpa, &snd, 1, 0, true);
    var out: [4]f32 = undefined;
    m.master = 0;
    m.mix(&out);
    try std.testing.expectEqual(@as(f32, 0.5), out[0]);
    m.publishLevels();
    try std.testing.expect(m.setVoice(id, 1, null, true));
    m.mix(&out);
    try std.testing.expectEqual(@as(f32, 0), out[0]);
    try std.testing.expect(!m.hasVoice(id));
}
