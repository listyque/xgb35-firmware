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



 out=0xb406a200
 cmu=0xb0150000
 names=("initial registers","assert DDR reset","enable stock DDR clocks",
        "write DDR PLL","repeat reset assertion","release DDR reset",
        "read DDR controller","restore original registers")
 def mov32(reg,val):
  return (0xe3000000|reg<<12|(val&0xf000)<<4|(val&0xfff),
          0xe3400000|reg<<12|((val>>16)&0xf000)<<4|((val>>16)&0xfff))
 def get(reg,offset): return 0xe5910000|reg<<12|offset
 def put(reg,offset): return 0xe5800000|reg<<12|offset
 def set_cmu(reg,offset): return 0xe5810000|reg<<12|offset
 def phase(value):
  return (0xe3a02000|value,put(2,4),0xee070f3a,0xf57ff04f)
 default_reset=(get(2,0xa8),0xe3c22004,set_cmu(2,0xa8))
 def operation(step,original):
  if step==1 or step==4: return default_reset
  if step==2: return (get(2,0xa0),*mov32(3,0x1004000a),0xe1822003,set_cmu(2,0xa0))
  if step==3: return (*mov32(2,0x13c),set_cmu(2,8))
  if step==5: return (get(2,0xa8),0xe3822004,set_cmu(2,0xa8))
  if step==6: return (*mov32(3,0xb0230000),0xe5932004,put(2,28))
  if step==7:
   pll,gate,reset=original
   return (*default_reset,*mov32(2,pll),set_cmu(2,8),0xf57ff04f,
           *mov32(2,gate),set_cmu(2,0xa0),*mov32(2,reset),set_cmu(2,0xa8))
  return ()
 def make(step,nonce,original=None):
  ops=(*mov32(0,out),*mov32(1,cmu),*mov32(2,nonce),put(2,0),*phase(1),
       *operation(step,original),0xf57ff04f,get(2,8),put(2,8),
       get(2,0xa0),put(2,12),get(2,0xa8),put(2,16),
       *mov32(3,0xb0158000),0xe5932008,put(2,20),0xe5932010,put(2,24),
       *phase(2),0xe12fff1e)
  assert len(ops)*4<=512
  return struct.pack('<'+'I'*len(ops),*ops).ljust(512,b'\0')
 if '--dry-run' in sys.argv:
  root=Path(__file__).resolve().parent
  sys.path.insert(0,str(root/'python-analysis'))
  from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
  for i,name in enumerate(names):
   body=operation(i,(10,0,0xff9fffff))
   ins=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(struct.pack('<'+'I'*len(body),*body),0))
   assert len(ins)==len(body)
   print(name,[(op.mnemonic,op.op_str) for op in ins])
   make(i,3,(10,0,0xff9fffff))
  sys.exit()
 command(bytes((0xcc,0,0,0,0,0,0,18)),18,0x80)
 if read_data(18,'IDENT')!=bytes.fromhex('004341444655445202450000000000000000'): raise ValueError('unexpected target')
 root=Path(__file__).resolve().parent
 logfile=root/'adfu-x70-ddr-pll-steps.tsv'
 samples=[]
 def send(at,payload):
  command(struct.pack('<BII',5,at,len(payload)),len(payload))
  write(payload)
  status('step SRAM upload')
  time.sleep(0.025)
  if sram(at)!=payload: raise ValueError(f'SRAM readback mismatch @ {at:08X}')
 def execute(step,original=None):
  nonce=secrets.randbits(32)
  at=0xb406a000+step*0x400
  send(at,make(step,nonce,original))
  print(f'{step} {names[step]}',flush=True)
  command(struct.pack('<BI',0x10,at))
  status('step SRAM handoff')
  deadline=time.monotonic()+3
  trace=None
  while time.monotonic()<deadline:
   time.sleep(.075)
   values=struct.unpack('<8I',sram(out,32))
   if values[0]==nonce:
    trace=values
    if values[1]==2: break
  if trace is None: raise ValueError(f'step {step} no fresh marker')
  _,phase_no,pll,gate,reset,ctl,ticks,ddrc=trace
  print(f'  marker={phase_no} pll={pll:08X} gate={gate:08X} reset={reset:08X} timer0={ctl:08X}/{ticks:08X} ddrc={ddrc:08X}',flush=True)
  samples.append('\t'.join((str(step),names[step],str(phase_no),*(f'{v:08X}' for v in trace[2:]))))
  logfile.write_text('step\toperation\tphase\tpll\tgate\treset\ttimer_ctl\ttimer_ticks\tddrc\n'+'\n'.join(samples)+'\n')
  if phase_no!=2: raise ValueError(f'step {step} entered without completion')
  return pll,gate,reset
 original=execute(0)
 try:
  for step in range(1,7):
   pll,gate,reset=execute(step)
   if step in (1,4) and reset&4 or step==5 and not reset&4:
    raise ValueError('reset readback differs')
   if step==2 and gate&0x1004000a!=0x1004000a:
    raise ValueError('clock enable readback differs')
   if step==3 and pll!=0x13c:
    raise ValueError('PLL readback differs')
 finally:
  try:
   if execute(7,original)!=original: raise ValueError('clock and reset restoration differs')
  except Exception as error:
   raise RuntimeError(f'volatile registers not verified restored: {error}') from error

finally:
 if h.value: Free(h)
 if f: Close(f)
