#!/usr/bin/env python3
"""Extract real 3D dungeon environment meshes from the AC client data.

An interior landblock (client_cell_1.dat) is a graph of EnvCells: each EnvCell names
an Environment (0x0D..., in client_portal.dat) — a reusable room-piece mesh (walls,
arches, pillars, stairs) — plus a CellStruct index into that Environment's cell table
and a world placement Frame (origin + quaternion). The EnvCell also carries its own
Surfaces list (0x08... Surface DIDs); a CellStruct polygon's surface field is an INDEX
into THAT list, which is how one geometry template is re-skinned per placement.

This tool, for a sample of iconic dungeons (name->landblock via dungeon-landblocks.json,
the same map ac_dungeon_export.py uses), assembles every EnvCell of a dungeon into ONE
merged mesh: load each cell's Environment->CellStruct, rotate+translate its verts by the
cell's world frame in AC space, convert AC z-up -> three y-up via (x,z,-y), and merge into
per-material (per-texture / per-color) triangle groups.

Formats per the ACEmulator DatLoader (Environment/CellStruct/CVertexArray/SWVertex/
Polygon/BSPNode/BSPLeaf/BSPPortal, BSD-licensed reference implementation). The GfxObj
vertex/polygon and Surface/Texture/Palette parse is shared with ac_model_export.py.

Output (assets/acdungeons/):
  <name>.json    — { groups:[{tex|color, verts, normals, uvs, idx}] }  (acmodels schema)
  tex/<id>.png   — decoded textures
  index.json     — dungeon name -> file + landblock + stats
Huge dungeons are capped to the first CAP cells (noted in the per-dungeon stats).
"""
import struct, os, json, math
from collections import defaultdict

# reuse the proven readers/parsers rather than re-implementing them
import ac_dungeon_export as dung
import ac_model_export as mdl
from ac_model_export import (Buf, read_polygon, parse_surface, parse_surfacetexture,
                             parse_palette, decode_texture)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD  = os.path.join(ROOT, "acdata")
OUT  = os.path.join(ROOT, "assets", "acdungeons")
CAP  = 400   # cap huge dungeons to the first N cells

# ~8 iconic dungeons already in the game (name -> landblock hex from dungeon-landblocks.json)
SAMPLE = ["Holtburg Dungeon", "Green Mire Grave", "Mines of Despair", "Halls of the Helm",
          "Glenden Wood Dungeon", "Colossus Foundry", "Empyrean Foundry", "Mattekar Cave"]

# ── AC coordinate transform (AC z-up -> three y-up), shared with ac_model_export ──
def tx_pos(v): return [round(v[0], 4), round(v[2], 4), round(-v[1], 4)]

def qrot(q, v):
    """rotate vector v by quaternion q=[w,x,y,z] (AC space)."""
    w, x, y, z = q
    tx = 2.0 * (y * v[2] - z * v[1])
    ty = 2.0 * (z * v[0] - x * v[2])
    tz = 2.0 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty),
            v[1] + w * ty + (z * tx - x * tz),
            v[2] + w * tz + (x * ty - y * tx))

# ── EnvCell: id, flags, own Surfaces list, EnvironmentId, CellStructure, world Frame ──
def parse_envcell_full(data):
    r = Buf(data)
    r.u32()                                    # Id
    flags = r.u32()
    r.u32()                                    # CellId (repeat, skipped)
    nsurf = r.u8(); r.u8()                      # numSurfaces, numPortals
    r.u16()                                    # numStabs
    surfaces = [0x08000000 | r.u16() for _ in range(nsurf)]   # this cell's Surface DIDs
    envid   = 0x0D000000 | r.u16()
    cstruct = r.u16()
    pos = r.f(3); quat = r.f(4)                 # Frame: origin(x,y,z) + quaternion(w,x,y,z)
    return dict(surfaces=surfaces, envid=envid, cstruct=cstruct, pos=pos, quat=quat)

# ── BSP consumption (must stay byte-exact to reach the next CellStruct) ──
def align4(r):
    m = r.o % 4
    if m: r.o += 4 - m

# only these node tags carry children (per ACE BSPNode.Unpack switch); (pos, neg).
# ANY other tag (e.g. 'BPFL', 'BPOL') is terminal: plane only, no child descent.
_BSP_CHILD = {"BPnn": (1, 0), "BPIn": (1, 0), "BpIN": (0, 1),
              "BpnN": (0, 1), "BPIN": (1, 1), "BPnN": (1, 1)}

