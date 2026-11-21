"""Check whether 85h after cache readback preserves or changes the volatile cache."""
import json
import secrets
import struct
import time

from adfu_nand_cache_dma_probe import (
    BASE, CACHE_READBACK, PATCH, PATCH_SOURCE, REARM_CASES, SECOND_READBACK, SOURCE,
    TRACE, TRACE_SIZE, build, read_row,
)
from adfu_nand_write_ram_test import stage
from adfu_write_nand_block0 import load_plan
from build_autonomous_service import branch
from prepare_nand_write import OUT, sha
from service_usb import Service
from verify_boot_ecc import PAGE


def main():
    images, _ = load_plan()
    source = images['rollback'][:PAGE]
    updated = source[:-len(PATCH)] + PATCH
    if source == updated:
        raise RuntimeError('Patch must differ from the original OOB')
    results = {}
    with Service(exclusive=True) as usb:
        if usb.read(0xb4069efc, 4) != struct.pack('<I', branch(0xb4069efc, 0xb406a440)):
            raise RuntimeError('Expected the diagnostic RAM service')
        before = read_row(usb, 0)
        if before != b'\xff' * PAGE:
            raise RuntimeError('Page 0 no longer matches the captured erased state')
        for case in REARM_CASES:
            nonce = secrets.randbits(32)
            code, commands = build(nonce, True, rearm_test=case)
            if len(code) >= 0x2000 or set(commands) != {0xff, 0x80, 0x85, 5, 0xe0, 0x70}:
                raise RuntimeError('Nonvolatile or out-of-range operation in probe')
            stage(usb, SOURCE, source)
            stage(usb, PATCH_SOURCE, PATCH)
            for addr in (CACHE_READBACK, SECOND_READBACK):
                stage(usb, addr, b'\xa7' * PAGE)
            stage(usb, BASE, code)
            usb.write(TRACE, bytes(TRACE_SIZE + 0x20))
            usb.execute(BASE)
            time.sleep(.1)
            trace = usb.read(TRACE, TRACE_SIZE + 0x20)
            actual_nonce, error = struct.unpack_from('<2I', trace)
            reset, first_end, second_end, _, idle_status = struct.unpack_from('<5I', trace, 0x2c)
            if actual_nonce != nonce or reset != 1:
                raise RuntimeError('Probe did not confirm NAND reset')
            first = usb.read(CACHE_READBACK, PAGE)
            second = usb.read(SECOND_READBACK, PAGE)
            after = read_row(usb, 0)
            if after != before:
                raise RuntimeError('Array row 0 differs after cache probe')
            results[case] = {'error': error, 'idle_status_without_program': hex(idle_status),
                'first_full': first_end == CACHE_READBACK + PAGE, 'first_matches_backup': first == source,
                'second_full': second_end == SECOND_READBACK + PAGE, 'second_matches_backup': second == source,
                'second_matches_patched_OOB': second == updated, 'second_sha256': sha(second),
                'second_OOB_tail': second[-len(PATCH):].hex(), 'nand_commands': [hex(c) for c in commands],
                'reset_busy_observed': struct.unpack_from('<I', trace, 0x50)[0] == 1,
                'extra_dma_registers': [hex(v) for v in struct.unpack_from('<8I', trace, TRACE_SIZE)],
                'array_row0_unchanged': True}
            print(case, json.dumps(results[case]), flush=True)
            (OUT / 'adfu-nand-rearm-probe.json').write_text(json.dumps({
                'row': 0, 'array_before_after_sha256': sha(before), 'block1_addressed': False,
                'array_commands_program_or_erase': False, 'backup_page_sha256': sha(source),
                'cases': results}, indent=2) + '\n')


if __name__ == '__main__':
    main()
