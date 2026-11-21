//! opengorky-viewer: the game (`--play`) and a viewer for sprites, backgrounds and levels.
//! Keys: app/keys.zig.
const std = @import("std");
const viewer = @import("app/viewer.zig");
const run = @import("app/run.zig");

pub const std_options: std.Options = .{ .logFn = logFn };

fn logFn(comptime level: std.log.Level, comptime scope: @EnumLiteral(), comptime format: []const u8, args: anytype) void {
    const allowed = switch (viewer.log_level) {
        .none => false,
        .warn => @backingInt(level) <= @backingInt(std.log.Level.warn),
        .info => @backingInt(level) <= @backingInt(std.log.Level.info),
    };
    if (allowed) std.log.defaultLog(level, scope, format, args);
}

pub const main = if (@import("builtin").os.tag == .linux) mainLinux else mainDefault;

fn mainDefault(init: std.process.Init) !void {
    return run.run(init);
}

fn mainLinux(minimal: std.process.Init.Minimal) !void {
    if (@hasDecl(@import("build_options"), "xgb35_sgx")) @import("xgb35_scene").install();
    return @import("linux310_io").runWithMinimal(minimal, run.run);
}
