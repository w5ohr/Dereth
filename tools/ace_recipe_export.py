#!/usr/bin/env python3
"""Crafting-recipe extraction -> assets/acrecipes.json

Streams the 155MB ACE-World SQL dump (same parser as tools/ace_world_more.py) and
joins the retail crafting tables:

  recipe    : id, skill, difficulty, salvage_Type, success_W_C_I_D (the result item).
              cols -> 0 id,1 unknown_1,2 skill,3 difficulty,4 salvage_Type,5 success_W_C_I_D ...
  cook_book : recipe_Id -> source_W_C_I_D (the TOOL) + target_W_C_I_D (the TARGET item).
              A recipe fans out over many tool/target combos; we keep one representative
              (fully-named preferred) per (tool,target,result,skill).

wcids resolve to names via weenie_properties_string type 1.

The DB uses the classic Asheron's Call client Skill enum (verified against result names):
  18 Item Tinkering · 28 Weapon Tinkering · 29 Magic Item Tinkering · 30 Armor Tinkering
  37 Fletching · 38 Alchemy · 39 Cooking · 40 Salvaging
We keep the salvage/tinkering/alchemy/cooking/fletching families, cap at ~1500 recipes,
preferring rows with a named tool + target + result.

Output: { recipes: [ {tool,target,result,skill,difficulty,family}, .. ] }
"""
import re, os, json
from collections import defaultdict

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

# ---- names ----
names = {}
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1:
            names[int(f[1])] = f[3]
print(f"names {len(names):,}")

# ---- crafting families (classic AC Skill enum) ----
FAMILY = {
    18: "tinkering", 28: "tinkering", 29: "tinkering", 30: "tinkering",
    37: "fletching", 38: "alchemy", 39: "cooking", 40: "salvaging",
}

# ---- recipes ----
recipe = {}   # id -> (skill, difficulty, salvageType, resultWCID)
for b in stmts("recipe"):
    for r in rows(b):
        f = fields(r)
        recipe[int(f[0])] = (int(f[2]), int(f[3]), int(f[4]), int(f[5]))
print(f"recipes {len(recipe):,}")

# ---- cook_book : recipe_Id -> [(tool_wcid, target_wcid)] ----
cook = defaultdict(list)
for b in stmts("cook_book"):
    for r in rows(b):
        f = fields(r)
        cook[int(f[1])].append((int(f[2]), int(f[3])))
print(f"cook_book recipes referenced {len(cook):,}")

# ---- join, filter to crafting families, dedupe, prefer fully-named ----
seen = set()
cand = []   # (rank, dict)  rank 0 = fully named
for rid, (skill, diff, sal, res) in recipe.items():
    fam = FAMILY.get(skill)
    if not fam:
        continue
    result = names.get(res)
    combos = cook.get(rid) or [(0, 0)]
    # prefer a cook_book combo where both tool and target are named
    combos = sorted(combos, key=lambda st: (names.get(st[0]) is None, names.get(st[1]) is None))
    for tool_w, tgt_w in combos:
        tool = names.get(tool_w)
        tgt = names.get(tgt_w)
        key = (tool, tgt, result, skill)
        if key in seen:
            continue
        seen.add(key)
        rank = 0 if (tool and tgt and result) else (1 if result else 2)
        cand.append((rank, fam, diff, {
            "tool": tool or (f"#{tool_w}" if tool_w else None),
            "target": tgt or (f"#{tgt_w}" if tgt_w else None),
            "result": result or (f"#{res}" if res else None),
            "skill": skill,
            "difficulty": diff,
            "family": fam,
        }))
        break  # one representative combo per recipe

# order: fully-named first, then round-robin across families so every family is
# represented within the cap (a plain family sort would truncate whole families).
cand.sort(key=lambda c: (c[0], c[2]))          # rank, then difficulty
CAP = 1500
by_fam = defaultdict(list)
for c in cand:
    by_fam[c[1]].append(c[3])
recipes, i = [], 0
while len(recipes) < CAP and any(i < len(v) for v in by_fam.values()):
    for fam in sorted(by_fam):
        if i < len(by_fam[fam]) and len(recipes) < CAP:
            recipes.append(by_fam[fam][i])
    i += 1

out = {"recipes": recipes}
path = os.path.join(ROOT, "assets", "acrecipes.json")
json.dump(out, open(path, "w", encoding="utf-8"), ensure_ascii=False)
sz = os.path.getsize(path)
print(f"wrote {path} ({sz//1024} KB) · {len(recipes)} recipes (of {len(cand)} in families)")
from collections import Counter
fam_counts = Counter(r["family"] for r in recipes)
print("  by family:", dict(fam_counts))
named = sum(1 for r in recipes if not str(r["tool"]).startswith("#") and not str(r["target"]).startswith("#") and not str(r["result"]).startswith("#"))
print(f"  fully-named tool+target+result: {named}/{len(recipes)}")
print("  samples:")
for r in recipes[:6]:
    print(f"    [{r['family']}] {r['tool']} + {r['target']} -> {r['result']}  (skill {r['skill']}, diff {r['difficulty']})")
