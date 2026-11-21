"""Cross-compile robcodedev/onionos-mainui-opensource for XGB35 (ATM7051H armv7) and pack SD bundle.

Produces:
  - onionos-bundle/MainUI               (32-bit ARM Cortex-A5 musl ELF)
  - onionos-bundle/unit-tests           (cross-compiled Open MainUI unit test suite)
  - onionos-bundle/onion-xgb35-sd.tar.gz (ready to unpack onto /mnt/sd/onion: MainUI + libs +
                                          official Onion skin/fonts/sounds/Icons + Emu configs)
"""
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile

import build_alpine_bundle as alp

ROOT = Path(__file__).resolve().parent
SRC_ROOT = ROOT.parents[1] / 'ports' / 'onion-mainui'
BUNDLE = ROOT / 'onionos-bundle'
SYSROOT = BUNDLE / 'sysroot'
OBJ_DIR = BUNDLE / 'obj'
ONION_STATIC = BUNDLE / 'Onion' / 'static' / 'build'

DEV_PKGS = [
    'sdl2', 'sdl2-dev', 'sdl12-compat', 'sdl12-compat-dev',
    'sdl_image', 'sdl_image-dev', 'sdl_mixer',
    'freetype', 'freetype-dev', 'sqlite-libs', 'sqlite-dev',
    'alsa-lib', 'libpng', 'libjpeg-turbo', 'libwebp', 'zlib', 'bzip2', 'brotli-libs',
]

SYSTEMS = [
    # (folder, label, icon, rom_sub, core_so, exts)
    ('FC',     'Nintendo NES',       'fc.png',     'nes',    'fceumm_libretro.so',    'nes|fds|unf|zip'),
    ('GB',     'Game Boy',           'gb.png',     'gb',     'gambatte_libretro.so',  'gb|dmg|zip'),
    ('GBC',    'Game Boy Color',     'gbc.png',    'gbc',    'gambatte_libretro.so',  'gbc|gb|zip'),
    ('GBA',    'Game Boy Advance',   'gba.png',    'gba',    'gpsp_libretro.so',      'gba|agb|bin|zip'),
    ('MD',     'Sega Genesis / MD',  'md.png',     'md',     'picodrive_libretro.so', 'md|gen|smd|bin|32x|sms|zip'),
    ('SFC',    'Super Nintendo',     'sfc.png',    'snes',   'snes9x2005_libretro.so','sfc|smc|fig|zip'),
    ('PCE',    'PC Engine / TG-16',  'pce.png',    'pce',    'mednafen_pce_fast_libretro.so', 'pce|ccd|cue|chd|zip'),
    ('PS',     'PlayStation',        'ps.png',     'psx',    'pcsx_rearmed_libretro.so', 'pbp|chd|cue|bin|iso|img'),
    ('ARCADE', 'Arcade (MAME/FBA)',  'arcade.png', 'arcade', 'fbalpha2012_libretro.so', 'zip'),
]


def setup_sysroot():
    marker = SYSROOT / '.ready'
    if marker.exists():
        return
    print('Preparing Alpine v3.20 armv7 sysroot for Open MainUI...', flush=True)
    SYSROOT.mkdir(parents=True, exist_ok=True)
    by_name, by_prov = alp.load_indices()
    pkgs = alp.resolve(DEV_PKGS, by_name, by_prov)
    print(f'Resolved {len(pkgs)} Alpine packages for sysroot.')
    symlinks = []
    for p in pkgs:
        fn = f"{p['name']}-{p['ver']}.apk"
        raw = alp.fetch(f"{p['base']}/{fn}", alp.CACHE / fn)
        dtar = alp.apk_data_tar(raw)
        with tarfile.open(fileobj=io.BytesIO(dtar), mode='r:') as tf:
            for m in alp.safe_members(tf):
                dest = SYSROOT / m.name
                if m.isdir():
                    dest.mkdir(parents=True, exist_ok=True)
                elif m.isfile():
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(tf.extractfile(m).read())
                elif m.issym():
                    symlinks.append((dest, m.linkname))
    # Materialize library/header symlinks as copies on Windows so zig ld resolves libfoo.so -> libfoo.so.X cleanly
    for _ in range(4):
        for dest, target in symlinks:
            if dest.exists():
                continue
            src = (dest.parent / target).resolve()
            if src.is_file():
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dest)
    # Copy ONLY third-party non-libc headers (sqlite3 + freetype2) into ext-include so Zig's built-in musl headers stay pure
    ext_inc = BUNDLE / 'ext-include'
    ext_inc.mkdir(parents=True, exist_ok=True)
    for hdr in ('sqlite3.h', 'sqlite3ext.h'):
        s = SYSROOT / 'usr' / 'include' / hdr
        if s.exists():
            shutil.copy2(s, ext_inc / hdr)
    ft_src = SYSROOT / 'usr' / 'include' / 'freetype2'
    if ft_src.exists() and not (ext_inc / 'freetype').exists():
        shutil.copytree(ft_src, ext_inc, dirs_exist_ok=True)
    marker.write_text('ok')


