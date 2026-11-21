"""Return the first late Linux fault or a PID 1 checkpoint over ADFU."""
import json
import struct
import sys
import time

import adfu_usb_storage_ram as storage
from build_autonomous_service import Arm, branch, mov
from rom_usb import ROM
from service_usb import Service


VIRTUAL = 0xC0000000
STUBS = storage.IDMAP + 0x100
EVENTS = (
    (0xD9E0, 'data_abort', 0xE24DD044),
    (0xDB40, 'prefetch_abort', 0xE24DD044),
    (0xDAE0, 'undefined_instruction', 0xE24DD044),
    (0x4DC370, 'panic', 0xE92D000F),
    (0x4D6B34, 'kernel_init', 0xE92D4038),
)
INIT_EXEC = 0x4D6B88
PATCH_CODE = 0xB406E800


def bootrom_tail(arm):
    """Restore the proven ROM vectors and banked stacks before entering its USB loop."""
    rom = (storage.ROOT/'atm7051h-high-rom-ffff0000-ffff7fff.bin').read_bytes()
    assert storage.sha256(rom) == 'e83e5ac0d25025a7eb948a796636bc7ff11a7a94f6904d1508434034a094757c'
    emit = arm.emit
    emit(*mov(0, 0xB0200000), 0xE5901000, 0xE3C11701, 0xE5801000,
         0xF57FF04F, 0xE3A00000, storage.cp15(False, 0, (12, 0, 0)),
         0xEE070F15, 0xF57FF04F, 0xF57FF06F, *mov(5, 0xB0158058))
    for i, offset in enumerate((0x420, 0x440, 0x460, 0x480, 0x4A0, 0x300, 0x400)):
        target, = struct.unpack_from('<I', rom, 0xAC0+4*i)
        assert target == 0xFFFF0000+offset
        emit(*mov(0, target), 0xE5850000 | 4*i)
    for mode, literal in ((0xD2, 0xAA8), (0xDF, 0xAAC), (0xD1, 0xAB0),
                          (0xD7, 0xAB4), (0xDB, 0xAB8), (0xD3, 0xABC)):
        stack, = struct.unpack_from('<I', rom, literal)
        assert 0xB406F800 <= stack < 0xB4070000
        emit(0xE321F000 | mode, *mov(13, stack))
    emit(0xF57FF04F, *mov(14, 0xFFFF01CC), *mov(12, 0xFFFF4A00), 0xE12FFF1C)


def instrument(kernel, events=EVENTS, fatal_only=False, headless=False):
    assert storage.sha256(kernel) == '81b0afc324d6396aad30fa3a2393dd1298679fa6a579bfbf94c07bf3c932d114'
    image = bytearray(kernel)
    if headless:
        # The stock panel drivers can reach owl_panel_register with no display engine.
        for address, second in ((0x6B787C, 0xE3001330), (0x6B78BC, 0xE30013C4)):
            offset = address-storage.KERNEL
            assert struct.unpack_from('<2I', image, offset) == (0xE30E0E34, second)
            struct.pack_into('<2I', image, offset, 0xE3A00000, 0xE12FFF1E)
    else:
        storage.patch_kernel_hw(image)
    # r0 points to the banked exception frame for the three SVC vector handlers.
    # The original instruction pointer is its second word.
    if fatal_only:
        frame = (0xE1A0800C, 0xE3A06000, 0xE1A05001, 0xE1A0C00E, 0xE3580001,
                 0x0591603C, 0x05915004, 0x0591C038)
    else:
        frame = (0xE1A0800C, 0xE3A06000, 0xE35C0003, 0x95906004, 0xE1A05001, 0xE1A0C00E)
    captures = (
        *frame,
        storage.cp15(True, 7, (6, 0, 0)), storage.cp15(True, 10, (5, 0, 0)),
        storage.cp15(True, 11, (5, 0, 1)),
    )
    idmap = storage.identity_stub(captures)
    assert len(idmap) < STUBS-storage.IDMAP
    index = storage.IDMAP-storage.KERNEL
    image[index:index+len(idmap)] = idmap
    for tag, (address, _, expected) in enumerate(events, 1):
        at = address-storage.KERNEL
        assert struct.unpack_from('<I', image, at)[0] == expected
        stub = STUBS+8*(tag-1)
        struct.pack_into('<I', image, at, branch(VIRTUAL+address, VIRTUAL+stub))
        struct.pack_into('<II', image, stub-storage.KERNEL,
                         0xE3A0C000 | tag, branch(VIRTUAL+stub+4, VIRTUAL+storage.IDMAP))
    return image


def patch_rom_words(usb, words):
    arm = Arm(PATCH_CODE)
    assert len(words) <= 32
    for address, data in words:
        assert storage.STAGING <= address <= storage.LIMIT-4 and address % 4 == 0
        arm.emit(*mov(0, address), *mov(1, int.from_bytes(data, 'little')),
                 0xE5801000, 0xEE070F3E)
    # The next command uploads a different instruction sequence to this entry point.
    arm.emit(0xF57FF04F, 0xEE070F15, 0xF57FF04F, 0xF57FF06F, 0xE3A00000, 0xE12FFF1E)
    storage.checked_write(usb, PATCH_CODE, arm.finish())
    usb.execute(PATCH_CODE)
    time.sleep(0.02)


