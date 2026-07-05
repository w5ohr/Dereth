#!/usr/bin/env python3
"""Trim acdata/ace-vendors.json to the game's towns -> assets/acvendors.json.

Matches every extracted retail vendor to the nearest game town (within 3.0 degrees),
dedupes repeated placements, and keeps only the item catalog those vendors sell.
Each vendor carries x/z: their TRUE retail position as a town-centre offset in world
units — the SAME 1:1 mapping actowns.json building placements use
(x = (lng-tlng)*COORD east, z = -(lat-tlat)*COORD south) — so every merchant stands
exactly where they stood in retail, in their real shop.
"""
import json, os, re
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = json.load(open(os.path.join(ROOT, "acdata", "ace-vendors.json"), encoding="utf-8"))

html = open(os.path.join(ROOT, "index.html"), encoding="utf-8").read()
m = re.search(r"const TOWN_DATA=\[(.*?)\];", html, re.S)
towns = re.findall(r'\["([^"]+)",(-?[\d.]+),(-?[\d.]+)', m.group(1))
towns = [(n, float(lat), float(lng)) for n, lat, lng in towns]
COORD = int(re.search(r"const COORD=(\d+)", html).group(1))   # world units per degree (same as actowns.json)
print(f"game towns: {len(towns)} · COORD {COORD}/deg")

by_town = defaultdict(dict)     # town -> {(name): vendor}
used_items = set()
for v in src["vendors"]:
    best, bd, bll = None, 3.0, None
    for n, lat, lng in towns:
        d = max(abs(v["lat"] - lat), abs(v["lng"] - lng))
        if d < bd: bd, best, bll = d, n, (lat, lng)
    if not best: continue
    key = v["n"]
    if key in by_town[best]: continue          # dedupe multi-placements
    stock = [s for s in v["stock"] if str(s) in src["items"]][:60]
    if len(stock) < 3: continue                # skip trivial stands
    # TRUE retail position as a 1:1 town-centre offset (identical mapping to actowns.json buildings)
    vx = round((v["lng"] - bll[1]) * COORD, 1)
    vz = round(-(v["lat"] - bll[0]) * COORD, 1)
    by_town[best][key] = {"n": v["n"], "x": vx, "z": vz, "stock": stock}
    used_items.update(stock)

catalog = {str(i): src["items"][str(i)] for i in used_items}
out = {"catalog": catalog,
       "towns": {t: sorted(vs.values(), key=lambda v: -len(v["stock"])) for t, vs in by_town.items()}}
path = os.path.join(ROOT, "assets", "acvendors.json")
json.dump(out, open(path, "w", encoding="utf-8"), ensure_ascii=False)
print(f"towns with vendors: {len(out['towns'])} · vendors: {sum(len(v) for v in by_town.values())} · catalog items: {len(catalog)}")
print(f"wrote {path} ({os.path.getsize(path)//1024} KB)")
for t in ["Holtburg", "Yaraq", "Shoushi"]:
    vs = out["towns"].get(t, [])
    print(f"  {t}: {', '.join(v['n'] for v in vs[:6])}{' …' if len(vs) > 6 else ''}")
