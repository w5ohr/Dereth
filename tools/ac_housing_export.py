#!/usr/bin/env python3
"""Extract Asheron's Call player housing IN FULL from the ACE world DB + client dats.

Replaces the old sampled achousing.json (name/type/cost/coords only) with the complete
retail housing system, per dwelling:

  - every dwelling AC ever sold: 2,601 cottages, ~570 villas, ~80 mansions (grouped from
    their multi-part house instances via the landblock building that contains them) and
    3,000 apartments (in the 50 residential-hall interior landblocks)
  - purchase terms from each dwelling's own SlumLord (weenie type 55) create list:
    pyreal price (destination 16), Writs of Refuge, per-settlement trophy items,
    minimum level (int 86), allegiance rank requirement (int 163 - mansions: rank 6),
    and the 30-day maintenance (destination 32: pyreals + writs)
  - the full hook/chest layout in dwelling-local coordinates: typed hooks (weenie type
    56: hook=wall / hook-floor / hook-ceiling / hook-yard / hook-roof), storage chests
    (type 57), the Covenant Crystal (the linked slumlord's spot), boot spot (type 58)
    and interior house portals (type 59)
  - REAL GRAPHICS: the exterior building model (LandblockInfo 0xFFFE building entry,
    GfxObj/Setup from portal.dat) and the interior EnvCell mesh (same pipeline as
    ac_env_export dungeon extraction), both in building-local three-space, plus
    walkable floor plates per interior cell for collision
  - apartment residential halls exported as dungeon-style interior meshes with the
    per-unit hook/chest/crystal positions in hall-local coordinates

Outputs:
  assets/achousing.json          - v2: settlements, dwellings, layouts, purchase terms
  assets/achouses/<did>.json     - building exterior + interior mesh + floor plates
  assets/achouses/hall_<lb>.json - apartment hall interior mesh (dungeon schema)
  assets/achouses/tex/*.png      - decoded real textures
  assets/achouses/index.json     - model/hall stats

Coordinate conventions match the rest of the toolchain: dwellings placed by lat/lng
degrees (x=lng, z=lat; game multiplies by COORD); meshes and layout offsets are metres
in three-space (x east, y up, z south = AC y negated), building-local.
"""
import struct, os, json, math, re, hashlib
from collections import defaultdict, Counter

import ac_dungeon_export as dung
import ac_model_export as mdl
import ac_env_export as env
from ac_model_export import Buf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD  = os.path.join(ROOT, "acdata")
SQL  = os.path.join(ACD, "ACE-World-Database-v0.9.294.sql")
OUT_JSON = os.path.join(ROOT, "assets", "achousing.json")
OUT_DIR  = os.path.join(ROOT, "assets", "achouses")

PYREAL, WRIT = 273, 11710
HOUSE_TYPE_INT = 155
HT = {1: "cottage", 2: "villa", 3: "mansion", 4: "apartment"}
HOOK_KIND = {"hook": "wall", "hook-floor": "floor", "hook-ceiling": "ceiling",
             "hook-yard": "yard", "hook-roof": "roof"}

# authentic settlement names (asheron.fandom.com) matched to extracted clusters by coords
SETTLEMENT_NAMES = [
    # (lat, lng, name)  -- cottage settlements
    (23.2, -42.3, "Mirthless Dale"), (-21.6, 18.5, "Ahr-Zona"), (13.0, 60.0, "Wisp Lake"),
    (33.0, 62.0, "Yukikaze"), (-40.0, 30.0, "Alfreth Ridge"), (10.0, -30.0, "Bright Blade"),
    # villa settlements
    (41.6, 15.4, "Bucolic Villas"), (54.0, 58.9, "Far Claw Villas"), (-30.0, 12.0, "Tarn Vinara"),
    (5.0, 5.0, "East Al-Jalima"), (-60.0, 60.0, "Beach Pass"),
]

# ───────────────────────── SQL streaming (same battle-tested splitter) ─────────────────────────
print("reading SQL ...")
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
    return round((gy / 24.0 - 1019.5) / 10.0, 3), round((gx / 24.0 - 1019.5) / 10.0, 3)

# ───────────────────────── weenies + properties ─────────────────────────
weenie = {}
for b in stmts("weenie"):
    for r in rows(b):
        f = fields(r); weenie[int(f[0])] = (f[1], int(f[2]))
W = lambda wid: weenie.get(wid, ("?", 0))
house_wids = {w for w, (cn, wt) in weenie.items() if wt == 53 and cn != "house"}
hook_wids  = {w for w, (cn, wt) in weenie.items() if wt == 56}
stor_wids  = {w for w, (cn, wt) in weenie.items() if wt == 57}
slum_wids  = {w for w, (cn, wt) in weenie.items() if wt == 55 and not cn.startswith("slumlordtest")}
boot_wids  = {w for w, (cn, wt) in weenie.items() if wt == 58}
hport_wids = {w for w, (cn, wt) in weenie.items() if wt == 59}

names = {}
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1: names[int(f[1])] = f[3]

