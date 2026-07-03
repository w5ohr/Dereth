#!/usr/bin/env python3
"""Second ACE-World extraction pass -> assets/acworld.json

  creatures : per-game-kind level/health/xp bands, matched by name keyword against
              7k+ retail creatures (weenie_properties_attribute_2nd init levels + XpOverride)
  spawns    : the REAL fauna map — the `encounter` table aggregated to a 51x51 world grid,
              each cell listing the dominant game-kind families that actually spawned there
  portals   : every placed overworld->overworld portal (name + position + destination)
  lifestones: every placed lifestone
(dungeon interiors intentionally untouched — another workstream owns those)
"""
import re, os, json
from collections import defaultdict, Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")
SQL = next(os.path.join(ACD, f) for f in os.listdir(ACD) if f.endswith(".sql"))
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
    for i, ch in enumerate(row):
        if q:
            if ch == "'": q = False
            else: cur.append(ch)
        else:
            if ch == "'": q = True
            elif ch == ",": out.append("".join(cur)); cur = []
            else: cur.append(ch)
    out.append("".join(cur))
    return out

# ---- base weenie facts ----
weenie, names = {}, {}
for b in stmts("weenie"):
    for r in rows(b):
        f = fields(r); weenie[int(f[0])] = (f[1], int(f[2]))
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1: names[int(f[1])] = f[3]
ints = defaultdict(dict)
for b in stmts("weenie_properties_int"):
    for r in rows(b):
        f = fields(r); p = int(f[2])
        if p in (25, 146): ints[int(f[1])][p] = int(f[3])       # Level, XpOverride
vit = defaultdict(dict)
for b in stmts("weenie_properties_attribute_2nd"):
    for r in rows(b):
        f = fields(r); t = int(f[2])
        if t in (1, 3, 5): vit[int(f[1])][t] = int(f[3])        # MaxHealth/Stam/Mana init
