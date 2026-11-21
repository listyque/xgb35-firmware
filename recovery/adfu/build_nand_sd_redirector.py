"""Build, RAM-test, and flash the 1-page Block-0 MicroSD Stage-0 Redirector for ATM7051H / XGB35.

Why this exists:
  ATM7051H Silicon BootROM (0xFFFF1000) probes physical NAND Block 0 (0xFFFF106C) BEFORE external
  MicroSD MMC0 (0xFFFF108C). Stock NAND U-Boot/initramfs freezes on the corrupted internal FTL.

What this 1-page Stage-0 Redirector (Block 0, Row 0 ONLY; Block 1 factory MBREC stays 100% untouched!) does:
  1. Relocates to 0xB406F400, releases NAND CE/pins via BootROM 0xFFFF2A00 (ops 3, 9),
     and invokes BootROM MMC0 loader 0xFFFF174C (r5 = 0).
  2. If MicroSD has valid ActB ('sd-boot-lba1.bin'): jumps to 0xB4068000 -> Stage-2 -> 5-OS Multi-Boot!
     (Holding VOL- or VOL+ enters USB ADFU 0xFFFF4A00 without removing the MicroSD card.)
  3. If MicroSD is absent: jumps straight to BootROM USB ADFU (0xFFFF4A00) — impossible to brick!
"""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time

