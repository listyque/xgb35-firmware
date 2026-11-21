#!/usr/bin/env python3
"""Build OpenGorky (Gorky 17 / Gorky 18) port for XGB35 (ATM7051H Alpine Linux ARMv7: X11 + DE_ML0 + 480x320 fb0)."""
import hashlib
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
import zlib

PORT = Path(__file__).resolve().parent
G18 = PORT / 'upstream'
SRC = PORT / 'src'
BUILD = PORT / 'build'
GEN = BUILD / 'gen'
OBJ = BUILD / 'arm-obj'
SYSROOT = BUILD / 'sysroot'
DIST = PORT / 'dist'
ALPINE_BUNDLE = PORT.parents[1] / 'recovery' / 'adfu' / 'alpine-bundle'
FFMPEG_SYSROOT = PORT.parent / 'Jellyfin_fpl' / 'build' / 'sysroot'


def apk_data_tar(raw: bytes) -> bytes:
    last = b''
    while raw:
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        last = d.decompress(raw)
        raw = d.unused_data
    return last


def prepare_sysroot() -> None:
    inc = SYSROOT / 'include'
    lib = SYSROOT / 'lib'
    lib.mkdir(parents=True, exist_ok=True)

    musl_so = ALPINE_BUNDLE / 'staging/ld-musl-armhf.so.1'
    assert musl_so.is_file(), f'Missing {musl_so}'
    shutil.copy2(musl_so, lib / 'libc.so')
    for stub in ('libdl.a', 'libpthread.a', 'libm.a', 'librt.a'):
        (lib / stub).write_bytes(b'!<arch>\n')

    if not (inc / 'X11/extensions/XShm.h').is_file():
        cache = ALPINE_BUNDLE / 'cache'
        for pat in ('xorgproto-*.apk', 'libx11-dev-*.apk', 'libxext-dev-*.apk'):
            for apk in cache.glob(pat):
                with tarfile.open(fileobj=io.BytesIO(apk_data_tar(apk.read_bytes())), mode='r:') as tf:
                    for mem in tf.getmembers():
                        if mem.isfile() and mem.name.startswith('usr/include/X11/'):
                            out = SYSROOT / mem.name[len('usr/'):]
                            out.parent.mkdir(parents=True, exist_ok=True)
                            out.write_bytes(tf.extractfile(mem).read())
    assert (inc / 'X11/extensions/XShm.h').is_file(), 'Missing XShm.h from alpine-bundle cache'
    print('[1/5] Prepared Alpine v3.20 ARMv7 musl dynamic sysroot & X11 headers')


def run_zig(cmd: list[str]) -> None:
    lcache = BUILD / 'zig-local-cache'
    gcache = BUILD / 'zig-cache'
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(gcache), ZIG_LOCAL_CACHE_DIR=str(lcache))
    if os.name == 'nt' and 'build-obj' in cmd:
        # LLVM's integrated ARM code emission crashes on this engine on Windows.
        output = next(arg for arg in cmd if arg.startswith('-femit-bin='))
        bitcode = OBJ / 'opengorky.bc'
        subprocess.run([arg for arg in cmd if arg != output] +
                       ['-fno-emit-bin', '--verbose-llvm-bc=' + str(bitcode), '-fopt-bisect-limit=0'], env=env, check=True)
        obj = Path(output.removeprefix('-femit-bin='))
        pending = obj.with_suffix('.new.o')
        subprocess.run(cmd[:cmd.index('build-obj')] + ['cc', '-target', 'arm-linux.3.10-musleabihf',
                       '-mcpu=cortex_a5', '-mfpu=neon-vfpv4', '-O2', '-fno-PIE', '-c', str(bitcode),
                       '-o', str(pending)], env=env, check=True)
        pending.replace(obj)
        return
    for attempt in range(8):
        r = subprocess.run(cmd, env=env)
        if r.returncode == 0:
            return
        print(f'  [zig retry {attempt + 1}/8 after exit {r.returncode}]')
        if r.returncode != 1:
            shutil.rmtree(lcache, ignore_errors=True)
    raise subprocess.CalledProcessError(r.returncode, cmd)


