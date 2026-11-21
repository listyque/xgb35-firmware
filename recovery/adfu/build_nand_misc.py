"""Build verified NAND MISC (nand0p1) boot files for autonomous U-Boot -> Linux boot."""
import json
import struct
import sys
from pathlib import Path

import adfu_usb_storage_ram as storage
from prepare_diagnostic import make_ramdisk, read_uimage, sha256

OUT = storage.ROOT / 'nand-misc-autonomous'
UENV = b'uenvcmd=setenv os_type linux;\nbootargs.add=rdinit=/init clk_ignore_unused\n'


def build_misc_files():
    update = storage.ROOT.parent / 'misc'
    uimage_bytes = (update / 'uImage').read_bytes()
    uimage_hdr, _ = read_uimage(uimage_bytes)
    assert uimage_hdr[4:6] == (0x8000, 0x8000)

    orig_ramdisk_hdr, _ = read_uimage((update / 'ramdisk.img').read_bytes())
    built = storage.build(headless=False, memory256=True, adb=True)
    _, initrd_gz = built['initramfs.cpio.gz']
    _, dtb = built['kernel.dtb']
    ramdisk_uimage = make_ramdisk(initrd_gz, orig_ramdisk_hdr)
    rd_hdr, rd_payload = read_uimage(ramdisk_uimage)
    assert rd_payload == initrd_gz and rd_hdr[7:11] == (5, 2, 3, 1)

    # Verify U-Boot NAND load map has zero overlaps:
    # uImage @ 0x7fc0, kernel.dtb @ 0xe00000, uEnv @ 0x1000000, ramdisk @ 0x10fffc0, U-Boot @ 0x2000000
    assert 0x7FC0 + len(uimage_bytes) < 0xE00000
    assert 0xE00000 + len(dtb) < 0x1000000
    assert 0x10FFFC0 + len(ramdisk_uimage) < 0x2000000
    assert len(uimage_bytes) + len(ramdisk_uimage) + len(dtb) + len(UENV) < 7 * 1024 * 1024

    OUT.mkdir(exist_ok=True)
    files = {
        'uImage': uimage_bytes,
        'ramdisk.img': ramdisk_uimage,
        'kernel.dtb': dtb,
        'uenv.txt': UENV,
    }
    manifest = {}
    for name, data in files.items():
        (OUT / name).write_bytes(data)
        manifest[name] = {'bytes': len(data), 'sha256': sha256(data)}
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    build_misc_files()
