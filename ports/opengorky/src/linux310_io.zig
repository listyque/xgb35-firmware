//! Linux 3.10 runtime bootstrap & std.Io.VTable wrapper:
//! 1. Uses std.heap.c_allocator instead of Debug SafeAllocator (saves RAM, avoids ENOSYS).
//! 2. Replaces statx-based stat/length calls (ENOSYS on Linux < 4.11) with POSIX fstat/fstatat.
const std = @import("std");
const Io = std.Io;
const Dir = Io.Dir;
const File = Io.File;

const OgStat310 = extern struct {
    ino: u64,
    nlink: u64,
    size: u64,
    mode: u32,
    blksize: u32,
    atime_sec: i64,
    atime_nsec: i64,
    mtime_sec: i64,
    mtime_nsec: i64,
    ctime_sec: i64,
    ctime_nsec: i64,
};

extern fn og_linux310_fstat(fd: c_int, out: *OgStat310) c_int;
extern fn og_linux310_fstatat(dirfd: c_int, path: [*:0]const u8, follow_symlinks: c_int, out: *OgStat310) c_int;
extern fn og_linux310_renameat(olddirfd: c_int, oldpath: [*:0]const u8, newdirfd: c_int, newpath: [*:0]const u8) c_int;
extern fn og_linux310_pread(fd: c_int, buf: [*]u8, count: usize, offset: i64) isize;
extern fn og_linux310_pwrite(fd: c_int, buf: [*]const u8, count: usize, offset: i64) isize;
extern fn og_linux310_faccessat(dirfd: c_int, path: [*:0]const u8, mode: c_int) c_int;
extern fn getrandom(buf: [*]u8, buflen: usize, flags: c_uint) isize;

var g_vtable: Io.VTable = undefined;

fn modeToKind(mode: u32) File.Kind {
    return switch (mode & 0o170000) {
        0o040000 => .directory,
        0o100000 => .file,
        0o120000 => .sym_link,
        0o010000 => .named_pipe,
        0o020000 => .character_device,
        0o060000 => .block_device,
        0o140000 => .unix_domain_socket,
        else => .unknown,
    };
}

fn toTs(sec: i64, nsec: i64) Io.Timestamp {
    return .{ .nanoseconds = @as(i96, sec) * std.time.ns_per_s + @as(i96, nsec) };
}

fn toFileStat(s: *const OgStat310) File.Stat {
    return .{
        .inode = @intCast(s.ino),
        .nlink = @intCast(s.nlink),
        .size = s.size,
        .permissions = .fromMode(@intCast(s.mode & 0o7777)),
        .kind = modeToKind(s.mode),
        .atime = toTs(s.atime_sec, s.atime_nsec),
        .mtime = toTs(s.mtime_sec, s.mtime_nsec),
        .ctime = toTs(s.ctime_sec, s.ctime_nsec),
        .block_size = @intCast(s.blksize),
    };
}

fn dirStat310(userdata: ?*anyopaque, dir: Dir) Dir.StatError!Dir.Stat {
    _ = userdata;
    var s: OgStat310 = undefined;
    const rc = og_linux310_fstat(dir.handle, &s);
    if (rc < 0) return switch (-rc) {
        13 => error.AccessDenied,
        12 => error.SystemResources,
        else => error.Unexpected,
    };
    return toFileStat(&s);
}

fn dirStatFile310(userdata: ?*anyopaque, dir: Dir, sub_path: []const u8, options: Dir.StatFileOptions) Dir.StatFileError!File.Stat {
    _ = userdata;
    var buf: [4096]u8 = undefined;
    if (sub_path.len >= buf.len) return error.NameTooLong;
    @memcpy(buf[0..sub_path.len], sub_path);
    buf[sub_path.len] = 0;
    var s: OgStat310 = undefined;
    const rc = og_linux310_fstatat(dir.handle, @ptrCast(&buf), if (options.follow_symlinks) 1 else 0, &s);
    if (rc < 0) return switch (-rc) {
        2 => error.FileNotFound,
        13 => error.AccessDenied,
        20 => error.NotDir,
        36 => error.NameTooLong,
        40 => error.SymLinkLoop,
        12 => error.SystemResources,
        else => error.Unexpected,
    };
    return toFileStat(&s);
}

