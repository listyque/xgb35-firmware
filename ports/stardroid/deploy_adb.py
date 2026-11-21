#!/usr/bin/env python3
"""Deploy complete StarDroid port + game MPQs + shortcuts to XGB35 via WinUSB ADB or SD drive."""
import argparse
from pathlib import Path
import shutil
import struct
import sys
import time

PORT = Path(__file__).resolve().parent
DIST = PORT / 'dist'
ADB_DIR = PORT.parents[1] / 'multiboot'
sys.path.insert(0, str(ADB_DIR))

import adb_winusb  # type: ignore


def adb_exec(adb: adb_winusb.Adb, cmd: str, timeout: float = 120.0) -> str:
    stream = adb.open('shell:' + cmd)
    out = b''
    while (chunk := stream.read(timeout=timeout)) is not None:
        out += chunk
    return out.decode('utf-8', 'replace')


def stream_push(adb: adb_winusb.Adb, local: Path, remote: str, mode: int = 0o755) -> None:
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
                if pct >= last_pct + 10:
                    last_pct = pct
                    mb_s = (sent / (1024 * 1024)) / max(0.05, time.time() - started)
                    print(f'  [{pct:3d}%] {local.name}: {sent // (1024 * 1024)}/{size // (1024 * 1024)} MiB ({mb_s:.1f} MiB/s)', flush=True)
    stream.write(b'DONE' + struct.pack('<I', int(time.time())))
    tag, sz = struct.unpack('<4sI', stream.read_exact(8))
    if tag != b'OKAY':
        raise OSError(stream.read_exact(sz).decode(errors='replace'))
    adb_winusb.sync_request(stream, b'QUIT', b'')
    stream.close()


def deploy_via_sd_drive(drive_root: Path) -> None:
    if not drive_root.exists():
        raise SystemExit(f'Drive path not found: {drive_root}')
    target = drive_root / 'ports' / 'stardroid'
    print(f'Copying complete StarDroid bundle to {target} ...')
    for src in sorted(DIST.rglob('*')):
        rel = src.relative_to(DIST)
        dst = target / rel
        if src.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.exists() and dst.stat().st_size == src.stat().st_size and src.suffix.lower() == '.mpq':
                print(f'  [skip identical] {rel}')
                continue
            print(f'  [copy] {rel} ({src.stat().st_size:,} bytes)')
            shutil.copy2(src, dst)
    for pdir in (drive_root / 'Roms' / 'PORTS', drive_root / 'ports', drive_root / 'game'):
        if pdir.is_dir():
            (pdir / 'StarDroid.sh').write_bytes(b'#!/bin/sh\nexec "/mnt/sd/ports/stardroid/StarDroid.sh"\n')
    print('Copied to SD card! Run `sh /mnt/sd/ports/stardroid/install_on_device.sh` once on console to add X11 Desktop icons.')


def deploy_via_adb(skip_campaign_mpqs: bool = False, bin_only: bool = False) -> None:
    usb = adb_winusb.WinUsb()
    try:
        adb = adb_winusb.Adb(usb)
        print(f'Connected to XGB35 ADB: {adb.banner}')
        remote_root = '/mnt/sd/ports/stardroid'
        sd_chk = adb_exec(adb, '/bin/mount-games >/dev/null 2>&1 || true; ls /mnt/sd >/dev/null 2>&1 && echo SD_OK || echo SD_ERR').strip()
        if 'SD_OK' not in sd_chk:
            raise SystemExit('ERROR: /mnt/sd is not readable on console (reseat microSD card and reboot console, or pass --sd-drive E:\\)!')
        dirs = sorted({remote_root + '/' + p.relative_to(DIST).as_posix() for p in DIST.rglob('*') if p.is_dir()})
        mkdir_cmd = 'mkdir -p ' + ' '.join(f'"{d}"' for d in [remote_root, *dirs])
        adb_exec(adb, mkdir_cmd)

        # Query existing remote file sizes (BusyBox-compatible xargs stat) so re-runs skip 1 GB MPQs
        listing = adb_exec(adb, f'find "{remote_root}" -type f -print0 2>/dev/null | xargs -0 stat -c "%s %n" 2>/dev/null || true')
        existing = {}
        for line in listing.splitlines():
            parts = line.strip().split(' ', 1)
            if len(parts) == 2 and parts[0].isdigit():
                existing[parts[1]] = int(parts[0])

        files = [p for p in sorted(DIST.rglob('*')) if p.is_file()]
        for idx, src in enumerate(files, 1):
            if bin_only and src.suffix.lower() in ('.mpq', '.scx', '.scm'):
                continue
            if skip_campaign_mpqs and src.name.lower() in ('starcraft.mpq', 'broodwar.mpq'):
                continue
            rel = src.relative_to(DIST).as_posix()
            rem = f'{remote_root}/{rel}'
            sz = src.stat().st_size
            is_exec = src.suffix == '.sh' or src.parent.name in ('bin', 'lib')
            if existing.get(rem) == sz and not is_exec and src.name != 'stardroid.desktop':
                print(f'[{idx}/{len(files)}] Up-to-date: {rel} ({sz:,} bytes)')
                continue
            print(f'[{idx}/{len(files)}] Pushing {rel} ({sz:,} bytes)...', flush=True)
            stream_push(adb, src, rem, mode=0o755 if is_exec else 0o644)

        print('Running install_on_device.sh to register Desktop & Handheld shortcuts...')
        print(adb_exec(adb, f'sh "{remote_root}/install_on_device.sh"'))
    finally:
        usb.close()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sd-drive', type=Path, help='Copy directly to mounted microSD drive root (e.g. E:\\)')
    ap.add_argument('--bin-only', action='store_true', help='Push only updated stardroid binary & launcher scripts (~4.6 MB, 1s)')
    ap.add_argument('--lite', action='store_true', help='Skip 1.8 GB campaign/movie MPQs (push core 220 MB + maps only)')
    ap.add_argument('--selftest', action='store_true', help='Verify dist/ completeness & ADB framing offline')
    args = ap.parse_args()

    required = ('bin/stardroid', 'StarDroid.sh', 'StarDroid-Desktop.sh', 'stardroid.desktop',
                'install_on_device.sh', 'data/StarDat.mpq', 'data/BrooDat.mpq', 'data/Patch_rt.mpq')
    for r in required:
        assert (DIST / r).is_file(), f'Missing {DIST / r}'
    if args.selftest:
        adb_winusb.selftest()
        total = sum(p.stat().st_size for p in DIST.rglob('*') if p.is_file())
        print(f'Bundle self-test OK: {len(list(DIST.rglob("*")))} entries, {total:,} bytes ({total / (1024**3):.2f} GiB)')
        return

    if args.sd_drive:
        deploy_via_sd_drive(args.sd_drive)
    else:
        deploy_via_adb(skip_campaign_mpqs=args.lite, bin_only=args.bin_only)


if __name__ == '__main__':
    main()