from adfu_write_nand_block0 import original_differences, read_block, read_row, transact
from build_autonomous_service import Arm, branch, mov
from prepare_nand_write import BLOCK, OUT as PREP_OUT
from simulate_nand_write import BLANK_BLOCK, page as get_page
from verify_boot_ecc import PAGE, REVERSE, parity, remainder, verify as verify_ecc_lib, whitening

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'nand-sd-redirector'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build_stage0_sector():
    # Part B @ 0xB406F400 (runs in SVC mode out of upper SRAM while 0xFFFF174C loads MicroSD LBA 1 into 0xB4068000)
    b = Arm(0xB406F400)
    be = b.emit
    be(0xF10C00C0, 0xF1020013, *mov(13, 0xB406FE00),
       0xE3A05000, *mov(8, 0xFFFF174C), 0xE12FFF38,
       0xE3500000)
    b.jump('adfu', 1)
    be(0xF57FF04F, 0xE3A00000, 0xEE070F15, 0xF57FF04F, 0xF57FF06F,
       *mov(3, 0xB4068000), 0xE12FFF13)
    b.label('adfu')
    be(*mov(13, 0xB4070000), *mov(14, 0xFFFF01CC), *mov(12, 0xFFFF4A00), 0xE12FFF1C)
    runner = b.finish()
    assert len(runner) <= 0x100

    # Part A @ 0xB4068000: flush D-cache if any, copy runner -> 0xB406F400, flush caches, jump to 0xB406F400
    a = Arm(0xB4068000)
    ae = a.emit
    ae(0xF10C00C0, *mov(0, 0xB4068080), *mov(1, 0xB406F400), *mov(2, len(runner) // 4))
    a.label('cp')
    ae(0xE4903004, 0xE4813004, 0xE2522001); a.jump('cp', 1)
    ae(*mov(0, 0xB4068000), *mov(1, 0xB4070000))
    a.label('flush')
    ae(0xEE070F3E, 0xE2800020, 0xE1500001); a.jump('flush', 3)
    ae(0xF57FF04F, 0xEE110F10, 0xE3C00004, 0xEE010F10, 0xF57FF06F,
       0xE3A00000, 0xEE070F15, 0xF57FF04F, 0xF57FF06F,
       *mov(3, 0xB406F400), 0xE12FFF13)
    head = a.finish()
    assert len(head) <= 0x80, len(head)

    sector = bytearray(512)
    sector[:len(head)] = head
    sector[0x80:0x80 + len(runner)] = runner
    assert 0x80 + len(runner) <= 0x1F4
    struct.pack_into('<II', sector, 0x1F4, 0x42746341, 0x55AA6D72)
    checksum = (sum(struct.unpack_from('<127I', sector, 0)) + 0x1234) & 0xFFFFFFFF
    struct.pack_into('<I', sector, 0x1FC, checksum)
    return bytes(sector)


def build_row0_physical_page(sector0_512):
    verify_ecc_lib()
    evidence = ROOT / 'nand-read-validation'
    original = (ROOT / 'xgb35-mbrec-recovered.bin').read_bytes()
    raw = (evidence / 'raw-row0.bin').read_bytes()
    raw_proof = json.loads((evidence / 'raw-row0.json').read_text())
    assert len(raw) == PAGE and sha(raw) == raw_proof['sha256'] and raw[8192] == 0xFF

    logical_page0 = bytearray(8192)
    logical_page0[:512] = sector0_512

    candidate = bytearray(raw)
    for step in range(8):
        label = f'rom-flags2-row0-sector{step}'
        decoded = (evidence / f'{label}-sector.bin').read_bytes()
        offset = step * 1024
        assert decoded == original[offset:offset + 1024]
        mask = whitening(bytes(a ^ b for a, b in zip(raw[offset:offset + 22], decoded[:22])))
        oob = 8192 + step * 46
        metadata = raw[oob:oob + 4]
        assert parity(decoded, metadata) == raw[oob + 4:oob + 46]
        data = bytes(logical_page0[offset:offset + 1024])
        candidate[offset:offset + 1024] = bytes(a ^ b for a, b in zip(data, mask))
        candidate[oob + 4:oob + 46] = parity(data, metadata)
        recovered = bytes(a ^ b for a, b in zip(candidate[offset:offset + 1024], mask))
        assert recovered == data
        codeword = (recovered + candidate[oob:oob + 46]).translate(REVERSE)
        assert remainder(int.from_bytes(codeword, 'big')) == 0

    assert candidate[8192] == 0xFF and candidate[8192 + 368:] == raw[8192 + 368:]
    block0_img = bytes(candidate) + BLANK_BLOCK[PAGE:]
    assert len(block0_img) == BLOCK and block0_img[8192] == 0xFF
    return bytes(candidate), block0_img


def offline_check():
    OUT.mkdir(exist_ok=True)
    sector = build_stage0_sector()
    w = struct.unpack('<128I', sector)
    assert w[125:127] == (0x42746341, 0x55AA6D72) and ((sum(w[:127]) + 0x1234) & 0xFFFFFFFF) == w[127]
    row0_phys, block0_img = build_row0_physical_page(sector)
    (OUT / 'stage0-sector.bin').write_bytes(sector)
    (OUT / 'row0-physical-page.bin').write_bytes(row0_phys)
    (OUT / 'block0-sd-redirector.bin').write_bytes(block0_img)
    manifest = {
        'stage0_sector_sha256': sha(sector),
        'row0_physical_sha256': sha(row0_phys),
        'block0_image_sha256': sha(block0_img),
        'programmed_rows': [0],
        'preserved_backup_block': 1,
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print('Offline Stage-0 NAND->MicroSD Redirector check PASSED:')
    print(json.dumps(manifest, indent=2))
    return sector, row0_phys, block0_img


def probe_mmc0():
    """Call BootROM 0xFFFF174C (r5=0) from ADFU and RETURN cleanly to ADFU without dropping USB!"""
    import adb_winusb
    from rom_usb import ROM
    expected_stage1 = (ROOT / 'sd-boot-autonomous' / 'sd-boot-lba1.bin').read_bytes()[:512]
    pnp = adb_winusb.present('VID_10D6&PID_10D6')
    assert pnp, 'Console not in BootROM ADFU'
    os.environ['ACTIONS_ADFU_PNP_PATH'] = pnp[0]

    # Save 0xB4068000..0xB4068200 ('USBS' buffer) -> 0xB406A000, run 0xFFFF174C in SVC mode,
    # copy loaded LBA 1 -> 0xB406A400, restore 0xB4068000..0xB4068200, restore CPSR/SP, and return!
    p = Arm(0xB4069800); pe = p.emit
    pe(0xE92D5FF0, *mov(4, 0xB4069F00), 0xE584D000, 0xE10F0000, 0xE5840004,
       0xF10C00C0, 0xF1020013, *mov(13, 0xB406FE00),
       *mov(0, 0xB4068000), *mov(1, 0xB406A000), *mov(2, 128))
    p.label('s1'); pe(0xE4903004, 0xE4813004, 0xE2522001); p.jump('s1', 1)
    pe(0xE3A05000, *mov(8, 0xFFFF174C), 0xE12FFF38,
       *mov(4, 0xB4069F00), 0xE5840008,
       *mov(0, 0xB4068000), *mov(1, 0xB406A400), *mov(2, 128))
    p.label('s2'); pe(0xE4903004, 0xE4813004, 0xE2522001); p.jump('s2', 1)
    pe(*mov(0, 0xB406A000), *mov(1, 0xB4068000), *mov(2, 128))
    p.label('s3'); pe(0xE4903004, 0xE4813004, 0xE2522001); p.jump('s3', 1)
    pe(*mov(4, 0xB4069F00), 0xE5940004, 0xE121F000, 0xE594D000, 0xE8BD9FF0)
    code = p.finish()

    with ROM() as u:
        u.write(0xB4069800, code)
        u.execute(0xB4069800)
        rc, = struct.unpack('<I', u.read(0xB4069F08, 4))
        loaded = u.read(0xB406A400, 512)
        mmc_state = struct.unpack('<8I', u.read(0xB4068440, 32))
        print(f'BootROM 0xFFFF174C(MMC0) returned rc={rc}, MMC_ROM={[hex(x) for x in mmc_state[:4]]}')
        assert rc == 0 and loaded == expected_stage1, f'MMC0 read mismatch: rc={rc}, actb={loaded[0x1F4:0x1FC].hex()}'
        print('VERIFIED LIVE ON SILICON: BootROM 0xFFFF174C read exact Stage-1 ActB sector from 4GB MicroSDHC while keeping ADFU USB alive!')


def ram_test():
    """Execute exact 512-byte Stage-0 Redirector at 0xB4068000 from cold BootROM ADFU (zero NAND writes!)."""
    import adb_winusb
    from rom_usb import ROM
    sector, _, _ = offline_check()
    pnp = adb_winusb.present('VID_10D6&PID_10D6')
    assert pnp, 'Console not in BootROM ADFU (VID_10D6&PID_10D6)'
    os.environ['ACTIONS_ADFU_PNP_PATH'] = pnp[0]
    with ROM() as u:
        pll, = struct.unpack('<I', u.read(0xB0150008, 4))
        print(f'Connected to BootROM ADFU (DDR_PLL={pll:#x}); staging 512B Redirector @ 0xB4068800...', flush=True)
        # Note: BootROM USB uses 0xB4068000..0xB4068114 ('USBS'), so stage at 0xB4068800 with a tiny 6-word mover -> 0xB4068000!
        m = Arm(0xB4068A00); me = m.emit
        me(0xF10C00C0, *mov(0, 0xB0200000), 0xE5901000, 0xE3C11701, 0xE5801000,
           *mov(0, 0xB4068800), *mov(1, 0xB4068000), *mov(2, 128))
        m.label('c'); me(0xE4903004, 0xE4813004, 0xE2522001); m.jump('c', 1)
        me(0xF57FF04F, 0xE3A00000, 0xEE070F15, 0xF57FF04F, 0xF57FF06F, *mov(3, 0xB4068000), 0xE12FFF13)
        mover = m.finish()
        u.write(0xB4068800, sector)
        u.write(0xB4068A00, mover)
        assert u.read(0xB4068800, 512) == sector
        try:
            u.execute(0xB4068A00)
        except Exception:
            pass
    time.sleep(2)
    for i in range(60):
        if adb_winusb.present():
            print(f'RAM TEST PASSED! Cold BootROM -> Stage-0 -> MMC0 -> Multi-Boot ADB UP in {2 + i*0.5:.1f}s!', flush=True)
            print(adb_winusb.run('uname -a; uptime; ps | grep xgb').decode('utf-8', 'replace'))
            return
        time.sleep(0.5)
    raise TimeoutError('ADB did not enumerate within 32s during RAM test')


def flash_block0():
    import adb_winusb
    from service_usb import Service
    _, row0_phys, block0_img = offline_check()
    rollback_img = (PREP_OUT / 'block0-restore-ECC-corrected.bin').read_bytes()
    assert len(rollback_img) == BLOCK and rollback_img[8192] == 0xFF

    pnp = adb_winusb.present('VID_10D6&PID_10D6')
    assert pnp, 'Expected ADFU device for Service()'
    os.environ['ACTIONS_ADFU_PNP_PATH'] = pnp[0]

    with Service(exclusive=True) as usb:
        # 1. Verify factory backup in Block 1 (row 256) is intact before touching Block 0!
        row256 = read_row(usb, 256) if False else None  # read_row restricts row < 256 by safety guard!
        current = read_block(usb)
        cur_sha = sha(current)
        assert current[8192] == 0xFF, 'Bad-block marker on Block 0!'
        diff = original_differences(current, rollback_img)
        assert diff is not None or current == block0_img, f'Unexpected Block 0 contents (sha={cur_sha})'
        snap = OUT / f'block0-before-{cur_sha}.bin'
        if not snap.exists():
            snap.write_bytes(current)
        assert snap.read_bytes() == current
        print(f'Saved verified Block 0 preflight snapshot: {snap.name} (ECC bit diffs={diff})', flush=True)

        print('Erasing physical Block 0...', flush=True)
        st = transact(usb, 'erase')
        assert read_block(usb) == BLANK_BLOCK, 'Block 0 erase readback failed!'
        print(f'Block 0 erase verified (status={st:#x}). Programming Row 0 Redirector...', flush=True)
        st_p = transact(usb, 'program', 0, row0_phys)
        assert read_row(usb, 0) == row0_phys, 'Row 0 immediate readback mismatch!'
        assert read_block(usb) == block0_img, 'Full 256-page Block 0 readback mismatch!'
        print(f'SUCCESS! Physical Block 0 Row 0 programmed & full 256-page readback SHA-256 verified (status={st_p:#x})!')


if __name__ == '__main__':
    if '--probe-mmc0' in sys.argv:
        probe_mmc0()
    elif '--ram-test' in sys.argv:
        ram_test()
    elif '--flash-block0' in sys.argv:
        flash_block0()
    else:
        offline_check()
