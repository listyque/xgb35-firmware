# Run only with one ATM7051H XGB35 attached in ADFU.
# The payload reads row zero, records completion in SRAM and restores controller setup.
import ctypes as c
import re
import struct
import subprocess
import time
from ctypes import wintypes as w
from pathlib import Path
P,U,B=c.c_void_p,c.c_uint32,c.c_ubyte
def bind(lib,name,result,*args):
 f=getattr(lib,name)
 f.restype,f.argtypes=result,args
 return f
usb=c.WinDLL('winusb',use_last_error=True)
krn=c.WinDLL('kernel32',use_last_error=True)
CFile=bind(krn,'CreateFileW',w.HANDLE,w.LPCWSTR,U,U,P,U,U,P)
Close=bind(krn,'CloseHandle',w.BOOL,w.HANDLE)
Init=bind(usb,'WinUsb_Initialize',w.BOOL,w.HANDLE,c.POINTER(P))
Free=bind(usb,'WinUsb_Free',w.BOOL,P)
Policy=bind(usb,'WinUsb_SetPipePolicy',w.BOOL,P,B,U,U,P)
Write=bind(usb,'WinUsb_WritePipe',w.BOOL,P,B,P,U,c.POINTER(U),P)
Read=bind(usb,'WinUsb_ReadPipe',w.BOOL,P,B,P,U,c.POINTER(U),P)
def check(ok,label):
 if not ok: raise OSError(c.get_last_error(),label)
listing = subprocess.check_output(
    ['pnputil', '/enum-interfaces', '/enabled', '/class',
     '{FF7B846F-B0A3-45C0-B0FC-D8122086996D}']
)
paths = re.findall(rb'\\\\\?\\USB#VID_10D6&PID_10D6#[^\s]+', listing, re.IGNORECASE)
if len(paths) != 1:
    raise RuntimeError(f'Expected one active Actions interface, found {len(paths)}')
