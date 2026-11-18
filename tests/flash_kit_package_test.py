"""Offline tests for the downloadable kit. No firmware or Windows operations."""
import hashlib
import json
from pathlib import Path
import re
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
KIT = ROOT / 'flash-kit'
MAINTAINER = ROOT / 'tools/flash-kit-maintainer'


class PackageTest(unittest.TestCase):
    def test_minimal_download(self):
        self.assertEqual({p.name for p in KIT.iterdir() if p.name != '.gitignore'},
                         {'README.md', 'flash.cmd', 'flash.ps1', 'payload.zip', 'LICENSES.txt'})

    def test_payload_and_manifest(self):
        launcher = (KIT / 'flash.ps1').read_text()
        with zipfile.ZipFile(KIT / 'payload.zip') as archive:
            payload_hash = re.search(r"\$payloadHash = '([A-F0-9]{64})'", launcher)[1]
            self.assertEqual(hashlib.sha256((KIT / 'payload.zip').read_bytes()).hexdigest().upper(), payload_hash)
            manifest_hash = re.search(r"\$manifestHash = '([A-F0-9]{64})'", launcher)[1]
            self.assertEqual(hashlib.sha256(archive.read('SHA256.json')).hexdigest().upper(), manifest_hash)
            manifest = json.loads(archive.read('SHA256.json'))
            self.assertEqual(set(archive.namelist()), set(manifest) | {'SHA256.json'})
            for name, expected in manifest.items():
                self.assertNotIn('..', Path(name).parts)
                self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), expected)
                if name.startswith('bin/'):
                    self.assertEqual(archive.read(name), (MAINTAINER / name).read_bytes())
            self.assertEqual(len([n for n in manifest if n.endswith('.efi')]), 4)
            engine = archive.read('engine.ps1').decode()
            self.assertIn("'CheckComplete' { No-Pending; Check-Flash; Check-Verify; Check-EC $work $patchedEC }", engine)
            self.assertIn("'..\\backups'", engine)
            for name in manifest:
                if name.startswith('scripts/'):
                    self.assertIn("'..\\..\\backups'", archive.read(name).decode())
            self.assertEqual(archive.read('payload/original-sectors.bin'), (MAINTAINER / 'payload/original-sectors.bin').read_bytes())


if __name__ == '__main__':
    unittest.main()