def compile_c(src: Path, obj: Path, extra_flags=()):
    obj.parent.mkdir(parents=True, exist_ok=True)
    if obj.exists() and obj.stat().st_mtime >= src.stat().st_mtime:
        return
    opt = '-O2' if src.name == 'xgb35_hw.c' else ('-O0' if src.name == 'SDL_ttf.c' else '-O1')
    def make_cmd(opt_flag):
        return [
            sys.executable, '-m', 'ziglang', 'cc',
            '-target', 'arm-linux-musleabihf', '-mcpu=cortex_a5',
            '-std=c11', opt_flag, '-fPIE', '-ffunction-sections', '-fdata-sections',
            '-D_GNU_SOURCE', '-D_FILE_OFFSET_BITS=64',
            '-DCJSON_HIDE_SYMBOLS', '-DMAINUI_ONION', '-DMAINUI_XGB35',
            '-DMAINUI_VERSION="1.1.0-xgb35"',
            f'-I{SRC_ROOT / "src"}',
            f'-I{SRC_ROOT / "vendor" / "cjson"}',
            f'-I{SRC_ROOT / "vendor" / "sdl_ttf"}',
            f'-I{SYSROOT / "usr" / "include" / "SDL"}',
            f'-I{BUNDLE / "ext-include"}',
            *extra_flags,
            '-c', str(src), '-o', str(obj),
        ]
    try:
        subprocess.check_call(make_cmd(opt))
    except subprocess.CalledProcessError:
        subprocess.check_call(make_cmd('-O0'))


def verify_arm_elf(path: Path):
    raw = path.read_bytes()
    assert raw[:6] == b'\x7fELF\x01\x01', f'{path.name}: not 32-bit little-endian ELF'
    e_type, e_machine = struct.unpack_from('<HH', raw, 16)
    assert e_machine == 40 and e_type == 3, f'{path.name}: expected PIE ET_DYN (3) ARM (40), got {(e_type, e_machine)}'


