#!/usr/bin/env python3
"""Map the game's housing FURNITURE_ITEMS to their retail weenies -> Setup DIDs.

The housing hooks place furniture by the game's item names (Steiner's/Jasin's price
lists). This tool finds each name's weenie in the ACE world DB (exact name first,
then normalized/contains fallbacks), takes its Setup DID (did property 1), and writes
assets/acfurnishings.json { lowercase game name: setuphex }.

ac_item_models.py ingests this file and bakes the meshes into assets/acitemmodels/
alongside the weapon models, so buildItemModel() picks furniture up by name like any
other retail item.
"""
import re, os, json
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SQL = os.path.join(ROOT, "acdata", "ACE-World-Database-v0.9.294.sql")
OUT = os.path.join(ROOT, "assets", "acfurnishings.json")

# the game's FURNITURE_ITEMS names (index.html) -> DB search terms, best first
WANT = {
    "bench": ["bench"],
    "stool": ["stool"],
    "chair": ["chair"],
    "couch": ["couch"],
    "desk": ["desk"],
    "workbench": ["workbench", "work bench"],
    "bed": ["bed"],
    "bedroll": ["bedroll", "bed roll"],
    "cooking table": ["cooking table"],
    "set dinner table": ["set dinner table", "dinner table"],
    "screen": ["screen"],
    "aluvian rug": ["aluvian rug", "rug"],
    "aluvian hanging rug": ["hanging rug"],
    "tapestry": ["tapestry"],
    "candelabra": ["candelabra"],
    "chandelier": ["chandelier"],
    "torch": ["torch"],
    "aluvian lamp": ["aluvian lamp", "lamp"],
    "crystal vase": ["crystal vase", "vase"],
    "pedestal": ["pedestal"],
    "painting: into the unknown": ["into the unknown", "painting"],
    "painting: the studious mind": ["studious mind", "painting"],
    "book shelf": ["bookshelf", "book shelf"],
    "barrel": ["barrel"],
    "cask": ["cask"],
    "banner": ["banner"],
    "campfire": ["campfire", "camp fire"],
    "carved tusker statue": ["tusker statue"],
    "oxidized drudge statue": ["drudge statue", "oxidized statue"],
    "garden drudge": ["garden drudge"],
    "ornate fountain": ["ornate fountain", "fountain"],
    "coffin": ["coffin"],
    "alchemy table": ["alchemy table"],
    "doorbell": ["doorbell", "door bell"],
    "plaque": ["plaque"],
    "chalk board": ["chalkboard", "chalk board"],
    "cooking pot": ["cooking pot"],
    "wine rack": ["wine rack"],
    "easel": ["easel"],
}

data = open(SQL, encoding="utf-8", errors="replace").read()

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

# names (string prop 1) and setups (did prop 1)
names = {}
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1: names[int(f[1])] = f[3]
setups = {}
for b in stmts("weenie_properties_d_i_d"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1: setups[int(f[1])] = int(f[3])
# weenie types: prefer plain items (avoid creatures/vendors sharing a word)
wtype = {}
for b in stmts("weenie"):
    for r in rows(b):
        f = fields(r); wtype[int(f[0])] = int(f[2])
GOOD_TYPES = {1, 2, 5, 6, 19, 20, 38, 44, 66}   # generic/clothing/food/gem/light/container/hookable-ish

# candidate pool: named weenies with setups, indexed by lowercase name
pool = defaultdict(list)
for wid, nm in names.items():
    if wid in setups:
        pool[nm.lower()].append(wid)

def pick(term, exact):
    """best weenie for a term: exact name, else name containing the term (shortest name wins)"""
    cands = []
    if exact and term in pool:
        cands = pool[term][:]
    if not cands:
        for nm, wids in pool.items():
            if term in nm:
                cands += wids
    if not cands:
        return None
    cands.sort(key=lambda w: ((wtype.get(w, 0) not in GOOD_TYPES), len(names[w]), w))
    return cands[0]

out, missed = {}, []
for game_name, terms in WANT.items():
    hit = None
    for i, t in enumerate(terms):
        hit = pick(t, exact=True) or (pick(t, exact=False) if i == len(terms) - 1 or True else None)
        if hit: break
    if hit:
        out[game_name] = "%08X" % setups[hit]
        print(f"  {game_name:<28} -> {names[hit][:40]:<42} setup {out[game_name]} (wcid {hit}, type {wtype.get(hit)})")
    else:
        missed.append(game_name)
        print(f"  {game_name:<28} -> NOT FOUND")

json.dump(out, open(OUT, "w"), indent=1)
print(f"\nwrote {OUT}: {len(out)} furnishings mapped, {len(missed)} missing {missed}")
