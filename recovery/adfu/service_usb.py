"""Host transport for the resident X70 RAM service. No flash commands."""
import ctypes as c
import os
import struct
from ctypes import wintypes as w


def bind(lib, name, result, *args):
    f = getattr(lib,name)
    f.restype,f.argtypes = result,args
    return f


class Service:
    def __init__(self, *, exclusive=False):
        p,u,b = c.c_void_p,c.c_uint32,c.c_ubyte
        usb = c.WinDLL('winusb',use_last_error=True)
        kernel = c.WinDLL('kernel32',use_last_error=True)
        create = bind(kernel,'CreateFileW',w.HANDLE,w.LPCWSTR,u,u,p,u,u,p)
        self.close_file = bind(kernel,'CloseHandle',w.BOOL,w.HANDLE)
        self.free = bind(usb,'WinUsb_Free',w.BOOL,p)
        init = bind(usb,'WinUsb_Initialize',w.BOOL,w.HANDLE,c.POINTER(p))
        policy = bind(usb,'WinUsb_SetPipePolicy',w.BOOL,p,b,u,u,p)
        self.write_pipe = bind(usb,'WinUsb_WritePipe',w.BOOL,p,b,p,u,c.POINTER(u),p)
        self.read_pipe = bind(usb,'WinUsb_ReadPipe',w.BOOL,p,b,p,u,c.POINTER(u),p)
        self.file = create(os.environ['ACTIONS_ADFU_PNP_PATH'],0xc0000000,0 if exclusive else 3,None,3,0x40000000,None)
        self.handle = p()
        self.check(self.file not in (None,p(-1).value),'open RAM service')
        try:
            self.check(init(self.file,c.byref(self.handle)),'initialize WinUSB')
            timeout=u(2400)
            for ep in (1,0x82):
                self.check(policy(self.handle,ep,3,4,c.byref(timeout)),'set timeout')
        except BaseException:
            self.close()
            raise

    @staticmethod
    def check(ok, label):
        if not ok:
            raise OSError(c.get_last_error(),label)

    def close(self):
        if self.handle.value:
            self.free(self.handle)
            self.handle = c.c_void_p()
        if self.file:
            self.close_file(self.file)
            self.file = None

    def __enter__(self):
        return self

    def __exit__(self,*args):
        self.close()

    def send(self,data):
        count=c.c_uint32()
        self.check(self.write_pipe(self.handle,1,data,len(data),c.byref(count),None),'USB OUT')
        if count.value != len(data):
            raise ValueError('short USB OUT')

    def receive(self,size):
        count=c.c_uint32()
        buf=c.create_string_buffer(size)
        self.check(self.read_pipe(self.handle,0x82,buf,size,c.byref(count),None),'USB IN')
        if count.value != size:
            raise ValueError(f'short USB IN: {count.value}/{size}')
        return buf.raw

    def command(self,op,address,size=0,read=False):
        if op not in (0x13,0x93,0x20):
            raise ValueError('unsupported operation')
        cdb=struct.pack('<BBBBBII',0xcc,op,0,0,0,size,address)
        self.send(struct.pack('<4sIIBBB16s',b'USBC',0,size,128 if read else 0,0,16,cdb))

    def status(self):
        value=struct.unpack('<4sIIB',self.receive(13))
        if value != (b'USBS',0,0,0):
            raise ValueError(f'USB status: {value}')

    def read(self,address,size):
        if size <= 0 or size > 65536:
            raise ValueError('invalid read size')
        self.command(0x93,address,size,True)
        value=self.receive(size)
        self.status()
        return value

    @staticmethod
    def writable(address,size):
        return size > 0 and any(start <= address < address+size <= end for start,end in
                                ((0xb406d000,0xb406f400),(0x01000000,0x02000000)))

    def write(self,address,data):
        if not self.writable(address,len(data)):
            raise ValueError('write outside diagnostic RAM')
        self.command(0x13,address,len(data))
        self.send(data)
        self.status()

    def execute(self,address):
        if address % 4 or not self.writable(address,4):
            raise ValueError('execute outside diagnostic RAM')
        self.command(0x20,address)
        self.status()


if __name__ == '__main__':
    assert Service.writable(0xb406d000,512)
    assert not Service.writable(0xb0320000,4)
    assert not Service.writable(0xb4068c00,4)
    assert not Service.writable(0x01fffffc,8)
    print('RAM bounds checks passed')
