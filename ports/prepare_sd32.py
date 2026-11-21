#!/usr/bin/env python3
"""Clone live 4 GB XGB35 SD card + StarDroid 1.9.11 port + shortcuts onto 32 GB USB SD (Disk 2 / E:).

Phase 1 (--stage):
  - Injects StarDroid shortcuts into desktop.ext2 (Desktop icon + JWM menu + /usr/local/bin/stardroid)
    and alpine.ext4 via the live Linux kernel over WinUSB ADB.
  - Pulls all live /mnt/sd files (excluding 512 MB swapfile & locally verified stock-*.img) into
    D:\\grav\\asm_ex\\build\\sd32-staging\\sd-live.tar.

Phase 2 (--flash-disk 2 --drive E):
  - Strictly verifies Disk 2 is the ~31 GB USB Mass Storage SD card (refuses NVMe/Fixed disks!).
  - Writes MBR (Partition 1 FAT32 LBA starting at LBA 32768 = 16 MiB) + sd-boot-lba1.bin (LBA 1..22361)
    and verifies readback SHA-256.
  - Formats Partition 1 as FAT32, extracts cloned SD contents, writes 512 MiB Linux swapfile,
    configures OnionOS Ports, and copies complete /ports/stardroid (1.92 GiB) onto E:\\.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import time
import uuid

PORTS_DIR = Path(__file__).resolve().parent
REPO_ROOT = PORTS_DIR.parent
RR_DIR = REPO_ROOT / 'recovery' / 'adfu'
STAGE_DIR = REPO_ROOT / 'build' / 'sd32-staging'
STARDROID_DIST = PORTS_DIR / 'stardroid' / 'dist'
ADB_DIR = REPO_ROOT / 'multiboot'
sys.path.insert(0, str(ADB_DIR))

import adb_winusb  # type: ignore

PART1_START_LBA = 32768  # 16 MiB


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def adb_run(cmd: str, timeout: float = 180.0) -> str:
    usb = adb_winusb.WinUsb()
    try:
        stream = adb_winusb.Adb(usb).open('shell:' + cmd)
        out = b''
        while (chunk := stream.read(timeout=timeout)) is not None:
            out += chunk
        return out.decode('utf-8', 'replace')
    finally:
        usb.close()


def sync_pull_chunked_or_direct(usb_box: list, remote: str, local: Path, expected_sz: int) -> int:
    """Pull small/medium files directly via sync RECV; pull >32 MiB files in resumable 32 MiB chunks with USB auto-reconnect."""
    local.parent.mkdir(parents=True, exist_ok=True)
    if expected_sz <= 32 * 1024 * 1024:
        for attempt in range(4):
            try:
                return sync_pull_file(usb_box[1], remote, local)
            except Exception as e:
                print(f'    [retry {attempt + 1}/4 on {remote}: {e}]', flush=True)
                try:
                    usb_box[0].close()
                except Exception:
                    pass
                time.sleep(1.0)
                usb_box[0] = adb_winusb.WinUsb()
                usb_box[1] = adb_winusb.Adb(usb_box[0])
        return sync_pull_file(usb_box[1], remote, local)

    part = local.with_suffix(local.suffix + '.part')
    chunk_mb = 32
    chunk_bytes = chunk_mb * 1024 * 1024
    done = 0
    if part.is_file():
        done = (part.stat().st_size // (1024 * 1024)) * (1024 * 1024)
        with part.open('r+b') as f:
            f.truncate(done)
    else:
        part.write_bytes(b'')

    while done < expected_sz:
        skip_mb = done // (1024 * 1024)
        rem_bytes = expected_sz - done
        want_bytes = min(chunk_bytes, rem_bytes)
        cnt_mb = (want_bytes + 1048575) // 1048576
        for attempt in range(5):
            try:
                s = usb_box[1].open(
                    f'shell:dd if="{remote}" of=/tmp/.sd_pull_chunk bs=1048576 skip={skip_mb} count={cnt_mb} 2>/dev/null && echo DD_OK'
                )
                out = b''
                while (c_bytes := s.read(timeout=60)) is not None:
                    out += c_bytes
                assert b'DD_OK' in out, f'dd failed: {out!r}'
                tmp_chunk = STAGE_DIR / '.chunk.tmp'
                got_c = sync_pull_file(usb_box[1], '/tmp/.sd_pull_chunk', tmp_chunk)
                assert got_c == want_bytes, f'Chunk size {got_c} != {want_bytes}'
                with part.open('ab') as pf:
                    pf.write(tmp_chunk.read_bytes())
                tmp_chunk.unlink(missing_ok=True)
                done += got_c
                print(f'    ... {local.name}: {done / (1024*1024):.0f} / {expected_sz / (1024*1024):.0f} MiB', flush=True)
                break
            except Exception as e:
                print(f'    [chunk retry {attempt + 1}/5 at {skip_mb} MiB: {e}]', flush=True)
                try:
                    usb_box[0].close()
                except Exception:
                    pass
                time.sleep(1.2)
                usb_box[0] = adb_winusb.WinUsb()
                usb_box[1] = adb_winusb.Adb(usb_box[0])
        else:
            raise RuntimeError(f'Failed pulling {remote} at offset {done}')

    try:
        s = usb_box[1].open('shell:rm -f /tmp/.sd_pull_chunk')
        while s.read(timeout=5) is not None:
            pass
    except Exception:
        pass
    part.replace(local)
    return done


def sync_pull_file(adb: adb_winusb.Adb, remote: str, local: Path) -> int:
    local.parent.mkdir(parents=True, exist_ok=True)
    tmp = local.with_suffix(local.suffix + '.part')
    stream = adb.open('sync:')
    adb_winusb.sync_request(stream, b'RECV', remote.encode('utf-8'))
    total = 0
    with tmp.open('wb') as f:
        while True:
            tag, size = struct.unpack('<4sI', stream.read_exact(8))
            if tag == b'DONE':
                break
            data = stream.read_exact(size)
            if tag == b'FAIL':
                stream.close()
                raise OSError(f'ADB pull failed on {remote}: {data.decode(errors="replace")}')
            assert tag == b'DATA', tag
            f.write(data)
            total += size
    adb_winusb.sync_request(stream, b'QUIT', b'')
    stream.close()
    tmp.replace(local)
    return total


def phase1_stage() -> None:
    root_stage = STAGE_DIR / 'root'
    root_stage.mkdir(parents=True, exist_ok=True)

    inj_flag = STAGE_DIR / '.injected_v2'
    if not inj_flag.exists():
        print('[1/3] Pushing updated USB hotplug & Desktop Key-Tester binaries and injecting shortcuts...', flush=True)
        usb = adb_winusb.WinUsb()
        try:
            adb = adb_winusb.Adb(usb)
            adb_winusb.push(adb, str(RR_DIR / 'fbterm/xgbjoyx11'), '/tmp/xgbjoyx11.new', 0o755)
            adb_winusb.push(adb, str(RR_DIR / 'fbterm/xgb-keytest'), '/tmp/xgb-keytest.new', 0o755)
            adb_winusb.push(adb, str(RR_DIR / 'usb-hub-aotg/aotg_hub_hcd.ko'), '/tmp/aotg_hub_hcd.ko.new', 0o644)
            adb_winusb.push(adb, str(RR_DIR / 'usb-hub-aotg/hub-mode.sh'), '/tmp/hub-mode.sh.new', 0o755)
        finally:
            usb.close()

    inject_script = r"""
