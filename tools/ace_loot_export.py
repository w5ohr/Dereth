#!/usr/bin/env python3
"""Retail loot-table extraction -> assets/acloot.json

Streams the 155MB ACE-World SQL dump (same regex/streaming parser as
tools/ace_world_more.py) and reconstructs the retail death-loot system:

  treasure_death   : per (treasure_Type) loot profile -> tier + item/magic/mundane
                     drop counts & chances.
  weenie DID 35    : DeathTreasureType. Empirically this DID property (NOT int 105, which
                     originally was assumed) is the creature->treasure_death link: 96% of
                     its distinct values are treasure_death.treasure_Type ids, carried by
                     3693 weenies.  It links a creature -> its treasure_death profile -> tier.
  treasure_wielded : per (treasure_Type) list of wielded item wcids + probability.
  weenie DID 32    : WieldedTreasureType (98% of its values are treasure_wielded types) ->
                     the wielded-drop pool for a creature.
  create_list d=2  : Wield destination -> named items a creature actually wields/drops.
                     (DestinationType.Wield is 2 here; value 3 is Shop.)

Output:
  { tiers: { "1": {itemChance,itemMin,itemMax,magicChance,magicMin,magicMax,
                    mundaneChance,mundaneMin,mundaneMax,lootQualityMod,profiles,pool:[names]}, .. },
    creatureTier: { "<bestiary kind>": tier, .. } }
"""
import re, os, json
from collections import defaultdict, Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SQL = next(os.path.join(ROOT, "acdata", f) for f in os.listdir(os.path.join(ROOT, "acdata")) if f.endswith(".sql"))
data = open(SQL, encoding="utf-8", errors="replace").read()
print(f"sql {len(data)/1e6:.0f} MB")

def stmts(table):
    for m in re.finditer(r"INSERT INTO `%s`(?: \([^)]*\))? VALUES (.*?);\n" % table, data, re.S):
        yield m.group(1)

def rows(body):
    out, depth, cur, q = [], 0, [], False
    i, n = 0, len(body)
    while i < n:
        ch = body[i]
        if q:
            if ch == "\\": i += 2; continue
            if ch == "'": q = False
        else:
            if ch == "'": q = True
            elif ch == "(":
                depth += 1
                if depth == 1: cur = []; i += 1; continue
            elif ch == ")":
                depth -= 1
                if depth == 0: out.append("".join(cur))
        if depth >= 1: cur.append(ch)
        i += 1
    return out

def fields(row):
    out, cur, q = [], [], False
    for ch in row:
        if q:
            if ch == "'": q = False
            else: cur.append(ch)
        else:
            if ch == "'": q = True
            elif ch == ",": out.append("".join(cur)); cur = []
            else: cur.append(ch)
    out.append("".join(cur))
    return out

# ---- names (weenie_properties_string type 1) ----
names = {}
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1:
            names[int(f[1])] = f[3]
print(f"names {len(names):,}")

# ---- treasure_death: treasure_Type -> profile ; also aggregate per tier ----
# cols: 0 id,1 treasure_Type,2 tier,3 loot_Quality_Mod,4 unknown_Chances,
#       5 item_Chance,6 item_Min,7 item_Max,8 item_TTSC,
#       9 magic_Chance,10 magic_Min,11 magic_Max,12 magic_TTSC,
#       13 mundane_Chance,14 mundane_Min,15 mundane_Max,16 mundane_TTSC
death_tier = {}                       # treasure_Type -> tier
tier_rows = defaultdict(list)         # tier -> list of profile dicts
for b in stmts("treasure_death"):
    for r in rows(b):
        f = fields(r)
        tt = int(f[1]); tier = int(f[2])
        death_tier[tt] = tier
        tier_rows[tier].append(dict(
            lqm=float(f[3]),
            iC=int(f[5]), iMin=int(f[6]), iMax=int(f[7]),
            mC=int(f[9]), mMin=int(f[10]), mMax=int(f[11]),
            uC=int(f[13]), uMin=int(f[14]), uMax=int(f[15]),
        ))
print(f"treasure_death profiles {len(death_tier):,} · tiers {sorted(tier_rows)}")

# ---- treasure_wielded: treasure_Type -> [(item_wcid, prob)] ----
# cols: 0 id,1 treasure_Type,2 weenie_Class_Id,...,8 probability
wielded = defaultdict(list)
for b in stmts("treasure_wielded"):
    for r in rows(b):
        f = fields(r)
        wielded[int(f[1])].append((int(f[2]), float(f[8])))
print(f"treasure_wielded types {len(wielded):,}")

# ---- weenie DID properties: 35 = DeathTreasureType, 32 = WieldedTreasureType ----
# (resolved by fraction-of-values-in-treasure-type-set; see module docstring.)
DEATH_DID, WIELD_DID = 35, 32
did = defaultdict(dict)   # object_Id -> {type: value}
for b in stmts("weenie_properties_d_i_d"):
    for r in rows(b):
        f = fields(r)
        t = int(f[2])
        if t in (DEATH_DID, WIELD_DID):
            did[int(f[1])][t] = int(f[3])

