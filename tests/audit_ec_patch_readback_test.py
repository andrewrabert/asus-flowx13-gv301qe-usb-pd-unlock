"""Check that the readback auditor cannot mistake partial/bad data for a patch."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, 'tools')
from prepare_ec_candidate import patch
stock=Path('out/update_415/uefi_backup_probe/ec-backup-pass0.bin').read_bytes()
modified=patch(stock,stock)
log='result=0 completed_passes=2 window=196608 chip=51,25,02,00,00,00\nlast_result=0 incomplete=0 dirty=0 restored=1'
with tempfile.TemporaryDirectory() as td:
    d=Path(td)
    def run(a,b,status=log):
        (d/'result.json').write_text(json.dumps({'DiagnosticLog':status,'LastBoot':'TEST FIXTURE'}))
        (d/'ec-backup-pass0.bin').write_bytes(a)
        (d/'ec-backup-pass1.bin').write_bytes(b)
        return subprocess.run([sys.executable,'tools/audit_ec_patch_readback.py',td],capture_output=True,text=True)
    for data,expected in [(stock,'STOCK'),(modified,'PATCHED'),(modified[:-1]+bytes([modified[-1]^1]),'OTHER')]:
        r=run(data,data);assert r.returncode==0,r.stderr
        assert json.loads(r.stdout)['classification']==expected
    for a,b,status in [(modified,stock,log),(modified[:-1],modified[:-1],log),
                       (modified,modified,log.replace('result=0 ','result=2 ',1)),
                       (modified,modified,log.replace('restored=1','restored=0')),
                       (modified,modified,log.replace('chip=51,25,02','chip=51,26,02'))]:
        assert run(a,b,status).returncode!=0
print('PASS: exact stock/patched/other classification; unequal/truncated/failed/unrestored/wrong-chip readouts refused.')
