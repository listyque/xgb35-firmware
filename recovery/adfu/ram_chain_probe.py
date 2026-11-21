"""Cold SRAM chain test: RAM-backed NAND reads, original SRAM destinations, no NAND writes."""
import argparse
import hashlib
import json
import secrets
import struct
import time
import sys
from build_autonomous_service import ROOT, Arm, mov, pack, unpack
from build_service_nand_candidate import checksum_section
from rom_usb import ROM
from service_usb import Service

parser=argparse.ArgumentParser()
parser.add_argument('--case',choices=('bad-header','bad-payload','success'),required=True)
parser.add_argument('--dry-run',action='store_true')
args=parser.parse_args()
folder=ROOT/'autonomous-service-nand-candidate'
logical=(folder/'logical-boot-image.bin').read_bytes()
manifest=json.loads((folder/'manifest.json').read_text())
assert hashlib.sha256(logical).hexdigest()==manifest['logical_image_sha256']
image=logical[0x1000:]
packed=pack(image)
assert unpack(packed,len(image))==image
PACKED=(0xb406f800-len(packed)) & ~3
HEADER, PROVIDER, STATE, PROOF=0xb4069400,0xb4068500,0xb4068470,0xb4068340
assert PACKED>=0xb406a400
# Prove the entire in-place expansion cannot overwrite unread compressed bytes.
consumed=produced=0
while consumed<len(packed):
    token=packed[consumed];consumed+=1
    if token<128:
        produced+=token+1;consumed+=token+1
    else:
        produced+=((token>>4)&7)+3;consumed+=1
    assert consumed==len(packed) or 0xb4068800+produced<=PACKED+consumed
header=bytearray(logical[:0xc00])
assert struct.unpack_from('<I',header,0x1a8)[0]==0xffff2a00
struct.pack_into('<I',header,0x1a8,PROVIDER)
assert header[0x6e4:0x6ec]==struct.pack('<2I',*mov(12,0xffff2a00))
struct.pack_into('<2I',header,0x6e4,*mov(12,PROVIDER))
struct.pack_into('<I',header,0xbfc,checksum_section(header[0x600:0xc00]))
struct.pack_into('<I',header,0x1fc,(0x1234+sum(struct.unpack('<127I',header[:508])))&0xffffffff)
if args.case=='bad-header':
    header[0x220]^=1  # fail the factory metadata checksum, before relocation
nonce=secrets.randbits(32)
a=Arm(PROVIDER)
e=a.emit
e(0xe92d4ff0,0xe351000f)
a.jump('return',0)
e(0xe3510004)
a.jump('fault',1)
e(0xe1d020b0,0xe1d030b2,0xe0822683,0xe5908008,*mov(4,PROOF),
  0xe5949004,0xe2899001,0xe5849004,0xe5842008,0xe584800c)
# Row zero, 1 KiB-aligned reads only. Validate destinations before any copy.
e(*mov(3,0x3ff),0xe1120003)
a.jump('fault',1)
e(*mov(3,0xc00),0xe1520003)
a.jump('payload',2)
e(*mov(3,0xb4068800),0xe0833002,0xe1580003)
a.jump('fault',1)
e(*mov(0,HEADER),0xe0800002,0xe1a01008,*mov(2,256))
a.label('header_copy')
e(0xe4903004,0xe4813004,0xe2522001)
a.jump('header_copy',1)
a.jump('return')
a.label('payload')
e(*mov(3,0x1000),0xe1520003)
a.jump('fault',3)
e(*mov(3,0x8000),0xe1520003)
a.jump('fault',2)
e(*mov(3,0xb4067800),0xe0833002,0xe1580003)
a.jump('fault',1)
e(*mov(0,STATE),0xe5901000,0xe3510001)
a.jump('return',0)
e(*mov(3,0x1000),0xe1520003)
a.jump('fault',1)
e(*mov(0,PACKED),*mov(1,0xb4068800),*mov(5,PACKED+len(packed)),*mov(6,0xb406f800))
a.label('token')
e(0xe1500005)
a.jump('unpacked',0)
a.jump('fault',8)
e(0xe4d02001,0xe3520080)
a.jump('match',2)
e(0xe2822001,0xe0803002,0xe1530005)
a.jump('fault',8)
e(0xe0813002,0xe1530006)
a.jump('fault',8)
a.label('literal')
e(0xe4d03001,0xe4c13001,0xe2522001)
a.jump('literal',1)
a.jump('token')
a.label('match')
e(0xe1500005)
a.jump('fault',2)
e(0xe202700f,0xe4d03001,0xe1833407,0xe2833001,
  0xe1a02222,0xe2022007,0xe2822003,0xe0817002,0xe1570006)
a.jump('fault',8)
e(0xe0413003,*mov(7,0xb4068800),0xe1530007)
a.jump('fault',3)
a.label('match_copy')
e(0xe4d37001,0xe4c17001,0xe2522001)
a.jump('match_copy',1)
a.jump('token')
a.label('unpacked')
e(0xe1510006)
a.jump('fault',1)
if args.case=='bad-payload':
    e(*mov(0,0xb406f7f0),0xe5901000,0xe2211001,0xe5801000)
