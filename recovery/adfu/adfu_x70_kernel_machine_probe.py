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



 import json
 import lzma
 from hashlib import sha256
 from zipfile import ZipFile
 root=Path(__file__).resolve().parent
 image_root=root/'x70-recovery-ram'
 manifest=json.loads((image_root/'manifest.json').read_text())
 addresses={'zImage':0x01000000,'ram-initramfs.cpio.gz':0x01800000,'ram-kernel.dtb':0x01e00000}
 assert manifest['physical_addresses']==addresses
 blobs={name:(image_root/name).read_bytes() for name in addresses}
 with ZipFile(root/'x70-recovery-ram.zip') as archive:
  for name, data in blobs.items():
   assert sha256(data).hexdigest()==manifest['files'][name]
   assert archive.read(name)==data
 for i,(name,address) in enumerate(addresses.items()):
  end=list(addresses.values())[i+1] if i+1<len(addresses) else manifest['ram_limit']
  assert address+len(blobs[name])+3<end
 assert struct.unpack_from('<I',blobs['zImage'],0x24)[0]==0x016f2818
 assert struct.unpack_from('>II',blobs['ram-kernel.dtb'])==(0xd00dfeed,len(blobs['ram-kernel.dtb']))
 raw_kernel=True
 boot_word_offset=0x24
 if raw_kernel:
  compressed=blobs.pop('zImage')
  stream=lzma.LZMADecompressor(format=lzma.FORMAT_XZ)
  kernel=stream.decompress(compressed[0x3b2c:],max_length=16*1024*1024)
  assert stream.eof and len(kernel)==7462784 and b'Linux version 3.10.52' in kernel
  boot_word_offset=0
  # The stock zImage computes the expansion address as 0x8000 for low DDR.
  addresses={'Image':0x00008000,**{k:v for k,v in addresses.items() if k!='zImage'}}
  blobs={'Image':kernel,**blobs}
  assert addresses['Image']+len(kernel)<addresses['ram-initramfs.cpio.gz']
 boot_name=next(iter(addresses))
 loaded_hashes={name:sha256(data).hexdigest() for name,data in blobs.items()}
 expected_boot_word,=struct.unpack_from('<I',blobs[boot_name],boot_word_offset)
 prior=(root/'adfu-x70-ddr-pmic-once-result-360.bin').read_bytes()
 ddr_pll,core,dev,bus=struct.unpack_from('<4I',prior,0x18c)
 assert struct.unpack_from('<I',prior,0x1c)[0]==2 and ddr_pll==0x13c
 source,header,out,worker,loader=0xb4069000,0xb406a400,0xb406a200,0xb406b800,0xb406bc00
 maximum=4096
 request_magic=0x52414d31
 proof_mask=secrets.randbits(31)
 def mov32(reg,val):
  return (0xe3000000|reg<<12|(val&0xf000)<<4|(val&0xfff),
          0xe3400000|reg<<12|((val>>16)&0xf000)<<4|((val>>16)&0xfff))
 class Arm:
  def __init__(self):
   self.ops=[]; self.labels={}; self.jumps=[]
  def emit(self,*ops): self.ops.extend(ops)
  def label(self,name): self.labels[name]=len(self.ops)
  def jump(self,name,cond=14):
   self.jumps.append((len(self.ops),name,cond))
   self.emit(0)
  def pack(self):
   for i,name,cond in self.jumps:
    self.ops[i]=cond<<28|0x0a000000|((self.labels[name]-i-2)&0xffffff)
   code=struct.pack('<'+'I'*len(self.ops),*self.ops)
   assert len(code)<=512
   return code.ljust(512,b'\0')
 enable_ddr_clocks=(*mov32(0,core),0xe5850000,0xf57ff04f,*mov32(1,0x10000),
                    0xe2511001,0x1afffffd,*mov32(0,dev),0xe585006c,
                    *mov32(0,bus),0xe585001c,0xf57ff04f)

 # ADFU can enter a resident command repeatedly. An SRAM nonce commits one chunk.
 arm=Arm()
 emit,jump,label=arm.emit,arm.jump,arm.label
 emit(0xe92d4ff0,*mov32(4,out),*mov32(5,header),0xe89503c0,
      *mov32(1,request_magic),0xe1590001)
 jump('return',1)
 emit(0xe3580000)
 jump('return',0)
 emit(0xe3580b01)
 jump('return',8)
 emit(*mov32(1,min(addresses.values())),0xe1570001)
 jump('return',3)
 emit(0xe3570402)
 jump('return',2)
 emit(*mov32(1,0xfff),0xe1170001)
 jump('return',1)
 emit(0xe5940000,0xe1500006)
 jump('return',0)
 emit(0xe5846000,0xe3a00001,0xe5840004,0xe5847008,0xe584800c,
      0xee074f3a,0xf57ff04f)
 emit(*mov32(5,0xb0150000),0xe5950008,0xe3500f4f) # compare DDR PLL with 0x13c
 jump('bad_clock',1)
 emit(0xe5950000,0xe5840010,0xe595006c,0xe5840014,0xe595001c,0xe5840018)
 emit(*enable_ddr_clocks)
 emit(*mov32(0,source),0xe1a01007,0xe1a02008)
 label('write')
 emit(0xe4903004,0xe4813004,0xe2522001)
 jump('write',1)
 emit(0xf57ff04f,0xe1a02007)
 label('clean')
 emit(0xee072f3e,0xe2822020,0xe1520001)
 jump('clean',3)
 emit(0xf57ff04f,*mov32(0,source),0xe1a01007,0xe1a02008)
 label('check')
 emit(0xe490a004,0xe491b004,0xe15a000b)
 jump('mismatch',1)
 emit(0xe2522001)
 jump('check',1)
 emit(0xe3a03002)
 jump('finish')
 label('mismatch')
 emit(0xe3a030ff,0xe584101c)
 label('finish')
 emit(0xe5940018,0xe585001c,0xe5940014,0xe585006c,0xe5940010,0xe5850000,
      0xf57ff04f,0xe5843004,0xee074f3a,0xf57ff04f)
 jump('return')
 label('bad_clock')
 emit(0xe3a030fe,0xe5843004,0xee074f3a,0xf57ff04f)
 label('return')
 emit(0xe3a00000,0xe8bd8ff0)
 worker_code=arm.pack()

 mmu_report=0xb406b000
 probes=(('after_processor_announcement',0x6a98f0),('before_machine_fdt_lookup',0x6a9b1c),
         ('after_machine_fdt_lookup',0x6a9b20),('after_machine_selection',0x6a9b38))
 sysregs=((2,0,0),(2,0,1),(2,0,2),(3,0,0),(12,0,0))
 def cp15(read,rd,reg):
  crn,crm,op2=reg
  return (0xee100f10 if read else 0xee000f10)|crn<<16|rd<<12|op2<<5|crm
 def branch_at(at,to): return 0xea000000|(((to-at-8)//4)&0xffffff)
 memory=blobs['Image']
 def kernel_page(at):
  return bytearray(memory[at-0x8000:at-0x8000+4096])
 assert struct.unpack_from('<I',memory,0x8078-0x8000)[0]==0xc06a72bc
 cases=[]
 for index,(name,where) in enumerate(probes):
  cell=mmu_report+index*0x80
  start=0xb406c800+index*0x200
  stop=0xb406d800+index*0x200
  nonce=secrets.randbits(32)
  pages={at:kernel_page(at) for at in (0x8000,0x4e4000,0x6a7000,0x6a9000)}
  target=pages[where&~0xfff]
  struct.pack_into('<II',target,where&0xfff,0xe51ff004,0x4e4580)
  mmu=pages[0x4e4000]
  struct.pack_into('<4I',mmu,0x580,0xee11af10,0xe59f0000,branch_at(0x4e4588,0x4e459c),stop)
  assert struct.unpack_from('<I',mmu,0x5a0)[0]==0xe3c11001
  struct.pack_into('<I',mmu,0x5a0,0xe3c11005)
  boot=Arm()
  boot.emit(0xe92d5ff0,*mov32(4,cell),*mov32(0,nonce),0xe5941000,0xe1500001)
  boot.jump('leave',0)
  boot.emit(0xe5840000,0xe3a00001,0xe5840004,0xe584d008,0xe10f0000,0xe584000c,
            0xee110f10,0xe5840010,*mov32(5,0xb0150000),
            0xe5950000,0xe5840014,0xe595006c,0xe5840018,0xe595001c,0xe584001c)
  for j,reg in enumerate(sysregs):
   boot.emit(cp15(True,0,reg),0xe5840040+j*4)
  boot.emit(0xe2841040,0xee071f3a,0xee074f3a,0xf57ff04f)
  boot.emit(*enable_ddr_clocks,*mov32(3,0x8000),*mov32(2,addresses['ram-kernel.dtb']),
            0xf10c00c0,0xf1020013,0xf57ff04f,0xee110f10,
            0xe3c00005,0xe3c00a01,0xee010f10,0xf57ff06f,
            0xe3a00000,0xee070f15,0xee070fd5,0xf57ff04f,0xf57ff06f,
            0xe3e01000,0xe12fff13)
  boot.label('leave')
  boot.emit(0xe3a00000,0xe8bd9ff0)
  resume=Arm()
  resume.emit(*mov32(4,cell),0xe584003c,0xe5849020,0xe5845024,0xe5842028,0xe10f0000,0xe584002c,
              0xee110f10,0xe5840030,0xe584a034,cp15(True,0,sysregs[0]),0xe5840038,
              0xe3a00002,0xe5840004,
              0xe2841020,0xee071f3a,0xee074f3a,0xf57ff04f)
  resume.emit(*mov32(5,0xb0150000),0xe594001c,0xe585001c,0xe5940018,0xe585006c,
              0xe5940014,0xe5850000,0xf57ff04f)
  for j,reg in enumerate(sysregs):
   resume.emit(0xe5940040+j*4,cp15(False,0,reg))
  resume.emit(0xe3a00000,0xee080f17,0xf57ff04f,0xf57ff06f)
  resume.emit(0xe5940010,0xee010f10,0xf57ff06f,0xe594d008,
              0xe594000c,0xe121f000,0xe3a00000,0xe8bd9ff0)
  cases.append((name,where,start,stop,cell,nonce,pages,boot.pack(),resume.pack()))
 if '--dry-run' in sys.argv:
  sys.path.insert(0,str(root/'python-analysis'))
  from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
  dis=Cs(CS_ARCH_ARM,CS_MODE_ARM)
  for name,where,start,stop,cell,nonce,pages,begin,end in cases:
   at=where&0xfff
   ins,=dis.disasm(pages[where&~0xfff][at:at+4],where)
   assert ins.mnemonic=='ldr' and ins.op_str=='pc, [pc, #-4]'
   print(f'{name} 0x{0xc0000000+where:08x} -> idmap 0x004e4580 -> SRAM 0x{stop:08x}')
  restore=list(dis.disasm(cases[-1][-1],cases[-1][3]))
  assert any(op.mnemonic=='mcr' and 'c8, c7' in op.op_str for op in restore)
  assert any(op.mnemonic=='msr' and op.op_str=='cpsr_c, r0' for op in restore)
  sys.exit()
 command(bytes((0xcc,0,0,0,0,0,0,18)),18,0x80)
 if read_data(18,'IDENT')!=bytes.fromhex('004341444655445202450000000000000000'): raise ValueError('unexpected target')
 def transfer(at,data):
  data=bytes(data)
  command(struct.pack('<BII',5,at,len(data)),len(data))
  write(data)
  status(f'SRAM write {at:08x}')
  if sram(at,len(data))!=data: raise ValueError(f'SRAM verification failed at {at:08x}')
 def handoff(at,label):
  command(struct.pack('<BI',0x10,at))
  status(label)
 transfer(header,bytes(512))
 transfer(worker,worker_code)
 chunk_nonce=proof_mask+1
 def to_ddr(at,data):
  global chunk_nonce
  words_count=(len(data)+3)//4
  transfer(source,data.ljust((len(data)+511)&~511,b'\0'))
  request=struct.pack('<3I',chunk_nonce,at,words_count)
  transfer(header,request.ljust(512,b'\0'))
  transfer(header,(request+struct.pack('<I',request_magic)).ljust(512,b'\0'))
  handoff(worker,'Write and check head probe data')
  deadline=time.monotonic()+2
  time.sleep(.04)
  while True:
   sample=struct.unpack_from('<4I',sram(out,512))
   if sample[0]==chunk_nonce and sample[1]!=1: break
   if time.monotonic()>=deadline: raise ValueError(f'No DDR receipt at {at:08x}: {sample!r}')
   time.sleep(.02)
  assert sample==(chunk_nonce,2,at,words_count),f'Wrong DDR receipt: {sample!r}'
  chunk_nonce+=1
 receipt=json.loads((root/'adfu-x70-recovery-raw-upload.json').read_text())
 assert receipt['files']==loaded_hashes and receipt['physical_addresses']==addresses
 observations=[]
 for name,where,start,stop,cell,nonce,pages,begin,end in cases:
  transfer(stop,end)
  transfer(start,begin)
  for at,page in pages.items(): to_ddr(at,page)
  print(f'Executing to {name} at 0x{where:08x}',flush=True)
  handoff(start,f'MMU probe {name}')
  deadline=time.monotonic()+6
  time.sleep(.25)
  while True:
   report=struct.unpack_from('<16I',sram(mmu_report,512),cell-mmu_report)
   if report[0]==nonce and report[1]==2: break
   if time.monotonic()>=deadline: raise ValueError(f'No return at {where:08x}: {report!r}')
   time.sleep(.20)
  obs={'stage':name,'checkpoint':where,'cpsr':report[11],'sctlr_after_return':report[12],
       'cpuid':report[8],'sctlr_across_mmu':report[13],
       'ttbr0':report[14],'r2':report[10],'r0':report[15]}
  observations.append(obs)
  (root/'adfu-x70-kernel-machine-probes.json').write_text(json.dumps(observations,indent=2))
  print(f'Checkpoint returned: {obs!r}',flush=True)

finally:
 if h.value: Free(h)
 if f: Close(f)