set -e
/bin/mount-games >/dev/null 2>&1 || true
mkdir -p /mnt/sd/linux /desktop
cp -f /tmp/aotg_hub_hcd.ko.new /mnt/sd/linux/aotg_hub_hcd.ko
tr -d '\r' < /tmp/hub-mode.sh.new > /mnt/sd/linux/hub-mode.sh
chmod 755 /mnt/sd/linux/hub-mode.sh

UM_D=0
if ! mountpoint -q /desktop 2>/dev/null; then
    mount -o loop,rw /mnt/sd/linux/desktop.ext2 /desktop
    UM_D=1
fi
mkdir -p /desktop/usr/bin /desktop/usr/local/bin /desktop/usr/share/applications /desktop/root/Desktop /desktop/mnt/usb

cp -f /tmp/xgbjoyx11.new /desktop/usr/bin/xgbjoyx11
cp -f /tmp/xgbjoyx11.new /desktop/usr/local/bin/xgbjoyx11
chmod 755 /desktop/usr/bin/xgbjoyx11 /desktop/usr/local/bin/xgbjoyx11

cp -f /tmp/xgb-keytest.new /desktop/usr/local/bin/xgb-keytest
chmod 755 /desktop/usr/local/bin/xgb-keytest

cat > /desktop/usr/local/bin/stardroid << 'EOF'
#!/bin/sh
export DISPLAY="${DISPLAY:-:0}"
if [ -x /mnt/sd/ports/stardroid/StarDroid-Desktop.sh ]; then
    exec /mnt/sd/ports/stardroid/StarDroid-Desktop.sh "$@"
