import argparse
import ctypes as c
import hashlib
import os
import re
import struct
import time
from ctypes import wintypes as w
from pathlib import Path

P, U, B = c.c_void_p, c.c_uint32, c.c_ubyte
PAGE_BYTES = 8936
DATA_BYTES = 8192
PAGE_COUNT = 4096
ROOT = Path(__file__).resolve().parent
INTERFACE = re.compile(
    r"^\\\\\?\\USB#VID_10D6&PID_10D6#[^#]+#\{ff7b846f-b0a3-45c0-b0fc-d8122086996d\}$",
    re.IGNORECASE,
)


def find_pato(data):
    return [m.start() for m in re.finditer(b"PATO", data)]


def report_dump(path):
    raw = path.read_bytes()
    if len(raw) != PAGE_COUNT * PAGE_BYTES:
        raise ValueError(
            f"Expected {PAGE_COUNT * PAGE_BYTES:,} bytes, got {len(raw):,}"
        )
    main = b"".join(
        raw[offset : offset + DATA_BYTES] for offset in range(0, len(raw), PAGE_BYTES)
    )
    print(f"SHA-256: {hashlib.sha256(raw).hexdigest()}")
    print(f"PATO in raw dump: {[hex(offset) for offset in find_pato(raw)]}")
    print(
        f"PATO in page data without OOB: {[hex(offset) for offset in find_pato(main)]}"
    )


def reference_pages(all_pages=False):
    for name, first, end, rows in (
        ("adfu-nand-first-256-pages-raw-with-oob.bin", 0, 256, (0, 31, 255)),
        ("adfu-nand-blocks-1-3-raw-with-oob.bin", 256, 1024, (256, 512, 768, 1023)),
    ):
        with (ROOT / name).open("rb") as reference:
            if all_pages:
                rows = range(first, end)
            for row in rows:
                reference.seek((row - first) * PAGE_BYTES)
                page = reference.read(PAGE_BYTES)
                if len(page) != PAGE_BYTES:
                    raise ValueError(f"Reference page {row} is incomplete")
                yield row, page


def main():
    parser = argparse.ArgumentParser(
        description="Read the first 32 MiB of XGB35 NAND via USB ADFU."
    )
    parser.add_argument(
        "interface",
        help="WinUSB device interface path from pnputil /enum-devices /interfaces",
    )
    parser.add_argument(
        "--probe",
        action="store_true",
        help="Test fast reading on the first 8 MiB against the saved raw dumps",
    )
    parser.add_argument(
        "--fast",
        action="store_true",
        help="Use the shorter ADFU delays tested on the first 8 MiB",
    )
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--pages", nargs=2, type=int, metavar=("FIRST", "COUNT"))
    selection.add_argument(
        "--block-heads", nargs=2, type=int, metavar=("FIRST", "COUNT")
    )
    args = parser.parse_args()
    if not INTERFACE.fullmatch(args.interface):
        parser.error("expected the Actions ADFU WinUSB interface (10d6:10d6)")
    rows = range(PAGE_COUNT)
    out = ROOT / "adfu-nand-first-32MiB-raw-with-oob.bin"
    if args.pages or args.block_heads:
        if args.probe:
            parser.error("--probe reads the entire reference range")
        first, count = args.block_heads or args.pages
        step = 256 if args.block_heads else 1
        if first < 0 or count < 1 or (first + count - 1) * step >= 1 << 20:
            parser.error("requested range is outside the 64 Gbit NAND")
        rows = range(first * step, (first + count) * step, step)
        label = "block-heads" if args.block_heads else "pages"
        out = ROOT / f"adfu-nand-{label}-{first}-{count}-raw-with-oob.bin"
    read_nand(
        args.interface, args.probe, fast=args.fast or args.probe, rows=rows, out=out
    )


def bind(lib, name, result, *args):
    f = getattr(lib, name)
    f.restype, f.argtypes = result, args
    return f


