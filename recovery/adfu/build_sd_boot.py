"""Build autonomous cold-boot MicroSD payload (LBA 1..~20700) for ATM7051H / XGB35.

Boot chain on cold power-on (no PC, no NAND block 0, never need to remove SD card):
  BootROM (0xFFFF108C) -> MMC0 LBA 1/9/17/25 @ 0xB4068000 ('ActB', 80 bytes)
  -> Stage-2 @ 0xB4068800..0xB406D000:
       * If VOL- or VOL+ is held at power-on -> enter BootROM USB ADFU (0xFFFF4A00) directly!
       * Calibrate 128 MiB DDR3 @ 360 MHz (preserving MMC0/GPIO clocks)
       * Load Image @ 0x8000 + initramfs @ 0x01800000 + DTB @ 0x01E00000 via BootROM MMC0 (0xFFFF1C00)
       * Jump to ARM Linux kernel @ 0x8000 (or fall back to 0xFFFF4A00 ADFU on any error).
"""
import hashlib
import json
import struct

import adfu_usb_storage_ram as storage
from build_autonomous_service import Arm, NATIVE, PARAM, REPORT, ROOT, STACK, branch, mov

OUT = ROOT / 'sd-boot-autonomous'
ROM_MMC = 0xFFFF1C00
ROM_ADFU = 0xFFFF4A00
DDR_ENTRY = 0xB406C800
BOUNCE = 0xB406D000
STAGE2_LBA = 32
STAGE2_SECTORS = 36  # 18 KiB: 0xB4068800 .. 0xB406D000
PAYLOAD_START_LBA = 68
PART1_START_LBA = 32768  # 16 MiB raw boot area; FAT32 partition 1 starts at 16 MiB


def find_dtb_memory_offsets(dtb: bytes) -> tuple[int, int]:
    off_struct, off_strings = struct.unpack_from('>II', dtb, 8)[0:2]
    p, path, mem_found, ion_found = off_struct, [], [], []
    while p < len(dtb):
        tok, = struct.unpack_from('>I', dtb, p); p += 4
        if tok == 1:
            end = dtb.index(0, p)
            path.append(dtb[p:end].decode())
            p = (end + 4) & ~3
        elif tok == 2:
            path.pop()
        elif tok == 3:
            plen, nameoff = struct.unpack_from('>II', dtb, p); p += 8
            pnm = dtb[off_strings + nameoff:dtb.index(0, off_strings + nameoff)].decode()
            if path == ['', 'memory'] and pnm == 'reg' and plen == 8:
                assert struct.unpack_from('>I', dtb, p)[0] == 0
                mem_found.append(p + 4)
            elif path == ['', 'ion_config'] and pnm == 'actions,ion_heap_pmem' and plen == 8:
                ion_found.append(p)
            p = (p + plen + 3) & ~3
        elif tok == 9:
            break
    assert len(mem_found) == 1 and len(ion_found) == 1, (mem_found, ion_found)
    return mem_found[0], ion_found[0]


