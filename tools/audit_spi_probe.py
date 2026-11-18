"""Decode fixed host SPI snapshot logs; no hardware access or write support."""
import json
from pathlib import Path
import re
import sys

def audit(text):
    # Collection output starts with JSON and may append bcdedit text.
    if 'DiagnosticLog' in text and '{' in text:
        obj,_=json.JSONDecoder().raw_decode(text[text.index('{'):])
        text=obj['DiagnosticLog']
    if 'result=0 pci_writes=0 mmio_writes=0 spi_commands=0 ec_access=0' not in text:
        raise ValueError('Snapshot did not complete successfully')
    pci={}; mmio={}
    for p,o,v,status in re.findall(r'pass=(\d+) pci=00:14.3 offset=([0-9a-fA-F]+) value=([0-9a-fA-F]+) status=([0-9a-fA-F]+)',text):
        if int(status,16):raise ValueError('PCI read failed')
        key=(int(p),int(o,16))
        if key in pci:raise ValueError('Duplicate PCI row')
        pci[key]=int(v,16)
    for p,a,v,status in re.findall(r'pass=(\d+) mmio=([0-9a-fA-F]+) value=([0-9a-fA-F]+) status=([0-9a-fA-F]+)',text):
        if int(status,16):raise ValueError('MMIO read failed')
        key=(int(p),int(a,16))
        if key in mmio:raise ValueError('Duplicate MMIO row')
        mmio[key]=int(v,16)
    offsets=[0,8,0x40,0x44,0x48,0x50,0x54,0x58,0x5c,0xa0,0xb8]
    assert set(pci)=={(p,o) for p in range(2) for o in offsets}
    assert set(mmio)=={(p,a) for p in range(2) for a in [0xfec10000,0xfec10004,0xfec10008]}
    assert all(pci[0,o]==pci[1,o] for o in offsets), 'PCI changed between passes'
    assert all(mmio[0,a]==mmio[1,a] for a in [0xfec10000,0xfec10004,0xfec10008]), 'MMIO changed between passes'
    assert pci[0,0]==0x790e1022 and pci[0,0xa0]&0xffffffe0==0xfec10000
    control=mmio[0,0xfec10000]
    regions=[]
    for o in [0x50,0x54,0x58,0x5c]:
        v=pci[0,o];start=v&0xfffff800;end=start+(((v&0x7fc)<<8)|0x3ff)
        regions.append(dict(register=hex(o),raw=hex(v),read_protected=bool(v&2),write_protected=bool(v&1),start=hex(start),end_inclusive=hex(end)))
    return dict(two_passes_match=True,control0=hex(control),spi_access_mac_rom_en=bool(control&(1<<22)),spi_host_access_rom_en=bool(control&(1<<23)),restricted_command_dwords=[hex(mmio[0,a]) for a in [0xfec10004,0xfec10008]],rom_protect=regions,limitations=['UEFI-time snapshot, not Windows runtime or irreversible-lock proof','No flash ID/status/data read and no write attempted','Unprotected ranges do not establish write capability or EC boot behavior'])
if __name__=='__main__':
    print(json.dumps(audit(Path(sys.argv[1]).read_text()),indent=2))
