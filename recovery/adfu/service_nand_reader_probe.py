"""Exercise the candidate's actual 28-sector NAND loop against factory data, in RAM."""
import argparse
import hashlib
import json
import secrets
import struct
import time
from build_autonomous_service import ROOT, Arm, mov, branch
from service_usb import Service

BASE, REPORT, DATA = 0x01000000, 0x01002000, 0x01008000
parser=argparse.ArgumentParser()
parser.add_argument('--row',type=int,choices=(0,256),default=0)
parser.add_argument('--bad-checksum',action='store_true')
parser.add_argument('--dry-run',action='store_true')
args=parser.parse_args()
candidate=ROOT/'autonomous-service-nand-candidate'
logical=(candidate/'logical-boot-image.bin').read_bytes()
manifest=json.loads((candidate/'manifest.json').read_text())
assert hashlib.sha256(logical).hexdigest()==manifest['logical_image_sha256']
reader=bytearray(logical[0x680:0x798])
assert len(reader)==0x118
expected=(ROOT/'xgb35-mbrec-recovered.bin').read_bytes()[0x1000:0x8000]
checksum=sum(struct.unpack('<7168I',expected)) & 0xffffffff
original_sum=sum(struct.unpack('<7168I',logical[0x1000:0x8000])) & 0xffffffff
def replace(offset,reg,before,after):
    assert reader[offset:offset+8]==struct.pack('<2I',*mov(reg,before))
    struct.pack_into('<2I',reader,offset,*mov(reg,after))
replace(0x10,6,0xb40683e0,REPORT+0x100)
replace(0x18,7,0xb4068800,DATA)
replace(0xa8,0,0xb4068800,DATA)
replace(0xcc,3,original_sum,checksum ^ int(args.bad_checksum))
replace(0xdc,0,0xb406c7f0,REPORT+0x80)
replace(0x100,12,0xb406f500,BASE+0xa00)
replace(0x10c,12,0xffff4a00,BASE+0xa20)
payload=bytearray(0xc00)
rom=(ROOT/'atm7051h-high-rom-ffff0000-ffff7fff.bin').read_bytes()
assert hashlib.sha256(rom).hexdigest()=='e83e5ac0d25025a7eb948a796636bc7ff11a7a94f6904d1508434034a094757c'
payload[0x400:0x69c]=rom[0x2d88:0x3024]
assert struct.unpack_from('<I',payload,0x430)[0]==0xe5874000
struct.pack_into('<I',payload,0x430,0xe1a00000)
payload[0x800:0x918]=reader
nonce=secrets.randbits(32)
a=Arm(BASE)
e=a.emit
def call(address):
    e(*mov(12,address),0xe12fff3c)
e(0xe92d5ff0,*mov(4,REPORT),*mov(0,nonce),0xe5840000,0xe3a00001)
call(BASE+0x400)
e(*mov(4,0xb0150000),0xe594000c,*mov(5,REPORT),0xe585000c,
  *mov(0,0x210a),0xe584000c,0xf57ff04f,*mov(0,2048))
a.label('pll')
e(0xe2500001)
a.jump('pll',1)
e(0xe3a00001,0xe584004c,0xf57ff04f,0xe3a00002)
call(0xffff2ccc)
call(0xffff2d1c)
e(*mov(4,REPORT),0xe5840010,*mov(4,0xb4068444),*mov(0,0x20000002),0xe5840000,
  *mov(4,0xb0320008),0xe3a00021,0xe5840000,*mov(5,1024),*mov(10,8192),
  *mov(9,args.row),*mov(12,BASE+0x800),0xe12fff1c)
code=a.finish()
assert len(code)<=0x400
payload[:len(code)]=code
for off,result in ((0xa00,1),(0xa20,2)):
    struct.pack_into('<2I',payload,off,0xe3a00000|result,branch(BASE+off+4,BASE+0xb00))
a=Arm(BASE+0xb00)
e=a.emit
e(*mov(4,REPORT),0xe5840004,0xe5842008)
call(0xffff2cfc)
call(0xffff2ee8)
e(*mov(4,REPORT),0xe594000c,*mov(5,0xb0150000),0xe585000c,
  0xe1a00004,0xee070f3a,0xf57ff04f,0xe3a00000,0xe8bd9ff0)
code=a.finish()
assert len(code)<=0x100
payload[0xb00:0xb00+len(code)]=code
if args.dry_run:
    print('Exact 280-byte reader loop preserved; only RAM destinations, reference checksum and return targets patched')
    raise SystemExit(0)
with Service() as usb:
    assert usb.read(0xb4069efc,4)==struct.pack('<I',branch(0xb4069efc,0xb406a440))
    for off in range(0,len(payload),1024):
        usb.write(BASE+off,bytes(payload[off:off+1024]))
    assert usb.read(BASE,len(payload))==payload
    usb.write(REPORT,b'\xff'*0x110)
    for off in range(0,len(expected),4096):
        usb.write(DATA+off,b'\xa5'*4096)
    usb.execute(BASE)
    time.sleep(.2)
    report=usb.read(REPORT,32)
    assert struct.unpack_from('<I',report)[0]==nonce
    result,actual_sum,_,reset=struct.unpack_from('<4I',report,4)
    data=usb.read(DATA,len(expected))
    info={'row':args.row,'bad_checksum':args.bad_checksum,'result':result,'reset':reset,
          'all_28_sectors_match':data==expected,'checksum':actual_sum,
          'candidate_sha256':manifest['logical_image_sha256'],
          'data_sha256':hashlib.sha256(data).hexdigest(),
          'scope':'candidate read loop; relocated to DDR, factory content, RAM return stubs; no NAND writes'}
    out=ROOT/'nand-read-validation'
    (out/f'candidate-reader-row{args.row}-bad{int(args.bad_checksum)}.json').write_text(json.dumps(info,indent=2))
    print(json.dumps(info,indent=2))
    assert reset==0 and data==expected and actual_sum==checksum
    assert result==(2 if args.bad_checksum else 1)
