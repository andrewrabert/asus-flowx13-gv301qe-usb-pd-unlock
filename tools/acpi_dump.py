import sys, os, hashlib, struct
sys.path.insert(0,'/home/ar/src/bios/tools')
from fwx import load
im = load(sys.argv[1]); out = sys.argv[2]
os.makedirs(out, exist_ok=True)
seen = set(); n = 0
for p, b in im.blobs:
    if len(b) < 36: continue
    sig = b[:4]
    if sig not in (b'DSDT', b'SSDT', b'FACP', b'MCFG', b'HPET'): continue
    ln = struct.unpack_from('<I', b, 4)[0]
    if ln > len(b): continue
    t = b[:ln]
    h = hashlib.sha256(t).hexdigest()
    if h in seen: continue
    seen.add(h)
    oem = t[16:24].decode('latin1').strip('\x00 ')
    rev = t[8]
    fn = f'{out}/{sig.decode()}_{oem or "x"}_{ln}_{h[:6]}.aml'
    open(fn, 'wb').write(t); n += 1
    print(f'{sig.decode()} oemtid={oem:9s} len={ln:6d} oemrev={struct.unpack_from("<I",t,24)[0]:#x} -> {os.path.basename(fn)}')
print(n, 'tables ->', out)
