#!/usr/bin/env python3
"""Build the small distributable from the retained reviewed sources and binaries."""
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parent
DEST = ROOT.parents[1] / 'flash-kit'


def build():
    files = {}
    for folder in ('bin', 'scripts'):
        for path in sorted((ROOT / folder).iterdir()):
            data = path.read_bytes()
            if path.suffix == '.ps1':
                data = data.replace(b"'..\\work'", b"'..\\..\\backups'")
            files[f'{folder}/{path.name}'] = data
    files['payload/original-sectors.bin'] = (ROOT / 'payload/original-sectors.bin').read_bytes()
    engine = (ROOT / 'flash.ps1').read_text()
    engine = engine.replace("'CollectVerifyEC','Cancel'", "'CollectVerifyEC','Cancel','CheckComplete'")
    engine = engine.replace("$work = Join-Path $PSScriptRoot 'work'", "$work = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\\backups'))")
    engine = engine.replace("switch ($Action) {", "switch ($Action) {\n    'CheckComplete' { No-Pending; Check-Flash; Check-Verify; Check-EC $work $patchedEC }")
    files['engine.ps1'] = engine.encode()
    manifest = {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}
    files['SHA256.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    with zipfile.ZipFile(DEST / 'payload.zip', 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, (2026, 11, 18, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    launcher = DEST / 'flash.ps1'
    text = launcher.read_text()
    for variable, data in [('payloadHash', (DEST / 'payload.zip').read_bytes()), ('manifestHash', files['SHA256.json'])]:
        text, count = re.subn(rf"\${variable} = '[^']+'", f"${variable} = '{hashlib.sha256(data).hexdigest().upper()}'", text)
        assert count == 1
    launcher.write_text(text)
    notices = ['GNU-EFI notices for the libraries linked into the included EFI binaries.\n']
    for path in sorted((ROOT / 'licenses/gnu-efi').iterdir()):
        notices.extend([f'\n--- {path.name} ---\n', path.read_text()])
    (DEST / 'LICENSES.txt').write_text(''.join(notices))
    print(f'Packaged {len(files)} runtime files into {DEST / "payload.zip"}')


if __name__ == '__main__':
    build()
