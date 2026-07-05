#!/usr/bin/env python3
"""ClothingTable armor/clothing visuals -> assets/acclothing.json

THE authentic AC clothing model. Equipping a garment ran its ClothingBase (weenie DID
prop 7, a portal.dat DID 0x10000000-0x10FFFFFF -> a ClothingTable) against the WEARER'S
body Setup. The ClothingTable holds:
  * ClothingBaseEffects  — keyed by wearer Setup DID (the SAME garment differs per body).
      each = list of CloObjectEffect { part index, replacement GfxObj (model swap),
             list of CloTextureEffect { oldTexture -> newTexture } }.
      -> model swap changes the SILHOUETTE (torso part BECOMES a breastplate mesh);
         texture swap changes the LOOK on the unchanged part (a shirt retextures the
         bare torso). Applied to the character's OWN body parts, never overlaid.
  * ClothingSubPalEffects — the DYE data: per palette-template, a list of CloSubPalette
      { list of CloSubPaletteRange {offset,numColors}, PaletteSet DID }.

Our engine body is the SAME setup the table keys on: human_male = 0x02000001,
human_female = 0x0200004E (ac_model_export.py). So a CloObjectEffect for part 9 maps
straight onto body part 9 — no coordinate translation.

Binary layout (ACE DatLoader, VALIDATED byte-exact on all 1917 dat ClothingTables):
  Id u32; ClothingBaseEffects PackedHashTable(u16 count,u16 buckets) of
     key=setup u32 -> { u32 nObj × CloObjectEffect(part u32, model u32,
                        u32 nTex × (oldTex u32, newTex u32)) };
  ClothingSubPalEffects PackedHashTable(u16,u16) of
     key u32 -> { icon u32, u32 nSub × CloSubPalette( u32 nRange × (off u32, num u32),
                  palSet u32 ) }.   # ranges come BEFORE palSet — verified empirically

byName (lowercase clothing/armor name -> ClothingBase did-hex) is preserved from the
ACE world DB scan when its SQL is present, else carried over from the existing json
(the geometry below needs only portal.dat).

Output:
  { clothing: { "<clothingBase-hex>": {
       m:[{part,gfxobj,tex:[[oldHex,newHex],...]}],   # male 0x02000001 effects
       f:[{part,gfxobj,tex:[...]}],                    # female 0x0200004E effects
       dye:[{icon,subs:[{palset,ranges:[[off,num],...]}]}] } },
    byName: { "<name>": "<clothingBase-hex>" } }
"""
import os, re, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
Buf = ame.Buf

ACD = os.path.join(ROOT, "acdata")
OUTJSON = os.path.join(ROOT, "assets", "acclothing.json")
SETUP_M, SETUP_F = 0x02000001, 0x0200004E

# ---- ClothingTable parser (byte-exact on all 1917 dat entries) ----
def parse_clothingtable(d):
    r = Buf(d)
    r.u32()                                        # table id
    base = {}
    n = r.u16(); r.u16()                           # ClothingBaseEffects PackedHashTable
    for _ in range(n):
        setup = r.u32()
        effects = []
        for _ in range(r.u32()):                   # CloObjectEffects
            idx = r.u32(); model = r.u32()
            texs = []
            for _ in range(r.u32()):               # CloTextureEffects
                old = r.u32(); new = r.u32()
                texs.append((old, new))
            effects.append((idx, model, texs))
        base[setup] = effects
    subpal = []
    m = r.u16(); r.u16()                           # ClothingSubPalEffects PackedHashTable
    for _ in range(m):
        r.u32()                                    # key (palette template) — unused here
        icon = r.u32()
        subs = []
        for _ in range(r.u32()):                   # CloSubPalettes
            ranges = []
            for _ in range(r.u32()):               # CloSubPaletteRange (BEFORE palSet)
                off = r.u32(); num = r.u32()
                ranges.append((off, num))
            palset = r.u32()
            subs.append({"palset": "%08x" % palset, "ranges": [[o, n2] for o, n2 in ranges]})
        subpal.append({"icon": "%08x" % icon, "subs": subs})
    if r.o != len(r.d):
        raise ValueError("byte mismatch %d/%d" % (r.o, len(r.d)))
    return base, subpal

