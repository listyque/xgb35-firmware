"""Minimal ADB host over WinUSB for the RAM Linux gadget (VID 10D6, PID FE02, adfuwinusb.inf).

python adb_winusb.py shell [command]      interactive shell without a command
python adb_winusb.py pull <remote> <local>
python adb_winusb.py push <local> <remote>
python adb_winusb.py term                 type lines into the on-screen terminal (fbterm)
python adb_winusb.py selftest             offline protocol checks
"""
import ctypes as c
from ctypes import wintypes as w
import os
import queue
import struct
import sys
import threading
import time
import winreg

GUID = '{ff7b846f-b0a3-45c0-b0fc-d8122086996d}'
VIDPID = 'VID_10D6&PID_FE02'
CNXN, OPEN, OKAY, CLSE, WRTE, AUTH = (struct.unpack('<I', s)[0] for s in
                                      (b'CNXN', b'OPEN', b'OKAY', b'CLSE', b'WRTE', b'AUTH'))
MAXDATA = 4096


def message(cmd, arg0, arg1, data=b''):
    return struct.pack('<6I', cmd, arg0, arg1, len(data), sum(data) & 0xFFFFFFFF,
                       cmd ^ 0xFFFFFFFF), data


def parse_header(raw):
    cmd, arg0, arg1, size, check, magic = struct.unpack('<6I', raw)
    if magic != cmd ^ 0xFFFFFFFF or size > 0x100000:
        raise ValueError(f'bad ADB header {raw.hex()}')
    return cmd, arg0, arg1, size, check


def device_paths(vidpid=VIDPID):
    try:
        key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, rf'SYSTEM\CurrentControlSet\Enum\USB\{vidpid}')
    except OSError:
        return []
    names = []
    while True:
        try:
            names.append(winreg.EnumKey(key, len(names)))
        except OSError:
            return [rf'\\?\USB#{vidpid}#{n}#{GUID}' for n in names]


def present(vidpid=VIDPID):
    """Device paths that can be opened right now (registry also keeps unplugged instances)."""
    k = c.WinDLL('kernel32', use_last_error=True)
    k.CreateFileW.restype = w.HANDLE
    k.CreateFileW.argtypes = (w.LPCWSTR, c.c_uint32, c.c_uint32, c.c_void_p, c.c_uint32, c.c_uint32, c.c_void_p)
    found = []
    for path in device_paths(vidpid):
        h = k.CreateFileW(path, 0, 3, None, 3, 0, None)
        if h not in (None, c.c_void_p(-1).value):
            k.CloseHandle(h)
            found.append(path)
    return found


class PipeInfo(c.Structure):
    _fields_ = [('type', c.c_int), ('id', c.c_ubyte), ('size', c.c_ushort), ('interval', c.c_ubyte)]


class WinUsb:
    def __init__(self):
        p, u = c.c_void_p, c.c_uint32
        k = c.WinDLL('kernel32', use_last_error=True)
        self.lib = c.WinDLL('winusb', use_last_error=True)
        create = k.CreateFileW
        create.restype, create.argtypes = w.HANDLE, (w.LPCWSTR, u, u, p, u, u, p)
        self.close_handle = k.CloseHandle
        self.lib.WinUsb_Initialize.argtypes = (w.HANDLE, c.POINTER(p))
        self.lib.WinUsb_QueryInterfaceSettings.argtypes = (p, c.c_ubyte, c.c_char_p)
        self.lib.WinUsb_QueryPipe.argtypes = (p, c.c_ubyte, c.c_ubyte, c.POINTER(PipeInfo))
        self.lib.WinUsb_SetPipePolicy.argtypes = (p, c.c_ubyte, u, u, p)
        for name in ('WinUsb_WritePipe', 'WinUsb_ReadPipe'):
            getattr(self.lib, name).argtypes = (p, c.c_ubyte, p, u, c.POINTER(u), p)
        self.lib.WinUsb_Free.argtypes = (p,)
        self.file, self.handle = None, p()
        for path in device_paths():
            f = create(path, 0xC0000000, 0, None, 3, 0x40000000, None)
            if f not in (None, p(-1).value):
                self.file = f
                break
        if not self.file:
            raise OSError(f'No ADB gadget {VIDPID} found (is RAM Linux running with --adb?)')
        self.check(self.lib.WinUsb_Initialize(self.file, c.byref(self.handle)), 'WinUsb_Initialize')
        desc = c.create_string_buffer(9)
        self.check(self.lib.WinUsb_QueryInterfaceSettings(self.handle, 0, desc), 'query interface')
        if desc.raw[5:8] != b'\xff\x42\x01':
            raise OSError(f'interface is not ADB: {desc.raw.hex()}')
        self.ep_in = self.ep_out = None
        for i in range(desc.raw[4]):
            info = PipeInfo()
            self.check(self.lib.WinUsb_QueryPipe(self.handle, 0, i, c.byref(info)), 'query pipe')
            if info.type == 2:  # bulk
                if info.id & 0x80:
                    self.ep_in = info.id
                else:
                    self.ep_out = info.id
        assert self.ep_in and self.ep_out, 'missing bulk endpoints'

    @staticmethod
    def check(ok, what):
        if not ok:
            raise OSError(c.get_last_error(), what)

    def write(self, data):
        if not data:
            return
        n = c.c_uint32()
        buf = c.create_string_buffer(bytes(data), len(data))
        self.check(self.lib.WinUsb_WritePipe(self.handle, self.ep_out, buf, len(data), c.byref(n), None), 'USB OUT')
        if n.value != len(data):
            raise OSError('short USB OUT')

    def read(self, size):
        out = b''
        while len(out) < size:
            n = c.c_uint32()
            buf = c.create_string_buffer(size - len(out))
            self.check(self.lib.WinUsb_ReadPipe(self.handle, self.ep_in, buf, size - len(out), c.byref(n), None), 'USB IN')
            out += buf.raw[:n.value]
        return out

    def close(self):
        if self.handle.value:
            self.lib.WinUsb_Free(self.handle)
            self.handle = c.c_void_p()
        if self.file:
            self.close_handle(self.file)
            self.file = None


