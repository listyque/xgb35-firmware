import json
import secrets
import struct
import sys
import time
from build_autonomous_service import ROOT, mov
from service_usb import Service

code_address,report_address=0xb406d000,0xb406d200
nonce=secrets.randbits(32)
ops=[*mov(0,report_address),*mov(1,nonce),0xe5801000,
     0xee070f3a,0xf57ff04f,0xe3a00000,0xe12fff1e]
code=struct.pack('<'+'I'*len(ops),*ops)
sys.path.insert(0,str(ROOT/'python-analysis'))
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
decoded=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(code,code_address))
assert len(decoded)==len(ops) and decoded[-1].mnemonic=='bx'
if '--dry-run' in sys.argv:
    print('Marker/restart stub ARM verified')
    sys.exit(0)
with Service() as usb:
    assert usb.read(0xb4068c00,12)==bytes.fromhex('010000ea00f020e3008806b4')
    from build_autonomous_service import branch
    assert usb.read(0xb4069efc,4)==struct.pack('<I',branch(0xb4069efc,0xb406a440))
    usb.write(code_address,code)
    assert usb.read(code_address,len(code))==code
    usb.write(report_address,bytes(32))
    usb.execute(code_address)
    print('Service execute accepted',flush=True)
    time.sleep(2)
    marker,=struct.unpack('<I',usb.read(report_address,4))
    assert marker==nonce, (hex(marker),hex(nonce))
    assert usb.read(0xb4068c00,12)==bytes.fromhex('010000ea00f020e3008806b4')
    result={'RAM_execution':True,'USB_returned_without_restart':True,'nonce':nonce}
    (ROOT/'service-exec-proof.json').write_text(json.dumps(result,indent=2))
    print(result)
