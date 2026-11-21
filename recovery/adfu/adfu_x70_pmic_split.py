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



 root=Path(__file__).resolve().parent
 mb=(root/'xgb35-mbrec-recovered.bin').read_bytes()
 from hashlib import sha256
 assert sha256(mb).hexdigest()=='34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc'
 wrap,out,stage=0xB406A800,0xB406AC00,0xB406B000
 origin=0x2300
 native=bytearray(mb[origin:0x6300].ljust(0x4800,b'\0'))
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
 voltage_hook=stage+0x4400
 words(native,0x4400,0xe92d4010,*load32(4,out),0xe5841024,
       br(voltage_hook+16,dest(0x2a68),link=True),0xe5840028,0xe8bd8010)
 # Record the native calibration outcome before its caller handles the result.
 cal_hook=stage+0x4440
 words(native,0x3db4-origin,br(dest(0x3db4),cal_hook))
 cal_trace=[0xe92d000e,*load32(1,out),0xe581003c,*load32(2,0xb0230000),
            0xe5923050,0xe5813040,0xe3a03001,0xe58130b8,0xe8bd000e,0xe3500000]
 cal_trace.append(br(cal_hook+4*len(cal_trace),dest(0x3db8)))
 words(native,0x4440,*cal_trace)
 # Log the original calibration loop without changing its wait or status checks.
 settle=stage+0x4480
 words(native,0x437c-origin,br(dest(0x437c),settle))
 probe=[
  0xe92d0005,*load32(0,out+0x60),0xe203200f,0xe2422002,
  0xe0802202,0xe5823000,0xe582c004,
  0xe591c050,0xe582c008,0xee072f3a,0xf57ff04f,0xe8bd0005,
 ]
 probe.append(br(settle+len(probe)*4,dest(0x4380)))
 words(native,0x4480,*probe)
 # Reissue the unchanged calibration sequence up to eight times per cold start.
 retry=stage+0x44c0
 words(native,0x4394-origin,br(dest(0x4394),retry))
 retries=[0xe92d0010,*load32(4,out),0xe594c058,0xe25cc001,
          0xe584c058,0xe8bd0010,br(retry+28,dest(0x4358),cond=1),0xe3e00000,0xe12fff1e]
 words(native,0x44c0,*retries)
 # Keep a committed I2C checkpoint even if the stock routine never returns.
 checkpoints=((0x2ab8,0x2fec,0x4500),(0x2adc,0x2a58,0x4540),
              (0x2b10,0x2a58,0x4580),(0x2b58,0x28f0,0x45c0),
              (0x408c,0x2a68,0x4000),(0x40ec,0x3a74,0x4040),
              (0x3db0,0x4340,0x4080))
 for i,(site,callee,off) in enumerate(checkpoints):
  addr=stage+off
  trace=0xc0+i*8
  line=trace&~31
  target=voltage_hook if site==0x408c else dest(callee)
  words(native,site-origin,br(dest(site),addr,link=True))
  instructions=[0xe92d4010,*load32(4,out),0xe3a0c001,0xe584c000|trace,
                0xe284c000|line,0xee07cf3a,0xf57ff04f]
  instructions.append(br(addr+4*len(instructions),target,link=True))
  instructions.extend((0xe5840000|(trace+4),0xe3a0c002,0xe584c000|trace,
                       0xe284c000|line,0xee07cf3a,0xf57ff04f,0xe8bd8010))
  assert len(instructions)*4<=0x40
  words(native,off,*instructions)
 # Log DDR waits without adding cache flushes to the PMIC's 1 us polling loop.
 wait_sites=(0x3ab0,0x3adc,0x3b10,0x3b24,0x3d1c)
 for i,site in enumerate(wait_sites):
  slot=0x40c0+i*0x40
  adr=stage+slot
  words(native,site-origin,br(dest(site),adr,link=True))
  ops=[0xe92d400e,*load32(2,out),0xe582e0f8,0xe28230e0,0xee073f3a,0xf57ff04f]
  ops.append(br(adr+4*len(ops),dest(0x23fc),link=True))
  ops.extend((*load32(2,out),0xe59de00c,0xe582e0fc,
              0xe28230e0,0xee073f3a,0xf57ff04f,0xe8bd800e))
  assert len(ops)*4==0x40
  words(native,slot,*ops)
 # Use a private SRAM stack so Boot ROM can service USB while MBREC is suspended.
 stack_top=0xb406a7c0
 resume_at=0xb406a000
 suspend_at=stage+0x4600
 boot_return=(0xe3a00000,0xe594d180,0xe8bd9ff0)
 restore_clocks=(*load32(1,0xb0150000),0xe5942038,0xe581201c,
                 0xe5942034,0xe581206c,0xe5942030,0xe5812000,0xf57ff04f)
 suspend=[0xe92d5fff,*load32(4,out),0xe584d184,0xe584e1a0,0xe58401a4,0xe58411a8,0xe3a00003,0xe584019c,
          0xe2842d06,0xee072f3a,0xf57ff04f,0xe10f0000,0xe5840188,
          *load32(1,0xb0150000),0xe5910008,0xe584018c,0xe5910000,0xe5840190,
          0xe591006c,0xe5840194,0xe591001c,0xe5840198,
          0xe3a00001,0xe584019c,0xe3cd201f]
 suspend.extend((*load32(3,stack_top),0xe3a01020))
 clean_index=len(suspend)
 suspend.extend((0xee072f3a,0xe0822001,0xe1520003))
 suspend.append(br(suspend_at+4*len(suspend),suspend_at+4*clean_index,cond=3))
 suspend.extend((0xe2842d06,0xee072f3a,0xe2822020,0xee072f3a,0xf57ff04f,*restore_clocks,*boot_return))
 assert len(suspend)*4<=0x100
 pmic_continue=dest(0x4090)
 pll_continue=dest(0x3b00)
 words(native,0x408c-origin,br(dest(0x408c),suspend_at,link=True))
 words(native,0x3afc-origin,br(dest(0x3afc),suspend_at,link=True))
 words(native,0x4600,*suspend)
 resume=[0xe92d5ff0,*load32(4,out),0xe594019c,0xe3500001,0x18bd9ff0,
         0xe584d180,*load32(1,0xb0150000),0xe5940190,0xe5810000,0xf57ff04f,
         *load32(2,0x00100000),0xe2522001,0x1afffffd,
         0xe5940194,0xe581006c,0xe5940198,0xe581001c,0xf57ff04f,
         0xe3a00002,0xe584019c,0xe2842d06,0xee072f3a,0xf57ff04f,
         0xe59401a0,*load32(1,pmic_continue),0xe1500001]
 restore_context=(0xe5940188,0xe128f000,0xe594d184,0xe8bd5fff)
 branch=len(resume)
 resume.append(0)
 resume.extend((*restore_context,0xe12fff1e))
 resume[branch]=br(resume_at+4*branch,resume_at+4*len(resume),cond=0)
 resume.extend(restore_context)
 resume.append(br(resume_at+4*len(resume),stage+0x4000))
 resume_frame=struct.pack('<'+'I'*len(resume),*resume).ljust(512,b'\0')
 # Log stock PMIC transactions after the validated chip ID. The low bit of
 # the saved return address distinguishes a completed transfer from an entry.
 i2c_trace=0xb406a200
 i2c_sites={
  0x28f0:(0x2b88,0x2dc0,0x2d58,0x2d68,0x2d78),
  0x2a58:(0x2c28,0x2c34,0x2c4c,0x2c64,0x2d00,0x2d38,0x4050),
  0x2a68:(0x2c44,0x2c5c,0x2c74,0x2ca4,0x2cc0,0x2ccc,0x2cf8,0x2d24,0x2d30,0x2d4c),
 }
 common=stage+0x4740
 for i,(target,sites) in enumerate(i2c_sites.items()):
  offset=0x4700+i*0x10
  address=stage+offset
  words(native,offset,*load32(12,dest(target)),br(address+8,common))
  for site in sites:
   assert struct.unpack_from('<I',native,site-origin)[0]==br(dest(site),dest(target),link=True)
   words(native,site-origin,br(dest(site),address,link=True))
 i2c_bridge=[
  0xe92d41f0,0xe1a0500c,*load32(4,i2c_trace),0xe5946000,0xe356001e,0,
  0xe0847206,0xe2877010,0xe587e000,0xe5870004,0xe5871008,0xe3e08000,0xe587800c,
  0xe2866001,0xe5846000,0xee074f3a,0xee077f3a,0xf57ff04f,0xe12fff35,
  0xe587000c,0xe5978000,0xe3888001,0xe5878000,0xee077f3a,0xf57ff04f,0xe8bd81f0,
  0xe12fff35,0xe8bd81f0,
 ]
 i2c_bridge[6]=br(common+6*4,common+27*4,cond=2)
 assert len(i2c_bridge)*4<=0xc0
 words(native,0x4740,*i2c_bridge)
 assert len(native)==0x4800 and mb[0x30c00:0x30c04]==b'!AFI'
 frame=bytearray(1024)
 frame[0x2c0:0x350]=mb[0x30c00:0x30c90]
 assert struct.unpack_from('<H',frame,0x2e8)[0]==360
 mhz=int(sys.argv[1])
 assert mhz in (240,360)
 struct.pack_into('<H',frame,0x2e8,mhz)
 seq=[]
 def emit(*ops): seq.extend(ops)
 def call(at): emit(br(wrap+len(seq)*4,dest(at),link=True))
 emit(0xe92d5ff0,*load32(4,out),0xe584d180,*load32(13,stack_top),0xe1a05004,0xe3a00000,0xe584019c,0xe3a01040)
 # Initialize the report from the CPU, including any previous cached copy.
 clear=len(seq)
 emit(0xe4850004,0xe2511001)
 emit(br(wrap+len(seq)*4,wrap+clear*4,cond=1))
 marker_expected=secrets.randbits(32)
 emit(*load32(4,out),*load32(6,i2c_trace),0xe5860000,0xee076f3a,
      0xee074f3a,0xe2842d06,0xee072f3a,
      *load32(0,marker_expected),0xe5840054,0xe2841040,0xee071f3a,0xf57ff04f)
 emit(*load32(0,0x00080000),0xe5840010,0xe3a00008,0xe5840058)
 emit(0xe3a00000,0xe5840014,0xe5840018)
 emit(0xe3e00000,0xe5840000,0xe5840020,0xe5840024,0xe5840028)
 # Keep timer 0 for PMIC setup, as on the successful cold trace.
 emit(0xe3a00000)
 call(0x2438)
 emit(*load32(5,0xb0158010),0xe5956000,0xe5846048,*load32(7,0x10000))
 timer_poll=len(seq)
 emit(0xe5950000,0xe1500006)
 timer_ready=len(seq)
 emit(0,0xe2577001)
 emit(br(wrap+len(seq)*4,wrap+timer_poll*4,cond=1))
 timer_failed=len(seq)
 emit(0)
 seq[timer_ready]=br(wrap+timer_ready*4,wrap+len(seq)*4,cond=8)
 emit(0xe584004c)
 emit(*load32(0,wrap+0x2c0))
 call(0x37fc)
 emit(0xe58400bc)
 emit(*load32(0,0xb0150000),0xe5902000,0xe5842030,0xe590206c,0xe5842034,0xe590201c,0xe5842038)
 emit(0xe3a06003)
 pmu_try=len(seq)
 emit(0xe58460b0)
 call(0x2e7c)
 emit(*load32(4,out),0xe5840020,0xe3500000)
 pmu_ok=len(seq)
 emit(0,0xe2566001)
 emit(br(wrap+len(seq)*4,wrap+pmu_try*4,cond=1))
 pmu_skip=len(seq)
 emit(0)
 seq[pmu_ok]=br(wrap+pmu_ok*4,wrap+len(seq)*4,cond=0)
 emit(0xe3a02001,0xe58420b4)
 # Use timer 1 only once the stock PMIC initialization has succeeded.
 emit(0xe3a00001)
 call(0x2438)
 emit(*load32(0,0xb0150000),0xe59020a0,0xe382200a,0xe58020a0,0xf57ff04f)
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
 emit(*load32(1,0xb0150000),0xe5912008,0xe5842008)
 emit(0xe3a00014)
 call(0x2a58)
 emit(0xe584002c)
 seq[pmu_skip]=br(wrap+pmu_skip*4,wrap+len(seq)*4)
 emit(0xe3a02002,0xe584201c)
 emit(0xf57ff04f,0xee074f3a,0xe2841020,0xee071f3a,0xe2841040,0xee071f3a,0xe2841060,0xee071f3a,0xe2841080,0xee071f3a,0xe28410a0,0xee071f3a,0xf57ff04f)
 # Restore the original CPU/bus clock after the physical-memory checks for USB.
 emit(*restore_clocks,*boot_return)
 seq[timer_failed]=br(wrap+timer_failed*4,wrap+len(seq)*4)
 emit(0xe2841040,0xee071f3a,0xf57ff04f,*boot_return)
 assert len(seq)*4<=0x2c0
 words(frame,0,*seq)

 if '--dry-run' in sys.argv:
  sys.path.insert(0,str(root/'python-analysis'))
  from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
  code=bytes(native[0x4480:0x4480+4*len(probe)])
  decoded=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(code,settle))
  assert len(decoded)==len(probe)
  decoded.extend(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(bytes(native[0x44c0:0x44c0+4*len(retries)]),retry))
  for ins in decoded: print(f'{ins.address:08x} {ins.mnemonic} {ins.op_str}')
  for site,callee,off in checkpoints:
   address=stage+off
   hook=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(bytes(native[off:off+0x40]),address))
   assert len(hook)==16 and hook[8].mnemonic=='bl' and int(hook[8].op_str[1:],16)==(voltage_hook if site==0x408c else dest(callee))
   assert hook[15].mnemonic=='pop' and hook[15].op_str=='{r4, pc}'
   print(f'PMIC call 0x{site:04x} -> 0x{callee:04x} at {address:08x}')
  for blob,base,points in ((native,stage,(0x3db0-origin,0x4394-origin,0x3afc-origin,*(site-origin for site in wait_sites))),(frame,wrap,tuple(i*4 for i in (pmu_try,pmu_ok,pmu_skip,skip)))):
   for offset in points:
    ins,=Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(bytes(blob[offset:offset+4]),base+offset)
    print(f'{ins.address:08x} {ins.mnemonic} {ins.op_str}')
  for machine,address in ((suspend,suspend_at),(resume,resume_at)):
   ops=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(struct.pack('<'+'I'*len(machine),*machine),address))
   assert len(ops)==len(machine)
   assert any(i.mnemonic=='add' and i.op_str=='r2, r4, #0x180' for i in ops)
   for ins in ops: print(f'{ins.address:08x} {ins.mnemonic} {ins.op_str}')
  patch,=Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(bytes(native[0x408c-origin:0x4090-origin]),dest(0x408c))
  assert patch.mnemonic=='bl' and int(patch.op_str[1:],16)==suspend_at
  bridge_ops=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(bytes(native[0x4740:0x4740+4*len(i2c_bridge)]),common))
  assert len(bridge_ops)==len(i2c_bridge) and bridge_ops[19].mnemonic=='blx'
  for ins in bridge_ops: print(f'{ins.address:08x} {ins.mnemonic} {ins.op_str}')
  sys.exit()

 def transfer(at,blob):
  command(struct.pack('<BII',5,at,len(blob)),len(blob))
  write(bytes(blob))
  status('DDR SRAM upload')
  time.sleep(0.025)
  try:
   readback=sram(at,len(blob))
  except OSError as error:
   raise OSError(f'SRAM transfer at 0x{at:08X}: {error}') from error
  if readback!=blob: raise ValueError(f'DDR SRAM mismatch at {at:08x}')
 command(bytes((0xcc,0,0,0,0,0,0,18)),18,0x80)
 if read_data(18,'IDENT')!=bytes.fromhex('004341444655445202450000000000000000'): raise ValueError('unexpected target')
 for i in range(0,len(native),1024): transfer(stage+i,native[i:i+1024])
 transfer(wrap,frame)
 transfer(resume_at,resume_frame)
 command(struct.pack('<BI',0x10,wrap))
 status('DDR init handoff')
 def read_until(wanted):
  stop=time.monotonic()+7
  time.sleep(1.5)
  while True:
   report=sram(out)
   marker,=struct.unpack_from('<I',report,0x54)
   state,=struct.unpack_from('<I',report,0x19c)
   done,=struct.unpack_from('<I',report,0x1c)
   if (marker==marker_expected and (wanted(report) or done==2)) or time.monotonic()>=stop:
    return report
   time.sleep(.25)
 def paused_at(sample,pc):
  phase,continuation=struct.unpack_from('<2I',sample,0x19c)
  return phase==1 and continuation==pc
 for name,pc in (('before PMIC write',pmic_continue),('after DDR PLL',pll_continue)):
  report=read_until(lambda sample: paused_at(sample,pc))
  marker,=struct.unpack_from('<I',report,0x54)
  if marker!=marker_expected or not paused_at(report,pc):
   phase,continuation=struct.unpack_from('<2I',report,0x19c)
   print(f'Missing {name} checkpoint: phase={phase} PC={continuation:08X} marker={marker:08X}',flush=True)
   break
  suspend_sp,_,paused_pll=struct.unpack_from('<3I',report,0x184)
  assert 0xb406a400<=suspend_sp<stack_top and suspend_sp%4==0
  args=struct.unpack_from('<2I',report,0x1a4)
  if pc==pmic_continue:
   assert args==(0x14,0x8cca), f'Unexpected stock PMIC request {args}'
  else:
   assert paused_pll==0x100+mhz//6
  (root/f'adfu-x70-ddr-pmic-pause-{pc:08x}.bin').write_bytes(report)
  print(f'Suspended {name}: PLL={paused_pll:08X} args={args!r}',flush=True)
  time.sleep(.020)
  command(struct.pack('<BI',0x10,resume_at))
  status(f'Resume {name}')
  print(f'Resume accepted {name}',flush=True)
 else:
  report=read_until(lambda sample: struct.unpack_from('<I',sample,0x1c)[0]==2)
 if struct.unpack_from('<I',report,0x54)[0]==marker_expected:
  (root/f'adfu-x70-ddr-pmic-split-result-{mhz}.bin').write_bytes(report)
 report=report[:0x1a0]
 marker,=struct.unpack_from('<I',report,0x54)
 if marker==marker_expected:
  transactions=sram(i2c_trace,512)
  count,=struct.unpack_from('<I',transactions)
  known_sites={site:target for target,sites in i2c_sites.items() for site in sites}
  assert 0<=count<=30, f'Invalid PMIC transaction count {count}'
  print(f'PMIC trace count={count}',flush=True)
  (root/f'adfu-x70-pmic-after-id-{mhz}.bin').write_bytes(transactions[:16+count*16])
  for lr,arg0,arg1,result in struct.iter_unpack('<4I',transactions[16:16+count*16]):
   site=(lr&~1)-stage+origin-4
   target=known_sites[site]
   reg=arg1 if target==0x28f0 else arg0
   action=f'write 0x{arg1:04X}' if target==0x2a68 else 'read'
   state=f'returned 0x{result:08X}' if lr&1 else 'entered, no return'
   print(f'PMIC 0x{site:04X} register 0x{reg:02X} {action}: {state}',flush=True)
 result,mem,pll,_,budget,which,ddr_return,done,pmu,vdd,vddret,vddread,_,_,_,cal,reg50,stack_word,timer_before,timer_after=struct.unpack_from('<20I',report)
 print(f'PMU={pmu:08X} VDD_DDR={vdd:08X} VDD_RET={vddret:08X} VDD_READ={vddread:08X} DDR_RET={result:08X} DDR_WORD={mem:08X} DDR_STACK={stack_word:08X} DDRPLL={pll:08X} CAL={cal:08X} DDR_REG50={reg50:08X} GUARD={which} RETURNED={ddr_return} DONE={done} TIMER={timer_before:08X}->{timer_after:08X}',flush=True)
 marker,=struct.unpack_from('<I',report,0x54)
 if marker!=marker_expected: raise ValueError(f'SRAM routine did not start: marker={marker:08X}')
 for (site,callee,_),(state,value) in zip(checkpoints,struct.iter_unpack('<2I',report[0xc0:0xf8])):
  assert state in (0,1,2), f'invalid PMIC checkpoint 0x{site:04X}: {state}'
  status_text=('skipped','entered, no return',f'returned 0x{value:08X}')[state]
  print(f'MBREC 0x{site:04X} -> 0x{callee:04X}: {status_text}',flush=True)
 entered,completed=struct.unpack_from('<2I',report,0xf8)
 def delay_site(address):
  return f'0x{address-stage+origin:04X}' if stage<=address<stage+len(native) else 'not reached'
 print(f'last DDR delay entered={delay_site(entered)} returned={delay_site(completed)}',flush=True)
 (root/f'adfu-x70-ddr-split-pmic-result-{mhz}.bin').write_bytes(report)
 for expected,(setting,cmd,reg,_) in zip((0x32,0x43,0x54,0x65,0x76),struct.iter_unpack('<4I',report[0x60:0xb0])):
  if setting!=expected or reg&0xff!=setting: break
  print(f'{mhz} MHz setting={setting:02X} command={cmd:08X} status={reg:08X}',flush=True)
 attempts_left,=struct.unpack_from('<I',report,0x58)
 pmu_left,ddr_entered,cal_returned,config=struct.unpack_from('<4I',report,0xb0)
 assert 1<=pmu_left<=3 and ddr_entered in (0,1) and cal_returned in (0,1)
 print(f'PMU calls={4-pmu_left} config={config:08X} DDR entered={ddr_entered} calibration returned={cal_returned} full failed rounds={8-attempts_left}',flush=True)
 if done!=2 or not ddr_entered or not cal_returned:
  raise ValueError('calibration did not return in this run')
 if cal!=0 or mem!=0x44524431 or stack_word!=0x1fa27be3: raise ValueError('independent DDR readback not verified')
finally:
 if h.value: Free(h)
 if f: Close(f)
