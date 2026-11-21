"""Download and verify Alpine Linux v3.20 armv7 minirootfs + handheld packages & Libretro cores."""
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request
import zlib

ROOT = Path(__file__).resolve().parent
BUNDLE = ROOT / 'alpine-bundle'
CACHE = BUNDLE / 'cache'
STAGE = BUNDLE / 'staging'

MINI_URL = 'https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/armv7/alpine-minirootfs-3.20.10-armv7.tar.gz'
MINI_SHA256 = 'bb903f7e47b4991c91ebf5677bc8f66df5d8e8c24a9c093ba072222243c83b75'

REPOS = [
    ('v3.20/main', 'https://dl-cdn.alpinelinux.org/alpine/v3.20/main/armv7'),
    ('v3.20/community', 'https://dl-cdn.alpinelinux.org/alpine/v3.20/community/armv7'),
    ('edge/testing', 'https://dl-cdn.alpinelinux.org/alpine/edge/testing/armv7'),
]

WANTED = [
    'libgcc', 'libstdc++', 'ncurses-terminfo-base', 'ncurses-libs',
    'e2fsprogs', 'htop', 'nano', 'evtest', 'sdl2', 'sdl12-compat',
    'libretro-2048', 'libretro-fceumm', 'libretro-gambatte', 'libretro-gpsp',
    'libretro-picodrive',
]


def fetch(url, dest):
    if dest.exists() and dest.stat().st_size > 0:
        return dest.read_bytes()
    dest.parent.mkdir(parents=True, exist_ok=True)
    data = urllib.request.urlopen(url, timeout=30).read()
    dest.write_bytes(data)
    return data


def load_indices():
    by_name, by_prov = {}, {}
    for tag, base in REPOS:
        raw = fetch(f'{base}/APKINDEX.tar.gz', CACHE / f'{tag.replace("/", "_")}_APKINDEX.tar.gz')
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
            if entry['name'] not in by_name:
                by_name[entry['name']] = entry
            for p in entry['prov']:
                by_prov.setdefault(p.split('=')[0], entry['name'])
    return by_name, by_prov


def resolve(wanted, by_name, by_prov):
    order, seen = [], set()
    def visit(pkg):
        if pkg in seen or pkg in ('musl', 'busybox', 'alpine-baselayout', 'alpine-keys'):
            return
        seen.add(pkg)
        info = by_name.get(pkg)
        if not info:
            return
        for dep in info['deps']:
            dep_key = dep.split('=')[0].split('<')[0].split('>')[0]
            target = dep_key if dep_key in by_name else by_prov.get(dep_key)
            if target:
                visit(target)
        order.append(info)
    for w in wanted:
        visit(w)
    return order


def safe_members(tf):
    for m in tf.getmembers():
        m.name = m.name.lstrip('./')
        if not m.name or m.name.startswith('.') or '..' in m.name:
            continue
        yield m


def apk_data_tar(raw):
    last = b''
    while raw:
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        last = d.decompress(raw)
        raw = d.unused_data
    return last


def main():
    BUNDLE.mkdir(exist_ok=True)
    mini = fetch(MINI_URL, CACHE / 'alpine-minirootfs-3.20.10-armv7.tar.gz')
    actual_sha = hashlib.sha256(mini).hexdigest()
    assert actual_sha == MINI_SHA256, f'minirootfs SHA256 mismatch: {actual_sha}'

    by_name, by_prov = load_indices()
    pkgs = resolve(WANTED, by_name, by_prov)
    print(f'Resolved {len(pkgs)} packages:', [p['name'] for p in pkgs])

    apk_blobs = []
    for p in pkgs:
        fn = f"{p['name']}-{p['ver']}.apk"
        apk_blobs.append((p['name'], fn, fetch(f"{p['base']}/{fn}", CACHE / fn)))

    # Repack a single clean Alpine rootfs tar.gz (preserving POSIX symlinks & modes inside tar!)
    out_tar = BUNDLE / 'alpine-xgb35-rootfs.tar.gz'
    seen_paths = set()
    ram_extract = {}
    ram_targets = {
        'lib/ld-musl-armhf.so.1',
        'lib/libc.musl-armv7.so.1',
        'usr/lib/libgcc_s.so.1',
        'usr/lib/libstdc++.so.6',
        'usr/lib/libstdc++.so.6.0.32',
        'usr/lib/libretro/2048_libretro.so',
    }

    with tarfile.open(out_tar, 'w:gz', compresslevel=6) as out:
        # 1. Base minirootfs
        with tarfile.open(fileobj=io.BytesIO(mini), mode='r:gz') as base_tf:
            for m in safe_members(base_tf):
                data = base_tf.extractfile(m).read() if m.isfile() else None
                if m.name in ram_targets and data:
                    ram_extract[m.name] = data
                out.addfile(m, io.BytesIO(data) if data is not None else None)
                seen_paths.add(m.name)
        # 2. Overlay resolved APK packages
        for name, fn, raw in apk_blobs:
            with tarfile.open(fileobj=io.BytesIO(apk_data_tar(raw)), mode='r:') as apk_tf:
                for m in safe_members(apk_tf):
                    if m.isdir() and m.name in seen_paths:
                        continue
                    data = apk_tf.extractfile(m).read() if m.isfile() else None
                    if (m.name in ram_targets or m.name.startswith('usr/lib/libstdc++.so.6.')) and data:
                        ram_extract[m.name] = data
                    out.addfile(m, io.BytesIO(data) if data is not None else None)
                    seen_paths.add(m.name)
        xgbretro = ROOT / 'fbterm' / 'xgbretro'
        if xgbretro.exists():
            raw_r = xgbretro.read_bytes()
            ti = tarfile.TarInfo('bin/xgbretro')
            ti.size, ti.mode = len(raw_r), 0o755
            out.addfile(ti, io.BytesIO(raw_r))

    # Save minimal musl-armhf runtime + 2048_libretro.so for instant in-RAM execution
    STAGE.mkdir(exist_ok=True)
    stdc_keys = [k for k in ram_extract if k.startswith('usr/lib/libstdc++.so.6.')]
    assert 'lib/ld-musl-armhf.so.1' in ram_extract and 'usr/lib/libretro/2048_libretro.so' in ram_extract
    assert 'usr/lib/libgcc_s.so.1' in ram_extract and stdc_keys, list(ram_extract)
    (STAGE / 'ld-musl-armhf.so.1').write_bytes(ram_extract['lib/ld-musl-armhf.so.1'])
    (STAGE / 'libgcc_s.so.1').write_bytes(ram_extract['usr/lib/libgcc_s.so.1'])
    (STAGE / 'libstdc++.so.6').write_bytes(ram_extract[stdc_keys[0]])
    (STAGE / '2048_libretro.so').write_bytes(ram_extract['usr/lib/libretro/2048_libretro.so'])

    manifest = {
        'minirootfs_sha256': actual_sha,
        'packages': [f"{p['name']}-{p['ver']}" for p in pkgs],
        'rootfs_tar_gz': {'file': out_tar.name, 'bytes': out_tar.stat().st_size,
                          'sha256': hashlib.sha256(out_tar.read_bytes()).hexdigest()},
    }
    (BUNDLE / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