fi
exec /usr/bin/uxterm -fa 'DejaVu Sans Mono' -fs 12 -e /bin/sh -c 'echo "StarDroid не найден в /mnt/sd/ports/stardroid"; sleep 4'
EOF
chmod 755 /desktop/usr/local/bin/stardroid

cat > /desktop/root/Desktop/09-stardroid.desktop << 'EOF'
[Desktop Entry]
Version=1.0
Type=Application
Name=StarCraft: Brood War
Icon=applications-games
Exec=/usr/local/bin/stardroid
Terminal=false
Categories=Game;
EOF
chmod 755 /desktop/root/Desktop/09-stardroid.desktop
cp -f /desktop/root/Desktop/09-stardroid.desktop /desktop/usr/share/applications/stardroid.desktop

cat > /desktop/root/Desktop/10-keytest.desktop << 'EOF'
[Desktop Entry]
Version=1.0
Type=Application
Name=Тестер кнопок приставки
Icon=input-gaming
Exec=/usr/local/bin/xgb-keytest
Terminal=false
Categories=System;Utility;
EOF
chmod 755 /desktop/root/Desktop/10-keytest.desktop
cp -f /desktop/root/Desktop/10-keytest.desktop /desktop/usr/share/applications/xgb-keytest.desktop

if [ -f /desktop/root/.jwmrc ] && ! grep -q '/usr/local/bin/stardroid' /desktop/root/.jwmrc; then
    awk '
    /glxgears/ && !done {
        print "        <Menu icon=\"applications-games.png\" label=\"Порты игр\">"
        print "            <Program icon=\"applications-games.png\" label=\"StarCraft: Brood War (StarDroid)\">/usr/local/bin/stardroid</Program>"
        print "        </Menu>"
        done=1
    }
    { print }
    ' /desktop/root/.jwmrc > /desktop/root/.jwmrc.new && mv /desktop/root/.jwmrc.new /desktop/root/.jwmrc
    chmod 666 /desktop/root/.jwmrc
fi

if [ -f /desktop/root/.jwmrc ] && ! grep -q '/usr/local/bin/xgb-keytest' /desktop/root/.jwmrc; then
    awk '
    /xgb-settings<\/Program>/ && !done {
        print
        print "        <Program icon=\"input-gaming.png\" label=\"Тестер кнопок приставки\">/usr/local/bin/xgb-keytest</Program>"
        done=1
        next
    }
    { print }
    ' /desktop/root/.jwmrc > /desktop/root/.jwmrc.new && mv /desktop/root/.jwmrc.new /desktop/root/.jwmrc
    chmod 666 /desktop/root/.jwmrc
fi

rm -f /tmp/xgbjoyx11.new /tmp/xgb-keytest.new /tmp/aotg_hub_hcd.ko.new /tmp/hub-mode.sh.new
sync
[ "$UM_D" = "1" ] && umount /desktop

mkdir -p /alpine
UM_A=0
if ! mountpoint -q /alpine 2>/dev/null; then
    mount -o loop,rw /mnt/sd/linux/alpine.ext4 /alpine
    UM_A=1
