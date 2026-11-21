"""Resume the interrupted factory rollback after verifying physical rows 0 through 4."""
import argparse
import json
import os
import struct
import time
from types import SimpleNamespace

from adfu_write_nand_block0 import load_plan, read_block, transact
from adfu_nand_cache_dma_probe import read_row
from build_autonomous_service import branch
from prepare_nand_write import OUT, sha
from service_nand_rom_probe import probe
from service_usb import Service
from simulate_nand_write import BLANK_PAGE, page, populated
from verify_boot_ecc import PAGE, parity


def verify_prefix(snapshot, image):
    if len(snapshot) != len(image) or snapshot[:4 * PAGE] != image[:4 * PAGE]:
        raise RuntimeError('Rows 0–3 or block size differ from factory backup')
    original = page(image, 4)
    known_bits = {1892: 0x10, 4487: 0x01}
    actual = page(snapshot, 4)
    if any(a != b and a ^ b != known_bits.get(i) for i, (a, b) in enumerate(zip(actual, original))):
        raise RuntimeError('Row 4 differs beyond the two observed ECC bits')


def exact_read(usb, row, expected):
    for attempt in range(1, 5):
        actual = read_row(usb, row)
        if actual == expected:
            return attempt
    if expected == BLANK_PAGE or actual[8192:] != expected[8192:]:
        raise RuntimeError(f'Row {row}: physical OOB or erased page differs')
    corrections = []
    for sector in range(8):
        off = sector * 1024
        errors = sum((a ^ b).bit_count() for a,b in zip(actual[off:off+1024], expected[off:off+1024]))
        if errors > 2:
            raise RuntimeError(f'Row {row}, sector {sector}: {errors} raw bits differ')
        label = f'rom-flags2-row{row}-sector{sector}'
        reference = (OUT / f'decoded/{label}-sector.bin').read_bytes()
        oob = 8192 + sector * 46
        if parity(reference, expected[oob:oob+4]) != expected[oob+4:oob+46]:
            raise RuntimeError(f'Row {row}, sector {sector}: backup ECC does not match reference')
        report, decoded, _ = probe(SimpleNamespace(row=row, sector=sector, flags=2, rom=False,
                                                    dry_run=False, backup_check=True), attached=usb)
        count = report['wait_results'][2][3]
        if decoded != reference or count > 2:
            raise RuntimeError(f'Row {row}, sector {sector}: hardware ECC readback differs')
        corrections.append(count)
    return {'raw_reads': 4, 'OOB_exact': True, 'ECC_corrected_bits_by_sector': corrections}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--continue-block0', metavar='ROLLBACK_SHA256')
    parser.add_argument('--start-row', type=int, choices=(5, 9, 12), default=12)
    args = parser.parse_args()
    images, _ = load_plan()
    image = images['rollback']
    digest = sha(image)
    if args.continue_block0 is not None and args.continue_block0 != digest:
        parser.error('Rollback image SHA-256 mismatch')
    proof = json.loads((OUT / 'block0-row4-resume-ecc.json').read_text())
    if [item['sector'] for item in proof] != list(range(8)):
        raise RuntimeError('Incomplete row 4 ECC evidence')
    for item in proof:
        reference = (OUT / f'decoded/rom-flags2-row4-sector{item["sector"]}-sector.bin').read_bytes()
        if len(reference) != 1024 or sha(reference) != item['decoded_sha256'] or item['ecc_register'] not in (0, 1):
            raise RuntimeError('Missing matching hardware ECC evidence')
    order = [row for row in populated(image) if row >= args.start_row]
    if len(order) != {5:55, 9:53, 12:52}[args.start_row] or order[0] != args.start_row or any(
        not args.start_row <= row < 256 for row in order
    ):
        raise RuntimeError('Unexpected resume range')
    with Service(exclusive=True) as usb:
        if usb.read(0xb4069efc, 4) != struct.pack('<I', branch(0xb4069efc, 0xb406a440)):
            raise RuntimeError('Expected diagnostic RAM service')
        snapshot = read_block(usb)
        if args.continue_block0 is None:
            (OUT / f'block0-resume-snapshot-{sha(snapshot)}.bin').write_bytes(snapshot)
        verify_prefix(snapshot, image)
        for row in range(5, args.start_row):
            if page(snapshot, row) != page(image, row):
                print(json.dumps({'preflight_row': row, 'verification': exact_read(usb, row, page(image, row))}), flush=True)
        if any(page(snapshot, row) != BLANK_PAGE for row in range(args.start_row, 256)):
            raise RuntimeError('Pending rows are not all blank. Stop without programming')
        summary = {'block': 0, 'current_sha256': sha(snapshot), 'rollback_sha256': digest,
                   'row4_exact': page(snapshot, 4) == page(image, 4), 'row4_OOB_exact': True,
                   'first_pending_row': order[0], 'last_pending_row': order[-1], 'pending_pages': len(order)}
        print(json.dumps(summary, indent=2), flush=True)
        (OUT / 'block0-resume-preflight.json').write_text(json.dumps(summary, indent=2) + '\n')
        if args.continue_block0 is None:
            return
        with (OUT / f'block0-resume-{time.time_ns()}.jsonl').open('x', encoding='utf-8') as journal:
            def record(event, **fields):
                msg = json.dumps({'event': event, **fields})
                journal.write(msg + '\n')
                journal.flush()
                os.fsync(journal.fileno())
                print(msg, flush=True)

            record('preflight', **summary)
            try:
                for row in order:
                    data = page(image, row)
                    record('program-start', row=row, sha256=sha(data))
                    status = transact(usb, 'program', row, data)
                    attempts = exact_read(usb, row, data)
                    record('program-verified', row=row, status=status, busy_observed=True, reads=attempts)
                current = read_block(usb)
                verify_prefix(current, image)
                for row in range(5, 256):
                    expected = page(image, row)
                    if page(current, row) != expected:
                        attempts = exact_read(usb, row, expected)
                        record('final-read-retried', row=row, reads=attempts)
                record('complete', sha256=sha(current), row4_exact=page(current, 4) == page(image, 4))
            except (Exception, KeyboardInterrupt) as error:
                record('stopped', error=str(error))
                raise


if __name__ == '__main__':
    main()