def build_binaries():
    setup_sysroot()
    lib_srcs = sorted(p for p in (SRC_ROOT / 'src').rglob('*.c') if p.name != 'main.c')
    lib_srcs += [SRC_ROOT / 'vendor' / 'cjson' / 'cJSON.c', SRC_ROOT / 'vendor' / 'sdl_ttf' / 'SDL_ttf.c']

    print(f'Cross-compiling {len(lib_srcs) + 1} Open MainUI source files (ARMv7 Cortex-A5 musl)...', flush=True)
    lib_objs = []
    for src in lib_srcs:
        rel = src.relative_to(SRC_ROOT).with_suffix('.o')
        obj = OBJ_DIR / rel
        compile_c(src, obj)
        lib_objs.append(obj)

    main_obj = OBJ_DIR / 'src' / 'app' / 'main.o'
    compile_c(SRC_ROOT / 'src' / 'app' / 'main.c', main_obj)

    out_bin = BUNDLE / 'MainUI'
    link_cmd = [
        sys.executable, '-m', 'ziglang', 'cc',
        '-target', 'arm-linux-musleabihf', '-mcpu=cortex_a5', '-pie',
        '-Wl,--gc-sections', '-Wl,--allow-shlib-undefined', '-s',
        '-Wl,-rpath,/mnt/SDCARD/lib:/usr/lib:/lib',
        f'-L{SYSROOT / "usr" / "lib"}', f'-L{SYSROOT / "lib"}',
        str(main_obj), *(str(o) for o in lib_objs),
        '-lSDL', '-lSDL_image', '-lfreetype', '-lsqlite3',
        '-lpthread', '-lm', '-ldl',
        '-o', str(out_bin),
    ]
    subprocess.check_call(link_cmd)
    verify_arm_elf(out_bin)
    print(f'Linked {out_bin.name}: {out_bin.stat().st_size:,} bytes', flush=True)

    # Also cross-compile the upstream Open MainUI unit test suite!
    unit_srcs = [
        SRC_ROOT / 'tests' / 'main.c',
        SRC_ROOT / 'tests' / 'test_context.c',
        SRC_ROOT / 'tests' / 'test_core.c',
        SRC_ROOT / 'tests' / 'test_input.c',
        SRC_ROOT / 'tests' / 'test_launch.c',
        SRC_ROOT / 'tests' / 'test_letter_jump.c',
        SRC_ROOT / 'tests' / 'test_menu.c',
        SRC_ROOT / 'tests' / 'test_name_input.c',
        SRC_ROOT / 'tests' / 'test_options.c',
        SRC_ROOT / 'tests' / 'test_screen_events.c',
        SRC_ROOT / 'tests' / 'test_state.c',
        SRC_ROOT / 'tests' / 'test_timing.c',
    ]
    unit_objs = []
    for src in unit_srcs:
        obj = OBJ_DIR / src.relative_to(SRC_ROOT).with_suffix('.o')
        compile_c(src, obj)
        unit_objs.append(obj)
    unit_bin = BUNDLE / 'unit-tests'
    subprocess.check_call([
        sys.executable, '-m', 'ziglang', 'cc',
        '-target', 'arm-linux-musleabihf', '-mcpu=cortex_a5', '-pie',
        '-Wl,--gc-sections', '-Wl,--allow-shlib-undefined', '-s',
        '-Wl,-rpath,/mnt/SDCARD/lib:/usr/lib:/lib',
        f'-L{SYSROOT / "usr" / "lib"}', f'-L{SYSROOT / "lib"}',
        *(str(o) for o in unit_objs), *(str(o) for o in lib_objs),
        '-lSDL', '-lSDL_image', '-lfreetype', '-lsqlite3',
        '-lpthread', '-lm', '-ldl',
        '-o', str(unit_bin),
    ])
    verify_arm_elf(unit_bin)
    print(f'Linked {unit_bin.name}: {unit_bin.stat().st_size:,} bytes', flush=True)
    return out_bin, unit_bin


def add_bytes(tf: tarfile.TarFile, arcname: str, data: bytes, mode=0o644):
    ti = tarfile.TarInfo(arcname)
    ti.size = len(data)
    ti.mode = mode
    tf.addfile(ti, io.BytesIO(data))


def elf_dt_needed(raw: bytes):
    if raw[:6] != b'\x7fELF\x01\x01':
        return []
    e_shoff, = struct.unpack_from('<I', raw, 32)
    e_shentsize, e_shnum = struct.unpack_from('<HH', raw, 46)
    if not e_shoff or not e_shnum:
        return []
    secs = [struct.unpack_from('<10I', raw, e_shoff + i * e_shentsize) for i in range(e_shnum)]
    needed = []
    for sh in secs:
        if sh[1] == 6:  # SHT_DYNAMIC
            dyn_off, dyn_sz, link = sh[4], sh[5], sh[6]
            str_off, str_sz = secs[link][4], secs[link][5]
            strtab = raw[str_off:str_off + str_sz]
            for pos in range(dyn_off, dyn_off + dyn_sz, 8):
                d_tag, d_val = struct.unpack_from('<iI', raw, pos)
                if d_tag == 0:
                    break
                if d_tag == 1:  # DT_NEEDED
                    end = strtab.find(0, d_val)
                    needed.append(strtab[d_val:end].decode('ascii', 'ignore'))
    return needed


