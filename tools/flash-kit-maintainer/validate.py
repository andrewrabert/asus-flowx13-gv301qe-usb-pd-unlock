#!/usr/bin/env python3
"""Offline integrity and payload validation. Never accesses hardware."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
TESTED = {
    'host-backup.efi': 'abb02ada0f7b353122d23f180067e020204afd921ad7fe60449acc2e5f0a2402',
    'backup-probe.efi': '75af4282af1c86dd903d53961601da3aee13db67c2eb6bb3a0701d8e5d4d9935',
    'smi-vid-patch.efi': '09c45b4d540ca4ce5a03c56f3c2422c33273c2a4d68c985750cec516482d6a23',
    'smi-vid-verify.efi': '0e230e4e07fdc6daf3bae2e8f2f606a737c012c16239664331735a4323d989b5',
}


def validate(root=ROOT):
    manifest = json.loads((root / 'SHA256.json').read_text())
    for name, expected in manifest.items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected.lower(), name
    for name, expected in TESTED.items():
        assert hashlib.sha256((root / 'bin' / name).read_bytes()).hexdigest() == expected, name
    images = []
    for name in ('original', 'vid'):
        payload = (root / 'payload' / f'{name}-sectors.bin').read_bytes()
        header = (root / 'source' / f'host_spi_{name}_sectors.h').read_text()
        array = header.split('{', 1)[1].split('}', 1)[0]
        assert payload == bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', array))
        assert len(payload) == 8192
        images.append(payload)
    changes = [i for i, (a, b) in enumerate(zip(*images)) if a != b]
    assert changes == [0x5EE, 0x5F0, 0x5F1, 0x1EFA, 0x1EFB], changes
    for f in (root / 'source').glob('*'):
        for include in re.findall(r'#include "([^"]+)"', f.read_text()):
            assert (root / 'source' / include).is_file(), include
    for f in (root / 'scripts').glob('*.ps1'):
        text = f.read_text()
        assert '$env:TEMP' not in text
        assert '3F8A4CBBC97B89836DF92C4895236BE1FAC4A127665420D90509E897A7298511' not in text
        assert 'bootmgr.sha256' in text
    print(f'PASS: {len(manifest)} manifest files, four exact tested binaries, complete sources, exact five-byte payload delta')


if __name__ == '__main__':
    validate()