def stage_from_rom(usb, files):
    for name, (address, data) in files.items():
        previous = b''.join(usb.read(address+offset, min(0x4000, len(data)-offset))
                            for offset in range(0, len(data), 0x4000))
        words = [(address+offset, bytes(data[offset:offset+4]))
                 for offset in range(0, len(data), 4)
                 if data[offset:offset+4] != previous[offset:offset+4]]
        for offset in range(0, len(words), 32):
            patch_rom_words(usb, words[offset:offset+32])
        assert b''.join(usb.read(address+offset, min(0x4000, len(data)-offset))
                        for offset in range(0, len(data), 0x4000)) == data, name
        print(f'Verified {name} in DDR with {len(words)} rewritten words', flush=True)


def main():
    headless = '--headless' in sys.argv
    return_to_rom = '--rom-return' in sys.argv
    rom_staged = '--rom-staged' in sys.argv
    fatal_only = '--fatal-only' in sys.argv
    usb_test = '--usb-test' in sys.argv
    assert not usb_test or (fatal_only and (headless or '--adb' in sys.argv) and return_to_rom
                            and '--init-exec' not in sys.argv)
    assert not rom_staged or return_to_rom
    if rom_staged:
        # The previous session's CPU-written report can still occupy the original SRAM cache lines.
        storage.REPORT = 0xB406DC00
    files = storage.build(headless=headless, memory128='--memory-128' in sys.argv,
                          adb='--adb' in sys.argv)
    address, image = files['Image']
    events = ((0x116A4, 'die', 0xE92D4FF0), *EVENTS[-2:]) if fatal_only else EVENTS
    run_until_exec = '--init-exec' in sys.argv
    if run_until_exec:
        expected = branch(VIRTUAL+INIT_EXEC, 0xC00C99AC, link=True)
        events = (*events[:-1], (INIT_EXEC, 'before_init_exec', expected))
    if usb_test:
        events = events[:-1]
    files['Image'] = (address, instrument(image, events, fatal_only, headless))
    if '--execute' not in sys.argv:
        print('Verified stock image and exception hook inputs', flush=True)
        return
    with (ROM() if rom_staged else Service(exclusive=True)) as usb:
        if not rom_staged:
            assert usb.read(0xB4069EFC, 4) == struct.pack('<I', branch(0xB4069EFC, 0xB406A440))
        assert usb.read(0xB0150008, 4) == struct.pack('<I', 0x13C)
        if run_until_exec or usb_test:
            from adfu_backlight_probe import run
            # Timer1's interrupt flag can be left pending by an earlier debugger stop.
            # The stock Linux timer handler acknowledges it with this write.
            run(usb, [(0xB0158014, 1)], (0xB0158014,))
        if rom_staged:
            stage_from_rom(usb, files)
        else:
            for name, (address, data) in files.items():
                for offset in range(0, len(data), 0x4000):
                    storage.checked_write(usb, address+offset, bytes(data[offset:offset+0x4000]))
                print(f'Verified {name} in DDR', flush=True)
        entry = storage.launcher(len(image), checkpoint=True)
        resume = storage.restore_stub(storage.gic_restore(usb), finish_timer_irq=True,
                                       capture_domain=True,
                                       return_tail=bootrom_tail if return_to_rom else None,
                                       extra_regs=(5, 12))
        assert storage.RESUME+len(resume) <= storage.REPORT
        storage.checked_write(usb, storage.ENTRY, entry)
        storage.checked_write(usb, storage.RESUME, resume)
        storage.checked_write(usb, storage.REPORT, bytes(188))
        usb.execute(storage.ENTRY)
        if not return_to_rom:
            time.sleep(12 if run_until_exec else 0.8)
            return read_report(usb, events, run_until_exec, headless, return_to_rom)
    time.sleep(15 if run_until_exec or usb_test else 3)
    with ROM() as usb:
        read_report(usb, events, run_until_exec, headless, return_to_rom)


def read_report(usb, events, run_until_exec, headless, return_to_rom):
    report = struct.unpack('<47I', usb.read(storage.REPORT, 188))
    assert report[1] == 2 and 1 <= report[40] <= len(events)
    name = events[report[40]-1][1]
    record = dict(event=name, r0=hex(report[8]), r2=hex(report[9]),
                  pc=hex(report[41]), dfar=hex(report[42]), dfsr=hex(report[43]),
                  ifsr=hex(report[44]), r1=hex(report[45]), lr=hex(report[46]),
                  headless=headless, rom_return=return_to_rom)
    physical = report[8]-VIRTUAL
    if name in ('die', 'panic', 'before_init_exec') and storage.KERNEL <= physical <= storage.LIMIT - 128:
        record['argument'] = usb.read(physical, 128).split(b'\0', 1)[0].decode(errors='replace')
    report_path = 'init-exec-trace.json' if run_until_exec else 'fault-trace.json'
    if '--usb-test' in sys.argv:
        report_path = 'usb-test-trace.json'
    if '--memory-128' in sys.argv:
        report_path = 'ram128-' + report_path
    if '--fatal-only' in sys.argv:
        report_path = 'fatal-' + report_path
    (storage.OUTPUT/report_path).write_text(json.dumps(record, indent=2))
    print(record, flush=True)


if __name__ == '__main__':
    main()
