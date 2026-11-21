"""Read one physical NAND page through the diagnostic RAM USB service. No flash writes."""
import argparse
import hashlib
import json
import secrets
import struct
import time
from build_autonomous_service import ROOT, Arm, mov, branch
from service_usb import Service

BASE, REPORT, DATA = 0x01000000, 0x01002000, 0x01003000


def build(row, nonce, check_wp=False):
    assert 0 <= row < 1024
    a = Arm(BASE)
    e = a.emit
    e(0xe92d5ff0,*mov(9,REPORT),*mov(0,nonce),0xe5890000,
      *mov(10,0xb0150000),*mov(11,0xb0320000))
    for i,reg in enumerate((0xa0,0x4c,0xa8)):
        e(0xe59a0000|reg,0xe5890000|(4+i*4))
    # Restore the known raw dumper's NAND PLL and clock together, not the clock alone.
    e(0xe59a000c,0xe5890018,*mov(0,0x210a),0xe58a000c,0xf57ff04f,
      *mov(0,2048))
    a.label('pll_delay')
    e(0xe2500001)
    a.jump('pll_delay',1)
    e(0xe59a00a0,0xe3800010,0xe58a00a0,0xe3a00001,0xe58a004c,
      0xe59a00a8,0xe3c00008,0xe58a00a8,0xf57ff04f)
    def delay(label):
        e(0xe3a00c01)
        a.label(label)
        e(0xe2500001)
        a.jump(label,1)
    delay('reset_delay')
    e(0xe59a00a8,0xe3800008,0xe58a00a8,0xf57ff04f)
    delay('clock_delay')
    def store(reg,value):
        e(*mov(0,value),0xe58b0000|reg)
    def wait(label,mode,code):
        e(*mov(1,0x400000))
        a.label(label)
        e(0xe59b0004)
        if mode == 'page':
            e(0xe2002288,0xe3520008)
            cond = 0
        elif mode == 'fifo':
            e(0xe3100010)
            cond = 1
        else:
            e(0xe3500000)
            cond = 5
        a.jump(label+'_done',cond)
        e(0xe2511001)
        a.jump(label,1)
        e(0xe3a00000|code)
        a.jump('restore')
        a.label(label+'_done')
    store(0,0x11)
    store(0x20,0xff)
    store(0x24,0x60)
    store(0x38,1)
    wait('reset','idle',1)
    store(0x14,0)
    store(0x18,row)
    store(8,0x20)
    store(0x20,0x3000)
    store(0x24,0x606c)
    store(0x38,1)
    e(0xf57ff04f)
    wait('page','page',2)
    e(*mov(6,DATA),*mov(7,8936//8))
    a.label('read_pair')
    store(0xc,8)
    store(0x24,0x41)
    store(0x38,1)
    wait('fifo','fifo',3)
    e(0xe59b0010,0xe4860004,0xe59b0010,0xe4860004)
    wait('idle','idle',4)
    e(0xe2577001)
    a.jump('read_pair',1)
    if check_wp:
        store(0x20,0x70)
        store(0x0c,1)
        store(0x24,0x61)
        store(0x38,1)
        wait('status_fifo','fifo',5)
        e(0xe59b0010,0xe589001c)
        wait('status_idle','idle',6)
    e(0xe3a00000)
    a.label('restore')
    e(0xe5890010,0xe59b0004,0xe5890014)
    for i,reg in reversed(list(enumerate((0xa0,0x4c,0xa8)))):
        e(0xe5990000|(4+i*4),0xe58a0000|reg)
    e(0xe5990018,0xe58a000c)
    e(0xf57ff04f,*mov(0,DATA),*mov(1,DATA+8936))
    a.label('clean_data')
    e(0xee070f3a,0xe2800020,0xe1500001)
    a.jump('clean_data',3)
    e(*mov(0,REPORT),0xee070f3a,0xf57ff04f,0xe3a00000,0xe8bd9ff0)
    code=a.finish()
    assert len(code)<0x1000 and DATA+8936<0x02000000
    return code


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--row',type=int,choices=range(1024),default=0)
    parser.add_argument('--check-wp',action='store_true')
    parser.add_argument('--dry-run',action='store_true')
    args=parser.parse_args()
    nonce=secrets.randbits(32)
    code=build(args.row,nonce,args.check_wp)
    if args.dry_run:
        import sys
        sys.path.insert(0,str(ROOT/'python-analysis'))
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
        instructions=list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(code,BASE))
        assert len(instructions)*4==len(code)
        commands = 'FF/00/30/70' if args.check_wp else 'FF/00/30'
        print(f'RAM reader: {len(code)} bytes, bounded waits, only NAND {commands} commands')
        return
    with Service() as usb:
        assert usb.read(0xb4069efc,4)==struct.pack('<I',branch(0xb4069efc,0xb406a440))
        usb.write(BASE,code)
        assert usb.read(BASE,len(code))==code
        usb.write(REPORT,b'\xff'*32)
        usb.execute(BASE)
        time.sleep(.2)
        report=usb.read(REPORT,32)
        assert struct.unpack_from('<I',report)[0]==nonce
        error=struct.unpack_from('<I',report,16)[0]
        data=usb.read(DATA,8936)
        name='adfu-nand-first-256-pages-raw-with-oob.bin' if args.row<256 else 'adfu-nand-blocks-1-3-raw-with-oob.bin'
        index=args.row if args.row<256 else args.row-256
        expected=(ROOT/name).read_bytes()[index*8936:(index+1)*8936]
        result={'row':args.row,'error':error,'raw_bit_differences':sum((a^b).bit_count() for a,b in zip(data,expected)),
                'sha256':hashlib.sha256(data).hexdigest(),'report':report.hex()}
        if args.check_wp and error == 0:
            status=report[28]
            result['nand_status']=hex(status)
            result['hardware_write_protected']=not bool(status & 0x80) if status & 0x60 == 0x60 else None
        out=ROOT/'nand-read-validation'
        out.mkdir(exist_ok=True)
        name=f'raw-row{args.row}'+('-wp' if args.check_wp else '')
        (out/f'{name}.bin').write_bytes(data)
        (out/f'{name}.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(result,indent=2))
        assert error==0, result


if __name__=='__main__':
    main()