house_type = {}     # house wid -> cottage/villa/mansion/apartment
house_main = {}     # house wid -> int161 (HooksVisible cap): present only on villa/mansion MAIN parts
slum_ints  = defaultdict(dict)
for b in stmts("weenie_properties_int"):
    for r in rows(b):
        f = fields(r); wid = int(f[1]); prop = int(f[2])
        if prop == HOUSE_TYPE_INT and wid in house_wids:
            house_type[wid] = HT.get(int(f[3]), "cottage")
        elif prop == 161 and wid in house_wids:
            house_main[wid] = int(f[3])
        elif wid in slum_wids and prop in (86, 163):   # MinLevel, AllegianceRank
            slum_ints[wid][prop] = int(f[3])

# slumlord purchase (dest 16) & rent (dest 32) create lists
slum_buy  = defaultdict(list)   # wid -> [(item_wcid, qty)]
slum_rent = defaultdict(list)
for b in stmts("weenie_properties_create_list"):
    for r in rows(b):
        f = fields(r); owner = int(f[1])
        if owner not in slum_wids: continue
        dt, wcid, qty = int(f[2]), int(f[3]), int(f[4])
        if dt == 16:  slum_buy[owner].append((wcid, max(1, qty)))
        elif dt == 32: slum_rent[owner].append((wcid, max(1, qty)))

terms_tab, terms_ix = [], {}
def term_idx(tp):
    key = json.dumps(tp)
    if key not in terms_ix:
        terms_ix[key] = len(terms_tab)
        terms_tab.append(dict(cost=tp[0], writs=tp[1], trophies=tp[2],
                              rentPy=tp[3], rentWrits=tp[4], lvl=tp[5], rank=tp[6]))
    return terms_ix[key]

def terms(slum_wid):
    """(cost_py, writs, trophies[names], rent_py, rent_writs, min_level, rank)"""
    cost = writs = rent_py = rent_wr = 0; troph = []
    for wcid, qty in slum_buy.get(slum_wid, []):
        if wcid == PYREAL: cost += qty
        elif wcid == WRIT: writs += qty
        else: troph.append(names.get(wcid, W(wcid)[0]))
    for wcid, qty in slum_rent.get(slum_wid, []):
        if wcid == PYREAL: rent_py += qty
        elif wcid == WRIT: rent_wr += qty
    ints = slum_ints.get(slum_wid, {})
    return cost, writs, sorted(set(troph)), rent_py, rent_wr, ints.get(86, 0), ints.get(163, 0)

# ───────────────────────── placements + links ─────────────────────────
inst = {}    # guid -> (wcid, cell, x, y, z, qw, qx, qy, qz)
for b in stmts("landblock_instance"):
    for r in rows(b):
        f = fields(r)
        inst[int(f[0])] = (int(f[1]), int(f[2]), float(f[3]), float(f[4]), float(f[5]),
                           float(f[6]), float(f[7]), float(f[8]), float(f[9]))
links = defaultdict(list)
for b in stmts("landblock_instance_link"):
    for r in rows(b):
        f = fields(r); links[int(f[1])].append(int(f[2]))

# house_portal: house_id -> [(cell, x, y, z, qw, qz)] — dest 1 = the dwelling's private
# interior landblock entry; dest 2 (when present) = the outdoor return spot
hp_cells = defaultdict(list)
for b in stmts("house_portal"):
    for r in rows(b):
        f = fields(r)
        hp_cells[int(f[1])].append((int(f[2]), float(f[3]), float(f[4]), float(f[5]),
                                    float(f[6]), float(f[9])))
print(f"weenies={len(weenie)} instances={len(inst)} houses(types)={len(house_type)} "
      f"house_portals={len(hp_cells)}")

# the house id IS embedded in the weenie class name (housecottage1..housemansion6250);
# one class = one dwelling; villa/mansion classes are placed twice: outdoors + interior lb
hid_of = {}
for w in house_wids:
    m = re.match(r"house(cottage|villa|mansion|apartment)(\d+)$", weenie[w][0])
    if m: hid_of[w] = (m.group(1), int(m.group(2)))

# free the big SQL string
del data

# ───────────────────────── client dats ─────────────────────────
cell_dat = dung.DatReader(os.path.join(ACD, "client_cell_1.dat"))
portal   = mdl.DatReader(os.path.join(ACD, "client_portal.dat"))

def lb_structures(lb):
    """buildings of landblock lb: [(modelDID, x, y, z, quat(w,x,y,z), seed_cells)]"""
    fid = (lb << 16) | 0xFFFE
    if fid not in cell_dat.files: return []
    d = cell_dat.read(fid)
    _, _, numObj = struct.unpack_from("<3I", d, 0)
    p = 12 + numObj * 32
    numB, _pack = struct.unpack_from("<2H", d, p); p += 4
    out = []
    for _ in range(numB):
        (did,) = struct.unpack_from("<I", d, p)
        bx, by, bz, qw, qx, qy, qz = struct.unpack_from("<7f", d, p + 4)
        numLeaves, numPortals = struct.unpack_from("<2I", d, p + 32); p += 40
        cells = set()
        for _ in range(numPortals):
            _, otherCell, _, ns = struct.unpack_from("<4H", d, p); p += 8
            for _ in range(ns):
                (stab,) = struct.unpack_from("<H", d, p); p += 2
                cells.add(stab)
            if p % 4: p += 4 - (p % 4)
            if 0x0100 <= otherCell < 0xFFFE: cells.add(otherCell)
        out.append((did, bx, by, bz, (qw, qx, qy, qz),
                    {c for c in cells if 0x0100 <= c < 0xFFFE}))
    return out

