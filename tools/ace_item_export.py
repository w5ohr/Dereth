#!/usr/bin/env python3
"""Export the complete retail item catalog from the ACE world DB (3-Core split SQL).

For every item weenie across the equipment categories, capture its exact specs:
  weapons  — Damage (int 44), DamageVariance (float 22), DamageType (int 45),
             WeaponTime/speed (int 49), WeaponSkill (int 48)
  armor    — ArmorLevel (int 28)
  casters  — spell book, ItemMaxMana (int 105), ManaRate (float 5),
             SpellCraft (int 106?), wield requirements
  everyone — Value (int 19), EncumbranceVal/burden (int 5), Icon DID (did 8),
             wield difficulty (int 160)

Output: assets/acitems.json — { lowercase name: {t, dmg, dvar, dt, spd, skill, al,
val, bur, mana, mrate, spells, icon} } (lowest-wcid entry wins per name). The game
merges these specs onto any item it creates with a matching name.
"""
import os, re, json
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core", "9 WeenieDefaults", "SQL")
CATS = ["MeleeWeapon", "MissileLauncher", "Missile", "Caster", "Clothing", "Gem",
        "Food", "Ammunition", "Healer", "Lockpick", "ManaStone", "SpellComponent", "Key"]

RE_INT   = re.compile(r"\(\d+,\s*(\d+),\s*(-?\d+)\)")
RE_FLOAT = re.compile(r"\(\d+,\s*(\d+),\s*(-?[\d.]+)\)")
RE_DID   = re.compile(r"\(\d+,\s*(\d+),\s*(0x[0-9A-Fa-f]+)\)")
RE_SPELL = re.compile(r"\((\d+),\s*(\d+),\s*2\)")   # spell_book rows: (wcid, spellId, 2.0 prob)

def parse_item(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    def section(name):
        m = re.search(r"INSERT INTO `weenie_properties_%s`.*?;" % name, src, re.S)
        return m.group(0) if m else ""
    ints, floats, dids = {}, {}, {}
    for t, v in RE_INT.findall(section("int")): ints[int(t)] = int(v)
    for t, v in RE_FLOAT.findall(section("float")): floats[int(t)] = float(v)
    for t, v in RE_DID.findall(section("d_i_d")): dids[int(t)] = int(v, 16)
    spells = [int(s) for _, s in re.findall(r"\((\d+),\s*(\d+),\s*[\d.]+\)", section("spell_book"))][:8]
    o = {}
    if ints.get(44): o["dmg"] = ints[44]
    if floats.get(22): o["dvar"] = round(floats[22], 2)
    if ints.get(45): o["dt"] = ints[45]
    if ints.get(49): o["spd"] = ints[49]
    if ints.get(48): o["skill"] = ints[48]
    if ints.get(28): o["al"] = ints[28]
    if ints.get(19): o["val"] = ints[19]
    if ints.get(5): o["bur"] = ints[5]
    if ints.get(105): o["mana"] = ints[105]
    if floats.get(5): o["mrate"] = round(floats[5], 5)
    if ints.get(160): o["wield"] = ints[160]
    if spells: o["spells"] = spells
    if dids.get(8): o["icon"] = "%08x" % dids[8]
    return o

def main():
    cat = {}
    counts = defaultdict(int)
    for c in CATS:
        cdir = os.path.join(BASE, c)
        if not os.path.isdir(cdir): continue
        entries = []
        for root, _, files in os.walk(cdir):
            for fn in files:
                m = re.match(r"(\d+) (.+)\.sql$", fn)
                if m: entries.append((int(m.group(1)), m.group(2), os.path.join(root, fn)))
        for wcid, name, path in sorted(entries):
            key = name.lower()
            if key in cat: continue                 # lowest wcid (classic item) wins
            o = parse_item(path)
            if not o: continue
            o["t"] = c
            cat[key] = o
            counts[c] += 1
    path = os.path.join(ROOT, "assets", "acitems.json")
    json.dump(cat, open(path, "w"), separators=(",", ":"))
    print(f"items: {len(cat):,} across {len(counts)} categories")
    for c in CATS:
        if counts[c]: print(f"  {c:16s} {counts[c]:,}")
    for probe in ("bandit shield", "sword of lost light", "atlan sword", "wand"):
        if probe in cat: print(f"  e.g. {probe}: {cat[probe]}")
    print(f"wrote {path} ({os.path.getsize(path)//1024} KB)")

if __name__ == "__main__":
    main()
