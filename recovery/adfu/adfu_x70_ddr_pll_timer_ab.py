import ctypes as c, os, secrets, struct, sys, time
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
f=None if '--dry-run' in sys.argv else CFile(os.environ['ACTIONS_ADFU_PNP_PATH'],0xc0000000,3,None,3,0x40000000,None)
if '--dry-run' not in sys.argv: check(f not in (None,P(-1).value),'open ADFU')
h=P()
try:
 if f:
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



 base=0xb406c000
 out=0xb406a200
 nonce=secrets.randbits(32)
 def mov32(reg,val):
  return (0xe3000000|reg<<12|(val&0xf000)<<4|(val&0xfff),
          0xe3400000|reg<<12|((val>>16)&0xf000)<<4|((val>>16)&0xfff))
 code=[]
 labels={}
 jumps=[]
 def emit(*ops): code.extend(ops)
 def label(name): labels[name]=len(code)
 def jump(name,condition=14,link=False):
  jumps.append((len(code),name,condition,link))
  emit(0)
 emit(0xe92d4ff0,*mov32(4,out),*mov32(5,0xb0150000),*mov32(6,0xb0158000),
      0xe5957008,0xe59580a4,0xe5969008,0xe596a00c,0xe596b010)
 emit(0xe3e00000,0xe5840018,0xe5840038)
 emit(*mov32(0,nonce),0xe5840000,0xe3a00001,0xe5840004,0xe5847008,
      0xee074f3a,0xf57ff04f)
 # Match MBREC timer 0 setup, preserving the incoming clock gate and timer registers.
 emit(*mov32(3,0x08000000),0xe1883003,0xe58530a4,0xe3a03000,0xe5863008,
      0xe586300c,0xe5863010,0xe3a03004,0xe5863008,0xf57ff04f)
 emit(0xe2841010)
 jump('measure',link=True)
 emit(*mov32(0,0x13c),0xe5850008,0xf57ff04f,0xe5950008,0xe5840020,0xe2841030)
 jump('measure',link=True)
 emit(0xe5857008,0xf57ff04f,0xe5950008,0xe584000c)
 emit(0xe3a03000,0xe5863008,0xe586a00c,0xe586b010,0xe5869008,0xe58580a4,0xf57ff04f)
 emit(0xe3a00002,0xe5840004,0xee074f3a,0xe2841020,0xee071f3a,
      0xe2841040,0xee071f3a,0xf57ff04f,0xe8bd8ff0)
 label('measure')
 emit(0xe5960010,0xe5810000,0xe5962008,0xe581200c,0xee071f3a,0xf57ff04f)
 emit(*mov32(3,0x00100000))
 label('sample')
 emit(0xe596c010,0xe04c2000,0xe3520906) # 4096 * 24 ticks = 0x18000
 jump('reached',10)
 emit(0xe2533001)
 jump('sample',1)
 jump('publish')
 label('reached')
 emit(0xe3a03001)
 label('publish')
 emit(0xe581c004,0xe5813008,0xee071f3a,0xf57ff04f,0xe12fff1e)
 for i,name,cond,link in jumps:
  code[i]=cond<<28|0x0a000000|int(link)<<24|((labels[name]-i-2)&0xffffff)
 payload=struct.pack('<'+'I'*len(code),*code).ljust(512,b'\0')
 assert len(payload)==512
 root=Path(__file__).resolve().parent
 if '--dry-run' in sys.argv:
  sys.path.insert(0,str(root/'python-analysis'))
  from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
  ins=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(payload[:len(code)*4],base))
  assert ins[labels['sample']+2].op_str=='r2, #0x18000'
  print('ARM compare:',ins[labels['sample']+2].op_str,'instructions',len(ins))
  sys.exit()
 command(bytes((0xcc,0,0,0,0,0,0,18)),18,0x80)
 if read_data(18,'IDENT')!=bytes.fromhex('004341444655445202450000000000000000'): raise ValueError('unexpected target')
 def stage(at,data):
  command(struct.pack('<BII',5,at,len(data)),len(data))
  write(data)
  status('SRAM upload')
  time.sleep(0.025)
  if sram(at,len(data))!=data: raise ValueError('SRAM readback mismatch')
 stage(base,payload)
 print(f'Timer comparison nonce {nonce:08X}',flush=True)
 command(struct.pack('<BI',0x10,base))
 status('Timer comparison handoff')
 time.sleep(.25)
 trace=sram(out,96)
 for i in range(12):
  marker,phase,original,restored=struct.unpack_from('<4I',trace)
  if marker==nonce and phase==2: break
  time.sleep(.25)
  trace=sram(out,96)
 if marker!=nonce: raise ValueError(f'No fresh test marker ({marker:08X})')
 (root/'adfu-x70-ddr-pll-timer-ab.bin').write_bytes(trace)
 for label_name,off in (('before',0x10),('after',0x30)):
  start,end,wait_ok,ctrl=struct.unpack_from('<4I',trace,off)
  print(f'{label_name} PLL timerctl={ctrl:08X} ticks={start:08X}->{end:08X} hit={wait_ok}')
 print(f'phase={phase} PLL initial={original:08X} test={struct.unpack_from("<I",trace,32)[0]:08X} restored={restored:08X}')
 if phase!=2 or original!=restored: raise ValueError('Timer comparison did not finish restoring clocks')

finally:
 if h.value: Free(h)
 if f: Close(f)
