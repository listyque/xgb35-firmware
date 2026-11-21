import ctypes as c, os, struct, time
from ctypes import wintypes as w
from pathlib import Path

P, U, B = c.c_void_p, c.c_uint32, c.c_ubyte


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


f = CFile(os.environ["ACTIONS_ADFU_PNP_PATH"], 0xC0000000, 3, None, 3, 0x40000000, None)
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
    if read_data(18, "IDENT") != bytes.fromhex("004341444655445202450000000000000000"):
        raise ValueError("unexpected target")
    address = 0xB406A000
    size = 512

    def upload(data):
        command(struct.pack("<BII", 5, address, size), size)
        write(data)
        status("SRAM upload")
        time.sleep(0.15)

    def sram(at=address, nbytes=size):
        command(struct.pack("<BIIB", 5, at, nbytes, 0x80), nbytes, 0x80)
        return read_data(nbytes, "SRAM read")

    root = Path(__file__).resolve().parent
    mb = (root / "xgb35-mbrec-recovered.bin").read_bytes()
    from hashlib import sha256

    assert (
        sha256(mb).hexdigest()
        == "34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc"
    )
    wrap, out, stage = 0xB406A800, 0xB406AC00, 0xB406B000
    origin = 0x2300
    native = bytearray(mb[origin:0x6300].ljust(0x4600, b"\0"))

    def words(buf, offset, *seq):
        struct.pack_into("<" + "I" * len(seq), buf, offset, *seq)

    def dest(offset):
        return stage + offset - origin

    def br(src, dst, cond=14, link=False):
        delta = dst - src - 8
        assert delta % 4 == 0 and abs(delta) < 2**25
        return cond << 28 | 0x0A000000 | (int(link) << 24) | ((delta // 4) & 0xFFFFFF)

    def load32(reg, val):
        lo = val & 0xFFFF
        hi = val >> 16
        return [
            0xE3000000 | (reg << 12) | ((lo & 0xF000) << 4) | (lo & 0xFFF),
            0xE3400000 | (reg << 12) | ((hi & 0xF000) << 4) | (hi & 0xFFF),
        ]

    for off in range(0, 0x6300 - origin, 4):
        value = struct.unpack_from("<I", native, off)[0]
        relative = value - 0xB4068000
        if origin <= relative < 0x6300:
            words(native, off, dest(relative))
    native[0x61E0 - origin : 0x6220 - origin] = bytes(0x40)
    # Keep UART disabled during the isolated DDR test.
    words(native, 0x2608 - origin, 0xE12FFF1E)
    for i, (site, again, failed) in enumerate(
        (
            (0x3D34, 0x3D28, 0x3EF4),
            (0x3D68, 0x3D5C, 0x3EF4),
            (0x3D88, 0x3D7C, 0x3EF4),
            (0x3DA4, 0x3D9C, 0x3EF4),
            (0x3E58, 0x3E50, 0x3EF4),
            (0x4378, 0x4370, 0x4394),
            (0x43E0, 0x43D8, 0x43FC),
        )
    ):
        off = 0x4200 + i * 64
        addr = stage + off
        words(native, site - origin, br(dest(site), addr, cond=0))
        words(
            native,
            off,
            0xE92D0003,
            *load32(0, out),
            0xE5901010,
            0xE2511001,
            0xE5801010,
            0x03A01000 | (i + 1),
            0x05801014,
            0xE8BD0003,
            br(addr + 9 * 4, dest(again), cond=1),
            br(addr + 10 * 4, dest(failed)),
        )
    # Record the native PMIC write to register 0x14, and its return, unchanged.
    hook = stage + 0x4400
    words(native, 0x408C - origin, br(dest(0x408C), hook, link=True))
    words(
        native,
        0x4400,
        0xE92D4010,
        *load32(4, out),
        0xE5841024,
        br(hook + 16, dest(0x2A68), link=True),
        0xE5840028,
        0xE8BD8010,
    )
    # Record the native calibration outcome before its caller handles the result.
    cal_hook = stage + 0x4440
    words(native, 0x3DB4 - origin, br(dest(0x3DB4), cal_hook))
    words(
        native,
        0x4440,
        0xE92D000E,
        *load32(1, out),
        0xE581003C,
        *load32(2, 0xB0230000),
        0xE5923050,
        0xE5813040,
        0xE8BD000E,
        0xE3500000,
        br(cal_hook + 40, dest(0x3DB8)),
    )
    assert len(native) == 0x4600 and mb[0x30C00:0x30C04] == b"!AFI"
    frame = bytearray(1024)
    frame[0x2C0:0x350] = mb[0x30C00:0x30C90]
    seq = []

    def emit(*ops):
        seq.extend(ops)

    def call(at):
        emit(br(wrap + len(seq) * 4, dest(at), link=True))

    emit(0xE92D41F0, *load32(4, out), *load32(0, 0x00080000), 0xE5840010)
    emit(0xE3A00000, 0xE5840014, 0xE5840018)
    emit(0xE3E00000, 0xE5840000, 0xE5840020, 0xE5840024, 0xE5840028)
    # Use MBREC's actual hardware timer and confirm it counts before training DDR.
    emit(0xE3A00000)
    call(0x2438)
    emit(*load32(5, 0xB0158010), 0xE5956000, 0xE5846048, *load32(7, 0x10000))
    timer_poll = len(seq)
    emit(0xE5950000, 0xE1500006)
    timer_ready = len(seq)
    emit(0, 0xE2577001)
    emit(br(wrap + len(seq) * 4, wrap + timer_poll * 4, cond=1))
    timer_failed = len(seq)
    emit(0)
    seq[timer_ready] = br(wrap + timer_ready * 4, wrap + len(seq) * 4, cond=1)
    emit(0xE584004C)
    emit(*load32(0, wrap + 0x2C0))
    call(0x37FC)
    call(0x2E7C)
    emit(0xE5840020, 0xE3500000)
    pmu_skip = len(seq)
    emit(0)
    emit(*load32(0, 0xB0150000), 0xE59020A0, 0xE382200A, 0xE58020A0, 0xF57FF04F)
    emit(
        *load32(0, 0xB0150000),
        0xE5902000,
        0xE5842030,
        0xE590206C,
        0xE5842034,
        0xE590201C,
        0xE5842038,
    )
    call(0x387C)
    emit(0xE3A00000)
    call(0x3FFC)
    # Retain the native clock configuration for the independent DRAM check.
    emit(*load32(4, out), 0xE5840000, 0xE3A02001, 0xE5842018, 0xE3500000)
    skip = len(seq)
    emit(0)
    # Test two distinct addresses used by U-Boot, including its startup stack.
    emit(*load32(1, 0x02000000), *load32(3, 0x00DFE000))
    emit(0xEE071F3E, 0xEE073F3E, 0xF57FF04F, 0xE5915000, 0xE5936000)
    emit(*load32(7, 0x44524431), *load32(8, 0x1FA27BE3))
    emit(0xE5817000, 0xE5838000, 0xF57FF04F, 0xEE071F3E, 0xEE073F3E, 0xF57FF04F)
    emit(0xE5912000, 0xE5842004, 0xE5932000, 0xE5842044)
    emit(0xE5815000, 0xE5836000, 0xF57FF04F, 0xEE071F3E, 0xEE073F3E, 0xF57FF04F)
    seq[skip] = br(wrap + skip * 4, wrap + len(seq) * 4, cond=1)
    seq[pmu_skip] = br(wrap + pmu_skip * 4, wrap + len(seq) * 4, cond=1)
    emit(*load32(1, 0xB0150000), 0xE5912008, 0xE5842008)
    emit(0xE3A00014)
    call(0x2A58)
    emit(0xE584002C, 0xE3A02002, 0xE584201C)
    emit(
        0xF57FF04F,
        0xEE074F3A,
        0xE2841020,
        0xEE071F3A,
        0xE2841040,
        0xEE071F3A,
        0xF57FF04F,
    )
    # Restore the original CPU/bus clock after the physical-memory checks for USB.
    emit(
        *load32(1, 0xB0150000),
        0xE5942038,
        0xE581201C,
        0xE5942034,
        0xE581206C,
        0xE5942030,
        0xE5812000,
        0xF57FF04F,
        0xE8BD81F0,
    )
    seq[timer_failed] = br(wrap + timer_failed * 4, wrap + len(seq) * 4)
    emit(0xE2841040, 0xEE071F3A, 0xF57FF04F, 0xE8BD81F0)
    assert len(seq) * 4 <= 0x2C0
    words(frame, 0, *seq)

    def transfer(at, blob):
        command(struct.pack("<BII", 5, at, len(blob)), len(blob))
        write(bytes(blob))
        status("DDR SRAM upload")
        if sram(at, len(blob)) != blob:
            raise ValueError(f"DDR SRAM mismatch at {at:08x}")

    for i in range(0, len(native), 4096):
        transfer(stage + i, native[i : i + 4096])
    transfer(out, bytes(512))
    transfer(wrap, frame)
    command(struct.pack("<BI", 0x10, wrap))
    status("DDR init handoff")
    time.sleep(5)
    report = sram(out)[:80]
    (root / "adfu-x70-ddr-hardware-timer.bin").write_bytes(report)
    (
        result,
        mem,
        pll,
        _,
        budget,
        which,
        ddr_return,
        done,
        pmu,
        vdd,
        vddret,
        vddread,
        _,
        _,
        _,
        cal,
        reg50,
        stack_word,
        timer_before,
        timer_after,
    ) = struct.unpack("<20I", report)
    print(
        f"PMU={pmu:08X} VDD_DDR={vdd:08X} VDD_RET={vddret:08X} VDD_READ={vddread:08X} DDR_RET={result:08X} DDR_WORD={mem:08X} DDR_STACK={stack_word:08X} DDRPLL={pll:08X} CAL={cal:08X} DDR_REG50={reg50:08X} GUARD={which} RETURNED={ddr_return} DONE={done} TIMER={timer_before:08X}->{timer_after:08X}",
        flush=True,
    )
    if cal != 0 or mem != 0x44524431 or stack_word != 0x1FA27BE3:
        raise ValueError("DDR not yet verified")
finally:
    if h.value:
        Free(h)
    Close(f)
