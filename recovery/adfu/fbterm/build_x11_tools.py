#!/usr/bin/env python3
"""Build X11 Desktop utilities (xgbjoyx11 + xgb-keytest) for XGB35 (ARMv7 Alpine musl)."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
RR = HERE.parent
STAGING = RR / 'alpine-bundle' / 'staging'
PORT_SYSROOT = RR.parents[1] / 'ports' / 'stardroid' / 'build' / 'sysroot'


def generate_cyr8x16(out_h: Path) -> None:
    """Render 66 crisp 8x16 Russian glyphs (U+0410..U+044F + U+0401 + U+0451) via Windows GDI."""
    gdi = c.windll.gdi32
    u32 = c.windll.user32

    class BITMAPINFOHEADER(c.Structure):
        _fields_ = [
            ('biSize', w.DWORD), ('biWidth', w.LONG), ('biHeight', w.LONG),
            ('biPlanes', w.WORD), ('biBitCount', w.WORD), ('biCompression', w.DWORD),
            ('biSizeImage', w.DWORD), ('biXPelsPerMeter', w.LONG), ('biYPelsPerMeter', w.LONG),
            ('biClrUsed', w.DWORD), ('biClrImportant', w.DWORD),
        ]

    hdc = gdi.CreateCompatibleDC(0)
    bmi = BITMAPINFOHEADER(c.sizeof(BITMAPINFOHEADER), 16, -16, 1, 32, 0, 0, 0, 0, 0, 0)
    bits_ptr = c.c_void_p()
    hbmp = gdi.CreateDIBSection(hdc, c.byref(bmi), 0, c.byref(bits_ptr), None, 0)
    gdi.SelectObject(hdc, hbmp)

    # Bold-ish crisp 14px Consolas / Courier New fits cleanly inside 8x16 cell
    hfont = gdi.CreateFontW(
        -13, 0, 0, 0, 600, 0, 0, 0, 204, 0, 0, 3, 49, 'Consolas'
    )
    gdi.SelectObject(hdc, hfont)
    gdi.SetBkColor(hdc, 0x000000)
    gdi.SetTextColor(hdc, 0xFFFFFF)
    gdi.SetBkMode(hdc, 2)  # OPAQUE

    cps = list(range(0x0410, 0x0450)) + [0x0401, 0x0451]
    assert len(cps) == 66
    raw_bytes = []
    pix = (c.c_uint32 * (16 * 16)).from_address(bits_ptr.value)

    for cp in cps:
        c.memset(bits_ptr, 0, 16 * 16 * 4)
        ch = chr(cp)
        gdi.TextOutW(hdc, 0, 1, ch, 1)
        gdi.GdiFlush()
        for y in range(16):
            row_bits = 0
            for x in range(8):
                val = pix[y * 16 + x]
                lum = ((val >> 16) & 0xFF) + ((val >> 8) & 0xFF) + (val & 0xFF)
                if lum >= 240:
                    row_bits |= (0x80 >> x)
            raw_bytes.append(row_bits)

    gdi.DeleteObject(hfont)
    gdi.DeleteObject(hbmp)
    gdi.DeleteDC(hdc)

    # Assert 'А' (idx 0), 'Я' (idx 31), 'ж' (idx 38) rendered non-empty bitmaps
    for idx in (0, 31, 38, 64, 65):
        assert sum(raw_bytes[idx * 16:(idx + 1) * 16]) > 0, f'Empty glyph at {idx}'

    lines = ['/* Auto-generated 8x16 Cyrillic glyph table (U+0410..U+044F, U+0401, U+0451) */',
             'static const unsigned char cyr8x16[66 * 16] = {']
    for i in range(66):
        chunk = ','.join(f'0x{b:02x}' for b in raw_bytes[i * 16:(i + 1) * 16])
        lines.append(f'    {chunk},')
    lines.append('};\n')
    out_h.write_text('\n'.join(lines), encoding='utf-8')


def build_stub_so(out_so: Path, soname: str, symbols: list[str]) -> None:
    c_src = out_so.with_suffix('.stub.c')
    c_src.write_text('\n'.join(f'void {s}(void) {{}}' for s in symbols) + '\n', encoding='utf-8')
    subprocess.run([
        sys.executable, '-m', 'ziglang', 'cc', '-target', 'arm-linux-musleabihf',
        '-nostdlib', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
        '-shared', f'-Wl,-soname,{soname}', str(c_src), '-o', str(out_so)
    ], check=True)
    c_src.unlink(missing_ok=True)


def verify_arm_elf(path: Path) -> None:
    raw = path.read_bytes()
    assert raw[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<H', raw, 18)[0] == 40, path
    assert b'/lib/ld-musl-armhf.so.1\x00' in raw, f'Missing musl PT_INTERP in {path}'
    print(f'  Verified {path.name}: {len(raw):,} bytes (ARMv7 dynamic musl ELF)')


def main() -> None:
    generate_cyr8x16(HERE / 'cyr8x16.h')
    stub_dir = PORT_SYSROOT / 'stubs'
    stub_dir.mkdir(parents=True, exist_ok=True)

    x11_stub = stub_dir / 'libX11.so.6'
    xtst_stub = stub_dir / 'libXtst.so.6'
    build_stub_so(x11_stub, 'libX11.so.6', [
        'XOpenDisplay', 'XCloseDisplay', 'XFlush', 'XKeysymToKeycode',
        'XCreateSimpleWindow', 'XSetWMNormalHints', 'XStoreName', 'XInternAtom',
        'XSetWMProtocols', 'XSelectInput', 'XMapWindow', 'XCreateGC', 'XFreeGC',
        'XCreateImage', 'XPutImage', 'XPending', 'XNextEvent', 'XLookupKeysym',
        'XDestroyWindow',
    ])
    build_stub_so(xtst_stub, 'libXtst.so.6', [
        'XTestFakeKeyEvent', 'XTestFakeButtonEvent', 'XTestFakeRelativeMotionEvent',
    ])

    cc = [
        sys.executable, '-m', 'ziglang', 'cc', '-target', 'arm-linux-musleabihf', '-mcpu=cortex_a5',
        '-O2', '-g0', '-fno-PIE', '-I' + str(HERE), '-I' + str(PORT_SYSROOT / 'include'), '-c',
    ]
    ld = [
        sys.executable, '-m', 'ziglang', 'ld.lld', '-m', 'armelf_linux_eabi',
        '-dynamic-linker', '/lib/ld-musl-armhf.so.1',
    ]
    libc_so = PORT_SYSROOT / 'lib' / 'libc.so'
    assert libc_so.is_file(), f'Missing {libc_so}'

    for idx, (label, stem) in enumerate([
        ('xgbjoyx11 (Gamepad + Hotplug USB Mouse/Keyboard -> X11)', 'xgbjoyx11'),
        ('xgb-keytest (Graphical XGB35 Button, Stick & USB Tester)', 'xgb-keytest'),
    ], 1):
        print(f'[{idx}/2] Compiling {label}...')
        obj = HERE / f'{stem}.o'
        out = HERE / stem
        subprocess.run(cc + [str(HERE / f'{stem}.c'), '-o', str(obj)], check=True)
        subprocess.run(ld + [str(obj), str(STAGING / 'libgcc_s.so.1'), str(libc_so), '-o', str(out)], check=True)
        obj.unlink(missing_ok=True)
        verify_arm_elf(out)


if __name__ == '__main__':
    main()