e(*mov(0,STATE),0xe3a01001,0xe5801000,*mov(0,0xb4068800))
a.label('clean_expanded')
e(0xee070f3a,0xe2800020,0xe1500006)
a.jump('clean_expanded',3)
e(0xf57ff04f)
a.label('return')
e(*mov(0,PROOF),0xee070f3a,0xf57ff04f,0xe3a00000,0xe8bd8ff0)
a.label('fault')
e(*mov(0,PROOF),*mov(1,0xbad),0xe5801010,0xee070f3a,0xf57ff04f,
  *mov(12,0xffff4a00),0xe12fff1c)
provider=a.finish()
assert len(provider)<=0x300, len(provider)

a=Arm(0xb4068800)
e=a.emit
e(0xf10c00c0,*mov(0,PROOF),*mov(1,nonce),0xe5801000,0xe3a01000,
  0xe5801004,0xe5801008,0xe580100c,0xe5801010,*mov(0,STATE),0xe5801000)
# The factory sector checks the NAND boot path and accesses geometry registers.
# Set clocks/registers only; the read dispatcher is replaced by the RAM provider.
e(*mov(4,0xb0150000),0xe59400a0,0xe3800010,0xe58400a0,
  *mov(0,0x210a),0xe584000c,*mov(0,2048))
a.label('pll_wait')
e(0xe2500001)
a.jump('pll_wait',1)
e(0xe3a00001,0xe584004c,*mov(4,0xb0320018),0xe3a00000,0xe5840000,0xf57ff04f)
for label,src,dst,size in (('provider',0xb406a000,PROVIDER,len(provider)),('sector',HEADER,0xb4068000,512)):
    e(*mov(0,src),*mov(1,dst),*mov(2,size//4))
    a.label(label)
    e(0xe4903004,0xe4813004,0xe2522001)
    a.jump(label,1)
e(*mov(0,0xb4068000),*mov(1,0xb4068800))
a.label('clean')
e(0xee070f3a,0xe2800020,0xe1500001)
a.jump('clean',3)
e(0xf57ff04f,0xee070f15,0xf57ff04f,0xf57ff06f,*mov(12,0xb4068000),0xe12fff1c)
entry=a.finish()
assert len(entry)<=0x400
sys.path.insert(0,str(ROOT/'python-analysis'))
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
cs=Cs(CS_ARCH_ARM,CS_MODE_ARM)
for address,data in ((PROVIDER,provider),(0xb4068800,entry)):
    assert len(list(cs.disasm(data,address)))*4==len(data)
assert any(i.mnemonic=='ldrh' and i.op_str=='r3, [r0, #2]' for i in cs.disasm(provider,PROVIDER))
output=ROOT/'nand-read-validation'
plan={'case':args.case,'candidate_sha256':manifest['logical_image_sha256'],
      'nonce':nonce,'provider_bytes':len(provider),'entry_bytes':len(entry),'compressed_bytes':len(packed),
      'source':'RAM-backed reads; physical NAND is neither read nor written',
      'limits':'BootROM power-on NAND search/ECC and true NAND read timings are not emulated'}
(output/f'ram-chain-{args.case}-plan.json').write_text(json.dumps(plan,indent=2))
if args.dry_run:
    print(json.dumps(plan,indent=2))
    raise SystemExit(0)
with ROM() as usb:
    assert usb.read(0xb0150008,4)==struct.pack('<I',0x0a), 'Requires DDR before initialization'
    for address,data in ((HEADER,header),(0xb406a000,provider),(PACKED,packed),(0xb4068800,entry)):
        print(f'Upload {address:08x}: {len(data)} bytes',flush=True)
        for off in range(0,len(data),1024):
            chunk=bytes(data[off:off+1024])
            usb.write(address+off,chunk)
            time.sleep(.025)
            assert usb.read(address+off,len(chunk))==chunk
    print('All SRAM segments verified; executing chain',flush=True)
    usb.execute(0xb4068800)
time.sleep(3)
with (Service() if args.case=='success' else ROM()) as usb:
    proof=usb.read(PROOF,32)
    actual_nonce,reads,last_offset,last_destination,error=struct.unpack_from('<5I',proof)
    assert actual_nonce==nonce and error==0, proof.hex()
    assert reads==(3 if args.case=='bad-header' else 31), proof.hex()
    result={**plan,'proof':proof.hex(),'read_calls':reads,'last_offset':last_offset,
            'last_destination':last_destination,'provider_error':error,
            'ROM_ADFU_returned':args.case!='success','USB_service_returned':args.case=='success'}
    if args.case=='success':
        report=usb.read(0x01f00000,64)
        result['DDR_report']=report.hex()
        assert struct.unpack_from('<4I',report)==(0x41555431,3,0,0)
        assert usb.read(0xb4068c00,12)==bytes.fromhex('010000ea00f020e3008806b4')
        for n in range(8):
            value=hashlib.sha256(struct.pack('<I',n)).digest()*32
            usb.write(0x01000000,value)
            assert usb.read(0x01000000,len(value))==value
        result['DDR_USB_rounds']=8
    else:
        assert usb.read(0xffff4a00,16)==(ROOT/'atm7051h-high-rom-ffff0000-ffff7fff.bin').read_bytes()[0x4a00:0x4a10]
    (output/f'ram-chain-{args.case}-result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
