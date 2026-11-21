#!/usr/bin/env python3
"""Build StarDroid 1.9.11 port for XGB35 (ATM7051H Alpine Linux ARMv7: X11 Desktop + 480x320 fb0)."""
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import zlib

PORT = Path(__file__).resolve().parent
UPSTREAM = PORT / 'upstream'
STAGE = UPSTREAM / 'native-engine'
SRC = PORT / 'src'
BUILD = PORT / 'build'
OBJ = BUILD / 'arm-obj'
SYSROOT = BUILD / 'sysroot'
DIST = PORT / 'dist'
ALPINE_BUNDLE = PORT.parents[1] / 'recovery' / 'adfu' / 'alpine-bundle'


def apk_data_tar(raw: bytes) -> bytes:
    last = b''
    while raw:
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        last = d.decompress(raw)
        raw = d.unused_data
    return last


def generate_missions_header() -> None:
    missions = json.loads((UPSTREAM / 'app/src/main/assets/native/missions.json').read_text('utf-8'))
    after_cinematics = {
        2: ('smk\\\\terran1.smk', 'Wasteland Patrol'),
        5: ('smk\\\\terran2.smk', 'The Downing of the Norad II'),
        9: ('smk\\\\terran3.smk', 'Open Rebellion'),
        12: ('smk\\\\terran4.smk', 'The Inauguration'),
        103: ('smk\\\\zerg1.smk', 'The Dream'),
        105: ('smk\\\\zerg2.smk', 'Battle on the Amerigo'),
        108: ('smk\\\\zerg3.smk', 'The Warp'),
        110: ('smk\\\\zerg4.smk', 'The Invasion of Aiur'),
        203: ('smk\\\\protoss1.smk', 'The Fall of Fenix'),
        205: ('smk\\\\protoss2.smk', 'The Ambush'),
        207: ('smk\\\\protoss3.smk', 'The Return to Aiur'),
        210: ('smk\\\\protoss4.smk', 'The Death of the Overmind'),
        308: ('smk\\\\xprotoss.smk', 'The Destruction of Shakuras'),
        408: ('smk\\\\xterran.smk', 'UED Victory Report'),
        510: ('smk\\\\xzerg.smk', 'The Ascension'),
    }
    lines = [
        '// Auto-generated from StarDroid 1.9.11 missions.json + CinematicCatalog.java',
        '#pragma once',
        'struct MissionInfo {',
        '    int code; int episode; int number; int race; int player; bool expansion;',
        '    const char* id; const char* prefix; const char* title; const char* opening;',
        '    int next_code; const char* after_smk; const char* after_title;',
        '};',
        'static const MissionInfo kMissions[] = {',
    ]
    kept = 0
    for m in missions:
        if m.get('hidden'):
            continue
        p = m['prefix']
        ep = (0 if '/terran/' in p else 1 if '/zerg/' in p else 2 if '/protoss/' in p
              else 3 if '/expprotoss/' in p else 4 if '/expterran/' in p else 5)
        exp = 'true' if (m.get('game') == 'broodwar' or '/exp' in p) else 'false'
        race = int(m.get('race', 1))
        player = int(m.get('player', 0))
        mid = str(m.get('id', '')).replace('"', '\\"')
        pref = p.replace('"', '\\"')
        title = m['title'].replace('"', '\\"')
        op = (m.get('opening') or '').replace('"', '\\"')
        nxt = int(m.get('next') or 0)
        raw_num = m.get('number', 0)
        num = int(raw_num) if str(raw_num).isdigit() else 99
        asmk, atitle = after_cinematics.get(int(m['code']), ('', ''))
        lines.append(
            f'    {{{m["code"]}, {ep}, {num}, {race}, {player}, {exp}, '
            f'"{mid}", "{pref}", "{title}", "{op}", {nxt}, "{asmk}", "{atitle}"}},'
        )
        kept += 1
    lines.append('};')
    lines.append(f'static constexpr int kMissionCount = {kept};')
    (SRC / 'missions_table.h').write_text('\n'.join(lines) + '\n', 'utf-8')
    print(f'[1/5] Generated missions_table.h ({kept} playable missions)')


