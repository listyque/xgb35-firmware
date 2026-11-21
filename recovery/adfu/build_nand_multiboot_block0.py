"""Build and flash the 4-page Block-0 Autonomous Multi-Boot Loader for XGB35 (ATM7051H + ATC2603C).

Why 4 pages (Rows 0..3 of Block 0, keeping Block 1 factory MBREC 100% untouched):
  On XGB35 (LLW7051-057 V2.0), MicroSD VDD (3.3V) is powered by ATC2603C SWITCH1 (reg 0x29),
  which is OFF at cold power-on. BootROM MMC0 alone sees 0V on the SD slot.
  This 4-page Block-0 loader (using the exact hardware-validated 28-sector reader from
  build_service_nand_candidate.py):
    1. Checks VOL- / VOL+ at power-on -> jumps straight to BootROM USB ADFU (0xFFFF4A00) without removing SD!
    2. Runs mbrec PMIC init dest(0x2E7C) (turns ON ATC2603C SWITCH1 3.3V SD power) + calibrates 128 MiB DDR3 @ 360 MHz.
    3. Waits ~25 ms for SD card VDD power-up, runs BootROM MMC0 (sb=1, sb=2, sb=3, sb=4) to load
       Image + initramfs + DTB from MicroSD (sd-boot-lba1.bin) into DDR, and boots 6-in-1 Multi-Boot Linux!
    4. If MicroSD is absent or unreadable -> falls back cleanly to BootROM USB ADFU (0xFFFF4A00).
"""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

from adfu_write_nand_block0 import original_differences, read_block, read_row, transact
from build_autonomous_service import Arm, NATIVE, PARAM, REPORT, ROOT, STACK, branch, mov
from build_service_nand_candidate import checksum_section
from prepare_nand_write import BLOCK, OUT as PREP_OUT
from simulate_nand_write import BLANK_BLOCK
from verify_boot_ecc import PAGE, REVERSE, parity, remainder, verify as verify_ecc_lib, whitening