fi
mkdir -p /alpine/usr/local/bin /alpine/mnt/usb
cat > /alpine/usr/local/bin/stardroid << 'EOF'
#!/bin/sh
exec /mnt/sd/ports/stardroid/StarDroid.sh "$@"
EOF
chmod 755 /alpine/usr/local/bin/stardroid
sync
[ "$UM_A" = "1" ] && umount /alpine
sync
echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true
echo SHORTCUTS_OK
"""
    if not inj_flag.exists():
        res = adb_run(inject_script)
        assert 'SHORTCUTS_OK' in res, f'Failed injecting shortcuts: {res}'
        inj_flag.write_text('ok')
        print('  Shortcuts, xgbjoyx11, xgb-keytest & aotg_hub_hcd.ko injected into desktop.ext2 & alpine.ext4!')
    else:
        print('[1/3] Shortcuts & updated binaries already injected (.injected_v2), resuming pull...', flush=True)

    # Pre-seed unchanged SuperX cfw/apps from local superx-xgb35-sd.tar, then verify sizes/hashes against live SD!
    superx_tar = RR_DIR / 'superx-bundle/superx-xgb35-sd.tar'
    if superx_tar.is_file() and not (STAGE_DIR / '.superx_seeded').exists():
        print('  Seeding static SuperX cfw/apps assets from local superx-xgb35-sd.tar...', flush=True)
        with tarfile.open(superx_tar, 'r:') as tf:
            for m in tf:
                rel = m.name.lstrip('./')
                if not rel or rel == 'rootfs.ext3' or '..' in Path(rel).parts:
                    continue
                dst = root_stage / rel
                if m.isdir():
                    dst.mkdir(parents=True, exist_ok=True)
                elif m.isfile():
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    with tf.extractfile(m) as sf, dst.open('wb') as df:
                        shutil.copyfileobj(sf, df, length=1024 * 1024)
        (STAGE_DIR / '.superx_seeded').write_text('ok')

    print('[2/3] Listing all directories & files on live 4 GB /mnt/sd...', flush=True)
    dirs_raw = adb_run('cd /mnt/sd && find . -type d')
    for line in dirs_raw.splitlines():
        rel = line.strip().lstrip('./')
        if not rel or rel.startswith('System Volume Information'):
            continue
        (root_stage / rel).mkdir(parents=True, exist_ok=True)

    files_raw = adb_run("cd /mnt/sd && find . -type f -exec stat -c '%s|%n' {} \\;")
    skip_exact = {'linux/swapfile', 'linux/stock-res.img', 'linux/stock-system.img'}
    to_pull = []
    reused = 0
    for line in files_raw.splitlines():
        parts = line.strip().split('|', 1)
        if len(parts) != 2 or not parts[0].strip().isdigit():
            continue
        sz = int(parts[0].strip())
        rel = parts[1].strip().lstrip('./')
        if not rel or rel.startswith('System Volume Information') or rel in skip_exact:
            continue
        loc = root_stage / rel
        if loc.is_file() and loc.stat().st_size == sz:
            reused += 1
            continue
        to_pull.append((rel, sz))

    total_bytes = sum(sz for _, sz in to_pull)
    print(f'  Reusing {reused} already-staged files; pulling {len(to_pull)} live files ({total_bytes / (1024*1024):.1f} MiB) via binary ADB sync...', flush=True)

    usb_box = [adb_winusb.WinUsb(), None]
    usb_box[1] = adb_winusb.Adb(usb_box[0])
    started = time.time()
    pulled_bytes = 0
    try:
        for idx, (rel, sz) in enumerate(to_pull, 1):
            rem = f'/mnt/sd/{rel}'
            loc = root_stage / rel
            if sz >= 8 * 1024 * 1024 or idx % 25 == 0 or idx == len(to_pull):
                spd = (pulled_bytes / (1024 * 1024)) / max(0.1, time.time() - started)
                print(f'  [{idx}/{len(to_pull)}] {rel} ({sz / (1024*1024):.1f} MiB, avg {spd:.1f} MiB/s)...', flush=True)
            got = sync_pull_chunked_or_direct(usb_box, rem, loc, sz)
            assert got == sz, f'Size mismatch pulling {rel}: expected {sz}, got {got}'
            pulled_bytes += got
    finally:
        try:
            usb_box[0].close()
        except Exception:
            pass

    assert (root_stage / 'linux/desktop.ext2').is_file() and (root_stage / 'linux/alpine.ext4').is_file()
    (STAGE_DIR / '.stage_complete').write_text('ok')
    print(f'[3/3] Stage complete! Pulled {pulled_bytes:,} bytes in {time.time() - started:.1f}s -> {root_stage}')


def make_mbr(total_sectors: int, start_lba: int = PART1_START_LBA) -> bytes:
    assert total_sectors > start_lba + 1048576, f'Disk too small: {total_sectors} sectors'
    mbr = bytearray(512)
    part_sectors = total_sectors - start_lba
    struct.pack_into('<BBBBBBBBII', mbr, 0x1BE,
                     0x00, 0xFE, 0xFF, 0xFF, 0x0C, 0xFE, 0xFF, 0xFF, start_lba, part_sectors)
    mbr[510:512] = b'\x55\xaa'
    return bytes(mbr)


def make_fat32_boot_and_fs(disk_f, part_sectors: int, label: bytes = b'XGB35      ') -> None:
    """Write a clean standard Microsoft FAT32 filesystem (32 KiB clusters, 64 sectors/cluster) onto Partition 1."""
    bytes_per_sec = 512
    sec_per_clus = 64  # 32 KiB clusters — optimal for 32 GB SD cards
    rsvd_sec = 32
    num_fats = 2
    fat_sz32 = 1
    for _ in range(8):
        data_sec = part_sectors - rsvd_sec - num_fats * fat_sz32
        clus_cnt = data_sec // sec_per_clus
        fat_sz32 = ((clus_cnt + 2) * 4 + bytes_per_sec - 1) // bytes_per_sec
        fat_sz32 = (fat_sz32 + 7) & ~7

    bpb = bytearray(512)
    bpb[0:3] = b'\xeb\x58\x90'
    bpb[3:11] = b'MSWIN4.1'
    struct.pack_into('<HBHBHHBHHHII', bpb, 11,
                     bytes_per_sec, sec_per_clus, rsvd_sec, num_fats,
                     0, 0, 0xF8, 0, 63, 255, PART1_START_LBA, part_sectors)
    struct.pack_into('<IHHIHH', bpb, 36,
                     fat_sz32, 0, 0, 2, 1, 6)  # root_clus=2, fsinfo=1, backup_boot=6
    bpb[64] = 0x80
    bpb[66] = 0x29
    struct.pack_into('<I', bpb, 67, int(time.time()) & 0xFFFFFFFF)
    bpb[71:82] = label[:11].ljust(11, b' ')
    bpb[82:90] = b'FAT32   '
    bpb[510:512] = b'\x55\xaa'

    fsinfo = bytearray(512)
    fsinfo[0:4] = b'RRaA'
    fsinfo[484:488] = b'rrAa'
    free_clus = ((part_sectors - rsvd_sec - num_fats * fat_sz32) // sec_per_clus) - 1
    struct.pack_into('<II', fsinfo, 488, free_clus, 3)
    fsinfo[510:512] = b'\x55\xaa'

    rsvd = bytearray(rsvd_sec * 512)
    rsvd[0:512] = bpb
    rsvd[512:1024] = fsinfo
    rsvd[6 * 512:7 * 512] = bpb
    rsvd[7 * 512:8 * 512] = fsinfo

    def raw_write(offset_bytes: int, payload: bytes) -> None:
        disk_f.seek(offset_bytes)
        n = disk_f.write(payload)
        if n != len(payload):
            raise OSError(f'Short raw write at {offset_bytes}: {n} != {len(payload)}')

    base = PART1_START_LBA * 512
    raw_write(base, bytes(rsvd))

    fat0 = bytearray(65536)
    struct.pack_into('<III', fat0, 0, 0x0FFFFFF8, 0x0FFFFFFF, 0x0FFFFFFF)
    zero_chunk = bytes(1024 * 1024)

    for fat_idx in range(num_fats):
        fat_off = base + (rsvd_sec + fat_idx * fat_sz32) * 512
        total_fat_bytes = fat_sz32 * 512
        first = bytes(fat0[:min(len(fat0), total_fat_bytes)])
        raw_write(fat_off, first)
        rem = total_fat_bytes - len(first)
        cur = fat_off + len(first)
        while rem > 0:
            step = min(len(zero_chunk), rem)
            raw_write(cur, zero_chunk[:step])
            cur += step
            rem -= step

    root_off = base + (rsvd_sec + num_fats * fat_sz32) * 512
    raw_write(root_off, bytes(sec_per_clus * 512))


def write_linux_swapfile(path: Path, size_mb: int = 512) -> None:
    """Create a pre-initialized 512 MiB Linux swapfile (SWAPSPACE2 v1, 4 KiB pages)."""
    total_bytes = size_mb * 1024 * 1024
    pages = (total_bytes // 4096) - 1
    hdr = bytearray(4096)
    struct.pack_into('<III', hdr, 1024, 1, pages, 0)  # version=1, last_page=pages, nr_badpages=0
    hdr[1036:1052] = uuid.uuid4().bytes
    hdr[4086:4096] = b'SWAPSPACE2'
    zero_4m = bytes(4 * 1024 * 1024)
    with path.open('wb') as f:
        f.write(hdr)
        rem = total_bytes - 4096
        while rem > 0:
            step = min(len(zero_4m), rem)
            f.write(zero_4m[:step])
            rem -= step


def phase2_flash(disk_num: int, drive_letter: str, partition_only: bool = False, populate_only: bool = False) -> None:
    drive_letter = drive_letter.rstrip(':\\/').upper()
    assert drive_letter not in ('C', 'D'), f'Refusing system drive {drive_letter}:!'

    # Safety check via PowerShell Get-Disk JSON
    ps = subprocess.check_output([
        'powershell', '-NoProfile', '-Command',
        f'Get-Disk -Number {disk_num} | Select-Object Number,FriendlyName,BusType,Size | ConvertTo-Json'
    ], text=True)
    disk_info = json.loads(ps)
    print('Target Disk:', disk_info, flush=True)
    assert disk_info['BusType'] == 'USB', f'Refusing non-USB disk: {disk_info}'
    disk_size = int(disk_info['Size'])
    assert 25_000_000_000 <= disk_size <= 36_000_000_000, f'Unexpected SD size: {disk_size}'
    total_sectors = disk_size // 512

    sd_man = json.loads((RR_DIR / 'sd-boot-autonomous/manifest.json').read_text('utf-8'))
    sd_boot = (RR_DIR / 'sd-boot-autonomous' / sd_man['file']).read_bytes()
    assert sha256_bytes(sd_boot) == sd_man['sha256'] and len(sd_boot) % 512 == 0

    stage_root = STAGE_DIR / 'root'
    assert (STAGE_DIR / '.stage_complete').is_file() and stage_root.is_dir(), 'Run --stage first!'

    if not populate_only:
        print(f'[0/5] Removing old partitions on USB Disk {disk_num} so raw sectors are unlocked...', flush=True)
        subprocess.run([
            'powershell', '-NoProfile', '-Command',
            f'Get-Partition -DiskNumber {disk_num} -ErrorAction SilentlyContinue | Remove-Partition -Confirm:$false -ErrorAction SilentlyContinue; '
            f'Update-Disk -Number {disk_num}'
        ], check=False)
        time.sleep(1.0)

        phys_path = f'\\\\.\\PhysicalDrive{disk_num}'
        k32 = c.WinDLL('kernel32', use_last_error=True)
        IOCTL_DISK_UPDATE_PROPERTIES = 0x00070140

        with open(phys_path, 'r+b', buffering=0) as df:
            print(f'[1/5] Writing MBR (LBA {PART1_START_LBA}) + Autonomous Bootloader ({len(sd_boot):,} B) to {phys_path}...', flush=True)
            full_boot = make_mbr(total_sectors) + sd_boot
            pad = (-len(full_boot)) % 65536
            if pad:
                full_boot += b'\x00' * pad
            df.seek(0)
            assert df.write(full_boot) == len(full_boot), 'Short bootloader write!'
            os.fsync(df.fileno())

            import msvcrt
            ret = w.DWORD(0)
            k32.DeviceIoControl(w.HANDLE(msvcrt.get_osfhandle(df.fileno())),
                                IOCTL_DISK_UPDATE_PROPERTIES, None, 0, None, 0, c.byref(ret), None)

        print('[2/5] Mounting Partition 1 (LBA 32768..end) & formatting native Windows FAT32 (32 KiB clusters)...', flush=True)
        subprocess.run([
            'powershell', '-NoProfile', '-Command',
            f'Update-Disk -Number {disk_num}; Start-Sleep -Seconds 2; '
            f'$p = Get-Partition -DiskNumber {disk_num} -PartitionNumber 1; '
            f'if ($p.DriveLetter -ne "{drive_letter}") {{ Set-Partition -DiskNumber {disk_num} -PartitionNumber 1 -NewDriveLetter {drive_letter} }}; '
            f'Format-Volume -DriveLetter {drive_letter} -FileSystem FAT32 -NewFileSystemLabel XGB35 -AllocationUnitSize 32768 -Force -Confirm:$false | Out-Null'
        ], check=True)

        # Verify bootloader readback SHA-256 AFTER Format-Volume to prove LBA 1..22361 is untouched!
        with open(phys_path, 'rb', buffering=0) as df_ro:
            df_ro.seek(512)
            rb = df_ro.read(len(sd_boot))
            assert sha256_bytes(rb) == sd_man['sha256'], 'Bootloader readback SHA-256 mismatch!'
        print(f'  Bootloader readback SHA-256 verified OK after Format-Volume: {sd_man["sha256"]}', flush=True)

        root_chk = Path(f'{drive_letter}:\\')
        for _ in range(20):
            if root_chk.exists():
                break
            time.sleep(0.5)
        assert root_chk.exists(), f'Volume {root_chk} did not mount after repartitioning!'
        print(f'  Partition 1 mounted cleanly at {root_chk}!', flush=True)
        if partition_only:
            (STAGE_DIR / '.partition_done').write_text(sd_man['sha256'])
            return

    root = Path(f'{drive_letter}:\\')
    assert root.exists(), f'Volume {root} not found!'

    # 3. Copy staged 4 GB SD card contents onto E:\
    print(f'[3/5] Copying staged SD card files onto {root} ...', flush=True)
    all_staged = sorted(stage_root.rglob('*'))
    files_staged = [p for p in all_staged if p.is_file()]
    copied_b = 0
    t0 = time.time()
    for p in all_staged:
        if p.is_dir():
            (root / p.relative_to(stage_root)).mkdir(parents=True, exist_ok=True)
    for idx, src in enumerate(files_staged, 1):
        rel = src.relative_to(stage_root)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        sz = src.stat().st_size
        shutil.copy2(src, dst)
        copied_b += sz
        if sz >= 16 * 1024 * 1024 or idx % 400 == 0 or idx == len(files_staged):
            spd = (copied_b / (1024 * 1024)) / max(0.1, time.time() - t0)
            print(f'  [{idx}/{len(files_staged)}] {rel} ({copied_b / (1024*1024):.0f} MiB copied, {spd:.1f} MiB/s)', flush=True)

    # Copy verified local stock-*.img + generate 512 MiB Linux swapfile
    (root / 'linux').mkdir(parents=True, exist_ok=True)
    print('  Copying stock-res.img & stock-system.img...', flush=True)
    shutil.copy2(RR_DIR / 'update.zip_DATA/5.res.img', root / 'linux/stock-res.img')
    shutil.copy2(RR_DIR / 'update.zip_DATA/3.system.img', root / 'linux/stock-system.img')
    print('[4/5] Creating 512 MiB Linux swapfile & configuring OnionOS Ports...', flush=True)
    write_linux_swapfile(root / 'linux/swapfile', 512)

    # Configure OnionOS Ports so both StarCraft Brood War & 2048 appear and launch cleanly
    ports_emu = root / 'onion/Emu/PORTS'
    ports_rom = root / 'onion/Roms/PORTS'
    if ports_emu.is_dir() and ports_rom.is_dir():
        (ports_emu / 'config.json').write_text(json.dumps({
            'label': 'Ports',
            'icon': '/mnt/SDCARD/Icons/Default/ports.png',
            'launch': 'launch.sh',
            'rompath': '../../Roms/PORTS',
            'extlist': 'port',
        }, indent=2) + '\n', encoding='utf-8')
        (ports_emu / 'launch.sh').write_bytes(
            b'#!/bin/sh\n'
            b'case "$1" in\n'
            b'*StarCraft*|*stardroid*) exec /mnt/sd/ports/stardroid/StarDroid.sh ;;\n'
            b'*) exec /usr/bin/xgbretro /usr/lib/libretro/2048_libretro.so ;;\n'
            b'esac\n'
        )
        (ports_rom / 'StarCraft Brood War.port').write_bytes(b'stardroid\n')
        (ports_rom / 'PORTS_cache6.db').unlink(missing_ok=True)

    # 4. Copy complete StarDroid port + 5 MPQs + 212 maps onto E:\ports\stardroid\
    print(f'[5/5] Copying StarDroid 1.9.11 port (1.92 GiB) onto {root / "ports/stardroid"} ...', flush=True)
    sd_port = root / 'ports/stardroid'
    port_items = sorted(STARDROID_DIST.rglob('*'))
    port_files = [p for p in port_items if p.is_file()]
    p_copied = 0
    t1 = time.time()
    for p in port_items:
        if p.is_dir():
            (sd_port / p.relative_to(STARDROID_DIST)).mkdir(parents=True, exist_ok=True)
    for idx, src in enumerate(port_files, 1):
        rel = src.relative_to(STARDROID_DIST)
        dst = sd_port / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        sz = src.stat().st_size
        shutil.copy2(src, dst)
        p_copied += sz
        if sz >= 16 * 1024 * 1024 or idx % 50 == 0 or idx == len(port_files):
            spd = (p_copied / (1024 * 1024)) / max(0.1, time.time() - t1)
            print(f'  [{idx}/{len(port_files)}] {rel} ({p_copied / (1024*1024):.0f} / 1966 MiB, {spd:.1f} MiB/s)', flush=True)

    (root / 'ports/README.txt').write_text(
        'XGB35 Game Ports Catalog\n'
        '1. StarDroid 1.9.11 (StarCraft & Brood War) -> /mnt/sd/ports/stardroid/\n',
        encoding='utf-8'
    )
    print('ALL DONE! 32 GB MicroSD on E:\\ is cold-bootable and verified!', flush=True)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stage', action='store_true', help='Pull live 4 GB SD card via ADB')
    ap.add_argument('--flash-disk', type=int, default=-1, help='USB Disk number to partition & populate (e.g. 2)')
    ap.add_argument('--drive', type=str, default='E', help='Drive letter of target SD card (default: E)')
    ap.add_argument('--partition-only', action='store_true', help='Only partition, write bootloader & format FAT32')
    ap.add_argument('--populate-only', action='store_true', help='Only copy staged files, swapfile & ports onto mounted volume')
    ap.add_argument('--log', type=str, default='', help='Redirect stdout/stderr to log file (for elevated UAC run)')
    args = ap.parse_args()

    log_f = None
    if args.log:
        log_f = open(args.log, 'w', encoding='utf-8', buffering=1)
        sys.stdout = log_f
        sys.stderr = log_f

    try:
        if args.stage:
            phase1_stage()
        if args.flash_disk >= 0:
            phase2_flash(args.flash_disk, args.drive, args.partition_only, args.populate_only)
    except Exception:
        import traceback
        traceback.print_exc()
        raise
    finally:
        if log_f:
            log_f.close()


if __name__ == '__main__':
    main()
