"""Verify the 128 MiB RAM mapping and check for aliases without modifying NAND."""
import json
import secrets
import struct
import time

from build_autonomous_service import ROOT, Arm, mov
from rom_usb import ROM


ENTRY, REPORT = 0xB406D000, 0xB406D800
LIMIT = 0x08000000
ADDRESSES = (0x00008000, 0x01008000, 0x02008000, 0x04008000, 0x06008000,
             0x07FF8000, 0x07FFF000)
PATTERNS = tuple(0x5239AC1D ^ value for value in ADDRESSES)
PROOF = ROOT / 'usb-storage-ram' / 'ram-128-proof.json'


def build(nonce):
    arm = Arm(ENTRY)
    emit = arm.emit
    emit(0xE92D4010, *mov(4, REPORT))
    for index, address in enumerate(ADDRESSES):
        emit(*mov(0, address), 0xE5901000, 0xE5841000 | 4*index)
    # Store all patterns before reading back so mirroring is detectable.
    for address, pattern in zip(ADDRESSES, PATTERNS):
        emit(*mov(0, address), *mov(1, pattern), 0xE5801000, 0xEE070F3E)
    emit(0xF57FF04F)
    for index, address in enumerate(ADDRESSES):
        emit(*mov(0, address), 0xE5901000, 0xE5841040 | 4*index)
    for index, address in enumerate(ADDRESSES):
        emit(*mov(0, address), 0xE5941000 | 4*index, 0xE5801000, 0xEE070F3E)
    emit(0xF57FF04F)
    for index, address in enumerate(ADDRESSES):
        emit(*mov(0, address), 0xE5901000, 0xE5841080 | 4*index)
    emit(*mov(0, nonce), 0xE58400C0, 0xF57FF04F, 0xE3A00000, 0xE8BD8010)
    return arm.finish()


def main():
    nonce = secrets.randbits(32) or 1
    with ROM() as usb:
        pll, = struct.unpack('<I', usb.read(0xB0150008, 4))
        assert pll == 0x13C, hex(pll)
        code = build(nonce)
        assert len(code) < REPORT - ENTRY
        for address, data in ((REPORT, bytes(196)), (ENTRY, code)):
            usb.write(address, data)
            assert usb.read(address, len(data)) == data
        usb.execute(ENTRY)
        time.sleep(.3)
        report = usb.read(REPORT, 196)
    count = len(ADDRESSES)
    original, observed, restored = (struct.unpack_from(f'<{count}I', report, offset)
                                     for offset in (0, 0x40, 0x80))
    assert struct.unpack_from('<I', report, 0xC0)[0] == nonce
    assert original == restored and observed == PATTERNS, (original, observed, restored)
    PROOF.write_text(json.dumps(dict(memory_limit=LIMIT, ddr_pll=pll, addresses=ADDRESSES,
                                     patterns=PATTERNS, originals_restored=True), indent=2))
    print(f'Confirmed {count} distinct RAM locations through {max(ADDRESSES):#010x}')


if __name__ == '__main__':
    main()
