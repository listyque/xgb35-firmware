//! SDL3 audio output: the main loop fills the stream queue from the mixer.

const std = @import("std");
const og = @import("opengorky");
const c = @import("sdl");
const Mixer = og.audio.mixer.Mixer;

extern fn og_audio_entry_open(c_int, u32, u32) ?*anyopaque;
extern fn og_audio_entry_mix(*anyopaque, [*]f32, usize, u32, f32, f32, c_int) c_int;
extern fn og_audio_entry_close(*anyopaque) void;

pub fn configureBank(bank: *og.audio.sound.Bank) void {
    if (@hasDecl(c, "og_xgb35_OpenMixer")) og.compose.sounds.streamArchive(bank, &.{
        .open = og_audio_entry_open, .mix = og_audio_entry_mix, .close = og_audio_entry_close,
    });
}

pub const rate = 44100;
/// Queue target: ~60 ms (low latency, slack for an uneven frame); two frames
/// when frames are rare (background, frame cap).
const min_ms = 60;

pub const AudioOut = struct {
    stream: *c.SDL_AudioStream,

    /// null — no audio device; the game runs silently.
    pub fn open(io: std.Io, mixer: *Mixer) ?AudioOut {
        if (!c.SDL_InitSubSystem(c.SDL_INIT_AUDIO)) {
            std.log.warn("audio: {s}", .{c.SDL_GetError()});
            return null;
        }
        const spec: c.SDL_AudioSpec = .{ .format = c.SDL_AUDIO_F32, .channels = 2, .freq = rate };
        const stream = if (@hasDecl(c, "og_xgb35_OpenMixer")) blk: {
            mixer.io = io;
            mixer.publishLevels();
            break :blk c.og_xgb35_OpenMixer(&spec, mixAudio, mixer);
        } else c.SDL_OpenAudioDeviceStream(c.SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, null, null);
        const device = stream orelse {
            mixer.io = null;
            std.log.warn("audio: {s}", .{c.SDL_GetError()});
            return null;
        };
        _ = c.SDL_ResumeAudioStreamDevice(device);
        return .{ .stream = device };
    }

    fn mixAudio(context: ?*anyopaque, samples: [*c]f32, count: usize) callconv(.c) void {
        const mixer: *Mixer = @ptrCast(@alignCast(context.?));
        mixer.mix(samples[0..count]);
    }

    /// Pause the device (window unfocused): the queue stops until resumed.
    pub fn setPaused(self: *AudioOut, paused: bool) void {
        _ = if (paused) c.SDL_PauseAudioStreamDevice(self.stream) else c.SDL_ResumeAudioStreamDevice(self.stream);
    }

    pub fn close(self: *AudioOut) void {
        c.SDL_DestroyAudioStream(self.stream);
    }

    /// Tops the queue up with mixed audio; `frame_ms` is the time to the next
    /// call (the target is at least two such intervals).
    pub fn pump(self: *AudioOut, mixer: *Mixer, frame_ms: u32) void {
        if (@hasDecl(c, "og_xgb35_OpenMixer")) {
            mixer.publishLevels();
            return;
        }
        const target_frames: usize = rate * @as(usize, @max(min_ms, 2 * frame_ms)) / 1000;
        var buf: [1024 * 2]f32 = undefined;
        while (true) {
            const queued_bytes = c.SDL_GetAudioStreamQueued(self.stream);
            if (queued_bytes < 0) return;
            const queued: usize = @as(usize, @intCast(queued_bytes)) / (2 * @sizeOf(f32));
            if (queued >= target_frames) return;
            const frames: usize = @min(buf.len / 2, target_frames - queued);
            const chunk = buf[0 .. frames * 2];
            mixer.mix(chunk);
            if (!c.SDL_PutAudioStreamData(self.stream, chunk.ptr, @intCast(chunk.len * @sizeOf(f32)))) return;
        }
    }
};
