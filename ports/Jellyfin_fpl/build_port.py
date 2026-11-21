#!/usr/bin/env python3
"""Build Jellyfin_fpl native port for XGB35 (ATM7051H Alpine Linux ARMv7: X11 Desktop + 480x320 fb0)."""
import argparse
import io
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
import zlib

PORT = Path(__file__).resolve().parent
SRC = PORT / 'src'
BUILD = PORT / 'build'
OBJ = BUILD / 'arm-obj'
SYSROOT = BUILD / 'sysroot'
DIST = PORT / 'dist'
ALPINE_BUNDLE = PORT.parents[1] / 'recovery' / 'adfu' / 'alpine-bundle'
CACHE = ALPINE_BUNDLE / 'cache'

REPOS = [
    ('v3.20_main', 'https://dl-cdn.alpinelinux.org/alpine/v3.20/main/armv7'),
    ('v3.20_community', 'https://dl-cdn.alpinelinux.org/alpine/v3.20/community/armv7'),
]

RUNTIME_PKGS = [
    'libcurl',
    'ca-certificates-bundle',
    'cjson',
    'freetype',
    'ffmpeg-libavformat',
    'ffmpeg-libavcodec',
    'ffmpeg-libswscale',
    'ffmpeg-libswresample',
    'ffmpeg-libavutil',
]

DEV_PKGS = [
    'curl-dev',
    'cjson-dev',
    'freetype-dev',
    'ffmpeg-dev',
    'xorgproto',
    'libx11-dev',
    'libxext-dev',
    'font-dejavu',
]

LINK_STEMS = [
    ('libcurl.so', 'libcurl.so.'),
    ('libcjson.so', 'libcjson.so.'),
    ('libfreetype.so', 'libfreetype.so.'),
    ('libavformat.so', 'libavformat.so.'),
    ('libavcodec.so', 'libavcodec.so.'),
    ('libswscale.so', 'libswscale.so.'),
    ('libswresample.so', 'libswresample.so.'),
    ('libavutil.so', 'libavutil.so.'),
]


def fetch_cached(url: str, dest: Path) -> bytes:
    if dest.is_file() and dest.stat().st_size > 0:
        return dest.read_bytes()
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f'  [download] {dest.name} ...', flush=True)
    req = urllib.request.Request(url, headers={'User-Agent': 'Jellyfin_fpl-builder/1.0'})
    data = urllib.request.urlopen(req, timeout=90).read()
    dest.write_bytes(data)
    return data


def apk_data_tar(raw: bytes) -> bytes:
    last = b''
    while raw:
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        last = d.decompress(raw)
        raw = d.unused_data
    return last


def parse_elf32_dyn(raw: bytes) -> tuple[str, list[str]]:
    """Return (soname, needed_list) from a 32-bit little-endian ARM ELF."""
    if len(raw) < 52 or raw[:6] != b'\x7fELF\x01\x01':
        return '', []
    e_shoff = struct.unpack_from('<I', raw, 32)[0]
    e_shentsize, e_shnum = struct.unpack_from('<HH', raw, 46)
    if e_shoff == 0 or e_shnum == 0 or e_shoff + e_shentsize * e_shnum > len(raw):
        return '', []

    sections = []
    for i in range(e_shnum):
        sh = struct.unpack_from('<10I', raw, e_shoff + i * e_shentsize)
        sections.append(sh)  # sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, ...

    soname = ''
    needed = []
    for sh in sections:
        sh_type, sh_offset, sh_size, sh_link = sh[1], sh[4], sh[5], sh[6]
        if sh_type != 6 or sh_link >= len(sections):  # SHT_DYNAMIC = 6
            continue
        str_sh = sections[sh_link]
        strtab = raw[str_sh[4]:str_sh[4] + str_sh[5]]
        for pos in range(sh_offset, sh_offset + sh_size, 8):
            if pos + 8 > len(raw):
                break
            d_tag, d_val = struct.unpack_from('<iI', raw, pos)
            if d_tag == 0:
                break
            if d_tag in (1, 14) and d_val < len(strtab):  # DT_NEEDED=1, DT_SONAME=14
                end = strtab.find(b'\x00', d_val)
                s = strtab[d_val:end if end >= 0 else len(strtab)].decode('ascii', 'ignore')
                if d_tag == 14:
                    soname = s
                elif s:
                    needed.append(s)
    return soname, needed