f=CFile(paths[0].decode('ascii'),0xc0000000,3,None,3,0x40000000,None)
check(f not in (None,P(-1).value),'open ADFU')
h=P()
try:
 check(Init(f,c.byref(h)),'start WinUSB')
 ms=U(2400)
 for ep in (1,0x82): check(Policy(h,ep,3,4,c.byref(ms)),'set timeout')
 def write(data):
  n=U()
  check(Write(h,1,data,len(data),c.byref(n),None),'write USB packet')
  if n.value!=len(data): raise ValueError('short OUT')
 def read(size,label):
  buf=c.create_string_buffer(size)
  n=U()
  check(Read(h,0x82,buf,size,c.byref(n),None),label)
  return buf.raw[:n.value]
 def command(cdb,size=0,direction=0):
  write(struct.pack('<4sIIBBB16s',b'USBC',0,size,direction,0,16,cdb))
 def status(label,data=None):
  s=read(13,label) if data is None else data
  if len(s)!=13 or s[:4]!=b'USBS': raise ValueError(f'{label}: invalid status {s.hex()}')
  _,tag,res,code=struct.unpack('<4sIIB',s)
  if label=='IDENT': print(f'{label}: tag={tag} residue={res} status={code}',flush=True)
  if (tag,res,code)!=(0,0,0): raise ValueError(f'{label}: refused')
 def read_data(size,label):
  data=read(size,label)
  if len(data)==13 and data.startswith(b'USBS'): status(label,data)
  if len(data)!=size: raise ValueError(f'{label}: got {len(data)} bytes, expected {size}')
  status(label)
  return data
 command(bytes((0xcc,0,0,0,0,0,0,18)),18,0x80)
 if read_data(18,'IDENT')!=bytes.fromhex('004341444655445202450000000000000000'): raise ValueError('unexpected target')

 root=Path(__file__).resolve().parent
 rom=(root/'atm7051h-high-rom-ffff0000-ffff7fff.bin').read_bytes()
 base=0xb406a000
 trace=0xb406bc00
 rom_init=base+0x400
 rom_read=base+0x800
 def wordbuf(words): return struct.pack('<'+'I'*len(words),*words)
 def l32(reg,value):
  lo,hi=value&0xffff,value>>16
  return [0xe3000000|(reg<<12)|((lo&0xf000)<<4)|(lo&0xfff),
          0xe3400000|(reg<<12)|((hi&0xf000)<<4)|(hi&0xfff)]
 def call(addr):
  return [*l32(12,addr),0xe12fff3c]
 def ld(rd,rn,off): return 0xe5900000|(rn<<16)|(rd<<12)|off
 def st(rd,rn,off): return 0xe5800000|(rn<<16)|(rd<<12)|off
 def cache(addr,op,nlines):
  return [*l32(0,addr),0xe3a01000|nlines,op,0xe2800020,0xe2511001,0x1afffffb,0xf57ff04f]
 payload=bytearray(0xc00)
 def put(offset,words):
  data=wordbuf(words)
  assert offset+len(data)<=len(payload)
  payload[offset:offset+len(data)]=data
 # Preserve USB PLL selection. Pin mux, NAND clock and NAND reset follow ROM.
 payload[0x400:0x400+0x29c]=rom[0x2d88:0x3024]
 assert struct.unpack_from('<I',rom,0x2db8)[0]==0xe5874000
 put(0x400+0x30,[0xe1a00000])
 payload[0x800:0x800+0x2ac]=rom[0x3598:0x3844]
 hooks=0xb00
 # Report how each ROM wait terminated and sample controller state before cleanup.
 for i,(literal,target) in enumerate(((0x3824,0xffff337c),(0x3828,0xffff3224),(0x3838,0xffff31d4),(0x383c,0xffff32cc))):
  off=hooks+i*0x40
  assert struct.unpack_from('<I',rom,literal)[0]==target
  put(0x800+literal-0x3598,[base+off])
  code=[0xe92d4010,*call(target),st(0,9,i*20),*l32(2,0xb0320000)]
  for j,reg in enumerate((4,0x38,0x3c,0x68),1):
   code.extend((ld(3,2,reg),st(3,9,i*20+4*j)))
  code.append(0xe8bd8010)
  assert len(code)==16
  put(off,code)
 main=[0xe92d4ff0,*l32(9,trace),*l32(0,0x524f4d31),st(0,9,88),
       0xe3a00001,*call(rom_init),
       0xe3a00002,*call(0xffff2ccc),
       *call(0xffff2d1c),st(0,9,84),
       *l32(4,0xb4068400),0xe3a00000,st(0,4,0),st(0,4,4),
       *l32(0,0x20000002),st(0,4,0x44),*l32(5,0xb0320000),
       ld(6,5,8),0xe3c66003]
 for i,profile in enumerate((0,1,3)):
  dst=0xb406b000+i*1024
  main.extend([*l32(9,trace+i*0x80),
               *cache(dst,0xee070f3e,32),0xe3860000|profile,st(0,5,8),
               *l32(0,dst),st(0,4,8),
               0xe1a00004,*call(rom_read),
               *cache(dst,0xee070f36,32),0xe3a00001,st(0,9,80)])
 main.extend([*call(0xffff2cfc),*call(0xffff2ee8),
              *l32(9,trace),*l32(0,0x524f4d32),st(0,9,88),
              *cache(trace,0xee070f3a,12),0xe8bd8ff0])
 assert len(main)*4<=0x400
 put(0,main)
 def sram(addr,size):
  command(struct.pack('<BIIB',5,addr,size,128),size,128)
  return read_data(size,'SRAM read')
 def upload(addr,data):
  command(struct.pack('<BII',5,addr,len(data)),len(data))
  write(data); status('SRAM upload')
  if sram(addr,len(data))!=data: raise ValueError(f'SRAM verify {addr:08x}')
 print(f'Uploading {len(payload)} bytes of instrumentation to SRAM',flush=True)
 for off in range(0,len(payload),512):
  upload(base+off,bytes(payload[off:off+512]))
 for off in range(0,0xc00,512):
  upload(0xb406b000+off,b'\xA5'*512)
 upload(trace,b'\xff'*384)
 command(struct.pack('<BI',0x10,base)); status('start SRAM diagnostic')
 print('Got SRAM handoff status',flush=True)
 time.sleep(.5)
 result=sram(trace,384)
 print('ready marker',hex(struct.unpack_from('<I',result,88)[0]),'NAND reset result',struct.unpack_from('<I',result,84)[0],flush=True)
 out=bytearray(result)
 expected=(root/'xgb35-mbrec-recovered.bin').read_bytes()[:512]
 for i,profile in enumerate((0,1,3)):
  print('PROFILE',profile,'done:',struct.unpack_from('<I',result,i*128+80)[0])
  for j,label in enumerate(('ready_busy','read_command','dma','transfer')):
   vals=struct.unpack_from('<5I',result,i*128+j*20)
   print(label,'return',vals[0],'status/xfer/ecc_or_status/dma',*[f'{v:08x}' for v in vals[1:]])
  sector=sram(0xb406b000+i*1024,512)
  out.extend(sector)
  p=struct.unpack('<128I',sector)
  sig=(p[125],p[126])==(0x42746341,0x55aa6d72)
  checksum=(sum(p[:127])+0x1234)&0xffffffff
  print('sentinel',sector==b'\xA5'*512,'match expected',sum(x==y for x,y in zip(sector,expected)),
        f'tags={p[125]:08x}/{p[126]:08x} sum={p[127]:08x}/{checksum:08x}', 'valid',sig and checksum==p[127],flush=True)
 (root/'adfu-bootrom-page0-trace.bin').write_bytes(out)

finally:
 if h.value: Free(h)
 Close(f)
