import ctypes as c, os, struct, time
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
f=CFile(os.environ['ACTIONS_ADFU_PNP_PATH'],0xc0000000,3,None,3,0x40000000,None)
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
 address=0xB406A000
 size=512
 def upload(data):
  command(struct.pack('<BII',5,address,size),size)
  write(data)
  status('SRAM upload')
  time.sleep(0.15)
 def sram(at=address,nbytes=size):
  command(struct.pack('<BIIB',5,at,nbytes,0x80),nbytes,0x80)
  return read_data(nbytes,'SRAM read')



 root=Path(__file__).resolve().parent
 mb=(root/'xgb35-mbrec-recovered.bin').read_bytes()
 from hashlib import sha256
 assert sha256(mb).hexdigest()=='34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc'
 wrap,out,stage=0xB406A800,0xB406AC00,0xB406B000
 origin=0x2300
 native=bytearray(mb[origin:0x6300].ljust(0x4600,b'\0'))
 def words(buf,offset,*seq): struct.pack_into('<'+'I'*len(seq),buf,offset,*seq)
 def dest(offset): return stage+offset-origin
 def br(src,dst,cond=14,link=False):
  delta=dst-src-8
  assert delta%4==0 and abs(delta)<2**25
  return cond<<28|0x0a000000|(int(link)<<24)|((delta//4)&0xffffff)
 def load32(reg,val):
  lo=val&0xffff
  hi=val>>16
  return [0xe3000000|(reg<<12)|((lo&0xf000)<<4)|(lo&0xfff),
          0xe3400000|(reg<<12)|((hi&0xf000)<<4)|(hi&0xfff)]
 for off in range(0,0x6300-origin,4):
  value=struct.unpack_from('<I',native,off)[0]
  relative=value-0xb4068000
  if origin<=relative<0x6300: words(native,off,dest(relative))
 native[0x61e0-origin:0x6220-origin]=bytes(0x40)
 # Keep UART disabled during the isolated DDR test.
 words(native,0x2608-origin,0xe12fff1e)
 # Bounded local delay because the full MBREC timer setup is bypassed.
 words(native,0x23fc-origin,0xe1a00480,0xe2500001,0x8afffffd,0xe12fff1e)
 for i,(site,again,failed) in enumerate(((0x3d34,0x3d28,0x3ef4),(0x3d68,0x3d5c,0x3ef4),
        (0x3d88,0x3d7c,0x3ef4),(0x3da4,0x3d9c,0x3ef4),(0x3e58,0x3e50,0x3ef4),
        (0x4378,0x4370,0x4394),(0x43e0,0x43d8,0x43fc))):
  off=0x4200+i*64
  addr=stage+off
  words(native,site-origin,br(dest(site),addr,cond=0))
  words(native,off,0xe92d0003,*load32(0,out),0xe5901010,0xe2511001,0xe5801010,
        0x03a01000|(i+1),0x05801014,0xe8bd0003,
        br(addr+9*4,dest(again),cond=1),br(addr+10*4,dest(failed)))
 # Record the native PMIC write to register 0x14, and its return, unchanged.
 hook=stage+0x4400
 words(native,0x408c-origin,br(dest(0x408c),hook,link=True))
 words(native,0x4400,0xe92d4010,*load32(4,out),0xe5841024,
       br(hook+16,dest(0x2a68),link=True),0xe5840028,0xe8bd8010)
 # Record the native calibration outcome before its caller handles the result.
 cal_hook=stage+0x4440
 words(native,0x3db4-origin,br(dest(0x3db4),cal_hook))
 words(native,0x4440,0xe92d000e,*load32(1,out),0xe581003c,*load32(2,0xb0230000),
       0xe5923050,0xe5813040,0xe8bd000e,0xe3500000,br(cal_hook+40,dest(0x3db8)))
 assert len(native)==0x4600 and mb[0x30c00:0x30c04]==b'!AFI'
 frame=bytearray(1024)
 frame[0x200:0x290]=mb[0x30c00:0x30c90]
 seq=[]
 def emit(*ops): seq.extend(ops)
 def call(at): emit(br(wrap+len(seq)*4,dest(at),link=True))
 emit(0xe92d41f0,*load32(4,out),*load32(0,0x00080000),0xe5840010)
 emit(0xe3a00000,0xe5840014,0xe5840018)
 emit(0xe3e00000,0xe5840000,0xe5840020,0xe5840024,0xe5840028)
 emit(*load32(0,wrap+0x200))
 call(0x37fc)
 call(0x2e7c)
 emit(0xe5840020,0xe3500000)
 pmu_skip=len(seq)
 emit(0)
 emit(*load32(0,0xb0150000),0xe59020a0,0xe382200a,0xe58020a0,0xf57ff04f)
 emit(*load32(0,0xb0150000),0xe5902000,0xe5842030,0xe590206c,0xe5842034,0xe590201c,0xe5842038)
 call(0x387c)
 emit(0xe3a00000)
 call(0x3ffc)
 # Retain the native clock configuration for the independent DRAM check.
 emit(*load32(4,out),0xe5840000,0xe3a02001,0xe5842018,0xe3500000)
 skip=len(seq)
 emit(0)
 # Test two distinct addresses used by U-Boot, including its startup stack.
 emit(*load32(1,0x02000000),*load32(3,0x00dfe000))
 emit(0xee071f3e,0xee073f3e,0xf57ff04f,0xe5915000,0xe5936000)
 emit(*load32(7,0x44524431),*load32(8,0x1fa27be3))
 emit(0xe5817000,0xe5838000,0xf57ff04f,0xee071f3e,0xee073f3e,0xf57ff04f)
 emit(0xe5912000,0xe5842004,0xe5932000,0xe5842044)
 emit(0xe5815000,0xe5836000,0xf57ff04f,0xee071f3e,0xee073f3e,0xf57ff04f)
 seq[skip]=br(wrap+skip*4,wrap+len(seq)*4,cond=1)
 seq[pmu_skip]=br(wrap+pmu_skip*4,wrap+len(seq)*4,cond=1)
 emit(*load32(1,0xb0150000),0xe5912008,0xe5842008)
 emit(0xe3a00014)
 call(0x2a58)
 emit(0xe584002c,0xe3a02002,0xe584201c)
 emit(0xf57ff04f,0xee074f3a,0xe2841020,0xee071f3a,0xe2841040,0xee071f3a,0xf57ff04f)
 # Restore the original CPU/bus clock after the physical-memory checks for USB.
 emit(*load32(1,0xb0150000),0xe5942038,0xe581201c,0xe5942034,0xe581206c,0xe5942030,0xe5812000,0xf57ff04f,0xe8bd81f0)
 assert len(seq)*4<=0x200
 words(frame,0,*seq)

 def transfer(at,blob):
  command(struct.pack('<BII',5,at,len(blob)),len(blob))
  write(bytes(blob))
  status('DDR SRAM upload')
  if sram(at,len(blob))!=blob: raise ValueError(f'DDR SRAM mismatch at {at:08x}')
 for i in range(0,len(native),4096): transfer(stage+i,native[i:i+4096])
 transfer(out,bytes(512))
 transfer(wrap,frame)
 command(struct.pack('<BI',0x10,wrap))
 status('DDR init handoff')
 time.sleep(5)
 report=sram(out)[:72]
 (root/'adfu-x70-recovery-ddr-check.bin').write_bytes(report)
 result,mem,pll,_,budget,which,ddr_return,done,pmu,vdd,vddret,vddread,_,_,_,cal,reg50,stack_word=struct.unpack('<18I',report)
 print(f'PMU={pmu:08X} VDD_DDR={vdd:08X} VDD_RET={vddret:08X} VDD_READ={vddread:08X} DDR_RET={result:08X} DDR_WORD={mem:08X} DDR_STACK={stack_word:08X} DDRPLL={pll:08X} CAL={cal:08X} DDR_REG50={reg50:08X} GUARD={which} RETURNED={ddr_return} DONE={done}',flush=True)
 if cal!=0 or mem!=0x44524431 or stack_word!=0x1fa27be3: raise ValueError('DDR not yet verified')
finally:
 if h.value: Free(h)
 Close(f)
