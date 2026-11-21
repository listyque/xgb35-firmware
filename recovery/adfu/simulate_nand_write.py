"""Rehearse block 0 replacement and rollback entirely in host memory."""
import json
from zipfile import ZipFile

from prepare_nand_write import BLOCK, OUT, ROOT, sha
from verify_boot_ecc import PAGE, parity, whitening

DATA = 8192
PAGES = 256
BLANK_PAGE = b'\xff' * PAGE
BLANK_BLOCK = BLANK_PAGE * PAGES


class NandError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise NandError(message)


def page(image, row):
    return image[row * PAGE:(row + 1) * PAGE]


def populated(image):
    return [row for row in range(PAGES) if page(image, row) != BLANK_PAGE]


class SimulatedNand:
    def __init__(self, original, reserve, fault=None):
        self.blocks = [bytearray(original), bytearray(reserve)]
        self.last_row = PAGES - 1
        self.fault = fault
        self.erases = 0
        self.programs = []

    def read_block(self, block):
        return bytes(self.blocks[block])

    def read_page(self, row):
        return bytes(page(self.blocks[0], row))

    def erase(self, block):
        require(block == 0, 'erase outside block 0')
        require(self.blocks[0][DATA] == 0xff, 'bad-block marker')
        self.erases += 1
        self.blocks[0][:] = BLANK_BLOCK
        self.last_row = -1
        if self.fault == ('incomplete_erase', None):
            self.blocks[0][-1] = 0xfe

    def program(self, row, data):
        require(0 <= row < PAGES, 'program outside block 0')
        require(len(data) == PAGE, 'program requires a whole page with OOB')
        require(row > self.last_row, 'page order or repeat program')
        require(self.read_page(row) == BLANK_PAGE, 'page is not erased')
        start = row * PAGE
        self.programs.append(row)
        self.last_row = row
        if self.fault == ('program_failure', row):
            self.blocks[0][start:start + PAGE // 2] = data[:PAGE // 2]
            raise NandError(f'program status failed at row {row}')
        self.blocks[0][start:start + PAGE] = data
        if self.fault == ('readback_mismatch', row):
            self.blocks[0][start + PAGE - 1] ^= 1


def replace_block0(nand, expected_before, reserve, image, order):
    require(len(image) == BLOCK and image[DATA] == 0xff, 'invalid image or OOB marker')
    require(order == populated(image), 'wrong page order or page set')
    current = nand.read_block(0)
    require(current[DATA] == 0xff, 'bad-block marker')
    require(current == expected_before, 'block 0 changed since capture')
    require(nand.read_block(1) == reserve, 'block 1 changed since capture')
    nand.erase(0)
    require(nand.read_block(0) == BLANK_BLOCK, 'erase readback mismatch')
    for row in order:
        expected = page(image, row)
        nand.program(row, expected)
        require(nand.read_page(row) == expected, f'page readback mismatch at row {row}')
    require(nand.read_block(0) == image, 'full block readback mismatch')
    require(nand.read_block(1) == reserve, 'reserved block changed')


def must_fail(operation, reason):
    try:
        operation()
    except NandError as error:
        require(reason in str(error), f'unexpected failure: {error}')
        return str(error)
    raise NandError(f'expected rejection: {reason}')


def main():
    plan = json.loads((OUT / 'write-plan.json').read_text())
    target = plan['target']
    geom = plan['geometry']
    require((geom['page_data'], geom['page_OOB'], geom['pages_per_erase_block']) ==
            (DATA, PAGE - DATA, PAGES), 'geometry mismatch')
    require(target['erase_blocks'] == [0] and target['physical_rows'] == [0, 255]
            and plan['preserve']['blocks'] == [1] and plan['rollback']['target_block'] == 0,
            'unexpected write scope')

    archive_path = OUT / 'nand-boot-recovery.zip'
    archive_digest = sha(archive_path.read_bytes())
    require(archive_digest == (OUT / 'recovery-archive.sha256').read_text().split()[0],
            'recovery archive hash mismatch')
    names = ('block0-original-raw.bin', 'block1-keep-original-raw.bin',
             plan['rollback']['preferred_image'], target['image'])
    with ZipFile(archive_path) as archive:
        images = []
        for name in names:
            require(name == (OUT / name).name, 'expected a local image filename')
            data = (OUT / name).read_bytes()
            require(len(data) == BLOCK and sha(data) == plan['artifacts'][name]['sha256'],
                    f'image size or hash mismatch: {name}')
            require(archive.read('nand-write-preparation/' + name) == data,
                    f'archive image mismatch: {name}')
            images.append(data)
    original, reserve, rollback, candidate = images
    order = target['program_page_order']
    require(order == populated(candidate) == populated(rollback), 'page list mismatch')
    changed = [i for i in range(PAGES) if page(candidate, i) != page(rollback, i)]
    require(changed == target['logical_replacement_rows'] == [0, 1, 2, 3],
            'unexpected changed pages')
    require(PAGES - len(order) == target['all_FF_pages_left_unprogrammed'], 'blank count mismatch')

    # Decode the simulated readback with the mask from the original row, and verify its ECC.
    factory = (OUT / 'block0-original-decoded.bin').read_bytes()
    require(len(factory) == PAGES * DATA and sha(factory) ==
            plan['artifacts']['block0-original-decoded.bin']['sha256'], 'factory decode mismatch')
    logical = (ROOT / 'autonomous-service-nand-candidate/logical-boot-image.bin').read_bytes()
    require(len(logical) == 4 * DATA and sha(logical) == plan['logical_candidate_sha256'],
            'logical candidate mismatch')
    nand = SimulatedNand(original, reserve)
    replace_block0(nand, original, reserve, candidate, order)
    written = nand.read_block(0)
    for row in changed:
        old_page, new_page = page(rollback, row), page(written, row)
        for sector in range(8):
            offset = sector * 1024
            index = row * DATA + offset
            original_data = factory[index:index + 1024]
            mask = whitening(bytes(a ^ b for a, b in zip(old_page[offset:offset + 22], original_data)))
            decoded = bytes(a ^ b for a, b in zip(new_page[offset:offset + 1024], mask))
            record = DATA + sector * 46
            meta = new_page[record:record + 4]
            require(decoded == logical[index:index + 1024] and
                    parity(decoded, meta) == new_page[record + 4:record + 46],
                    f'data or ECC mismatch at row {row}, sector {sector}')
    replace_block0(nand, written, reserve, rollback, order)
    require(nand.erases == 2 and nand.programs == order * 2, 'unexpected operations')

    faults = {}
    for fault, row, reason, attempted in (
        ('incomplete_erase', None, 'erase readback mismatch', []),
        ('program_failure', order[3], 'program status failed', order[:4]),
        ('readback_mismatch', order[3], 'page readback mismatch', order[:4]),
    ):
        trial = SimulatedNand(original, reserve, (fault, row))
        faults[fault] = must_fail(lambda: replace_block0(trial, original, reserve, candidate, order), reason)
        require(trial.programs == attempted and trial.read_block(1) == reserve, 'unsafe failure handling')
        trial.fault = None
        replace_block0(trial, trial.read_block(0), reserve, rollback, order)
        faults[fault] += ' (simulated rollback verified)'
    trial = SimulatedNand(original, reserve)
    blocked = {
        'erase_block1': must_fail(lambda: trial.erase(1), 'outside block 0'),
        'program_block1': must_fail(lambda: trial.program(256, BLANK_PAGE), 'outside block 0'),
        'reordered_pages': must_fail(lambda: replace_block0(trial, original, reserve, candidate,
                                                            order[::-1]), 'wrong page order'),
        'stale_backup': must_fail(lambda: replace_block0(trial, BLANK_BLOCK, reserve, candidate,
                                                        order), 'changed since capture'),
    }
    require(trial.erases == 0 and not trial.programs and trial.read_block(1) == reserve,
            'rejected preflight changed NAND')
    trial.blocks[0][DATA] = 0
    blocked['bad_block'] = must_fail(lambda: trial.erase(0), 'bad-block marker')
    trial = SimulatedNand(original, reserve)
    trial.erase(0)
    trial.program(1, page(candidate, 1))
    blocked['repeat_page'] = must_fail(lambda: trial.program(1, page(candidate, 1)), 'repeat program')
    blocked['backward_page'] = must_fail(lambda: trial.program(0, page(candidate, 0)), 'page order')
    blocked['partial_page'] = must_fail(lambda: trial.program(2, b'\x00'), 'whole page')
    require(trial.programs == [1] and trial.read_block(1) == reserve, 'rejected program changed NAND')

    report = {'mode': 'host_memory_only', 'physical_NAND_access': False, 'physical_write_ready': False,
              'archive_sha256': archive_digest, 'programmed_pages_per_pass': len(order),
              'blank_pages_per_pass': PAGES - len(order), 'changed_pages_vs_rollback': changed,
              'candidate_readback_sha256': sha(written), 'readback_ECC_sectors_verified': len(changed) * 8,
              'rollback_readback_sha256': sha(nand.read_block(0)), 'block1_sha256': sha(nand.read_block(1)),
              'injected_failures': faults, 'rejected_operations': blocked,
              'untested': ['device PROGRAM/ERASE transport, status and timing', 'power-on NAND boot']}
    (OUT / 'nand-write-simulation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