def effects_json(eff):
    return [{"part": i, "gfxobj": "%08x" % m,
             "tex": [["%08x" % o, "%08x" % nn] for o, nn in t]} for i, m, t in sorted(eff)]

# ---- byName: from ACE world SQL if available, else keep the existing json ----
def build_byname():
    sqls = []
    for base_dir in (ACD, os.path.join(ROOT, "ACE-World-16PY-master")):
        for dp, _dn, fn in os.walk(base_dir) if os.path.isdir(base_dir) else []:
            for f in fn:
                if f.endswith(".sql"):
                    sqls.append(os.path.join(dp, f))
    # need weenie (type), weenie_properties_string (name), weenie_properties_d_i_d (ClothingBase)
    text = "".join(open(s, encoding="utf-8", errors="replace").read()
                   for s in sqls if os.path.getsize(s) < 200_000_000)
    if "weenie_properties_d_i_d" not in text:
        return None
    def stmts(t):
        for m in re.finditer(r"INSERT INTO `%s`(?: \([^)]*\))? VALUES (.*?);\n" % t, text, re.S):
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
            if ch == "(": depth += 1;  (cur.clear() if depth == 1 else cur.append(ch)); i += 1; continue
            if ch == ")":
                depth -= 1
                if depth == 0: out.append("".join(cur)); i += 1; continue
                cur.append(ch); i += 1; continue
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
            if int(f[2]) == 7: clobase[int(f[1])] = int(f[3])
    byname = {}
    for wid, did in clobase.items():
        if wtype.get(wid) != 2 or not (0x10000000 <= did <= 0x10FFFFFF):
            continue
        nm = (names.get(wid) or cls_names.get(wid, "")).strip()
        if nm and nm != "NULL":
            byname.setdefault(nm.lower(), "%08x" % did)
    return byname or None

# ---- resolve EVERY ClothingTable in the dat (comprehensive; a few MB) ----
portal = ame.DatReader(os.path.join(ACD, "client_portal.dat"))
clothing, exact, m_only, f_only, both, dyeable = {}, 0, 0, 0, 0, 0
for did in portal.files:
    if not (0x10000000 <= did <= 0x10FFFFFF):
        continue
    try:
        base, subpal = parse_clothingtable(portal.read(did))
    except Exception:
        continue
    exact += 1
    em, ef = base.get(SETUP_M), base.get(SETUP_F)
    if not em and not ef:
        continue
    ent = {}
    if em: ent["m"] = effects_json(em)
    if ef: ent["f"] = effects_json(ef)
    if subpal: ent["dye"] = subpal; dyeable += 1
    clothing["%08x" % did] = ent
    both += 1 if (em and ef) else 0
    m_only += 1 if (em and not ef) else 0
    f_only += 1 if (ef and not em) else 0
print(f"parsed byte-exact {exact} ClothingTables · with human effects {len(clothing)} "
      f"(both m+f {both} · m-only {m_only} · f-only {f_only}) · dyeable {dyeable}")

# byName: refresh from SQL, else preserve existing
byname = build_byname()
if byname is None:
    try:
        byname = json.load(open(OUTJSON, encoding="utf-8")).get("byName", {})
        print(f"byName: SQL absent — preserved {len(byname)} existing names")
    except Exception:
        byname = {}
        print("byName: SQL absent and no existing json — empty")
else:
    print(f"byName: {len(byname)} names from ACE world SQL")

out = {"clothing": clothing, "byName": byname}
json.dump(out, open(OUTJSON, "w", encoding="utf-8"), separators=(",", ":"))
print(f"wrote {OUTJSON} ({os.path.getsize(OUTJSON)/1e6:.3f} MB)")

# sanity
j = json.load(open(OUTJSON, encoding="utf-8"))
named = [(n, d) for n, d in j["byName"].items() if d in j["clothing"]]
for n, d in named[:4]:
    e = j["clothing"][d]
    print(f"  {n} [{d}]: m-swaps {len(e.get('m',[]))} · f-swaps {len(e.get('f',[]))} · "
          f"dye {len(e.get('dye',[]))} · tex[0]={ (e.get('m') or e.get('f') or [{}])[0].get('tex') }")
