"""Check the ARM boot address through the CPU and return the result from SRAM."""
import json
import struct
import time

from adfu_usb_storage_ram import ENTRY, KERNEL, OUTPUT, REPORT, STAGING, checked_write
from build_autonomous_service import Arm, branch, mov
from service_usb import Service


def payload(address, *, write_test=False, boot_clocks=False):
    assert address in (KERNEL, STAGING)
    arm = Arm(ENTRY)
    emit = arm.emit
    emit(0xE92D41F0, *mov(4, REPORT), *mov(5, 0xB0150000),
         0xEE110F10, 0xE5840000, 0xE595701C, 0xE5847004,
         *mov(6, 0xB0200000), 0xE5960000, 0xE5840008)
    if boot_clocks:
        emit(0xE3A00002, 0xE585001C, 0xF57FF04F)
    emit(*mov(6, address), 0xE5968000, 0xE584800C)
    if write_test:
        emit(*mov(1, 0x61B7039F), 0xE5861000, 0xF57FF04F,
             0xE5960000, 0xE5840010, 0xE5868000, 0xF57FF04F,
             0xE5960000, 0xE5840014)
    if boot_clocks:
        emit(0xE585701C, 0xF57FF04F)
    emit(0xE3A00001, 0xE5840018, 0xF57FF04F, 0xE3A00000, 0xE8BD81F0)
    return arm.finish()


def main():
    observations = []
    with Service(exclusive=True) as usb:
        assert usb.read(0xB4069EFC, 4) == struct.pack('<I', branch(0xB4069EFC, 0xB406A440))
        tests = ((STAGING, False, False), (KERNEL, False, False), (KERNEL, True, False))
        for address, write_test, boot_clocks in tests:
            print(f'CPU probe {address:#x}, write={write_test}, boot_clocks={boot_clocks}', flush=True)
            code = payload(address, write_test=write_test, boot_clocks=boot_clocks)
            checked_write(usb, REPORT, bytes(32))
            checked_write(usb, ENTRY, code)
            usb.execute(ENTRY)
            time.sleep(0.05)
            sctlr, bus, remap, initial, seen, restored, done = struct.unpack('<7I', usb.read(REPORT, 28))
            assert done == 1
            if write_test:
                assert seen == 0x61B7039F and restored == initial
            observations.append(dict(address=hex(address), write=write_test, boot_clocks=boot_clocks,
                                     sctlr=hex(sctlr), bus=hex(bus), remap=hex(remap),
                                     initial=hex(initial), seen=hex(seen), restored=hex(restored)))
            (OUTPUT / 'memory-probe.json').write_text(json.dumps(observations, indent=2))
            print(observations[-1], flush=True)


if __name__ == '__main__':
    main()
