"""Trigger the NAND candidate's geometry failure at its SRAM address; expect ROM ADFU."""
import hashlib
import json
import secrets
import struct
import sys
import time
from build_autonomous_service import ROOT, Arm, mov, branch
from service_usb import Service
from rom_usb import ROM

folder=ROOT/'autonomous-service-nand-candidate'
manifest=json.loads((folder/'manifest.json').read_text())
logical=(folder/'logical-boot-image.bin').read_bytes()
assert hashlib.sha256(logical).hexdigest()==manifest['logical_image_sha256']
reader=logical[0x680:0x798]
assert reader[-12:]==struct.pack('<3I',*mov(12,0xffff4a00),0xe12fff1c)
cold='--cold' in sys.argv
BASE, PROOF=(0xb4068800 if cold else 0x01000000),0xb406d200
nonce=secrets.randbits(32)
a=Arm(BASE)
e=a.emit
e(0xf10c00c0,*mov(0,PROOF),*mov(1,nonce),0xe5801000,0xe10f1000,0xe5801004,
  0xee111f10,0xe5801008,0xee070f3a,0xf57ff04f,
  *mov(0,BASE+0x100),*mov(1,0xb4068200),*mov(2,len(reader)//4))
a.label('copy')
e(0xe4903004,0xe4813004,0xe2522001)
a.jump('copy',1)
e(*mov(0,0xb4068200),*mov(1,0xb4068200+len(reader)))
a.label('clean')
e(0xee070f3a,0xe2800020,0xe1500001)
a.jump('clean',3)
e(0xf57ff04f,0xee070f15,0xf57ff04f,0xf57ff06f,*mov(13,0xb4070000),
  0xe3a05000,*mov(10,8192),*mov(12,0xb4068200),0xe12fff1c)
code=a.finish()
assert len(code)<=0x100
payload=code+bytes(0x100-len(code))+reader
if '--dry-run' in sys.argv:
    print('Original reader/fallback retained; r5=0 forces failure before any NAND read')
    raise SystemExit(0)
with (ROM() if cold else Service()) as usb:
    if cold:
        assert usb.read(0xb0150008,4)==struct.pack('<I',0x0a)
    else:
        assert usb.read(0xb4069efc,4)==struct.pack('<I',branch(0xb4069efc,0xb406a440))
    usb.write(BASE,payload)
    assert usb.read(BASE,len(payload))==payload
    usb.write(PROOF,bytes(32))
    usb.execute(BASE)
time.sleep(3)
with Service() as usb:
    def rom(cdb,size):
        usb.send(struct.pack('<4sIIBBB16s',b'USBC',0,size,128,0,16,cdb))
        value=usb.receive(size)
        usb.status()
        return value
    ident=rom(bytes((0xcc,0,0,0,0,0,0,18)),18)
    assert ident==bytes.fromhex('004341444655445202450000000000000000')
    proof=rom(struct.pack('<BIIB',5,PROOF,32,128),32)
    assert struct.unpack_from('<I',proof)[0]==nonce
    # A fresh ROM command must be functional, not merely an enumeration response.
    assert rom(struct.pack('<BIIB',5,0xffff4a00,16,128),16)==(ROOT/'atm7051h-high-rom-ffff0000-ffff7fff.bin').read_bytes()[0x4a00:0x4a10]
    result={'cold_ROM_context':cold,'ROM_ADFU_identified':True,'SRAM_nonce_matches':True,'ROM_memory_read_works':True,
            'candidate_sha256':manifest['logical_image_sha256'],'proof':proof.hex(),
            'failure':'wrong ECC step (r5=0), exact reader at 0xb4068200',
            'NAND_writes':False}
    (ROOT/f'nand-read-validation/native-adfu-fallback-cold{int(cold)}.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