def write_if_changed(path: Path, content: str) -> None:
    data = content.encode('utf-8')
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)


def generate_zig_bindings() -> None:
    GEN.mkdir(parents=True, exist_ok=True)
    zig = [sys.executable, '-m', 'ziglang']
    sdl_zig = GEN / 'sdl_arm.zig'
    ui_zig = GEN / 'ui_arm.zig'
    sdl_header = SRC / 'sdl_xgb35.h'
    sdl_headers = (sdl_header, G18 / 'src/viewer/sdl2_compat.h')
    if not sdl_zig.is_file() or any(h.stat().st_mtime > sdl_zig.stat().st_mtime for h in sdl_headers):
        sdl_zig.write_bytes(subprocess.check_output(
            zig + ['translate-c', '-target', 'arm-linux.3.10-musleabihf', '-mcpu=cortex_a5+neon+vfp4',
                   '-I', str(G18 / 'src/viewer'), str(sdl_header)]
        ))
    if not ui_zig.is_file() or (G18 / 'src/editor/ui.h').stat().st_mtime > ui_zig.stat().st_mtime:
        ui_zig.write_bytes(subprocess.check_output(
            zig + ['translate-c', '-target', 'arm-linux.3.10-musleabihf', '-mcpu=cortex_a5+neon+vfp4',
                   '-I', str(G18 / 'third_party/clay'), '-I', str(G18 / 'third_party/stb'),
                   str(G18 / 'src/editor/ui.h')]
        ))
    ff_zig = GEN / 'ffmpeg_arm.zig'
    has_ff = (FFMPEG_SYSROOT / 'include/libavcodec/avcodec.h').is_file()
    if has_ff:
        write_if_changed(ff_zig,
            'pub extern fn og_ff_open(path: [*:0]const u8, out_w: *c_int, out_h: *c_int) ?*anyopaque;\n'
            'pub extern fn og_ff_next(handle: *anyopaque, rgba: [*]u8, w: u32, out_pts: *f64) c_int;\n'
            'pub extern fn og_ff_start_async(handle: *anyopaque, rgba: [*]u8, audio: ?*anyopaque) c_int;\n'
            'pub extern fn og_ff_poll(handle: *anyopaque, elapsed: f64) c_int;\n'
            'pub extern fn og_ff_release(handle: *anyopaque) void;\n'
            'pub extern fn og_ff_close(handle: *anyopaque) void;\n'
            'pub extern fn og_ff_audio_open(path: [*:0]const u8) ?*anyopaque;\n'
            'pub extern fn og_ff_audio_fill(handle: *anyopaque) void;\n'
            'pub extern fn og_ff_audio_mix(handle: *anyopaque, out: [*]f32, count: usize, gain: f32) void;\n'
            'pub extern fn og_ff_audio_close(handle: *anyopaque) void;\n'
        )
    write_if_changed(GEN / 'version.zig', 'pub const version: []const u8 = "xgb35";\n')
    write_if_changed(GEN / 'build_opts.zig', f'pub const xgb35_sgx = true;\npub const ffmpeg: bool = {"true" if has_ff else "false"};\n')
    print(f'[2/5] Generated ARMv7 musl Zig C-bindings (sdl, ui, ffmpeg={"ON" if has_ff else "off"})')


