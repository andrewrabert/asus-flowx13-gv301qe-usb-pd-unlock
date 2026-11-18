import sys, struct, uuid, hashlib, zlib

FV_SIG = b'_FVH'

def guid(b):
    return str(uuid.UUID(bytes_le=b)).upper()

def find_fvs(data):
    out = []
    off = 0
    while True:
        i = data.find(FV_SIG, off)
        if i < 0: break
        base = i - 40
        if base < 0:
            off = i + 4; continue
        zerov, fsg, flen, sig, attr, hdrlen, csum, ext, rev = struct.unpack_from('<16s16sQ4sIHHHxB', data, base)
        if flen and base + flen <= len(data) and 56 <= hdrlen < 1024 and rev in (1, 2):
            start = hdrlen
            if ext:
                extsz = struct.unpack_from('<I', data, base + ext + 16)[0]
                start = (ext + extsz + 7) & ~7
            out.append((base, flen, guid(fsg), start))
        off = i + 4
    return out

def sections(data, base, end, depth=0, acc=None):
    # iterate FFS sections
    while base + 4 <= end:
        s0, s1, s2, stype = struct.unpack_from('<BBBB', data, base)
        size = s0 | s1 << 8 | s2 << 16
        hdr = 4
        if size == 0xFFFFFF:
            size = struct.unpack_from('<I', data, base + 4)[0]
            hdr = 8
        if size < hdr or base + size > end: break
        body = data[base + hdr: base + size]
        if stype == 0x15:  # user interface / name
            try: acc['name'] = body.decode('utf-16-le').rstrip('\x00')
            except Exception: pass
        elif stype == 0x01:  # compression
            ctype = body[4]
            if ctype == 0:
                sections(data, base + hdr + 5, base + size, depth+1, acc)
        elif stype == 0x02:  # GUID defined
            sg = guid(body[:16]); doff = struct.unpack_from('<H', body, 16)[0]
            acc.setdefault('guided', []).append(sg)
        elif stype in (0x10, 0x11, 0x12, 0x13, 0x19):
            acc.setdefault('bodies', []).append((stype, body))
        base += (size + 3) & ~3
    return acc

def files(data, fvbase, fvlen, hdrlen):
    base = fvbase + hdrlen
    end = fvbase + fvlen
    while base + 24 <= end:
        fg = data[base:base+16]
        if fg == b'\xff'*16: break
        ftype = data[base+18]
        attr = data[base+19]
        sz = data[base+20] | data[base+21] << 8 | data[base+22] << 16
        hdr = 24
        if sz == 0xFFFFFF:
            sz = struct.unpack_from('<Q', data, base+24)[0]
            hdr = 32
        if sz < hdr or base + sz > end: break
        acc = {'guid': guid(fg), 'type': ftype, 'size': sz,
               'sha': hashlib.sha256(data[base+hdr:base+sz]).hexdigest()[:16]}
        try: sections(data, base + hdr, base + sz, 0, acc)
        except Exception: pass
        yield acc
        base += (sz + 7) & ~7

def inventory(path):
    data = open(path, 'rb').read()
    inv = {}
    for (b, l, g, h) in find_fvs(data):
        for f in files(data, b, l, h):
            inv[f['guid']] = f
    return data, inv

if __name__ == '__main__':
    d, inv = inventory(sys.argv[1])
    print(len(d), 'bytes', len(inv), 'files')
    for g, f in list(inv.items())[:10]:
        print(g, f['type'], f['size'], f.get('name'))
