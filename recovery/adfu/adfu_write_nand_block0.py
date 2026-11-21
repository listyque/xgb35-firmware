"""Program or restore physical block 0 via the diagnostic ADFU RAM service."""
import argparse
import json
import os
import secrets
import struct
import time
from zipfile import ZipFile

from adfu_nand_cache_dma_probe import BASE, CACHE_READBACK, PROGRAM_TRACE_SIZE, SOURCE, TRACE, TRACE_SIZE, build, read_row
from adfu_nand_write_ram_test import stage
from build_autonomous_service import branch
from nand_backup_format import distance
from prepare_nand_write import BLOCK, OUT, sha
from service_usb import Service
from simulate_nand_write import BLANK_BLOCK, BLANK_PAGE, page, populated
from verify_boot_ecc import PAGE


def load_plan():
    plan_bytes = (OUT / 'write-plan.json').read_bytes()
    plan = json.loads(plan_bytes)
    target, rollback = plan['target'], plan['rollback']
    if target['erase_blocks'] != [0] or target['physical_rows'] != [0, 255] or rollback['target_block'] != 0:
        raise ValueError('Plan does not target physical block 0 exclusively')
    archive_path = OUT / 'nand-boot-recovery.zip'
    if sha(archive_path.read_bytes()) != (OUT / 'recovery-archive.sha256').read_text().split()[0]:
        raise ValueError('Recovery archive SHA-256 mismatch')
    with ZipFile(archive_path) as archive:
        if archive.read('nand-write-preparation/write-plan.json') != plan_bytes:
            raise ValueError('Write plan differs from the archived plan')
        images = {}
        for mode, name in (('write', target['image']), ('rollback', rollback['preferred_image'])):
            data = archive.read('nand-write-preparation/' + name)
            if len(data) != BLOCK or data[8192] != 0xff or sha(data) != plan['artifacts'][name]['sha256']:
                raise ValueError(f'Wrong {mode} image size, bad-block marker or SHA-256')
            if populated(data) != target['program_page_order']:
                raise ValueError(f'Unexpected {mode} page order')
            images[mode] = data
        archived_reads = {sha(archive.read('nand-write-preparation/' + name)[:BLOCK]) for name in (
            'live-blocks-0-1-raw.bin', 'live-blocks-0-1-repeat-raw.bin')}
        for name in ('adfu-nand-first-256-pages-raw-with-oob.bin', 'adfu-nand-first-32MiB-raw-with-oob.bin'):
            with archive.open(name) as dump:
                archived_reads.add(sha(dump.read(BLOCK)))
    return images, archived_reads


def read_block(usb):
    return b''.join(read_row(usb, row) for row in range(256))


def original_differences(captured, corrected):
    """Compare against the recovered codewords without tolerating changes outside ECC."""
    if len(captured) != BLOCK or captured[8192] != 0xff:
        return None
    differences = 0
    for row in range(256):
        actual, known = page(captured, row), page(corrected, row)
        if actual == known:
            continue
        if known == BLANK_PAGE or actual[8560:] != known[8560:]:
            return None
        for step in range(8):
            offset, oob = step * 1024, 8192 + step * 46
            errors = distance(actual[offset:offset + 1024], known[offset:offset + 1024])
            errors += distance(actual[oob:oob + 46], known[oob:oob + 46])
            if errors > 2:
                return None
            differences += errors
    return differences


def check_reply(report, nonce, operation):
    actual_nonce, error = struct.unpack_from('<2I', report)
    confirmed, cache_ok, status = struct.unpack_from('<3I', report, 0x34)
    busy, = struct.unpack_from('<I', report, 0x50)
    if (actual_nonce != nonce or error or confirmed != 1 or busy != 1 or status & 0xe1 != 0xe0
            or (operation == 'program' and cache_ok != 1)):
        raise RuntimeError(f'{operation} failed: nonce match={actual_nonce == nonce}, error={error}, '
                           f'confirmation flag={confirmed}, busy={busy}, cache verified={cache_ok}, status={status:#04x}')
    return status


def transact(usb, operation, row=0, data=None):
    nonce = secrets.randbits(32)
    program = operation == 'program'
    code, _ = build(nonce, program, operation=operation, row=row)
    if not BASE + len(code) <= 0x01002000:
        raise ValueError('Payload overlaps the diagnostic service scratch space')
    if program:
        if data is None or len(data) != PAGE:
            raise ValueError('Expected a complete data and OOB page')
        stage(usb, SOURCE, data)
        stage(usb, CACHE_READBACK, bytes(v ^ 0xff for v in data))
    stage(usb, BASE, code)
    report_size = PROGRAM_TRACE_SIZE if program else TRACE_SIZE
    usb.write(TRACE, bytes(report_size))
    usb.execute(BASE)
    time.sleep(.1)
    status = check_reply(usb.read(TRACE, report_size), nonce, operation)
    if program and usb.read(CACHE_READBACK, PAGE) != data:
        raise RuntimeError(f'Row {row}: saved verification buffer differs from input')
    return status