def build_stage2_ddr_and_loader(segments, dtb_mem_size_addr, dtb_ion_base_addr):
    mb = (ROOT / 'xgb35-mbrec-recovered.bin').read_bytes()
    assert hashlib.sha256(mb).hexdigest() == '34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc'
    regions = [(0x2300, 0x4F1C), (0x5E30, 0x6220), (0x5358, 0x5360), (0x563C, 0x5B94), (0x5DBC, 0x5E30)]
    native = bytearray().join(mb[s:e] for s, e in regions)

    def dest(offset):
        base = NATIVE
        for s, e in regions:
            if s <= offset < e:
                return base + offset - s
            base += e - s
        raise ValueError(f'Unsupported native pointer {offset:x}')

    for off in range(0, len(native), 4):
        val, = struct.unpack_from('<I', native, off)
        if 0xB406A300 <= val < 0xB406E220:
            old = val - 0xB4068000
            struct.pack_into('<I', native, off, dest(old) if any(s <= old < e for s, e in regions) else 0)
    globals_off = dest(0x61E0) - NATIVE
    native[globals_off:globals_off + 0x40] = bytes(0x40)

    def patch(offset, *words):
        struct.pack_into('<' + 'I' * len(words), native, offset - 0x2300, *words)

    patch(0x2608, 0xE12FFF1E)
    for site, again, failed in ((0x3D34, 0x3D28, 0x3EF4), (0x3D68, 0x3D5C, 0x3EF4),
                                (0x3D88, 0x3D7C, 0x3EF4), (0x3DA4, 0x3D9C, 0x3EF4),
                                (0x3E58, 0x3E50, 0x3EF4), (0x4378, 0x4370, 0x4394),
                                (0x43E0, 0x43D8, 0x43FC)):
        at = NATIVE + len(native)
        patch(site, branch(dest(site), at, cond=0))
        ops = [0xE92D0003, *mov(0, REPORT), 0xE5901010, 0xE2511001, 0xE5801010,
               0xE8BD0003, branch(at + 28, dest(again), cond=1), branch(at + 32, dest(failed))]
        native.extend(struct.pack('<' + 'I' * len(ops), *ops))
    assert NATIVE + len(native) <= REPORT

    a = Arm(DDR_ENTRY)
    e = a.emit
    # 1. Snapshot BootROM's live MMC0 pinmux/pull/clock/reset state into REPORT+0x30..0x48 BEFORE touching GPIO/DDR
    e(*mov(4, REPORT), *mov(0, 0xB01C0000), *mov(1, 0xB0150000),
      0xE5902044, 0xE5842030, 0xE5902048, 0xE5842034,
      0xE5902050, 0xE5842038, 0xE5902054, 0xE584203C,
      0xE5912078, 0xE5842040, 0xE59120A0, 0xE5842044,
      0xE59120A8, 0xE5842048)

    # 2. Hold VOL- or VOL+ at power-on -> jump straight to BootROM ADFU (0xFFFF4A00) without removing SD card!
    e(*mov(2, 0x60600000), 0xE5802044, *mov(2, 0x01F88100), 0xE5802050,
      0xE5902020, 0xE3C22001, 0xE5802020,
      0xE5902018, 0xE3822001, 0xE5802018,
      0xE590201C, 0xE3822030, 0xE580201C, 0xE3A020C0)
    a.label('kdelay')
    e(0xE2522001); a.jump('kdelay', 1)
    e(0xE5903020, 0xE2033030,
      0xE5902018, 0xE3C22001, 0xE5802018,
      0xE590201C, 0xE3C22030, 0xE580201C,
      0xE5942030, 0xE5802044, 0xE5942038, 0xE5802050,
      0xE3530030); a.jump('adfu', 1)

    # 3. Calibrate 128 MiB DDR3 @ 360 MHz
    e(0xE5910000, 0xE5840020, 0xE591006C, 0xE5840024, 0xE591001C, 0xE5840028,
      *mov(13, STACK), *mov(0, 0x80000), 0xE5840010)
    e(0xE3A00000); a.call(dest(0x2438))
    e(*mov(0, PARAM)); a.call(dest(0x37FC)); e(0xE3500000); a.jump('fail', 1)
    a.call(dest(0x2E7C)); e(0xE3500000); a.jump('fail', 1)
    e(0xE3A00001); a.call(dest(0x2438))
    e(*mov(1, 0xB0150000), 0xE59100A0, *mov(2, 0x4001A), 0xE1800002, 0xE58100A0, 0xF57FF04F)
    a.call(dest(0x387C))
    e(0xE3A00000); a.call(dest(0x3FFC)); e(0xE3500000); a.jump('fail', 1)
    e(*mov(1, 0x01F00000), *mov(2, 0x44524431), 0xE5812000, 0xF57FF04F,
      0xE5913000, 0xE1530002); a.jump('fail', 1)

    # 4. Restore BootROM MMC0 pinmux/clock/reset WITHOUT calling sb=2 (keeps 0xB4068444 SDHC 0x80 flag intact!)
    e(*mov(4, REPORT), *mov(0, 0xB01C0000), *mov(1, 0xB0150000),
      0xE5942030, 0xE5802044, 0xE5942034, 0xE5802048,
      0xE5942038, 0xE5802050, 0xE594203C, 0xE5802054,
      0xE5942040, 0xE5812078,
      0xE59120A0, 0xE5943044, 0xE1822003, *mov(3, 0x4001A), 0xE1822003, 0xE58120A0,
      0xE59120A8, 0xE5943048, 0xE1822003, 0xE58120A8,
      *mov(2, 0xFF00), 0xE5812080, 0xF57FF04F,
      *mov(13, 0xB406FF80), *mov(10, ROM_MMC))

    # 4. Load Image @ 0x8000, initramfs @ 0x01800000, DTB @ 0x01E00000 in 16-sector (8 KiB) chunks
    for lba, count, ddr_addr in segments:
        e(*mov(4, lba), *mov(5, count), *mov(6, ddr_addr))
        a.jump('load_seg', link=True)

    # 5. Unlock full 256 MiB DDR3 (0x10000000) when 2-Gbit rank is present (CMA & ION stay < 128 MiB!)
    if True:
        e(*mov(0, 0xB0230004), 0xE5900000, 0xE7E20250,
          0xE3500002)
        a.jump('enter_linux', 3)
        e(0xE3500004)
        a.jump('enter_linux', 8)
        e(0xE3A01402, 0xE1A01011, 0xE6BF1F31,
          *mov(2, dtb_mem_size_addr), 0xE5821000)

    # 6. Pre-set factory DEVPLL=480MHz (0x1150 -> SD0=48MHz rock-solid, DECLK=240MHz) BEFORE Linux starts!
    a.label('enter_linux')
    e(*mov(0, 0xB0150000), *mov(1, 0x00001150), 0xE5801004, *mov(2, 0x1000))
    a.label('devpll_wait')
    e(0xE2522001); a.jump('devpll_wait', 1)
    e(*mov(1, 0x00001111), 0xE5801030)
    e(0xF57FF04F, 0xF10C00C0, 0xF1020013, 0xEE110F10, 0xE3C00005, 0xE3C00A01,
      0xEE010F10, 0xF57FF06F, 0xE3A00000, 0xEE070F15, 0xEE080F17, 0xF57FF04F, 0xF57FF06F,
      0xE3A00000, 0xE3E01000, *mov(2, storage.DTB), *mov(3, storage.KERNEL), 0xE12FFF13)

    a.label('fail')
    e(*mov(4, REPORT), *mov(1, 0xB0150000), 0xE5940028, 0xE581001C,
      0xE5940024, 0xE581006C, 0xE5940020, 0xE5810000, 0xF57FF04F)
    a.label('adfu')
    e(*mov(13, 0xB4070000), *mov(12, ROM_ADFU), 0xE12FFF1C)

    a.label('load_seg')
    e(0xE92D4080)
    a.label('chunk_loop')
    e(0xE3550000); a.jump('seg_done', 0)
    e(0xE3550010, 0x23A07010, 0x31A07005)
    e(0xE1A00004, 0xE1A01007, *mov(2, BOUNCE), 0xE3A09004, 0xE12FFF3A, 0xE3500000)
    a.jump('seg_fail', 1)
    e(0xF57FF04F, *mov(0, BOUNCE), 0xE1A01387)
    a.label('wcopy')
    e(0xE4902004, 0xE4862004, 0xE2511001); a.jump('wcopy', 1)
    e(0xE0844007, 0xE0455007); a.jump('chunk_loop')
    a.label('seg_done')
    e(0xE8BD8080)
    a.label('seg_fail')
    e(0xE8BD4080); a.jump('fail')

    a.label('pll_wait')
    e(0xE92D0002, *mov(1, 0x100000))
    a.label('spin')
    e(0xE2511001); a.jump('spin', 1)
    e(0xE8BD0002, 0xE12FFF1E)
    patch(0x3AFC, branch(dest(0x3AFC), a.labels['pll_wait'], link=True))

    entry_code = a.finish()
    buf = bytearray(STAGE2_SECTORS * 512)
    buf[0:1024] = mb[0x30C00:0x31000]
    buf[NATIVE - PARAM:NATIVE - PARAM + len(native)] = native
    buf[DDR_ENTRY - PARAM:DDR_ENTRY - PARAM + len(entry_code)] = entry_code
    assert DDR_ENTRY - PARAM + len(entry_code) <= len(buf), len(entry_code)
    return bytes(buf)