def load_apk_index() -> tuple[dict, dict]:
    by_name, by_prov = {}, {}
    for tag, base in REPOS:
        raw = fetch_cached(f'{base}/APKINDEX.tar.gz', CACHE / f'{tag}_APKINDEX.tar.gz')
        with tarfile.open(fileobj=io.BytesIO(raw), mode='r:gz') as tf:
            idx = tf.extractfile('APKINDEX').read().decode('utf-8', 'ignore')
        for blk in idx.split('\n\n'):
            d = dict(line.split(':', 1) for line in blk.splitlines() if ':' in line)
            if 'P' not in d:
                continue
            entry = {
                'name': d['P'],
                'ver': d['V'],
                'base': base,
                'deps': [x for x in d.get('D', '').split() if not x.startswith('!')],
                'prov': d.get('p', '').split(),
            }
            by_name.setdefault(entry['name'], entry)
            for pr in entry['prov']:
                by_prov.setdefault(pr.split('=')[0], entry['name'])
    return by_name, by_prov


def prepare_sysroot() -> None:
    inc_dir = SYSROOT / 'include'
    sys_lib = SYSROOT / 'lib'
    dist_lib = DIST / 'lib'
    cert_dir = DIST / 'share' / 'certs'
    font_dir = DIST / 'share' / 'fonts'
    for d in (inc_dir, sys_lib, dist_lib, cert_dir, font_dir):
        d.mkdir(parents=True, exist_ok=True)

    stamp = SYSROOT / '.sysroot_v1'
    if stamp.is_file() and (inc_dir / 'libavcodec/avcodec.h').is_file() and (font_dir / 'DejaVuSans.ttf').is_file():
        print('[1/4] Alpine v3.20 ARMv7 sysroot & runtime libraries already prepared')
        return

    musl_so = ALPINE_BUNDLE / 'staging' / 'ld-musl-armhf.so.1'
    gcc_so = ALPINE_BUNDLE / 'staging' / 'libgcc_s.so.1'
    stdcpp_so = ALPINE_BUNDLE / 'staging' / 'libstdc++.so.6'
    assert musl_so.is_file(), f'Missing {musl_so}'
    shutil.copy2(musl_so, sys_lib / 'libc.so')
    shutil.copy2(musl_so, dist_lib / 'ld-musl-armhf.so.1')
    for extra in (gcc_so, stdcpp_so):
        if extra.is_file():
            shutil.copy2(extra, sys_lib / extra.name)
            shutil.copy2(extra, dist_lib / extra.name)
    for stub in ('libdl.a', 'libpthread.a', 'libm.a', 'librt.a'):
        (sys_lib / stub).write_bytes(b'!<arch>\n')

    by_name, by_prov = load_apk_index()
    seen = set()
    runtime_order = []

    def visit(pkg: str) -> None:
        if pkg in seen or pkg in ('musl', 'busybox', 'busybox-binsh', 'alpine-baselayout', 'alpine-keys'):
            return
        seen.add(pkg)
        info = by_name.get(pkg)
        if not info:
            return
        for dep in info['deps']:
            dk = dep.split('=')[0].split('<')[0].split('>')[0]
            target = dk if dk in by_name else by_prov.get(dk)
            if target:
                visit(target)
        runtime_order.append(info)

    for p in RUNTIME_PKGS:
        visit(p)

    # Extract runtime shared libraries by ELF DT_SONAME (FAT32-safe, zero symlinks or duplicates)
    so_count = 0
    for info in runtime_order:
        fn = f"{info['name']}-{info['ver']}.apk"
        raw_apk = fetch_cached(f"{info['base']}/{fn}", CACHE / fn)
        with tarfile.open(fileobj=io.BytesIO(apk_data_tar(raw_apk)), mode='r:') as tf:
            for mem in tf.getmembers():
                if not mem.isfile():
                    continue
                name = mem.name.lstrip('./')
                if (name.startswith('lib/') or name.startswith('usr/lib/')) and '.so' in Path(name).name:
                    blob = tf.extractfile(mem).read()
                    if blob[:4] != b'\x7fELF':
                        continue
                    soname, _ = parse_elf32_dyn(blob)
                    out_name = soname or Path(name).name
                    # Skip X11/DRM/Mesa client libs only if already on desktop, wait: keeping all small .so
                    # in dist/lib guarantees handheld /dev/fb0 works even without /desktop mounted!
                    (dist_lib / out_name).write_bytes(blob)
                    (sys_lib / out_name).write_bytes(blob)
                    so_count += 1
                elif name in ('etc/ssl/certs/ca-certificates.crt', 'etc/ssl/cert.pem'):
                    blob = tf.extractfile(mem).read()
                    if len(blob) > 1024:
                        (cert_dir / 'ca-certificates.crt').write_bytes(blob)

    # Extract dev headers and DejaVuSans fonts
    for pkg in DEV_PKGS:
        info = by_name[pkg]
        fn = f"{info['name']}-{info['ver']}.apk"
        raw_apk = fetch_cached(f"{info['base']}/{fn}", CACHE / fn)
        with tarfile.open(fileobj=io.BytesIO(apk_data_tar(raw_apk)), mode='r:') as tf:
            for mem in tf.getmembers():
                if not mem.isfile():
                    continue
                name = mem.name.lstrip('./')
                if name.startswith('usr/include/'):
                    out = inc_dir / name[len('usr/include/'):]
                    out.parent.mkdir(parents=True, exist_ok=True)
                    out.write_bytes(tf.extractfile(mem).read())
                elif name.endswith('/DejaVuSans.ttf') or name.endswith('/DejaVuSans-Bold.ttf'):
                    (font_dir / Path(name).name).write_bytes(tf.extractfile(mem).read())

    # Create unversioned .so copies in build/sysroot/lib so -lcurl -lavcodec etc. resolve cleanly
    staged_names = [p.name for p in sys_lib.glob('*.so.*')]
    for link_name, prefix in LINK_STEMS:
        matches = [n for n in staged_names if n.startswith(prefix)]
        assert matches, f'Missing shared library matching {prefix} in {sys_lib}'
        shutil.copy2(sys_lib / matches[0], sys_lib / link_name)

    assert (cert_dir / 'ca-certificates.crt').is_file(), 'Missing ca-certificates.crt'
    assert (font_dir / 'DejaVuSans.ttf').is_file(), 'Missing DejaVuSans.ttf'
    stamp.write_text('ok\n', 'utf-8')
    print(f'[1/4] Prepared sysroot & {so_count} runtime ARMv7 shared libraries + certs + DejaVu fonts')


