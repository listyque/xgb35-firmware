"""Build a review-only block image and recovery archive offline. No USB or flash API."""
import hashlib
import json
import struct
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
from nand_backup_format import recover_mask, recover_record
from verify_boot_ecc import PAGE, parity

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'nand-write-preparation'
BLOCK=PAGE*256
SPEC='https://datasheet.lcsc.com/lcsc/1908271118_Micron-Tech-MT29F64G08CBABAWP-B_C410865.pdf'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build():
    names=['adfu-nand-first-256-pages-raw-with-oob.bin','adfu-nand-blocks-1-3-raw-with-oob.bin',
           'adfu-nand-first-32MiB-raw-with-oob.bin']
    historical=[(ROOT/n).read_bytes() for n in names]
    assert [len(b) for b in historical]==[BLOCK,3*BLOCK,16*BLOCK]
    assert [sha(b) for b in historical]==[
        '370cfd3dcaa23a9b13788c8df04955a693e3177c409d890acd462e7c59ad8b0c',
        'bf7929eb1b74e97f64748a0474de03606f23a36a8284d8716ce57fa6182ae7b8',
        'c1fee31a433b94adb23118c12632adbcb0095b981b5836f2d6c38c4c9c708cc4']
    fresh=[(OUT/n).read_bytes() for n in ('live-blocks-0-1-raw.bin','live-blocks-0-1-repeat-raw.bin')]
    assert all(len(b)==2*BLOCK for b in fresh)
    sources=[*fresh,(historical[0]+historical[1])[:2*BLOCK],historical[2][:2*BLOCK]]
    for block in (0,1):
        assert all(b[block*BLOCK+8192]==255 for b in sources), 'Bad-block marker: stop, do not erase'
    evidence=json.loads((OUT/'live-ECC-verification.json').read_text())
    rows=evidence['rows'];checks={(c['row'],c['sector']):c for c in evidence['checks']}
    assert evidence['sector_count']==len(rows)*8==960
    corrected=bytearray(b'\xff'*(2*BLOCK));decoded=bytearray(b'\xff'*(512*8192));unprotected=[]
    for row in range(512):
        pages=[b[row*PAGE:(row+1)*PAGE] for b in sources]
        if row not in rows:
            assert all(p==b'\xff'*PAGE for p in pages), ('Unverified erased page',row)
            continue
        for sector in range(8):
            data=(OUT/f'decoded/rom-flags2-row{row}-sector{sector}-sector.bin').read_bytes()
            assert sha(data)==checks[row,sector]['decoded_sha256']
            start=sector*1024;oob=8192+sector*46
            mask,errors=recover_mask(data,[p[start:start+1024] for p in pages])
            record,record_errors=recover_record(data,[p[oob:oob+46] for p in pages])
            assert max(x+y for x,y in zip(errors,record_errors))<=24
            assert record[4:]==parity(data,record[:4])
            corrected[row*PAGE+start:row*PAGE+start+1024]=bytes(x^y for x,y in zip(data,mask))
            corrected[row*PAGE+oob:row*PAGE+oob+46]=record
            decoded[row*8192+start:row*8192+start+1024]=data
        tails=[p[8560:] for p in pages]
        if len(set(tails))>1:
            unprotected.append({'row':row,'tail_sha256_by_capture':[sha(t) for t in tails],
                                'choice':'first fresh capture, not an ECC correction'})
        # There is no ECC evidence for unused OOB; preserve the first fresh observation verbatim.
        corrected[row*PAGE+8560:(row+1)*PAGE]=tails[0]
    repairs=[]
    for offset,(raw,clean) in enumerate(zip(fresh[0],corrected)):
        if raw!=clean:
            page_offset=offset%PAGE
            repairs.append({'row':offset//PAGE,'page_byte':page_offset,'before':raw,'after':clean,
                'bits':(raw^clean).bit_count(),'area':'data' if page_offset<8192 else
                ('OOB_ECC_or_metadata' if page_offset<8560 else 'unused_OOB_consensus')})
    boot=[]
    for row in (0,32,64,128,192,256,384):
        data=decoded[row*8192:row*8192+512];w=struct.unpack('<128I',data)
        valid=w[125:127]==(0x42746341,0x55aa6d72) and ((sum(w[:127])+0x1234)&0xffffffff)==w[127]
        boot.append({'row':row,'block':row//256,'valid_boot_sector':valid})
    assert [b['row'] for b in boot if b['valid_boot_sector']]==[0,256]
    candidate=ROOT/'autonomous-service-nand-candidate'
    manifest=json.loads((candidate/'physical-pages-manifest.json').read_text())
    prefix=(candidate/'UNFLASHED-physical-rows-0-3.bin').read_bytes()
    assert len(prefix)==4*PAGE and sha(prefix)==manifest['images'][0]['sha256']
    chain=json.loads((ROOT/'nand-read-validation/ram-chain-validation.json').read_text())
    assert chain['all_requested_RAM_cases_passed'] and chain['candidate_sha256']==manifest['logical_sha256']
    target=bytearray(corrected[:BLOCK]);target[:4*PAGE]=prefix
    changed=[row for row in range(256) if target[row*PAGE:(row+1)*PAGE]!=corrected[row*PAGE:(row+1)*PAGE]]
    assert changed==[0,1,2,3] and target[4*PAGE:]==corrected[4*PAGE:BLOCK]
    assert target[8192]==255
    program_pages=[row for row in range(256) if target[row*PAGE:(row+1)*PAGE]!=b'\xff'*PAGE]
    assert program_pages==sorted(program_pages) and len(program_pages)==60
    # The existing image uses only unpaired/lower pages; do not add programs to the shared upper pages.
    lower={0,1,2,3,248,249,252,253,254,255}|{v for n in range(4,248,4) for v in (n,n+1)}
    assert set(program_pages)<=lower
    outputs={
        'block0-original-raw.bin':fresh[0][:BLOCK],
        'block1-keep-original-raw.bin':fresh[0][BLOCK:],
        'block0-restore-ECC-corrected.bin':bytes(corrected[:BLOCK]),
        'block1-keep-ECC-corrected.bin':bytes(corrected[BLOCK:]),
        'block0-original-decoded.bin':bytes(decoded[:256*8192]),
        'block1-original-decoded.bin':bytes(decoded[256*8192:]),
        'block0-candidate-REVIEW-ONLY.bin':bytes(target)}
    for name,data in outputs.items():
        (OUT/name).write_bytes(data)
    (OUT/'corrections-vs-live-raw.json').write_text(json.dumps(repairs,indent=2))
    (OUT/'unprotected-OOB-disagreements.json').write_text(json.dumps(unprotected,indent=2))
    result={'status':'PREPARATION_ONLY_NO_FLASH_COMMANDS','NAND_written_or_erased':False,
        'logical_candidate_sha256':manifest['logical_sha256'],
        'chip_marking':'Micron 29F64G08CBABA WP:B (board photograph)',
        'recorded_NAND_ID':(ROOT/'adfu-nand-id.bin').read_bytes().hex(),
        'datasheet':{'url':SPEC,'revision':'E 11/11; advance copy','pages':[1,99,117,118,119,136]},
        'geometry':{'page_data':8192,'page_OOB':744,'pages_per_erase_block':256,
                    'erase_block_raw_bytes':BLOCK,'erase_block_data_bytes':2097152},
        'target':{'erase_blocks':[0],'physical_rows':[0,255],'logical_replacement_rows':[0,1,2,3],
                  'program_page_order':program_pages,'all_FF_pages_left_unprogrammed':196,
                  'image':'block0-candidate-REVIEW-ONLY.bin'},
        'preserve':{'blocks':[1],'rows':[256,511],'all_blocks_from':2,
                    'copy_is_not_automatic_failover_after_valid_header':True},
        'boot_search_checks':boot,
        'backup_validation':{'independent_raw_captures':4,'fresh_complete_raw_captures':2,
            'raw_pages_per_fresh_capture':512,'hardware_ECC_sectors':960,
            'corrected_bits_vs_first_fresh_capture':sum(r['bits'] for r in repairs),
            'block_decoded_copies_equal':decoded[:256*8192]==decoded[256*8192:],
            'block_corrected_raw_copies_equal':corrected[:BLOCK]==corrected[BLOCK:]},
        'unprotected_OOB_policy':'first fresh capture preserved verbatim; disagreements logged, no correction claimed',
        'artifacts':{name:{'bytes':len(data),'sha256':sha(data)} for name,data in outputs.items()},
        'rollback':{'target_block':0,'preferred_image':'block0-restore-ECC-corrected.bin',
            'forensic_image':'block0-original-raw.bin','block1_must_remain_untouched':True,
            'restores':'pre-experiment NAND boot content, not a proven working SYSTEM firmware'},
        'write_ready':False,'remaining':['review and validate the actual NAND PROGRAM/ERASE implementation',
            'copy this recovery archive to an independent storage device',
            'fresh ID/bad-block/backup preflight immediately before any separately authorized write',
            'power-on boot of the new physical NAND image has not been tested']}
    (OUT/'write-plan.json').write_text(json.dumps(result,indent=2))
    (OUT/'RECOVERY-PLAN.txt').write_text(f'''Подготовка записи NAND — без выполнения записи

Цель первой записи: только блок 0, физические страницы 0–255.
Замена загрузчика: страницы 0–3. Размер сырого блока с OOB: {BLOCK} байт.
Образ для рассмотрения: block0-candidate-REVIEW-ONLY.bin
SHA-256: {sha(target)}
Остальные 252 страницы сохранены относительно отдельно проверенного исправленного образа.
В блоке 60 заполненных страниц: четыре новые и 56 сохранённых. 196 пустых страниц остаются FF.
Исправления ошибок чтения вынесены в corrections-vs-live-raw.json; исходные дампы не менялись.
Не защищённый ECC хвост OOB сохранён как в первом свежем чтении, без исправлений по догадке.
Его расхождения вынесены в unprotected-OOB-disagreements.json; все варианты сохранены в сырых дампах.

Блок 1 (страницы 256–511) оставить нетронутым. Блоки 2 и далее не менять.
Валидные заводские загрузочные секторы подтверждены в строках 0 и 256.
Метки FF BB в строках 32/64 сами по себе не являются загрузочными копиями.
Сохранение блока 1 не гарантирует автоматического переключения: после принятого заголовка
блока 0 ошибка следующих ступеней ведёт в ADFU; при зависании может понадобиться аппаратный вход.
Резервная копия возвращает прежнее содержимое загрузочной области, а не доказывает исправность SYSTEM.

Перед будущей записью:
1. Скопировать nand-boot-recovery.zip и recovery-archive.sha256 на другой физический носитель.
   Проверить SHA-256 архива и CONTENTS-SHA256.json после копирования. В этой задаче второй носитель не создавался.
   PowerShell: Get-FileHash -Algorithm SHA256 ./nand-boot-recovery.zip
   Сравнить Hash с первой строкой recovery-archive.sha256.
2. Проверить фактический ID NAND, сырые маркеры плохого блока и свежие полные чтения обоих блоков.
   Маркер первого OOB-байта страницы 0 и 256 в четырёх проверенных снимках: FF.
3. Проверить реализацию PROGRAM/ERASE для этой NAND. Готового проверенного писателя в пакете нет.
   Только адреса блока 0; никаких multi-plane, copyback, OTP, глобального форматирования или записи BBT.
4. После отдельного разрешения стереть лишь блок 0, проверить статус и чтением подтвердить стирание.
   Если маркер изменился, проверка статуса не прошла или ECC не исправляет чтение — остановиться;
   блок 1 не использовать как запасную площадку для эксперимента.
5. Программировать полную страницу данных+OOB одним проходом, в порядке program_page_order из write-plan.json.
   Это уже физический образ: повторно накладывать ECC и скремблирование нельзя.
   Не писать пустые страницы, не добавлять запись в парные верхние страницы.
   Заголовок нельзя дописывать последним или вторым проходом по уже записанной странице.
6. После каждого действия проверять статус. После записи перечитать весь блок сырым способом и с ECC:
   сравнить декодированные данные, метаданные, маркер, сохранённые страницы; проверить и неизменность блока 1.
   Только затем проверять включение без ADFU и возврат в ADFU.

Восстановление при неудаче:
1. Войти в аппаратный BootROM ADFU; загрузить в RAM проверенные средства восстановления.
2. Проверить сохранённый архив и фактическую геометрию. Блок 1 не стирать.
3. После отдельного разрешения восстановить только блок 0 из block0-restore-ECC-corrected.bin
   тем же проверенным писателем: стирание, последовательная запись заполненных страниц, контроль статуса.
   block0-original-raw.bin — нетронутый forensic-снимок, он сохраняет и исходные битовые ошибки.
4. Перечитать и сверить восстановленный блок с block0-original-decoded.bin и его ECC/OOB.
   Если контроллер/блок больше не позволяет восстановление — остановиться; резервный блок 1 остаётся сохранённым.

Подтверждено: два свежих полных сырых чтения, два исторических источника, 960 секторов с аппаратной ECC,
независимая проверка кодовых слов, RAM-цепочка и испытанные аварийные возвраты.
Не подтверждено: PROGRAM/ERASE на устройстве, фактический откат записью и включение с новым образом NAND.
Архив содержит первые 32 МиБ данных NAND с OOB; это не полный дамп всей NAND объёмом 8 ГиБ.

Источник геометрии и ограничений программирования: документация Micron Rev. E 11/11, стр. 1, 99, 117–119, 136.
Страницы записываются по возрастанию номера; допустим один проход программирования страницы между стираниями.
Парные страницы MLC требуют сохранения порядка; прерывание записи может повредить данные общей ячейки.
{SPEC}
''',encoding='utf-8')
    print(json.dumps({'target_block':0,'reserved_block':1,**result['backup_validation'],
                     'target_sha256':sha(target),'write_ready':False},indent=2))
    return result


def archive():
    paths={}
    for p in OUT.rglob('*'):
        if p.is_file() and p.suffix!='.zip' and p.name not in ('recovery-archive.sha256','archive-verification.json'):
            paths['nand-write-preparation/'+p.relative_to(OUT).as_posix()]=p
    for name in ('adfu-nand-first-256-pages-raw-with-oob.bin','adfu-nand-blocks-1-3-raw-with-oob.bin',
                 'adfu-nand-first-32MiB-raw-with-oob.bin','adfu-nand-id.bin',
                 'atm7051h-high-rom-ffff0000-ffff7fff.bin','xgb35-mbrec-recovered.bin',
                 'autonomous-service-ram-verified.zip','atm7051h-bootrom-nand.md'):
        paths[name]=ROOT/name
    for name in ('adfu_read_nand.py','adfu_autonomous_service.py','build_autonomous_service.py',
                 'service_usb.py','rom_usb.py','service_return_to_rom.py','service_nand_rom_probe.py',
                 'service_nand_raw_probe.py','verify_boot_ecc.py','nand_backup_format.py','prepare_nand_write.py',
                 'ram_chain_probe.py','build_service_nand_candidate.py','build_service_nand_pages.py',
                 'service_nand_reader_probe.py','service_adfu_fallback_probe.py'):
        paths[name]=ROOT/name
    for directory in ('autonomous-service-nand-candidate','autonomous-service-diagnostic','autonomous-service','nand-read-validation'):
        for p in (ROOT/directory).rglob('*'):
            if p.is_file():paths[directory+'/'+p.relative_to(ROOT/directory).as_posix()]=p
    for name in ('atm7051h-high-rom-ffff0000-ffff7fff.bin','xgb35-mbrec-recovered.bin','x70-parts.zip'):
        paths[name]=ROOT/name
    for p in (ROOT/'python-analysis/capstone').rglob('*'):
        if p.is_file() and '__pycache__' not in p.parts:
            paths['python-analysis/capstone/'+p.relative_to(ROOT/'python-analysis/capstone').as_posix()]=p
    hashes={name:{'bytes':p.stat().st_size,'sha256':sha(p.read_bytes())} for name,p in sorted(paths.items())}
    archive_path=OUT/'nand-boot-recovery.zip'
    with ZipFile(archive_path,'w',ZIP_DEFLATED) as z:
        for name,p in sorted(paths.items()):z.write(p,name)
        z.writestr('CONTENTS-SHA256.json',json.dumps(hashes,indent=2))
    with ZipFile(archive_path) as z:
        assert z.testzip() is None
        for name,entry in hashes.items():
            data=z.read(name)
            assert len(data)==entry['bytes'] and sha(data)==entry['sha256'],name
    digest=sha(archive_path.read_bytes())
    (OUT/'recovery-archive.sha256').write_text(f'{digest}  {archive_path.name}\n')
    (OUT/'archive-verification.json').write_text(json.dumps({'files_verified':len(hashes),
        'archive_sha256':digest,'archive_bytes':archive_path.stat().st_size,'CRC_and_all_SHA256_passed':True,
        'independent_storage_copy_created':False},indent=2))
    print(f'Archive verified: {len(hashes)} files, {archive_path.stat().st_size} bytes, SHA256 {digest}')


if __name__=='__main__':
    build()
    archive()
