"""Restore BootROM exception routes/stacks before leaving the running RAM USB service."""
import hashlib
import json
import secrets
import struct
import sys
import time
from build_autonomous_service import ROOT, Arm, mov
from service_usb import Service
from rom_usb import ROM

rom=(ROOT/'atm7051h-high-rom-ffff0000-ffff7fff.bin').read_bytes()
assert hashlib.sha256(rom).hexdigest()=='e83e5ac0d25025a7eb948a796636bc7ff11a7a94f6904d1508434034a094757c'
BASE,PROOF=0xb406d000,0xb406d400
nonce=secrets.randbits(32)
a=Arm(BASE)
e=a.emit
e(0xf10c00c0,*mov(4,PROOF),*mov(0,nonce),0xe5840000,
  0xee110f10,0xe5840004,*mov(5,0xb0158058))
e(*mov(0,0xb0200000),0xe5901000,0xe5841024,0xe3c11701,0xe5801000,
  0xf57ff04f,0xee070f15,0xf57ff04f,0xf57ff06f,
  *mov(0,0xffff0000),0xe5901000,0xe5841028)
for i in range(7):
    e(0xe5950000|i*4,0xe5840000|(8+i*4))
    target=struct.unpack_from('<I',rom,0xac0+i*4)[0]
    assert target==0xffff0000+(0x420,0x440,0x460,0x480,0x4a0,0x300,0x400)[i]
    e(*mov(0,target),0xe5850000|i*4)
e(0xe1a00004,0xee070f3a,0xe2800020,0xee070f3a,0xf57ff04f)
for mode,literal in ((0xd2,0xaa8),(0xdf,0xaac),(0xd1,0xab0),
                     (0xd7,0xab4),(0xdb,0xab8),(0xd3,0xabc)):
    stack=struct.unpack_from('<I',rom,literal)[0]
    assert 0xb406f800<=stack<0xb4070000
    e(0xe321f000|mode,*mov(13,stack))
e(*mov(14,0xffff01cc),*mov(12,0xffff4a00),0xe12fff1c)
payload=a.finish()
assert len(payload)<0x400
if '--dry-run' in sys.argv:
    print(f'{len(payload)} bytes; seven ROM exception routes and six banked stacks restored')
    raise SystemExit(0)
with Service() as usb:
    assert usb.read(0xb4068c00,12)==bytes.fromhex('010000ea00f020e3008806b4')
    usb.write(BASE,payload)
    assert usb.read(BASE,len(payload))==payload
    usb.write(PROOF,bytes(64))
    usb.execute(BASE)
time.sleep(3)
with ROM() as usb:
    proof=usb.read(PROOF,64)
    assert struct.unpack_from('<I',proof)[0]==nonce
    assert usb.read(0xffff4a00,16)==rom[0x4a00:0x4a10]
    result={'ROM_ADFU_returned':True,'nonce_matches':True,'memory_reads_work':True,
            'previous_exception_routes':[hex(v) for v in struct.unpack_from('<7I',proof,8)],
            'SCTLR':hex(struct.unpack_from('<I',proof,4)[0]),
            'previous_remap':hex(struct.unpack_from('<I',proof,36)[0]),
            'restored_ROM_first_word':hex(struct.unpack_from('<I',proof,40)[0]),
            'NAND_writes':False}
    (ROOT/'nand-read-validation/service-return-to-rom.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
