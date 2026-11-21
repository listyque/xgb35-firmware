import ctypes as c
import os
import struct
import time
import sys
import json
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


dry_run = "--dry-run" in sys.argv
f = None if dry_run else CFile(os.environ["ACTIONS_ADFU_PNP_PATH"], 0xC0000000, 3, None, 3, 0x40000000, None)
if not dry_run:
    check(f not in (None, P(-1).value), "open ADFU")
h = P()
try:
    if not dry_run:
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

    from hashlib import sha256
    from build_autonomous_service import ROOT, DDR_REPORT
    folder = ROOT / ('autonomous-service-diagnostic' if '--diagnostic' in sys.argv else 'autonomous-service')
    manifest = json.loads((folder / "manifest.json").read_text())
    segments = []
    for item in manifest["segments"]:
        blob = (folder / item["file"]).read_bytes()
        assert len(blob) == item["bytes"] and sha256(blob).hexdigest() == item["sha256"]
        address = item["address"]
        assert 0xB4068800 <= address < address+len(blob) <= manifest['reserved_rom_stack'][0]
        segments.append((address,blob))
    if dry_run:
        print("Preflight passed: all segment hashes and SRAM addresses verified")
        sys.exit(0)
    path = folder / "ram-test.json"
    attach = '--attach' in sys.argv
    if attach:
        result = json.loads(path.read_text())
        assert result['manifest_sha256'] == sha256((folder/'manifest.json').read_bytes()).hexdigest()
        assert result['cold_ddr_pll'] == 0x0A and result['host_execution_commands'] == 1
    else:
        command(bytes((0xCC,0,0,0,0,0,0,18)),18,128)
        assert read_data(18,"IDENT") == bytes.fromhex("004341444655445202450000000000000000")
        command(struct.pack("<BIIB",5,0xB0150008,4,128),4,128)
        before_pll, = struct.unpack("<I",read_data(4,"cold DDR PLL"))
        assert before_pll in (0x0A, 0x13C), f"Expected DDR PLL 0xA or 0x13C, found {before_pll:#x}"
        result = {"cold_ddr_pll": before_pll, "host_execution_commands": 0,
                  "manifest_sha256": sha256((folder / "manifest.json").read_bytes()).hexdigest(),
                  "checks": []}
    def record(check):
        result["checks"].append(check)
        path.write_text(json.dumps(result,indent=2))
        print(check,flush=True)
    if not attach:
        for address,blob in segments:
            for offset in range(0,len(blob),1024):
                chunk = blob[offset:offset+1024]
                command(struct.pack("<BII",5,address+offset,len(chunk)),len(chunk))
                write(chunk)
                status("SRAM upload")
                time.sleep(0.025)
                command(struct.pack("<BIIB",5,address+offset,len(chunk),128),len(chunk),128)
                assert read_data(len(chunk),"SRAM verify") == chunk
            print(f"SRAM staged {address:08x}: {len(blob)} bytes",flush=True)
        command(struct.pack("<BI",0x10,manifest["entry"]))
        status("autonomous chain start")
        result["host_execution_commands"] = 1
        record({"stage":"single_handoff_accepted"})
        time.sleep(3)
    if manifest.get('probe_ddr') or manifest.get('stop_entry'):
        command(struct.pack('<BIIB',5,manifest['report_sram'],256,128),256,128)
        report = read_data(256,'autonomous DDR probe')
        (folder/'ddr-probe.bin').write_bytes(report)
        magic,phase,pmic,ddr = struct.unpack_from('<4I',report)
        assert struct.unpack_from('<I',report,0xf0)[0] == manifest['nonce']
        record({'magic':hex(magic),'phase':phase,'pmic':pmic,'ddr':ddr})
        assert (magic,phase,pmic,ddr) == (0x41555431,1 if manifest.get('stop_entry') else 3,0,0)
        sys.exit(0)
    def memory(address,size):
        command(struct.pack("<BBBBBII",0xCC,0x93,0,0,0,size,address),size,128)
        return read_data(size,"service memory read")
    def memory_write(address,data):
        assert 0x01000000 <= address < address+len(data) <= 0x02000000
        command(struct.pack("<BBBBBII",0xCC,0x13,0,0,0,len(data),address),len(data))
        write(data)
        status("service DDR write")
    parameters = memory(0xB4068800,64)
    changed = [i for i,(before,after) in enumerate(zip(segments[0][1][:64],parameters)) if before != after]
    assert set(changed) <= {0x2A,0x2B,0x2D}, f'Unexpected parameter changes: {changed}'
    assert memory(0xB4068C00,12) == bytes.fromhex('010000ea00f020e3008806b4')
    assert memory(0xB4069A30,4) == struct.pack("<I",0xE3A00000)
    report = memory(DDR_REPORT,64)
    (folder / "init-report.bin").write_bytes(report)
    magic,phase,pmic,ddr = struct.unpack_from("<4I",report)
    assert (magic,phase,pmic,ddr) == (0x41555431,3,0,0)
    record({"stage":"autonomous_DDR_and_USB", "init_report":report.hex(), "calibration_parameter_changes":changed,"new_host_process":attach})
    original = memory(0x01000000,4096)
    started = time.perf_counter()
    try:
        for iteration in range(64):
            sample = sha256(struct.pack("<I",iteration)).digest()*128
            memory_write(0x01000000,sample)
            assert memory(0x01000000,4096) == sample
    finally:
        memory_write(0x01000000,original)
    assert memory(0x01000000,4096) == original
    record({"rounds":64,"bytes_per_round":4096,"seconds":round(time.perf_counter()-started,3)})
    for _ in range(10):
        time.sleep(1)
        assert memory(0xB4068800,64) == parameters
    record({"idle_checks":10,"USB_responds":True})
finally:
    if h.value: Free(h)
    if f: Close(f)
