#!/usr/bin/env python3
"""Generate room-for-room dungeon layouts from the real AC client data (acdata/client_cell_1.dat).

Every interior landblock stores its EnvCells: 10x10-unit structural cells with a local
position and explicit portals (doorways) to neighbouring cells — the dungeon's true
room graph. This tool, for every canon dungeon that is NOT already hand-scripted in
index.html's DUNGEON_SCRIPTS:

  1. looks up its interior landblock id (assets/dungeon-landblocks.json — community
     GoArrow navigation data, name -> hex landblock),
  2. reads all EnvCells for that landblock and keeps the component connected to the
     entrance cell (0x0100),
  3. clusters cells into rooms on a coarse grid (adaptive Q so dungeons stay <= ~72
     rooms), stacking z-levels apart so every room owns a unique (col,depth) slot,
  4. routes every cell-portal link through the grid with straight segments only
     (elbow "hall" rooms are inserted at corners — the engine's corridors are straight),
  5. emits assets/dungeon-layouts.json in the DUNGEON_SCRIPTS schema
     (rooms:[[id,col,depth,fy,{...}]], links:[[a,b,"flat|ramp"]]), which index.html
     merges over DUNGEON_SCRIPTS at boot.

Hand-scripted dungeons are skipped on purpose: their layouts encode documented key
chains/levers from the wiki walkthroughs and stay authoritative.
"""
import struct, os, re, json, sys
from collections import defaultdict, deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")
FY_SCALE = 2.6 / 6.0          # AC dungeon levels are 6 units apart; engine ramps span 2.6
MAX_ROOMS = 72                # coarsen the cluster grid until a dungeon fits

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
    # keep only the component reachable from the entrance cell
    seen, q = {0x0100}, deque([0x0100])
    while q:
        for o in cells[q.popleft()]["ports"]:
            if o in cells and o not in seen:
                seen.add(o); q.append(o)
    return {i: c for i, c in cells.items() if i in seen}

def layout_from_cells(cells):
    """Cluster cells into rooms on a grid; return (rooms, links) in engine schema, or None."""
    for Q in (20, 26, 34, 44, 56):
        rooms, links = _cluster(cells, Q)
        if rooms is not None and len(rooms) <= MAX_ROOMS:
            return rooms, links
    return rooms, links   # largest Q attempt, even if slightly over

