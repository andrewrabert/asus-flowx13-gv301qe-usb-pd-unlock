"""Minimal HII IFR + string package parser."""
import sys, struct, re

def parse_strings(b):
    """Return {strid: text} from all string packages found."""
    out = {}
    for m in re.finditer(rb'en-US\x00', b):
        # header: Length(24)|Type(8), HdrSize(4), StringInfoOffset(4), LangWindow[16*2], LanguageName(2), Language[]
        # Language string starts at base+44
        base = m.start() - 46
        if base < 0: continue
        ln = int.from_bytes(b[base:base+3], 'little'); typ = b[base+3]
        if typ != 0x04 or not (46 < ln <= len(b) - base): continue
        hdrsz, sio = struct.unpack_from('<II', b, base+4)
        o = base + sio
        sid = 1
        end = base + ln
        while o < end:
            bt = b[o]; o += 1
            if bt == 0x00: break                       # END
            elif bt == 0x10:                           # STRING_SCSU
                e = b.index(b'\x00', o); out[sid] = b[o:e].decode('latin1'); sid += 1; o = e+1
            elif bt == 0x14:                           # STRING_UCS2
                e = o
                while e+1 < end and b[e:e+2] != b'\x00\x00': e += 2
                out[sid] = b[o:e].decode('utf-16-le', 'replace'); sid += 1; o = e+2
            elif bt == 0x15:                           # STRING_UCS2_FONT
                o += 1
                e = o
                while e+1 < end and b[e:e+2] != b'\x00\x00': e += 2
                out[sid] = b[o:e].decode('utf-16-le','replace'); sid += 1; o = e+2
            elif bt == 0x11:                           # STRINGS_SCSU (multiple)
                n = struct.unpack_from('<H', b, o)[0]; o += 2
                for _ in range(n):
                    e = b.index(b'\x00', o); out[sid] = b[o:e].decode('latin1'); sid += 1; o = e+1
            elif bt == 0x16:                           # STRINGS_UCS2
                n = struct.unpack_from('<H', b, o)[0]; o += 2
                for _ in range(n):
                    e = o
                    while e+1 < end and b[e:e+2] != b'\x00\x00': e += 2
                    out[sid] = b[o:e].decode('utf-16-le','replace'); sid += 1; o = e+2
            elif bt == 0x20: sid += 1                  # DUPLICATE
            elif bt == 0x21: o += 2                    # SKIP2
            elif bt == 0x22: sid += b[o]; o += 1       # SKIP1
            elif bt == 0x30: o += 2                    # EXT1
            elif bt == 0x31: o += 3
            elif bt == 0x32: o += 5
            else: break
    return out

OPS = {0x01:'FORM',0x02:'SUBTITLE',0x03:'TEXT',0x05:'ONE_OF',0x06:'CHECKBOX',0x07:'NUMERIC',
       0x08:'PASSWORD',0x09:'ONE_OF_OPTION',0x0A:'SUPPRESS_IF',0x0B:'LOCKED',0x0C:'ACTION',
       0x0E:'FORM_SET',0x0F:'REF',0x10:'NO_SUBMIT_IF',0x11:'INCONSISTENT_IF',0x12:'EQ_ID_VAL',
       0x18:'VARSTORE',0x19:'VARSTORE_NAME_VALUE',0x1A:'VARSTORE_EFI',0x1B:'VARSTORE_DEVICE',
       0x1D:'END',0x1E:'DEFAULT',0x20:'GRAY_OUT_IF',0x22:'DISABLE_IF',0x23:'ORDERED_LIST',
       0x24:'VARSTORE_ID',0x25:'STRING',0x29:'END_OP',0x2A:'DATE',0x2B:'TIME',0x5F:'GUID'}

def walk_forms(b, strs):
    res = []
    for m in re.finditer(rb'[\x00-\xff]', b''):  # placeholder
        pass
    # find FORMS packages: scan for plausible package header type 0x02
    for base in range(0, len(b) - 8):
        ln = int.from_bytes(b[base:base+3], 'little'); typ = b[base+3]
        if typ != 0x02 or not (32 < ln <= len(b) - base): continue
        # next byte should begin an IFR opcode FORM_SET (0x0E) header
        if b[base+4] != 0x0E: continue
        res.append((base, ln))
    return res

def ops(b, start, end):
    o = start
    out = []
    while o + 2 <= end:
        op = b[o]; hdr = b[o+1]
        length = hdr & 0x7F
        scope = (hdr >> 7) & 1
        if length < 2: break
        out.append((o, op, scope, b[o+2:o+length]))
        o += length
    return out
