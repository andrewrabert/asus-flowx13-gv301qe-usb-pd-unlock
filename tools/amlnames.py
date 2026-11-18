import sys, re, struct, collections
d = open(sys.argv[1],'rb').read()[36:]
# NameSegs: 4 chars from A-Z0-9_ , first char A-Z or _
segs = collections.Counter()
for m in re.finditer(rb'(?=([A-Z_][A-Z0-9_]{3}))', d):
    segs[m.group(1).decode()] += 1
pat = re.compile(sys.argv[2], re.I) if len(sys.argv)>2 else None
for s,c in segs.most_common():
    if pat is None or pat.search(s): print(f'{s} x{c}')
