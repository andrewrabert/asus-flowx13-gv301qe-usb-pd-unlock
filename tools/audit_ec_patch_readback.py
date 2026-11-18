#!/usr/bin/env python3
"""Classify two successful complete post-patch EC readbacks; no hardware access."""
import argparse
import hashlib
import json
import re
from pathlib import Path
from prepare_ec_candidate import patch

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=Path)
a=p.parse_args()
r=json.loads((a.directory/'result.json').read_text())
log=r['DiagnosticLog']
for key,want in [('result',0),('completed_passes',2),('window',0x30000),
                 ('last_result',0),('incomplete',0),('dirty',0),('restored',1)]:
    m=re.search(r'(?<![\w])'+key+r'=(\d+)',log)
    if not m or int(m[1])!=want: raise SystemExit(f'Invalid backup completion: {key}')
if 'chip=51,25,02,' not in log: raise SystemExit('Unexpected EC identification')
stock=Path('out/update_415/uefi_backup_probe/ec-backup-pass0.bin').read_bytes()
if hashlib.sha256(stock).hexdigest()!='5bc14fb3c61c9fa346210d18c67d45c93b7ef020e7d1c06369fe1d92a5fb7478':
    raise SystemExit('Original reference hash mismatch')
expected=patch(stock,stock)
b=[(a.directory/f'ec-backup-pass{i}.bin').read_bytes() for i in range(2)]
if len(b[0])!=len(stock) or b[0]!=b[1]: raise SystemExit('Readouts must be matching complete 192KiB passes')
changed=[i for i,(x,y) in enumerate(zip(stock,b[0])) if x!=y]
classification='PATCHED' if b[0]==expected else 'STOCK' if b[0]==stock else 'OTHER'
report=dict(classification=classification,sha256=hashlib.sha256(b[0]).hexdigest(),
            bytes=len(b[0]),passes_match=True,changed_bytes=len(changed),
            first_64_changed_ec_offsets=[hex(i) for i in changed[:64]],
            patch_sites={hex(i):b[0][i:i+n].hex() for i,n in [(0xc5ee,4),(0x11efa,2)]},
            secondary_predicates={hex(i):b[0][i:i+4].hex() for i in [0xc5f6,0x11efe]},
            last_windows_boot=r['LastBoot'],behavior_tested=False)
(a.directory/'patch-readback-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
