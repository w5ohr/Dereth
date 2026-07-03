#!/usr/bin/env python3
"""Real town STRUCTURE / BUILDING placement -> assets/actowns.json

Two authoritative sources, merged per town:

 1. client_cell_1.dat  (the CLIENT's own world geometry — the real buildings)
    Every outdoor landblock has a LandblockInfo file (id = <landblock><<16 | 0xFFFE)
    holding two lists in landblock-local coords (0..192 m):
      • Objects   : static scenery/props/walls (GfxObj 0x0100.. / Setup 0x0200.. DIDs)
      • Buildings : the actual multi-part building shells (houses, halls, shops) — a
                    GfxObj/Setup DID + a placement frame + interior portals.
    (Same Turbine DAT reader as tools/ac_dungeon_export.py; parsing verified byte-exact
     on 2000 random landblocks — the building-portal record is 4-byte aligned.)

 2. ACE-World database `landblock_instance`  (the SERVER's placed weenies — named landmarks)
    Named town fixtures the server spawns: Portals, Wells/Fountains, Town Statues/Menhirs,
    Life Stones, Bind Stones (same streaming SQL parser as tools/ace_world_more.py).
    (Houses ARE weenies but live in interior cells, not the surface, so they are not here.)

Landblock-local coords -> world lat/long via the SAME convention acworld.json portals use;
verified aligned with index.html's TOWN_DATA (Holtburg's landblock 0xA9B4 — 12 buildings —
sits exactly on TOWN_DATA lat 42.3 / lng 33.3). Each structure is grouped to its nearest
town and emitted as a LOCAL offset (index.html world units = COORD per degree) with a
y-axis rotation and its mesh/Setup DID.

index.html is only READ (for TOWN_DATA + COORD), never modified.
Output: { "meta": {...}, "towns": { "Holtburg": [ {name,kind,x,z,rot,did}, ... ] } }
"""
import struct, re, os, json, math
from collections import defaultdict, Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")

# ─────────────── shared coordinate convention (tools/ace_world_more.py) ───────────────
def latlng(cell, ox, oy):
    gx = ((cell >> 24) & 0xFF) * 192.0 + ox      # world metres east  (landblock X byte)
    gy = ((cell >> 16) & 0xFF) * 192.0 + oy      # world metres north (landblock Y byte)
    return (gy / 24.0 - 1019.5) / 10.0, (gx / 24.0 - 1019.5) / 10.0   # lat, lng degrees

def yaw(qw, qz):
    return round(2.0 * math.atan2(qz, qw), 4)    # y-axis heading (rad) from upright quat (w,0,0,z)

# ─────────────── town list, read straight from index.html (never modified) ───────────────
html = open(os.path.join(ROOT, "index.html"), encoding="utf-8", errors="replace").read()
tblk = re.search(r"const TOWN_DATA=\[(.*?)\n\];", html, re.S).group(1)
COORD = int(re.search(r"const COORD=(\d+)", html).group(1))          # world units per degree
TOWNS = [(m.group(1), float(m.group(2)), float(m.group(3)))          # name, lat, lng
         for m in re.finditer(r'\["([^"]+)",\s*(-?[\d.]+),\s*(-?[\d.]+)', tblk)]
print(f"towns {len(TOWNS)} · COORD {COORD}/deg")