class Adb:
    def __init__(self, usb):
        self.usb, self.lock, self.streams, self.next_id = usb, threading.Lock(), {}, 1
        self.send(CNXN, 0x01000000, MAXDATA, b'host::\0')
        for _ in range(64):  # skip leftovers (e.g. CLSE) of a previous session still queued in the device
            cmd, arg0, arg1, data = self.recv()
            if cmd in (CNXN, AUTH):
                break
        if cmd == AUTH:
            raise OSError('adbd asks for AUTH; not supported by this client')
        if cmd != CNXN:
            raise OSError(f'unexpected first packet {cmd:08x}')
        self.maxdata = min(arg1, MAXDATA)
        self.banner = data.rstrip(b'\0').decode(errors='replace')
        threading.Thread(target=self.pump, daemon=True).start()

    def send(self, cmd, arg0, arg1, data=b''):
        header, data = message(cmd, arg0, arg1, data)
        with self.lock:
            self.usb.write(header)
            self.usb.write(data)

    def recv(self):
        cmd, arg0, arg1, size, check = parse_header(self.usb.read(24))
        data = self.usb.read(size) if size else b''
        if sum(data) & 0xFFFFFFFF != check:
            raise ValueError('ADB data checksum mismatch')
        return cmd, arg0, arg1, data

    def pump(self):
        try:
            while True:
                cmd, remote, local, data = self.recv()
                stream = self.streams.get(local)
                if stream:
                    stream.event(cmd, remote, data)
        except Exception as error:  # surface USB loss to every waiter
            for stream in list(self.streams.values()):
                stream.inbox.put((CLSE, 0, repr(error).encode()))

    def open(self, service):
        stream = Stream(self, self.next_id)
        self.next_id += 1
        self.streams[stream.local] = stream
        self.send(OPEN, stream.local, 0, service.encode() + b'\0')
        cmd, remote, _ = stream.inbox.get(timeout=10)
        if cmd != OKAY:
            raise OSError(f'device refused {service!r}')
        stream.remote = remote
        return stream


class Stream:
    def __init__(self, adb, local):
        self.adb, self.local, self.remote = adb, local, 0
        self.inbox, self.okays, self.closed, self.buffer = queue.Queue(), queue.Queue(), False, b''

    def event(self, cmd, remote, data):
        if cmd == WRTE:
            self.adb.send(OKAY, self.local, remote)
            self.inbox.put((cmd, remote, data))
        elif cmd == OKAY and self.remote:
            self.okays.put(True)
        else:
            if cmd == CLSE:
                self.okays.put(False)
            self.inbox.put((cmd, remote, data))

    def write(self, data):
        for i in range(0, len(data), self.adb.maxdata):
            self.adb.send(WRTE, self.local, self.remote, data[i:i+self.adb.maxdata])
            if not self.okays.get(timeout=30):
                raise OSError('stream closed by device')

    def read(self, timeout=None):
        """Next chunk, or None when the device closed the stream."""
        if self.closed:
            return None
        cmd, _, data = self.inbox.get(timeout=timeout)
        if cmd == WRTE:
            return data
        self.closed = True
        return None

    def read_exact(self, size):
        while len(self.buffer) < size:
            chunk = self.read(timeout=30)
            if chunk is None:
                raise OSError('stream closed early')
            self.buffer += chunk
        out, self.buffer = self.buffer[:size], self.buffer[size:]
        return out

    def close(self):
        if not self.closed:
            self.adb.send(CLSE, self.local, self.remote)
            self.closed = True