def replace_block0(usb, image, record, *, already_erased=False):
    if not already_erased:
        record('erase-start', block=0)
        record('erase-status', status=transact(usb, 'erase'))
        if read_block(usb) != BLANK_BLOCK:
            raise RuntimeError('Block 0 failed full erase readback')
    record('erase-verified', block=0, reused_blank_block=already_erased)
    for row in populated(image):
        data = page(image, row)
        record('program-start', row=row, sha256=sha(data))
        status = transact(usb, 'program', row, data)
        if read_row(usb, row) != data:
            raise RuntimeError(f'Row {row}: physical data/OOB readback differs')
        record('program-verified', row=row, status=status)
    if read_block(usb) != image:
        raise RuntimeError('Full block 0 readback differs')
    record('complete', sha256=sha(image))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('plan', 'inspect', 'write', 'rollback'), nargs='?', default='plan')
    parser.add_argument('--expect-current', metavar='CURRENT_BLOCK0_SHA256')
    parser.add_argument('--execute-block0', metavar='TARGET_IMAGE_SHA256')
    args = parser.parse_args()
    images, archived_reads = load_plan()
    target_hashes = {mode: sha(image) for mode, image in images.items()}
    if args.mode == 'plan':
        for row in populated(images['write']):
            code, _ = build(1, True, operation='program', row=row)
            if BASE + len(code) > 0x01002000:
                raise ValueError('Page payload exceeds its RAM region')
        build(1, operation='erase')
        print(json.dumps({'block': 0, 'pages': len(populated(images['write'])),
                          'write_or_rollback_sha256': target_hashes, 'device_access': False}, indent=2))
        return
    destructive = args.mode in images
    if destructive and (args.execute_block0 != target_hashes[args.mode] or not args.expect_current):
        parser.error('write and rollback require --execute-block0 <target hash> and --expect-current <inspect hash>')
    if destructive and (len(args.expect_current) != 64 or any(c not in '0123456789abcdef' for c in args.expect_current)):
        parser.error('--expect-current must be a lowercase SHA-256 digest')
    with Service(exclusive=True) as usb:
        if usb.read(0xb4069efc, 4) != struct.pack('<I', branch(0xb4069efc, 0xb406a440)):
            raise RuntimeError('Expected diagnostic ADFU RAM service')
        current = read_block(usb)
        digest = sha(current)
        if not destructive:
            snapshot = OUT / f'block0-inspect-{digest}.bin'
            if not snapshot.exists():
                snapshot.write_bytes(current)
            print(json.dumps({'current_block0_sha256': digest, 'bad_block_marker': current[8192],
                              'matches_archived_original': digest in archived_reads,
                              'bits_from_corrected_original': original_differences(current, images['rollback']),
                              'target_hashes': target_hashes, 'snapshot': str(snapshot)}, indent=2))
            return
        image = images[args.mode]
        reviewed = (OUT / f'block0-inspect-{args.expect_current}.bin').read_bytes()
        if sha(reviewed) != args.expect_current or len(reviewed) != BLOCK or current[8192] != 0xff:
            raise RuntimeError('Inspect snapshot hash, length or live bad-block marker failed')
        differences = original_differences(current, images['rollback']) if args.mode == 'write' else None
        if current == image:
            print('Block 0 already matches the selected image')
            return
        if args.mode == 'write' and (differences is None or original_differences(reviewed, images['rollback']) is None):
            raise RuntimeError('Inspected or current block is outside the verified original codeword bounds')
        if args.mode == 'rollback' and current != reviewed:
            raise RuntimeError('Block 0 changed since the inspected rollback snapshot')
        snapshot = OUT / f'block0-before-{digest}.bin'
        if not snapshot.exists():
            with snapshot.open('xb') as handle:
                handle.write(current)
                handle.flush()
                os.fsync(handle.fileno())
        if snapshot.read_bytes() != current:
            raise RuntimeError('Saved preflight snapshot differs from the live read')
        journal = OUT / f'block0-{args.mode}-{time.time_ns()}.jsonl'
        with journal.open('x', encoding='utf-8') as handle:
            def record(event, **details):
                line = json.dumps({'event': event, **details})
                handle.write(line + '\n')
                handle.flush()
                os.fsync(handle.fileno())
                print(line, flush=True)

            record('preflight', mode=args.mode, reviewed=args.expect_current, before=digest,
                   target=target_hashes[args.mode], bits_from_corrected_original=differences)
            try:
                replace_block0(usb, image, record, already_erased=current == BLANK_BLOCK)
            except (Exception, KeyboardInterrupt) as error:
                record('stopped', error=str(error))
                raise


if __name__ == '__main__':
    main()