_lb_cells_cache = {}
def lb_cells(lb):
    """all interior EnvCells of a landblock: {cellid: {'x','y','z','ports'}}"""
    if lb not in _lb_cells_cache:
        cells = {}
        for cid in range(0x0100, 0xFFFE):
            fid = (lb << 16) | cid
            if fid not in cell_dat.files:
                if cid > 0x0100 and (cid - 0x0100) > len(cells) + 32: break
                continue
            try: cells[cid] = dung.parse_envcell(cell_dat.read(fid))
            except Exception: pass
        _lb_cells_cache[lb] = cells
    return _lb_cells_cache[lb]

def building_cellsets(lb):
    """[(building_tuple, full_interior_cellset)] via BFS through EnvCell portals"""
    blds = lb_structures(lb)
    cells = lb_cells(lb)
    out = []
    for b in blds:
        seed = set(b[5])
        seen = set(seed)
        q = list(seed)
        while q:
            c = q.pop()
            for o in cells.get(c, {}).get("ports", []):
                if o in cells and o not in seen:
                    seen.add(o); q.append(o)
        out.append((b, seen))
    return out

# ───────────────────────── group house instances into dwellings ─────────────────────────
def yaw_of(qw, qz):    # heading around AC z-axis
    return math.atan2(2.0 * qw * qz, 1.0 - 2.0 * qz * qz)

def local(px, py, pz, ax, ay, az, yaw):
    """world(landblock) offset -> anchor-local coords, yaw-unrotated, in three-space [x,y,z]."""
    dx, dy, dz = px - ax, py - ay, pz - az
    c, s = math.cos(-yaw), math.sin(-yaw)
    lx = dx * c - dy * s
    ly = dx * s + dy * c
    return [round(lx, 2), round(dz, 2), round(-ly, 2)]   # AC z-up -> three y-up

dwellings = []     # dict per dwelling
apt_units = []     # apartments handled separately
skipped = Counter()

# bucket house instances per landblock
lb_house_inst = defaultdict(list)
for g, v in inst.items():
    if v[0] in house_wids:
        t = house_type.get(v[0])
        if not t: skipped["untyped"] += 1; continue
        lb = (v[1] >> 16) & 0xFFFF
        lb_house_inst[lb].append((g, v, t))

def collect_children(guids):
    """merge linked children of several house instances -> hooks/chests/etc (world lb-local)"""
    hooks, chests, crystals, boots, hports = [], [], [], [], []
    slum = None
    for g in guids:
        for k in links.get(g, []):
            kv = inst.get(k)
            if not kv: continue
            wcid = kv[0]
            rec = (kv[2], kv[3], kv[4], yaw_of(kv[5], kv[8]), kv[1])
            if wcid in hook_wids:
                hooks.append((HOOK_KIND.get(W(wcid)[0], "wall"),) + rec)
            elif wcid in stor_wids: chests.append(rec)
            elif wcid in slum_wids:
                crystals.append(rec); slum = slum or wcid
            elif wcid in boot_wids: boots.append(rec)
            elif wcid in hport_wids: hports.append(rec)
    return hooks, chests, crystals, boots, hports, slum

def glob_xy(lb, x, y):
    """landblock-local -> global metres (east, north)"""
    return ((lb >> 8) & 0xFF) * 192.0 + x, (lb & 0xFF) * 192.0 + y

print("grouping dwellings by house id ...")
_bsets_cache = {}
def bsets_of(lb):
    if lb not in _bsets_cache: _bsets_cache[lb] = building_cellsets(lb)
    return _bsets_cache[lb]

Q = lambda x: round(round(x * 20) / 20, 2)          # 5 cm quantisation
QY = lambda a: round(round(((a + math.pi) % (2 * math.pi) - math.pi) * 50) / 50, 2)

# dwelling = house id; the same weenie class is placed outdoors (buildings) and — for
# villas/mansions — once more inside its private interior landblock (house_portal dest)
by_hid = defaultdict(list)
for g, v in inst.items():
    if v[0] in hid_of: by_hid[hid_of[v[0]]].append((g, v))