def build_arm_binary() -> Path:
    OBJ.mkdir(parents=True, exist_ok=True)
    zig = [sys.executable, '-m', 'ziglang']
    cc = zig + ['cc', '-target', 'arm-linux.3.10-musleabihf',
                '-mcpu=cortex_a5', '-mfpu=neon-vfpv4',
                '-O3', '-g0', '-fno-PIE', '-ffunction-sections', '-fdata-sections', '-fno-sanitize=undefined']

    c_jobs = [
        ('clay.o', G18 / 'third_party/clay/clay.c', ['-I', str(G18 / 'third_party/clay')]),
        ('stb_truetype.o', G18 / 'third_party/stb/stb_truetype.c', ['-I', str(G18 / 'third_party/stb')]),
        ('stb_image.o', G18 / 'third_party/stb/stb_image.c', ['-I', str(G18 / 'third_party/stb')]),
        ('dr_libs.o', G18 / 'third_party/dr_libs/dr_libs.c', ['-I', str(G18 / 'third_party/dr_libs')]),
        ('archive_audio.o', SRC / 'archive_audio.c', ['-I', str(G18 / 'third_party/dr_libs')]),
        ('og_platform_xgb35.o', SRC / 'og_platform_xgb35.c',
         ['-I', str(SYSROOT / 'include'), '-I', str(G18 / 'src/viewer')]),
    ]
    if (FFMPEG_SYSROOT / 'include/libavcodec/avcodec.h').is_file():
        c_jobs.append(('og_ffmpeg.o', SRC / 'og_ffmpeg.c', ['-I', str(FFMPEG_SYSROOT / 'include')]))
    for name, cfile, inc in c_jobs:
        out_o = OBJ / name
        inputs = (cfile,)
        if name in ('archive_audio.o', 'dr_libs.o'):
            inputs += tuple(G18 / 'third_party/dr_libs' / h for h in ('dr_mp3.h', 'dr_wav.h'))
        if name == 'og_platform_xgb35.o':
            inputs += (SRC / 'menu_pixels.h', SRC / 'sgx_menu.h', SRC / 'sgx_scene.h', SRC / 'sgx_sprites.h', SRC / 'sdl_xgb35.h', G18 / 'src/viewer/sdl2_compat.h')
            inputs += (PORT.parents[1] / 'xgb35-firmware/drivers/sgx540/wsegl-stream.h',)
        if not out_o.is_file() or any(p.stat().st_mtime > out_o.stat().st_mtime for p in inputs):
            print(f'  -> Compiling {name} (ARMv7 NEON+VFPv4)...')
            run_zig(cc + inc + ['-c', str(cfile), '-o', str(out_o)])

    og_obj = OBJ / 'opengorky_opt.o'
    has_ff = (GEN / 'ffmpeg_arm.zig').is_file()
    built_at = og_obj.stat().st_mtime_ns if og_obj.is_file() else 0
    engine_inputs = (G18 / 'src', G18 / 'foundation', G18 / 'assets', GEN)
    if (not built_at or any(p.stat().st_mtime_ns > built_at for p in (SRC / 'linux310_io.zig', SRC / 'sgx_scene.zig')) or
            any(p.stat().st_mtime_ns > built_at
                for directory in engine_inputs for p in directory.rglob('*') if p.is_file())):
        print('[3/5] Compiling OpenGorky Zig engine (-OReleaseFast on root+opengorky, Cortex-A5 NEON+VFPv4)...')
        # Note: only unused Clay launcher (-Mkit) stays Debug to avoid Zig 0.17.1 Windows LLVM ARM32 crash.
        deps = [
            '--dep', 'opengorky', '--dep', 'sdl', '--dep', 'build_options', '--dep', 'xgb35_scene',
            '--dep', 'icon_png', '--dep', 'panel_plate_png', '--dep', 'kit', '--dep', 'ui',
            '--dep', 'font_regular', '--dep', 'font_bold', '--dep', 'font_heading',
            '--dep', 'launcher_settings', '--dep', 'launcher_info', '--dep', 'launcher_back',
            '--dep', 'launcher_check', '--dep', 'launcher_warning', '--dep', 'launcher_play',
            '--dep', 'launcher_background', '--dep', 'version', '--dep', 'linux310_io',
        ]
        if has_ff:
            deps += ['--dep', 'ffmpeg']
        mods = [
            '-OReleaseFast', f'-Mroot={G18 / "src/viewer/main.zig"}',
            '-OReleaseFast', '--dep', 'opengorky', f'-Mxgb35_scene={SRC / "sgx_scene.zig"}',
            '-OReleaseFast', f'-Mlinux310_io={SRC / "linux310_io.zig"}',
            '-OReleaseFast', '--dep', 'foundation_traits', '--dep', 'foundation_types', '--dep', 'foundation_hosts',
            f'-Mopengorky={G18 / "src/root.zig"}',
            '-OReleaseFast', f'-Msdl={GEN / "sdl_arm.zig"}',
            f'-Mbuild_options={GEN / "build_opts.zig"}',
            f'-Micon_png={G18 / "assets/icon-256.png"}', f'-Mpanel_plate_png={G18 / "assets/panel-plate.png"}',
            '--dep', 'sdl', '--dep', 'ui', '--dep', 'opengorky', f'-Mkit={G18 / "src/editor/kit.zig"}',
            f'-Mui={GEN / "ui_arm.zig"}',
            f'-Mfont_regular={G18 / "assets/fonts/PTSans-Regular.ttf"}',
            f'-Mfont_bold={G18 / "assets/fonts/PTSans-Bold.ttf"}',
            f'-Mfont_heading={G18 / "assets/fonts/Oswald-Bold.ttf"}',
            f'-Mlauncher_settings={G18 / "assets/launcher/icons/settings.png"}',
            f'-Mlauncher_info={G18 / "assets/launcher/icons/info.png"}',
            f'-Mlauncher_back={G18 / "assets/launcher/icons/back.png"}',
            f'-Mlauncher_check={G18 / "assets/launcher/icons/check.png"}',
            f'-Mlauncher_warning={G18 / "assets/launcher/icons/warning.png"}',
            f'-Mlauncher_play={G18 / "assets/launcher/icons/play.png"}',
            f'-Mlauncher_background={G18 / "assets/launcher/textures/background.jpg"}',
            f'-Mversion={GEN / "version.zig"}',
            f'-Mfoundation_traits={G18 / "foundation/traits.ogs"}',
            f'-Mfoundation_types={G18 / "foundation/types.ogs"}',
            f'-Mfoundation_hosts={G18 / "foundation/hosts.ogs"}',
        ]
        if has_ff:
            mods += [f'-Mffmpeg={GEN / "ffmpeg_arm.zig"}']
        run_zig(zig + [
            'build-obj', '-j1', '-target', 'arm-linux.3.10-musleabihf', '-mcpu=cortex_a5+neon+vfp4',
            '-fstrip', '-fno-PIC', *deps, *mods,
            '-lc', f'-femit-bin={og_obj}'
        ])
    else:
        print('[3/5] Reusing cached opengorky_opt.o')

    out_bin = DIST / 'bin' / 'opengorky'
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    print('[4/5] Linking dynamic-musl ARMv7 executable (zero SDL2, FFmpeg + X11 dlopen)...')
    ff_libs = []
    if has_ff:
        ff_libs = [
            str(OBJ / 'og_ffmpeg.o'),
            '-Wl,-rpath,/mnt/sd/ports/Jellyfin_fpl/lib:/mnt/sd/ports/opengorky/lib',
            '-Wl,--allow-shlib-undefined',
            str(FFMPEG_SYSROOT / 'lib/libavformat.so'),
            str(FFMPEG_SYSROOT / 'lib/libavcodec.so'),
            str(FFMPEG_SYSROOT / 'lib/libswscale.so'),
            str(FFMPEG_SYSROOT / 'lib/libswresample.so'),
            str(FFMPEG_SYSROOT / 'lib/libavutil.so'),
        ]
    run_zig(cc + [
        '-no-pie',
        '-Wl,--dynamic-linker,/lib/ld-musl-armhf.so.1',
        '-Wl,--gc-sections', '-s',
        str(og_obj),
        str(OBJ / 'clay.o'), str(OBJ / 'stb_truetype.o'), str(OBJ / 'stb_image.o'),
        str(OBJ / 'dr_libs.o'), str(OBJ / 'archive_audio.o'), str(OBJ / 'og_platform_xgb35.o'),
        *ff_libs,
        str(ALPINE_BUNDLE / 'staging/libgcc_s.so.1'),
        str(SYSROOT / 'lib/libc.so'),
        '-o', str(out_bin)
    ])
    return out_bin