# ---- create_list destination 2 (Wield) : creature -> [item_wcid] ----
create_wield = defaultdict(list)
for b in stmts("weenie_properties_create_list"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 2:
            create_wield[int(f[1])].append(int(f[3]))
print(f"create_list Wield entries on {len(create_wield):,} weenies")

# ---- BESTIARY kind matcher (mirrors ace_world_more.py keyword table) ----
KIND_PAT = [
    ("olthoisoldier", r"olthoi soldier"), ("olthoiworker", r"olthoi worker"),
    ("olthoi", r"olthoi"), ("phyntoswasp", r"phyntos"), ("armoredillo", r"armoredillo"),
    ("banderling", r"banderling"), ("drudge", r"drudge"), ("mosswart", r"mosswart"),
    ("reedshark", r"reed ?shark"), ("tusker", r"tusker"), ("auroch", r"auroch"),
    ("shadow", r"shadow|umbris|panumbris"), ("gromnie", r"gromnie"), ("lugian", r"lugian"),
    ("tumerok", r"tumerok"), ("mattekar", r"mattekar"), ("skeleton", r"skeleton"),
    ("virindi", r"virindi"), ("rat", r"\brat\b"), ("shreth", r"shreth"), ("mite", r"mite"),
    ("sclavus", r"sclavus"), ("zefir", r"zefir"), ("monouga", r"monouga"), ("ursuin", r"ursuin"),
    ("moarsman", r"moarsman"), ("wisp", r"wisp"), ("golem", r"golem"), ("zombie", r"zombie"),
    ("mummy", r"mumiyah|mummy"), ("grievver", r"grievver"), ("rabbit", r"rabbit"),
    ("cow", r"\bcow\b"), ("carenzi", r"carenzi"), ("niffis", r"niffis"),
    ("remoran", r"remoran"), ("burun", r"burun"), ("mukkir", r"mukkir"),
]
KIND_RE = [(k, re.compile(p)) for k, p in KIND_PAT]
def kind_of(nm):
    n = (nm or "").lower()
    for k, rx in KIND_RE:
        if rx.search(n): return k
    return None

# ---- assemble per-tier item pools + creature->tier ----
def item_name(wcid):
    nm = names.get(wcid)
    return nm if nm else None

# skip obviously-non-item names (creatures, generators)
SKIP = re.compile(r"generator|template|\btest\b|\(|admin|\bpvp\b", re.I)

tier_pool = defaultdict(Counter)         # tier -> Counter(name)
kind_tiers = defaultdict(Counter)        # bestiary kind -> Counter(tier)
creatures_seen = 0
for obj, m in did.items():
    tt = m.get(DEATH_DID)                 # DeathTreasureType
    if tt is None or tt not in death_tier:
        continue
    tier = death_tier[tt]
    creatures_seen += 1
    cname = names.get(obj, "")
    k = kind_of(cname)
    if k:
        kind_tiers[k][tier] += 1
    # named wielded items this creature carries -> that tier's pool
    for iw in create_wield.get(obj, []):
        nm = item_name(iw)
        if nm and not SKIP.search(nm):
            tier_pool[tier][nm] += 1
    wt = m.get(WIELD_DID) if WIELD_DID else None
    if wt and wt in wielded:
        for iw, prob in wielded[wt]:
            nm = item_name(iw)
            if nm and not SKIP.search(nm) and prob > 0:
                tier_pool[tier][nm] += 1
print(f"creatures with death-treasure {creatures_seen:,}")

# also fold treasure_wielded item names straight into the tier a creature that references
# them sits at (handled above); additionally seed pools from any leftover wielded items
def med(xs):
    xs = sorted(xs); return xs[len(xs)//2]

tiers = {}
for tier in range(1, 9):
    profs = tier_rows.get(tier, [])
    if not profs:
        continue
    def avg(key): return round(sum(p[key] for p in profs)/len(profs))
    pool = [nm for nm, _ in tier_pool[tier].most_common(40)]
    tiers[str(tier)] = dict(
        itemChance=avg("iC"), itemMin=min(p["iMin"] for p in profs), itemMax=max(p["iMax"] for p in profs),
        magicChance=avg("mC"), magicMin=min(p["mMin"] for p in profs), magicMax=max(p["mMax"] for p in profs),
        mundaneChance=avg("uC"), mundaneMin=min(p["uMin"] for p in profs), mundaneMax=max(p["uMax"] for p in profs),
        lootQualityMod=round(max(p["lqm"] for p in profs), 2),
        profiles=len(profs),
        pool=pool,
    )

creatureTier = {k: c.most_common(1)[0][0] for k, c in kind_tiers.items()}

out = {"tiers": tiers, "creatureTier": creatureTier}
path = os.path.join(ROOT, "assets", "acloot.json")
json.dump(out, open(path, "w", encoding="utf-8"), ensure_ascii=False)
sz = os.path.getsize(path)
print(f"wrote {path} ({sz//1024} KB)")
for t in ["1", "4", "8"]:
    d = tiers.get(t, {})
    print(f"  tier {t}: item {d.get('itemChance')}%/{d.get('itemMin')}-{d.get('itemMax')} "
          f"magic {d.get('magicChance')}% mundane {d.get('mundaneChance')}% "
          f"profiles={d.get('profiles')} poolN={len(d.get('pool',[]))}")
    print("    pool sample:", d.get("pool", [])[:8])
print("  creatureTier:", dict(sorted(creatureTier.items(), key=lambda x: x[1])))
