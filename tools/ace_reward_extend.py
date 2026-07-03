#!/usr/bin/env python3
"""Extend assets/acrewards.json killxp with the creature deep-dive kinds.

ace_reward_export.py built the original 37 entries from the (no longer present)
3-Core split DB. This tool adds the remaining kinds using the SAME methodology
against the big ACE world SQL dump:
  - representative retail creature per kind (exact display name; among duplicates
    the LOWEST level wins — late-era bosses reuse classic names)
  - lvl = int 25, xp = XpOverride int 146
  - hp  = MaxHealth current level (attribute_2nd type 1, last column)
  - dmg/dvar = the hardest-hitting body part (max d_Val + its d_Var)
  - al  = median base_Armor across body parts
Existing entries are left untouched (methodology parity preserved).
"""
import re, os, json, statistics
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SQL = os.path.join(ROOT, "acdata", "ACE-World-Database-v0.9.294.sql")
REW = os.path.join(ROOT, "assets", "acrewards.json")

NEW_KINDS = {
    "rabbit": "Rabbit", "cow": "Cow", "chicken": "Chicken",
    "wight": "Frozen Wight", "lich": "Lich", "hollow": "Hollow Minion",
    "thrungus": "Thrungus", "ruschk": "Vile Ruschk", "gurog": "Gurog Soldier",
    "sleech": "Parfal Sleech", "fiun": "Frenzied Fiun", "marionette": "Marionette",
    "doll": "Pristine Doll", "penguin": "Aggressive Penguin",
    "crystalwisp": "Crystalline Wisp", "margul": "Stalking Margul",
    "phantom": "Phantom", "elemental": "Water Elemental", "snowman": "Snowman",
    "viamontian": "Viamontian Knight", "falatacot": "Falatacot Consort",
    "tormented": "Tormented Attendant", "hea": "Hea Hunter",
    "gearknight": "Gold Gear Knight",
    "olthoiqueen": "Olthoi Queen", "lichlord": "Lich Lord",
    "virindidirector": "Virindi Director",
}

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
        f = fields(r); weenie[int(f[0])] = int(f[2])
crea = {w for w, wt in weenie.items() if wt in (10, 15)}
byname = defaultdict(list)
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1 and int(f[1]) in crea: byname[f[3]].append(int(f[1]))
ints = defaultdict(dict)
for b in stmts("weenie_properties_int"):
    for r in rows(b):
        f = fields(r); wid = int(f[1])
        if wid in crea and int(f[2]) in (25, 146): ints[wid][int(f[2])] = int(f[3])
hp_cur = {}
for b in stmts("weenie_properties_attribute_2nd"):
    for r in rows(b):
        f = fields(r); wid = int(f[1])
        if wid in crea and int(f[2]) == 1: hp_cur[wid] = int(f[-1])
bp = defaultdict(list)
for b in stmts("weenie_properties_body_part"):
    for r in rows(b):
        f = fields(r); wid = int(f[1])
        if wid in crea:
            try: bp[wid].append((int(f[4]), float(f[5]), int(f[6])))   # d_Val, d_Var, base_Armor
            except Exception: pass

rew = json.load(open(REW))
killxp = rew.setdefault("killxp", {})
added = 0
for kind, nm in sorted(NEW_KINDS.items()):
    if kind in killxp: continue
    wids = byname.get(nm) or []
    if not wids:
        print(f"  {kind:<16} '{nm}' NOT FOUND — skipped"); continue
    w = min(wids, key=lambda x: ints[x].get(25, 9999))   # lowest level = the classic breed
    parts = bp.get(w, [])
    best = max(parts, key=lambda p: p[0]) if parts else (0, 0, 0)
    arm = [p[2] for p in parts]
    e = dict(lvl=ints[w].get(25, 1), xp=ints[w].get(146, 0), hp=hp_cur.get(w, 0),
             dmg=best[0], dvar=round(best[1], 2),
             al=int(statistics.median(arm)) if arm else 0)
    killxp[kind] = e; added += 1
    print(f"  {kind:<16} {nm:<22} lvl={e['lvl']} xp={e['xp']} hp={e['hp']} dmg={e['dmg']} al={e['al']}")
json.dump(rew, open(REW, "w"), separators=(",", ":"))
print(f"\nacrewards.json: +{added} kinds -> {len(killxp)} total")
