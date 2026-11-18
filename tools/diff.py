import sys, re
sys.path.insert(0, '/home/ar/src/bios/tools')
from fwx import load
a = load(sys.argv[1]); b = load(sys.argv[2])
ka, kb = set(a.mods), set(b.mods)
def nm(im, k): return im.mods[k].get('name') or '(unnamed)'
print(f"A={len(ka)} B={len(kb)}  only-A={len(ka-kb)} only-B={len(kb-ka)}")
print("\n--- only in NEW (418) ---")
for k in sorted(kb-ka, key=lambda k: nm(b,k)): print(f"  {nm(b,k):40s} {k} sz={b.mods[k]['size']}")
print("\n--- only in OLD (415) ---")
for k in sorted(ka-kb, key=lambda k: nm(a,k)): print(f"  {nm(a,k):40s} {k} sz={a.mods[k]['size']}")
ch = [k for k in ka & kb if a.mods[k]['sha'] != b.mods[k]['sha']]
print(f"\n--- changed: {len(ch)} of {len(ka&kb)} ---")
for k in sorted(ch, key=lambda k: nm(b,k)):
    print(f"  {nm(b,k):40s} {k} {a.mods[k]['size']} -> {b.mods[k]['size']}")
