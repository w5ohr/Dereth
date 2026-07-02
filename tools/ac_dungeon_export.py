#!/usr/bin/env python3
"""Generate room-for-room dungeon layouts from the real AC client data + ACE world database.

Geometry (acdata/client_cell_1.dat): every interior landblock stores its EnvCells —
10x10-unit structural cells with a local position and explicit portals (doorways) to
neighbouring cells: the dungeon's true room graph. Cells are clustered into rooms on an
adaptive grid, z-levels stacked onto unique slots, and every adjacency routed with
straight segments (elbow halls at corners) so the engine's axis guard never drops a link.

Population (ACE-World-16PY-master/Database/3-Core, if present):
  - 6 LandBlockExtendedData/SQL/<LB>.sql  -> every placed instance (creatures, doors,
    levers, pressure plates, chests, portals) with its cell + a name comment, plus
    landblock_instance_link rows wiring levers/plates to the doors they open.
  - 9 WeenieDefaults/SQL/<Category>/...   -> classId -> category (Creature/Door/...),
    and Portal weenies carry their destination cell (-> interior landblock), which
    resolves dungeon names GoArrow's-era data didn't know.
Real creature counts become per-room m, a unique deep creature becomes the boss, real
levers gate their documented links, and interior portal instances flag portal rooms.

Output: assets/dungeon-layouts.json in the DUNGEON_SCRIPTS schema; index.html merges it
over DUNGEON_SCRIPTS at boot. Hand-scripted dungeons are skipped on purpose: their
layouts encode documented key chains from the wiki walkthroughs and stay authoritative.
"""
import struct, os, re, json, sys
from collections import defaultdict, deque, Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")
ACE = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core")
FY_SCALE = 2.6 / 6.0          # AC dungeon levels are 6 units apart; engine ramps span 2.6
MAX_ROOMS = 72                # coarsen the cluster grid until a dungeon fits

# ─────────────────────────── Turbine DAT reader ───────────────────────────
class DatReader:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.f.seek(0x140)
        hdr = self.f.read(0x24)
        (self.file_type, self.block_size, self.file_size, self.data_set,
         self.data_subset, self.free_head, self.free_tail, self.free_count,
         self.btree) = struct.unpack("<9I", hdr)
        self.files = {}
        self._walk(self.btree)

    def _read_chain(self, offset, want=None):
        out = bytearray()
        bs = self.block_size
        while offset:
            self.f.seek(offset)
            block = self.f.read(bs)
            nxt = struct.unpack("<I", block[:4])[0] & 0x7FFFFFFF
            out += block[4:]
            offset = nxt
            if want is not None and len(out) >= want:
                break
        return bytes(out[:want]) if want is not None else bytes(out)

    def _walk(self, offset):
        raw = self._read_chain(offset, 62*4 + 4 + 61*24)
        branches = struct.unpack("<62I", raw[:248])
        count = struct.unpack("<I", raw[248:252])[0]
        p = 252
        for _ in range(count):
            bitflags, oid, off, size, date, it = struct.unpack("<6I", raw[p:p+24]); p += 24
            self.files[oid] = (off, size)
        if branches[0] != 0:
            for i in range(count + 1):
                if branches[i]:
                    self._walk(branches[i])

    def read(self, oid):
        off, size = self.files[oid]
        return self._read_chain(off, size)

def parse_envcell(data):
    """EnvCell (per ACEmulator's DatLoader): id, flags, portals to other cells, local origin."""
    p = 0
    cid, flags, _ = struct.unpack_from("<3I", data, p); p += 12
    nsurf, nport = struct.unpack_from("<2B", data, p); p += 2
    nstab, = struct.unpack_from("<H", data, p); p += 2
    p += 2 * nsurf
    envid, cstruct = struct.unpack_from("<2H", data, p); p += 4
    ox, oy, oz = struct.unpack_from("<3f", data, p); p += 28   # origin + (skipped) quaternion
    ports = []
    for _ in range(nport):
        f, poly, other, opid = struct.unpack_from("<4H", data, p); p += 8
        if 0x0100 <= other < 0xFFFE:
            ports.append(other)
    return dict(id=cid & 0xFFFF, x=ox, y=oy, z=oz, ports=ports)

