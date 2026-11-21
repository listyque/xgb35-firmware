"""Exercise MBREC NAND register sequences against DDR through the ADFU RAM service."""
import argparse
import json
import secrets
import struct
import time

from build_autonomous_service import Arm, branch, mov
from prepare_nand_write import BLOCK, OUT, ROOT, sha
from service_usb import Service
from verify_boot_ecc import PAGE

CODE, INPUT, OUTPUT, TRACE = 0x01010000, 0x01011000, 0x01014000, 0x01017000
STATUS = TRACE + 0x200
NAND_FIFO = 0xb0320010  # Expected DMA destination in MBREC, recorded only as data.
SOURCE_SITES = {'erase': 0x17e38, 'program_setup': 0x188f0, 'program_data': 0x18934,
                'program_confirm': 0x189d4, 'status_read': 0x166ec, 'dma_helper': 0x1afc8}
STATUS_READ = ((0x20, 0x70), (0x0c, 1), (0x24, 0x61), (0x38, 1))


def encode(row):
    if row is None:
        # MBREC 0x17e38. 60h, three row cycles (zero), D0h.
        return (((0x20, 0xd060), (8, 0x20), (0x18, 0), (0x24, 0x6068), (0x38, 1)),)
    if not 0 <= row < 256:
        raise ValueError('Only physical block 0 is allowed')
    # Same three phases as MBREC 0x11f04..0x11fc0. The data phase is one full
    # raw transfer (data + OOB), as in the single-chunk branch of 0x18934.
    setup = ((0x14, 0), (8, 0x20), (0x18, row), (0x20, 0x80), (0x24, 0x6c), (0x38, 1))
    data = ((0x14, 0), (8, 0x20), (0x18, row), (0x0c, PAGE),
            (0x24, 0x40), (0x28, 0x85), (0x2c, 0x65), (0x38, 3))
    confirm = ((0x20, 0x10), (0x24, 0x60), (0x38, 1))
    return setup, data, confirm