def pack_sd_bundle(mainui_bin: Path, unit_bin: Path):
    assert (ONION_STATIC / 'miyoo' / 'app' / 'skin').is_dir(), 'Missing Onion static/build/miyoo/app/skin'
    tar_path = BUNDLE / 'onion-xgb35-sd.tar.gz'
    print(f'Packing {tar_path.name}...', flush=True)

    # Trace exact transitive DT_NEEDED shared libraries of MainUI + SDL_mixer (no 220MB Mesa/LLVM bloat!)
    lib_lookup = {}
    for lib_dir in (SYSROOT / 'usr' / 'lib', SYSROOT / 'lib'):
        if lib_dir.exists():
            for f in lib_dir.iterdir():
                if f.is_file() and '.so' in f.name and f.name not in lib_lookup:
                    lib_lookup[f.name] = f

    queue = list(elf_dt_needed(mainui_bin.read_bytes())) + [
        'libSDL_mixer-1.2.so.0', 'libSDL2-2.0.so.0',
        'libasound.so.2', 'libjpeg.so.8', 'libmpg123.so.0', 'libvorbisfile.so.3',
    ]
    wanted_libs = {}
    while queue:
        soname = queue.pop(0)
        if soname in wanted_libs or soname.startswith('libc.musl') or soname.startswith('ld-musl'):
            continue
        f = lib_lookup.get(soname)
        if not f:
            continue
        raw = f.read_bytes()
        wanted_libs[soname] = raw
        queue.extend(elf_dt_needed(raw))
    print(f'Resolved {len(wanted_libs)} exact runtime shared libraries:', sorted(wanted_libs))

    with tarfile.open(tar_path, 'w:gz') as tf:
        # 1. Binaries
        add_bytes(tf, 'onion/bin/MainUI', mainui_bin.read_bytes(), 0o755)
        add_bytes(tf, 'onion/bin/unit-tests', unit_bin.read_bytes(), 0o755)

        # 2. Exact runtime .so libraries
        for soname, raw in sorted(wanted_libs.items()):
            add_bytes(tf, f'onion/lib/{soname}', raw, 0o755)

        # 3. Official Onion miyoo/app (skin, sound, lang, fonts — skip arm glibc binaries audioserver/wpa_*)
        skip_bins = {'audioserver', 'wpa_cli', 'wpa_supplicant'}
        app_root = ONION_STATIC / 'miyoo' / 'app'
        for p in sorted(app_root.rglob('*')):
            if p.is_file() and p.name not in skip_bins:
                rel = p.relative_to(ONION_STATIC).as_posix()
                add_bytes(tf, f'onion/{rel}', p.read_bytes(), 0o644)
        # Also overlay latest lang/*.lang from robcodedev/onionos-mainui-opensource
        for lp in sorted((SRC_ROOT / 'lang').glob('*.lang')):
            add_bytes(tf, f'onion/miyoo/app/lang/{lp.name}', lp.read_bytes(), 0o644)

        # 4. Official Onion console Icons
        icons_root = ONION_STATIC / 'Icons'
        if icons_root.exists():
            for p in sorted(icons_root.rglob('*')):
                if p.is_file():
                    rel = p.relative_to(ONION_STATIC).as_posix()
                    add_bytes(tf, f'onion/{rel}', p.read_bytes(), 0o644)

        # 5. Default system.json & .tmp_update/config + deviceModel (354 = Mini Plus battery% layout)
        sys_json = {
            'vol': 14, 'bgmvol': 8, 'brightness': 7, 'hibernate': 0,
            'language': 'ru.lang', 'fontsize': 24, 'theme': './',
            'lumination': 7, 'hue': 10, 'saturation': 10, 'contrast': 10, 'mute': 0,
        }
        add_bytes(tf, 'onion/system.json', (json.dumps(sys_json, indent=2) + '\n').encode(), 0o644)
        for flag in ('show_recents', 'show_expert'):
            add_bytes(tf, f'onion/.tmp_update/config/.{flag}', b'', 0o644)

        # 6. Emulator configs + universal XGB35 ROM launcher (supports both xgbretro & SuperX cores)
        for folder, label, icon, rom_sub, core_so, exts in SYSTEMS:
            cfg = {
                'label': label,
                'icon': f'/mnt/SDCARD/Icons/Default/{icon}',
                'iconsel': f'/mnt/SDCARD/Icons/Default/sel/{icon}',
                'launch': f'launch.sh',
                'rompath': f'../../Roms/{folder}',
                'imgpath': f'../../Roms/{folder}/Imgs',
                'useswap': 0,
                'shortname': 0,
                'hidebios': 1,
                'extlist': exts,
            }
            add_bytes(tf, f'onion/Emu/{folder}/config.json', (json.dumps(cfg, indent=2) + '\n').encode(), 0o644)
            launch_sh = f"""#!/bin/sh
ROM="$1"
[ -f "$ROM" ] || exit 0
if [ -f "/usr/lib/libretro/{core_so}" ]; then
    exec /usr/bin/xgbretro "/usr/lib/libretro/{core_so}" "$ROM"
elif [ -f "/mnt/sd/cfw/retroarch/.retroarch/cores/{core_so}" ] && [ -x /superx/usr/bin/retroarch ]; then
    exec chroot /superx /bin/sh -c 'export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib HOME=/mnt/card SDL_VIDEODRIVER=fbcon SDL_NOMOUSE=1 SDL_VIDEO_FBCON_ROTATION=CW SCREEN_WIDTH=480 SCREEN_HEIGHT=320 SDL_AUDIODRIVER=alsa; exec /usr/bin/retroarch -v -c /mnt/card/cfw/retroarch/retroarch.cfg -L /mnt/card/cfw/retroarch/.retroarch/cores/{core_so} "'"$ROM"'"'
fi
"""
            add_bytes(tf, f'onion/Emu/{folder}/launch.sh', launch_sh.encode(), 0o755)
            add_bytes(tf, f'onion/Roms/{folder}/.keep', b'', 0o644)

        # Built-in 2048 game in Ports so Onion UI can launch a game immediately even before adding ROMs!
        ports_cfg = {
            'label': 'Ports (2048)',
            'icon': '/mnt/SDCARD/Icons/Default/ports.png',
            'launch': 'launch.sh',
            'rompath': '../../Roms/PORTS',
            'extlist': 'port',
        }
        add_bytes(tf, 'onion/Emu/PORTS/config.json', (json.dumps(ports_cfg, indent=2) + '\n').encode(), 0o644)
        add_bytes(tf, 'onion/Emu/PORTS/launch.sh',
                  b'#!/bin/sh\nexec /usr/bin/xgbretro /usr/lib/libretro/2048_libretro.so\n', 0o755)
        add_bytes(tf, 'onion/Roms/PORTS/2048 Puzzle.port', b'2048\n', 0o644)

        # 7. Apps: Return to Multi-Boot Menu & Fullscreen Terminal
        for app_id, app_label, body in (
            ('MultiBoot', 'Multi-Boot Menu', '#!/bin/sh\ntouch /tmp/.exit_onion\n'),
            ('Terminal',  'Linux Terminal',  '#!/bin/sh\nexec /bin/fbterm\n'),
        ):
            acfg = {'label': app_label, 'icon': '/mnt/SDCARD/miyoo/app/skin/ic-retroarch.png', 'launch': 'launch.sh'}
            add_bytes(tf, f'onion/App/{app_id}/config.json', (json.dumps(acfg, indent=2) + '\n').encode(), 0o644)
            add_bytes(tf, f'onion/App/{app_id}/launch.sh', body.encode(), 0o755)

    manifest = {
        'mainui_elf': {'bytes': mainui_bin.stat().st_size, 'sha256': hashlib.sha256(mainui_bin.read_bytes()).hexdigest()},
        'unit_tests_elf': {'bytes': unit_bin.stat().st_size, 'sha256': hashlib.sha256(unit_bin.read_bytes()).hexdigest()},
        'sd_bundle_tar_gz': {'bytes': tar_path.stat().st_size, 'sha256': hashlib.sha256(tar_path.read_bytes()).hexdigest()},
    }
    (BUNDLE / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print('Onion OS Open MainUI XGB35 bundle COMPLETE:')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    m_bin, u_bin = build_binaries()
    pack_sd_bundle(m_bin, u_bin)
