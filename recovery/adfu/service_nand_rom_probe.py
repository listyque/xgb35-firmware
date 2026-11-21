"""Run the BootROM NAND read path from RAM and return to the diagnostic USB loop."""
import hashlib
import json
import secrets
import struct
import sys
import time
import argparse
from contextlib import nullcontext
from build_autonomous_service import ROOT, Arm, mov, branch
from service_usb import Service
from rom_usb import ROM

def probe(args, attached=None):
    boot_clock = getattr(args, 'boot_clock', False)
    base,trace,destination=0x01000000,0x01002000,0x01003000
    if args.rom:
        # Stay above the legacy raw dumper's 0x8a40..0xad28 output buffer.
        base,trace,destination=0xb406b800,0xb406c800,0xb406c900
    rom=(ROOT/'atm7051h-high-rom-ffff0000-ffff7fff.bin').read_bytes()
    assert hashlib.sha256(rom).hexdigest()=='e83e5ac0d25025a7eb948a796636bc7ff11a7a94f6904d1508434034a094757c'
    payload=bytearray(0xc00)
    payload[0x400:0x69c]=rom[0x2d88:0x3024]
    assert struct.unpack_from('<I',rom,0x2db8)[0]==0xe5874000
    struct.pack_into('<I',payload,0x430,0xe1a00000)  # retain the current USB PLL selection
    payload[0x800:0xaac]=rom[0x3598:0x3844]
    for i,(literal,target) in enumerate(((0x3824,0xffff337c),(0x3828,0xffff3224),
                                       (0x3838,0xffff31d4),(0x383c,0xffff32cc))):
        off=0xb00+i*0x40
        assert struct.unpack_from('<I',rom,literal)[0]==target
        struct.pack_into('<I',payload,0x800+literal-0x3598,base+off)
        ops=[0xe92d4010,*mov(12,target),0xe12fff3c,0xe5890000+i*20,*mov(2,0xb0320000)]
        for j,reg in enumerate((4,0x38,0x3c,0x68),1):
            ops.extend((0xe5923000|reg,0xe5893000|(i*20+4*j)))
        ops.append(0xe8bd8010)
        assert len(ops)==16
        struct.pack_into('<16I',payload,off,*ops)
    a=Arm(base)
    e=a.emit
    nonce=secrets.randbits(32)
    e(0xe92d5ff0,*mov(9,trace),*mov(0,nonce),0xe5991078,0xe1500001,0x08bd9ff0,
      0xe5890078,0xe5890058,0xe3a00001,0xe5890050)
    def call(address):
        e(*mov(12,address),0xe12fff3c)
    call(base+0x400)
    e(*mov(4,0xb0150000),0xe594000c,0xe5890074)
    if not boot_clock:
        e(*mov(0,0x210a),0xe584000c,0xf57ff04f,*mov(0,2048))
        a.label('pll_delay')
        e(0xe2500001)
        a.jump('pll_delay',1)
        e(0xe3a00001,0xe584004c,0xf57ff04f)
    e(0xe594004c,0xe589007c)
    e(0xe3a00002,0xe5890050)
    call(0xffff2ccc)
    call(0xffff2d1c)
    e(0xe5890054,*mov(4,0xb4068400),*mov(0,(args.row<<16)|(args.sector*1024)),0xe5840000,
      0xe3a00000,0xe5840004,
      *mov(0,0x20000000|args.flags),0xe5840044,*mov(0,destination),0xe5840008,
      *mov(5,0xb0320000),0xe3a00021,0xe5850008)
    e(0xe3a00003,0xe5890050,*mov(0,destination),0xe3a01020)
    a.label('clean')
    e(0xee070f3e,0xe2800020,0xe2511001)
    a.jump('clean',1)
    e(0xf57ff04f,0xe1a00004)
    call(base+0x800)
    e(0xe589005c,0xe3a00004,0xe5890050,*mov(0,destination),0xe3a01020)
    a.label('invalidate')
    e(0xee070f36,0xe2800020,0xe2511001)
    a.jump('invalidate',1)
    e(0xf57ff04f)
    for i,offset in enumerate((0,8,0x14,0x18,0x5c)):
        e(0xe5950000|offset,0xe5890000|(96+i*4))
    call(0xffff2cfc)
    call(0xffff2ee8)
    e(*mov(4,0xb0150000),0xe5990074,0xe584000c,0xf57ff04f)
    e(0xe3a00005,0xe5890050,*mov(0,trace),0xe3a01004)
    a.label('report_clean')
    e(0xee070f3a,0xe2800020,0xe2511001)
    a.jump('report_clean',1)
    e(0xf57ff04f,0xe3a00000,0xe8bd9ff0)
    code=a.finish()
    assert len(code)<=0x400
    payload[:len(code)]=code
    sys.path.insert(0,str(ROOT/'python-analysis'))
    from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
    assert len(list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(code,base)))==len(a.ops)
    if args.dry_run:
        return None
    with (nullcontext(attached) if attached is not None else (ROM() if args.rom else Service())) as usb:
        if not args.rom:
            assert usb.read(0xb4069efc,4)==struct.pack('<I',branch(0xb4069efc,0xb406a440))
        for off in range(0,len(payload),1024):
            usb.write(base+off,bytes(payload[off:off+1024]))
            if args.rom:
                time.sleep(.025)
        assert usb.read(base,len(payload))==payload
        usb.write(trace,b'\xff'*128)
        usb.write(destination,b'\xa5'*1024)
        usb.execute(base)
        time.sleep(.025 if args.rom or args.backup_check else .2)
        report=usb.read(trace,128)
        data=usb.read(destination,1024)
        assert struct.unpack_from('<I',report,88)[0]==nonce
        phase,reset=struct.unpack_from('<2I',report,80)
        offset=(args.row%256)*8192+args.sector*1024
        expected=(ROOT/'xgb35-mbrec-recovered.bin').read_bytes()[offset:offset+1024] if args.row%256<4 else None
        raw_name='adfu-nand-first-256-pages-raw-with-oob.bin' if args.row<256 else 'adfu-nand-blocks-1-3-raw-with-oob.bin'
        offset=(args.row%256)*8936+args.sector*1024
        raw=(ROOT/raw_name).read_bytes()[offset:offset+1024]
        result={'flags':args.flags,'row':args.row,'sector':args.sector,'phase':phase,'reset_result':reset,'sector_matches_factory':data==expected if expected is not None else None,
                'boot_clock':boot_clock,'nand_pll_before':struct.unpack_from('<I',report,0x74)[0],
                'nand_clock_during_probe':struct.unpack_from('<I',report,0x7c)[0],
                'matches_raw':data==raw,'first_bytes':data[:32].hex(),
                'control_registers':list(struct.unpack_from('<5I',report,96)),
                'sentinel_unchanged':data==b'\xa5'*1024,
                'wait_results':[list(struct.unpack_from('<5I',report,i*20)) for i in range(4)]}
        assert phase==5
        assert reset==0 and all(values[0]==0 for values in result['wait_results']), result
        if args.flags==2 and expected is not None and not boot_clock:
            assert data==expected, result
        return result, data, report


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--flags',type=int,choices=(0,1,2,3),default=2)
    parser.add_argument('--row',type=int,choices=range(512),default=0)
    parser.add_argument('--sector',type=int,choices=range(8),default=0)
    parser.add_argument('--rom',action='store_true',help='Run directly from BootROM ADFU in SRAM')
    parser.add_argument('--backup-check',action='store_true',help='Save full-block backup verification evidence separately')
    parser.add_argument('--dry-run',action='store_true')
    args=parser.parse_args()
    value=probe(args)
    if args.dry_run:
        print('ROM reader copy, wait hooks and RAM boundaries verified')
        return
    result,data,report=value
    output=ROOT/('nand-write-preparation/decoded' if args.rom or args.backup_check else 'nand-read-validation')
    output.mkdir(exist_ok=True,parents=True)
    label=f'rom-flags{args.flags}-row{args.row}-sector{args.sector}'
    (output/f'{label}-report.bin').write_bytes(report)
    (output/f'{label}-sector.bin').write_bytes(data)
    (output/f'{label}.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
