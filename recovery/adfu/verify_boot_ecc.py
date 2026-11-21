"""Verify boot-page ECC and whitening against saved dumps; never access a device."""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PAGE = 8192 + 744
GENERATOR = int('16503cf432240d9d16dc7226fb4d67f1f51f35292f095d4f6a2be42503ab2bb0a6aed13a3cc57edfdd70b', 16)
REVERSE = bytes(int(f'{i:08b}'[::-1], 2) for i in range(256))


def remainder(value):
    while value.bit_length() >= GENERATOR.bit_length():
        value ^= GENERATOR << (value.bit_length() - GENERATOR.bit_length())
    return value


def parity(data, metadata):
    if len(data) != 1024 or len(metadata) != 4:
        raise ValueError('Boot profile requires 1024 data bytes and 4 metadata bytes')
    value = int.from_bytes((data + metadata).translate(REVERSE), 'big') << 336
    return remainder(value).to_bytes(42, 'big').translate(REVERSE)


def whitening(seed):
    if len(seed) != 22:
        raise ValueError('Expected eleven 16-bit seed words')
    words = list(struct.unpack('<11H', seed))
    for i in range(11, 512):
        words.append(words[i-2] ^ words[i-3] ^ words[i-10] ^ words[i-11])
    return struct.pack('<512H', *words)


def verify():
    original = (ROOT/'xgb35-mbrec-recovered.bin').read_bytes()
    assert hashlib.sha256(original).hexdigest() == '34ff694f29e69e35972d34cc9244355c6066b43c8669e860e97575439c016edc'
    assert GENERATOR.bit_length() - 1 == 336
    assert parity(bytes(1024), bytes(4)) == bytes(42)
    checks = []
    for name, first_row in (('adfu-nand-first-256-pages-raw-with-oob.bin', 0),
                            ('adfu-nand-blocks-1-3-raw-with-oob.bin', 256)):
        raw = (ROOT/name).read_bytes()
        for page in range(4):
            physical = raw[page*PAGE:(page+1)*PAGE]
            rebuilt = bytearray(physical)
            for step in range(8):
                offset = step*1024
                data = original[page*8192+offset:page*8192+offset+1024]
                recorded = physical[offset:offset+1024]
                # shortcut: recover the row/column seed from known factory data; no general seed formula yet.
                mask = whitening(bytes(a ^ b for a, b in zip(recorded[:22], data[:22])))
                rebuilt[offset:offset+1024] = bytes(a ^ b for a, b in zip(data, mask))
                oob = 8192 + step*46
                metadata = physical[oob:oob+4]
                ecc = parity(data, metadata)
                assert ecc == physical[oob+4:oob+46], (first_row+page, step, 'ECC')
                rebuilt[oob+4:oob+46] = ecc
                # A changed data bit must invalidate this stored codeword.
                changed = bytes([data[0] ^ 1]) + data[1:]
                assert parity(changed, metadata) != ecc
            differences = [{'byte':i, 'xor':a ^ b} for i,(a,b) in enumerate(zip(rebuilt,physical)) if a != b]
            # The backup dump has one data-bit discrepancy; its stored ECC matches the original plaintext.
            assert not differences or (first_row+page == 259 and len(differences) == 1
                                       and differences[0]['byte'] == 2375
                                       and differences[0]['xor'].bit_count() == 1), differences
            assert physical[8192+368:] == b'\xff'*376
            checks.append({'row':first_row+page, 'exact_physical_roundtrip':not differences,
                           'raw_data_discrepancies':differences,
                           'sha256':hashlib.sha256(physical).hexdigest()})
    result = {'verified_pages':len(checks),'verified_ECC_steps':len(checks)*8,
              'data_bytes':8192,'OOB_bytes':744,'step_data_bytes':1024,
              'step_metadata_bytes':4,'step_parity_bytes':42,
              'generator_hex':hex(GENERATOR),'bit_order':'reverse bits within each byte, MSB polynomial',
              'whitening_words':'w[n-2] ^ w[n-3] ^ w[n-10] ^ w[n-11]',
              'seed_source':'22 known plaintext bytes per physical row and step',
              'checks':checks,'flash_ready':False,
              'remaining':['hardware read settings','seed formula for arbitrary rows',
                           'autonomous NAND boot and fallback','error correction capability']}
    output = ROOT/'nand-read-validation'
    output.mkdir(exist_ok=True)
    probe = output/'rom-page0-sector0.bin'
    if probe.exists():
        raw_first = (ROOT/'adfu-nand-first-256-pages-raw-with-oob.bin').read_bytes()[:1024]
        observed = probe.read_bytes()
        assert len(observed) == 1024
        mask = bytes(a ^ b for a,b in zip(raw_first,original))
        assert bytes(a ^ b for a,b in zip(observed,mask)) == b'\xe0'*1024
        result['hardware_whitening'] = {'matching_bytes':1024,
            'input_was_command_echo_not_NAND_data':True,
            'observed_sha256':hashlib.sha256(observed).hexdigest()}
    (output/'ecc-verification.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    verify()