def verify_elf_and_bundle(out_bin: Path) -> None:
    raw = out_bin.read_bytes()
    assert raw[:6] == b'\x7fELF\x01\x01', 'Not 32-bit little-endian ELF'
    e_type, e_machine, _, _, e_phoff, _, _, _, e_phentsize, e_phnum = struct.unpack_from('<HHIIIIIHHH', raw, 16)
    assert e_machine == 40 and e_type in (2, 3), f'Unexpected ELF type/machine: {(e_type, e_machine)}'

    interp = b''
    has_dynamic = False
    for i in range(e_phnum):
        pt, po, _, _, pfs, _, _, _ = struct.unpack_from('<8I', raw, e_phoff + i * e_phentsize)
        if pt == 3:
            interp = raw[po:po + pfs]
        elif pt == 2:
            has_dynamic = True
    assert interp == b'/lib/ld-musl-armhf.so.1\x00' and has_dynamic, f'Expected dynamic musl PT_INTERP, got {interp!r}'

    for sub in ('lib', 'config/opengorky', 'data/opengorky/saves'):
        (DIST / sub).mkdir(parents=True, exist_ok=True)
    gl = ALPINE_BUNDLE.parent / 'onionos-bundle/sysroot/usr'
    zig = [sys.executable, '-m', 'ziglang']
    run_zig(zig + ['cc', '-target', 'arm-linux.3.10-musleabihf', '-Os', '-s',
                   '-I', str(SYSROOT / 'include'), str(SRC / 'refresh_desktop.c'),
                   str(gl / 'lib/libX11.so'), '-Wl,--allow-shlib-undefined',
                   '-o', str(DIST / 'bin/refresh-desktop')])
    (DIST / 'bin/refresh-desktop').chmod(0o755)
    if (gl / 'include/EGL/egl.h').is_file():
        objects = []
        for name in ('sgx_menu', 'sgx_scene', 'sgx_sprites'):
            obj = OBJ / (name + '.o')
            run_zig(zig + ['cc','-target','arm-linux.3.10-musleabihf','-mcpu=cortex_a5+neon+vfp4','-O2','-fPIC',
                           '-I',str(G18 / 'src/viewer'),'-idirafter',str(gl / 'include'),
                           '-c',str(SRC / (name + '.c')),'-o',str(obj)])
            objects.append(str(obj))
        run_zig(zig + ['ld.lld','-m','armelf_linux_eabi','-shared','-s','--no-undefined',
                       *objects,str(gl / 'lib/libEGL.so'),str(gl / 'lib/libGLESv2.so'),
                       str(SYSROOT / 'lib/libc.so'),str(ALPINE_BUNDLE / 'staging/libgcc_s.so.1'),
                       '-o',str(DIST / 'lib/opengorky-sgx.so')])
    else:
        (DIST / 'lib/opengorky-sgx.so').unlink(missing_ok=True)
    shutil.copy2(ALPINE_BUNDLE / 'staging/ld-musl-armhf.so.1', DIST / 'lib/ld-musl-armhf.so.1')
    shutil.copy2(ALPINE_BUNDLE / 'staging/libgcc_s.so.1', DIST / 'lib/libgcc_s.so.1')
    shutil.copy2(G18 / 'assets/icon-256.png', DIST / 'opengorky.png')

    # Memory-optimized default config for XGB35 (212 MB RAM, Cortex-A5):
    # All 13 keys included so Config.load never rewrites the file on startup.
    (DIST / 'config/opengorky/opengorky.cfg').write_bytes(
        b'scaling = integer\n'
        b'hires = 0\n'
        b'upscale = off\n'
        b'fullscreen = 1\n'
        b'debug = 0\n'
        b'log = warn\n'
        b'fps_limit = 60\n'
        b'fps_background = 15\n'
        b'pause_background = 1\n'
        b'console = 0\n'
        b'launcher = 0\n'
        b'game_path = /mnt/sd/ports/opengorky/game\n'
        b'package =\n'
    )

    # Handheld 3.5" LCD (/dev/fb0) launcher
    (DIST / 'OpenGorky.sh').write_bytes(
        b'#!/bin/sh\n'
        b'DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"\n'
        b'export OG_MODE=fb\n'
        b'export XDG_CONFIG_HOME="$DIR/config"\n'
        b'export XDG_DATA_HOME="$DIR/data"\n'
        b'LPATH="$DIR/lib:/mnt/sd/ports/Jellyfin_fpl/lib:/alpine/usr/lib:/alpine/lib:/usr/lib:/lib"\n'
        b'export LD_LIBRARY_PATH="$LPATH"\n'
        b'if [ -x /lib/ld-musl-armhf.so.1 ]; then\n'
        b'    exec "$DIR/bin/opengorky" "$DIR/game" --play --hires 1 --upscale off "$@" >/tmp/opengorky_last.log 2>&1\n'
        b'elif [ -x /alpine/lib/ld-musl-armhf.so.1 ]; then\n'
        b'    exec /alpine/lib/ld-musl-armhf.so.1 --library-path "$LPATH" "$DIR/bin/opengorky" "$DIR/game" --play --hires 1 --upscale off "$@" >/tmp/opengorky_last.log 2>&1\n'
        b'else\n'
        b'    exec "$DIR/lib/ld-musl-armhf.so.1" --library-path "$LPATH" "$DIR/bin/opengorky" "$DIR/game" --play --hires 1 --upscale off "$@" >/tmp/opengorky_last.log 2>&1\n'
        b'fi\n'
    )

    # HDMI Linux Desktop (X11 + DE_ML0 0-ms hardware scaler) launcher
    debug_wsegl = ALPINE_BUNDLE.parent / 'sgx-inspection/libpvrWSEGL_fbdev.so'
    private_wsegl = DIST / 'lib/frame-debug' / debug_wsegl.name
    if debug_wsegl.is_file() and b'OG_SGX_ASYNC\0' in debug_wsegl.read_bytes():
        private_wsegl.parent.mkdir(exist_ok=True)
        shutil.copy2(debug_wsegl, private_wsegl)
    else:
        private_wsegl.unlink(missing_ok=True)
    for script in ('OpenGorky-Desktop.sh', 'bench-menu.sh'):
        (DIST / script).write_bytes((SRC / script).read_bytes().replace(b'\r\n', b'\n'))
        (DIST / script).chmod(0o755)
    apk = ALPINE_BUNDLE / 'cache/xmessage-1.0.6-r0.apk'
    url = 'https://dl-cdn.alpinelinux.org/alpine/v3.20/community/armv7/' + apk.name
    raw_apk = apk.read_bytes() if apk.is_file() else urllib.request.urlopen(url, timeout=30).read()
    if hashlib.sha256(raw_apk).hexdigest() != 'fe0c27fd840ea5549250bd0ec8d57969dc8f9a83506d9abab373e93bd5afb252':
        raise ValueError('Unexpected Alpine xmessage archive')
    with tarfile.open(fileobj=io.BytesIO(apk_data_tar(raw_apk)), mode='r:') as tf:
        (DIST / 'bin/xmessage').write_bytes(tf.extractfile('usr/bin/xmessage').read())
    (DIST / 'bin/xmessage').chmod(0o755)
    shutil.copyfile(SRC / 'xmessage-COPYING', DIST / 'xmessage-COPYING')

    # XDG Desktop Entry
    (DIST / 'opengorky.desktop').write_bytes(
        b'[Desktop Entry]\n'
        b'Type=Application\n'
        b'Name=OpenGorky (Gorky 18)\n'
        b'Comment=Gorky 17 / Gorky 18 (OpenGorky Native ARMv7 NEON Port)\n'
        b'Exec=/mnt/sd/ports/opengorky/OpenGorky-Desktop.sh\n'
        b'Icon=/mnt/sd/ports/opengorky/opengorky.png\n'
        b'Terminal=false\n'
        b'Categories=Game;StrategyGame;\n'
    )

    # On-device installer script (mounts desktop.ext2 if needed, registers LXDE/JWM/Ports shortcuts)
    (DIST / 'install_on_device.sh').write_bytes(
        b'#!/bin/sh\n'
        b'set -e\n'
        b'DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"\n'
        b'chmod +x "$DIR/bin/xmessage" "$DIR/bin/opengorky" "$DIR/bin/refresh-desktop" "$DIR/lib/ld-musl-armhf.so.1" "$DIR/OpenGorky.sh" "$DIR/OpenGorky-Desktop.sh"\n'
        b'MOUNTED_DESK=0\n'
        b'if [ ! -d /desktop/usr/bin ] && [ -f /mnt/sd/linux/desktop.ext2 ]; then\n'
        b'    mkdir -p /desktop\n'
        b'    if mount -t ext2 -o loop,noatime /mnt/sd/linux/desktop.ext2 /desktop 2>/dev/null || mount -t ext4 -o loop,noatime /mnt/sd/linux/desktop.ext2 /desktop 2>/dev/null; then\n'
        b'        MOUNTED_DESK=1\n'
        b'    fi\n'
        b'fi\n'
        b'for ROOT in "" "/desktop" "/alpine"; do\n'
        b'    if [ -d "$ROOT/usr/bin" ]; then\n'
        b'        mkdir -p "$ROOT/usr/local/bin" "$ROOT/usr/share/applications" "$ROOT/root/Desktop" "$ROOT/usr/share/pixmaps" 2>/dev/null || true\n'
        b'        cp -f "$DIR/opengorky.png" "$ROOT/usr/share/pixmaps/opengorky.png" 2>/dev/null || true\n'
        b'        ln -sf "$DIR/bin/opengorky" "$ROOT/usr/local/bin/opengorky" 2>/dev/null || cp -f "$DIR/OpenGorky-Desktop.sh" "$ROOT/usr/local/bin/opengorky"\n'
        b'        cp -f "$DIR/opengorky.desktop" "$ROOT/usr/share/applications/opengorky.desktop"\n'
        b'        cp -f "$DIR/opengorky.desktop" "$ROOT/root/Desktop/OpenGorky.desktop"\n'
        b'        chmod +x "$ROOT/root/Desktop/OpenGorky.desktop"\n'
        b'        for JWMRC in "$ROOT/root/.jwmrc" "$ROOT/etc/jwm/system.jwmrc"; do\n'
        b'            if [ -f "$JWMRC" ] && ! grep -q "OpenGorky-Desktop.sh" "$JWMRC" 2>/dev/null; then\n'
        b'                sed -i "s|<RootMenu|<Program label=\\"OpenGorky (Gorky 18)\\">/mnt/sd/ports/opengorky/OpenGorky-Desktop.sh</Program>\\n<RootMenu|" "$JWMRC" 2>/dev/null || true\n'
        b'            fi\n'
        b'        done\n'
        b'    fi\n'
        b'done\n'
        b'if [ "$MOUNTED_DESK" = "1" ]; then sync; umount /desktop 2>/dev/null || true; fi\n'
        b'for PDIR in "/mnt/sd/onion/Roms/PORTS" "/mnt/sd/Roms/PORTS" "/mnt/sd/ports" "/mnt/sd/game"; do\n'
        b'    if [ -d "$PDIR" ]; then\n'
        b'        printf "#!/bin/sh\\nexec \\"%s/OpenGorky.sh\\"\\n" "$DIR" > "$PDIR/OpenGorky.sh"\n'
        b'        chmod +x "$PDIR/OpenGorky.sh"\n'
        b'    fi\n'
        b'done\n'
        b'sync\n'
        b'echo "OpenGorky ready at $DIR (Desktop shortcuts + Handheld Ports installed)!"\n'
    )

    # Host assert-based self-check
    chk_exe = BUILD / 'selfcheck.exe'
    subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-O2',
                    str(SRC / 'selfcheck.c'), '-o', str(chk_exe)], check=True)
    subprocess.run([str(chk_exe)], check=True)

    print(f'[5/5] Verified dynamic ARMv7 ELF: {out_bin} ({len(raw):,} bytes, PT_INTERP=/lib/ld-musl-armhf.so.1)')


if __name__ == '__main__':
    prepare_sysroot()
    generate_zig_bindings()
    binary = build_arm_binary()
    verify_elf_and_bundle(binary)
