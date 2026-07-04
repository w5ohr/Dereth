#!/usr/bin/env python3
"""Export creature BREED variants — the real Ivory/Ash/Brass Gromnies of retail.

Retail creature colors are not per-weenie palettes: a breed weenie carries
  ClothingBase (DID 7, a ClothingTable 0x10) + PaletteTemplate (int 3) + Shade (float 12)
and the ClothingTable maps (wearer Setup, template) -> part-model swaps, texture swaps
and CloSubPalEffects (palette ranges filled from a PaletteSet chosen by Shade).

For each exported kind this tool discovers up to 3 named breeds sharing the kind's
setup, composes their palettes, and bakes <kind>_2.glb / _3.glb / _4.glb through
ac_creature_export.export_creature (variant hooks). It also bakes the REAL Mu-miyah
(human rig + burial wraps via its ClothingTable) as mummy.glb — retiring the tinted
zombie stand-in.

Output:
  assets/models/monsters/ac/<kind>_N.glb
  assets/models/monsters/ac/breeds.json   {kind:[{f,name,scale},...]} (first = base glb)
"""
import os, re, json, importlib.util
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ace", os.path.join(ROOT, "tools", "ac_creature_export.py"))
ace = importlib.util.module_from_spec(spec); spec.loader.exec_module(ace)
ame = ace.ame
Buf = ame.Buf
SQL = os.path.join(ROOT, "acdata", "ACE-World-Database-v0.9.294.sql")
OUT = os.path.join(ROOT, "assets", "models", "monsters", "ac")
MAX_EXTRA = 3
BAD_NAME = re.compile(r"statue|apparition|corrupt|controlled|chained|banished|marker|sparring|academy|snowman|conflagration|paradox|reliquary", re.I)

# ── SQL scan ──
data = open(SQL, encoding="utf-8", errors="replace").read()
def stmts(t):
    for m in re.finditer(r"INSERT INTO `%s`(?: \([^)]*\))? VALUES (.*?);\n" % t, data, re.S):
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

weenie = {}
for b in stmts("weenie"):
    for r in rows(b):
        f = fields(r); weenie[int(f[0])] = (f[1], int(f[2]))
crea = {w for w, (cn, wt) in weenie.items() if wt in (10, 15)}
names = {}
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1 and int(f[1]) in crea: names[int(f[1])] = f[3]
dids = defaultdict(dict)
for b in stmts("weenie_properties_d_i_d"):
    for r in rows(b):
        f = fields(r); wid = int(f[1])
        if wid in crea and int(f[2]) in (1, 2, 7): dids[wid][int(f[2])] = int(f[3])
palT, lvl = {}, {}
for b in stmts("weenie_properties_int"):
    for r in rows(b):
        f = fields(r); wid = int(f[1])
        if wid in crea:
            if int(f[2]) == 3: palT[wid] = int(f[3])
            elif int(f[2]) == 25: lvl[wid] = int(f[3])
shade, dscale = {}, {}
for b in stmts("weenie_properties_float"):
    for r in rows(b):
        f = fields(r); wid = int(f[1])
        if wid in crea:
            if int(f[2]) == 12: shade[wid] = float(f[3])
            elif int(f[2]) == 39: dscale[wid] = float(f[3])
print(f"creatures {len(crea)}; with clothing {sum(1 for w in crea if 7 in dids[w])}")

portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))

# ── ClothingTable WITH palette templates (extends ac_clothing_export's parser) ──
def parse_clothing_full(d):
    r = Buf(d); r.u32()
    base = {}
    n = r.u16(); r.u16()
    for _ in range(n):
        setup = r.u32(); effs = []
        for _ in range(r.u32()):
            idx = r.u32(); model = r.u32()
            tex = [(r.u32(), r.u32()) for _ in range(r.u32())]
            effs.append((idx, model, tex))
        base[setup] = effs
    pals = {}
    n2 = r.u16(); r.u16()
    for _ in range(n2):
        tid = r.u32(); r.u32()                       # template id, icon
        subs = []
        for _ in range(r.u32()):
            nr = r.u32()
            ranges = [(r.u32(), r.u32()) for _ in range(nr)]
            palset = r.u32()
            subs.append((ranges, palset))
        pals[tid] = subs
    if r.o != len(d):
        raise ValueError("clothing table consumed %d of %d" % (r.o, len(d)))
    return base, pals

_pal_cache = {}
def raw_palette(pid):
    if pid not in _pal_cache: _pal_cache[pid] = ame.parse_palette(portal.read(pid))
    return _pal_cache[pid]
