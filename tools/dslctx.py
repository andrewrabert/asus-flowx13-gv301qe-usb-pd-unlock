import sys, re
path, pat = sys.argv[1], sys.argv[2]
lines = open(path, errors='replace').read().split('\n')
rx = re.compile(pat)
# find enclosing Method/Scope for each hit
mre = re.compile(r'^\s*(Method|Scope|Device)\s*\(([^,)]+)')
starts = [(i, m.group(1), m.group(2).strip()) for i, l in enumerate(lines) if (m := mre.match(l))]
def encl(i):
    best = None
    for s, k, n in starts:
        if s <= i: best = (s, k, n)
        else: break
    return best
hits = [i for i, l in enumerate(lines) if rx.search(l)]
groups = {}
for i in hits:
    e = encl(i)
    groups.setdefault(e, []).append(i)
for e, idxs in groups.items():
    print(f'\n######## in {e[1]} ({e[2]}) @line {e[0]+1}')
    lo, hi = max(0, min(idxs)-14), min(len(lines), max(idxs)+14)
    for n in range(lo, hi):
        print(f'{n+1:6d}: {lines[n]}')
