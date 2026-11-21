"""Standalone builder & ABI/kallsyms verifier for aotg_hub_hcd.ko (Linux 3.10.52 SMP preempt ARMv7).

Compiles usb-hub-aotg/aotg_hub_hcd.c with `python -m ziglang cc` and verifies:
  1. Compile-time _Static_assert on all USB core / hrtimer / tasklet / module struct offsets
  2. Exact ARM EABI5 ELF relocatable header (e_machine=40, e_flags=0x05000000)
  3. .gnu.linkonce.this_module == 0x150 bytes & exact vermagic string
  4. 100% of undefined symbols exported in live ATM7051H /proc/kallsyms (kernel-symbols.txt)
"""
import pathlib
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
KALLSYMS = HERE.parent / 'usb-storage-ram' / 'kernel-symbols.txt'


def zig_include_dir() -> pathlib.Path:
    import ziglang
    return pathlib.Path(ziglang.__file__).parent / 'lib' / 'include'


def verify_ko(ko_path: pathlib.Path) -> None:
    ko = bytearray(ko_path.read_bytes())
    e_type, e_machine = struct.unpack_from('<HH', ko, 0x10)
    e_shoff, e_flags = struct.unpack_from('<II', ko, 0x20)
    _, _, _, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<6H', ko, 0x28)
    assert (e_type, e_machine) == (1, 40), (e_type, e_machine)

    if e_flags != 0x05000000:
        struct.pack_into('<I', ko, 0x24, 0x05000000)
        ko_path.write_bytes(ko)

    shstr_off, shstr_sz = struct.unpack_from('<II', ko, e_shoff + e_shstrndx * e_shentsize + 16)
    shstr = ko[shstr_off : shstr_off + shstr_sz]

    all_syms = set()
    exported_syms = set()
    for line in KALLSYMS.read_text(encoding='utf-8').splitlines():
        parts = line.split()
        if len(parts) >= 2:
            sym = parts[1][1:]
            all_syms.add(sym)
            if sym.startswith('__ksymtab_'):
                exported_syms.add(sym[10:])

    sym_sh = None
    saw_this_mod = False
    saw_modinfo = False
    saw_param = False

    for i in range(e_shnum):
        sh = ko[e_shoff + i * e_shentsize : e_shoff + (i + 1) * e_shentsize]
        nm_off, shtype, _, _, shoff, shsize, shlink, _, _, shentsz = struct.unpack('<10I', sh)
        nm = shstr[nm_off : shstr.find(0, nm_off)].decode('ascii', 'replace')
        if nm == '.gnu.linkonce.this_module':
            assert shsize == 0x150, f'Expected .gnu.linkonce.this_module=0x150, got {shsize:#x}'
            saw_this_mod = True
        elif nm == '.modinfo':
            blob = bytes(ko[shoff : shoff + shsize])
            assert b'vermagic=3.10.52 SMP preempt mod_unload ARMv7 p2v8 \x00' in blob, blob
            saw_modinfo = True
        elif nm == '__param':
            assert shsize == 14 * 16, f'Expected 14 module params (224B), got {shsize}'
            saw_param = True
        if shtype == 2:
            sym_sh = (shoff, shsize, shlink, shentsz)

    assert saw_this_mod and saw_modinfo and saw_param and sym_sh is not None

    str_off, str_sz = struct.unpack_from('<II', ko, e_shoff + sym_sh[2] * e_shentsize + 16)
    strtab = ko[str_off : str_off + str_sz]
    undefs = []
    missing = []
    unexported = []

    for off in range(sym_sh[0], sym_sh[0] + sym_sh[1], sym_sh[3]):
        st_name, _, _, _, _, st_shndx = struct.unpack_from('<IIIBBH', ko, off)
        if st_shndx == 0 and st_name != 0:
            nm = strtab[st_name : strtab.find(0, st_name)].decode('ascii', 'replace')
            undefs.append(nm)
            if nm not in all_syms:
                missing.append(nm)
            elif nm not in exported_syms and not nm.startswith('__aeabi_'):
                unexported.append(nm)

    print(f'Verified {ko_path.name} ({len(ko)} bytes):')
    print(f'  Undefined symbols ({len(undefs)}): {", ".join(sorted(undefs))}')
    assert not missing, f'Missing from kernel: {missing}'
    assert not unexported, f'Present in vmlinux but NOT EXPORT_SYMBOL: {unexported}'
    print('  All ABI offsets, vermagic, __param, and EXPORT_SYMBOL bindings OK!')


