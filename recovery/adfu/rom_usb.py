"""BootROM ADFU memory transport, restricted to SRAM uploads; no flash commands."""
import struct
from service_usb import Service


class ROM(Service):
    def __init__(self):
        super().__init__()
        try:
            self.packet(bytes((0xcc,0,0,0,0,0,0,18)),18,128)
            value=self.receive(18)
            self.status()
            if value!=bytes.fromhex('004341444655445202450000000000000000'):
                raise ValueError('Unexpected BootROM identity')
        except BaseException:
            self.close()
            raise

    def packet(self,cdb,size=0,direction=0):
        self.send(struct.pack('<4sIIBBB16s',b'USBC',0,size,direction,0,16,cdb))

    def read(self,address,size):
        if not 0<size<=65536:
            raise ValueError('Invalid read size')
        self.packet(struct.pack('<BIIB',5,address,size,128),size,128)
        value=self.receive(size)
        self.status()
        return value

    def write(self,address,data):
        if not 0xb4068800<=address<address+len(data)<=0xb406f800:
            raise ValueError('Upload outside staging SRAM')
        self.packet(struct.pack('<BII',5,address,len(data)),len(data))
        self.send(data)
        self.status()

    def execute(self,address):
        if address%4 or not 0xb4068800<=address<0xb406f800:
            raise ValueError('Execute outside staging SRAM')
        self.packet(struct.pack('<BI',0x10,address))
        self.status()