def build_stage1_sector():
    """Minimal 84-byte ActB Stage-1 @ 0xB4068000: disable SRAM D-cache, read Stage-2 -> 0xB4068800, jump to 0xB406C800."""
    a = Arm(0xB4068000)
    e = a.emit
    e(*mov(0, 0xB4068000), *mov(1, 0xB4070000))
    a.label('sram_flush')
    e(0xEE070F3E, 0xE2800020, 0xE1500001); a.jump('sram_flush', 3)
    e(0xF57FF04F, 0xEE110F10, 0xE3C00004, 0xEE010F10, 0xF57FF06F)
    e(*mov(10, ROM_MMC), 0xE3A00020, 0xE3A01024, *mov(2, PARAM), 0xE3A09004, 0xE12FFF3A, 0xE3500000)
    a.jump('adfu', 1)
    e(0xF57FF04F, 0xE3A00000, 0xEE070F15, 0xF57FF04F, 0xF57FF06F, *mov(3, DDR_ENTRY), 0xE12FFF13)
    a.label('adfu')
    e(*mov(13, 0xB4070000), *mov(12, ROM_ADFU), 0xE12FFF1C)

    code = a.finish()
    assert len(code) <= 0x1F4, len(code)
    sector = bytearray(512)
    sector[:len(code)] = code
    struct.pack_into('<II', sector, 0x1F4, 0x42746341, 0x55AA6D72)
    checksum = (sum(struct.unpack_from('<127I', sector, 0)) + 0x1234) & 0xFFFFFFFF
    struct.pack_into('<I', sector, 0x1FC, checksum)
    return bytes(sector)


