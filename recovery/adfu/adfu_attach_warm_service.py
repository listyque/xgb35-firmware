"""Install the tested SRAM service from BootROM while preserving live DDR timing."""
import json
from hashlib import sha256
import struct
import time

from build_autonomous_service import Arm, ROOT, branch, mov, unpack
from rom_usb import ROM
from service_usb import Service


SOURCE, TARGET, CLEAN_ENTRY, ENTRY = 0xB406B000, 0xB4068C00, 0xB406F380, 0xB406F400


def main():
    folder = ROOT / 'autonomous-service-diagnostic'
    manifest = json.loads((folder/'manifest.json').read_text())
    assert manifest['diagnostic_callback']
    segments = {item['file']: item for item in manifest['segments']}
    packed = (folder/'usb-packed.bin').read_bytes()
    params = (folder/'parameters.bin').read_bytes()
    assert sha256(packed).hexdigest() == segments['usb-packed.bin']['sha256']
    assert sha256(params).hexdigest() == segments['parameters.bin']['sha256']
    service = unpack(packed, manifest['service_bytes'])
    assert service[:12] == bytes.fromhex('010000ea00f020e3008806b4')
    callback = struct.pack('<I', branch(0xB4069EFC, 0xB406A440))
    assert service[0x12FC:0x1300] == callback
    assert service[0xE30:0xE34] == bytes.fromhex('0000a0e3')
    assert len(service) % 4 == 0 and SOURCE + len(service) <= ENTRY
    assert TARGET + len(service) < SOURCE + len(service)
    arm = Arm(ENTRY)
    emit = arm.emit
    emit(0xF10C00C0, *mov(0, SOURCE), *mov(1, TARGET), *mov(2, len(service)//4))
    arm.label('copy')
    emit(0xE4903004, 0xE4813004, 0xE2522001)
    arm.jump('copy', cond=1)
    emit(*mov(0, TARGET), *mov(1, TARGET+len(service)))
    arm.label('clean')
    emit(0xEE070F3A, 0xE2800020, 0xE1500001)
    arm.jump('clean', cond=3)
    emit(0xF57FF04F, 0xEE070F15, 0xF57FF04F, 0xF57FF06F, *mov(13, 0xB4070000),
         *mov(12, TARGET), 0xE12FFF1C)
    clean = Arm(CLEAN_ENTRY)
    clean.emit(*mov(0, SOURCE), *mov(1, ENTRY + 0x80))
    clean.label('evict')
    # CPU-written SRAM survives in cache across the BootROM USB return.
    clean.emit(0xEE070F3E, 0xE2800020, 0xE1500001)
    clean.jump('evict', cond=3)
    clean.emit(0xF57FF04F, 0xE3A00000, 0xEE070F15, 0xF57FF04F, 0xF57FF06F, 0xE3A00000, 0xE12FFF1E)
    with ROM() as usb:
        assert usb.read(0xB0150008, 4) == struct.pack('<I', 0x13C)
        clean_code = clean.finish()
        usb.write(CLEAN_ENTRY, clean_code)
        assert usb.read(CLEAN_ENTRY, len(clean_code)) == clean_code
        usb.execute(CLEAN_ENTRY)
        for address, data in ((0xB4068800, params), (SOURCE, service), (ENTRY, arm.finish())):
            for offset in range(0, len(data), 1024):
                chunk = data[offset:offset+1024]
                usb.write(address+offset, chunk)
                actual = usb.read(address+offset, len(chunk))
                assert actual == chunk, (hex(address+offset), next(i for i, (a, b) in
                                            enumerate(zip(actual, chunk)) if a != b))
        usb.execute(ENTRY)
    time.sleep(2)
    with Service(exclusive=True) as usb:
        assert usb.read(0xB4069EFC, 4) == callback
        assert usb.read(0xB0150008, 4) == struct.pack('<I', 0x13C)
        address = 0x01001000
        original = usb.read(address, 32)
        test = bytes(range(32))
        try:
            usb.write(address, test)
            assert usb.read(address, len(test)) == test
        finally:
            usb.write(address, original)
        assert usb.read(address, len(original)) == original
    print('Warm diagnostic service attached. DDR unchanged and readback verified.', flush=True)


if __name__ == '__main__':
    main()