def prepare_sysroot() -> None:
    inc = SYSROOT / 'include'
    lib = SYSROOT / 'lib'
    lib.mkdir(parents=True, exist_ok=True)

    # Copy Alpine v3.20 armv7 musl dynamic linker/libc so dlopen("libX11.so.6") works at runtime!
    musl_so = ALPINE_BUNDLE / 'staging/ld-musl-armhf.so.1'
    assert musl_so.is_file(), f'Missing {musl_so}'
    shutil.copy2(musl_so, lib / 'libc.so')
    for stub in ('libdl.a', 'libpthread.a', 'libm.a', 'librt.a'):
        # Empty AR archive header so -ldl/-lpthread/-lm resolve directly against musl libc.so
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
    print('[2/5] Prepared Alpine v3.20 ARMv7 musl dynamic sysroot & X11 headers')


def run_zig(cmd: list[str]) -> None:
    for attempt in range(3):
        r = subprocess.run(cmd)
        if r.returncode == 0:
            return
        print(f'  [zig retry {attempt + 1}/3 after exit {r.returncode}]')
    raise subprocess.CalledProcessError(r.returncode, cmd)


def build_arm_binary() -> Path:
    if not STAGE.is_dir():
        env = dict(os.environ, PATH=r'C:\Program Files\Git\usr\bin;' + os.environ.get('PATH', ''))
        subprocess.run([sys.executable, str(UPSTREAM / 'scripts/prepare_native.py')], check=True, env=env)

    OBJ.mkdir(parents=True, exist_ok=True)
    neon_stamp = OBJ / '.neon_vfpv4'
    if not neon_stamp.is_file():
        for stale in OBJ.glob('*.o'):
            stale.unlink()

    cc = [sys.executable, '-m', 'ziglang', 'cc', '-target', 'arm-linux-musleabihf',
          '-mcpu=cortex_a5', '-mfpu=neon-vfpv4',
          '-O2', '-g0', '-fno-PIE', '-ffunction-sections', '-fdata-sections', '-fno-sanitize=undefined']
    cxx = [sys.executable, '-m', 'ziglang', 'c++', '-target', 'arm-linux-musleabihf',
           '-mcpu=cortex_a5', '-mfpu=neon-vfpv4',
           '-std=c++17', '-O2', '-g0', '-fno-PIE',
           '-ffunction-sections', '-fdata-sections', '-fno-sanitize=undefined',
           '-Wno-deprecated-declarations', '-Wno-nontrivial-memcall']

    inc_engine = ['-DOPENBW_ENABLE_UI', '-DOPENBW_NO_SDL_IMAGE', '-DOPENBW_NO_SDL_MIXER',
                  '-I' + str(SRC), '-I' + str(UPSTREAM / 'native'),
                  '-I' + str(STAGE), '-I' + str(STAGE / 'ui'), '-I' + str(STAGE / 'web')]

    if not (OBJ / 'smacker.o').is_file():
        print('  -> Compiling smacker.c (ARMv7 NEON+VFPv4)...')
        run_zig(cc + ['-c', str(UPSTREAM / 'vendor/libsmacker/smacker.c'), '-o', str(OBJ / 'smacker.o')])
    if not (OBJ / 'wasm_backend.o').is_file():
        print('  -> Compiling wasm_backend.cpp (ARMv7 NEON+VFPv4)...')
        run_zig(cxx + inc_engine + ['-c', str(UPSTREAM / 'native/generated/wasm_backend.cpp'), '-o', str(OBJ / 'wasm_backend.o')])
    engine_deps = (SRC / 'engine.cpp', SRC / 'engine_api.h', UPSTREAM / 'native/runtime.cpp')
    if not (OBJ / 'engine.o').is_file() or any(d.stat().st_mtime > (OBJ / 'engine.o').stat().st_mtime for d in engine_deps):
        print('  -> Compiling engine.cpp (ARMv7 NEON+VFPv4)...')
        run_zig(cxx + inc_engine + ['-c', str(SRC / 'engine.cpp'), '-o', str(OBJ / 'engine.o')])
    neon_stamp.write_text('ok\n', 'utf-8')

    print('[3/5] Compiling frontend main.cpp for ARMv7 musl...')
    run_zig(cxx + ['-I' + str(SRC), '-I' + str(SYSROOT / 'include'),
                   '-c', str(SRC / 'main.cpp'), '-o', str(OBJ / 'main.o')])

    out_bin = DIST / 'bin' / 'stardroid'
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    print('[4/5] Linking dynamic-musl ARMv7 executable (static libc++, dlopen-capable)...')
    run_zig(cxx + [
        '-no-pie',
        '-Wl,--dynamic-linker,/lib/ld-musl-armhf.so.1',
        '-Wl,--gc-sections', '-s',
        str(OBJ / 'main.o'), str(OBJ / 'engine.o'), str(OBJ / 'wasm_backend.o'), str(OBJ / 'smacker.o'),
        str(ALPINE_BUNDLE / 'staging/libgcc_s.so.1'),
        str(SYSROOT / 'lib/libc.so'),
        '-o', str(out_bin)
    ])
    return out_bin