def run_zig(cmd: list[str]) -> None:
    for attempt in range(3):
        r = subprocess.run(cmd)
        if r.returncode == 0:
            return
        print(f'  [zig retry {attempt + 1}/3 after exit {r.returncode}]')
    raise subprocess.CalledProcessError(r.returncode, cmd)


def run_host_selfcheck() -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    chk_exe = BUILD / 'selfcheck.exe'
    print('[2/4] Building & running host self-check suite (selfcheck.cpp)...')
    run_zig([
        sys.executable, '-m', 'ziglang', 'c++',
        '-std=c++17', '-O2',
        '-I' + str(SRC),
        str(SRC / 'selfcheck.cpp'),
        '-lws2_32',
        '-o', str(chk_exe),
    ])
    subprocess.run([str(chk_exe)], check=True)


def build_arm_binary() -> Path:
    OBJ.mkdir(parents=True, exist_ok=True)
    out_bin = DIST / 'bin' / 'jellyfin_fpl'
    out_bin.parent.mkdir(parents=True, exist_ok=True)

    cxx = [
        sys.executable, '-m', 'ziglang', 'c++',
        '-target', 'arm-linux-musleabihf',
        '-mcpu=cortex_a5', '-mfpu=neon-vfpv4',
        '-std=c++17', '-O2', '-g0', '-fno-PIE',
        '-ffunction-sections', '-fdata-sections', '-fno-sanitize=undefined',
        '-Wno-deprecated-declarations',
    ]

    print('[3/4] Cross-compiling main.cpp for ARMv7 Cortex-A5 NEON musl...')
    run_zig(cxx + [
        '-I' + str(SRC),
        '-I' + str(SYSROOT / 'include'),
        '-I' + str(SYSROOT / 'include' / 'freetype2'),
        '-c', str(SRC / 'main.cpp'),
        '-o', str(OBJ / 'main.o'),
    ])

    link_libs = [
        str(SYSROOT / 'lib' / 'libcurl.so'),
        str(SYSROOT / 'lib' / 'libcjson.so'),
        str(SYSROOT / 'lib' / 'libfreetype.so'),
        str(SYSROOT / 'lib' / 'libavformat.so'),
        str(SYSROOT / 'lib' / 'libavcodec.so'),
        str(SYSROOT / 'lib' / 'libswscale.so'),
        str(SYSROOT / 'lib' / 'libswresample.so'),
        str(SYSROOT / 'lib' / 'libavutil.so'),
        str(ALPINE_BUNDLE / 'staging' / 'libgcc_s.so.1'),
        str(SYSROOT / 'lib' / 'libc.so'),
    ]

    run_zig(cxx + [
        '-no-pie',
        '-Wl,--dynamic-linker,/lib/ld-musl-armhf.so.1',
        '-Wl,-rpath,$ORIGIN/../lib',
        '-Wl,--allow-shlib-undefined',
        '-Wl,--gc-sections', '-s',
        str(OBJ / 'main.o'),
        *link_libs,
        '-o', str(out_bin),
    ])
    return out_bin