def read_bsp(r, tree):
    """tree in {'cell','physics','drawing'}; per ACE BSPNode/BSPLeaf/BSPPortal.Unpack."""
    t = r.tag()
    if t == "LEAF":
        r.i32()                                 # LeafIndex
        if tree == "physics":
            r.i32()                             # Solid
            r.f(4)                              # Sphere: origin(3f)+radius(f)
            n = r.u32()
            for _ in range(n): r.u16()          # InPolys
        return
    if t == "PORT":
        r.f(4)                                  # SplittingPlane
        read_bsp(r, tree)                       # PosNode
        read_bsp(r, tree)                       # NegNode
        if tree == "drawing":
            r.f(4)                              # Sphere
            npoly = r.u32(); nport = r.u32()
            for _ in range(npoly): r.u16()      # InPolys
            for _ in range(nport): r.u16(); r.u16()   # InPortals (PortalPoly = 2 shorts)
        return
    if not (len(t) == 4 and t[0] == "B"):
        raise ValueError("bad BSP tag %r @%d" % (t, r.o))
    r.f(4)                                      # SplittingPlane
    pos, neg = _BSP_CHILD.get(t, (0, 0))
    if pos: read_bsp(r, tree)                   # positive child
    if neg: read_bsp(r, tree)                   # negative child
    if tree == "cell":
        return
    r.f(4)                                      # Sphere
    if tree == "drawing":
        n = r.u32()
        for _ in range(n): r.u16()              # InPolys

# ── CellStruct + Environment ──
def parse_cellstruct(r):
    n_poly = r.u32(); n_phys = r.u32(); n_port = r.u32()
    vtype = r.i32()
    if vtype != 1:
        raise ValueError("VertexType %d unsupported" % vtype)
    nverts = r.u32()
    verts = {}
    for _ in range(nverts):
        idx = r.u16(); nuv = r.u16()
        o = r.f(3); nrm = r.f(3)
        uvs = [(r.f(), r.f()) for _ in range(nuv)]
        verts[idx] = dict(o=o, n=nrm, uv=uvs)
    polys = {}
    for _ in range(n_poly):
        k = r.u16(); polys[k] = read_polygon(r)
    for _ in range(n_port): r.u16()             # PortalPolygon ids
    align4(r)
    read_bsp(r, "cell")
    for _ in range(n_phys): r.u16(); read_polygon(r)
    read_bsp(r, "physics")
    if r.u32():                                 # hasDrawingBSP
        read_bsp(r, "drawing")
    align4(r)
    return dict(verts=verts, polys=polys)

def parse_environment(data):
    r = Buf(data)
    r.u32()                                     # Id
    ncells = r.u32()
    cells = {}
    for _ in range(ncells):
        key = r.u32()
        cells[key] = parse_cellstruct(r)
    if r.o < len(data) - 4 or r.o > len(data):  # allow <4 bytes trailing alignment pad
        raise ValueError("environment consumed %d of %d bytes" % (r.o, len(data)))
    return cells

# ── DXT decoding (environment textures are DXT1/3/5 — SurfacePixelFormat FourCCs) ──
DXT1, DXT3, DXT5 = 827611204, 861165636, 894720068

def _rgb565(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)

