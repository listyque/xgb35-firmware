"""Load one raw page into the NAND chip cache, reset it, and compare array row 0."""
import argparse
import json
import secrets
import struct
import time

from adfu_nand_write_ram_test import stage
from build_autonomous_service import Arm, branch, mov
from prepare_nand_write import OUT, sha
from service_nand_raw_probe import BASE, DATA, REPORT, build as raw_reader
from service_usb import Service
from verify_boot_ecc import PAGE

SOURCE, TRACE = 0x01011000, 0x01014000
CACHE_READBACK = 0x01015000
SECOND_READBACK, PATCH_SOURCE = 0x01018000, SOURCE + 0x2400
PATCH = bytes.fromhex('d35a09c7a48146be')
REARM_CASES = ('empty-column', 'update-column', 'update-row', 'reload-row')
CMU_REGS = (0xa0, 0x4c, 0xa8, 0x0c)
COMMAND_REGS = (0x20, 0x28)
CHUNKS = tuple((column, min(1024, PAGE - column)) for column in range(0, PAGE, 1024))
TRACE_SIZE = 0x60 + len(CHUNKS) * 0x20
PROGRAM_TRACE_SIZE = TRACE_SIZE + 0x20


def build(nonce, read_cache=False, *, operation='probe', row=0, rearm_test=None):
    if operation not in ('probe', 'program', 'erase') or not 0 <= row < 256:
        raise ValueError('Only block 0 is allowed')
    if (operation in ('probe', 'erase') and row != 0) or (operation == 'program' and not read_cache):
        raise ValueError('Invalid row or missing pre-confirmation readback')
    if rearm_test is not None and (rearm_test not in REARM_CASES or operation != 'probe' or not read_cache):
        raise ValueError('Rearm tests are restricted to the cache-only probe')
    arm = Arm(BASE)
    e = arm.emit
    commands = []
    e(0xe92d5ff0, *mov(9, TRACE), *mov(10, 0xb0150000), *mov(11, 0xb0320000),
      *mov(0, nonce), 0xe5890000, 0xe3a06000)

    def store(reg, value):
        if reg in COMMAND_REGS:
            allowed = {(0x20, 0xff), (0x20, 0x80), (0x28, 0x85), (0x20, 0xe005)}
            if operation == 'program':
                allowed.update(((0x20, 0x10), (0x20, 0x70), (0x20, 0x85)))
            if operation == 'erase':
                allowed.update(((0x20, 0xd060), (0x20, 0x70)))
            if rearm_test:
                allowed.update(((0x20, 0x85), (0x20, 0x70)))
            if (reg, value) not in allowed:
                raise ValueError('Unexpected NAND command in cache-only probe')
            commands.extend((value & 255, value >> 8) if value > 255 else (value,))
        if reg == 0x2c and value != 0x65 and not (
            (operation == 'program' or rearm_test in ('update-row', 'reload-row')) and value == 0x6d
        ):
            raise ValueError('Only one data-input sequence is allowed')
        e(*mov(0, value), 0xe58b0000 | reg)

    def delay(label, count):
        e(*mov(0, count))
        arm.label(label)
        e(0xe2500001)
        arm.jump(label, 1)

    def sample(reg, offset):
        e(0xe59b0000 | reg, 0xe5890000 | offset)

    def wait(label, *, ready, error):
        e(*mov(1, 0x400000))
        arm.label(label)
        e(0xe59b0004, 0xe59b2068, 0xe1803002, 0xe3130102)
        arm.jump(label + '_again', 1)
        if ready:
            e(0xe3100008)
            arm.jump(label + '_again', 0)
        arm.jump(label + '_done')
        arm.label(label + '_again')
        e(0xe2511001)
        arm.jump(label, 1)
        e(0xe3a06000 | error)
        arm.jump('abort' if error != 4 else 'restore')
        arm.label(label + '_done')

    def dma_chunk(index, column, count, source, sequence):
        if not 0 < count <= 1024 or column + count > PAGE:
            raise ValueError('Invalid NAND transfer length')
        offset = 0x60 + index * 0x20
        store(0x68, 0x60000000)
        store(0x0c, count)
        sample(0x0c, offset + 24)
        e(*mov(2, count), 0xe1500002, 0x13a06005)
        arm.jump('abort', 1)
        for reg, value in ((0x14, column), (8, 0x20), (0x18, row), (0x24, 0x40),
                           (0x28, 0x85), (0x2c, sequence), (0x38, 3), (0, 0x111),
                           (0x64, source), (0x68, 0x20000000 | count)):
            store(reg, value)
        store(0x68, 0xa0000000 | count)
        sample(0x68, offset)
        sample(4, offset + 4)
        e(0xe3a07000, *mov(1, 0x400000))
        arm.label(f'dma_poll_{index}')
        e(0xe59b0068, 0xe1877000, 0xe59b2004, 0xe1803002, 0xe3130102)
        arm.jump(f'dma_done_{index}', 0)
        e(0xe2511001)
        arm.jump(f'dma_poll_{index}', 1)
        e(0xe3a06003)
        arm.label(f'dma_done_{index}')
        e(0xe5897000 | (offset + 8))
        for i, reg in enumerate((0x68, 4, 0x38, 0x0c, 0x64), 3):
            sample(reg, offset + i * 4)
        e(0xe3560000)
        arm.jump('abort', 1)

    def cache_to_ddr(label, output, progress):
        # Change Read Column only. A READ PAGE would replace the cache with array data.
        for reg, value in ((0, 0x11), (0x14, 0), (0x20, 0xe005), (0x24, 0x6064), (0x38, 1)):
            store(reg, value)
        wait(label + '_column_idle', ready=False, error=6)
        delay(label + '_column_settle', 2048)
        e(*mov(4, output), *mov(5, PAGE // 8))
        arm.label(label + '_fifo_pair')
        for reg, value in ((0x0c, 8), (0x24, 0x41), (0x38, 1)):
            store(reg, value)
        e(*mov(1, 0x10000))
        arm.label(label + '_fifo_ready')
        e(0xe59b0004, 0xe3100010)
        arm.jump(label + '_fifo_copy', 1)
        e(0xe2511001)
        arm.jump(label + '_fifo_ready', 1)
        e(0xe3a06007)
        arm.jump('abort')
        arm.label(label + '_fifo_copy')
        e(0xe59b0010, 0xe4840004, 0xe59b0010, 0xe4840004, 0xe5894000 | progress, *mov(1, 0x10000))
        arm.label(label + '_fifo_idle')
        e(0xe59b0004, 0xe3100102)
        arm.jump(label + '_fifo_next', 0)
        e(0xe2511001)
        arm.jump(label + '_fifo_idle', 1)
        e(0xe3a06008)
        arm.jump('abort')
        arm.label(label + '_fifo_next')
        e(0xe2555001)
        arm.jump(label + '_fifo_pair', 1)

    def status_read(timeout_target):
        for reg, value in ((0x20, 0x70), (0x0c, 1), (0x24, 0x61), (0x38, 1)):
            store(reg, value)
        e(*mov(1, 0x400000))
        arm.label('write_status_fifo')
        e(0xe59b0004, 0xe3100010)
        arm.jump('write_status_received', 1)
        e(0xe2511001)
        arm.jump('write_status_fifo', 1)
        e(0xe3a0600b)
        arm.jump(timeout_target)
        arm.label('write_status_received')
        e(0xe59b0010, 0xe20000ff, 0xe589003c)

    def watch_busy(label, *, required):
        e(*mov(1, 0x400000 if required else 4096))
        arm.label(label + '_poll')
        e(0xe59b0004, 0xe3100008)
        arm.jump(label + '_seen', 0)
        e(0xe2511001)
        arm.jump(label + '_poll', 1)
        if required:
            e(0xe3a0600d)
            arm.jump('restore')
        else:
            arm.jump(label + '_done')
        arm.label(label + '_seen')
        e(0xe3a00001, 0xe5890050)
        arm.label(label + '_done')

    for index, reg in enumerate(CMU_REGS):
        e(0xe59a0000 | reg, 0xe5890000 | (0x40 + index * 4))
    e(*mov(0, 0x210a), 0xe58a000c, 0xf57ff04f)
    delay('pll_settle', 2048)
    e(0xe59a00a0, 0xe3800010, 0xe58a00a0, 0xe3a00001, 0xe58a004c,
      0xe59a00a8, 0xe3c00008, 0xe58a00a8, 0xf57ff04f)
    delay('controller_reset', 256)
    e(0xe59a00a8, 0xe3800008, 0xe58a00a8, 0xf57ff04f)
    delay('controller_settle', 256)
    store(0, 0x11)
    store(0x20, 0xff)
    store(0x24, 0x60)
    store(0x38, 1)
    delay('initial_reset_delay', 2048)
    wait('initial_ready', ready=True, error=1)
    sample(4, 8)

    if operation == 'erase':
        e(0xe3a00001, 0xe5890034)
        for reg, value in ((0x20, 0xd060), (8, 0x20), (0x18, 0), (0x24, 0x6068), (0x38, 1)):
            store(reg, value)

    if operation != 'erase':
        # MBREC 0x188f0: five address cycles, column zero and a row confined to block 0.
        for reg, value in ((0x14, 0), (8, 0x20), (0x18, row), (0x20, 0x80), (0x24, 0x6c), (0x38, 1)):
            store(reg, value)
        wait('setup_idle', ready=False, error=2)
        sample(4, 0x0c)

        # MBREC 0x18934 + 0x1afc8: raw data phase with 85h column addressing.
        # Prepare the DMA source before triggering the controller's transfer.
        e(*mov(0, SOURCE), *mov(1, SOURCE + PAGE))
        arm.label('clean_source')
        e(0xee070f3a, 0xe2800020, 0xe1500001)
        arm.jump('clean_source', 3)
        e(0xf57ff04f)
        # Match the ordering in MBREC: arm the transfer, enable the dedicated DMA
        # request, then start DMA. Its initializer sets bits 29 and 30.
        for index, (column, count) in enumerate(CHUNKS):
            dma_chunk(index, column, count, SOURCE + column, 0x65)
        if read_cache:
            cache_to_ddr('first', CACHE_READBACK, 0x30)
    if rearm_test:
        if rearm_test == 'empty-column':
            for reg, value in ((0x14, 0), (0x20, 0x85), (0x24, 0x64), (0x38, 1)):
                store(reg, value)
            wait('test_rearm_idle', ready=False, error=10)
            delay('test_rearm_settle', 2048)
        elif rearm_test == 'reload-row':
            dma_chunk(len(CHUNKS), PAGE - len(PATCH), len(PATCH), SOURCE + PAGE - len(PATCH), 0x6d)
        else:
            e(*mov(0, PATCH_SOURCE), 0xee070f3a, 0xf57ff04f)
            dma_chunk(len(CHUNKS), PAGE - len(PATCH), len(PATCH), PATCH_SOURCE,
                      0x6d if rearm_test == 'update-row' else 0x65)
        cache_to_ddr('second', SECOND_READBACK, 0x34)
        status_read('abort')
    if operation == 'program':
        e(*mov(4, CACHE_READBACK), *mov(5, SOURCE), *mov(12, PAGE // 4))
        arm.label('verify_cache')
        e(0xe4940004, 0xe4952004, 0xe1500002, 0x13a06009)
        arm.jump('abort', 1)
        e(0xe25cc001)
        arm.jump('verify_cache', 1)
        e(0xe3a00001, 0xe5890038)
        # After output mode use CHANGE ROW ADDRESS, then re-input the last
        # eight verified bytes. Two-column 85h fails to update data in this state.
        dma_chunk(len(CHUNKS), PAGE - len(PATCH), len(PATCH), SOURCE + PAGE - len(PATCH), 0x6d)
        delay('rearm_settle', 2048)
        e(0xe3a00001, 0xe5890034)
        for reg, value in ((0x20, 0x10), (0x24, 0x60), (0x38, 1)):
            store(reg, value)
    if operation != 'probe':
        watch_busy('confirm_busy', required=True)
        # A timeout after confirmation returns without sending RESET to a potentially busy die.
        wait('write_ready', ready=True, error=4)
        status_read('restore')
        e(0xe20010e1, 0xe35100e0, 0x13a0600c)
        arm.jump('restore')
    arm.label('abort')
    # Both completion and timeout run the same abort/reset sequence.
    store(0x68, 0x40000000)
    store(0, 0x11)
    store(0x20, 0xff)
    store(0x24, 0x60)
    store(0x38, 1)
    if rearm_test:
        watch_busy('reset_busy', required=False)
    delay('final_reset_delay', 2048)
    wait('final_ready', ready=True, error=4)
    sample(4, 0x28)
    e(0xe3a00001, 0xe589002c)
    arm.label('restore')
    e(0xe5896004)
    for index in (2, 1, 0, 3):
        e(0xe5990000 | (0x40 + index * 4), 0xe58a0000 | CMU_REGS[index])
    if read_cache:
        e(*mov(0, CACHE_READBACK), *mov(1, CACHE_READBACK + PAGE))
        arm.label('clean_readback')
        e(0xee070f3a, 0xe2800020, 0xe1500001)
        arm.jump('clean_readback', 3)
    if rearm_test:
        e(*mov(0, SECOND_READBACK), *mov(1, SECOND_READBACK + PAGE))
        arm.label('clean_second')
        e(0xee070f3a, 0xe2800020, 0xe1500001)
        arm.jump('clean_second', 3)
    e(0xf57ff04f, 0xe1a00009)
    e(*mov(1, TRACE + (PROGRAM_TRACE_SIZE if rearm_test or operation == 'program' else TRACE_SIZE)))
    arm.label('clean_trace')
    e(0xee070f3a, 0xe2800020, 0xe1500001)
    arm.jump('clean_trace', 3)
    e(0xf57ff04f, 0xe3a00000, 0xe8bd9ff0)
    read_commands = [5, 0xe0] if read_cache else []
    expected = [0xff, 0x80, *([0x85] * len(CHUNKS)), *read_commands]
    if operation == 'program':
        expected.extend((0x85, 0x10, 0x70))
    if operation == 'erase':
        expected = [0xff, 0x60, 0xd0, 0x70]
    if rearm_test:
        expected.extend((0x85, 5, 0xe0, 0x70))
    if commands != [*expected, 0xff]:
        raise RuntimeError('Unexpected bus command sequence')
    return arm.finish(), commands


def read_row(usb, row):
    if not 0 <= row < 256:
        raise ValueError('Only block 0 is allowed')
    nonce = secrets.randbits(32)
    stage(usb, BASE, raw_reader(row, nonce))
    usb.write(REPORT, bytes(32))
    usb.execute(BASE)
    time.sleep(.05)
    report = usb.read(REPORT, 32)
    if struct.unpack_from('<I', report)[0] != nonce or struct.unpack_from('<I', report, 16)[0] != 0:
        raise RuntimeError(f'Raw row {row} read failed: {report.hex()}')
    return usb.read(DATA, PAGE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--read-cache', action='store_true')
    args = parser.parse_args()
    nonce = secrets.randbits(32)
    code, commands = build(nonce, args.read_cache)
    if len(code) >= 4096 or commands[-1] != 0xff:
        raise RuntimeError('Invalid cache probe')
    if args.offline:
        print(f'{len(code)} code bytes, cache-only opcodes {[hex(c) for c in commands]}')
        return
    plan = json.loads((OUT / 'write-plan.json').read_text())
    name = plan['target']['image']
    image = (OUT / name).read_bytes()
    if sha(image) != plan['artifacts'][name]['sha256'] or plan['target']['erase_blocks'] != [0]:
        raise RuntimeError('Unexpected candidate')
    source = image[:PAGE]
    baseline = (OUT / 'block0-original-raw.bin').read_bytes()[:PAGE]
    with Service() as usb:
        signature = struct.pack('<I', branch(0xb4069efc, 0xb406a440))
        if usb.read(0xb4069efc, 4) != signature:
            raise RuntimeError('Expected the diagnostic RAM service')
        before = read_row(usb, 0)
        if before != baseline:
            raise RuntimeError('Row 0 differs from the saved baseline before the cache test')
        if before == source:
            raise RuntimeError('Cache payload must differ from the array contents')
        stage(usb, SOURCE, source)
        if args.read_cache:
            stage(usb, CACHE_READBACK, bytes(v ^ 0xff for v in source))
        stage(usb, BASE, code)
        usb.write(TRACE, bytes(TRACE_SIZE))
        usb.execute(BASE)
        time.sleep(.1)
        trace = usb.read(TRACE, TRACE_SIZE)
        words = struct.unpack_from('<12I', trace)
        if words[0] != nonce or words[11] != 1:
            raise RuntimeError(f'Probe did not confirm reset; stop here: {words!r}')
        cached = usb.read(CACHE_READBACK, PAGE) if args.read_cache else b''
        read_end, = struct.unpack_from('<I', trace, 0x30)
        after = read_row(usb, 0)
    transfers = []
    for i, (column, count) in enumerate(CHUNKS):
        start, ctl_start, seen, end, ctl_end, phase, data_count, address = struct.unpack_from(
            '<8I', trace, 0x60 + i * 0x20)
        transfers.append({'column': column, 'requested_bytes': count, 'data_count_register': data_count,
                          'dma_address_register': hex(address), 'dma_first_sample': hex(start),
                          'controller_first_sample': hex(ctl_start), 'dma_active_observed': bool((seen | start) >> 31),
                          'dma_final': hex(end), 'controller_final': hex(ctl_end), 'transfer_final': hex(phase),
                          'completed': bool((seen | start) >> 31) and not ((end | ctl_end) >> 31)
                          and data_count == count and address == SOURCE + column})
    report = {'row': 0, 'bytes_staged': len(source), 'source_sha256': sha(source),
              'array_before_sha256': sha(before), 'array_after_sha256': sha(after),
              'array_row_unchanged': before == after, 'block1_addressed': False,
              'nand_commands_in_cache_probe': [hex(c) for c in commands], 'error': words[1],
              'after_initial_reset': hex(words[2]), 'after_setup': hex(words[3]),
              'dma_transfers': transfers, 'after_final_reset': hex(words[10]),
              'final_reset_ready': words[11] == 1, 'cache_data_readback': False}
    filename = 'adfu-nand-cache-dma-probe.json'
    if args.read_cache:
        complete = read_end == CACHE_READBACK + PAGE
        report.update({'cache_bytes_clocked_out': read_end - CACHE_READBACK if read_end else 0,
                       'cache_data_readback': complete and cached == source and before != source,
                       'readback_matches_prior_array': complete and cached == before,
                       'readback_data_matches_input': complete and cached[:8192] == source[:8192],
                       'readback_OOB_matches_input': complete and cached[8192:] == source[8192:],
                       'readback_data_different_bytes': sum(a != b for a, b in zip(cached[:8192], source[:8192])),
                       'readback_OOB_different_bytes': sum(a != b for a, b in zip(cached[8192:], source[8192:])),
                       'input_difference_from_array': sum(a != b for a, b in zip(before, source)),
                       'first_32_bytes_hex': cached[:32].hex()})
        (OUT / 'adfu-nand-cache-readback.bin').write_bytes(cached)
        filename = 'adfu-nand-cache-readback.json'
    (OUT / filename).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'dma_transfers'}, indent=2))
    if words[1] or before != after or not all(t['completed'] for t in transfers):
        raise RuntimeError('Cache DMA verification did not pass')
    if args.read_cache and not report['cache_data_readback']:
        raise RuntimeError('Pre-reset cache data or OOB did not match the staged page')


if __name__ == '__main__':
    main()