def dungeon_cells(cell, lb):
    ids = sorted(fid for fid in cell.files if (fid >> 16) == lb and 0x0100 <= (fid & 0xFFFF) < 0xFFFE)
    cells = {}
    for fid in ids:
        try:
            c = parse_envcell(cell.read(fid))
        except struct.error:
            continue
        cells[c["id"]] = c
    if 0x0100 not in cells:
        return None
    seen, q = {0x0100}, deque([0x0100])
    while q:
        for o in cells[q.popleft()]["ports"]:
            if o in cells and o not in seen:
                seen.add(o); q.append(o)
    return {i: c for i, c in cells.items() if i in seen}

# ─────────────────────────── ACE world database ───────────────────────────
def ace_category_index():
    """classId -> (category, name) from the WeenieDefaults folder layout."""
    idx = {}
    base = os.path.join(ACE, "9 WeenieDefaults", "SQL")
    if not os.path.isdir(base):
        return idx
    for cat in os.listdir(base):
        catdir = os.path.join(base, cat, cat)
        if not os.path.isdir(catdir):
            catdir = os.path.join(base, cat)
        for root, _, files in os.walk(os.path.join(base, cat)):
            for fn in files:
                m = re.match(r"(\d+) (.+)\.sql$", fn)
                if m:
                    idx[int(m.group(1))] = (cat, m.group(2))
    return idx

INST_RE = re.compile(
    r"VALUES \((0x[0-9A-Fa-f]+),\s*(\d+),\s*(0x[0-9A-Fa-f]+),\s*([-\d.eE]+),\s*([-\d.eE]+),\s*([-\d.eE]+)"
    r"[^;]*?;\s*/\* ([^*]*?) \*/")
LINK_RE = re.compile(r"\((0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*'[^']*'\)\s*(?:/\*[^*]*\*/)?")

def ace_landblock(lb):
    """instances + lever/door links for a landblock, or None."""
    path = os.path.join(ACE, "6 LandBlockExtendedData", "SQL", "%04X.sql" % lb)
    if not os.path.isfile(path):
        return None
    src = open(path, encoding="utf-8", errors="replace").read()
    inst = {}
    for m in INST_RE.finditer(src):
        guid, wcid, cellid = int(m.group(1), 16), int(m.group(2)), int(m.group(3), 16)
        inst[guid] = dict(wcid=wcid, cell=cellid & 0xFFFF, lbpart=cellid >> 16,
                          x=float(m.group(4)), y=float(m.group(5)), z=float(m.group(6)),
                          name=m.group(7).strip())
    links = []
    for stmt in re.findall(r"INSERT INTO `landblock_instance_link`.*?;", src, re.S):
        links += [(int(a, 16), int(b, 16)) for a, b in LINK_RE.findall(stmt)]
    return dict(inst=inst, links=links)

def ace_portal_destinations():
    """portal weenie name -> destination landblock (for closing the name->landblock gap)."""
    out = defaultdict(set)
    base = os.path.join(ACE, "9 WeenieDefaults", "SQL", "Portal")
    if not os.path.isdir(base):
        return out
    dest_re = re.compile(r"VALUES \(\d+, 2, (0x[0-9A-Fa-f]{8})")
    for root, _, files in os.walk(base):
        for fn in files:
            m = re.match(r"\d+ (.+)\.sql$", fn)
            if not m:
                continue
            src = open(os.path.join(root, fn), encoding="utf-8", errors="replace").read()
            d = dest_re.search(src)
            if d:
                lb = int(d.group(1), 16) >> 16
                if (int(d.group(1), 16) & 0xFFFF) >= 0x0100:   # interior destination only
                    out[m.group(1).lower()].add(lb)
    return out

def norm(s): return re.sub(r"[^a-z0-9]", "", s.lower())

def resolve_missing(names, portal_dest):
    """Match unmapped canon dungeon names against portal weenie names."""
    found = {}
    pnorm = {}
    for pname, lbs in portal_dest.items():
        pnorm.setdefault(norm(pname), set()).update(lbs)
    for n in names:
        nn = norm(n)
        lbs = set(pnorm.get(nn, set()))
        if not lbs:   # containment either way, but only if it resolves to ONE landblock
            for k, v in pnorm.items():
                if len(k) >= 8 and min(len(k), len(nn)) >= 8 and (nn in k or k in nn):
                    lbs |= v
        if len(lbs) == 1:
            found[n] = "%04X" % lbs.pop()
    return found