usb = c.WinDLL("winusb", use_last_error=True)
krn = c.WinDLL("kernel32", use_last_error=True)
CFile = bind(krn, "CreateFileW", w.HANDLE, w.LPCWSTR, U, U, P, U, U, P)
Close = bind(krn, "CloseHandle", w.BOOL, w.HANDLE)
Init = bind(usb, "WinUsb_Initialize", w.BOOL, w.HANDLE, c.POINTER(P))
Free = bind(usb, "WinUsb_Free", w.BOOL, P)
Policy = bind(usb, "WinUsb_SetPipePolicy", w.BOOL, P, B, U, U, P)
Write = bind(usb, "WinUsb_WritePipe", w.BOOL, P, B, P, U, c.POINTER(U), P)
Read = bind(usb, "WinUsb_ReadPipe", w.BOOL, P, B, P, U, c.POINTER(U), P)


def check(ok, label):
    if not ok:
        raise OSError(c.get_last_error(), label)


def read_nand(interface, probe, *, fast, rows, out):
    f = CFile(interface, 0xC0000000, 3, None, 3, 0x40000000, None)
    check(f not in (None, P(-1).value), "open ADFU")
    h = P()
    try:
        check(Init(f, c.byref(h)), "start WinUSB")
        ms = U(2400)
        for ep in (1, 0x82):
            check(Policy(h, ep, 3, 4, c.byref(ms)), "set timeout")

        def write(data):
            n = U()
            check(Write(h, 1, data, len(data), c.byref(n), None), "write USB packet")
            if n.value != len(data):
                raise ValueError("short OUT")

        def read(size, label):
            buf = c.create_string_buffer(size)
            n = U()
            check(Read(h, 0x82, buf, size, c.byref(n), None), label)
            return buf.raw[: n.value]

        def command(cdb, size=0, direction=0):
            write(struct.pack("<4sIIBBB16s", b"USBC", 0, size, direction, 0, 16, cdb))

        def status(label, data=None):
            s = read(13, label) if data is None else data
            if len(s) != 13 or s[:4] != b"USBS":
                raise ValueError(f"{label}: invalid status {s.hex()}")
            _, tag, res, code = struct.unpack("<4sIIB", s)
            if label == "IDENT":
                print(f"{label}: tag={tag} residue={res} status={code}", flush=True)
            if (tag, res, code) != (0, 0, 0):
                raise ValueError(f"{label}: refused")

        def read_data(size, label):
            data = read(size, label)
            if len(data) == 13 and data.startswith(b"USBS"):
                status(label, data)
            if len(data) != size:
                raise ValueError(f"{label}: got {len(data)} bytes, expected {size}")
            status(label)
            return data

        command(bytes((0xCC, 0, 0, 0, 0, 0, 0, 18)), 18, 0x80)
        if read_data(18, "IDENT") != bytes.fromhex(
            "004341444655445202450000000000000000"
        ):
            raise ValueError("unexpected target")
        address = 0xB4068800
        size = 512
        status_bytes = 64

        def upload(data):
            command(struct.pack("<BII", 5, address, size), size)
            write(data)
            status("SRAM upload")

        def sram(at, nbytes):
            command(struct.pack("<BIIB", 5, at, nbytes, 0x80), nbytes, 0x80)
            return read_data(nbytes, "SRAM read")

        def read_page(row):
            # Read one physical page and its OOB through the controller FIFO.
            words = [
                0xE3000000,
                0xE34B0015,
                0xE28F1E1F,
                0xE5814020,
                0xE59020A0,
                0xE5812000,
                0xE3822010,
                0xE58020A0,
                0xE590200C,
                0xE5812004,
                0xE300210A,
                0xE580200C,
                0xF57FF04F,
                0xE3A02B02,
                0xE2522001,
                0x1AFFFFFD,
                0xE590204C,
                0xE5812008,
                0xE3A02001,
                0xE580204C,
                0xE59020A8,
                0xE581200C,
                0xE3C22008,
                0xE58020A8,
                0xF57FF04F,
                0xE3A02C01,
                0xE2522001,
                0x1AFFFFFD,
                0xE591200C,
                0xE3822008,
                0xE58020A8,
                0xF57FF04F,
                0xE3A02C01,
                0xE2522001,
                0x1AFFFFFD,
            ]

            def emit(*ops):
                words.extend(ops)

            labels = {}
            branches = []

            def label(name):
                labels[name] = len(words)

            def branch(cond, name):
                branches.append((len(words), cond, name))
                emit(0)

            def nand_store(offset, value):
                emit(0xE3002000 | ((value & 0xF000) << 4) | (value & 0xFFF))
                if value >> 16:
                    emit(0xE3402000 | (value >> 16))
                emit(0xE58C2000 | offset)

            def wait_status(name, offset, error, fifo=False):
                emit(0xE3A03601)
                label(name + "poll")
                # The controller can be idle while the NAND array is still busy.
                emit(0xE59C2004)
                if name == "page":
                    emit(0xE2022288, 0xE3520008)
                    branch(0, name + "ready")
                else:
                    emit(0xE3120010 if fifo else 0xE3520000)
                    branch(1 if fifo else 5, name + "ready")
                emit(0xE2533001)
                branch(1, name + "poll")
                emit(0xE5812000 | offset, 0xE3A02000 | error)
                branch(14, "restore")
                label(name + "ready")
                if name == "page":
                    emit(0xE59C2004)
                emit(0xE5812000 | offset)

            emit(0xE300C000, 0xE34BC032)
            nand_store(0, 0x11)
            nand_store(0x20, 0xFF)
            nand_store(0x24, 0x60)
            nand_store(0x38, 1)
            wait_status("reset", 16, 2)
            # Use column zero and select the requested physical row.
            emit(0xE3A02000, 0xE58C2014)
            nand_store(0x18, row)
            nand_store(8, 0x20)
            nand_store(0x20, 0x3000)
            nand_store(0x24, 0x606C)
            nand_store(0x38, 1)
            emit(0xF57FF04F)
            wait_status("page", 20, 3)
            # The READ PAGE already selected column zero. Read data with no command or address phase.
            # Vendor 0x49 is address + data without a command. Clear the address phase.
            emit(0xE2813040, 0xE30008BA)
            label("block_start")
            nand_store(0xC, 8)
            nand_store(0x24, 0x41)
            nand_store(0x38, 1)
            emit(0xE3A04601)
            label("block_wait")
            emit(0xE59C2004, 0xE3120010)
            branch(1, "copy_fifo")
            emit(0xE2544001)
            branch(1, "block_wait")
            branch(14, "incomplete")
            label("copy_fifo")
            emit(0xE59C2010, 0xE4832004, 0xE2500001)
            branch(0, "complete")
            emit(0xE3100001)
            branch(1, "copy_fifo")
            emit(0xE3A04601)
            label("block_idle")
            emit(0xE59C2004, 0xE3520000)
            branch(5, "block_start")
            emit(0xE2544001)
            branch(1, "block_idle")
            label("incomplete")
            emit(0xE5812018, 0xE5810024, 0xE3A02006)
            branch(14, "restore")
            label("complete")
            completion = 0x8000 | ((row ^ (row >> 15)) & 0x7FFF)
            emit(
                0xE5810024,
                0xE3002000 | ((completion & 0xF000) << 4) | (completion & 0xFFF),
            )
            label("restore")
            emit(
                0xE581201C,
                0xE5914020,
                0xE3000000,
                0xE34B0015,
                0xE591200C,
                0xE58020A8,
                0xE5912000,
                0xE58020A0,
                0xE5912008,
                0xE580204C,
                0xE5912004,
                0xE580200C,
                0xF57FF04F,
                0xE12FFF1E,
            )
            for i, cond, name in branches:
                words[i] = (
                    (cond << 28) | 0x0A000000 | ((labels[name] - i - 2) & 0xFFFFFF)
                )
            if len(words) * 4 > size:
                raise ValueError(f"program overlaps output: {len(words) * 4}")
            payload = struct.pack("<" + "I" * len(words), *words).ljust(size, b"\0")
            upload(payload)
            if not fast:
                time.sleep(0.15)
            if sram(address, len(payload)) != payload:
                raise ValueError(f"Page {row}: SRAM readback differs; no launch")
            command(struct.pack("<BI", 0x10, address))
            status("ROM handoff")
            # A 10 ms delay corrupted a page. 25 ms passed a comparison of all 1024 reference pages.
            poll_delay = 0.025 if fast else 0.10
            time.sleep(poll_delay)
            deadline = time.monotonic() + 0.8
            while True:
                result = sram(address + size, size)
                reset, page, fifo, code = struct.unpack_from("<4I", result, 16)
                if code == completion:
                    break
                if time.monotonic() >= deadline:
                    raise ValueError(
                        f"Page {row}: reset={reset:08X} page={page:08X} FIFO={fifo:08X} code={code:08X}"
                    )
                time.sleep(poll_delay)
            if struct.unpack_from("<I", result, 36)[0] != 0:
                raise ValueError(f"Page {row}: data is incomplete")
            return sram(address + size + status_bytes, PAGE_BYTES)

        def verify_reference(row, actual, expected):
            if actual == expected:
                return 0
            differences = [
                (offset, left, right)
                for offset, (left, right) in enumerate(zip(actual, expected))
                if left != right
            ]
            changed_bits = sum(
                (left ^ right).bit_count() for _, left, right in differences
            )
            details = ", ".join(
                f"{offset:#x}={left:02x}/{right:02x}"
                for offset, left, right in differences[:8]
            )
            if changed_bits > 3:
                raise ValueError(
                    f"Page {row}: {changed_bits} bit differences across "
                    f"{len(differences)} bytes vs reference ({details})"
                )
            return changed_bits

        if probe:
            references = dict(reference_pages(all_pages=True))
            difference_bits = 0
            exact_pages = 0
            started = time.perf_counter()
            for row, expected in references.items():
                differences = verify_reference(row, read_page(row), expected)
                difference_bits += differences
                exact_pages += differences == 0
                if (row + 1) % 256 == 0:
                    print(f"Checked {row + 1}/{len(references)} pages", flush=True)
            elapsed = time.perf_counter() - started
            print(
                f"Exact pages including OOB: {exact_pages}/{len(references)}; differing bits: {difference_bits}"
            )
            print(
                f"Probe completed in {elapsed:.3f} s ({len(references) * DATA_BYTES / 1024 / elapsed:.1f} KiB/s)"
            )
            return

        references = dict(reference_pages())
        started = time.perf_counter()
        with out.open("xb") as output:
            for index, row in enumerate(rows, 1):
                page = read_page(row)
                if row in references:
                    differences = verify_reference(row, page, references[row])
                    if differences:
                        print(
                            f"Page {row}: {differences} raw bit differences vs reference",
                            flush=True,
                        )
                output.write(page)
                if index == 1 or index % 256 == 0 or index == len(rows):
                    print(
                        f"Pages {index}/{len(rows)}, last physical row {row}, {time.perf_counter() - started:.2f} s",
                        flush=True,
                    )
            output.flush()
            os.fsync(output.fileno())
        elapsed = time.perf_counter() - started
        print(f"Elapsed: {elapsed:.3f} s")
        print(f"NAND data: {len(rows) * DATA_BYTES / 2**20 / elapsed:.3f} MiB/s")
        print(
            f"USB data with OOB: {len(rows) * PAGE_BYTES / 2**20 / elapsed:.3f} MiB/s"
        )
        if rows == range(PAGE_COUNT):
            report_dump(out)
    finally:
        if h.value:
            Free(h)
        Close(f)


if __name__ == "__main__":
    main()