interior_lbs = {}   # lb -> house id (for the interior-mesh dedupe pass)
model_reps = {}     # "%08X" model -> (lb, bi) representative building placement
for (t, hid), parts in sorted(by_hid.items()):
    if t == "apartment":
        g, v = parts[0]
        hooks, chests, crystals, boots, hports, slum = collect_children([g])
        apt_units.append(dict(g=g, v=v, lb=(v[1] >> 16) & 0xFFFF, hid=hid, hooks=hooks,
                              chests=chests, crystals=crystals, boots=boots, slum=slum))
        continue
    int_lb = None
    hp = hp_cells.get(hid)
    if hp: int_lb = (hp[0][0] >> 16) & 0xFFFF
    ext_parts = [(g, v) for g, v in parts if ((v[1] >> 16) & 0xFFFF) != int_lb]
    int_parts = [(g, v) for g, v in parts if ((v[1] >> 16) & 0xFFFF) == int_lb]
    if not ext_parts: skipped["no_exterior_" + t] += 1; continue

    # match each exterior part to its building (cell containment, then nearest — the
    # settlement can straddle a landblock seam, so search the 8 neighbours too)
    def neighbours(lb):
        bx, by = (lb >> 8) & 0xFF, lb & 0xFF
        for dx in (0, -1, 1):
            for dy in (0, -1, 1):
                nx, ny = bx + dx, by + dy
                if 0 <= nx < 256 and 0 <= ny < 256: yield (nx << 8) | ny
    part_bld = []
    for g, v in ext_parts:
        lb = (v[1] >> 16) & 0xFFFF
        hcell = v[1] & 0xFFFF
        hit = None
        for bi, (b, cs) in enumerate(bsets_of(lb)):
            if hcell in cs: hit = (lb, bi); break
        if hit is None:
            gx, gy = glob_xy(lb, v[2], v[3])
            bd = 1e18
            for nlb in neighbours(lb):
                for bi, (b, cs) in enumerate(bsets_of(nlb)):
                    bgx, bgy = glob_xy(nlb, b[1], b[2])
                    d = (bgx - gx) ** 2 + (bgy - gy) ** 2
                    if d < bd: bd, hit = d, (nlb, bi)
        part_bld.append((g, v, hit))
    if all(h is None for _, _, h in part_bld):
        skipped["nobuilding_" + t] += 1; continue
    # anchor building = the one whose part links the house portal (the entrance), else first
    anchor_i = 0
    for i, (g, v, h) in enumerate(part_bld):
        if h and any(inst.get(k) and inst[k][0] in hport_wids for k in links.get(g, [])):
            anchor_i = i; break
    ag, av, (alb, abi) = part_bld[anchor_i]
    b, acells = bsets_of(alb)[abi]
    adid, ax, ay, az, aquat, _ = b
    ayaw = yaw_of(aquat[0], aquat[3])
    agx, agy = glob_xy(alb, ax, ay)

    def locr(clb, x, y, z, ryaw):
        cgx, cgy = glob_xy(clb, x, y)
        p = local(cgx, cgy, z, agx, agy, az, ayaw)
        return [Q(p[0]), Q(p[1]), Q(p[2]), QY(ryaw - ayaw)]
    def child(rec):   # rec=(x,y,z,yaw,cell)
        return locr((rec[4] >> 16) & 0xFFFF, rec[0], rec[1], rec[2], rec[3])

    # exterior children (hooks/chests/crystal/boot/entrance portal), anchor-local
    hooks, chests, crystals, boots, hports, slum = collect_children(
        [g for g, v, h in part_bld if h is not None])
    cost, writs, troph, rent_py, rent_wr, lvl, rank = terms(slum) if slum else (0,0,[],0,0,0,0)

    # all buildings of the compound (anchor first) — satellite = other parts' buildings
    blds = [["%08X" % adid, 0, 0, 0, 0]]
    seen_b = {(alb, abi)}
    model_reps.setdefault("%08X" % adid, (alb, abi))
    for g, v, h in part_bld:
        if h is None or h in seen_b: continue
        seen_b.add(h)
        sb, scells = bsets_of(h[0])[h[1]]
        sdid, sx, sy, sz, squat, _ = sb
        sgx, sgy = glob_xy(h[0], sx, sy)
        sp = local(sgx, sgy, sz, agx, agy, az, ayaw)
        blds.append(["%08X" % sdid, Q(sp[0]), Q(sp[1]), Q(sp[2]),
                     QY(yaw_of(squat[0], squat[3]) - ayaw)])
        model_reps.setdefault("%08X" % sdid, h)

    # interior part: hooks/chests in interior-landblock-local three-space
    interior = None
    if int_parts and int_lb is not None:
        ih, ic, icr, ib = [], [], None, None
        ihooks, ichests, icrys, iboots, ihports, islum = collect_children(
            [g for g, v in int_parts])
        L = lambda r: [Q(r[0]), Q(r[2]), Q(-r[1]), QY(r[3])]     # lb-local AC -> three
        interior = dict(lb="%04X" % int_lb,
                        hooks=[[h[0]] + L(h[1:5]) for h in ihooks],
                        chests=[L(c[:4]) for c in ichests],
                        boot=(L(iboots[0][:4]) if iboots else None),
                        portals=[L(p[:4]) for p in ihports])
        hp0 = hp[0]
        interior["entry"] = [Q(hp0[1]), Q(hp0[3]), Q(-hp0[2]), QY(yaw_of(hp0[4], hp0[5]))]
        interior["cell"] = hp0[0] & 0xFFFF
        interior_lbs[int_lb] = hid
        if not slum and islum:
            cost, writs, troph, rent_py, rent_wr, lvl, rank = terms(islum)

    cellid = (alb << 16) | 0x0001
    lat, lng = latlng(cellid, ax, ay)
    dwellings.append(dict(
        hid=hid, type=t, lat=lat, lng=lng, yaw=round(ayaw, 3), blds=blds, lb="%04X" % alb,
        cost=cost, writs=writs, trophies=troph, rentPy=rent_py, rentWrits=rent_wr,
        lvl=lvl, rank=rank,
        hooks=[[h[0]] + child(h[1:]) for h in hooks],
        chests=[child(c) for c in chests],
        crystal=(child(crystals[0]) if crystals else None),
        boot=(child(boots[0]) if boots else None),
        portals=[child(p) for p in hports],
        interior=interior,
        _cells=sorted(acells), _pos=(ax, ay, az)))
