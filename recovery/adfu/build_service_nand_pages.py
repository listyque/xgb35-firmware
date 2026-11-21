"""Build row-specific NAND page candidates offline; not a flash utility."""
import hashlib
import json
from verify_boot_ecc import ROOT, PAGE, parity, whitening, remainder, REVERSE, verify


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build():
    verify()
    folder=ROOT/'autonomous-service-nand-candidate'
    evidence=ROOT/'nand-read-validation'
    logical=(folder/'logical-boot-image.bin').read_bytes()
    source=json.loads((folder/'manifest.json').read_text())
    assert sha(logical)==source['logical_image_sha256'] and len(logical)==32768
    for row,bad in ((0,0),(256,0),(0,1)):
        proof=json.loads((evidence/f'candidate-reader-row{row}-bad{bad}.json').read_text())
        assert proof['candidate_sha256']==sha(logical)
        assert proof['all_28_sectors_match'] and proof['result']==(2 if bad else 1)
    original=(ROOT/'xgb35-mbrec-recovered.bin').read_bytes()
    images=[]
    for first in (0,256):
        output=bytearray()
        for page in range(4):
            row=first+page
            raw=(evidence/f'raw-row{row}.bin').read_bytes()
            raw_proof=json.loads((evidence/f'raw-row{row}.json').read_text())
            assert raw_proof['error']==0 and sha(raw)==raw_proof['sha256'] and len(raw)==PAGE
            candidate=bytearray(raw)
            for step in range(8):
                label=f'rom-flags2-row{row}-sector{step}'
                proof=json.loads((evidence/f'{label}.json').read_text())
                decoded=(evidence/f'{label}-sector.bin').read_bytes()
                offset=step*1024
                expected=original[page*8192+offset:page*8192+offset+1024]
                assert proof['sector_matches_factory'] and proof['phase']==5 and decoded==expected
                mask=whitening(bytes(a^b for a,b in zip(raw[offset:offset+22],decoded[:22])))
                oob=8192+step*46
                metadata=raw[oob:oob+4]
                assert parity(decoded,metadata)==raw[oob+4:oob+46]
                data=logical[page*8192+offset:page*8192+offset+1024]
                candidate[offset:offset+1024]=bytes(a^b for a,b in zip(data,mask))
                candidate[oob+4:oob+46]=parity(data,metadata)
                recovered=bytes(a^b for a,b in zip(candidate[offset:offset+1024],mask))
                assert recovered==data
                codeword=(recovered+candidate[oob:oob+46]).translate(REVERSE)
                assert remainder(int.from_bytes(codeword,'big'))==0
            assert candidate[8192+368:]==raw[8192+368:]
            for step in range(8):
                oob=8192+46*step
                assert candidate[oob:oob+4]==raw[oob:oob+4]
            output.extend(candidate)
        name=f'UNFLASHED-physical-rows-{first}-{first+3}.bin'
        (folder/name).write_bytes(output)
        images.append({'file':name,'first_row':first,'page_count':4,'bytes':len(output),'sha256':sha(output)})
    result={'status':'OFFLINE_PAGE_CANDIDATES_FULL_BOOT_UNTESTED','flash_ready':False,
            'logical_sha256':sha(logical),'physical_page_bytes':PAGE,
            'data_bytes_per_page':8192,'OOB_bytes_per_page':744,
            'scope':'Only four pages for each known boot row; not erase-block or full-NAND images',
            'metadata':'preserved all factory 4-byte records and unused OOB bytes',
            'whitening':'row/step-specific mask recovered from factory data and verified against live reads',
            'hardware_evidence':{'raw_pages':8,'decoded_sectors':64,
                'candidate_28_sector_loop':'passed for boot rows 0 and 256, redirected to DDR',
                'bad_checksum_branch':'passed, redirected to RAM return stub',
                'one_bit_correction':'row 259, sector 2: decoded matches factory, ECC register 1'},
            'images':images,
            'not_checked':['power-on BootROM clock setup and NAND boot',
                'stage relocation in original SRAM placement',
                'native ADFU recovery fallback',
                'erase-block update plan and preservation of other boot copies'],
            'NAND_writes_performed':False}
    chain_path=evidence/'ram-chain-validation.json'
    if chain_path.exists():
        chain=json.loads(chain_path.read_text())
        if chain['candidate_sha256']==sha(logical):
            assert chain['all_requested_RAM_cases_passed']
            result['hardware_evidence']['RAM_chain_validation']=chain_path.name
            result['not_checked']=['power-on BootROM NAND search and physical loading of the new image',
                'erase-block update plan and preservation of other boot copies',
                'recovery from arbitrary CPU hangs (no watchdog test)']
    preparation=ROOT/'nand-write-preparation/write-plan.json'
    if preparation.exists():
        plan=json.loads(preparation.read_text())
        if plan.get('logical_candidate_sha256')==sha(logical):
            result['write_preparation']='../nand-write-preparation/write-plan.json'
            result['not_checked']=['actual NAND PROGRAM/ERASE implementation and physical rollback',
                'power-on BootROM NAND search and physical loading of the new image',
                'independent storage copy of the recovery archive',
                'recovery from arbitrary CPU hangs (no watchdog test)']
    (folder/'physical-pages-manifest.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    build()