def run(command):
    """One-shot shell command for scripts; returns output bytes."""
    usb = WinUsb()
    try:
        stream, out = Adb(usb).open('shell:' + command), b''
        while (chunk := stream.read(timeout=60)) is not None:
            out += chunk
        return out
    finally:
        usb.close()


def shell(adb, command):
    stream = adb.open('shell:' + command)
    out = sys.stdout.buffer
    if command:
        while (chunk := stream.read()) is not None:
            out.write(chunk)
            out.flush()
        return
    # shortcut: line-buffered interactive mode (no raw keys / Ctrl+C forwarding), exit with Ctrl+Z Enter.
    def printer():
        while (chunk := stream.read()) is not None:
            out.write(chunk)
            out.flush()
        os._exit(0)
    threading.Thread(target=printer, daemon=True).start()
    for line in sys.stdin:
        stream.write(line.replace('\r\n', '\n').encode())
    stream.close()


def sync_request(stream, tag, payload):
    stream.write(tag + struct.pack('<I', len(payload)) + payload)


def pull(adb, remote, local):
    stream = adb.open('sync:')
    sync_request(stream, b'RECV', remote.encode())
    total, started = 0, time.time()
    with open(local, 'wb') as f:
        while True:
            tag, size = struct.unpack('<4sI', stream.read_exact(8))
            if tag == b'DONE':
                break
            data = stream.read_exact(size)
            if tag == b'FAIL':
                raise OSError(data.decode(errors='replace'))
            assert tag == b'DATA', tag
            f.write(data)
            total += size
    sync_request(stream, b'QUIT', b'')
    stream.close()
    print(f'{remote} -> {local}: {total} bytes in {time.time()-started:.1f} s')


def push(adb, local, remote, mode=0o755):
    data = open(local, 'rb').read()
    stream = adb.open('sync:')
    sync_request(stream, b'SEND', f'{remote},{mode}'.encode())
    for i in range(0, len(data), 0x10000):
        sync_request(stream, b'DATA', data[i:i+0x10000])
    stream.write(b'DONE' + struct.pack('<I', int(time.time())))
    tag, size = struct.unpack('<4sI', stream.read_exact(8))
    if tag != b'OKAY':
        raise OSError(stream.read_exact(size).decode(errors='replace'))
    sync_request(stream, b'QUIT', b'')
    stream.close()
    print(f'{local} -> {remote}: {len(data)} bytes')


def selftest():
    header, data = message(CNXN, 0x01000000, 4096, b'host::\0')
    assert header == bytes.fromhex('434e584e00000001001000000700000032020000bcb1a7b1'), header.hex()
    assert parse_header(header)[:4] == (CNXN, 0x01000000, 4096, 7)
    try:
        parse_header(header[:20] + b'\0\0\0\0')
    except ValueError:
        pass
    else:
        raise AssertionError('bad magic accepted')
    print('ADB protocol self-test passed')


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ('shell', 'pull', 'push', 'term', 'selftest'):
        print(__doc__)
        return 2
    if sys.argv[1] == 'selftest':
        return selftest()
    usb = WinUsb()
    try:
        adb = Adb(usb)
        print(f'[connected: {adb.banner}]', file=sys.stderr)
        if sys.argv[1] == 'shell':
            shell(adb, ' '.join(sys.argv[2:]))
        elif sys.argv[1] == 'term':  # type into the on-screen fbterm shell
            stream = adb.open('shell:stty -echo; exec cat > /tmp/fbterm.in')
            print('[typing into the device screen terminal; Ctrl+Z Enter to quit]', file=sys.stderr)
            for line in sys.stdin:
                stream.write(line.replace('\r\n', '\n').encode())
            stream.close()
        elif sys.argv[1] == 'pull':
            pull(adb, sys.argv[2], sys.argv[3])
        else:
            push(adb, sys.argv[2], sys.argv[3])
    finally:
        usb.close()


if __name__ == '__main__':
    sys.exit(main())