def verify_elf_and_bundle(out_bin: Path) -> None:
    raw = out_bin.read_bytes()
    assert raw[:6] == b'\x7fELF\x01\x01', 'Not 32-bit little-endian ELF'
    e_type, e_machine, _, _, e_phoff, _, e_flags, _, e_phentsize, e_phnum = struct.unpack_from('<HHIIIIIHHH', raw, 16)
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

    # Bundle directories, bundled map, and fallback musl loader for non-chroot initramfs mode
    for sub in ('maps', 'data', 'saves', 'replays', 'lib'):
        (DIST / sub).mkdir(parents=True, exist_ok=True)
    shutil.copy2(PORT / 'maps/Weave_v1.scx', DIST / 'maps/Weave_v1.scx')
    shutil.copy2(ALPINE_BUNDLE / 'staging/ld-musl-armhf.so.1', DIST / 'lib/ld-musl-armhf.so.1')
    shutil.copy2(ALPINE_BUNDLE / 'staging/libgcc_s.so.1', DIST / 'lib/libgcc_s.so.1')
    if (SRC / 'pylon-launcher.svg').is_file():
        shutil.copy2(SRC / 'pylon-launcher.svg', DIST / 'stardroid.svg')

    # Handheld / Small-Screen port launcher (works in Alpine, OnionOS Ports, SuperX, or raw initramfs)
    (DIST / 'StarDroid.sh').write_bytes(
        b'#!/bin/sh\n'
        b'DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"\n'
        b'if [ -x /lib/ld-musl-armhf.so.1 ]; then\n'
        b'    exec "$DIR/bin/stardroid" --fb "$@"\n'
        b'elif [ -x /alpine/lib/ld-musl-armhf.so.1 ]; then\n'
        b'    exec /alpine/lib/ld-musl-armhf.so.1 --library-path /alpine/usr/lib:/alpine/lib "$DIR/bin/stardroid" --fb "$@"\n'
        b'else\n'
        b'    exec "$DIR/lib/ld-musl-armhf.so.1" --library-path "$DIR/lib:/usr/lib:/lib" "$DIR/bin/stardroid" --fb "$@"\n'
        b'fi\n'
    )

    # HDMI Linux Desktop (X11) launcher — fullscreen by default so 720p gets exact 960x720 smooth 2->3 and 1080p gets exact 2x 1280x960!
    (DIST / 'StarDroid-Desktop.sh').write_bytes(
        b'#!/bin/sh\n'
        b'DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"\n'
        b'export DISPLAY="${DISPLAY:-:0}"\n'
        b'exec "$DIR/bin/stardroid" --x11 -f "$@" >/tmp/stardroid_last.log 2>&1\n'
    )

    # XDG Desktop Entry for LXDE / Openbox / JWM on XGB35 Linux Desktop
    (DIST / 'stardroid.desktop').write_bytes(
        b'[Desktop Entry]\n'
        b'Type=Application\n'
        b'Name=StarDroid (StarCraft)\n'
        b'Comment=StarCraft & Brood War (OpenBW / StarDroid 1.9.11)\n'
        b'Exec=/mnt/sd/ports/stardroid/StarDroid-Desktop.sh\n'
        b'Icon=/mnt/sd/ports/stardroid/stardroid.svg\n'
        b'Terminal=false\n'
        b'Categories=Game;StrategyGame;\n'
    )

    # On-device installer script (mounts desktop.ext2 if needed and registers LXDE Desktop icon, Start Menu, JWM, CLI & Ports)
    (DIST / 'install_on_device.sh').write_bytes(
        b'#!/bin/sh\n'
        b'set -e\n'
        b'DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"\n'
        b'chmod +x "$DIR/bin/stardroid" "$DIR/lib/ld-musl-armhf.so.1" "$DIR/StarDroid.sh" "$DIR/StarDroid-Desktop.sh"\n'
        b'MOUNTED_DESK=0\n'
        b'if [ ! -d /desktop/usr/bin ] && [ -f /mnt/sd/linux/desktop.ext2 ]; then\n'
        b'    mkdir -p /desktop\n'
        b'    if mount -t ext2 -o loop,noatime /mnt/sd/linux/desktop.ext2 /desktop 2>/dev/null || mount -t ext4 -o loop,noatime /mnt/sd/linux/desktop.ext2 /desktop 2>/dev/null; then\n'
        b'        MOUNTED_DESK=1\n'
        b'    fi\n'
        b'fi\n'
        b'for ROOT in "" "/desktop" "/alpine"; do\n'
        b'    if [ -d "$ROOT/usr/bin" ]; then\n'
        b'        mkdir -p "$ROOT/usr/local/bin" "$ROOT/usr/share/applications" "$ROOT/root/Desktop" 2>/dev/null || true\n'
        b'        mkdir -p "$ROOT/usr/share/pixmaps" 2>/dev/null || true\n'
        b'        cp -f "$DIR/stardroid.svg" "$ROOT/usr/share/pixmaps/stardroid.svg" 2>/dev/null || true\n'
        b'        ln -sf "$DIR/bin/stardroid" "$ROOT/usr/local/bin/stardroid" 2>/dev/null || cp -f "$DIR/StarDroid-Desktop.sh" "$ROOT/usr/local/bin/stardroid"\n'
        b'        cp -f "$DIR/stardroid.desktop" "$ROOT/usr/share/applications/stardroid.desktop"\n'
        b'        cp -f "$DIR/stardroid.desktop" "$ROOT/root/Desktop/StarDroid.desktop"\n'
        b'        chmod +x "$ROOT/root/Desktop/StarDroid.desktop"\n'
        b'        for JWMRC in "$ROOT/root/.jwmrc" "$ROOT/etc/jwm/system.jwmrc"; do\n'
        b'            if [ -f "$JWMRC" ] && ! grep -q "StarDroid-Desktop.sh" "$JWMRC" 2>/dev/null; then\n'
        b'                sed -i "s|<RootMenu|<Program label=\\"StarDroid (StarCraft)\\">/mnt/sd/ports/stardroid/StarDroid-Desktop.sh</Program>\\n<RootMenu|" "$JWMRC" 2>/dev/null || true\n'
        b'            fi\n'
        b'        done\n'
        b'    fi\n'
        b'done\n'
        b'if [ "$MOUNTED_DESK" = "1" ]; then sync; umount /desktop 2>/dev/null || true; fi\n'
        b'for PDIR in "/mnt/sd/onion/Roms/PORTS" "/mnt/sd/Roms/PORTS" "/mnt/sd/ports" "/mnt/sd/game"; do\n'
        b'    if [ -d "$PDIR" ]; then\n'
        b'        printf "#!/bin/sh\\nexec \\"%s/StarDroid.sh\\"\\n" "$DIR" > "$PDIR/StarDroid.sh"\n'
        b'        chmod +x "$PDIR/StarDroid.sh"\n'
        b'    fi\n'
        b'done\n'
        b'sync\n'
        b'echo "StarDroid ready at $DIR (Desktop shortcuts + Handheld Ports installed)!"\n'
    )

    # Assert-based MPQ/CHK integrity check on bundled Weave_v1.scx + host C++ self-check
    scx = (DIST / 'maps/Weave_v1.scx').read_bytes()
    assert scx[:4] == b'MPQ\x1a' and len(scx) == 41672, 'Invalid bundled Weave_v1.scx MPQ header'

    chk_exe = BUILD / 'selfcheck.exe'
    subprocess.run([sys.executable, '-m', 'ziglang', 'c++', '-std=c++17', '-O2',
                    '-Wno-deprecated-declarations', '-I' + str(SRC),
                    str(SRC / 'selfcheck.cpp'), '-o', str(chk_exe)], check=True)
    subprocess.run([str(chk_exe)], check=True)

    print(f'[5/5] Verified dynamic ARMv7 ELF: {out_bin} ({len(raw):,} bytes, PT_INTERP=/lib/ld-musl-armhf.so.1)')


if __name__ == '__main__':
    generate_missions_header()
    prepare_sysroot()
    binary = build_arm_binary()
    verify_elf_and_bundle(binary)