def parse_palset(pid):
    r = Buf(portal.read(pid)); r.u32()
    return [r.u32() for _ in range(r.u32())]

def composed_palettes(subs, sh):
    """palette fn for decode_texture: base palette overlaid with each CloSubPalEffect"""
    def fn(pid):
        base = list(raw_palette(pid))
        for ranges, ps in subs:
            if (ps >> 24) == 0x0F:
                lst = parse_palset(ps)
                if not lst: continue
                ov = raw_palette(lst[min(len(lst)-1, int(max(0.0, min(0.999, sh)) * len(lst)))])
            else:
                ov = raw_palette(ps)
            if not ov: continue
            for off, num in ranges:
                for i in range(num):
                    if off + i < len(base):
                        base[off + i] = ov[(i * len(ov)) // max(1, num)]
        return fn._cache.setdefault((pid, id(subs)), base)
    fn._cache = {}
    return fn

def variant_from(wid, rep_setup):
    """(variant dict, palettes fn) for a breed weenie, or None if its clothing lacks the setup"""
    cb = dids[wid].get(7)
    if not cb or not (0x10000000 <= cb <= 0x10FFFFFF) or cb not in portal.files: return None
    try: base, pals = parse_clothing_full(portal.read(cb))
    except Exception: return None
    eff = base.get(rep_setup)
    subs = pals.get(palT.get(wid, 0), [])
    if eff is None and not subs: return None
    swaps, texmap = {}, {}
    for idx, model, tex in (eff or []):
        if model: swaps[idx] = model
        for old, new in tex: texmap[old] = new
    return dict(swaps=swaps, texmap=texmap), composed_palettes(subs, shade.get(wid, 0.5))

def bake(fname, setup_id, mt_id, variant, palfn):
    texbytes = {"_pal": palfn if palfn else raw_palette}
    return ace.export_creature(fname, setup_id, mt_id, portal, texbytes, variant=variant)

# ── per-kind breed discovery ──
kind_dids = ace.creature_dids()
by_setup = defaultdict(list)
for w in crea:
    s = dids[w].get(1)
    if s: by_setup[s].append(w)

breeds = {}
for kind in sorted(kind_dids):
    setup_id, mt_id = kind_dids[kind]
    rep_name = ace.KINDS.get(kind, kind)
    entries = [dict(f=kind + ".glb", name=rep_name, scale=1.0)]
    seen_looks = set()
    cands = []
    for w in by_setup.get(setup_id, []):
        nm = names.get(w, "")
        if not nm or BAD_NAME.search(nm) or nm == rep_name: continue
        if 7 not in dids[w] or w not in palT: continue
        look = (dids[w][7], palT[w], round(shade.get(w, 0.5), 1))
        if look in seen_looks: continue
        seen_looks.add(look)
        cands.append((lvl.get(w, 999), nm, w))
    cands.sort()
    n = 2
    for _lv, nm, w in cands:
        if n > MAX_EXTRA + 1: break
        v = variant_from(w, setup_id)
        if not v: continue
        fname = f"{kind}_{n}"
        try:
            path = bake(fname, setup_id, mt_id, v[0], v[1])
        except Exception as e:
            print(f"  {kind}: '{nm}' bake failed ({e})"); continue
        if not path: continue
        entries.append(dict(f=fname + ".glb", name=nm, scale=round(dscale.get(w, 1.0), 3)))
        print(f"  {kind}_{n}: {nm} (palT {palT[w]}, shade {shade.get(w,0.5)}) {os.path.getsize(path)//1024}KB")
        n += 1
    if len(entries) > 1:
        breeds[kind] = entries

# ── the REAL Mu-miyah: human rig + burial wraps (replaces the tinted zombie) ──
mws = sorted([w for w in crea if "mu-miyah" in (names.get(w, "") or "").lower() and 7 in dids[w]],
             key=lambda w: lvl.get(w, 999))
if mws:
    w = mws[0]
    v = variant_from(w, dids[w][1])
    if v:
        try:
            path = bake("mummy", dids[w][1], dids[w][2], v[0], v[1])
            if path:
                breeds["mummy"] = [dict(f="mummy.glb", name=names[w], scale=round(dscale.get(w, 1.0), 3))]
                print(f"  mummy: {names[w]} — the real Mu-miyah (human rig + wraps) {os.path.getsize(path)//1024}KB")
        except Exception as e:
            print(f"  mummy bake failed ({e})")

json.dump(breeds, open(os.path.join(OUT, "breeds.json"), "w"), separators=(",", ":"))
tot = sum(len(v) for v in breeds.values())
print(f"\nwrote breeds.json: {len(breeds)} kinds, {tot} looks total")