// Threaded's path creation calls statx directly when mkdirat hits an existing directory.
fn dirCreateDirPath310(userdata: ?*anyopaque, dir: Dir, sub_path: []const u8, permissions: Dir.Permissions) Dir.CreateDirPathError!Dir.CreatePathStatus {
    var it = Dir.path.componentIterator(sub_path);
    var component = it.last() orelse return error.BadPathName;
    var status: Dir.CreatePathStatus = .existed;
    while (true) {
        if (g_vtable.dirCreateDir(userdata, dir, component.path, permissions)) |_| {
            status = .created;
        } else |err| switch (err) {
            error.PathAlreadyExists => {
                const stat = try dirStatFile310(userdata, dir, component.path, .{ .follow_symlinks = false });
                if (stat.kind != .directory) return error.NotDir;
            },
            error.FileNotFound => {
                component = it.previous() orelse return err;
                continue;
            },
            else => return err,
        }
        component = it.next() orelse return status;
    }
}

fn fileStat310(userdata: ?*anyopaque, file: File) File.StatError!File.Stat {
    _ = userdata;
    var s: OgStat310 = undefined;
    const rc = og_linux310_fstat(file.handle, &s);
    if (rc < 0) return switch (-rc) {
        13 => error.AccessDenied,
        12 => error.SystemResources,
        else => error.Unexpected,
    };
    return toFileStat(&s);
}

fn fileLength310(userdata: ?*anyopaque, file: File) File.LengthError!u64 {
    _ = userdata;
    var s: OgStat310 = undefined;
    const rc = og_linux310_fstat(file.handle, &s);
    if (rc < 0) return switch (-rc) {
        12 => error.SystemResources,
        else => error.Unexpected,
    };
    return s.size;
}

fn random310(userdata: ?*anyopaque, buffer: []u8) void {
    _ = userdata;
    var off: usize = 0;
    while (off < buffer.len) {
        const n = getrandom(buffer.ptr + off, buffer.len - off, 0);
        if (n > 0) {
            off += @intCast(n);
        } else {
            @memset(buffer[off..], 0x5A);
            break;
        }
    }
}

fn randomSecure310(userdata: ?*anyopaque, buffer: []u8) Io.RandomSecureError!void {
    random310(userdata, buffer);
}

fn dirRename310(userdata: ?*anyopaque, old_dir: Dir, old_sub_path: []const u8, new_dir: Dir, new_sub_path: []const u8) Dir.RenameError!void {
    _ = userdata;
    var obuf: [4096]u8 = undefined;
    var nbuf: [4096]u8 = undefined;
    if (old_sub_path.len >= obuf.len or new_sub_path.len >= nbuf.len) return error.NameTooLong;
    @memcpy(obuf[0..old_sub_path.len], old_sub_path);
    obuf[old_sub_path.len] = 0;
    @memcpy(nbuf[0..new_sub_path.len], new_sub_path);
    nbuf[new_sub_path.len] = 0;
    const rc = og_linux310_renameat(old_dir.handle, @ptrCast(&obuf), new_dir.handle, @ptrCast(&nbuf));
    if (rc < 0) return switch (-rc) {
        2 => error.FileNotFound,
        13 => error.AccessDenied,
        16 => error.FileBusy,
        18 => error.CrossDevice,
        20 => error.NotDir,
        21 => error.IsDir,
        28 => error.NoSpaceLeft,
        30 => error.ReadOnlyFileSystem,
        36 => error.NameTooLong,
        39 => error.DirNotEmpty,
        40 => error.SymLinkLoop,
        12 => error.SystemResources,
        else => error.Unexpected,
    };
}

