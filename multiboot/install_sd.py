"""Verify local XGB35 multiboot inputs or provision a dedicated microSD over WinUSB ADB."""

import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import struct
import tempfile

import adb_winusb


PARTITION_START = 32768
ROOTFS_BYTES = 128 * 1024 * 1024
STOCK_IMAGES = {
    "3.system.img": ("stock-system.img", "0814027cc2fb78ad3fbcf2ea92117b1666532d61d4ea49175f2ad76c2415bd50"),
    "5.res.img": ("stock-res.img", "1f6969cc1fe758d6612e8592eed594ad1fc1b3b6e604237d92922d90905c2775"),
}


def sha256(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def validate_inputs(root):
    boot_dir = root / "sd-boot-autonomous"
    boot = boot_dir / "sd-boot-lba1.bin"
    manifest = json.loads((boot_dir / "manifest.json").read_text(encoding="utf-8"))
    boot_size = boot.stat().st_size
    if (
        manifest["file"] != boot.name or manifest["start_lba"] != 1
        or manifest["part1_min_lba"] != PARTITION_START
        or not 512 <= boot_size < (PARTITION_START - 1) * 512
        or boot_size % 512 or manifest["end_lba"] != boot_size // 512
        or manifest["bytes"] != boot_size or manifest["sha256"] != sha256(boot)
    ):
        raise ValueError("Invalid SD boot image or manifest")
    with boot.open("rb") as file:
        words = struct.unpack("<128I", file.read(512))
    if words[125:127] != (0x42746341, 0x55AA6D72) or words[127] != (sum(words[:127]) + 0x1234) & 0xFFFFFFFF:
        raise ValueError("Invalid Actions BootROM header in LBA 1")
    alpine_dir = root / "alpine-bundle"
    alpine = alpine_dir / "alpine-xgb35-rootfs.tar.gz"
    meta = json.loads((alpine_dir / "manifest.json").read_text(encoding="utf-8"))["rootfs_tar_gz"]
    if meta["file"] != alpine.name or meta["bytes"] != alpine.stat().st_size or meta["sha256"] != sha256(alpine):
        raise ValueError("Alpine archive and manifest differ")
    images = {}
    for source, (name, expected) in STOCK_IMAGES.items():
        image = root / "update.zip_DATA" / source
        if sha256(image) != expected:
            raise ValueError(f"{source}: expected the XGB35 480x320 SYSTEM/RES image")
        images[name] = image
    return boot, alpine, images


def make_mbr(sectors):
    if not 0 < sectors - PARTITION_START <= 0xFFFFFFFF:
        raise ValueError("MicroSD sector count does not fit the MBR")
    sector = bytearray(512)
    struct.pack_into("<B3sB3sII", sector, 446, 0, bytes.fromhex("feffff"), 0x0C,
                     bytes.fromhex("feffff"), PARTITION_START, sectors - PARTITION_START)
    sector[510:] = b"\x55\xaa"
    return sector


def shell(command, timeout=180):
    with closing(adb_winusb.WinUsb()) as usb:
        stream = adb_winusb.Adb(usb).open("shell:set -e; " + command + "; echo XGB35_SD_OK")
        output = bytearray()
        while (chunk := stream.read(timeout=timeout)) is not None:
            output.extend(chunk)
    text = output.decode("utf-8", "replace").strip()
    if not text or text.splitlines()[-1].strip() != "XGB35_SD_OK":
        raise RuntimeError(text or "ADB command failed without output")
    return text.rsplit("XGB35_SD_OK", 1)[0].strip()


def push(local, remote):
    with closing(adb_winusb.WinUsb()) as usb:
        adb_winusb.push(adb_winusb.Adb(usb), str(local), remote)


def install(boot, alpine, images):
    lines = shell("test -x /bin/xgbshell; test -b /dev/mmcblk0; "
                  "for app in dd sha256sum hdparm mdev mkfs.vfat mount umount tar chroot; "
                  "do command -v \"$app\" >/dev/null; done; "
                  "cat /sys/block/mmcblk0/device/type /sys/block/mmcblk0/size").splitlines()
    if len(lines) != 2 or lines[0].strip() != "SD":
        raise ValueError("The XGB35 RAM session did not report an SD card in /dev/mmcblk0")
    sectors = int(lines[1])
    required = ROOTFS_BYTES + sum(image.stat().st_size for image in images.values()) + 16 * 1024 * 1024
    if (sectors - PARTITION_START) * 512 < required:
        raise ValueError("MicroSD has insufficient space for the multiboot data partition")
    print(f"Preparing /dev/mmcblk0 ({sectors * 512 // 1024**2} MiB).", flush=True)
    with tempfile.TemporaryDirectory(prefix="xgb35-sd-") as scratch:
        prefix = Path(scratch) / "boot-prefix.bin"
        data = make_mbr(sectors) + boot.read_bytes()
        prefix.write_bytes(data + bytes(-len(data) % 65536))
        blocks = prefix.stat().st_size // 65536
        expected = sha256(prefix)
        push(prefix, "/tmp/xgb35-boot-prefix.bin")
    got = shell(
        "if grep -q ' /mnt/sd ' /proc/mounts; then umount /mnt/sd; fi; "
        "if grep -q '^/dev/mmcblk0' /proc/mounts; then echo 'SD is still mounted'; exit 1; fi; "
        "test \"$(cat /sys/block/mmcblk0/device/type)\" = SD; "
        f"test \"$(cat /sys/block/mmcblk0/size)\" = {sectors}; "
        f"dd if=/tmp/xgb35-boot-prefix.bin of=/dev/mmcblk0 bs=65536 count={blocks} 2>/dev/null; sync; "
        "echo 3 > /proc/sys/vm/drop_caches; "
        f"dd if=/dev/mmcblk0 bs=65536 count={blocks} 2>/dev/null | sha256sum"
    ).split()[0]
    if got != expected:
        raise RuntimeError("MicroSD boot-sector readback did not match")
    shell("rm -f /tmp/xgb35-boot-prefix.bin; hdparm -z /dev/mmcblk0; "
          "mdev -s; sleep 1; test -b /dev/mmcblk0p1; "
          "test \"$(cat /sys/block/mmcblk0/mmcblk0p1/start)\" = 32768; "
          "mkfs.vfat /dev/mmcblk0p1 >/dev/null; mkdir -p /mnt/sd; "
          "mount -t vfat /dev/mmcblk0p1 /mnt/sd; mkdir -p /mnt/sd/linux")
    push(alpine, "/tmp/xgb35-alpine.tar.gz")
    shell(
        "mkdir -p /tmp/xgb35-rootfs /mnt/xgb35-alpine; "
        "tar -xzf /tmp/xgb35-alpine.tar.gz -C /tmp/xgb35-rootfs; "
        "mkdir -p /tmp/xgb35-rootfs/media/xgb35 /tmp/xgb35-rootfs/dev; "
        "mount -o bind /mnt/sd/linux /tmp/xgb35-rootfs/media/xgb35; "
        "trap 'umount /tmp/xgb35-rootfs/dev 2>/dev/null || true; umount /tmp/xgb35-rootfs/media/xgb35' 0; "
        "mount -o bind /dev /tmp/xgb35-rootfs/dev; "
        "dd if=/dev/zero of=/mnt/sd/linux/alpine.ext4 bs=1048576 count=128 2>/dev/null; "
        "chroot /tmp/xgb35-rootfs /sbin/mke2fs -F -t ext2 /media/xgb35/alpine.ext4 >/dev/null"
    )
    shell(
        "mount -o loop /mnt/sd/linux/alpine.ext4 /mnt/xgb35-alpine; "
        "trap 'umount /mnt/xgb35-alpine' 0; "
        "tar -xzf /tmp/xgb35-alpine.tar.gz -C /mnt/xgb35-alpine; "
        "test -x /mnt/xgb35-alpine/bin/ash; sync; rm -f /tmp/xgb35-alpine.tar.gz"
    )
    for name, image in images.items():
        remote = "/mnt/sd/linux/" + name
        push(image, remote)
        if shell(f"sha256sum {remote}").split()[0] != sha256(image):
            raise RuntimeError(f"MicroSD readback failed for {name}")
    shell("sync; umount /mnt/sd")
    print("MicroSD boot area and stock files verified. The card is unmounted.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True, help="Directory with the local XGB35 build artifacts")
    parser.add_argument("--erase-sd", action="store_true", help="Repartition and format the microSD in the console")
    args = parser.parse_args()
    assets = validate_inputs(args.inputs)
    test_mbr = make_mbr(524288)
    assert test_mbr[510:] == bytes.fromhex("55aa")
    assert struct.unpack_from("<II", test_mbr, 454) == (32768, 491520)
    print("Local SD boot, Alpine and 480x320 stock inputs verified.", flush=True)
    if args.erase_sd:
        install(*assets)


if __name__ == "__main__":
    main()