def _cluster(cells, Q):
    key = {}
    for i, c in cells.items():
        key[i] = (int(c["x"] // Q), int(c["y"] // Q), round(c["z"] / 6.0))
    groups = defaultdict(list)
    for i, k in key.items():
        groups[k].append(i)
    gids = {k: n for n, k in enumerate(sorted(groups))}
    entry_g = gids[key[0x0100]]
    # cluster adjacency from cell portals
    adj = defaultdict(set)
    for i, c in cells.items():
        a = gids[key[i]]
        for o in c["ports"]:
            if o in key:
                b = gids[key[o]]
                if a != b:
                    adj[a].add(b); adj[b].add(a)
    # per-cluster centroid / z / size
    info = {}
    for k, members in groups.items():
        g = gids[k]
        xs = [cells[i]["x"] for i in members]; ys = [cells[i]["y"] for i in members]
        zs = [cells[i]["z"] for i in members]
        info[g] = dict(x=sum(xs)/len(xs), y=sum(ys)/len(ys), z=sum(zs)/len(zs), n=len(members))
    # BFS from entry: grid slot assignment order + boss pick (farthest)
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
        return None, None
    boss_g = max(order, key=lambda g: (dist[g], info[g]["n"]))
    # unique (col,depth) per room; stacked levels spiral to the nearest free slot
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
        c0 = round((info[g]["x"] - ex) / Q)
        d0 = round((info[g]["y"] - ey) / Q) * ysign
        s = spiral(c0, d0)
        slot[g] = s; taken[s] = g
    ez = info[entry_g]["z"]
    fy = {g: round((info[g]["z"] - ez) * FY_SCALE, 2) for g in order}
    # emit rooms
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
    # route links: straight segments only; elbows + pass-through splits
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
    def emit(a_id, a_s, a_f, b_id, b_s, b_f):
        if a_id == b_id: return
        kk = tuple(sorted((a_id, b_id)))
        if kk in emitted: return
        emitted.add(kk)
        out.append([a_id, b_id, "flat" if abs(a_f - b_f) < 0.05 else "ramp"])
    def seg(a_id, a_s, a_f, b_id, b_s, b_f):
        # same row/col guaranteed; split at occupied intermediate slots
        (c1, d1), (c2, d2) = a_s, b_s
        step = (0, 1 if d2 > d1 else -1) if c1 == c2 else (1 if c2 > c1 else -1, 0)
        cur = (c1 + step[0], d1 + step[1])
        prev_id, prev_s, prev_f = a_id, a_s, a_f
        while cur != (c2, d2):
            mid = room_at(cur)
            if mid:
                mf = next((r[3] for r in rooms + halls if r[0] == mid), (prev_f + b_f) / 2)
                emit(prev_id, prev_s, prev_f, mid, cur, mf)
                prev_id, prev_s, prev_f = mid, cur, mf
            cur = (cur[0] + step[0], cur[1] + step[1])
        emit(prev_id, prev_s, prev_f, b_id, b_s, b_f)
    def route(a, b):
        a_id, b_id = ids[a], ids[b]
        a_s, b_s = slot[a], slot[b]
        a_f, b_f = fy[a], fy[b]
        if a_s[0] == b_s[0] or a_s[1] == b_s[1]:
            seg(a_id, a_s, a_f, b_id, b_s, b_f); return
        for corner in ((a_s[0], b_s[1]), (b_s[0], a_s[1])):
            mid = room_at(corner)
            if mid:   # bend through the room already standing at the corner
                mf = next((r[3] for r in rooms + halls if r[0] == mid), (a_f + b_f) / 2)
                seg(a_id, a_s, a_f, mid, corner, mf)
                seg(mid, corner, mf, b_id, b_s, b_f)
                return
        corner = (a_s[0], b_s[1])
        mid = hall(corner[0], corner[1], (a_f + b_f) / 2)
        seg(a_id, a_s, a_f, mid, corner, (a_f + b_f) / 2)
        seg(mid, corner, (a_f + b_f) / 2, b_id, b_s, b_f)
    # spanning-tree links first (connectivity), then the loops
    tree, extra = [], []
    for g in order:
        for b in sorted(adj[g]):
            if g < b:
                (tree if dist[b] == dist[g] + 1 or dist[g] == dist[b] + 1 else extra).append((g, b))
    for a, b in tree + extra:
        route(a, b)
    return rooms + halls, out

def main():
    lbmap = json.load(open(os.path.join(ROOT, "assets", "dungeon-landblocks.json")))
    src = open(os.path.join(ROOT, "index.html")).read()
    canon = re.findall(r'\["([^"]+)"', re.search(r'const CANON_DUNGEONS=\[(.*?)\n\];', src, re.S).group(1))
    block = src[src.index("const DUNGEON_SCRIPTS={"):]
    block = block[:block.index("\n};")]
    scripted = set(re.findall(r'\n  "([^"]+)":\{', block))
    print(f"canon {len(canon)} · scripted {len(scripted)} · mapped {len(lbmap)}")

    cell = DatReader(os.path.join(ACD, "client_cell_1.dat"))
    pack, skipped = {}, []
    for name in canon:
        if name in scripted: continue
        hexid = lbmap.get(name)
        if not hexid: skipped.append((name, "no landblock id")); continue
        cells = dungeon_cells(cell, int(hexid, 16))
        if not cells or len(cells) < 3: skipped.append((name, "no interior cells @ " + hexid)); continue
        rooms, links = layout_from_cells(cells)
        if not rooms or len(rooms) < 2: skipped.append((name, "degenerate layout")); continue
        levels = len(set(r[3] for r in rooms))
        pack[name] = {
            "d": f"The true halls of {name}, rebuilt stone for stone from the world's own records — "
                 f"{len(rooms)} chambers across {levels} level{'s' if levels > 1 else ''}.",
            "rooms": rooms, "links": links,
        }
        print(f"  {name}: {len(cells)} cells -> {len(rooms)} rooms / {len(links)} links / {levels} levels")
    outp = os.path.join(ROOT, "assets", "dungeon-layouts.json")
    json.dump(pack, open(outp, "w"), separators=(",", ":"))
    print(f"\nwrote {outp}: {len(pack)} dungeons ({os.path.getsize(outp)//1024} KB)")
    if skipped:
        print(f"skipped {len(skipped)}:")
        for n, why in skipped: print(f"  - {n}: {why}")

if __name__ == "__main__":
    main()