def town_landblock(lat, lng):
    gx = (lng * 10 + 1019.5) * 24; gy = (lat * 10 + 1019.5) * 24
    return int(gx // 192), int(gy // 192)   # LBX, LBY

RADIUS = 2.0    # degrees: how far from a town centre a structure still counts as "in town"
CAP = 60

# ═══════════════════ SOURCE 1: client_cell_1.dat buildings + scenery ═══════════════════
class DatReader:                                       # (as in tools/ac_dungeon_export.py)
    def __init__(self, path):
        self.f = open(path, "rb"); self.f.seek(0x140); hdr = self.f.read(0x24)
        (self.file_type, self.block_size, self.file_size, self.data_set, self.data_subset,
         self.free_head, self.free_tail, self.free_count, self.btree) = struct.unpack("<9I", hdr)
        self.files = {}; self._walk(self.btree)
    def _read_chain(self, offset, want=None):
        out = bytearray(); bs = self.block_size
        while offset:
            self.f.seek(offset); block = self.f.read(bs)
            nxt = struct.unpack("<I", block[:4])[0] & 0x7FFFFFFF
            out += block[4:]; offset = nxt
            if want is not None and len(out) >= want: break
        return bytes(out[:want]) if want is not None else bytes(out)
    def _walk(self, offset):
        raw = self._read_chain(offset, 62*4 + 4 + 61*24)
        branches = struct.unpack("<62I", raw[:248]); count = struct.unpack("<I", raw[248:252])[0]; p = 252
        for _ in range(count):
            _, oid, off, size, _, _ = struct.unpack("<6I", raw[p:p+24]); p += 24
            self.files[oid] = (off, size)
        if branches[0] != 0:
            for i in range(count + 1):
                if branches[i]: self._walk(branches[i])
    def read(self, oid):
        off, size = self.files[oid]; return self._read_chain(off, size)

def landblock_structures(cell, lb):
    """(objects, buildings) for landblock lb, each (modelDID, x, y, z, qw, qz). Local coords."""
    fid = (lb << 16) | 0xFFFE
    if fid not in cell.files:
        return [], []
    d = cell.read(fid)
    try:
        _, _, numObj = struct.unpack_from("<3I", d, 0)
        p = 12
        objs = []
        for _ in range(numObj):
            mid, = struct.unpack_from("<I", d, p)
            ox, oy, oz, qw, _, _, qz = struct.unpack_from("<7f", d, p + 4); p += 32
            objs.append((mid, ox, oy, oz, qw, qz))
        numB, _pack = struct.unpack_from("<2H", d, p); p += 4
        blds = []
        for _ in range(numB):
            mid, = struct.unpack_from("<I", d, p)
            bx, by, bz, qw, _, _, qz = struct.unpack_from("<7f", d, p + 4)
            numLeaves, numPortals = struct.unpack_from("<2I", d, p + 32); p += 40
            for _ in range(numPortals):               # skip interior portals (4-byte aligned)
                _, _, _, ns = struct.unpack_from("<4H", d, p); p += 8 + 2 * ns
                if p % 4: p += 4 - (p % 4)
            blds.append((mid, bx, by, bz, qw, qz))
        return objs, blds
    except struct.error:
        return [], []       # never seen in practice (2000/2000 clean); fail safe if it ever does

cell = DatReader(os.path.join(ACD, "client_cell_1.dat"))
print(f"cell.dat files {len(cell.files):,}")

cell_recs = defaultdict(list)   # town -> list of (dist, rec)
n_bld = n_obj = 0
for tname, tlat, tlng in TOWNS:
    lbx, lby = town_landblock(tlat, tlng)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            lb = ((lbx + dx) & 0xFF) << 8 | ((lby + dy) & 0xFF)
            objs, blds = landblock_structures(cell, lb)
            for kind, recs in (("building", blds), ("scenery", objs)):
                for mid, x, y, z, qw, qz in recs:
                    lat, lng = latlng(lb << 16, x, y)
                    d = math.hypot(lat - tlat, lng - tlng)
                    if d > RADIUS: continue
                    rec = {"name": "Building" if kind == "building" else "Scenery",
                           "kind": kind,
                           "x": round((lng - tlng) * COORD, 1),
                           "z": round(-(lat - tlat) * COORD, 1),
                           "rot": yaw(qw, qz), "did": "0x%08X" % mid}
                    cell_recs[tname].append((d, rec))
                    if kind == "building": n_bld += 1
                    else: n_obj += 1
print(f"cell.dat: {n_bld} buildings · {n_obj} scenery objects within {RADIUS}deg of a town")

# ═══════════════════ SOURCE 2: ACE landblock_instance named landmarks ═══════════════════
SQL = next(os.path.join(ACD, f) for f in os.listdir(ACD) if f.endswith(".sql"))
data = open(SQL, encoding="utf-8", errors="replace").read()
print(f"sql {len(data)/1e6:.0f} MB")

def stmts(table):
    for m in re.finditer(r"INSERT INTO `%s`(?: \([^)]*\))? VALUES (.*?);\n" % table, data, re.S):
        yield m.group(1)
def rows(body):
    out, depth, cur, q = [], 0, [], False; i, n = 0, len(body)
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
    out.append("".join(cur)); return out

weenie, names, setup = {}, {}, {}
for b in stmts("weenie"):
    for r in rows(b):
        f = fields(r); weenie[int(f[0])] = (f[1], int(f[2]))          # class_Name, WeenieType
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1: names[int(f[1])] = f[3]                    # display Name
for b in stmts("weenie_properties_d_i_d"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1: setup[int(f[1])] = int(f[3])              # Setup DID
print(f"weenies {len(weenie):,} · named {len(names):,} · setups {len(setup):,}")

FIXTURE_RE = re.compile(r"\b(well|fountain|font|pool|vat)\b", re.I)
STATUE_RE  = re.compile(r"\b(statue|menhir|obelisk|monument)\b", re.I)
def classify(wcid):
    w = weenie.get(wcid)
    if not w: return None
    cls, t = w; nm = names.get(wcid, cls)
    if t == 7:  return "portal"
    if t == 25: return "lifestone"
    if t == 65: return "bindstone"
    if t == 26 and FIXTURE_RE.search(nm): return "fixture"
    if t in (1, 8, 13) and STATUE_RE.search(nm): return "statue"
    return None

wee_recs = defaultdict(list)
n_wee = 0
for b in stmts("landblock_instance"):
    for r in rows(b):
        f = fields(r)
        wcid, ocell = int(f[1]), int(f[2])
        kind = classify(wcid)
        if not kind or (ocell & 0xFFFF) >= 0x0100: continue           # surface placements only
        lat, lng = latlng(ocell, float(f[3]), float(f[4]))
        aw, az = float(f[6]), float(f[9])
        bt, bd = None, 1e9
        for name, tlat, tlng in TOWNS:
            dd = math.hypot(lat - tlat, lng - tlng)
            if dd < bd: bd, bt = dd, name
        if bd > RADIUS: continue
        tlat, tlng = next((t[1], t[2]) for t in TOWNS if t[0] == bt)
        rec = {"name": names.get(wcid, weenie[wcid][0]), "kind": kind,
               "x": round((lng - tlng) * COORD, 1), "z": round(-(lat - tlat) * COORD, 1),
               "rot": yaw(aw, az)}
        if wcid in setup: rec["did"] = "0x%08X" % setup[wcid]
        wee_recs[bt].append((bd, rec))
        n_wee += 1
print(f"weenie landmarks within {RADIUS}deg of a town: {n_wee}")

# ═══════════════════ merge, prioritise, dedupe, cap ═══════════════════
PRIO = {"building": 0, "portal": 1, "lifestone": 1, "bindstone": 1, "fixture": 1,
        "statue": 1, "scenery": 2}
towns_out = {}
for tname, tlat, tlng in TOWNS:
    merged = cell_recs.get(tname, []) + wee_recs.get(tname, [])
    merged.sort(key=lambda p: (PRIO[p[1]["kind"]], p[0]))    # buildings, then named landmarks, then scenery; nearest first
    seen, out = set(), []
    for _, rec in merged:
        key = (round(rec["x"]), round(rec["z"]))            # collapse coincident placements
        if key in seen: continue
        seen.add(key); out.append(rec)
        if len(out) >= CAP: break
    if out: towns_out[tname] = out

out = {
    "meta": {
        "sources": ["client_cell_1.dat LandblockInfo (real buildings + scenery meshes)",
                    os.path.basename(SQL) + " landblock_instance (named landmarks)"],
        "coord_units_per_degree": COORD,
        "note": "x,z are local offsets from the town centre in index.html world units "
                "(x east, z south). rot is a y-axis yaw in radians. 'did' is the client "
                "mesh/Setup DataID (GfxObj 0x0100.. or Setup 0x0200..). Buildings/scenery "
                "come from the client geometry (no in-game name); named landmarks come from "
                "the server weenies (Setup DID where present).",
        "kinds": ["building", "portal", "fixture", "statue", "lifestone", "bindstone", "scenery"],
    },
    "towns": towns_out,
}
path = os.path.join(ROOT, "assets", "actowns.json")
json.dump(out, open(path, "w", encoding="utf-8"), ensure_ascii=False, separators=(",", ":"))
print(f"towns populated: {len(towns_out)}/{len(TOWNS)} · wrote {path} ({os.path.getsize(path)//1024} KB)")

# ─────────────── report ───────────────
for cap in ["Holtburg", "Shoushi", "Yaraq", "Cragstone", "Zaikhal"]:
    lst = towns_out.get(cap, [])
    print(f"  {cap}: {len(lst)}  {dict(Counter(r['kind'] for r in lst))}")
print("\nHoltburg sample:")
for r in towns_out.get("Holtburg", [])[:14]:
    print(f"   {r['kind']:10} {r['name']:28} x={r['x']:8} z={r['z']:8} rot={r['rot']:8} did={r.get('did','-')}")
