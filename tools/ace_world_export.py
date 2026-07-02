#!/usr/bin/env python3
"""Extract retail vendor + item data from the ACE-World community database dump.

Streams the SQL dump (acdata/ACE-World-Database-*.sql) and pulls:
  - weenie:                       class id -> (class_name, weenie type)
  - weenie_properties_string(1):  names
  - weenie_properties_int:        Value(19), ItemType(1), Level(25)
  - weenie_properties_create_list: vendor shop stock (destination 4 = Shop)
  - landblock_instance:           world placements -> lat/long, so each vendor lands in a town

Output: acdata/ace-vendors.json
  { "items":   { classId: {"n": name, "v": value, "it": itemType} },
    "vendors": [ {"n": name, "lat": .., "lng": .., "stock": [classId, ...]} ],
    "creatures": { classId: {"n": name, "lv": level} } }
"""
import re, os, json, sys
from collections import defaultdict

ACD = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "acdata")
SQL = next(os.path.join(ACD, f) for f in os.listdir(ACD) if f.endswith(".sql"))

VENDOR_TYPE = 12      # ACE WeenieType.Vendor
CREATURE_TYPE = 10    # WeenieType.Creature
SHOP_DEST = 4         # DestinationType.Shop

def rows(stmt_body):
    """Split the (..),(..),(..) tuples of one INSERT statement, respecting quotes."""
    out, depth, cur, q = [], 0, [], False
    i, n = 0, len(stmt_body)
    while i < n:
        ch = stmt_body[i]
        if q:
            if ch == "\\": i += 2; cur.append(stmt_body[i-1:i+1]); continue
            if ch == "'": q = False
            cur.append(ch)
        else:
            if ch == "'": q = True; cur.append(ch)
            elif ch == "(":
                depth += 1
                if depth == 1: cur = []
                else: cur.append(ch)
            elif ch == ")":
                depth -= 1
                if depth == 0: out.append("".join(cur))
                else: cur.append(ch)
            elif depth >= 1: cur.append(ch)
        i += 1
    return out

def fields(row):
    """Split one tuple body on commas, respecting quotes."""
    out, cur, q = [], [], False
    i, n = 0, len(row)
    while i < n:
        ch = row[i]
        if q:
            if ch == "\\": cur.append(stmt_unescape(row[i+1])); i += 2; continue
            if ch == "'": q = False
            else: cur.append(ch)
        else:
            if ch == "'": q = True
            elif ch == ",": out.append("".join(cur)); cur = []
            else: cur.append(ch)
        i += 1
    out.append("".join(cur))
    return out

def stmt_unescape(c):
    return {"n":"\n","t":"\t","'":"'","\\":"\\",'"':'"'}.get(c, c)

weenie = {}                 # id -> (class_name, type)
names = {}                  # id -> name
ints = defaultdict(dict)    # id -> {prop: value}
stock = defaultdict(list)   # vendor id -> [item class id]
placements = defaultdict(list)  # weenie id -> [(cellid, ox, oy)]

buf = ""
table_re = re.compile(r"INSERT INTO `([a-z_]+)`[^V]*VALUES\s*", re.I)
with open(SQL, encoding="utf-8", errors="replace") as f:
    data = f.read()
print(f"sql: {len(data)/1e6:.0f} MB")

for m in re.finditer(r"INSERT INTO `([a-z_]+)`(?: \([^)]*\))? VALUES (.*?);\n", data, re.S):
    tbl, body = m.group(1), m.group(2)
    if tbl == "weenie":
        for r in rows(body):
            fs = fields(r)
            weenie[int(fs[0])] = (fs[1], int(fs[2]))
    elif tbl == "weenie_properties_string":
        for r in rows(body):
            fs = fields(r)
            if int(fs[2]) == 1: names[int(fs[1])] = fs[3]
    elif tbl == "weenie_properties_int":
        for r in rows(body):
            fs = fields(r)
            p = int(fs[2])
            if p in (1, 19, 25): ints[int(fs[1])][p] = int(fs[3])
    elif tbl == "weenie_properties_create_list":
        for r in rows(body):
            fs = fields(r)
            # (id, object_id, destination_type, weenie_class_id, ...)
            if int(fs[2]) == SHOP_DEST: stock[int(fs[1])].append(int(fs[3]))
    elif tbl == "landblock_instance":
        for r in rows(body):
            fs = fields(r)
            # (guid, weenie_class_id, obj_cell_id, origin_x, origin_y, origin_z, ...)
            placements[int(fs[1])].append((int(fs[2]), float(fs[3]), float(fs[4])))

print(f"weenies {len(weenie):,} · names {len(names):,} · vendors-with-stock {len(stock):,} · placed classes {len(placements):,}")

def cell_to_latlng(cellid, ox, oy):
    bx, by = (cellid >> 24) & 0xFF, (cellid >> 16) & 0xFF
    gx, gy = bx*192.0 + ox, by*192.0 + oy      # AC east metres, north metres
    lng = (gx/24.0 - 1019.5) / 10.0
    lat = (gy/24.0 - 1019.5) / 10.0
    return round(lat, 2), round(lng, 2)

items, vendors, creatures = {}, [], {}
for wid, (cname, wtype) in weenie.items():
    if wtype == VENDOR_TYPE and wid in stock and wid in placements:
        cellid, ox, oy = placements[wid][0]
        lat, lng = cell_to_latlng(cellid, ox, oy)
        vendors.append({"n": names.get(wid, cname), "lat": lat, "lng": lng,
                        "stock": sorted(set(stock[wid]))})
        for it in stock[wid]:
            if it in weenie and it not in items:
                items[it] = {"n": names.get(it, weenie[it][0]),
                             "v": ints[it].get(19, 0), "it": ints[it].get(1, 0)}
    elif wtype == CREATURE_TYPE and 25 in ints.get(wid, {}):
        creatures[wid] = {"n": names.get(wid, cname), "lv": ints[wid][25]}

vendors.sort(key=lambda v: (v["lat"], v["lng"]))
out = {"items": items, "vendors": vendors, "creatures": creatures}
with open(os.path.join(ACD, "ace-vendors.json"), "w", encoding="utf-8") as f:
    json.dump(out, f, ensure_ascii=False)
print(f"vendors placed: {len(vendors):,} · catalog items: {len(items):,} · creatures w/ level: {len(creatures):,}")
print("sample vendors:")
for v in vendors[:6]:
    print(f"  {v['n']!r} @ {v['lat']}, {v['lng']} — {len(v['stock'])} wares")
