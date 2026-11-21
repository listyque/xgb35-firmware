"""Package a logical NAND boot candidate on the PC. No device access or flash writes."""
import hashlib
import json
import struct
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

from build_autonomous_service import Arm, mov, ROOT


def checksum_section(data):
    assert len(data) % 4 == 0
    assert struct.unpack_from('<H',data,len(data)-6)[0] == 0x55aa
    return (0x1234+sum(struct.unpack('<'+'I'*(len(data)//4-1),data[:-4]))) & 0xffffffff


def build():
    folder = ROOT/'autonomous-service'
    manifest_bytes = (folder/'manifest.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    tested = json.loads((folder/'ram-test.json').read_text())
    assert tested['manifest_sha256'] == hashlib.sha256(manifest_bytes).hexdigest()
    assert tested['host_execution_commands'] == 1 and tested['cold_ddr_pll'] == 0x0a
    assert any(x.get('stage') == 'autonomous_DDR_and_USB' for x in tested['checks'])
    assert any(x.get('rounds') == 64 for x in tested['checks'])
    assert any(x.get('USB_responds') for x in tested['checks'])
    payload = (folder/'sram-payload.bin').read_bytes()
    assert len(payload) == 0x7000 and manifest['payload']['load_address'] == 0xb4068800
    assert hashlib.sha256(payload).hexdigest() == manifest['payload']['sha256']
    mb = (ROOT/'xgb35-mbrec-recovered.bin').read_bytes()
    assert hashlib.sha256(mb).hexdigest() == '34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc'
    assert checksum_section(mb[0x200:0x600]) == struct.unpack_from('<I',mb,0x5fc)[0]
    # Factory stage 0 reads/validates the 0x600-byte stage at physical SRAM 0x8e00.
    # Move the reader below its destination before loading the service payload.
    base, descriptor = 0xb4068200, 0xb40683e0
    a = Arm(base)
    e = a.emit
    e(0xe3550b01)  # native stage 0 leaves the ECC data step in r5: 1024 bytes
    a.jump('failure',1)
    e(0xe35a0a02)  # r10 is the physical page data size: 8192 bytes
    a.jump('failure',1)
    e(*mov(6,descriptor),*mov(7,0xb4068800),0xe3a04001,0xe1a04604,0xe3a0801c)
    a.label('read_chunk')
    e(0xe1a00007,0xe3a01020)
    a.label('prepare_dma')
    e(0xee070f3e,0xe2800020,0xe2511001)
    a.jump('prepare_dma',1)
    e(0xf57ff04f,0xe1c640b0,0xe1c690b2,0xe1a00829,0xe5c60004,0xe5867008,
      0xe1a00006,0xe3a01004,*mov(12,0xffff2a00),0xe12fff3c)
    e(0xe1a00007,0xe3a01020)
    a.label('finish_dma')
    e(0xee070f36,0xe2800020,0xe2511001)
    a.jump('finish_dma',1)
    e(0xf57ff04f,0xe0877005,0xe0844005,0xe154000a,0x03a04000,0x02899001,0xe2588001)
    a.jump('read_chunk',1)
    e(*mov(0,0xb4068800),*mov(1,len(payload)//4),0xe3a02000)
    a.label('sum')
    e(0xe4903004,0xe0822003,0xe2511001)
    a.jump('sum',1)
    expected = sum(struct.unpack('<'+'I'*(len(payload)//4),payload)) & 0xffffffff
    e(*mov(3,expected),0xe1520003)
    a.jump('failure',1)
    e(*mov(0,manifest['payload']['stage0_must_clear_guard_at']),0xe3a01000,0xe5801000,
      0xee070f3a,0xf57ff04f,0xee070f15,0xf57ff04f,0xf57ff06f,
      *mov(12,manifest['entry']),0xe12fff3c)
    a.label('failure')
    e(*mov(12,0xffff4a00),0xe12fff1c)
    reader = a.finish()
    assert base+len(reader) <= descriptor
    move = Arm(0xb4068e00)
    move.emit(0xf10c00c0,*mov(0,0xb4068e80),*mov(1,base),*mov(2,len(reader)//4))
    move.label('copy')
    move.emit(0xe4903004,0xe4813004,0xe2522001)
    move.jump('copy',1)
    move.emit(*mov(0,base),*mov(1,base+len(reader)))
    move.label('clean')
    move.emit(0xee070f3a,0xe2800020,0xe1500001)
    move.jump('clean',3)
    move.emit(0xf57ff04f,0xee070f15,0xf57ff04f,0xf57ff06f,*mov(12,base),0xe12fff1c)
    relocation = move.finish()
    assert len(relocation) <= 0x80
    stage = bytearray(0x600)
    stage[:len(relocation)] = relocation
    stage[0x80:0x80+len(reader)] = reader
    struct.pack_into('<H',stage,0x5fa,0x55aa)
    struct.pack_into('<I',stage,0x5fc,checksum_section(stage))
    image = bytearray(0x8000)
    image[:0x600] = mb[:0x600]
    image[0x600:0xc00] = stage
    image[0x1000:] = payload
    assert ((0x1234+sum(struct.unpack('<127I',image[:508]))) & 0xffffffff) == struct.unpack_from('<I',image,508)[0]
    for start,length in ((0x200,0x400),(0x600,0x600)):
        section = image[start:start+length]
        assert checksum_section(section) == struct.unpack_from('<I',section,length-4)[0]
    # Simulate all NAND addresses for either factory boot copy, including page crossings.
    for boot_row in (0,256):
        reconstructed = bytearray()
        column,row = 0x1000,boot_row
        for _ in range(28):
            offset = (row-boot_row)*8192+column
            reconstructed.extend(image[offset:offset+1024])
            column += 1024
            if column == 8192:
                row += 1
                column = 0
        assert reconstructed == payload and row == boot_row+4 and column == 0
    import sys
    sys.path.insert(0,str(ROOT/'python-analysis'))
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
    cs=Cs(CS_ARCH_ARM,CS_MODE_ARM)
    instructions=list(cs.disasm(reader,base))
    assert len(instructions) == len(a.ops)
    assert instructions[0].op_str == 'r5, #0x400'
    assert instructions[2].op_str == 'sl, #0x2000'
    assert any(i.op_str == 'r4, r4, #0xc' for i in instructions if i.mnemonic == 'lsl')
    candidate = ROOT/'autonomous-service-nand-candidate'
    candidate.mkdir(exist_ok=True)
    (candidate/'logical-boot-image.bin').write_bytes(image)
    (candidate/'reader-disassembly.txt').write_text('\n'.join(f'{i.address:08x} {i.mnemonic} {i.op_str}' for i in instructions))
    info = {'status':'UNTESTED_NAND_READER_NOT_FLASHABLE','logical_bytes':len(image),
            'logical_image_sha256':hashlib.sha256(image).hexdigest(),
            'tested_ram_manifest_sha256':hashlib.sha256(manifest_bytes).hexdigest(),
            'boot_sector':'unchanged factory MBREC first sector',
            'payload_offset':0x1000,'reader_execution_address':base,
            'nand_operations':['BootROM dispatch operation 4: read only'],
            'required_page_data_bytes':8192,'required_ECC_step_bytes':1024,
            'not_validated':['physical ECC/OOB and scrambling layout','reader execution on device',
                             'CPU/interrupt state inherited from NAND boot','power-on boot and recovery fallback'],
            'ram_result':tested}
    (candidate/'manifest.json').write_text(json.dumps(info,indent=2))
    with ZipFile(ROOT/'autonomous-service-ram-verified.zip','w',ZIP_DEFLATED) as z:
        for name in ('manifest.json','parameters.bin','ddr-init.bin','usb-packed.bin','start.bin',
                     'sram-payload.bin','ram-test.json','init-report.bin','start-disassembly.txt'):
            z.write(folder/name,name)
    print(json.dumps({k:v for k,v in info.items() if k!='ram_result'},indent=2))


if __name__ == '__main__':
    build()
