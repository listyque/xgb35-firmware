import ctypes as c
import os
import struct
import time
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

    from hashlib import sha256
    from zipfile import ZipFile

    root = Path(__file__).resolve().parent
    mb = (root / "xgb35-mbrec-recovered.bin").read_bytes()
    assert (
        sha256(mb).hexdigest()
        == "34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc"
    )
    parameters = mb[0x30C00:0x31000]
    assert parameters.startswith(b"!AFI")
    with ZipFile(root / "x70-parts.zip") as z:
        decoder = z.read("adfudec.bin")
    assert len(decoder) == 16848 and decoder[:12] == bytes.fromhex(
        "010000ea00f020e3008806b4"
    )
    assert bytes.fromhex("000032b0") not in decoder
    entry = 0xB4068C00

    def upload(address, blob):
        command(struct.pack("<BII", 5, address, len(blob)), len(blob))
        write(blob)
        status("SRAM upload")
        command(struct.pack("<BIIB", 5, address, len(blob), 128), len(blob), 128)
        if read_data(len(blob), "SRAM readback") != blob:
            raise ValueError(f"SRAM readback failed at {address:08x}")

    upload(0xB4068800, parameters)
    decoder = decoder.ljust((len(decoder) + 511) & ~511, bytes(1))
    source = 0xB406B000
    launcher = 0xB406F400
    assert source + len(decoder) <= launcher

    def mov32(reg, value):
        lo, hi = value & 0xFFFF, value >> 16
        return [
            0xE3000000 | (reg << 12) | ((lo & 0xF000) << 4) | (lo & 0xFFF),
            0xE3400000 | (reg << 12) | ((hi & 0xF000) << 4) | (hi & 0xFFF),
        ]

    # Finish host I/O before moving the vendor stage into BootROM workspace.
    words = [
        0xF10C00C0,
        *mov32(0, source),
        *mov32(1, entry),
        *mov32(2, len(decoder) // 4),
        0xE4903004,
        0xE4813004,
        0xE2522001,
        0x1AFFFFFB,
        *mov32(0, entry),
        *mov32(1, len(decoder) // 32),
        0xEE070F3A,
        0xE2800020,
        0xE2511001,
        0x1AFFFFFB,
        0xF57FF04F,
        0xEE070F15,
        0xF57FF04F,
        0xF57FF06F,
        *mov32(12, entry),
        0xE12FFF1C,
    ]
    stage = struct.pack("<" + "I" * len(words), *words).ljust(512, bytes(1))
    for off in range(0, len(decoder), 2048):
        addr = source + off
        print(f"Staging vendor loader at {addr:08x}", flush=True)
        upload(addr, decoder[off : off + 2048])
    upload(launcher, stage)
    command(struct.pack("<BI", 0x10, launcher))
    status("stock ADFU DDR stage handoff")
    print("X70 ADFUDEC staged and started from SRAM", flush=True)
    time.sleep(3)

    def cdb(sub, address, size):
        return struct.pack("<BBBBBII", 0xCC, sub, 0, 0, 0, size, address)

    def stage_read(address, size):
        command(cdb(0x93, address, size), size, 128)
        return read_data(size, "ADFUDEC memory read")

    def stage_write(address, data):
        command(cdb(0x13, address, len(data)), len(data))
        write(data)
        status("ADFUDEC memory write")

    if stage_read(0xB4068800, 64) != parameters[:64]:
        raise ValueError("Could not verify the stock second-stage ADFU transport")
    print("Stock ADFUDEC transport verified", flush=True)
    spots = (0x01000000, 0x01800000, 0x01E00000)
    patterns = {addr: sha256(struct.pack("<I", addr)).digest() * 2 for addr in spots}
    for addr in spots:
        print(f"Checking short DDR read at {addr:08x}", flush=True)
        stage_read(addr, 64)
    for addr, data in patterns.items():
        stage_write(addr, data)
    for addr, data in patterns.items():
        if stage_read(addr, len(data)) != data:
            raise ValueError(f"DDR check failed at {addr:08x}")
    print(
        "Verified three independent DDR addresses used by the RAM recovery", flush=True
    )
    sample = bytes((i * 37 + 73) & 255 for i in range(4092))
    stage_write(spots[0], sample)
    if stage_read(spots[0], len(sample)) != sample:
        raise ValueError("Larger DDR block failed")
    print("Verified 4092-byte RAM block", flush=True)
finally:
    if h.value:
        Free(h)
    Close(f)
