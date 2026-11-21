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
 raw_kernel='--raw-kernel' in sys.argv
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

 boot=Arm()
 boot.emit(0xe92d41f0,*mov32(5,0xb0150000),0xe5950008,0xe3500f4f,0x18bd81f0)
 boot.emit(0xe5956000,0xe595706c,0xe595801c,*enable_ddr_clocks)
 boot.emit(*mov32(3,addresses[boot_name]),0xe5930000|boot_word_offset,
           *mov32(1,expected_boot_word),0xe1500001)
 boot.jump('bad_image',1)
 boot.emit(*mov32(2,addresses['ram-kernel.dtb']),0xe5920000,*mov32(1,0xedfe0dd0),
           0xe1500001)
 boot.jump('bad_image',1)
 boot.emit(*mov32(4,out),*mov32(0,0x58424f4b),0xe5840020,0xe2844020,0xee074f3a,0xf57ff04f)
 # ARM Linux entry. Only verified physical DDR addresses are used across the MMU change.
 boot.emit(0xf10c00c0,0xf1020013,0xf57ff04f,0xee110f10,
           0xe3c00005,0xe3c00a01,0xee010f10,0xf57ff06f,
           0xe3a00000,0xee070f15,0xee070fd5,0xf57ff04f,0xf57ff06f,
           0xe3e01000,0xe12fff13)
 boot.label('bad_image')
 boot.emit(0xe585801c,0xe585706c,0xe5856000,0xf57ff04f,0xe8bd81f0)
 boot_code=boot.pack()
 if '--dry-run' in sys.argv:
  sys.path.insert(0,str(root/'python-analysis'))
  from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
  dis=Cs(CS_ARCH_ARM,CS_MODE_ARM)
  loader_ins=list(dis.disasm(boot_code[:len(boot.ops)*4],loader))
  worker_ins=list(dis.disasm(worker_code[:len(arm.ops)*4],worker))
  assert len(loader_ins)==len(boot.ops) and len(worker_ins)==len(arm.ops)
  assert sum(ins.mnemonic=='cmp' and ins.op_str=='r0, #0x13c' for ins in worker_ins)==1
  assert any(ins.mnemonic=='bx' and ins.op_str=='r3' for ins in loader_ins)
  for ins in worker_ins[:22]: print(f'{ins.address:08x} {ins.mnemonic} {ins.op_str}')
  for ins in loader_ins: print(f'{ins.address:08x} {ins.mnemonic} {ins.op_str}')
  print('Verified manifest:',{name:len(blob) for name,blob in blobs.items()})
  sys.exit()
 command(bytes((0xcc,0,0,0,0,0,0,18)),18,0x80)
 if read_data(18,'IDENT')!=bytes.fromhex('004341444655445202450000000000000000'): raise ValueError('unexpected target')
 def transfer(at,data):
  command(struct.pack('<BII',5,at,len(data)),len(data))
  write(data)
  status(f'SRAM write {at:08x}')
  if sram(at,len(data))!=data: raise ValueError(f'SRAM verification failed at {at:08x}')
 def handoff(at,label):
  command(struct.pack('<BI',0x10,at))
  status(label)
 state=root/('adfu-x70-recovery-raw-upload.json' if raw_kernel else 'adfu-x70-recovery-upload.json')
 if '--launch' in sys.argv:
  saved=json.loads(state.read_text())
  assert saved['files']==loaded_hashes and saved['physical_addresses']==addresses
  report=struct.unpack_from('<4I',sram(out,512))
  assert report==tuple(saved['final_receipt']), 'DDR verification receipt is no longer resident'
  transfer(loader,boot_code)
  print(f'Executing X70 {boot_name} from 0x{addresses[boot_name]:08x} now',flush=True)
  handoff(loader,'X70 ARM boot handoff')
  print('ADFU accepted kernel handoff',flush=True)
  sys.exit()
 if state.exists(): state.unlink()
 transfer(header,bytes(512))
 transfer(worker,worker_code)
 global_chunks=0
 start=time.monotonic()
 for name,address in addresses.items():
  blob=blobs[name]
  print(f'{name} {len(blob)} bytes at 0x{address:08x}',flush=True)
  for off in range(0,len(blob),maximum):
   current=address+off
   chunk=blob[off:off+maximum]
   block=chunk.ljust((len(chunk)+511)&~511,b'\0')
   words_count=(len(chunk)+3)//4
   nonce=proof_mask+1+global_chunks
   transfer(source,block)
   request=struct.pack('<3I',nonce,current,words_count)
   transfer(header,request.ljust(512,b'\0'))
   transfer(header,(request+struct.pack('<I',request_magic)).ljust(512,b'\0'))
   handoff(worker,'Write and byte-compare DDR chunk')
   deadline=time.monotonic()+2
   time.sleep(.04)
   while True:
    receipt=struct.unpack_from('<4I',sram(out,512))
    if receipt[0]==nonce and receipt[1]!=1: break
    if time.monotonic()>=deadline:
     raise ValueError(f'DDR receipt missing for {name}+0x{off:x}: {receipt!r}')
    time.sleep(.01)
   if receipt!=(nonce,2,current,words_count):
    raise ValueError(f'DDR verification failed for {name}+0x{off:x}: {receipt!r}')
   global_chunks+=1
   if off==0 or (off+maximum)%(256*1024)==0 or off+len(chunk)==len(blob):
    print(f'  verified {off+len(chunk)}/{len(blob)} ({time.monotonic()-start:.1f}s total)',flush=True)
 state.write_text(json.dumps({'files':loaded_hashes,'physical_addresses':addresses,
                              'final_receipt':receipt,'chunks':global_chunks},indent=2))
 print(f'All {global_chunks} chunks compared after DDR cache clean/invalidate',flush=True)

finally:
 if h.value: Free(h)
 if f: Close(f)