def build_sd_payload():
    files = storage.build(headless=False, memory256=True, adb=True)
    kernel = bytearray(files['Image'][1])
    storage.patch_kernel_hw(kernel)

    blobs = [(bytes(kernel), storage.KERNEL), (files['initramfs.cpio.gz'][1], storage.INITRD),
             (files['kernel.dtb'][1], storage.DTB)]
    cur_lba = PAYLOAD_START_LBA
    segments, padded_blobs = [], []
    for raw, ddr_addr in blobs:
        pad = raw + bytes(-len(raw) % 512)
        if ddr_addr == storage.INITRD and len(pad) < 7680 * 512:
            pad += bytes(7680 * 512 - len(pad))  # keep DTB LBA stable at 22308 across initramfs edits!
        nsec = len(pad) // 512
        segments.append((cur_lba, nsec, ddr_addr))
        padded_blobs.append(pad)
        cur_lba += nsec
    assert cur_lba < PART1_START_LBA

    mem_off, ion_off = find_dtb_memory_offsets(files['kernel.dtb'][1])
    stage1 = build_stage1_sector()
    stage2 = build_stage2_ddr_and_loader(segments, storage.DTB + mem_off, storage.DTB + ion_off)

    image = bytearray((cur_lba - 1) * 512)
    for lba in (1, 9, 17, 25):
        image[(lba - 1) * 512:lba * 512] = stage1
    splash_bin = ROOT / 'fbterm' / 'boot_splash.bin'
    if splash_bin.exists():
        sb = splash_bin.read_bytes()
        assert len(sb) == 2048
        image[(28 - 1) * 512:31 * 512] = sb
    image[(STAGE2_LBA - 1) * 512:(STAGE2_LBA - 1) * 512 + len(stage2)] = stage2
    for (lba, _, _), pad in zip(segments, padded_blobs):
        image[(lba - 1) * 512:(lba - 1) * 512 + len(pad)] = pad

    OUT.mkdir(exist_ok=True)
    out_file = OUT / 'sd-boot-lba1.bin'
    out_file.write_bytes(image)
    manifest = {
        'file': out_file.name,
        'start_lba': 1,
        'end_lba': cur_lba - 1,
        'part1_min_lba': PART1_START_LBA,
        'stage1_code_bytes': next(i for i in range(0x1F4, 0, -4) if stage1[i - 4:i] != b'\0\0\0\0'),
        'bytes': len(image),
        'sha256': hashlib.sha256(image).hexdigest(),
        'segments': [{'lba': l, 'sectors': c, 'ddr': hex(d)} for l, c, d in segments],
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print(json.dumps(manifest, indent=2))
    return manifest


if __name__ == '__main__':
    build_sd_payload()
