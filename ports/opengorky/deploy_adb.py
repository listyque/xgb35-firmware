#!/usr/bin/env python3
"""Deploy OpenGorky port + Gorky 18 game assets (DAT, avi, adjust.kdt) + shortcuts to XGB35 via WinUSB ADB or SD drive."""
import argparse
from pathlib import Path
import shutil
import struct
import sys
import time

PORT = Path(__file__).resolve().parent
DIST = PORT / 'dist'
G18_ASSETS = Path(r'D:\grav\g18')
ADB_DIR = PORT.parents[1] / 'multiboot'
sys.path.insert(0, str(ADB_DIR))

import adb_winusb  # type: ignore


def adb_exec(adb: adb_winusb.Adb, cmd: str, timeout: float = 120.0) -> str:
    stream = adb.open('shell:' + cmd)
    out = b''
    while (chunk := stream.read(timeout=timeout)) is not None:
        out += chunk
    return out.decode('utf-8', 'replace')


def stream_push(adb: adb_winusb.Adb, local: Path, remote: str, mode: int = 0o644) -> None:
    size = local.stat().st_size
    started = time.time()
    stream = adb.open('sync:')
    adb_winusb.sync_request(stream, b'SEND', f'{remote},{mode}'.encode())
    sent = 0
    last_pct = -1
    with local.open('rb') as f:
        while chunk := f.read(0x10000):
            adb_winusb.sync_request(stream, b'DATA', chunk)
            sent += len(chunk)
            if size >= 16 * 1024 * 1024:
                pct = sent * 100 // size
                if pct >= last_pct + 20:
                    last_pct = pct
                    mb_s = (sent / (1024 * 1024)) / max(0.05, time.time() - started)
                    print(f'    [{pct:3d}%] {local.name}: {sent // (1024 * 1024)}/{size // (1024 * 1024)} MiB ({mb_s:.1f} MiB/s)', flush=True)
    stream.write(b'DONE' + struct.pack('<I', int(time.time())))
    tag, sz = struct.unpack('<4sI', stream.read_exact(8))
    if tag != b'OKAY':
        raise OSError(stream.read_exact(sz).decode(errors='replace'))
    adb_winusb.sync_request(stream, b'QUIT', b'')
    stream.close()


def collect_game_files(include_avi: bool) -> list[tuple[Path, str]]:
    """Return list of (local_path, rel_path_under_game_dir) for Gorky 18 assets."""
    out: list[tuple[Path, str]] = []
    kdt = G18_ASSETS / 'adjust.kdt'
    if kdt.is_file():
        out.append((kdt, 'adjust.kdt'))
    dat_dir = G18_ASSETS / 'DAT'
    if dat_dir.is_dir():
        for p in sorted(dat_dir.rglob('*')):
            if p.is_file():
                rel = p.relative_to(dat_dir).as_posix()
                if rel.lower().startswith('common/'):
                    rel = rel.lower()
                out.append((p, 'dat/' + rel))
    if include_avi:
        avi_dir = G18_ASSETS / 'avi'
        if avi_dir.is_dir():
            for p in sorted(avi_dir.rglob('*')):
                if p.is_file():
                    out.append((p, 'avi/' + p.relative_to(avi_dir).as_posix()))
    return out


def deploy_via_sd_drive(drive_root: Path, bin_only: bool, include_avi: bool) -> None:
    if not drive_root.exists():
        raise SystemExit(f'Drive path not found: {drive_root}')
    target = drive_root / 'ports' / 'opengorky'
    print(f'Copying OpenGorky bundle to {target} ...')
    for src in sorted(DIST.rglob('*')):
        rel = src.relative_to(DIST)
        dst = target / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
    if not bin_only:
        game_files = collect_game_files(include_avi)
        for idx, (src, rel) in enumerate(game_files, 1):
            dst = target / 'game' / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.exists() and dst.stat().st_size == src.stat().st_size:
                continue
            if idx % 25 == 1 or src.stat().st_size >= 5 * 1024 * 1024:
                print(f'  [{idx}/{len(game_files)}] copy {rel} ({src.stat().st_size:,} bytes)')
            shutil.copy2(src, dst)
    for pdir in (drive_root / 'Roms' / 'PORTS', drive_root / 'ports', drive_root / 'game'):
        if pdir.is_dir():
            (pdir / 'OpenGorky.sh').write_bytes(b'#!/bin/sh\nexec "/mnt/sd/ports/opengorky/OpenGorky.sh"\n')
    print('Copied to SD card! Run `sh /mnt/sd/ports/opengorky/install_on_device.sh` once on console to register shortcuts.')


