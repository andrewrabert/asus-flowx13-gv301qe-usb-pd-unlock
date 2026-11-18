"""Offline stock-415 layout/protocol evidence. Never accesses hardware.

Call-path interpretations are manual disassembly findings; this script checks
image identity, layout bytes, protocol GUIDs and function pointer tables only.
"""
import hashlib
import json
from pathlib import Path
import struct
import uuid
from audit_415_update_paths import pe_reader

ROOT = Path(__file__).resolve().parents[1]

def main():
    capsule = (ROOT / 'bios/bios_version_415/GV301QEAS.415').read_bytes()
    assert hashlib.sha256(capsule).hexdigest() == '55578ba418fb272c751f8e34a2a31c87775a39a8ffd19d8d021d886b17010f94'
    names = ['ReFlash', 'FlashDriver', 'FlashSmiDxe', 'FlashSmiSmm', 'SecSMIFlash', 'CapsuleRuntimeDxe']
    blobs = {n: (ROOT / f'out/update_415/{n}.efi').read_bytes() for n in names}
    readers = {n: pe_reader(b) for n,b in blobs.items()}
    rr = readers['ReFlash']
    matches = []
    for off in range(0x800, len(capsule)-60, 4):
        if (capsule[off:off+16] == rr(0x9210,16) and
            capsule[off+28:off+44] == rr(0x9220,16) and
            capsule[off+44:off+48] == b'ROML'):
            matches.append(off)
    assert len(matches) == 1
    layout = matches[0]
    stride,count = struct.unpack_from('<II',capsule,layout+52)
    assert stride == 40 and count == 8
    rows=[]
    for i in range(count):
        off=layout+60+stride*i
        address,offset,size,kind,flags=struct.unpack_from('<QIIII',capsule,off+16)
        rows.append(dict(index=i,guid=str(uuid.UUID(bytes_le=capsule[off:off+16])),
                         address=hex(address),payload_offset=hex(offset),size=hex(size),kind=kind,flags=hex(flags)))
    ec=rows[4]
    assert (ec['address'],ec['payload_offset'],ec['size'],ec['flags']) == ('0xff031000','0x31000','0x40000','0x200')
    assert 0x800+0x31000 == 0x31800 and 0x31800+0x30000 <= 0x800+0x71000
    fd=readers['FlashDriver']
    assert rr(0x9350,16) == fd(0x6080,16) == readers['FlashSmiDxe'](0x3090,16)
    protocol=struct.unpack('<6Q',fd(0x6090,48))
    assert protocol == (0x1ea4,0x1f3c,0x1fb0,0x2028,0x20cc,0x2100)
    controller=struct.unpack('<Q',fd(0x6258,8))[0]
    assert controller == 0xfec10000
    result=dict(hashes={n:hashlib.sha256(b).hexdigest() for n,b in blobs.items()},
        rom_layout_capsule_offset=hex(layout),regions=rows,
        ec_image_capsule_range=['0x31800','0x61800'],
        host_flash_protocol=str(uuid.UUID(bytes_le=rr(0x9350,16))),
        host_flash_methods=[hex(x) for x in protocol],controller_base=hex(controller),
        smm_flash_protocol=str(uuid.UUID(bytes_le=readers['SecSMIFlash'](0x5010,16))),
        limitations=['Static tables and disassembly, not live execution trace',
                     'SMM registration order and every alternate path not established',
                     'No EC boot-ROM mirroring or IT5125 programming semantics established',
                     'No standalone unsigned EC acceptance established'])
    print(json.dumps(result,indent=2))

if __name__ == '__main__':
    main()
