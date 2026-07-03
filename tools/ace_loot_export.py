#!/usr/bin/env python3
"""Extract retail loot tiers from the ACE-World database -> assets/acloot.json

Retail loot is a TIERED GENERATOR, not fixed drop lists:
  creature.DeathTreasureType (PropertyDataId 35) -> treasure_death row -> tier(1-8) + item/magic counts.
We pull, per game bestiary kind, its authentic loot tier and the tier's drop parameters, so kills
drop the real NUMBER of items at the real tier with the real magic-vs-mundane split. Also the
tier -> value/workmanship envelope so item worth tracks retail.
"""
import re, os, json, statistics
from collections import defaultdict

ACD = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "acdata")
SQL = next(os.path.join(ACD, f) for f in os.listdir(ACD) if f.endswith(".sql"))
data = open(SQL, encoding="utf-8", errors="replace").read()
print("sql %d MB" % (len(data) // 1_000_000))

def stmts(t):
    for m in re.finditer(r"INSERT INTO `%s`(?: \([^)]*\))? VALUES (.*?);\n" % t, data, re.S):
        yield m.group(1)

def rows(b):
    out, depth, cur, q = [], 0, [], False
    i, n = 0, len(b)
    while i < n:
        c = b[i]
        if q:
            if c == chr(92):   # backslash escape
                i += 2; continue
            if c == "'": q = False
        else:
            if c == "'": q = True
            elif c == "(":
                depth += 1
                if depth == 1: cur = []; i += 1; continue
            elif c == ")":
                depth -= 1
                if depth == 0: out.append("".join(cur))
        if depth >= 1: cur.append(c)
        i += 1
    return out

def fld(r):
    out, cur, q = [], [], False
    for c in r:
        if q:
            if c == "'": q = False
            else: cur.append(c)
        else:
            if c == "'": q = True
            elif c == ",": out.append("".join(cur)); cur = []
            else: cur.append(c)
    out.append("".join(cur))
    return out

# treasure_death: treasure_Type -> tier + counts
tprofile = {}
for b in stmts("treasure_death"):
    for r in rows(b):
        f = fld(r)
        tprofile[int(f[1])] = {"tier": int(f[2]), "imin": int(f[6]), "imax": int(f[7]),
                               "mmin": int(f[10]), "mmax": int(f[11]), "mchance": int(f[9])}
print("treasure_death profiles:", len(tprofile))

weenie, names = {}, {}
for b in stmts("weenie"):
    for r in rows(b):
        f = fld(r); weenie[int(f[0])] = int(f[2])
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fld(r)
        if int(f[2]) == 1: names[int(f[1])] = f[3]
dtt = {}
for b in stmts("weenie_properties_d_i_d"):
    for r in rows(b):
        f = fld(r)
        if int(f[2]) == 35: dtt[int(f[1])] = int(f[3])
print("creatures with DeathTreasureType:", len(dtt))

KIND = [("olthoisoldier", r"olthoi soldier"), ("olthoiworker", r"olthoi worker"), ("olthoi", r"olthoi"),
        ("phyntoswasp", r"phyntos"), ("armoredillo", r"armoredillo"), ("banderling", r"banderling"),
        ("drudge", r"drudge"), ("mosswart", r"mosswart"), ("reedshark", r"reed ?shark"),
        ("tusker", r"tusker"), ("auroch", r"auroch"), ("shadow", r"shadow|umbris"), ("gromnie", r"gromnie"),
        ("lugian", r"lugian"), ("tumerok", r"tumerok"), ("mattekar", r"mattekar"), ("skeleton", r"skeleton"),
        ("virindi", r"virindi"), ("rat", r"\brat\b"), ("shreth", r"shreth"), ("mite", r"mite"),
        ("sclavus", r"sclavus"), ("zefir", r"zefir"), ("monouga", r"monouga"), ("ursuin", r"ursuin"),
        ("moarsman", r"moarsman"), ("wisp", r"wisp"), ("golem", r"golem"), ("zombie", r"zombie"),
        ("mummy", r"mumiyah|mummy"), ("grievver", r"grievver"), ("carenzi", r"carenzi"),
        ("niffis", r"niffis"), ("remoran", r"remoran"), ("burun", r"burun"), ("mukkir", r"mukkir")]
KR = [(k, re.compile(p)) for k, p in KIND]
def kof(nm):
    n = nm.lower()
    for k, rx in KR:
        if rx.search(n): return k
    return None

perkind = defaultdict(list)
for wid, tt in dtt.items():
    if weenie.get(wid) != 10: continue
    k = kof(names.get(wid, ""))
    if k and tt in tprofile: perkind[k].append(tprofile[tt]["tier"])

kinds = {}
print("\nkind -> retail loot tier (median):")
for k in sorted(perkind):
    tiers = perkind[k]; med = int(statistics.median(tiers))
    kinds[k] = {"tier": med, "n": len(tiers)}
    print(f"  {k:15s} tier {med}  (n={len(tiers)}, {min(tiers)}-{max(tiers)})")

# tier params: aggregate ALL profiles of each tier (ignore wielded-only 0-0 rows), take the
# typical max item + magic counts so drop generosity tracks retail.
agg = defaultdict(lambda: {"items": [], "magic": []})
for p in tprofile.values():
    if p["imax"] > 0: agg[p["tier"]]["items"].append(p["imax"])
    if p["mmax"] > 0: agg[p["tier"]]["magic"].append(p["mmax"])
tiers = {}
print("\ntier params (median max item / median max magic across all profiles):")
for t in sorted(agg):
    it = agg[t]["items"] or [1]; mg = agg[t]["magic"] or [1]
    tiers[str(t)] = {"items": int(statistics.median(it)), "magic": int(statistics.median(mg))}
    print(f"  tier {t}: up to {tiers[str(t)]['items']} items + {tiers[str(t)]['magic']} magic")

out = {"kinds": kinds, "tiers": tiers}
path = os.path.join(os.path.dirname(ACD), "assets", "acloot.json")
json.dump(out, open(path, "w", encoding="utf-8"), ensure_ascii=False)
print(f"\nwrote {path} ({os.path.getsize(path)} bytes)")