def deploy_via_adb(bin_only: bool = False, include_avi: bool = True) -> None:
    usb = adb_winusb.WinUsb()
    try:
        adb = adb_winusb.Adb(usb)
        print(f'Connected to XGB35 ADB: {adb.banner}')
        remote_root = '/mnt/sd/ports/opengorky'
        sd_chk = adb_exec(adb, '/bin/mount-games >/dev/null 2>&1 || true; ls /mnt/sd >/dev/null 2>&1 && echo SD_OK || echo SD_ERR').strip()
        if 'SD_OK' not in sd_chk:
            raise SystemExit('ERROR: /mnt/sd is not readable on console (reseat microSD or pass --sd-drive E:\\)!')

        # Create dist directories
        dist_dirs = sorted({remote_root + '/' + p.relative_to(DIST).as_posix() for p in DIST.rglob('*') if p.is_dir()})
        all_dirs = [remote_root, remote_root + '/game', remote_root + '/data/opengorky/saves', *dist_dirs]
        game_files = [] if bin_only else collect_game_files(include_avi)
        game_dirs = sorted({remote_root + '/game/' + Path(rel).parent.as_posix() for _, rel in game_files if Path(rel).parent.as_posix() != '.'})
        all_dirs.extend(game_dirs)
        for i in range(0, len(all_dirs), 40):
            chunk = all_dirs[i:i + 40]
            adb_exec(adb, 'mkdir -p ' + ' '.join(f'"{d}"' for d in chunk))

        # Query existing remote file sizes so re-runs skip unchanged game assets
        listing = adb_exec(adb, f'find "{remote_root}" -type f -print0 2>/dev/null | xargs -0 stat -c "%s %n" 2>/dev/null || true')
        existing: dict[str, int] = {}
        for line in listing.splitlines():
            parts = line.strip().split(' ', 1)
            if len(parts) == 2 and parts[0].isdigit():
                existing[parts[1]] = int(parts[0])

        # Push dist files
        dist_files = [p for p in sorted(DIST.rglob('*')) if p.is_file()]
        for idx, src in enumerate(dist_files, 1):
            rel = src.relative_to(DIST).as_posix()
            rem = f'{remote_root}/{rel}'
            sz = src.stat().st_size
            is_exec = src.suffix == '.sh' or src.parent.name in ('bin', 'lib')
            print(f'[dist {idx}/{len(dist_files)}] Pushing {rel} ({sz:,} bytes)...', flush=True)
            stream_push(adb, src, rem, mode=0o755 if is_exec else 0o644)

        # Push game assets (skipping unchanged sizes)
        pushed = 0
        skipped = 0
        for idx, (src, rel) in enumerate(game_files, 1):
            rem = f'{remote_root}/game/{rel}'
            sz = src.stat().st_size
            if existing.get(rem) == sz:
                skipped += 1
                continue
            pushed += 1
            if pushed <= 5 or pushed % 20 == 0 or sz >= 4 * 1024 * 1024:
                print(f'[game {idx}/{len(game_files)}] Pushing {rel} ({sz:,} bytes)...', flush=True)
            stream_push(adb, src, rem, mode=0o644)
        if game_files:
            print(f'Game assets synced: {pushed} pushed, {skipped} up-to-date.')

        print('Running install_on_device.sh to register Desktop & Handheld shortcuts...')
        print(adb_exec(adb, f'sh "{remote_root}/install_on_device.sh"'))
    finally:
        usb.close()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sd-drive', type=Path, help='Copy directly to mounted microSD drive root (e.g. E:\\)')
    ap.add_argument('--bin-only', action='store_true', help='Push only updated opengorky binary & scripts (skip DAT/avi)')
    ap.add_argument('--no-avi', action='store_true', help='Skip avi/ videos (saves 55 MB when ffmpeg is disabled)')
    ap.add_argument('--selftest', action='store_true', help='Verify dist/ completeness & ADB framing offline')
    args = ap.parse_args()

    for r in ('bin/opengorky', 'bin/xmessage', 'bench-menu.sh', 'OpenGorky.sh', 'OpenGorky-Desktop.sh', 'opengorky.desktop', 'install_on_device.sh'):
        assert (DIST / r).is_file(), f'Missing {DIST / r}'
    if args.selftest:
        adb_winusb.selftest()
        gf = collect_game_files(not args.no_avi)
        total_g = sum(p.stat().st_size for p, _ in gf)
        print(f'Self-test OK: dist ready, {len(gf)} game asset files ({total_g / (1024**2):.1f} MiB)')
        return

    if args.sd_drive:
        deploy_via_sd_drive(args.sd_drive, bin_only=args.bin_only, include_avi=not args.no_avi)
    else:
        deploy_via_adb(bin_only=args.bin_only, include_avi=not args.no_avi)


if __name__ == '__main__':
    main()
