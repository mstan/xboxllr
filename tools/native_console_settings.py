#!/usr/bin/env python3
"""Prepare a private authenticated boot-region snapshot; never print payloads."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from create_eeprom import validate_eeprom, _decrypt_security

MAGIC = b'XLLESET1'
ARTIFACT_BYTES = 44

def derive_region(eeprom):
    checked = validate_eeprom(eeprom)
    if not checked.ok:
        raise ValueError('console-settings EEPROM rejected: ' + checked.reason)
    plain = _decrypt_security(eeprom, checked.revision)
    raw = struct.unpack_from('<I', plain, 24)[0]
    low = raw & 0x7fffffff
    if low & (low-1):
        raise ValueError('console-settings region domain rejected')
    return raw if raw else 0x80000000

def prepare(eeprom, output):
    region = derive_region(eeprom)
    digest = hashlib.sha256(eeprom).digest()
    artifact = MAGIC + digest + struct.pack('<I', region)
    assert len(artifact) == ARTIFACT_BYTES
    receipt = dict(format='XLLESET1', bytes=ARTIFACT_BYTES,
                   eeprom_sha256=digest.hex(), security_authenticated=True,
                   key_values_logged=False, payload_values_logged=False)
    with output.open('xb') as file:
        file.write(artifact)
    output.with_suffix(output.suffix+'.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    return receipt

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--eeprom', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.eeprom.read_bytes(), args.output)))
