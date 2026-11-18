#!/usr/bin/env python3
"""Offline-only host-ROM derivative. No hardware access or flashing support."""
import argparse,json
from pathlib import Path
from prepare_ec_candidate import patch,digest
SIZE=0x1000000
EC_START=0x31000
EC_SIZE=0x30000

def prepare(a,b,ec0,ec1):
    if len(a)!=SIZE or a!=b:
        raise ValueError('requires identical 16 MiB mapped host readouts')
    if a[EC_START:EC_START+EC_SIZE]!=ec0 or ec0!=ec1:
        raise ValueError('host EC image must match both internal EC readouts')
    ec=patch(ec0,ec1)
    out=bytearray(a);out[EC_START:EC_START+EC_SIZE]=ec
    changed=[i for i,(x,y) in enumerate(zip(a,out)) if x!=y]
    expected=[EC_START+i for i,(x,y) in enumerate(zip(ec0,ec)) if x!=y]
    if changed!=expected:raise ValueError('unexpected host changes')
    pages=sorted({i&~0xfff for i in changed})
    return bytes(out),changed,pages

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['host0','host1','ec0','ec1','output']:p.add_argument(name,type=Path)
    args=p.parse_args();a=args.host0.read_bytes()
    out,changed,pages=prepare(a,args.host1.read_bytes(),args.ec0.read_bytes(),args.ec1.read_bytes())
    args.output.mkdir(parents=True,exist_ok=False)
    (args.output/'host-vid-gates.UNAPPROVED.bin').write_bytes(out)
    for page in pages:
        (args.output/f'{page:06x}.original-4k.bin').write_bytes(a[page:page+0x1000])
        (args.output/f'{page:06x}.modified-4k.UNAPPROVED.bin').write_bytes(out[page:page+0x1000])
    m=dict(status='OFFLINE DERIVATIVE ONLY; NOT CLEARED FOR PROGRAMMING',backup_sha256=digest(a),candidate_sha256=digest(out),size=len(out),changed_bytes=list(map(hex,changed)),affected_4k_pages=list(map(hex,pages)),preserves_all_other_live_bytes=True,limitations=['4 KiB files are comparison artifacts, not an established erase geometry','No validated unsigned programming, EC propagation, boot authentication or recovery route'])
    (args.output/'manifest.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m,indent=2))
if __name__=='__main__':main()
