"""One-command Autonomous MicroSD Dual-Boot installer for XGB35 (Alpine Linux + GUI + Stock 480x320 OS).

Installs everything onto MicroSD (/dev/mmcblk0) with zero internal NAND writes by default:
  - LBA 0:        Standard MBR (Partition 1 = FAT32 LBA starting at 16 MiB / LBA 32768)
  - LBA 1..20653: Autonomous BootROM ActB SPL + 128MB DDR3 init + Kernel + Dual-Boot Initramfs + DTB
  - /mnt/sd/linux/alpine.ext4:      Alpine Linux v3.20 armv7 + 5 Libretro cores + htop + nano + xgbretro
  - /mnt/sd/linux/stock-system.img: Genuine 480x320 Stock XGB35 OS rootfs (update.zip)
  - /mnt/sd/linux/stock-res.img:    Genuine 480x320 Stock XGB35 UI resources (s480320_style_1)

Usage:
  python install_dualboot.py --offline-check   verify all PC-side Dual-Boot bundles & hashes offline
  python install_dualboot.py                   inspect SD card & install Dual-Boot (prompts if non-empty SD)
  python install_dualboot.py --format-sd       repartition & format MicroSD without prompting
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

import adb_winusb

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parents[1] / 'firmware/diagnostic'))
from prepare_diagnostic import read_uimage, sha256  # noqa: E402

SD_BOOT_DIR = ROOT / 'sd-boot-autonomous'
MISC_DIR = ROOT / 'nand-misc-autonomous'
ALPINE_TAR = ROOT / 'alpine-bundle' / 'alpine-xgb35-rootfs.tar.gz'
ALPINE_MAN = ROOT / 'alpine-bundle' / 'manifest.json'
STOCK_SYS = ROOT / 'update.zip_DATA' / '3.system.img'
STOCK_RES = ROOT / 'update.zip_DATA' / '5.res.img'
BACKUP_DIR = ROOT.parent / 'nand-partitions-backup'
PART1_START_LBA = 32768  # 16 MiB


def make_mbr(total_sectors, start_lba=PART1_START_LBA):
    assert total_sectors > start_lba + 262144, f'SD card too small: {total_sectors} sectors'
    mbr = bytearray(512)
    part_sectors = total_sectors - start_lba
    # Partition 1 entry at 0x1BE: status=0x00, CHS=0xFEFFFF, type=0x0C (FAT32 LBA), CHS_end=0xFEFFFF
    struct.pack_into('<BBBBBBBBII', mbr, 0x1BE,
                     0x00, 0xFE, 0xFF, 0xFF, 0x0C, 0xFE, 0xFF, 0xFF, start_lba, part_sectors)
    mbr[510:512] = b'\x55\xaa'
    return bytes(mbr)


def offline_check():
    assert BACKUP_DIR.exists() and (BACKUP_DIR / 'nand0p1-MISC.img').exists(), 'Missing NAND backups'
    sd_man = json.loads((SD_BOOT_DIR / 'manifest.json').read_text())
    sd_raw = (SD_BOOT_DIR / sd_man['file']).read_bytes()
    assert len(sd_raw) == sd_man['bytes'] and sha256(sd_raw) == sd_man['sha256']
    assert sd_man['end_lba'] < PART1_START_LBA

    misc_man = json.loads((MISC_DIR / 'manifest.json').read_text())
    for name, meta in misc_man.items():
        raw = (MISC_DIR / name).read_bytes()
        assert len(raw) == meta['bytes'] and sha256(raw) == meta['sha256'], f'MISC mismatch: {name}'
    read_uimage((MISC_DIR / 'uImage').read_bytes())
    read_uimage((MISC_DIR / 'ramdisk.img').read_bytes())

    alp_man = json.loads(ALPINE_MAN.read_text())
    alp_raw = ALPINE_TAR.read_bytes()
    assert len(alp_raw) == alp_man['rootfs_tar_gz']['bytes']
    assert sha256(alp_raw) == alp_man['rootfs_tar_gz']['sha256']

    for sq in (STOCK_SYS, STOCK_RES):
        assert sq.exists() and sq.read_bytes()[:4] == b'hsqs', f'Expected little-endian SquashFS: {sq}'

    mbr_test = make_mbr(15523840)
    assert len(mbr_test) == 512 and mbr_test[510:512] == b'\x55\xaa'

    summary = {
        'sd_boot_bytes': len(sd_raw),
        'sd_boot_sha256': sd_man['sha256'],
        'alpine_tar_gz': {'bytes': len(alp_raw), 'sha256': sha256(alp_raw)},
        'stock_system_img': {'bytes': STOCK_SYS.stat().st_size, 'sha256': sha256(STOCK_SYS.read_bytes())},
        'stock_res_img': {'bytes': STOCK_RES.stat().st_size, 'sha256': sha256(STOCK_RES.read_bytes())},
    }
    print('Offline Autonomous SD Dual-Boot verification PASSED:')
    print(json.dumps(summary, indent=2))
    return summary


def sh(cmd, timeout=180):
    usb = adb_winusb.WinUsb()
    try:
        stream, out = adb_winusb.Adb(usb).open('shell:' + cmd), b''
        while (chunk := stream.read(timeout=timeout)) is not None:
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            out += chunk
        return out.decode('utf-8', 'replace')
    finally:
        usb.close()


def push_bytes(data, remote_path):
    tmp = ROOT / 'sd-boot-autonomous' / '_push.tmp'
    tmp.write_bytes(data)
    try:
        push_file(tmp, remote_path)
    finally:
        tmp.unlink(missing_ok=True)


def push_file(local_path, remote_path):
    usb = adb_winusb.WinUsb()
    try:
        adb_winusb.push(adb_winusb.Adb(usb), str(local_path), remote_path)
    finally:
        usb.close()


def install_live(format_sd=False):
    summary = offline_check()
    if not adb_winusb.present():
        if adb_winusb.present('VID_10D6&PID_10D6'):
            print('Console detected in ADFU mode; booting Dual-Boot RAM Linux...', flush=True)
            subprocess.check_call([sys.executable, str(ROOT / 'ram_linux_cycle.py')])
        else:
            raise SystemExit('Console not connected over USB (neither ADB FE02 nor ADFU 10D6 present).')

    info = sh('uname -a; cat /proc/partitions; cat /sys/block/mmcblk0/size 2>/dev/null || echo NO_SD')
    if 'NO_SD' in info or 'mmcblk0' not in info:
        raise SystemExit('MicroSD card (/dev/mmcblk0) was not detected in the console slot!')
    total_sectors = int([ln.strip() for ln in info.splitlines() if ln.strip().isdigit()][-1])
    print(f'Detected MicroSD card: {total_sectors} sectors ({total_sectors * 512 // (1024*1024)} MiB)')

    # Check existing partition 1 start LBA & contents before repartitioning
    p1_start = sh('cat /sys/block/mmcblk0/mmcblk0p1/start 2>/dev/null || echo 0').strip()
    p1_start_lba = int(p1_start) if p1_start.isdigit() else 0
    existing = sh('mkdir -p /mnt/sd; umount /mnt/sd 2>/dev/null || true; '
                  'if [ -b /dev/mmcblk0p1 ]; then mount -t vfat -o ro /dev/mmcblk0p1 /mnt/sd 2>/dev/null && ls -A /mnt/sd && umount /mnt/sd; fi || true').strip()

    need_repart = (p1_start_lba < PART1_START_LBA)
    if need_repart and existing and not format_sd:
        raise SystemExit(
            f'MicroSD has existing files ({existing[:120]}) and Partition 1 starts at LBA {p1_start_lba} (< {PART1_START_LBA}).\n'
            'Re-run with --format-sd if it is OK to format this MicroSD card.'
        )

    sd_lba1 = (SD_BOOT_DIR / 'sd-boot-lba1.bin').read_bytes()
    if need_repart or format_sd or p1_start_lba == 0:
        print('Partitioning MicroSD (16 MiB raw boot area + FAT32 data partition)...', flush=True)
        full_boot = make_mbr(total_sectors) + sd_lba1
        nblocks64k = (len(full_boot) + 65535) // 65536
        full_boot += bytes(nblocks64k * 65536 - len(full_boot))
        expected_sha = sha256(full_boot)
        push_bytes(full_boot, '/tmp/sd-boot-full.bin')
        verify = sh(
            f'umount /mnt/sd 2>/dev/null || true; '
            f'dd if=/tmp/sd-boot-full.bin of=/dev/mmcblk0 bs=65536 count={nblocks64k} && sync; '
            f'rm -f /tmp/sd-boot-full.bin; echo 3 > /proc/sys/vm/drop_caches; '
            f'dd if=/dev/mmcblk0 bs=65536 count={nblocks64k} 2>/dev/null | sha256sum; '
            f'hdparm -z /dev/mmcblk0; mdev -s 2>/dev/null || true; sleep 1; '
            f'mkfs.vfat /dev/mmcblk0p1 && mkdir -p /mnt/sd && mount -t vfat -o rw,noatime,iocharset=utf8 /dev/mmcblk0p1 /mnt/sd && df -m /mnt/sd'
        )
        assert expected_sha in verify, f'SD boot area readback SHA-256 mismatch!'
    else:
        print(f'Updating SD boot payload in-place (Partition 1 already at LBA {p1_start_lba})...', flush=True)
        full_boot = make_mbr(total_sectors) + sd_lba1
        nblocks64k = (len(full_boot) + 65535) // 65536
        full_boot += bytes(nblocks64k * 65536 - len(full_boot))
        expected_sha = sha256(full_boot)
        push_bytes(full_boot, '/tmp/sd-boot-full.bin')
        verify = sh(
            f'dd if=/tmp/sd-boot-full.bin of=/dev/mmcblk0 bs=65536 count={nblocks64k} && sync; '
            f'rm -f /tmp/sd-boot-full.bin; echo 3 > /proc/sys/vm/drop_caches; '
            f'dd if=/dev/mmcblk0 bs=65536 count={nblocks64k} 2>/dev/null | sha256sum; '
            f'mkdir -p /mnt/sd && (mountpoint -q /mnt/sd || mount -t vfat -o rw,noatime,iocharset=utf8 /dev/mmcblk0p1 /mnt/sd)'
        )
        assert expected_sha in verify, 'SD boot readback SHA-256 mismatch!'

    if '--boot-only' in sys.argv:
        sh('mkdir -p /alpine/mnt/sd /alpine/mnt/udisk /alpine/mnt/media; ln -sf . /alpine/alpine 2>/dev/null || true; '
           'for sub in sd udisk media; do mountpoint -q /mnt/$sub && (mountpoint -q /alpine/mnt/$sub || mount --bind /mnt/$sub /alpine/mnt/$sub); done; sync')
        if '--install-superx' in sys.argv:
            install_superx()
        if '--install-onion' in sys.argv:
            install_onion()
        print('\nSD bootloader updated & SHA-256 verified!')
        return

    sh('mkdir -p /mnt/sd/linux /mnt/sd/game/nes /mnt/sd/game/gb /mnt/sd/game/gba /mnt/sd/game/md /alpine')

    # 1. Create 128 MiB /mnt/sd/linux/alpine.ext4 on MicroSD & unpack Alpine v3.20 + Libretro cores
    push_file(ALPINE_TAR, '/tmp/alpine.tar.gz')
    alp_out = sh(
        'set -e; umount /alpine 2>/dev/null || true; losetup -d /dev/loop0 2>/dev/null || true; '
        'rm -rf /tmp/alp && mkdir -p /tmp/alp && tar -xzf /tmp/alpine.tar.gz -C /tmp/alp 2>/dev/null; '
        '[ -s /mnt/sd/linux/alpine.ext4 ] || for i in 1 2 3 4; do dd if=/dev/zero bs=1M count=32 2>/dev/null; echo "  alloc $((i*32))/128 MB" >&2; done > /mnt/sd/linux/alpine.ext4; '
        'mkdir -p /tmp/alp/img /tmp/alp/dev && mount --bind /dev /tmp/alp/dev && mount --bind /mnt/sd/linux /tmp/alp/img; '
        'chroot /tmp/alp /sbin/mke2fs -F -t ext2 /img/alpine.ext4 >/dev/null; '
        'umount /tmp/alp/img && umount /tmp/alp/dev && rm -rf /tmp/alp; '
        'mount -o loop /mnt/sd/linux/alpine.ext4 /alpine; '
        'tar -xzf /tmp/alpine.tar.gz -C /alpine 2>/dev/null && rm -f /tmp/alpine.tar.gz; sync; '
        "chroot /alpine /bin/ash -c 'echo ALPINE_OK; cat /etc/alpine-release; ls /usr/lib/libretro/*.so | wc -l'"
    )
    assert 'ALPINE_OK' in alp_out, 'Alpine ext4 setup on MicroSD failed'

    # 2. Push genuine 480x320 Stock OS SquashFS images onto MicroSD (/mnt/sd/linux/)
    for local, remote, key in (
        (STOCK_SYS, '/mnt/sd/linux/stock-system.img', 'stock_system_img'),
        (STOCK_RES, '/mnt/sd/linux/stock-res.img', 'stock_res_img'),
    ):
        push_file(local, remote)
        got = sh(f'sha256sum {remote}').split()[0]
        assert got == summary[key]['sha256'], f'SHA256 mismatch on {remote}: {got}'

    sh('sync; /bin/mount-games; ls -lh /mnt/sd/linux')
    print('\nAutonomous MicroSD Dual-Boot installation COMPLETE and SHA-256 verified!')


def install_superx():
    tgz_path = ROOT / 'superx-bundle' / 'superx-xgb35-sd.tar.gz'
    assert tgz_path.exists(), f'Missing {tgz_path}'
    total = tgz_path.stat().st_size
    print(f'Pushing SuperX V1.1 ({total // 1048576} MB compressed) via binary ADB sync to /mnt/sd/superx.tar.gz ...', flush=True)
    usb = adb_winusb.WinUsb()
    try:
        adb = adb_winusb.Adb(usb)
        stream = adb.open('sync:')
        adb_winusb.sync_request(stream, b'SEND', b'/mnt/sd/superx.tar.gz,33261')
        sent, last_mb = 0, 0
        with open(tgz_path, 'rb') as f:
            while chunk := f.read(0x10000):
                adb_winusb.sync_request(stream, b'DATA', chunk)
                sent += len(chunk)
                mb = sent // 1048576
                if mb - last_mb >= 50:
                    print(f'  pushed {mb}/{total // 1048576} MB', flush=True)
                    last_mb = mb
        import time as _t
        stream.write(b'DONE' + struct.pack('<I', int(_t.time())))
        tag, size = struct.unpack('<4sI', stream.read_exact(8))
        if tag != b'OKAY':
            raise OSError(stream.read_exact(size).decode(errors='replace'))
        adb_winusb.sync_request(stream, b'QUIT', b'')
        stream.close()
    finally:
        usb.close()
    print('Extracting /mnt/sd/superx.tar.gz on console (takes ~1-2 min)...', flush=True)
    out = sh('set -e; (while [ -f /mnt/sd/superx.tar.gz ]; do sleep 5; echo "  extracting..." >&2; done) & '
             'tar -xzf /mnt/sd/superx.tar.gz -C /mnt/sd && rm -f /mnt/sd/superx.tar.gz && sync && ls -lh /mnt/sd/rootfs.ext3 && echo SUPERX_INSTALL_OK',
             timeout=300)
    assert 'SUPERX_INSTALL_OK' in out, 'SuperX extraction failed'


def install_onion():
    tgz_path = ROOT / 'onionos-bundle' / 'onion-xgb35-sd.tar.gz'
    assert tgz_path.exists(), f'Missing {tgz_path}'
    total = tgz_path.stat().st_size
    print(f'Pushing Onion OS Open MainUI ({total // 1048576} MB compressed) to /mnt/sd/onion.tar.gz ...', flush=True)
    push_file(tgz_path, '/mnt/sd/onion.tar.gz')
    out = sh('set -e; rm -rf /mnt/sd/onion; tar -xzf /mnt/sd/onion.tar.gz -C /mnt/sd && rm -f /mnt/sd/onion.tar.gz && sync && ls -lh /mnt/sd/onion/bin/MainUI && echo ONION_INSTALL_OK',
             timeout=180)
    assert 'ONION_INSTALL_OK' in out, 'Onion OS extraction failed'


if __name__ == '__main__':
    if '--offline-check' in sys.argv:
        offline_check()
    else:
        install_live(format_sd='--format-sd' in sys.argv)