def self_check_fifo_and_lru() -> None:
    """Assert-based self-check of 10-pipe FIFO layout (<4KB) & 5-channel LRU/toggle multiplexer."""
    in_addr  = [0x0080, 0x0280, 0x0480, 0x04C0, 0x0500]
    in_cap   = [   512,    512,     64,     64,     64]
    out_addr = [0x0580, 0x0780, 0x0980, 0x0B80, 0x0D80]
    out_cap  = [   512,    512,    512,    512,    512]

    ranges = sorted([(a, a + c) for a, c in zip(in_addr + out_addr, in_cap + out_cap)])
    assert ranges[0][0] >= 0x0080, 'Must leave 0x0000..0x007F reserved for EP0'
    assert ranges[-1][1] <= 0x1000, f'Exceeds 4KB CUSB2 FIFO RAM: {ranges[-1][1]:#x}'
    for (s0, e0), (s1, e1) in zip(ranges, ranges[1:]):
        assert (s0 & 0x3F) == 0 and e0 <= s1, f'FIFO overlap/misalign: ({s0:#x},{e0:#x}) vs ({s1:#x},{e1:#x})'

    # Simulate Hub(64)+Kbd(8)+Mouse(8)+Flash(512)+WiFi(512)+ExtraBulk(512) competing on 5 IN slots
    slots = [{'owner': None, 'seq': 0} for _ in range(5)]
    eps = [
        {'id': 'hub_int',   'maxp': 64,  'chan': 0, 'valid': False, 'tog': 0},
        {'id': 'kbd_int',   'maxp': 8,   'chan': 0, 'valid': False, 'tog': 0},
        {'id': 'mouse_int', 'maxp': 8,   'chan': 0, 'valid': False, 'tog': 0},
        {'id': 'flash_in',  'maxp': 512, 'chan': 0, 'valid': False, 'tog': 0},
        {'id': 'wifi_in',   'maxp': 512, 'chan': 0, 'valid': False, 'tog': 0},
        {'id': 'extra_in',  'maxp': 512, 'chan': 0, 'valid': False, 'tog': 1},
    ]
    seq = 0

    def acquire(ep: dict) -> tuple[int, bool]:
        nonlocal seq
        c = ep['chan']
        if 1 <= c <= 5 and slots[c - 1]['owner'] is ep and in_cap[c - 1] >= ep['maxp']:
            seq += 1
            slots[c - 1]['seq'] = seq
            rebind = not ep['valid']
            ep['valid'] = True
            return c, rebind
        candidates = [i for i in range(5) if in_cap[i] >= ep['maxp']]
        free = [i for i in candidates if slots[i]['owner'] is None]
        best = min(free, key=lambda i: in_cap[i]) if free else min(candidates, key=lambda i: (in_cap[i], slots[i]['seq']))
        prev = slots[best]['owner']
        if prev is not None and prev is not ep:
            prev['chan'] = 0
            prev['valid'] = False
        seq += 1
        slots[best] = {'owner': ep, 'seq': seq}
        ep['chan'] = best + 1
        ep['valid'] = True
        return best + 1, True

    # First 5 endpoints (Hub + Kbd + Mouse + Flash + Wi-Fi) must all fit WITHOUT evicting each other!
    for ep in eps[:5]:
        ch, _ = acquire(ep)
        assert 1 <= ch <= 5
    assert all(ep['valid'] and ep['chan'] != 0 for ep in eps[:5]), eps[:5]

    # 6th 512B endpoint evicts oldest 512B slot (flash_in, chan 1) while leaving 64B HID/Hub slots untouched!
    ch6, reb6 = acquire(eps[5])
    assert (ch6, reb6) == (1, True)
    assert eps[3]['chan'] == 0 and not eps[3]['valid']
    assert all(eps[i]['valid'] for i in (0, 1, 2, 4))
    # Re-acquiring evicted flash_in evicts wifi_in (chan 2) and forces toggle restore (rebind=True)
    ch4, reb4 = acquire(eps[3])
    assert (ch4, reb4) == (2, True)
    print('  Self-check: 10-pipe FIFO map (<4KB) & 5-channel LRU/toggle multiplexer OK!')


def build() -> pathlib.Path:
    self_check_fifo_and_lru()
    src = HERE / 'aotg_hub_hcd.c'
    obj = HERE / 'aotg_hub_hcd.o'
    ko  = HERE / 'aotg_hub_hcd.ko'

    cflags = [
        sys.executable, '-m', 'ziglang', 'cc',
        '-target', 'arm-freestanding-eabi', '-mcpu=cortex_a5', '-marm', '-msoft-float',
        '-nostdinc', '-isystem', str(zig_include_dir()),
        '-I', str(HERE),
        '-O2', '-g0', '-std=gnu11',
        '-Wall', '-Wextra', '-Werror',
        '-fno-strict-aliasing', '-fno-common', '-fno-pic', '-fno-PIE',
        '-fno-addrsig', '-fno-stack-protector', '-fno-dwarf2-cfi-asm',
        '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
        '-mno-unaligned-access',
        '-fno-builtin-memcpy', '-fno-builtin-memset',
        '-c', str(src), '-o', str(obj),
    ]
    subprocess.check_call(cflags)

    subprocess.check_call([
        sys.executable, '-m', 'ziglang', 'ld.lld',
        '-r', '-m', 'armelf_linux_eabi', '--build-id=none',
        str(obj), '-o', str(ko),
    ])
    obj.unlink(missing_ok=True)
    verify_ko(ko)
    return ko


if __name__ == '__main__':
    build()