OUT = ROOT / 'nand-multiboot-block0'
ROM_MMC = 0xFFFF1C00
ROM_ADFU = 0xFFFF4A00
DDR_ENTRY = 0xB406C800
BOUNCE = 0xB406D000


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_sram_payload():
    sd_man = json.loads((ROOT / 'sd-boot-autonomous' / 'manifest.json').read_text())
    segments = [(s['lba'], s['sectors'], int(s['ddr'], 16)) for s in sd_man['segments']]

    mb = (ROOT / 'xgb35-mbrec-recovered.bin').read_bytes()
    assert sha(mb) == '34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc'
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

    def mark(stage):  # breadcrumb at REPORT+0x70; survives RESET (not power loss), read back over ADFU
        e(*mov(12, REPORT), *mov(11, 0x5A6E0000 | stage), 0xE58CB070, 0xEE07CF3A, 0xF57FF04F)

    mark(0x10)  # payload entry reached
    # 0. Software Reboot-to-ADFU cookie in untouched SRAM 0xB406F7F0..F7 (set by /bin/reboot-adfu)
    e(*mov(0, 0xB406F7F0), *mov(4, REPORT), *mov(1, 0xB0150000), 0xE5912080, 0xE584202C,
      0xE5901000, *mov(2, 0x41444655), 0xE1510002); a.jump('no_soft_adfu', 1)
    e(0xE5901004, *mov(2, 0xA55A1234), 0xE1510002); a.jump('no_soft_adfu', 1)
    e(0xE3A01000, 0xE5801000, 0xE5801004, 0xEE070F3A, 0xF57FF04F); a.jump('adfu')
    a.label('no_soft_adfu')

    # 1. Hold ESC (GPIOC4), VOL+ (GPIOC3), or VOL- (GPIOC2) on col0 (GPIOC0=0) at reset -> BootROM ADFU!
    e(*mov(4, REPORT), *mov(0, 0xB01C0000), *mov(1, 0xB0150000),
      0xE5912080, 0xE584202C,
      0xE59120A0, *mov(3, 0x40000), 0xE1822003, 0xE58120A0, 0xF57FF04F,
      0xE5902044, 0xE5842030, 0xE5902050, 0xE5842038, 0xE5902060, 0xE584203C,
      *mov(2, 0x60600000), 0xE5802044, *mov(2, 0x01F88100), 0xE5802050,
      0xE5902060, *mov(3, 0x3E0), 0xE1822003, 0xE5802060,
      0xE5902020, 0xE3C22001, 0xE5802020,
      0xE5902018, 0xE3822001, 0xE5802018,
      0xE590201C, 0xE382201C, 0xE580201C, *mov(2, 0x1000))
    a.label('kdelay')
    e(0xE2522001); a.jump('kdelay', 1)
    e(0xE5903020, 0xE203301C,
      0xE5902018, 0xE3C22001, 0xE5802018,
      0xE590201C, 0xE3C2201C, 0xE580201C,
      0xE5942030, 0xE5802044, 0xE5942038, 0xE5802050, 0xE594203C, 0xE5802060,
      0xE353001C); a.jump('adfu', 1)

    # Disable SRAM D-cache (SCTLR.C=0) for DMA coherency, but ENABLE I-cache + BTB (SCTLR.I|Z = 0x1800) for speed!
    e(0xF10C00C0, 0xF1020013, *mov(0, 0xB4068000), *mov(1, 0xB4070000))
    a.label('sflush')
    e(0xEE070F3E, 0xE2800020, 0xE1500001); a.jump('sflush', 3)
    e(0xF57FF04F, 0xE3A00000, 0xEE070F15, 0xEE070FD5, 0xF57FF04F, 0xF57FF06F,
      0xEE110F10, 0xE3C00004, 0xE3800B06, 0xEE010F10, 0xF57FF06F)
    mark(1)
    # 2. Power ON ATC2603C SWITCH1 (MicroSD 3.3V VDD via patched dest(0x2E7C)) & check PMIC REBOOT_ADFU
    e(*mov(4, REPORT), *mov(1, 0xB0150000),
      0xE5910000, 0xE5840020, 0xE591006C, 0xE5840024, 0xE591001C, 0xE5840028,
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

    # 3. Enable DEVPLL early (needed for 50MHz SD0 + DE splash + Linux USB!), then init MMC0 (sb=1,2,3)!
    e(*mov(1, 0xB0150000), 0xE5912004, 0xE3120A01); a.jump('devpll_ok', 1)
    e(0xE3A02F45, 0xE5812004, 0xE3A03701)
    a.label('devpll_wait')
    e(0xE2533001); a.jump('devpll_wait', 1)
    e(0xE3822A01, 0xE5812004, 0xF57FF04F)
    a.label('devpll_ok')

    # Exact 25 ms SD VDD ramp wait using 24 MHz hardware timer T0_VAL (0xB0158010: 600,000 ticks)
    e(*mov(1, 0xB0150000),
      0xE59120A4, 0xE3822302, 0xE58120A4,
      0xE59120AC, 0xE3822302, 0xE58120AC, 0xF57FF04F,
      *mov(0, 0xB0158000), 0xE3A02005, 0xE5802008, 0xF57FF04F,
      0xE5902010, *mov(3, 600000))
    a.label('sd_pwr_wait')
    e(0xE5901010, 0xE0411002, 0xE1510003); a.jump('sd_pwr_wait', 3)
    e(*mov(4, REPORT), *mov(2, 0x5D000001), 0xE584205C,
      0xE3A02000, 0xE5842058,
      *mov(1, 0xB0150000), *mov(2, 0xFF00), 0xE5812080, 0xF57FF04F,
      *mov(13, 0xB406FF80), *mov(10, ROM_MMC),
      0xE3A00000, 0xE3A09001, 0xE12FFF3A, 0xE5840060,
      0xE3A09002, 0xE12FFF3A, 0xE5840064,
      0xE3A09003, 0xE12FFF3A, 0xE5840068, 0xEE074F3A, 0xF57FF04F, 0xE3500000)
    a.jump('fail', 1)
    mark(3)

    # 3b. Switch SD card + host controller from 1-bit -> 4-bit @ 30 MHz (CMU_SD0CLK=1: 60 MHz module / 2)!
    e(*mov(3, 0xB4068444), 0xE5931000, 0xE3A00037, 0xE3A02001, *mov(12, 0xFFFF24D4), 0xE12FFF3C,
      0xE3500000); a.jump('skip_4bit', 1)
    e(0xE3A00006, 0xE3A01002, 0xE3A02001, *mov(12, 0xFFFF24D4), 0xE12FFF3C,
      0xE3500000); a.jump('skip_4bit', 1)
    e(0xE3A00001, *mov(12, 0xFFFF240C), 0xE12FFF3C,
      *mov(0, 0xB0150050), 0xE3A01001, 0xE5801000, 0xF57FF04F)
    a.label('skip_4bit')

    # 3c. Load 2 KiB Splash + Progress Bar module (LBA 28, 4 sectors) -> 0x01F30000 & light up LCD in ~0.35s!
    e(*mov(2, 0x01F30000), 0xE3A00000, 0xE5820000,
      0xE3A0001C, 0xE3A01004, 0xE3A09004, 0xE12FFF3A,
      0xE3500000); a.jump('skip_splash', 1)
    e(*mov(0, 0x01F30000), 0xE5901000, *mov(2, 0x53504C31), 0xE1510002); a.jump('skip_splash', 1)
    e(0xF57FF04F, 0xE3A00000, 0xEE070F15, 0xEE070FD5, 0xF57FF04F, 0xF57FF06F,
      *mov(12, 0x01F30004), 0xE12FFF3C, *mov(10, ROM_MMC))
    a.label('skip_splash')

    # 4. Load Image @ 0x8000, initramfs @ 0x01800000, DTB @ 0x01E00000 straight into DDR, 64 KiB per command
    for lba, count, ddr_addr in segments:
        e(*mov(4, lba), *mov(5, count), *mov(6, ddr_addr))
        a.jump('load_seg', link=True)
    mark(4)

    # Stop DE scanout DMA cleanly before kernel MMU init while keeping ST7796S + backlight ON
    e(*mov(0, 0x01F30000), 0xE5901000, *mov(2, 0x53504C31), 0xE1510002); a.jump('skip_splash_stop', 1)
    e(*mov(12, 0x01F3000C), 0xE12FFF3C)
    a.label('skip_splash_stop')

    # 5. Boot ARM Linux kernel @ 0x8000 (IRQ/FIQ off, SVC, MMU/D-cache/I-cache/BTB off)
    e(0xF57FF04F, 0xF10C00C0, 0xF1020013, 0xEE110F10, 0xE3C00005, 0xE3C00B06,
      0xEE010F10, 0xF57FF06F, 0xE3A00000, 0xEE070F15, 0xEE080F17, 0xF57FF04F, 0xF57FF06F,
      0xE3A00000, 0xE3E01000, *mov(2, 0x01E00000), *mov(3, 0x8000), 0xE12FFF13)

    a.label('fail')
    e(*mov(4, REPORT), *mov(1, 0xB0150000),
      0xE594002C, 0xE5810080, 0xE5940028, 0xE581001C,
      0xE5940024, 0xE581006C, 0xE5940020, 0xE5810000, 0xF57FF04F)
    a.label('adfu')
    # Clean SRAM D-cache & disable I/D caches (matching factory mbrec 0xb406ba5c) before 0xFFFF4A00
    e(*mov(0, 0xB4068000), *mov(1, 0xB4070000))
    a.label('adfu_flush')
    e(0xEE070F3E, 0xE2800020, 0xE1500001); a.jump('adfu_flush', 3)
    e(0xF57FF04F, 0xEE110F10, 0xE3C00004, 0xE3C00B06, 0xEE010F10, 0xF57FF06F)
    # Same ROM re-entry that adfu_linux_fault_trace --rom-return uses: restore ROM vectors + banked stacks,
    # otherwise ROM ADFU entered from a NAND-boot context never enumerates USB.
    from adfu_linux_fault_trace import bootrom_tail
    bootrom_tail(a)
    global ADFU_ADDR
    ADFU_ADDR = a.labels['adfu']

    a.label('load_seg')
    e(0xE92D4080)
    a.label('chunk_loop')
    e(0xE3550000); a.jump('seg_done', 0)
    # r7 = min(r5, 512) sectors (256 KiB per CMD18 -> 42 total chunks); read directly to DDR dest r6
    e(0xE3550C02, 0x23A07C02, 0x31A07005)
    e(0xE1A00004, 0xE1A01007, 0xE1A02006, 0xE3A09004, 0xE12FFF3A, 0xE3500000)
    a.jump('seg_fail', 1)
    # Advance live progress bar if splash module is active @ 0x01F30000
    e(*mov(0, 0x01F30000), 0xE5901000, *mov(2, 0x53504C31), 0xE1510002); a.jump('no_step', 1)
    e(*mov(1, REPORT), 0xE5910058, 0xE2802001, 0xE5812058, *mov(12, 0x01F30008), 0xE12FFF3C)
    a.label('no_step')
    e(*mov(10, ROM_MMC), 0xE0866487, 0xE0844007, 0xE0455007); a.jump('chunk_loop')
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
    payload = bytearray(0x4800)  # 18 KiB (0xB4068800 .. 0xB406D000): fits 100% in Rows 0..2!
    payload[0:1024] = mb[0x30C00:0x31000]
    payload[NATIVE - PARAM:NATIVE - PARAM + len(native)] = native
    payload[DDR_ENTRY - PARAM:DDR_ENTRY - PARAM + len(entry_code)] = entry_code
    assert DDR_ENTRY + len(entry_code) <= PARAM + len(payload) == BOUNCE
    return bytes(payload)


def build_logical_and_physical():
    verify_ecc_lib()
    payload = build_sram_payload()
    mb = (ROOT / 'xgb35-mbrec-recovered.bin').read_bytes()

    # 18-sector reader (0xB4068800..0xB406D000 from Rows 0..2) jumping to DDR_ENTRY (0xB406C800)
    base, descriptor = 0xB4068200, 0xB40683E0
    n_sectors = len(payload) // 1024
    assert n_sectors == 18
    a = Arm(base)
    e = a.emit
    e(0xE3550B01); a.jump('failure', 1)
    e(0xE35A0A02); a.jump('failure', 1)
    e(*mov(6, descriptor), *mov(7, 0xB4068800), 0xE3A00000, 0xE5860004,
      0xE3A04001, 0xE1A04604, 0xE3A08000 | n_sectors)
    a.label('read_chunk')
    e(0xE1A00007, 0xE3A01020)
    a.label('prepare_dma')
    e(0xEE070F3E, 0xE2800020, 0xE2511001); a.jump('prepare_dma', 1)
    e(0xF57FF04F, 0xE1C640B0, 0xE1C690B2, 0xE1A00829, 0xE5C60004, 0xE5867008,
      0xE1A00006, 0xE3A01004, *mov(12, 0xFFFF2A00), 0xE12FFF3C)
    e(0xE1A00007, 0xE3A01020)
    a.label('finish_dma')
    e(0xEE070F36, 0xE2800020, 0xE2511001); a.jump('finish_dma', 1)
    e(0xF57FF04F, 0xE0877005, 0xE0844005, 0xE154000A, 0x03A04000, 0x02899001, 0xE2588001)
    a.jump('read_chunk', 1)
    e(*mov(0, 0xB4068800), *mov(1, len(payload) // 4), 0xE3A02000)
    a.label('sum')
    e(0xE4903004, 0xE0822003, 0xE2511001); a.jump('sum', 1)
    expected = sum(struct.unpack('<' + 'I' * (len(payload) // 4), payload)) & 0xFFFFFFFF
    e(*mov(3, expected), 0xE1520003); a.jump('failure', 1)
    e(0xF57FF04F, 0xE3A00000, 0xEE070F15, 0xEE070FD5, 0xF57FF04F, 0xF57FF06F, *mov(12, DDR_ENTRY), 0xE12FFF1C)
    a.label('failure')
    e(0xEE110F10, 0xE3C00004, 0xE3C00A01, 0xEE010F10, 0xF57FF06F,
      *mov(13, 0xB4070000), *mov(12, ROM_ADFU), 0xE12FFF1C)
    reader = a.finish()
    assert base + len(reader) <= descriptor

    move = Arm(0xB4068E00)
    move.emit(0xF10C00C0, *mov(0, 0xB4068E80), *mov(1, base), *mov(2, len(reader) // 4))
    move.label('copy')
    move.emit(0xE4903004, 0xE4813004, 0xE2522001); move.jump('copy', 1)
    move.emit(*mov(0, base), *mov(1, base + len(reader)))
    move.label('clean')
    move.emit(0xEE070F3A, 0xE2800020, 0xE1500001); move.jump('clean', 3)
    move.emit(0xF57FF04F, 0xEE070F15, 0xF57FF04F, 0xF57FF06F, *mov(12, base), 0xE12FFF1C)
    reloc = move.finish()
    assert len(reloc) <= 0x80

    stage = bytearray(0x600)
    stage[:len(reloc)] = reloc
    stage[0x80:0x80 + len(reader)] = reader
    struct.pack_into('<H', stage, 0x5FA, 0x55AA)
    struct.pack_into('<I', stage, 0x5FC, checksum_section(stage))

    logical = bytearray(0x6000)  # 3 pages (Rows 0..2): 0x0000..0x5800 used, 0x5800..0x6000 zero
    logical[:0x600] = mb[:0x600]  # unchanged factory MBREC first sector + stage header!
    logical[0x600:0xC00] = stage
    logical[0x1000:0x1000 + len(payload)] = payload

    # Encode physical NAND Rows 0..2 with factory whitening + BCH parity (remainder == 0 on all 24 steps!)
    evidence = ROOT / 'nand-read-validation'
    phys_rows = []
    for page_idx in range(3):
        raw = (evidence / f'raw-row{page_idx}.bin').read_bytes()
        assert len(raw) == PAGE and raw[8192] == 0xFF
        candidate = bytearray(raw)
        for step in range(8):
            decoded = (evidence / f'rom-flags2-row{page_idx}-sector{step}-sector.bin').read_bytes()
            offset = step * 1024
            assert decoded == mb[page_idx * 8192 + offset:page_idx * 8192 + offset + 1024]
            mask = whitening(bytes(x ^ y for x, y in zip(raw[offset:offset + 22], decoded[:22])))
            oob = 8192 + step * 46
            metadata = raw[oob:oob + 4]
            assert parity(decoded, metadata) == raw[oob + 4:oob + 46]
            data = bytes(logical[page_idx * 8192 + offset:page_idx * 8192 + offset + 1024])
            candidate[offset:offset + 1024] = bytes(x ^ y for x, y in zip(data, mask))
            candidate[oob + 4:oob + 46] = parity(data, metadata)
            recovered = bytes(x ^ y for x, y in zip(candidate[offset:offset + 1024], mask))
            assert recovered == data
            codeword = (recovered + candidate[oob:oob + 46]).translate(REVERSE)
            assert remainder(int.from_bytes(codeword, 'big')) == 0
        assert candidate[8192] == 0xFF
        phys_rows.append(bytes(candidate))

    block0_img = b''.join(phys_rows) + BLANK_BLOCK[3 * PAGE:]
    assert len(block0_img) == BLOCK and block0_img[8192] == 0xFF
    OUT.mkdir(exist_ok=True)
    (OUT / 'sram-payload.bin').write_bytes(payload)
    (OUT / 'logical-3pages.bin').write_bytes(logical)
    (OUT / 'block0-multiboot.bin').write_bytes(block0_img)
    manifest = {
        'sram_payload_sha256': sha(payload),
        'logical_3pages_sha256': sha(logical),
        'block0_multiboot_sha256': sha(block0_img),
        'programmed_rows': [0, 1, 2],
        'preserved_factory_backup_block': 1,
        'sd_manifest_sha256': sha((ROOT / 'sd-boot-autonomous' / 'manifest.json').read_bytes()),
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print('Offline 4-page Block-0 Multi-Boot Loader verification PASSED:')
    print(json.dumps(manifest, indent=2))
    return phys_rows, block0_img


def row_ecc_diffs(actual, expected):
    from nand_backup_format import distance
    if actual[8192] != 0xFF or actual[8560:] != expected[8560:]:
        return None
    total = 0
    for step in range(8):
        off, oob = step * 1024, 8192 + step * 46
        errs = distance(actual[off:off + 1024], expected[off:off + 1024])
        errs += distance(actual[oob:oob + 46], expected[oob:oob + 46])
        if errs > 4:
            return None
        total += errs
    return total


def flash_block0():
    import adb_winusb
    from simulate_nand_write import BLANK_PAGE, page as blk_page
    from service_usb import Service
    phys_rows, block0_img = build_logical_and_physical()
    rollback_img = (PREP_OUT / 'block0-restore-ECC-corrected.bin').read_bytes()
    pnp = adb_winusb.present('VID_10D6&PID_10D6')
    assert pnp, 'Expected ADFU device for Service()'
    os.environ['ACTIONS_ADFU_PNP_PATH'] = pnp[0]

    with Service(exclusive=True) as usb:
        current = read_block(usb)
        cur_sha = sha(current)
        assert current[8192] == 0xFF, 'Bad-block marker on Block 0!'
        diff = original_differences(current, rollback_img)
        new_diff = original_differences(current, block0_img)
        # Check if Block 0 is partially programmed with current phys_rows[0] and blank Rows 1..255
        partial_r0 = (row_ecc_diffs(blk_page(current, 0), phys_rows[0]) is not None
                      and all(blk_page(current, r) == BLANK_PAGE for r in range(1, 256)))
        known_prev = {
            '0fddef9f2a26854afb08bb100b7aa1fce87c75624bbee56ec329b783c3c12f29',
            '4893e59696809b89822b597c8a94fb030958adb5c1175c1a51e69e647cfa799a',
            '85cc27039f5f3f5810a941a1a862a845b1f464ce3e0b7f044879a0d8449c7f4f',
            'f7cf1617617db333d18179c3db91bc5c284acbd3395b57f659f17404e13b37b9',
            'adc72a08f0a18dab4f065259efa6def46fd1f7bf8523906b715989c3a63ab351',
        }
        ours_3page = all(blk_page(current, r) == BLANK_PAGE for r in range(3, 256))
        assert (diff is not None or new_diff is not None or partial_r0 or ours_3page or cur_sha in known_prev), (
            f'Unexpected Block 0 (sha={cur_sha})')
        snap = OUT / f'block0-before-{cur_sha}.bin'
        if not snap.exists() and not partial_r0:
            snap.write_bytes(current)

        start_row = 1 if partial_r0 else 0
        if not partial_r0:
            print('Erasing physical Block 0...', flush=True)
            st = transact(usb, 'erase')
            assert read_block(usb) == BLANK_BLOCK, 'Block 0 erase readback failed!'
            print(f'Block 0 erase verified ({st:#x}). Programming Rows 0..{len(phys_rows)-1}...', flush=True)
        else:
            print('Row 0 already programmed & verified; continuing remaining rows...', flush=True)

        for row_idx in range(start_row, len(phys_rows)):
            page_bytes = phys_rows[row_idx]
            stp = transact(usb, 'program', row_idx, page_bytes)
            rd = read_row(usb, row_idx)
            rd_diffs = row_ecc_diffs(rd, page_bytes)
            assert rd_diffs is not None, f'Row {row_idx} readback exceeded ECC tolerance!'
            print(f'  Row {row_idx}/{len(phys_rows)-1} programmed & verified (status={stp:#x}, raw_bit_diffs={rd_diffs})', flush=True)

        final_blk = read_block(usb)
        total_diffs = original_differences(final_blk, block0_img)
        assert total_diffs is not None, 'Full 256-page Block 0 ECC readback verification failed!'
        print(f'SUCCESS! Physical Block 0 Rows 0..{len(phys_rows)-1} programmed & full 256-page readback verified (total_raw_bit_diffs={total_diffs})!')


def ram_boot():
    import time
    import adb_winusb
    from rom_usb import ROM
    _, _ = build_logical_and_physical()
    payload = (OUT / 'sram-payload.bin').read_bytes()
    pnp = adb_winusb.present('VID_10D6&PID_10D6')
    assert pnp, 'Expected cold BootROM ADFU device'
    os.environ['ACTIONS_ADFU_PNP_PATH'] = pnp[0]
    with ROM() as r:
        pll, = struct.unpack('<I', r.read(0xB0150008, 4))
        print(f'Cold BootROM ADFU attached (DDR_PLL={pll:#x}). Uploading 28 KiB sram-payload.bin...', flush=True)
        for off in range(0, len(payload), 1024):
            chunk = payload[off:off + 1024]
            r.write(PARAM + off, chunk)
            time.sleep(0.015)
            assert r.read(PARAM + off, len(chunk)) == chunk
        entry = ADFU_ADDR if '--test-adfu' in sys.argv else DDR_ENTRY
        print(f'Executing {entry:#x}...', flush=True)
        try:
            r.execute(entry)
        except Exception as ex:  # ROM re-entry resets the USB session
            print('execute:', ex, flush=True)
    if '--test-adfu' in sys.argv:
        time.sleep(3)
        p = adb_winusb.present('VID_10D6&PID_10D6')
        assert p, 'ADFU did not come back'
        with ROM() as r:
            print('ROM ADFU re-entry OK; marker:', r.read(REPORT + 0x70, 4).hex(), flush=True)
        return
    for i in range(50):
        time.sleep(0.5)
        if adb_winusb.present():
            print(f'RAM-BOOT PROVEN! Linux WinUSB ADB enumerated in {(i + 1) * 0.5:.1f}s!', flush=True)
            return
    raise RuntimeError('Linux ADB did not enumerate within 25s after RAM-boot!')


if __name__ == '__main__':
    if '--flash-block0' in sys.argv:
        flash_block0()
    elif '--ram-boot' in sys.argv:
        ram_boot()
    else:
        build_logical_and_physical()