# ─────────────────────────── layout generation ───────────────────────────
def layout_from_cells(cells):
    for Q in (20, 26, 34, 44, 56):
        res = _cluster(cells, Q)
        if res[0] is not None and len(res[0]) <= MAX_ROOMS:
            return res
    return res

def _cluster(cells, Q):
    key = {}
    for i, c in cells.items():
        key[i] = (int(c["x"] // Q), int(c["y"] // Q), round(c["z"] / 6.0))
    groups = defaultdict(list)
    for i, k in key.items():
        groups[k].append(i)
    gids = {k: n for n, k in enumerate(sorted(groups))}
    entry_g = gids[key[0x0100]]
    adj = defaultdict(set)
    for i, c in cells.items():
        a = gids[key[i]]
        for o in c["ports"]:
            if o in key:
                b = gids[key[o]]
                if a != b:
                    adj[a].add(b); adj[b].add(a)
    info = {}
    for k, members in groups.items():
        g = gids[k]
        xs = [cells[i]["x"] for i in members]; ys = [cells[i]["y"] for i in members]
        zs = [cells[i]["z"] for i in members]
        info[g] = dict(x=sum(xs)/len(xs), y=sum(ys)/len(ys), z=sum(zs)/len(zs), n=len(members))
    dist = {entry_g: 0}
    order = [entry_g]
    q = deque([entry_g])
    while q:
        a = q.popleft()
        for b in sorted(adj[a]):
            if b not in dist:
                dist[b] = dist[a] + 1
                order.append(b); q.append(b)
    if len(order) < 2:
        return None, None, None, None
    boss_g = max(order, key=lambda g: (dist[g], info[g]["n"]))
    ex, ey = info[entry_g]["x"], info[entry_g]["y"]
    taken, slot = {}, {}
    def spiral(c0, d0):
        if (c0, d0) not in taken: return (c0, d0)
        for r in range(1, 40):
            for dc in range(-r, r+1):
                for dd in range(-r, r+1):
                    if max(abs(dc), abs(dd)) != r: continue
                    if (c0+dc, d0+dd) not in taken: return (c0+dc, d0+dd)
        return None
    ysign = -1 if sum(1 for g in order if info[g]["y"] < ey) > len(order)/2 else 1
    for g in order:
        s = spiral(round((info[g]["x"] - ex) / Q), round((info[g]["y"] - ey) / Q) * ysign)
        slot[g] = s; taken[s] = g
    ez = info[entry_g]["z"]
    fy = {g: round((info[g]["z"] - ez) * FY_SCALE, 2) for g in order}
    rooms, ids = [], {}
    for g in order:
        rid = "c%d" % g
        ids[g] = rid
        o = {}
        if g == entry_g: o["t"] = "entry"
        elif g == boss_g: o["t"] = "boss"; o["big"] = 1
        elif info[g]["n"] >= 6: o["big"] = 1; o["m"] = 2
        elif info[g]["n"] <= 1: o["small"] = 1; o["m"] = 1
        else: o["m"] = 1
        rooms.append([rid, slot[g][0], slot[g][1], fy[g], o])
    halls, out, emitted = [], [], set()
    hall_n = [0]
    def hall(c, d, f):
        rid = "h%d" % hall_n[0]; hall_n[0] += 1
        halls.append([rid, c, d, round(f, 2), {"small": 1, "m": 0}])
        taken[(c, d)] = rid
        return rid
    def room_at(s):
        g = taken.get(s)
        if isinstance(g, str): return g
        return ids[g] if g is not None else None
    def fy_of(rid):
        return next((r[3] for r in rooms + halls if r[0] == rid), 0)
    def emit(a_id, b_id, a_f, b_f):
        if a_id == b_id: return
        kk = tuple(sorted((a_id, b_id)))
        if kk in emitted: return
        emitted.add(kk)
        out.append([a_id, b_id, "flat" if abs(a_f - b_f) < 0.05 else "ramp"])
    def seg(a_id, a_s, a_f, b_id, b_s, b_f):
        (c1, d1), (c2, d2) = a_s, b_s
        step = (0, 1 if d2 > d1 else -1) if c1 == c2 else (1 if c2 > c1 else -1, 0)
        cur = (c1 + step[0], d1 + step[1])
        prev_id, prev_f = a_id, a_f
        while cur != (c2, d2):
            mid = room_at(cur)
            if mid:
                mf = fy_of(mid)
                emit(prev_id, mid, prev_f, mf)
                prev_id, prev_f = mid, mf
            cur = (cur[0] + step[0], cur[1] + step[1])
        emit(prev_id, b_id, prev_f, b_f)
    def route(a, b):
        a_id, b_id = ids[a], ids[b]
        a_s, b_s = slot[a], slot[b]
        a_f, b_f = fy[a], fy[b]
        if a_s[0] == b_s[0] or a_s[1] == b_s[1]:
            seg(a_id, a_s, a_f, b_id, b_s, b_f); return
        for corner in ((a_s[0], b_s[1]), (b_s[0], a_s[1])):
            mid = room_at(corner)
            if mid:
                mf = fy_of(mid)
                seg(a_id, a_s, a_f, mid, corner, mf)
                seg(mid, corner, mf, b_id, b_s, b_f)
                return
        corner = (a_s[0], b_s[1])
        mid = hall(corner[0], corner[1], (a_f + b_f) / 2)
        seg(a_id, a_s, a_f, mid, corner, (a_f + b_f) / 2)
        seg(mid, corner, (a_f + b_f) / 2, b_id, b_s, b_f)
    tree, extra = [], []
    for g in order:
        for b in sorted(adj[g]):
            if g < b:
                (tree if dist[b] == dist[g] + 1 or dist[g] == dist[b] + 1 else extra).append((g, b))
    for a, b in tree + extra:
        route(a, b)
    cellmap = {i: ids[gids[key[i]]] for i in cells}
    return rooms + halls, out, cellmap, {g: dist[g] for g in order}

# ─────────────────────────── ACE enrichment ───────────────────────────
def enrich(entry, lb, cellmap, catidx, name):
    data = ace_landblock(lb)
    if not data:
        return
    rooms, links = entry["rooms"], entry["links"]
    rid_dist = {r[0]: i for i, r in enumerate(rooms)}    # rough depth order
    by_room = defaultdict(list)
    for guid, o in data["inst"].items():
        rid = cellmap.get(o["cell"])
        if not rid:
            continue
        cat, wname = catidx.get(o["wcid"], (None, o["name"]))
        by_room[rid].append((cat, wname or o["name"], guid, o))
    # creatures: real counts -> m (capped), a rare deep denizen -> boss
    creature_rooms = defaultdict(list)
    all_creatures = Counter()
    for rid, objs in by_room.items():
        for cat, wname, guid, o in objs:
            if cat == "Creature":
                creature_rooms[rid].append(wname)
                all_creatures[wname] += 1
    roomdef = {r[0]: r for r in rooms}
    for rid, crs in creature_rooms.items():
        r = roomdef.get(rid)
        if r is None or r[4].get("t") == "entry":
            continue
        if r[4].get("t") != "boss":
            r[4]["m"] = max(0, min(3, len(crs)))
    boss_rid = next((r[0] for r in rooms if r[4].get("t") == "boss"), None)
    NOT_A_BOSS = re.compile(r"corpse|target|training|\bpup\b|\bcow\b|\brabbit\b|\bchicken\b|\bpenguin\b|\brat\b|vat\b|barrel|statue", re.I)
    RANKED = re.compile(r"lord|chief|captain|high |warlord|director|commander|master|queen|king|matron|overseer|prime|champion|elder|warchief|sovereign|magus|shaman|priest", re.I)
    uniques = [(bool(RANKED.search(n)), rid_dist.get(rid, 0), rid, n)
               for rid, crs in creature_rooms.items() for n in crs
               if all_creatures[n] == 1 and not NOT_A_BOSS.search(n)]
    if uniques:
        uniques.sort(reverse=True)
        entry["boss"] = uniques[0][3]
    elif boss_rid and creature_rooms.get(boss_rid):
        cands = [n for n in creature_rooms[boss_rid] if not NOT_A_BOSS.search(n)]
        if cands:
            entry["boss"] = Counter(cands).most_common(1)[0][0]
    # levers / pressure plates wired to doors -> lever-gated links
    linkmap = {}
    for a, b, *rest in links:
        linkmap.setdefault(a, []).append((a, b))
        linkmap.setdefault(b, []).append((a, b))
    parents = defaultdict(list)
    for pg, cg in data["links"]:
        parents[pg].append(cg)
    gated = 0
    for pg, children in parents.items():
        p = data["inst"].get(pg)
        if not p or catidx.get(p["wcid"], ("",))[0] != "Door":
            continue
        trig = next((data["inst"][c] for c in children if c in data["inst"]
                     and catidx.get(data["inst"][c]["wcid"], ("",))[0] in ("Switch", "PressurePlate")), None)
        if not trig:
            continue
        door_rid, trig_rid = cellmap.get(p["cell"]), cellmap.get(trig["cell"])
        if not door_rid or not trig_rid:
            continue
        for L in links:
            if L[2] != "flat" or len(L) > 3:
                continue
            if door_rid in (L[0], L[1]):
                L[2] = "lever"; L.append("@" + trig_rid); gated += 1
                break
    # interior portal instances -> portal rooms
    for rid, objs in by_room.items():
        r = roomdef.get(rid)
        if r is None or r[4].get("t") == "entry":
            continue
        if any(cat == "Portal" for cat, *_ in objs):
            r[4]["portal"] = 1
    # flavour: name the real denizens
    top = [n for n, _ in all_creatures.most_common(3)]
    if top:
        entry["d"] = entry["d"].rstrip(".") + " — held by " + ", ".join(top) + "."

# ─────────────────────────── main ───────────────────────────
def main():
    lbmap = json.load(open(os.path.join(ROOT, "assets", "dungeon-landblocks.json")))
    src = open(os.path.join(ROOT, "index.html")).read()
    canon = re.findall(r'\["([^"]+)"', re.search(r'const CANON_DUNGEONS=\[(.*?)\n\];', src, re.S).group(1))
    block = src[src.index("const DUNGEON_SCRIPTS={"):]
    block = block[:block.index("\n};")]
    scripted = set(re.findall(r'\n  "([^"]+)":\{', block))

    catidx = ace_category_index()
    print(f"canon {len(canon)} · scripted {len(scripted)} · mapped {len(lbmap)} · ACE weenies {len(catidx)}")

    missing = [n for n in canon if n not in scripted and n not in lbmap]
    if missing and os.path.isdir(ACE):
        print(f"resolving {len(missing)} unmapped names via ACE portal destinations...")
        found = resolve_missing(missing, ace_portal_destinations())
        for n, hexid in sorted(found.items()):
            print(f"  + {n} -> {hexid}")
            lbmap[n] = hexid
        json.dump(lbmap, open(os.path.join(ROOT, "assets", "dungeon-landblocks.json"), "w"),
                  indent=0, sort_keys=True)

    cell = DatReader(os.path.join(ACD, "client_cell_1.dat"))
    pack, skipped = {}, []
    for name in canon:
        if name in scripted: continue
        hexid = lbmap.get(name)
        if not hexid: skipped.append((name, "no landblock id")); continue
        cells = dungeon_cells(cell, int(hexid, 16))
        if not cells or len(cells) < 3: skipped.append((name, "no interior cells @ " + hexid)); continue
        rooms, links, cellmap, dist = layout_from_cells(cells)
        if not rooms or len(rooms) < 2: skipped.append((name, "degenerate layout")); continue
        levels = len(set(r[3] for r in rooms))
        entry = {
            "d": f"The true halls of {name}, rebuilt stone for stone from the world's own records — "
                 f"{len(rooms)} chambers across {levels} level{'s' if levels > 1 else ''}.",
            "rooms": rooms, "links": links,
        }
        if catidx:
            enrich(entry, int(hexid, 16), cellmap, catidx, name)
        pack[name] = entry
        print(f"  {name}: {len(cells)} cells -> {len(rooms)} rooms / {len(links)} links / {levels} levels"
              + (f" · boss {entry['boss']}" if 'boss' in entry else ""))
    outp = os.path.join(ROOT, "assets", "dungeon-layouts.json")
    json.dump(pack, open(outp, "w"), separators=(",", ":"))
    print(f"\nwrote {outp}: {len(pack)} dungeons ({os.path.getsize(outp)//1024} KB)")
    if skipped:
        print(f"skipped {len(skipped)}:")
        for n, why in skipped: print(f"  - {n}: {why}")

if __name__ == "__main__":
    main()