fn fileReadPositional310(userdata: ?*anyopaque, file: File, data: []const []u8, offset: u64) File.ReadPositionalError!usize {
    _ = userdata;
    var total: usize = 0;
    var off: i64 = @intCast(offset);
    for (data) |buf| {
        if (buf.len == 0) continue;
        while (true) {
            const n = og_linux310_pread(file.handle, buf.ptr, buf.len, off);
            if (n >= 0) {
                const got: usize = @intCast(n);
                total += got;
                off += @intCast(got);
                if (got < buf.len) return total;
                break;
            }
            if (-n == 4) continue; // EINTR
            return switch (-n) {
                5 => error.InputOutput,
                12 => error.SystemResources,
                13 => error.AccessDenied,
                21 => error.IsDir,
                29 => error.Unseekable,
                else => error.Unexpected,
            };
        }
    }
    return total;
}

fn fileWritePositional310(userdata: ?*anyopaque, file: File, header: []const u8, data: []const []const u8, splat: usize, offset: u64) File.WritePositionalError!usize {
    _ = userdata;
    var total: usize = 0;
    var off: i64 = @intCast(offset);
    if (header.len > 0) {
        const n = og_linux310_pwrite(file.handle, header.ptr, header.len, off);
        if (n < 0) return error.Unexpected;
        total += @intCast(n);
        off += @intCast(n);
    }
    if (data.len > 0) {
        for (data[0 .. data.len - 1]) |buf| {
            if (buf.len == 0) continue;
            const n = og_linux310_pwrite(file.handle, buf.ptr, buf.len, off);
            if (n < 0) return error.Unexpected;
            total += @intCast(n);
            off += @intCast(n);
        }
        const last = data[data.len - 1];
        if (last.len > 0) {
            for (0..splat) |_| {
                const n = og_linux310_pwrite(file.handle, last.ptr, last.len, off);
                if (n < 0) return error.Unexpected;
                total += @intCast(n);
                off += @intCast(n);
            }
        }
    }
    return total;
}

fn dirAccess310(userdata: ?*anyopaque, dir: Dir, sub_path: []const u8, options: Dir.AccessOptions) Dir.AccessError!void {
    _ = userdata;
    var buf: [4096]u8 = undefined;
    if (sub_path.len >= buf.len) return error.NameTooLong;
    @memcpy(buf[0..sub_path.len], sub_path);
    buf[sub_path.len] = 0;
    var mode: c_int = 0; // F_OK
    if (options.read) mode |= 4;
    if (options.write) mode |= 2;
    if (options.execute) mode |= 1;
    const rc = og_linux310_faccessat(dir.handle, @ptrCast(&buf), mode);
    if (rc < 0) return switch (-rc) {
        2 => error.FileNotFound,
        13 => error.AccessDenied,
        20 => error.FileNotFound,
        30 => error.ReadOnlyFileSystem,
        36 => error.NameTooLong,
        40 => error.SymLinkLoop,
        12 => error.SystemResources,
        else => error.Unexpected,
    };
}

pub fn runWithMinimal(minimal: std.process.Init.Minimal, comptime runFn: fn (std.process.Init) anyerror!void) !void {
    const gpa = std.heap.c_allocator;
    var arena_alloc = std.heap.ArenaAllocator.init(gpa);
    defer arena_alloc.deinit();

    var threaded: std.Io.Threaded = .init(gpa, .{
        .argv0 = .init(minimal.args),
        .environ = minimal.environ,
    });
    defer threaded.deinit();

    var environ_map = try minimal.environ.createMap(gpa);
    defer environ_map.deinit();

    const base_io = threaded.io();
    g_vtable = base_io.vtable.*;
    g_vtable.dirStat = dirStat310;
    g_vtable.dirStatFile = dirStatFile310;
    g_vtable.dirCreateDirPath = dirCreateDirPath310;
    g_vtable.dirAccess = dirAccess310;
    g_vtable.dirRename = dirRename310;
    g_vtable.fileStat = fileStat310;
    g_vtable.fileLength = fileLength310;
    g_vtable.fileReadPositional = fileReadPositional310;
    g_vtable.fileWritePositional = fileWritePositional310;
    g_vtable.random = random310;
    g_vtable.randomSecure = randomSecure310;

    return runFn(.{
        .minimal = minimal,
        .arena = &arena_alloc,
        .gpa = gpa,
        .io = .{ .userdata = base_io.userdata, .vtable = &g_vtable },
        .environ_map = &environ_map,
        .preopens = .empty,
    });
}
