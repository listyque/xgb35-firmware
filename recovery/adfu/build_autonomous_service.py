"""Build the SRAM payload for a future NAND boot chain; never access USB/NAND."""
import hashlib
import json
import struct
import sys
import secrets
from collections import defaultdict
from pathlib import Path
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parent
NATIVE, PACKED, ENTRY = 0xB4069000, 0xB406C800, 0xB406F500
REPORT, STACK, PARAM = 0xB406C700, 0xB4069000, 0xB4068800
ROM_STACK_FLOOR = 0xB406F800
DDR_REPORT = 0x01F00000


def pack(data):
    index = defaultdict(list)
    out, literals = bytearray(), bytearray()
    pos = 0
    def flush():
        if literals:
            out.append(len(literals) - 1)
            out.extend(literals)
            literals.clear()
    while pos < len(data):
        length = distance = 0
        for prior in reversed(index[data[pos:pos+3]][-64:]):
            if pos-prior > 4096:
                break
            n = 3
            while n < 10 and pos+n < len(data) and data[prior+n] == data[pos+n]:
                n += 1
            if n > length:
                length, distance = n, pos-prior
        if length >= 3:
            flush()
            distance -= 1
            out.extend((128+((length-3)<<4)+(distance>>8),distance & 255))
            step = length
        else:
            literals.append(data[pos])
            step = 1
            if len(literals) == 128:
                flush()
        for i in range(pos, pos+step):
            index[data[i:i+3]].append(i)
        pos += step
    flush()
    return bytes(out)


def unpack(data, limit):
    out = bytearray()
    pos = 0
    while pos < len(data):
        token = data[pos]
        pos += 1
        if token < 128:
            length = token+1
            assert pos+length <= len(data)
            out.extend(data[pos:pos+length])
            pos += length
        else:
            assert pos < len(data)
            distance = ((token & 15)<<8)+data[pos]+1
            pos += 1
            assert 0 < distance <= len(out)
            for _ in range(((token >> 4) & 7)+3):
                out.append(out[-distance])
        assert len(out) <= limit
    assert len(out) == limit
    return bytes(out)


def mov(reg, value):
    return (0xe3000000 | reg << 12 | (value & 0xf000) << 4 | (value & 0xfff),
            0xe3400000 | reg << 12 | ((value >> 16) & 0xf000) << 4 | ((value >> 16) & 0xfff))


