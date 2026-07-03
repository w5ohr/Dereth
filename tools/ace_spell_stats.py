#!/usr/bin/env python3
"""Merge the two halves of the authentic spell system into one stats pack.

  assets/acspells.json (client SpellTable, already extracted) — per spell id: name,
      description, school, level, MANA COST, components, casting words, icon.
  3-Core "2 SpellTableExtendedData" (ACE `spell` table) — the effect numbers per id:
      war projectiles' base_Intensity + variance (the damage roll), heals' boost +
      boost_Variance, buff/vuln stat_Mod_Val, drain percentages.

Output: assets/acspellstats.json keyed by lowercase spell name:
  { mana, words, comps, icon, dmg:[lo,hi], boost:[lo,hi], mod, drain }
The game overrides its SPELLBOOK entries by name at boot — costs, damage rolls,
buff magnitudes and heal amounts become exactly retail.
"""
import os, re, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXT = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core", "2 SpellTableExtendedData", "SQL")

def parse_ext(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"INSERT INTO `spell` \(([^)]+)\)\s*VALUES \((.+?)\);", src, re.S)
    if not m: return {}
    cols = [c.strip().strip("`") for c in m.group(1).split(",")]
    # strip comments first (they contain commas), then split respecting quotes
    body = re.sub(r"/\*.*?\*/", "", m.group(2))
    vals, cur, q = [], "", False
    for ch in body:
        if q:
            cur += ch
            if ch == "'": q = False
        elif ch == "'": q = True; cur += ch
        elif ch == ",": vals.append(cur.strip()); cur = ""
        else: cur += ch
    vals.append(cur.strip())
    out = {}
    for c, v in zip(cols, vals):
        v = re.sub(r"/\*.*?\*/", "", v).strip()
        try: out[c] = float(v) if "." in v else int(v)
        except ValueError: out[c] = v.strip("'")
    return out

def main():
    client = json.load(open(os.path.join(ROOT, "assets", "acspells.json")))["spells"]
    out = {}
    n_dmg = n_boost = n_mod = 0
    for fn in os.listdir(EXT):
        m = re.match(r"(\d+) (.+)\.sql$", fn)
        if not m: continue
        sid, name = str(int(m.group(1))), m.group(2)
        e = parse_ext(os.path.join(EXT, fn))
        c = client.get(sid, {})
        key = name.lower()
        if key in out: continue
        o = {}
        if c.get("mana"): o["mana"] = c["mana"]
        if c.get("words"): o["words"] = c["words"]
        if c.get("comps"): o["comps"] = c["comps"]
        if c.get("d"): o["desc"] = c["d"]
        bi = e.get("base_Intensity"); var = e.get("variance")
        if isinstance(bi, (int, float)) and bi:
            o["dmg"] = [int(bi), int(bi + (var or 0))]; n_dmg += 1
        bo = e.get("boost"); bv = e.get("boost_Variance")
        if isinstance(bo, (int, float)) and bo:
            o["boost"] = [int(bo), int(bo + (bv or 0))]; n_boost += 1
        sm = e.get("stat_Mod_Val")
        if isinstance(sm, (int, float)) and sm:
            o["mod"] = round(sm, 3) if isinstance(sm, float) else sm; n_mod += 1
        dr = e.get("drain_Percentage")
        if isinstance(dr, (int, float)) and dr: o["drain"] = dr
        if o: out[key] = o
    path = os.path.join(ROOT, "assets", "acspellstats.json")
    json.dump(out, open(path, "w"), separators=(",", ":"))
    print(f"spells: {len(out):,} (damage rolls {n_dmg:,} · boosts {n_boost:,} · stat mods {n_mod:,})")
    for probe in ("flame bolt i", "heal self i", "strength self i", "lightning bolt vii"):
        if probe in out: print(f"  {probe}: {out[probe]}")
    print(f"wrote {path} ({os.path.getsize(path)//1024} KB)")

if __name__ == "__main__":
    main()
