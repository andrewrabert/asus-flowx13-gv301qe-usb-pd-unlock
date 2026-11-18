#!/usr/bin/env python3
"""Pin and audit the reviewed VID-only payload before building its EFI updater."""
import hashlib
import json
from pathlib import Path
from prepare_host_candidate import prepare

def sha(b):
    return hashlib.sha256(b).hexdigest()

root = Path('out/update_415')
host = [(root/'uefi_host_backup'/f'host-backup-pass{i}.bin').read_bytes() for i in range(2)]
ec = [(root/'uefi_backup_probe'/f'ec-backup-pass{i}.bin').read_bytes() for i in range(2)]
assert sha(host[0]) == '74c8b75a51496d9d7821883552b0157a6685e233aa0581cdf888a899680ffade'
assert sha(ec[0]) == '5bc14fb3c61c9fa346210d18c67d45c93b7ef020e7d1c06369fe1d92a5fb7478'
candidate, changed, sectors = prepare(*host, *ec)
assert sha(candidate) == 'a7132dae37e0ebe8632de0da5ff4b9fa977e247781eab35c1f426bb76a0b1369'
assert changed == [0x3d5ee, 0x3d5f0, 0x3d5f1, 0x42efa, 0x42efb]
assert sectors == [0x3d000, 0x42000]
payload = b''.join(candidate[a:a+4096] for a in sectors)
for n, a in enumerate(sectors):
    bundle = root/'host_sector_bundle_v1'
    assert (bundle/f'{a:06x}.original.bin').read_bytes() == host[0][a:a+4096]
    assert (bundle/f'{a:06x}.modified.UNAPPROVED.bin').read_bytes() == payload[n*4096:(n+1)*4096]
header = '#ifndef HOST_SPI_VID_SECTORS_H\n#define HOST_SPI_VID_SECTORS_H\n#include <stdint.h>\nstatic const uint8_t host_spi_vid_sectors[8192] = {\n'
header += ''.join(' '+','.join(f'0x{x:02x}' for x in payload[i:i+16])+',\n' for i in range(0,len(payload),16))
header += '};\n#endif\n'
Path('tools/host_spi_vid_sectors.h').write_text(header)
dest = root/'uefi_smi_vid_patch'
dest.mkdir(exist_ok=True)
audit = dict(host_backup_sha256=sha(host[0]), ec_backup_sha256=sha(ec[0]),
             candidate_sha256=sha(candidate), embedded_payload_sha256=sha(payload),
             changed_host_offsets=[hex(a) for a in changed],
             write_sectors=[hex(a) for a in sectors],
             preserves_secondary_100_predicates=True,
             ec_reload='UNVERIFIED; requires post-reboot internal readback')
(dest/'input-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps(audit,indent=2))
