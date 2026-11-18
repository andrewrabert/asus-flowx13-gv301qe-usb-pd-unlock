"""Read-only PSPTool adapter for the verified stock-layout FET at raw 0x20000.

Upstream's preceding-padding heuristic misses this FET because it follows data.
No firmware bytes are modified. Run in the isolated PSPTool environment.
"""
import sys
from pathlib import Path
from psptool import PSPTool
from psptool.blob import Blob
b=Path(sys.argv[1]).read_bytes()
if len(b)!=0x1000000 or b[0x20000:0x20004]!=bytes.fromhex('aa55aa55'):
    raise ValueError('Expected 16 MiB raw mapping with stock-position FET')
if b'\xff'*16 not in b[0x20004:0x20400]:
    raise ValueError('Missing bounded FET terminator')
# Only supply the explicitly checked canonical location; preserve parser internals.
Blob._find_fets=lambda self: iter([0x20000])
pt=PSPTool(bytearray(b),filename=sys.argv[1])
pt.ls_json()
