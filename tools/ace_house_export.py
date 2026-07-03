#!/usr/bin/env python3
"""Extract real AC housing from the ACE-World SQL dump -> assets/achousing.json

This content DB has no `house` table (only house_portal); the authoritative housing
data lives in the weenie tables instead:
  - weenie type 53 = House (the physical dwelling: HouseType int 155 = 1 Cottage /
    2 Villa / 3 Mansion / 4 Apartment), placed by landblock_instance.
  - weenie type 55 = SlumLord (the covenant you pay). Its purchase price is the
    Pyreal (weenie_class_id 273) entry in weenie_properties_create_list with
    destination_Type 16 (the buy amount; destination_Type 32 is the upkeep).
Each placed house is matched to the SlumLord sharing its landblock for the exact
price; houses with no co-located slumlord fall back to the canonical per-type price.

Coordinates use the same lat/lng convention as assets/acworld.json (tools/
ace_world_more.py latlng): output x = lng, z = lat (degrees). lat/lng are kept too.

Output: { houses:[{name, type, cost, x, z, lat, lng}] }, capped ~2000 (evenly sampled).
"""
import re, os, json
from collections import defaultdict, Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SQL = os.path.join(ROOT, "acdata", "ACE-World-Database-v0.9.294.sql")
OUT = os.path.join(ROOT, "assets", "achousing.json")
CAP = 2000
PYREAL_WCID = 273
DEST_BUY = 16
HOUSETYPE_INT = 155
HOUSE_TYPE = {1: "cottage", 2: "villa", 3: "mansion", 4: "apartment"}

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

def latlng(cell, ox, oy):
    gx = ((cell >> 24) & 0xFF) * 192.0 + ox
    gy = ((cell >> 16) & 0xFF) * 192.0 + oy
    return round((gy / 24.0 - 1019.5) / 10.0, 2), round((gx / 24.0 - 1019.5) / 10.0, 2)

# ---- weenies ----
weenie = {}
for b in stmts("weenie"):
    for r in rows(b):
        f = fields(r); weenie[int(f[0])] = (f[1], int(f[2]))
house_wids = {w for w, (cn, wt) in weenie.items() if wt == 53}
slum_wids = {w for w, (cn, wt) in weenie.items() if wt == 55}

names = {}
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1: names[int(f[1])] = f[3]

htype = {}
for b in stmts("weenie_properties_int"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == HOUSETYPE_INT and int(f[1]) in house_wids:
            htype[int(f[1])] = int(f[3])

# ---- slumlord purchase prices (create_list pyreal, destination_Type 16) ----
slum_price = defaultdict(int)
for b in stmts("weenie_properties_create_list"):
    for r in rows(b):
        f = fields(r); owner = int(f[1])
        if owner in slum_wids and int(f[3]) == PYREAL_WCID and int(f[2]) == DEST_BUY:
            slum_price[owner] += int(f[4])

# canonical price per house type = most common slumlord price grouped by slumlord name
type_price_votes = defaultdict(Counter)
for w in slum_wids:
    if w in slum_price:
        type_price_votes[(names.get(w, "") or "").lower()][slum_price[w]] += 1
type_price = {}
for label in ("cottage", "villa", "mansion", "apartment"):
    if type_price_votes[label]:
        type_price[label] = type_price_votes[label].most_common(1)[0][0]

# ---- placements ----
house_place = defaultdict(list)   # house wid -> [(cell, ox, oy)]
slum_lb = {}                      # landblock -> purchase price (from co-located slumlord)
for b in stmts("landblock_instance"):
    for r in rows(b):
        f = fields(r); wid = int(f[1]); cell = int(f[2]); ox = float(f[3]); oy = float(f[4])
        if wid in house_wids:
            house_place[wid].append((cell, ox, oy))
        elif wid in slum_wids and wid in slum_price:
            slum_lb[(cell >> 16) & 0xFFFF] = slum_price[wid]

# ---- POI towns for nearest-name enrichment ----
poi_wcid = {}   # name -> portal weenie class id
for b in stmts("points_of_interest"):
    for r in rows(b):
        f = fields(r); poi_wcid[f[1]] = int(f[2])
dest = {}       # weenie -> (cell, x, y)  (destination position, type 2)
for b in stmts("weenie_properties_position"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 2:
            dest[int(f[1])] = (int(f[3]), float(f[4]), float(f[5]))
towns = []      # (lat, lng, name)
for nm, wc in poi_wcid.items():
    if wc in dest:
        c, x, y = dest[wc]
        if (c & 0xFFFF) < 0x0100:       # outdoor destination only
            lat, lng = latlng(c, x, y)
            towns.append((lat, lng, nm))

def nearest_town(lat, lng):
    best, bd = None, 1e18
    for tlat, tlng, nm in towns:
        d = (tlat - lat) ** 2 + (tlng - lng) ** 2
        if d < bd: bd, best = d, nm
    return best

# ---- build house list ----
houses = []
seen = set()
matched = fallback = 0
for wid in sorted(house_place):
    ht = htype.get(wid)
    label = HOUSE_TYPE.get(ht, "house")
    for cell, ox, oy in house_place[wid]:
        lat, lng = latlng(cell, ox, oy)
        key = (round(lat, 2), round(lng, 2))
        if key in seen:
            continue
        seen.add(key)
        lb = (cell >> 16) & 0xFFFF
        if lb in slum_lb:
            cost = slum_lb[lb]; matched += 1
        else:
            cost = type_price.get(label, 0); fallback += 1
        town = nearest_town(lat, lng)
        name = (label.capitalize() + (" near " + town if town else "")).strip()
        houses.append({"name": name, "type": label, "cost": cost,
                       "x": lng, "z": lat, "lat": lat, "lng": lng})

# ---- even-sample down to CAP, preserving type spread ----
total = len(houses)
if total > CAP:
    step = total / CAP
    houses = [houses[int(i * step)] for i in range(CAP)]

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as fh:
    json.dump({"houses": houses}, fh, ensure_ascii=False, separators=(",", ":"))

tc = Counter(h["type"] for h in houses)
print(f"houses total={total} (price matched={matched}, fallback={fallback}) -> output {len(houses)}")
print("  type counts (output):", dict(tc))
print("  type->price:", type_price)
print(f"  wrote {OUT} ({os.path.getsize(OUT)//1024} KB)")
if houses:
    print("  sample:", json.dumps(houses[0], ensure_ascii=False))