def verify_and_bundle(out_bin: Path) -> None:
    raw = out_bin.read_bytes()
    assert raw[:6] == b'\x7fELF\x01\x01', 'Not 32-bit little-endian ARM ELF'
    e_type, e_machine, _, _, e_phoff, _, _, _, e_phentsize, e_phnum = struct.unpack_from('<HHIIIIIHHH', raw, 16)
    assert e_machine == 40 and e_type in (2, 3), f'Unexpected ELF type/machine: {(e_type, e_machine)}'

    interp = b''
    for i in range(e_phnum):
        pt, po, _, _, pfs, _, _, _ = struct.unpack_from('<8I', raw, e_phoff + i * e_phentsize)
        if pt == 3:
            interp = raw[po:po + pfs]
    assert interp == b'/lib/ld-musl-armhf.so.1\x00', f'Unexpected PT_INTERP: {interp!r}'

    # Verify transitive DT_NEEDED closure across binary + all dist/lib/*.so*
    dist_lib = DIST / 'lib'
    available = {p.name for p in dist_lib.iterdir() if p.is_file()}
    musl_aliases = {'libc.musl-armv7.so.1', 'libc.musl-armhf.so.1', 'ld-musl-armhf.so.1', 'libc.so'}
    queue = [out_bin]
    for dlopen_root in ('libX11.so.6', 'libXext.so.6'):
        if (dist_lib / dlopen_root).is_file():
            queue.append(dist_lib / dlopen_root)
    checked = set()
    missing = set()
    while queue:
        cur = queue.pop()
        if cur.name in checked:
            continue
        checked.add(cur.name)
        _, needed = parse_elf32_dyn(cur.read_bytes())
        for req in needed:
            if req in musl_aliases:
                continue
            if req not in available:
                missing.add((cur.name, req))
            elif req not in checked:
                queue.append(dist_lib / req)
    assert not missing, f'Missing runtime shared libraries in dist/lib: {sorted(missing)}'

    keep_libs = (checked - {out_bin.name}) | {'ld-musl-armhf.so.1'}
    for p in list(dist_lib.iterdir()):
        if p.is_file() and p.name not in keep_libs:
            p.unlink()
    available = {p.name for p in dist_lib.iterdir() if p.is_file()}

    for sub in ('data', 'downloads'):
        (DIST / sub).mkdir(parents=True, exist_ok=True)

    (DIST / 'jellyfin_fpl.svg').write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">\n'
        '  <defs><linearGradient id="jf" x1="0" y1="0" x2="1" y2="1">\n'
        '    <stop offset="0%" stop-color="#AA5CC3"/><stop offset="100%" stop-color="#00A4DC"/>\n'
        '  </linearGradient></defs>\n'
        '  <rect width="64" height="64" rx="14" fill="#141218"/>\n'
        '  <path d="M32 10 C25 10 10 43 14 49 C18 55 46 55 50 49 C54 43 39 10 32 10 Z" fill="none" stroke="url(#jf)" stroke-width="5"/>\n'
        '  <polygon points="27,25 41,33 27,41" fill="url(#jf)"/>\n'
        '</svg>\n',
        'utf-8',
    )

    # Handheld 480x320 /dev/fb0 launcher
    (DIST / 'Jellyfin_fpl.sh').write_bytes(
        b'#!/bin/sh\n'
        b'DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"\n'
        b'export SSL_CERT_FILE="$DIR/share/certs/ca-certificates.crt"\n'
        b'if [ -x /lib/ld-musl-armhf.so.1 ]; then\n'
        b'    export LD_LIBRARY_PATH="$DIR/lib:/usr/lib:/lib:${LD_LIBRARY_PATH}"\n'
        b'    exec "$DIR/bin/jellyfin_fpl" --fb "$@"\n'
        b'elif [ -x /alpine/lib/ld-musl-armhf.so.1 ]; then\n'
        b'    exec /alpine/lib/ld-musl-armhf.so.1 --library-path "$DIR/lib:/alpine/usr/lib:/alpine/lib" "$DIR/bin/jellyfin_fpl" --fb "$@"\n'
        b'else\n'
        b'    exec "$DIR/lib/ld-musl-armhf.so.1" --library-path "$DIR/lib:/usr/lib:/lib" "$DIR/bin/jellyfin_fpl" --fb "$@"\n'
        b'fi\n'
    )

    # HDMI Linux Desktop (X11) launcher
    (DIST / 'Jellyfin_fpl-Desktop.sh').write_bytes(
        b'#!/bin/sh\n'
        b'DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"\n'
        b'export DISPLAY="${DISPLAY:-:0}"\n'
        b'export SSL_CERT_FILE="$DIR/share/certs/ca-certificates.crt"\n'
        b'export LD_LIBRARY_PATH="$DIR/lib:/usr/lib:/lib:${LD_LIBRARY_PATH}"\n'
        b'exec "$DIR/bin/jellyfin_fpl" --x11 "$@"\n'
    )

    (DIST / 'jellyfin_fpl.desktop').write_bytes(
        b'[Desktop Entry]\n'
        b'Type=Application\n'
        b'Name=Jellyfin FPL\n'
        b'Comment=Jellyfin Folder Player & Offline Downloads (Streamyfin Port)\n'
        b'Exec=/mnt/sd/ports/Jellyfin_fpl/Jellyfin_fpl-Desktop.sh\n'
        b'Icon=/mnt/sd/ports/Jellyfin_fpl/jellyfin_fpl.svg\n'
        b'Terminal=false\n'
        b'Categories=AudioVideo;Player;Video;\n'
    )

    (DIST / 'install_on_device.sh').write_bytes(
        b'#!/bin/sh\n'
        b'set -e\n'
        b'DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"\n'
        b'chmod +x "$DIR/bin/jellyfin_fpl" "$DIR/lib/ld-musl-armhf.so.1" "$DIR/Jellyfin_fpl.sh" "$DIR/Jellyfin_fpl-Desktop.sh"\n'
        b'MOUNTED_DESK=0\n'
        b'if [ ! -d /desktop/usr/bin ] && [ -f /mnt/sd/linux/desktop.ext2 ]; then\n'
        b'    mkdir -p /desktop\n'
        b'    if mount -t ext2 -o loop,noatime /mnt/sd/linux/desktop.ext2 /desktop 2>/dev/null || mount -t ext4 -o loop,noatime /mnt/sd/linux/desktop.ext2 /desktop 2>/dev/null; then\n'
        b'        MOUNTED_DESK=1\n'
        b'    fi\n'
        b'fi\n'
        b'for ROOT in "" "/desktop" "/alpine"; do\n'
        b'    if [ -d "$ROOT/usr/bin" ]; then\n'
        b'        mkdir -p "$ROOT/usr/local/bin" "$ROOT/usr/share/applications" "$ROOT/usr/share/pixmaps" "$ROOT/root/Desktop"\n'
        b'        cp -f "$DIR/jellyfin_fpl.svg" "$ROOT/usr/share/pixmaps/jellyfin_fpl.svg" 2>/dev/null || true\n'
        b'        ln -sf "$DIR/Jellyfin_fpl-Desktop.sh" "$ROOT/usr/local/bin/jellyfin_fpl" 2>/dev/null || cp -f "$DIR/Jellyfin_fpl-Desktop.sh" "$ROOT/usr/local/bin/jellyfin_fpl"\n'
        b'        cp -f "$DIR/jellyfin_fpl.desktop" "$ROOT/usr/share/applications/jellyfin_fpl.desktop"\n'
        b'        cp -f "$DIR/jellyfin_fpl.desktop" "$ROOT/root/Desktop/Jellyfin_fpl.desktop"\n'
        b'        chmod +x "$ROOT/root/Desktop/Jellyfin_fpl.desktop"\n'
        b'        for JWMRC in "$ROOT/root/.jwmrc" "$ROOT/etc/jwm/system.jwmrc"; do\n'
        b'            if [ -f "$JWMRC" ] && ! grep -q "Jellyfin_fpl-Desktop.sh" "$JWMRC" 2>/dev/null; then\n'
        b'                sed -i "s|<RootMenu|<Program label=\\"Jellyfin FPL\\">/mnt/sd/ports/Jellyfin_fpl/Jellyfin_fpl-Desktop.sh</Program>\\n<RootMenu|" "$JWMRC" 2>/dev/null || true\n'
        b'            fi\n'
        b'        done\n'
        b'    fi\n'
        b'done\n'
        b'if [ "$MOUNTED_DESK" = "1" ]; then sync; umount /desktop 2>/dev/null || true; fi\n'
        b'for PDIR in "/mnt/sd/Roms/PORTS" "/mnt/sd/ports" "/mnt/sd/game"; do\n'
        b'    if [ -d "$PDIR" ]; then\n'
        b'        printf "#!/bin/sh\\nexec \\"%s/Jellyfin_fpl.sh\\"\\n" "$DIR" > "$PDIR/Jellyfin_fpl.sh"\n'
        b'        chmod +x "$PDIR/Jellyfin_fpl.sh"\n'
        b'    fi\n'
        b'done\n'
        b'sync\n'
        b'echo "Jellyfin_fpl ready at $DIR (Desktop shortcuts + Handheld Ports installed)!"\n'
    )

    total_lib = sum(p.stat().st_size for p in dist_lib.iterdir() if p.is_file())
    print(f'[4/4] Verified ARMv7 ELF: {out_bin} ({len(raw):,} bytes) + {len(available)} libs ({total_lib / 1048576:.1f} MiB), 0 missing deps')


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--prepare-only', action='store_true', help='Only download/stage Alpine ARMv7 sysroot & runtime libs')
    args = ap.parse_args()

    prepare_sysroot()
    if args.prepare_only:
        return
    run_host_selfcheck()
    out_bin = build_arm_binary()
    verify_and_bundle(out_bin)


if __name__ == '__main__':
    main()