print(f"dwellings={len(dwellings)}; apartments={len(apt_units)}; "
      f"interior lbs={len(interior_lbs)}; skipped={dict(skipped)}")
print("  by type:", dict(Counter(d['type'] for d in dwellings)))

# ───────────────────────── settlement naming ─────────────────────────
# cluster dwellings by proximity (union-find on <= 2.5 landblock spacing) and name clusters
def cluster(dw):
    parent = list(range(len(dw)))
    def find(i):
        while parent[i] != i: parent[i] = parent[parent[i]]; i = parent[i]
        return i
    pts = [(d["lng"], d["lat"]) for d in dw]
    order = sorted(range(len(dw)), key=lambda i: pts[i])
    for ii in range(len(order)):
        i = order[ii]
        for jj in range(ii + 1, min(ii + 40, len(order))):
            j = order[jj]
            if abs(pts[j][0] - pts[i][0]) > 0.8: break
            if abs(pts[j][1] - pts[i][1]) > 0.8: continue
            ri, rj = find(i), find(j)
            if ri != rj: parent[rj] = ri
    groups = defaultdict(list)
    for i in range(len(dw)): groups[find(i)].append(i)
    return list(groups.values())

# POI towns for names
poi_wcid = {}
data = open(SQL, encoding="utf-8", errors="replace").read()
for b in stmts("points_of_interest"):
    for r in rows(b):
        f = fields(r); poi_wcid[f[1]] = int(f[2])