def payload(phases, has_data, nonce):
    arm = Arm(CODE)
    emit = arm.emit
    # r0 is a trace cursor, r3 is the fixed shadow register bank. No calls or MMIO.
    emit(0xe92d0010, *mov(0, TRACE), *mov(3, TRACE + 0x100), *mov(1, nonce), 0xe4801004)

    def log(value):
        emit(*mov(1, value), 0xe4801004)

    def write_sequence(sequence):
        for offset, value in sequence:
            if offset not in (8, 0x0c, 0x14, 0x18, 0x20, 0x24, 0x28, 0x2c, 0x38):
                raise ValueError('Unexpected shadow register')
            log(offset)
            log(value)
            emit(0xe5831000 | offset)

    for index, sequence in enumerate(phases):
        write_sequence(sequence)
        if has_data and index == 1:
            # Record vendor DMA arguments, then use a bounded CPU copy between DDR buffers.
            for value in (INPUT, NAND_FIFO, PAGE, 2):
                log(value)
            emit(*mov(1, INPUT), *mov(2, OUTPUT), *mov(12, PAGE // 4))
            arm.label('copy')
            emit(0xe4914004, 0xe4824004, 0xe25cc001)
            arm.jump('copy', 1)
    write_sequence(STATUS_READ)
    emit(*mov(1, STATUS), 0xe5911000, 0xe4801004, 0xe3a02000,
         0xe201c060, 0xe35c0060, 0x13822001, 0xe3110001, 0x13822002,
         0xe3110080, 0x03822004, 0xe5802000,
         0xf57ff04f, 0xe3a00000, 0xe8bd0010, 0xe12fff1e)
    code = arm.finish()
    if len(code) >= INPUT - CODE:
        raise ValueError('Test code overlaps input buffer')
    return code


def expected_trace(phases, data, nonce, status):
    words = [nonce]
    for index, sequence in enumerate(phases):
        words.extend(word for pair in sequence for word in pair)
        if data and index == 1:
            words.extend((INPUT, NAND_FIFO, PAGE, 2))
    words.extend(word for pair in STATUS_READ for word in pair)
    errors = (0 if status & 0x60 == 0x60 else 1) | (2 if status & 1 else 0) | (0 if status & 0x80 else 4)
    words.extend((status, errors))
    return struct.pack(f'<{len(words)}I', *words)


def stage(usb, address, data):
    for index in range(0, len(data), 1024):
        chunk = data[index:index + 1024]
        usb.write(address + index, chunk)
        if usb.read(address + index, len(chunk)) != chunk:
            raise RuntimeError(f'RAM staging mismatch at {address + index:#x}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--offline', action='store_true', help='Build test code without connecting')
    args = parser.parse_args()
    plan = json.loads((OUT / 'write-plan.json').read_text())
    artifact = plan['target']['image']
    image = (OUT / artifact).read_bytes()
    if len(image) != BLOCK or sha(image) != plan['artifacts'][artifact]['sha256']:
        raise ValueError('Block 0 image hash or length mismatch')
    factory = (ROOT / 'xgb35-mbrec-recovered.bin').read_bytes()
    if sha(factory) != '34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc':
        raise ValueError('Unexpected MBREC source binary')
    rows = plan['target']['program_page_order']
    if plan['target']['erase_blocks'] != [0] or plan['preserve']['blocks'] != [1] or rows != [
        row for row in range(256) if image[row * PAGE:(row + 1) * PAGE] != b'\xff' * PAGE
    ]:
        raise ValueError('Unexpected target blocks or page order')
    for invalid in (-1, 256, 511):
        try:
            encode(invalid)
        except ValueError:
            continue
        raise RuntimeError('Accepted an out-of-block page')
    cases = [(None, 0xe0)] + [(row, 0xe0) for row in rows]
    cases.extend((0, status) for status in (0x80, 0xc0, 0xe1, 0x60))
    results = []

    def run(usb):
        for row, status in cases:
            data = b'' if row is None else image[row * PAGE:(row + 1) * PAGE]
            nonce = secrets.randbits(32)
            phases = encode(row)
            code = payload(phases, bool(data), nonce)
            wanted = expected_trace(phases, data, nonce, status)
            if len(wanted) > 0x100:
                raise RuntimeError('Trace overlaps shadow registers')
            entry = {'row': row, 'injected_status': status, 'expected_status_errors': wanted[-4],
                     'register_phases': phases, 'trace_sha256': sha(wanted)}
            if usb is not None:
                stage(usb, CODE, code)
                if data:
                    stage(usb, INPUT, data)
                    # Poison every byte, including OOB, so a partial copy cannot pass.
                    for offset in range(0, PAGE, 1024):
                        usb.write(OUTPUT + offset, bytes(v ^ 0xff for v in data[offset:offset + 1024]))
                usb.write(TRACE, bytes(0x204))
                usb.write(STATUS, struct.pack('<I', status))
                usb.execute(CODE)
                time.sleep(.025)
                if usb.read(TRACE, len(wanted)) != wanted:
                    raise RuntimeError(f'DDR trace differs at row {row}, status {status:#x}')
                expected_registers = bytearray(0x3c)
                for sequence in (*phases, STATUS_READ):
                    for offset, value in sequence:
                        struct.pack_into('<I', expected_registers, offset, value)
                if usb.read(TRACE + 0x100, len(expected_registers)) != expected_registers:
                    raise RuntimeError(f'DDR register shadow differs at row {row}')
                if data:
                    actual = usb.read(OUTPUT, PAGE)
                    if actual != data:
                        raise RuntimeError(f'DDR page and OOB differ at row {row}')
                    entry['raw_page_sha256'] = sha(actual)
            results.append(entry)

    if args.offline:
        run(None)
        print(f'Built {len(results)} DDR payloads, scope block 0')
        return
    with Service() as usb:
        if usb.read(0xb4069efc, 4) != struct.pack('<I', branch(0xb4069efc, 0xb406a440)):
            raise RuntimeError('Expected the diagnostic RAM service with returning callbacks')
        run(usb)
    report = {'test': 'ADFU RAM execution with DDR register shadow', 'block': 0, 'reserved_block': 1,
              'nand_MMIO_accessed': False, 'physical_NAND_written_or_erased': False,
              'candidate_sha256': sha(image), 'MBREC_sites_file_offsets': SOURCE_SITES,
              'status_register_phase': STATUS_READ, 'checked_program_rows': rows, 'cases': results,
              'status_error_bits': {'1': 'RDY or ARDY clear', '2': 'FAIL set', '4': 'WP# asserted'},
              'data_transfer': 'MBREC DMA arguments traced, bytes copied with CPU between DDR buffers',
              'untested': ['actual controller and DMA transactions', 'ready/busy timing and real status',
                           'physical readback and boot'], 'write_ready': False}
    (OUT / 'adfu-nand-write-ram-test.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Verified erase trace, {len(rows)} page/OOB transfers and four error statuses in device RAM')


if __name__ == '__main__':
    main()
