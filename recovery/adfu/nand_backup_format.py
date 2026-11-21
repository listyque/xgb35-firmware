"""Offline reconstruction of boot-profile sectors from hardware-corrected reads."""
import struct
from verify_boot_ecc import parity, whitening


def distance(a,b):
    assert len(a)==len(b)
    return sum((x^y).bit_count() for x,y in zip(a,b))


def recover_record(data,records):
    assert len(data)==1024 and records and all(len(r)==46 for r in records)
    def rank(metas):
        out=[]
        for meta in metas:
            value=meta+parity(data,meta)
            errors=[distance(value,r) for r in records]
            out.append((sum(errors),max(errors),meta,value,errors))
        return sorted(out)
    metas={r[:4] for r in records}
    ranked=rank(metas)
    if ranked[0][1]>24:
        # shortcut: one damaged metadata bit is supported; stop for larger damage instead of guessing.
        metas.update((int.from_bytes(m,'little')^(1<<bit)).to_bytes(4,'little')
                     for m in tuple(metas) for bit in range(32))
        ranked=rank(metas)
    best=ranked[0]
    assert best[1]<=24 and (len(ranked)==1 or best[:2]!=ranked[1][:2]), 'Ambiguous ECC/metadata recovery'
    return best[3],best[4]


def mask_from_window(observed,start):
    assert len(observed)==1024 and 0<=start<=501
    values=list(struct.unpack('<512H',observed))
    result=[0]*512
    result[start:start+11]=values[start:start+11]
    for i in range(start-1,-1,-1):
        result[i]=result[i+11]^result[i+9]^result[i+8]^result[i+1]
    for i in range(start+11,512):
        result[i]=result[i-2]^result[i-3]^result[i-10]^result[i-11]
    return struct.pack('<512H',*result)


def recover_mask(data,raw_versions):
    observations=[bytes(x^y for x,y in zip(raw,data)) for raw in raw_versions]
    candidates={mask_from_window(v,start) for v in observations for start in (0,32,128)}
    ranked=sorted((sum(distance(mask,v) for v in observations),mask) for mask in candidates)
    assert len(ranked)==1 or ranked[0][0]!=ranked[1][0], 'Ambiguous whitening mask'
    mask=ranked[0][1]
    errors=[distance(mask,v) for v in observations]
    assert max(errors)<=24, 'Raw data differs too much from corrected data and mask'
    return mask,errors


if __name__=='__main__':
    data=bytes(range(256))*4
    meta=bytes(4)
    record=meta+parity(data,meta)
    damaged=bytearray(record);damaged[1]^=1
    assert recover_record(data,[bytes(damaged)]*4)[0]==record
    damaged=bytearray(record);damaged[26]^=0x20
    assert recover_record(data,[bytes(damaged)]*4)[0]==record
    mask=whitening(bytes(range(22)))
    raw=bytearray(x^y for x,y in zip(data,mask));raw[0]^=1
    assert recover_mask(data,[bytes(raw)]*4)[0]==mask
    assert mask_from_window(mask,128)==mask
    print('ECC metadata/parity correction and whitening recovery with damaged seed passed')
