#!/usr/bin/env python3
"""ClothingTable armor/clothing visuals -> assets/acclothing.json

For the base human male setup (0x02000001), which body-part GfxObjs does each armor /
clothing ClothingTable swap in? A ClothingTable (portal.dat DID 0x10000000-0x10FFFFFF)
holds ClothingBaseEffects keyed by wearer Setup DID; each effect is a list of
CloObjectEffects = {part index, replacement GfxObj DID, texture swaps}. Binary layout per
the ACEmulator DatLoader (ClothingTable.cs): outer table is a PackedHashTable (u16 count +
u16 buckets), CloObjectEffect / CloTextureEffect lists are u32-counted. Verified: all 1917
ClothingTables in the dat parse with exact byte consumption.

Armor/clothing weenies come from the ACE world DB dump: name = string prop 1,
ClothingBase = DID prop 7 (VERIFIED type 7 — not 4; e.g. wcid 2620 type7 = 0x10000453,
inside the ClothingTable range), WeenieType 2 = Clothing/Armor.

Output:
  { clothing: { "<clothingBase-did-hex>": {                          # top ~200 most-referenced
        parts:[{ setup:"02000001", swaps:[{part:<idx>, gfxobj:"<gfxobj-did-hex>"}] }] } },
    byName:   { "<lowercase armor name>": "<clothingBase-did-hex>" } }  # every named clothing piece
"""
import os, re, json, importlib.util
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
Buf = ame.Buf

ACD = os.path.join(ROOT, "acdata")
SQL = next(os.path.join(ACD, f) for f in os.listdir(ACD) if f.endswith(".sql"))
OUTJSON = os.path.join(ROOT, "assets", "acclothing.json")
HUMAN_MALE = 0x02000001
# Detail every ClothingBase referenced by a named clothing weenie (dedupe'd, ~772). The
# spec floor was the ~200 most common; the full set costs only a few hundred KB (<<2 MB)
# and lets every byName entry resolve to its part-swaps, so we take all of them.

# ---- streaming SQL parser (reused from ace_world_more.py) ----
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
            cur.append(ch)
            if ch == "\\" and i + 1 < n: cur.append(body[i+1]); i += 2; continue
            if ch == "'": q = False
            i += 1; continue
        if ch == "'":
            q = True
            if depth >= 1: cur.append(ch)
            i += 1; continue
        if ch == "(":
            depth += 1
            if depth == 1: cur = []; i += 1; continue
        if ch == ")":
            depth -= 1
            if depth == 0: out.append("".join(cur)); i += 1; continue
        if depth >= 1: cur.append(ch)
        i += 1
    return out
def fields(row):
    out, cur, q = [], [], False
    i, n = 0, len(row)
    while i < n:
        ch = row[i]
        if q:
            if ch == "\\":
                if i + 1 < n: cur.append(row[i+1]); i += 2; continue
                i += 1; continue
            if ch == "'": q = False
            else: cur.append(ch)
        else:
            if ch == "'": q = True
            elif ch == ",": out.append("".join(cur)); cur = []
            else: cur.append(ch)
        i += 1
    out.append("".join(cur))
    return out

# ---- ClothingTable parser (per ACE DatLoader; validated on all 1917 dat entries) ----
def parse_clothingtable(d):
    r = Buf(d)
    r.u32()                                        # table id
    base = {}
    n = r.u16(); r.u16()                           # PackedHashTable: count + buckets
    for _ in range(n):
        setup = r.u32()
        effects = []
        for _ in range(r.u32()):                   # CloObjectEffects (u32-counted)
            idx = r.u32(); model = r.u32()
            for _ in range(r.u32()):               # CloTextureEffects: old/new SurfaceTexture
                r.u32(); r.u32()
            effects.append((idx, model))
        base[setup] = effects
    # (ClothingSubPalEffects follow — palette/tint data, not needed for part-swaps)
    return base

# ---- ACE world DB scan: armor/clothing weenies ----
wtype, cls_names, names, clobase = {}, {}, {}, {}
for b in stmts("weenie"):
    for r in rows(b):
        f = fields(r); wid = int(f[0]); cls_names[wid] = f[1]; wtype[wid] = int(f[2])
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1: names[int(f[1])] = f[3]
for b in stmts("weenie_properties_d_i_d"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 7: clobase[int(f[1])] = int(f[3])   # 7 = ClothingBase (verified)
print(f"weenies {len(wtype):,} · with ClothingBase {len(clobase):,}")

# byName over every named WeenieType-2 (Clothing/Armor) piece that has a ClothingBase in range
byname, freq = {}, Counter()
for wid, did in clobase.items():
    if wtype.get(wid) != 2:                        # 2 = Clothing/Armor wearables
        continue
    if not (0x10000000 <= did <= 0x10FFFFFF):
        continue
    nm = (names.get(wid) or cls_names.get(wid, "")).strip()
    if not nm or nm == "NULL":
        continue
    did_hex = "%08x" % did
    byname.setdefault(nm.lower(), did_hex)
    freq[did] += 1
print(f"clothing/armor pieces named {len(byname):,} · distinct ClothingBases {len(freq):,}")

# ---- resolve part-swaps for the TOP_N most-referenced ClothingBases ----
portal = ame.DatReader(os.path.join(ACD, "client_portal.dat"))
clothing = {}
resolved = no_human = missing_dat = parse_fail = 0
for did, _cnt in freq.most_common():
    if did not in portal.files:
        missing_dat += 1; continue
    try:
        base = parse_clothingtable(portal.read(did))
    except Exception:
        parse_fail += 1; continue
    eff = base.get(HUMAN_MALE)
    if not eff:
        no_human += 1; continue
    swaps = [{"part": idx, "gfxobj": "%08x" % model} for idx, model in sorted(eff)]
    clothing["%08x" % did] = {"parts": [{"setup": "02000001", "swaps": swaps}]}
    resolved += 1
print(f"{len(freq)} distinct ClothingBases -> resolved {resolved} · no human-male effect "
      f"{no_human} · not in dat {missing_dat} · parse-fail {parse_fail}")

out = {"clothing": clothing, "byName": byname}
json.dump(out, open(OUTJSON, "w", encoding="utf-8"), separators=(",", ":"))
sz = os.path.getsize(OUTJSON)
print(f"wrote {OUTJSON} ({sz/1e6:.3f} MB)")

# sanity: reparse + sample
json.load(open(OUTJSON, encoding="utf-8"))
for did, _ in freq.most_common(3):
    key = "%08x" % did
    nm = next((n for n, d in byname.items() if d == key), "?")
    ent = clothing.get(key)
    sw = ent["parts"][0]["swaps"] if ent else []
    print(f"  {key} ({nm}): {len(sw)} part-swaps -> "
          + ", ".join(f"{s['part']}:{s['gfxobj']}" for s in sw[:5]))
