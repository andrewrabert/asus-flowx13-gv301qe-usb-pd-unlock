"""Recursive UEFI firmware extractor: FVs -> FFS files -> sections, LZMA-aware."""
import sys, os, struct, uuid, hashlib, lzma, json, zlib

LZMA_GUIDS = {'EE4E5898-3914-4259-9D6E-DC7BD79403CF',   # LZMA_CUSTOM_DECOMPRESS
              'A1A2CD82-1E99-4D48-B5DE-1C1D6E6C1D1D'}
CRC32_GUID = 'FC1BCDB0-7D31-49AA-936A-A4600D9DD083'
AMD_ZLIB_GUID = 'CE3233F5-2CD6-4D87-9152-4A238BB6D1C4'

def g(b): return str(uuid.UUID(bytes_le=b)).upper()

def unlzma(b):
    for skip in (0, 1):
        try:
            d = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
            return d.decompress(b[skip:])
        except Exception:
            continue
    return None

class Img:
    def __init__(self):
        self.mods = {}   # guid -> dict
        self.blobs = []  # (path, bytes) leaf raw/pe bodies

    def fvs(self, data, path):
        off = 0
        while True:
            i = data.find(b'_FVH', off)
            if i < 0: return
            off = i + 4
            base = i - 40
            if base < 0: continue
            try:
                z, fsg, flen, sig, attr, hdrlen, csum, ext, rev = \
                    struct.unpack_from('<16s16sQ4sIHHHxB', data, base)
            except struct.error: continue
            if not (flen and base + flen <= len(data) and 56 <= hdrlen < 1024 and rev in (1, 2)):
                continue
            start = hdrlen
            if ext:
                try: start = (ext + struct.unpack_from('<I', data, base+ext+16)[0] + 7) & ~7
                except struct.error: pass
            self.files(data, base, flen, start, f'{path}/FV@{base:#x}')
            off = base + flen

    def files(self, data, fvbase, fvlen, start, path):
        b = fvbase + start; end = fvbase + fvlen
        while b + 24 <= end:
            fg = data[b:b+16]
            # Padding files legitimately have an all-FF GUID. Only a completely
            # erased header ends the file list (stock 415 has a pad before PEI).
            if data[b:b+24] == b'\xff'*24: break
            ftype = data[b+18]
            sz = data[b+20] | data[b+21] << 8 | data[b+22] << 16
            hdr = 24
            if sz == 0xFFFFFF:
                sz = struct.unpack_from('<Q', data, b+24)[0]; hdr = 32
            if sz < hdr or b + sz > end: break
            fguid = g(fg)
            m = self.mods.setdefault(fguid, {'guid': fguid, 'type': ftype, 'path': path, 'name': None})
            m['sha'] = hashlib.sha256(data[b+hdr:b+sz]).hexdigest()[:16]
            m['size'] = sz
            if ftype == 0xF0:   # PAD, not a section stream
                pass
            elif ftype == 0x01:   # RAW
                self.blobs.append((f'{path}/{fguid}', data[b+hdr:b+sz]))
                self.fvs(data[b+hdr:b+sz], f'{path}/{fguid}')
            else:
                self.sections(data[b+hdr:b+sz], f'{path}/{fguid}', m)
            b += (sz + 7) & ~7

    def sections(self, buf, path, m, depth=0):
        if depth > 12: return
        o = 0
        while o + 4 <= len(buf):
            sz = buf[o] | buf[o+1] << 8 | buf[o+2] << 16
            st = buf[o+3]; hl = 4
            if sz == 0xFFFFFF:
                sz = struct.unpack_from('<I', buf, o+4)[0]; hl = 8
            if sz < hl or o + sz > len(buf): break
            body = buf[o+hl:o+sz]
            if st == 0x15:                      # USER_INTERFACE
                try: m['name'] = body.decode('utf-16-le').rstrip('\x00')
                except Exception: pass
            elif st == 0x01:                    # COMPRESSION
                ulen, ct = struct.unpack_from('<IB', body, 0)
                inner = body[5:]
                if ct == 0: self.sections(inner, path, m, depth+1)
                elif ct == 2:
                    d = unlzma(inner)
                    if d: self.sections(d, path+'/lzma', m, depth+1)
            elif st == 0x02:                    # GUID_DEFINED
                sg = g(body[:16]); doff = struct.unpack_from('<H', body, 16)[0]
                inner = body[doff-hl if False else 0:]
                payload = buf[o+hl+doff-0:o+sz] if doff else body[20:]
                payload = buf[o+doff:o+sz]
                if sg in LZMA_GUIDS:
                    d = unlzma(payload)
                    if d:
                        self.sections(d, path+'/lzma', m, depth+1)
                        self.fvs(d, path+'/lzma')
                elif sg == AMD_ZLIB_GUID:
                    # AMD prepends a 256-byte header; compressed size is at14h.
                    if len(payload) < 256:
                        raise ValueError('Truncated AMD zlib header')
                    compressed_size = struct.unpack_from('<I', payload, 0x14)[0]
                    if compressed_size != len(payload) - 256:
                        raise ValueError('AMD zlib compressed size mismatch')
                    decoder = zlib.decompressobj()
                    d = decoder.decompress(payload[256:]) + decoder.flush()
                    if not decoder.eof or decoder.unused_data:
                        raise ValueError('Incomplete or trailing AMD zlib stream')
                    self.sections(d, path+'/amd-zlib', m, depth+1)
                    self.fvs(d, path+'/amd-zlib')
                else:
                    self.sections(payload, path, m, depth+1)
            elif st == 0x17:                    # FIRMWARE_VOLUME_IMAGE
                self.fvs(body, path)
            elif st in (0x10, 0x12, 0x13, 0x19): # PE32, TE, DXE_DEPEX, RAW
                self.blobs.append((f'{path}#{st:02x}', body))
                if st == 0x19: self.fvs(body, path)
            o += (sz + 3) & ~3

def load(p):
    im = Img(); data = open(p, 'rb').read(); im.fvs(data, os.path.basename(p)); im.raw = data
    return im

if __name__ == '__main__':
    im = load(sys.argv[1])
    print(len(im.mods), 'modules', len(im.blobs), 'blobs')
    named = [m for m in im.mods.values() if m['name']]
    print(len(named), 'named')
    for m in sorted(named, key=lambda x: x['name'])[:20]:
        print(f"  {m['guid']} {m['name']}")