dest = {}
for b in stmts("weenie_properties_position"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 2: dest[int(f[1])] = (int(f[3]), float(f[4]), float(f[5]))
del data
towns = []
for nm, wc in poi_wcid.items():
    if wc in dest:
        c, x, y = dest[wc]
        if (c & 0xFFFF) < 0x0100:
            la, ln = latlng(c, x, y)
            towns.append((la, ln, nm))

def nearest_town(lat, lng):
    best, bd = None, 1e18
    for tla, tln, nm in towns:
        d = (tla - lat) ** 2 + (tln - lng) ** 2
        if d < bd: bd, best = d, nm
    return best or "Dereth"

clusters = cluster(dwellings)
used_names = Counter()
settlements = []
used_settlement_names = set()
for idxs in sorted(clusters, key=lambda ix: (dwellings[ix[0]]["lat"], dwellings[ix[0]]["lng"])):
    cla = sum(dwellings[i]["lat"] for i in idxs) / len(idxs)
    cln = sum(dwellings[i]["lng"] for i in idxs) / len(idxs)
    name = None
    for sla, sln, nm in SETTLEMENT_NAMES:
        if nm not in used_settlement_names and abs(sla - cla) < 2.0 and abs(sln - cln) < 2.0:
            name = nm; used_settlement_names.add(nm); break
    if not name:
        tn = nearest_town(cla, cln)
        base = f"{tn} {'Court' if len(idxs) <= 6 else 'Vale'}"
        used_names[base] += 1
        name = base if used_names[base] == 1 else f"{base} {used_names[base]}"
    si = len(settlements)
    settlements.append(dict(name=name, lat=round(cla, 2), lng=round(cln, 2), n=len(idxs)))
    # stable unit numbering within the settlement (west->east, north->south)
    idxs.sort(key=lambda i: (dwellings[i]["lat"], dwellings[i]["lng"]))
    for u, i in enumerate(idxs):
        dwellings[i]["set"] = si
        dwellings[i]["unit"] = u + 1
print(f"settlements={len(clusters)}")

# ───────────────────────── per-settlement layout files ─────────────────────────
# hooks were HAND-PLACED per dwelling in retail AC (verified: same-model cottages differ),
# so layouts don't dedupe. Keep the main JSON small by writing layouts per settlement,
# fetched on demand when the player nears / owns a dwelling there.
os.makedirs(OUT_DIR, exist_ok=True)
by_set = defaultdict(list)
for d in dwellings: by_set[d["set"]].append(d)
lay_kb = 0
for si, ds in sorted(by_set.items()):
    lay = {}
    for d in ds:
        lay[str(d["unit"])] = dict(hooks=d["hooks"], chests=d["chests"], crystal=d["crystal"],
                                   boot=d["boot"], portals=d["portals"], interior=d["interior"])
        for k in ("hooks", "chests", "crystal", "boot", "portals", "interior"): del d[k]
    fn = os.path.join(OUT_DIR, "lay_%d.json" % si)
    json.dump(lay, open(fn, "w"), separators=(",", ":"))
    lay_kb += os.path.getsize(fn) // 1024
print(f"layout files={len(by_set)} ({lay_kb} KB total)")

# ───────────────────────── building meshes (exterior + interior) ─────────────────────────
os.makedirs(os.path.join(OUT_DIR, "tex"), exist_ok=True)

# material resolver writing textures into achouses/tex (env's writes into acdungeons)
def make_resolver(outdir):
    pal_cache, tex_written, surf_cache = {}, {}, {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = mdl.parse_palette(portal.read(pid))
        return pal_cache[pid]
    def material(surf_did):
        if surf_did in surf_cache: return surf_cache[surf_did]
        try: s = mdl.parse_surface(portal.read(surf_did))
        except Exception:
            m = dict(color=0x888888); surf_cache[surf_did] = m; return m
        if "color" in s: m = dict(color=s["color"] & 0xFFFFFF)
        else:
            try: tid = mdl.parse_surfacetexture(portal.read(s["tex"]))[-1]
            except Exception:
                m = dict(color=0x888888); surf_cache[surf_did] = m; return m
            if tid not in tex_written:
                try: dec = env.decode_texture_ex(portal.read(tid), palettes)
                except Exception: dec = None
                if dec is None: tex_written[tid] = None
                else:
                    from PIL import Image
                    w, h, px = dec
                    Image.frombytes("RGBA", (w, h), px).save(
                        os.path.join(outdir, "tex", "%08X.png" % tid))
                    tex_written[tid] = "%08X.png" % tid
            fn = tex_written[tid]
            m = dict(tex=fn) if fn else dict(color=0x777777)
            if s.get("clip"): m["clip"] = 1
        surf_cache[surf_did] = m
        return m
    return material, tex_written

material, tex_written = make_resolver(OUT_DIR)
envcache = {}

def emit_gfxobj(did, groups, off=(0,0,0), quat=None):
    """append a GfxObj's drawing polys (own surface list) into groups, AC-space offset"""
    gfx = mdl.parse_gfxobj(portal.read(did))
    surfs = gfx["surfs"]; verts = gfx["verts"]
    for poly in gfx["polys"].values():
        si = poly["surf"]
        mat = material(surfs[si]) if 0 <= si < len(surfs) else dict(color=0x888888)
        gk = env.group_key(mat)
        g = groups.get(gk)
        if g is None:
            g = groups[gk] = dict(mat=mat, verts=[], norms=[], uvs=[], idx=[], vmap={})
        corner = []
        for i, vid in enumerate(poly["v"]):
            uvi = poly["uv"][i] if poly["uv"] else 0
            key = (did, vid, uvi)
            if key not in g["vmap"]:
                v = verts[vid & 0xFFFF]
                p = env.qrot(quat, v["o"]) if quat else v["o"]
                p = (p[0] + off[0], p[1] + off[1], p[2] + off[2])
                n = env.qrot(quat, v["n"]) if quat else v["n"]
                g["vmap"][key] = len(g["verts"]) // 3
                g["verts"] += env.tx_pos(p)
                g["norms"] += env.tx_pos(n)
                uv = v["uv"][uvi] if uvi < len(v["uv"]) else (0, 0)
                g["uvs"] += [round(uv[0], 4), round(uv[1], 4)]
            corner.append(g["vmap"][key])
        for i in range(1, len(corner) - 1):
            g["idx"] += [corner[0], corner[i + 1], corner[i]]

def emit_model(did, groups):
    """exterior building model: GfxObj (0x01) or Setup (0x02) of GfxObjs"""
    if did >> 24 == 0x01:
        emit_gfxobj(did, groups)
    elif did >> 24 == 0x02:
        s = mdl.parse_setup(portal.read(did))
        for pi, part in enumerate(s["parts"]):
            fr = s["frames"][pi] if s.get("frames") and pi < len(s["frames"]) else None
            off, quat = (0, 0, 0), None
            if fr: off, quat = tuple(fr["p"]), tuple(fr["q"])
            emit_gfxobj(part, groups, off, quat)

def groups_out(groups):
    out = []
    for g in groups.values():
        if not g["idx"]: continue
        e = dict(g["mat"]); e.update(verts=g["verts"], normals=g["norms"], uvs=g["uvs"], idx=g["idx"])
        out.append(e)
    return out

def interior_mesh_and_plats(lb, cellset, bx, by, bz, byaw):
    """interior cells -> mesh groups in building-local three-space + walkable floor plates"""
    groups = {}
    plats = []
    cy, sy = math.cos(-byaw), math.sin(-byaw)
    def to_local_ac(p):
        dx, dy, dz = p[0] - bx, p[1] - by, p[2] - bz
        return (dx * cy - dy * sy, dx * sy + dy * cy, dz)
    for cid in sorted(cellset):
        fid = (lb << 16) | cid
        if fid not in cell_dat.files: continue
        try: ec = env.parse_envcell_full(cell_dat.read(fid))
        except Exception: continue
        envid = ec["envid"]
        if envid not in envcache:
            try: envcache[envid] = env.parse_environment(portal.read(envid))
            except Exception: envcache[envid] = None
        e = envcache[envid]
        cs = e.get(ec["cstruct"]) if e else None
        if cs is None: continue
        pos, quat, surfs = ec["pos"], ec["quat"], ec["surfaces"]
        # accumulate floor polys for collision plates while emitting
        floor_pts = []
        verts = cs["verts"]
        for poly in cs["polys"].values():
            si = poly["surf"]
            surf_did = surfs[si] if 0 <= si < len(surfs) else None
            mat = material(surf_did) if surf_did is not None else dict(color=0x888888)
            gk = env.group_key(mat)
            g = groups.get(gk)
            if g is None:
                g = groups[gk] = dict(mat=mat, verts=[], norms=[], uvs=[], idx=[], vmap={})
            corner = []
            wps = []
            for i, vid in enumerate(poly["v"]):
                uvi = poly["uv"][i] if poly["uv"] else 0
                key = (cid, vid, uvi)
                v = verts[vid & 0xFFFF]
                wp = env.qrot(quat, v["o"])
                wp = (wp[0] + pos[0], wp[1] + pos[1], wp[2] + pos[2])
                lp = to_local_ac(wp)
                wps.append(lp)
                if key not in g["vmap"]:
                    wn = env.qrot(quat, v["n"])
                    g["vmap"][key] = len(g["verts"]) // 3
                    g["verts"] += env.tx_pos(lp)
                    g["norms"] += env.tx_pos(env.qrot(quat, v["n"]))
                    uv = v["uv"][uvi] if uvi < len(v["uv"]) else (0, 0)
                    g["uvs"] += [round(uv[0], 4), round(uv[1], 4)]
                corner.append(g["vmap"][key])
            for i in range(1, len(corner) - 1):
                g["idx"] += [corner[0], corner[i + 1], corner[i]]
            # upward-facing poly (AC z-up) = walkable floor candidate
            n0 = env.qrot(quat, verts[poly["v"][0] & 0xFFFF]["n"])
            if n0[2] > 0.7:
                floor_pts += wps
        if floor_pts:
            xs = [p[0] for p in floor_pts]; ys = [p[1] for p in floor_pts]
            zs = [p[2] for p in floor_pts]
            # three-space plate: x0,z0,x1,z1,y  (three x=ac x, three z=-ac y)
            plats.append([round(min(xs), 2), round(-max(ys), 2),
                          round(max(xs), 2), round(-min(ys), 2),
                          round(sum(zs) / len(zs), 2)])
    return groups_out(groups), plats

print("exporting building models ...")
model_index = {}
model_use = Counter()
for d in dwellings:
    for b in d["blds"]: model_use[b[0]] += 1
for mid, (rlb, rbi) in sorted(model_reps.items()):
    did = int(mid, 16)
    groups = {}
    try: emit_model(did, groups)
    except Exception as e2: print(f"  model {mid}: exterior parse failed: {e2}")
    ext = groups_out(groups)
    # walk-in interior cells (cottage rooms, villa ground floor) from a representative placement
    b, cells = bsets_of(rlb)[rbi]
    _, bx, by, bz, quat, _ = b
    byaw = yaw_of(quat[0], quat[3])
    interior, plats = interior_mesh_and_plats(rlb, set(cells), bx, by, bz, byaw)
    payload = dict(model=mid, exterior=ext, interior=interior, plats=plats)
    fn = "%s.json" % mid
    json.dump(payload, open(os.path.join(OUT_DIR, fn), "w"), separators=(",", ":"))
    model_index[mid] = dict(file=fn, n=model_use[mid],
                            extGroups=len(ext), intGroups=len(interior), plats=len(plats),
                            kb=os.path.getsize(os.path.join(OUT_DIR, fn)) // 1024)
    print(f"  {mid} x{model_use[mid]:<5} ext={len(ext):>2}g int={len(interior):>2}g "
          f"plats={len(plats):>3} {model_index[mid]['kb']}KB")

# ───────────────────────── villa/mansion private interior landblocks ─────────────────────────
# each villa/mansion has its own copy of a standard interior; extract one mesh per
# distinct geometry and map every interior lb to its shared mesh file
print("exporting dwelling interiors ...")
int_mesh_of = {}    # "%04X" lb -> mesh file
_int_hash_file = {}
n_int = 0
for ilb in sorted(interior_lbs):
    cells = lb_cells(ilb)
    if not cells: continue
    ids = sorted(cells)
    fids = [(ilb << 16) | c for c in ids]
    groups, stats = env.build_dungeon(fids, cell_dat, portal, envcache, material)
    gout = groups_out(groups)
    cellpos = [[round(cells[c]["x"], 2), round(cells[c]["z"], 2), round(-cells[c]["y"], 2)]
               for c in ids]
    ghash = hashlib.md5(json.dumps([cellpos, stats["tris"],
                                    sorted(len(g["idx"]) for g in gout)]).encode()).hexdigest()[:10]
    if ghash not in _int_hash_file:
        fn = "int_%04X.json" % ilb
        json.dump(dict(landblock="%04X" % ilb, cells=stats["cells_ok"],
                       cellPos=cellpos, groups=gout),
                  open(os.path.join(OUT_DIR, fn), "w"), separators=(",", ":"))
        _int_hash_file[ghash] = fn
        n_int += 1
    int_mesh_of["%04X" % ilb] = _int_hash_file[ghash]
print(f"  {len(interior_lbs)} interior landblocks -> {n_int} unique meshes")

# ───────────────────────── apartment halls ─────────────────────────
print("exporting apartment halls ...")
apt_layouts, apt_lay_ix = [], {}
apt_by_lb = defaultdict(list)
for a in apt_units: apt_by_lb[a["lb"]].append(a)
hall_index = {}
hall_hash_file = {}
for lb, units in sorted(apt_by_lb.items()):
    cells = lb_cells(lb)
    if not cells: continue
    ids = sorted(cells)
    fids = [(lb << 16) | c for c in ids]
    groups, stats = env.build_dungeon(fids, cell_dat, portal, envcache, material)
    gout = groups_out(groups)
    ghash = hashlib.md5(json.dumps(
        [len(gout), stats["tris"], len(ids)]).encode()).hexdigest()[:10]
    # hall-local = landblock-local three-space (same convention as dungeon meshes)
    cellpos = [[round(cells[c]["x"], 2), round(cells[c]["z"], 2), round(-cells[c]["y"], 2)]
               for c in ids]
    def hl(r):   # hall-local three-space + yaw
        return [round(r[0], 2), round(r[2], 2), round(-r[1], 2), round(r[3], 3)]
    us = []
    for a in sorted(units, key=lambda a: a["v"][1] & 0xFFFF):
        v = a["v"]
        uyaw = yaw_of(v[5], v[8])
        upos = (v[2], v[3], v[4])
        def ul(rec):    # unit-local three-space + yaw (rec = x,y,z,yaw in landblock space)
            p = local(rec[0], rec[1], rec[2], upos[0], upos[1], upos[2], uyaw)
            return [Q(p[0]), Q(p[1]), Q(p[2]), QY(rec[3] - uyaw)]
        lay = dict(hooks=sorted([h[0]] + ul(h[1:5]) for h in a["hooks"]),
                   chests=sorted(ul(c[:4]) for c in a["chests"]),
                   crystal=(ul(a["crystals"][0][:4]) if a["crystals"] else None),
                   boot=(ul(a["boots"][0][:4]) if a["boots"] else None))
        lkey = json.dumps(lay, sort_keys=True)
        if lkey not in apt_lay_ix:
            apt_lay_ix[lkey] = len(apt_layouts); apt_layouts.append(lay)
        us.append([v[1] & 0xFFFF] + hl((v[2], v[3], v[4], uyaw)) +
                  [apt_lay_ix[lkey], term_idx(terms(a["slum"]) if a["slum"] else (0,0,[],0,0,0,0)),
                   a["hid"]])
    if ghash in hall_hash_file:
        fn = hall_hash_file[ghash]; shared = True
    else:
        fn = "hall_%04X.json" % lb
        payload = dict(landblock="%04X" % lb, cells=stats["cells_ok"],
                       cellPos=cellpos, groups=gout)
        json.dump(payload, open(os.path.join(OUT_DIR, fn), "w"), separators=(",", ":"))
        hall_hash_file[ghash] = fn; shared = False
    lat, lng = latlng((lb << 16) | 1, 96, 96)
    hall_index["%04X" % lb] = dict(file=fn, shared=shared, units=us, lat=lat, lng=lng,
                                   cells=stats["cells_ok"], tris=stats["tris"])
    print(f"  hall {lb:04X}: {len(units)} units, {stats['cells_ok']} cells, "
          f"{stats['tris']} tris {'(shared mesh ' + fn + ')' if shared else ''}")

# ───────────────────────── final JSON ─────────────────────────
TYPE_ORDER = ["apartment", "cottage", "villa", "mansion"]
models = sorted(model_index)
model_ix = {m: i for i, m in enumerate(models)}

houses_out = []
for d in dwellings:
    trm = term_idx((d["cost"], d["writs"], d["trophies"], d["rentPy"], d["rentWrits"],
                    d["lvl"], d["rank"]))
    blds = [[model_ix[b[0]]] + b[1:] for b in d["blds"]]
    # [hid, type, settlement, unit, lat, lng, yaw, terms, buildings]; layout = lay_<set>.json[unit]
    houses_out.append([d["hid"], TYPE_ORDER.index(d["type"]), d["set"], d["unit"],
                       d["lat"], d["lng"], d["yaw"], trm, blds])

out = dict(version=2,
           typeOrder=TYPE_ORDER,
           types=dict(
               apartment=dict(n="Apartment", hooks=3, usable=3, chests=1, lvl=20, writs=1,
                              cost=100000, rentPy=10000, rentWrits=0, rank=0),
               cottage=dict(n="Cottage", hooks=50, usable=25, chests=1, lvl=20, writs=1,
                            cost=300000, rentPy=30000, rentWrits=0, rank=0),
               villa=dict(n="Villa", hooks=70, usable=50, chests=2, lvl=35, writs=5,
                          cost=2000000, rentPy=50000, rentWrits=1, rank=0),
               mansion=dict(n="Mansion", hooks=100, usable=100, chests=5, lvl=50, writs=20,
                            cost=10000000, rentPy=500000, rentWrits=5, rank=6)),
           models=models,
           terms=terms_tab,
           settlements=settlements,
           houses=houses_out,
           interiorMeshes=int_mesh_of,
           aptLayouts=apt_layouts,
           halls={k: dict(file=h["file"], units=h["units"])
                  for k, h in hall_index.items()})
json.dump(out, open(OUT_JSON, "w"), separators=(",", ":"))
json.dump(dict(models=model_index, halls={k: dict(file=v["file"], units=len(v["units"]))
                                          for k, v in hall_index.items()}),
          open(os.path.join(OUT_DIR, "index.json"), "w"), indent=1)

tc = Counter(d["type"] for d in dwellings)
print(f"\nwrote {OUT_JSON} ({os.path.getsize(OUT_JSON)//1024} KB): "
      f"{len(dwellings)} dwellings {dict(tc)} + {sum(len(h['units']) for h in hall_index.values())} apartments")
print(f"  settlements={len(settlements)} aptLayouts={len(apt_layouts)} "
      f"terms={len(terms_tab)} models={len(models)}")
print(f"wrote {OUT_DIR}: {len(model_index)} building models, "
      f"{len(hall_hash_file)} hall meshes, {len([t for t in tex_written.values() if t])} textures")