def decode_dxt(data, w, h, fmt):
    px = bytearray(w * h * 4)
    bw, bh = (w + 3) // 4, (h + 3) // 4
    o = 0
    blk = 8 if fmt == DXT1 else 16
    for by in range(bh):
        for bx in range(bw):
            block = data[o:o + blk]; o += blk
            alpha = None
            if fmt == DXT1:
                cd = block
            else:
                cd = block[8:]
            c0, c1 = struct.unpack_from("<2H", cd, 0)
            bits = struct.unpack_from("<I", cd, 4)[0]
            r0, g0, b0 = _rgb565(c0); r1, g1, b1 = _rgb565(c1)
            col = [(r0, g0, b0, 255), (r1, g1, b1, 255), None, None]
            if fmt == DXT1 and c0 <= c1:
                col[2] = ((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255)
                col[3] = (0, 0, 0, 0)                          # 1-bit transparent
            else:
                col[2] = ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255)
                col[3] = ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255)
            if fmt == DXT3:
                araw = struct.unpack_from("<Q", block, 0)[0]
                alpha = [((araw >> (4 * i)) & 0xF) * 17 for i in range(16)]
            elif fmt == DXT5:
                a0, a1 = block[0], block[1]
                at = [a0, a1]
                if a0 > a1:
                    at += [((6 - k) * a0 + (k + 1) * a1) // 7 for k in range(6)]
                else:
                    at += [((5 - k) * a0 + (k + 1) * a1) // 6 for k in range(5)] + [0, 255]
                aidx = int.from_bytes(block[2:8], "little")
                alpha = [at[(aidx >> (3 * i)) & 7] for i in range(16)]
            for i in range(16):
                x = bx * 4 + (i % 4); y = by * 4 + (i // 4)
                if x >= w or y >= h: continue
                rr, gg, bb, aa = col[(bits >> (2 * i)) & 3]
                if alpha is not None: aa = alpha[i]
                p = (y * w + x) * 4
                px[p:p + 4] = bytes((rr, gg, bb, aa))
    return w, h, bytes(px)

def decode_texture_ex(data, palettes):
    """dispatch DXT here, everything else to the proven ac_model_export.decode_texture."""
    fmt = struct.unpack_from("<I", data, 16)[0]
    w, h = struct.unpack_from("<ii", data, 8)
    if fmt in (DXT1, DXT3, DXT5):
        return decode_dxt(data[24:], w, h, fmt)
    return decode_texture(data, palettes)

# ── material resolution (shared surface->texture/color path) ──
def make_material_resolver(portal):
    pal_cache, tex_written, surf_cache = {}, {}, {}
    def palettes(pid):
        if pid not in pal_cache:
            pal_cache[pid] = parse_palette(portal.read(pid))
        return pal_cache[pid]
    def material(surf_did):
        if surf_did in surf_cache:
            return surf_cache[surf_did]
        try:
            s = parse_surface(portal.read(surf_did))
        except Exception:
            m = dict(color=0x888888); surf_cache[surf_did] = m; return m
        if "color" in s:
            m = dict(color=s["color"] & 0xFFFFFF)
        else:
            try:
                tid = parse_surfacetexture(portal.read(s["tex"]))[-1]
            except Exception:
                m = dict(color=0x888888); surf_cache[surf_did] = m; return m
            if tid not in tex_written:
                try:
                    dec = decode_texture_ex(portal.read(tid), palettes)
                except Exception:
                    dec = None
                if dec is None:
                    tex_written[tid] = None
                else:
                    from PIL import Image
                    w, h, px = dec
                    Image.frombytes("RGBA", (w, h), px).save(
                        os.path.join(OUT, "tex", "%08X.png" % tid))
                    tex_written[tid] = "%08X.png" % tid
            fn = tex_written[tid]
            m = dict(tex=fn) if fn else dict(color=0x777777)
            if s.get("clip"): m["clip"] = 1
        surf_cache[surf_did] = m
        return m
    return material, tex_written

# ── merge one dungeon's cells into per-material triangle groups ──
def group_key(mat):
    return ("tex", mat["tex"]) if "tex" in mat else ("col", mat["color"])

def build_dungeon(cellids, cell_dat, portal, envcache, material):
    """cellids: list of (landblock cell fid). returns (groups, stats)."""
    groups = {}   # group_key -> dict(mat, verts, norms, uvs, idx)
    stats = dict(cells_ok=0, cells_skipped=0, tris=0, env_fail=set(), no_cstruct=0)
    for fid in cellids:
        try:
            ec = parse_envcell_full(cell_dat.read(fid))
        except Exception:
            stats["cells_skipped"] += 1; continue
        envid = ec["envid"]
        if envid not in envcache:
            try:
                envcache[envid] = parse_environment(portal.read(envid))
            except Exception as e:
                envcache[envid] = None
                stats["env_fail"].add("%08X:%s" % (envid, str(e)[:40]))
        env = envcache[envid]
        if env is None:
            stats["cells_skipped"] += 1; continue
        cs = env.get(ec["cstruct"])
        if cs is None:
            stats["no_cstruct"] += 1; stats["cells_skipped"] += 1; continue
        pos, quat, surfs = ec["pos"], ec["quat"], ec["surfaces"]
        cell_tris = _emit_cell(cs, pos, quat, surfs, groups, material)
        stats["tris"] += cell_tris
        stats["cells_ok"] += 1
    stats["env_fail"] = sorted(stats["env_fail"])
    return groups, stats

def _emit_cell(cs, pos, quat, surfs, groups, material):
    verts = cs["verts"]
    tris = 0
    for poly in cs["polys"].values():
        si = poly["surf"]
        surf_did = surfs[si] if 0 <= si < len(surfs) else None
        mat = material(surf_did) if surf_did is not None else dict(color=0x888888)
        gk = group_key(mat)
        g = groups.get(gk)
        if g is None:
            g = groups[gk] = dict(mat=mat, verts=[], norms=[], uvs=[], idx=[], vmap={})
        corner = []
        for i, vid in enumerate(poly["v"]):
            uvi = poly["uv"][i] if poly["uv"] else 0
            key = (vid, uvi)
            if key not in g["vmap"]:
                v = verts[vid & 0xFFFF]
                wp = qrot(quat, v["o"])
                wp = (wp[0] + pos[0], wp[1] + pos[1], wp[2] + pos[2])
                wn = qrot(quat, v["n"])
                g["vmap"][key] = len(g["verts"]) // 3
                g["verts"] += tx_pos(wp)
                g["norms"] += tx_pos(wn)
                uv = v["uv"][uvi] if uvi < len(v["uv"]) else (0, 0)
                g["uvs"] += [round(uv[0], 4), round(uv[1], 4)]
            corner.append(g["vmap"][key])
        for i in range(1, len(corner) - 1):        # fan-triangulate, reversed winding (axis flip)
            g["idx"] += [corner[0], corner[i + 1], corner[i]]
            tris += 1
    return tris

# ── main ──
def main():
    os.makedirs(os.path.join(OUT, "tex"), exist_ok=True)
    lbmap = json.load(open(os.path.join(ROOT, "assets", "dungeon-landblocks.json")))
    cell_dat = dung.DatReader(os.path.join(ACD, "client_cell_1.dat"))
    portal   = mdl.DatReader(os.path.join(ACD, "client_portal.dat"))
    material, tex_written = make_material_resolver(portal)

    envcache = {}
    index = {}
    print("dungeon                        lb    cells  used  tris     groups  file")
    ALL = sorted(lbmap)
    for name in ALL:
        hexid = lbmap.get(name)
        if not hexid:
            print(f"  {name}: NOT IN dungeon-landblocks.json"); continue
        lb = int(hexid, 16)
        cells = dung.dungeon_cells(cell_dat, lb)
        if not cells:
            print(f"  {name} @ {hexid}: no interior cells"); continue
        ncells = len(cells)
        ids = sorted(cells)              # cell ids within the component
        capped = False
        if len(ids) > CAP:
            ids = ids[:CAP]; capped = True
        fids = [(lb << 16) | cid for cid in ids]

        groups, stats = build_dungeon(fids, cell_dat, portal, envcache, material)
        gout = []
        for g in groups.values():
            if not g["idx"]:
                continue
            entry = dict(g["mat"])
            entry.update(verts=g["verts"], normals=g["norms"], uvs=g["uvs"], idx=g["idx"])
            gout.append(entry)

        fn = name.replace(" ", "_") + ".json"
        # cell origins in three-space for spawns / entry / hoard placement + BFS depth order
        from collections import deque as _dq
        seen={0x0100 if 0x0100 in cells else min(cells):0}
        q=_dq(list(seen))
        while q:
            cur=q.popleft()
            for o in cells[cur]["ports"]:
                if o in cells and o not in seen: seen[o]=seen[cur]+1; q.append(o)
        order=sorted(seen, key=lambda c: seen[c])
        cellpos=[[round(cells[c]["x"],2), round(cells[c]["z"],2), round(-cells[c]["y"],2)] for c in order]
        payload = dict(name=name, landblock=hexid,
                       cells=stats["cells_ok"], entry=cellpos[0], far=cellpos[-1],
                       cellPos=cellpos, groups=gout)
        json.dump(payload, open(os.path.join(OUT, fn), "w"), separators=(",", ":"))
        kb = os.path.getsize(os.path.join(OUT, fn)) // 1024
        index[name] = dict(file=fn, landblock=hexid, cells=stats["cells_ok"],
                           tris=stats["tris"], groups=len(gout), capped=capped)
        note = " CAP@%d" % CAP if capped else ""
        print(f"  {name:<28} {hexid}  {ncells:5d} {stats['cells_ok']:5d} {stats['tris']:7d} "
              f"{len(gout):5d}   {kb}KB{note}")
        if stats["env_fail"]:
            print(f"      env parse failures: {stats['env_fail']}")
        if stats["no_cstruct"]:
            print(f"      cells with missing CellStruct index: {stats['no_cstruct']}")

    json.dump(index, open(os.path.join(OUT, "index.json"), "w"), indent=1)
    ntex = len([t for t in tex_written.values() if t])
    total = sum(os.path.getsize(os.path.join(dp, f))
                for dp, _, fs in os.walk(OUT) for f in fs) // 1024
    print(f"\nwrote {OUT}: {len(index)} dungeons, {ntex} textures, {total} KB total")

if __name__ == "__main__":
    main()