pos = {}
for b in stmts("weenie_properties_position"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 2:                                       # Destination
            pos[int(f[1])] = (int(f[3]), float(f[4]), float(f[5]))  # cell, x, y
place = defaultdict(list)
for b in stmts("landblock_instance"):
    for r in rows(b):
        f = fields(r)
        place[int(f[1])].append((int(f[2]), float(f[3]), float(f[4])))
print(f"weenies {len(weenie):,} · placements {sum(len(v) for v in place.values()):,} · dest-props {len(pos):,}")

def latlng(cell, ox, oy):
    gx = ((cell >> 24) & 0xFF)*192.0 + ox
    gy = ((cell >> 16) & 0xFF)*192.0 + oy
    return round((gy/24.0 - 1019.5)/10.0, 2), round((gx/24.0 - 1019.5)/10.0, 2)

# ---- creature kind matching (retail name -> our BESTIARY kind) ----
KIND_PAT = [
    ("olthoisoldier", r"olthoi soldier"), ("olthoiworker", r"olthoi worker"),
    ("olthoi", r"olthoi"), ("phyntoswasp", r"phyntos"), ("armoredillo", r"armoredillo"),
    ("banderling", r"banderling"), ("drudge", r"drudge"), ("mosswart", r"mosswart"),
    ("reedshark", r"reed ?shark"), ("tusker", r"tusker"), ("auroch", r"auroch"),
    ("shadow", r"shadow|umbris|panumbris"), ("gromnie", r"gromnie"), ("lugian", r"lugian"),
    ("tumerok", r"tumerok"), ("mattekar", r"mattekar"), ("skeleton", r"skeleton"),
    ("virindi", r"virindi"), ("rat", r"\brat\b"), ("shreth", r"shreth"), ("mite", r"mite"),
    ("sclavus", r"sclavus"), ("zefir", r"zefir"), ("monouga", r"monouga"), ("ursuin", r"ursuin"),
    ("moarsman", r"moarsman"), ("crystalwisp", r"crystalline"), ("wisp", r"wisp"),
    ("golem", r"golem"), ("zombie", r"zombie"),
    ("mummy", r"mumiyah|mummy"), ("grievver", r"grievver"), ("rabbit", r"rabbit"),
    ("cow", r"\bcow\b"), ("carenzi", r"carenzi"), ("niffis", r"niffis"),
    ("remoran", r"remoran"), ("burun", r"burun"), ("mukkir", r"mukkir"),
    # the creature deep-dive wave (specific tokens; generic families last)
    ("siraluun", r"siraluun"), ("eater", r"\beater\b"), ("chittick", r"chittick"),
    ("wight", r"wight"), ("lich", r"\blich\b"), ("hollow", r"hollow minion"),
    ("thrungus", r"thrungus"), ("ruschk", r"ruschk"), ("gurog", r"gurog"),
    ("sleech", r"sleech"), ("fiun", r"\bfiun\b"), ("marionette", r"marionette"),
    ("doll", r"\bdoll\b"), ("penguin", r"penguin"), ("margul", r"margul"),
    ("phantom", r"phantom"), ("elemental", r"elemental"), ("snowman", r"snowman"),
    ("chicken", r"chicken"), ("viamontian", r"viamontian"), ("falatacot", r"falatacot"),
    ("tormented", r"tormented"), ("hea", r"\bhea\b"), ("gearknight", r"gear knight"),
]
KIND_RE = [(k, re.compile(p)) for k, p in KIND_PAT]
def kind_of(nm):
    n = nm.lower()
    for k, rx in KIND_RE:
        if rx.search(n): return k
    return None

per_kind = defaultdict(lambda: {"lv": [], "hp": [], "xp": []})
for wid, (cls, wtype) in weenie.items():
    if wtype != 10: continue
    k = kind_of(names.get(wid, cls))
    if not k: continue
    d = per_kind[k]
    if 25 in ints[wid]: d["lv"].append(ints[wid][25])
    if 1 in vit[wid]: d["hp"].append(vit[wid][1])
    if 146 in ints[wid]: d["xp"].append(ints[wid][146])
def band(xs):
    if not xs: return None
    xs = sorted(xs); n = len(xs)
    return [xs[max(0, n//10)], xs[min(n-1, n*9//10)]]     # 10th..90th percentile
creatures = {}
for k, d in per_kind.items():
    creatures[k] = {"lv": band(d["lv"]), "hp": band(d["hp"]), "xp": band(d["xp"]), "n": len(d["lv"])}
print(f"kinds matched: {len(creatures)}")

# ---- the real spawn map: encounter table -> 51x51 grid of dominant kinds ----
grid = defaultdict(Counter)
enc_total = enc_matched = 0
for b in stmts("encounter"):
    for r in rows(b):
        f = fields(r)
        lb, wcid, cx, cy = int(f[1]), int(f[2]), int(f[3]), int(f[4])
        enc_total += 1
        w = weenie.get(wcid)
        if not w: continue
        nm = names.get(wcid, w[0])
        k = kind_of(nm)
        if not k: continue
        enc_matched += 1
        bx, by = (lb >> 8) & 0xFF, lb & 0xFF
        gx, gy = min(50, bx*51//255), min(50, by*51//255)
        grid[(gx, gy)][k] += 1
spawns = {f"{gx},{gy}": ",".join(k for k, _ in c.most_common(4)) for (gx, gy), c in grid.items()}
print(f"encounters: {enc_total:,} rows · {enc_matched:,} matched · {len(spawns)} grid cells")

# ---- portals (overworld destinations only) & lifestones ----
portals, seen = [], set()
for wid, (cls, wtype) in weenie.items():
    if wtype != 7 or wid not in pos or wid not in place: continue
    dcell, dx, dy = pos[wid]
    if (dcell & 0xFFFF) >= 0x0100: continue                # interior destination → dungeon team's lane
    dlat, dlng = latlng(dcell, dx, dy)
    nm = names.get(wid, cls)
    for cell, ox, oy in place[wid]:
        lat, lng = latlng(cell, ox, oy)
        key = (nm, round(lat), round(lng))
        if key in seen: continue
        seen.add(key)
        portals.append({"n": nm, "lat": lat, "lng": lng, "dlat": dlat, "dlng": dlng})
lifestones, seen = [], set()
for wid, (cls, wtype) in weenie.items():
    if "lifestone" not in cls.lower() or "sign" in cls.lower() or wid not in place: continue
    for cell, ox, oy in place[wid]:
        lat, lng = latlng(cell, ox, oy)
        key = (round(lat, 1), round(lng, 1))
        if key in seen: continue
        seen.add(key)
        lifestones.append({"lat": lat, "lng": lng})
print(f"overworld portals: {len(portals)} · lifestones: {len(lifestones)}")

out = {"creatures": creatures, "spawns": spawns, "portals": portals, "lifestones": lifestones}
path = os.path.join(ROOT, "assets", "acworld.json")
json.dump(out, open(path, "w", encoding="utf-8"), ensure_ascii=False)
print(f"wrote {path} ({os.path.getsize(path)//1024} KB)")
for k in ["drudge", "olthoi", "lugian", "tusker"]:
    print(" ", k, creatures.get(k))
print("  sample portals:", [p["n"] for p in portals[:5]])