def branch(src, dst, cond=14, link=False):
    delta = dst-src-8
    assert delta % 4 == 0 and -2**25 <= delta < 2**25
    return cond << 28 | 0x0a000000 | int(link) << 24 | ((delta//4) & 0xffffff)


class Arm:
    def __init__(self, base):
        self.base, self.ops, self.labels, self.fixups = base, [], {}, []
    def emit(self, *ops):
        self.ops.extend(ops)
    def label(self, name):
        self.labels[name] = self.base+4*len(self.ops)
    def jump(self, name, cond=14, link=False):
        self.fixups.append((len(self.ops), name, cond, link))
        self.emit(0)
    def call(self, address):
        self.emit(branch(self.base+4*len(self.ops), address, link=True))
    def finish(self):
        for i, name, cond, link in self.fixups:
            self.ops[i] = branch(self.base+4*i, self.labels[name], cond, link)
        return struct.pack('<'+'I'*len(self.ops), *self.ops)


def build():
    mb = (ROOT/'xgb35-mbrec-recovered.bin').read_bytes()
    assert hashlib.sha256(mb).hexdigest() == '34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc'
    with ZipFile(ROOT/'x70-parts.zip') as z:
        service = bytearray(z.read('adfudec.bin'))
    assert hashlib.sha256(service).hexdigest() == '5783ad177372044cb48a133da1c2917fdb0967cb848556212046b7c42a8468c8'
    assert struct.unpack_from('<I', service, 0xe30)[0] == branch(0xb4069a30, 0xb406a9c4, link=True)
    struct.pack_into('<I', service, 0xe30, 0xe3a00000)
    # The only caller of this DDR implementation is the disabled startup call.
    sys.path.insert(0,str(ROOT/'python-analysis'))
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
    cs = Cs(CS_ARCH_ARM,CS_MODE_ARM)
    cs.skipdata = True
    for ins in cs.disasm(bytes(service),0xb4068c00):
        if ins.id and ins.mnemonic.startswith('b') and ins.op_str.startswith('#'):
            target = int(ins.op_str[1:],16)
            if 0xb406a440 <= target < 0xb406b468:
                assert 0xb406a440 <= ins.address < 0xb406b468
    for offset in range(0,len(service)-3,4):
        value, = struct.unpack_from('<I',service,offset)
        if not 0x1840 <= offset < 0x2868:
            assert not 0xb406a440 <= value < 0xb406b468
    service[0x1840:0x2868] = struct.pack('<I',0xe12fff1e)*((0x2868-0x1840)//4)
    if '--diagnostic' in sys.argv:
        callback = Arm(0xb406a440)
        callback.emit(0xe92d41f0,0xe10f8000,0xf10c00c0,
                      *mov(4,0xb406c9c0),0xe3a00000,0xe5840010,
                      *mov(6,0xb406ce1c),0xe5965000,0xe3c5001f,0xe2801a02)
        callback.label('clean_target')
        callback.emit(0xee070f3a,0xe2800020,0xe1500001)
        callback.jump('clean_target',3)
        callback.emit(0xf57ff04f,0xee070f15,0xf57ff04f,0xf57ff06f,0xe12fff35,
                      0xe121f008,0xe8bd41f0)
        callback.emit(branch(callback.base+4*len(callback.ops),0xb4069fc8))
        callback_bytes = callback.finish()
        service[0x1840:0x1840+len(callback_bytes)] = callback_bytes
        assert struct.unpack_from('<I',service,0x12fc)[0] == 0xf10c0080
        struct.pack_into('<I',service,0x12fc,branch(0xb4069efc,callback.base))
        assert len(list(cs.disasm(callback_bytes,callback.base))) == len(callback.ops)
    service = bytes(service)
    compressed = pack(service)
    assert unpack(compressed, len(service)) == service
    assert PACKED+len(compressed) <= ENTRY
    # Omit UART strings; retain all native code and the live tables/globals.
    code_end, table_start, table_end = 0x4f1c, 0x5e30, 0x6220
    regions = [(0x2300,code_end),(table_start,table_end),
               (0x5358,0x5360),(0x563c,0x5b94),(0x5dbc,0x5e30)]
    native = bytearray().join(mb[start:end] for start,end in regions)
    def dest(offset):
        base = NATIVE
        for start,end in regions:
            if start <= offset < end:
                return base+offset-start
            base += end-start
        raise ValueError(f'Unsupported native pointer {offset:x}')
    for off in range(0, len(native), 4):
        value, = struct.unpack_from('<I', native, off)
        if 0xb406a300 <= value < 0xb406e220:
            old = value-0xb4068000
            retained = any(start <= old < end for start,end in regions)
            new = dest(old) if retained else 0
            struct.pack_into('<I', native, off, new)
    for old in (0x5358,0x563e,0x5794,0x5dbc,0x5e1c):
        relocated = dest(old)-NATIVE
        assert native[relocated:relocated+4] == mb[old:old+4]
    globals_offset = dest(0x61e0)-NATIVE
    native[globals_offset:globals_offset+0x40] = bytes(0x40)
    def patch(offset, *words):
        struct.pack_into('<'+'I'*len(words), native, offset-0x2300, *words)
    patch(0x2608, 0xe12fff1e)
    # Keep bounded polling from the board-tested initializer.
    for site, again, failed in ((0x3d34,0x3d28,0x3ef4),(0x3d68,0x3d5c,0x3ef4),
                               (0x3d88,0x3d7c,0x3ef4),(0x3da4,0x3d9c,0x3ef4),
                               (0x3e58,0x3e50,0x3ef4),(0x4378,0x4370,0x4394),
                               (0x43e0,0x43d8,0x43fc)):
        at = NATIVE+len(native)
        patch(site, branch(dest(site), at, cond=0))
        ops = [0xe92d0003, *mov(0,REPORT), 0xe5901010,0xe2511001,0xe5801010,
               0xe8bd0003,branch(at+28,dest(again),cond=1),branch(at+32,dest(failed))]
        native.extend(struct.pack('<'+'I'*len(ops), *ops))
    assert NATIVE+len(native) <= PACKED

    a = Arm(ENTRY)
    e = a.emit
    nonce = secrets.randbits(32)
    e(0xe92d5ff0,*mov(4,REPORT),*mov(0,nonce),0xe59410f0,0xe1500001,0x08bd9ff0,
      0xe58400f0,0xe584d0e0,0xe10f0000,0xe58400e4,
      *mov(1,0xb0150000),0xe5910000,0xe5840020,0xe591006c,0xe5840024,
      0xe591001c,0xe5840028,*mov(13,STACK),*mov(0,0x41555431),0xe5840000)
    e(0xe3a00000,0xe5840004,0xe5840008,0xe584000c,*mov(0,0x80000),0xe5840010)
    def mark(value):
        e(0xe3a00000|value,0xe5840004,0xee074f3a,0xf57ff04f)
    mark(1)
    if '--stop-entry' in sys.argv:
        a.jump('failed')
    e(0xe3a00000)
    a.call(dest(0x2438))
    e(*mov(0,PARAM))
    a.call(dest(0x37fc))
    e(0xe3500000)
    a.jump('failed',1)
    a.call(dest(0x2e7c))
    e(0xe5840008,0xe3500000)
    a.jump('failed',1)
    mark(2)
    e(0xe3a00001)
    a.call(dest(0x2438))
    e(*mov(1,0xb0150000),0xe59100a0,0xe380000a,0xe58100a0,0xf57ff04f)
    a.call(dest(0x387c))
    e(0xe3a00000)
    a.call(dest(0x3ffc))
    e(0xe584000c,0xe3500000)
    a.jump('failed',1)
    mark(3)
    if '--probe-ddr' in sys.argv:
        a.jump('failed')
    e(*mov(1,DDR_REPORT), *mov(2,0x44524431),0xe5812000,0xee071f3e,0xf57ff04f,
      0xe5913000,0xe1530002)
    a.jump('failed',1)
    # Publish the initialization result outside the service loader's SRAM image.
    e(0xe1a00004,*mov(1,DDR_REPORT),0xe3a02010)
    a.label('report_copy')
    e(0xe4903004,0xe4813004,0xe2522001)
    a.jump('report_copy',1)
    e(*mov(1,DDR_REPORT),0xee071f3a,0xe2811020,0xee071f3a,0xf57ff04f)
    e(0xf10c00c0,*mov(13,0xb4070000),*mov(0,PACKED),*mov(1,0xb4068c00),
      *mov(5,PACKED+len(compressed)),*mov(6,0xb4068c00+len(service)))
    a.label('token')
    e(0xe1500005)
    a.jump('unpacked',0)
    a.jump('failed',8)
    e(0xe4d02001,0xe3520080)
    a.jump('match',2)
    e(0xe2822001,0xe0803002,0xe1530005)
    a.jump('failed',8)
    e(0xe0813002,0xe1530006)
    a.jump('failed',8)
    a.label('literal')
    e(0xe4d03001,0xe4c13001,0xe2522001)
    a.jump('literal',1)
    a.jump('token')
    a.label('match')
    e(0xe1500005)
    a.jump('failed',2)
    e(0xe202700f,0xe4d03001,0xe1833407,0xe2833001,
      0xe1a02222,0xe2022007,0xe2822003,0xe0817002,0xe1570006)
    a.jump('failed',8)
    e(0xe0413003,*mov(7,0xb4068c00),0xe1530007)
    a.jump('failed',3)
    a.label('match_copy')
    e(0xe4d37001,0xe4c17001,0xe2522001)
    a.jump('match_copy',1)
    a.jump('token')
    a.label('unpacked')
    e(0xe1510006)
    a.jump('failed',1)
    e(*mov(0,0xb4068c00))
    a.label('clean')
    e(0xee070f3a,0xe2800020,0xe1500006)
    a.jump('clean',3)
    e(0xf57ff04f,0xee070f15,0xf57ff04f,0xf57ff06f,*mov(12,0xb4068c00),0xe12fff1c)
    a.label('failed')
    e(*mov(4,REPORT),*mov(1,0xb0150000),0xe5940028,0xe581001c,
      0xe5940024,0xe581006c,0xe5940020,0xe5810000,0xf57ff04f,
      0xee074f3a,0xf57ff04f,0xe59400e4,0xe594d0e0,0xe121f000,0xe8bd9ff0)
    # A fixed CPU wait avoids reliance on the timer while PLL changes.
    a.label('pll_wait')
    e(0xe92d0002,*mov(1,0x100000))
    a.label('spin')
    e(0xe2511001)
    a.jump('spin',1)
    e(0xe8bd0002,0xe12fff1e)
    patch(0x3afc,branch(dest(0x3afc),a.labels['pll_wait'],link=True))
    wrapper = a.finish()
    assert ENTRY+len(wrapper) <= ROM_STACK_FLOOR, len(wrapper)
    sys.path.insert(0,str(ROOT/'python-analysis'))
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
    decoded = list(Cs(CS_ARCH_ARM,CS_MODE_ARM).disasm(wrapper,ENTRY))
    assert len(decoded) == len(a.ops)
    assert any(i.mnemonic == 'orr' and i.op_str == 'r3, r3, r7, lsl #8' for i in decoded)
    # The decompressor may overwrite only already-consumed compressed bytes.
    consumed = produced = 0
    while consumed < len(compressed):
        token=compressed[consumed]; consumed+=1
        if token < 128:
            produced+=token+1; consumed+=token+1
        else:
            produced+=((token>>4)&7)+3; consumed+=1
        assert 0xb4068c00+produced <= PACKED+consumed
    output = ROOT/('autonomous-service-diagnostic' if '--diagnostic' in sys.argv else 'autonomous-service')
    output.mkdir(exist_ok=True)
    segments = [(PARAM,'parameters.bin',mb[0x30c00:0x31000]),
                (NATIVE,'ddr-init.bin',native),(PACKED,'usb-packed.bin',compressed),
                (ENTRY,'start.bin',wrapper)]
    manifest = {'entry':ENTRY,'report_ddr':DDR_REPORT,'report_sram':REPORT,'nonce':nonce,'service_bytes':len(service),
                'diagnostic_callback':'--diagnostic' in sys.argv,
                'retained_native_regions':regions,'service_disabled_DDR_range':[0x1840,0x2868],
                'nand_flashable':False,'stage0_status':'NAND reader and physical ECC layout pending',
                'host_continuations':0,'probe_ddr':'--probe-ddr' in sys.argv,
                'stop_entry':'--stop-entry' in sys.argv,'segments':[]}
    for address,name,data in segments:
        (output/name).write_bytes(data)
        manifest['segments'].append({'address':address,'file':name,'bytes':len(data),
                                    'sha256':hashlib.sha256(data).hexdigest()})
    assert PARAM+1024 <= STACK-0x300 and STACK <= NATIVE
    assert NATIVE+len(native) <= REPORT < REPORT+256 <= PACKED
    assert PACKED+len(compressed) <= ENTRY
    payload = bytearray(ROM_STACK_FLOOR-PARAM)
    for address,_,data in segments:
        payload[address-PARAM:address-PARAM+len(data)] = data
    (output/'sram-payload.bin').write_bytes(payload)
    manifest['payload'] = {'file':'sram-payload.bin','load_address':PARAM,
                           'bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest(),
                           'boot_entry_offset':ENTRY-PARAM,
                           'stage0_must_clear_guard_at':REPORT+0xf0}
    manifest['reserved_rom_stack'] = [ROM_STACK_FLOOR,0xb4070000]
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2))
    (output/'start-disassembly.txt').write_text('\n'.join(f'{i.address:08x} {i.mnemonic} {i.op_str}' for i in decoded))
    print(json.dumps(manifest,indent=2))
    return manifest


if __name__ == '__main__':
    for test in (b'',b'a',b'abc'*200,bytes(range(256))*4,b'\0'*4096):
        assert unpack(pack(test),len(test)) == test
    build()
