#!/usr/bin/env python3
"""Extract every enterable TOWN BUILDING interior from the cell dat (#767).

Surface landblocks carry a LandblockInfo record (fid = lb<<16 | 0xFFFE) whose
BuildInfo table lists each building: model id, landblock-local frame, and portals
whose OtherCellId links into the landblock's interior EnvCells. BFS from those
portal cells over the cell portal/visibility graph gives each building its full
interior — the real insides of every cottage, shop, hall and tavern in Dereth
(scan: 6,979 buildings, 5,464 with interiors).

The client's towns are procedural, not landblock-mapped, so instances are useless
raw; what it needs is a LIBRARY. Interiors are emitted in BUILDING-LOCAL space
(cell frames re-based through the inverse building frame) and deduped by content
hash — every identical cottage collapses to one file, with a use count.

Outputs:
  assets/acinteriors/I<hash>.json   {groups:[...acdungeons mesh schema...],
                                     stabs:[[sidHex,x,y,z,qx,qy,qz,qw]...],   # furniture
                                     cells,entry:[x,y,z],bbox:[min,max]}
  assets/acinteriors/index.json     {texBase, interiors:[{file,cells,tris,uses,
                                     mids,stabs,bbox,entry,sampleLb}]}
  textures                          shared pool assets/acdungeons/tex/ (same resolver)
  new furniture setups              appended to assets/acdstatics/ (+its index.json)

Coordinate note: all composition happens in AC space (z-up, w-first quats);
tx_pos/tx_quat convert only at the final write, exactly like the dungeon exporter.
"""
import os, sys, json, hashlib
from collections import Counter, deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_model_export as ame
import ac_env_export as env
from ac_model_export import DatReader, Buf
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "acinteriors")
DSTAT = os.path.join(ROOT, "assets", "acdstatics")

def qconj(q): return (q[0], -q[1], -q[2], -q[3])          # w-first
def qmul(a, b):
    aw, ax, ay, az = a; bw, bx, by, bz = b
    return (aw*bw - ax*bx - ay*by - az*bz,
            aw*bx + ax*bw + ay*bz - az*by,
            aw*by - ax*bz + ay*bw + az*bx,
            aw*bz + ax*by - ay*bx + az*bw)

def parse_lbi(data):
    """LandblockInfo → buildings [{mid,pos,quat,ports}]. Layout per ACE (numBuildings
    is a u16 followed by a u16 flags word — reading them as one u32 breaks 36% of
    landblocks; portal stab lists end on a 4-byte AlignBoundary, not parity)."""
    r = Buf(data)
    r.u32(); r.u32()
    for _ in range(r.u32()): r.u32(); r.f(7)               # outdoor Stab objects
    nb = r.u16(); r.u16()                                   # numBuildings, buildingFlags
    blds = []
    for _ in range(nb):
        mid = r.u32(); pos = r.f(3); quat = r.f(4)          # frame quat is w-first
        r.u32()                                             # numLeaves
        ports = []
        for _ in range(r.u32()):
            r.u16(); oc = r.u16(); r.u16(); ns = r.u16()    # Flags, OtherCellId, OtherPortalId? — oc verified against interiors
            for _ in range(ns): r.u16()
            if r.o % 4: r.o += 4 - (r.o % 4)
            if 0x100 <= oc < 0xFFFE: ports.append(oc)
        blds.append(dict(mid=mid, pos=pos, quat=quat, ports=ports))
    return blds

def parse_cell_all(data):
    """One EnvCell: geometry fields + portal/vis edges + furniture stabs."""
    r = Buf(data)
    r.u32(); flags = r.u32(); r.u32()
    nsurf = r.u8(); nport = r.u8(); nvis = r.u16()
    surfaces = [0x08000000 | r.u16() for _ in range(nsurf)]
    envid = 0x0D000000 | r.u16(); cstruct = r.u16()
    pos = r.f(3); quat = r.f(4)
    edges = []
    for _ in range(nport):
        r.u16(); r.u16(); oc = r.u16(); r.u16()             # CellPortal: Flags, PolygonId, OtherCellId, OtherPortalId
        if 0x100 <= oc < 0xFFFE: edges.append(oc)
    for _ in range(nvis):
        vc = r.u16()
        if 0x100 <= vc < 0xFFFE: edges.append(vc)
    stabs = []
    if flags & 2:
        for _ in range(r.u32()):
            sid = r.u32(); sp = r.f(3); sq = r.f(4)
            if 0x01000000 <= sid <= 0x02FFFFFF:
                stabs.append((sid, sp, sq))
    return dict(surfaces=surfaces, envid=envid, cstruct=cstruct, pos=pos, quat=quat,
                edges=edges, stabs=stabs)

def main():
    portal = DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    cell = DatReader(os.path.join(ROOT, "acdata", "client_cell_1.dat"))
    os.makedirs(OUT, exist_ok=True)
    material, tex_written, _skip = env.make_material_resolver(portal)
    envcache = {}

    # per-landblock interior cell ids, discovered once
    lb_cells = {}
    for fid in cell.files:
        low = fid & 0xFFFF
        if 0x100 <= low < 0xFFFE:
            lb_cells.setdefault(fid >> 16, set()).add(low)

    interiors = {}            # content hash -> record
    stat_use = Counter()      # furniture setups referenced by any kept interior
    n_bld = n_int = n_badcell = 0
    for fid in sorted(cell.files):
        if (fid & 0xFFFF) != 0xFFFE: continue
        lb = fid >> 16
        try:
            blds = parse_lbi(cell.read(fid))
        except Exception:
            continue
        blds = [b for b in blds if b["ports"]]
        if not blds: continue
        have = lb_cells.get(lb, set())
        cache = {}
        def cellrec(cid):
            if cid not in cache:
                try:
                    cache[cid] = parse_cell_all(cell.read((lb << 16) | cid))
                except Exception:
                    cache[cid] = None
            return cache[cid]
        n_bld += len(blds)
        for b in blds:
            seen = set(); q = deque(c for c in b["ports"] if c in have)
            while q:
                c = q.popleft()
                if c in seen: continue
                seen.add(c)
                rec = cellrec(c)
                if rec is None: continue
                for e in rec["edges"]:
                    if e in have and e not in seen: q.append(e)
            cells = [c for c in sorted(seen) if cellrec(c)]
            if not cells: continue
            n_int += 1
            bq, bp = b["quat"], b["pos"]; cj = qconj(bq)
            def rebase(p, qq):                              # landblock-local -> building-local (AC space)
                d = (p[0]-bp[0], p[1]-bp[1], p[2]-bp[2])
                return env.qrot(cj, d), qmul(cj, qq)
            sig = [b["mid"]]
            for c in cells:
                rc = cellrec(c)
                rp, rq = rebase(rc["pos"], rc["quat"])
                sig.append((rc["envid"], rc["cstruct"], tuple(rc["surfaces"]),
                            tuple(round(v, 2) for v in rp), tuple(round(v, 3) for v in rq)))
                for sid, sp, sq in rc["stabs"]:
                    rsp, rsq = rebase(sp, sq)
                    sig.append((sid, tuple(round(v, 2) for v in rsp)))
            h = hashlib.md5(repr(sig).encode()).hexdigest()[:10]
            if h in interiors:
                interiors[h]["uses"] += 1
                interiors[h]["mids"].add(b["mid"])
                continue
            # build geometry + stabs once, in building-local space
            groups = {}; tris = 0; stabs_out = []; cellpos = []
            for c in cells:
                rc = cellrec(c)
                e = envcache.get(rc["envid"])
                if rc["envid"] not in envcache:
                    try:
                        e = envcache[rc["envid"]] = env.parse_environment(portal.read(rc["envid"]))
                    except Exception:
                        e = envcache[rc["envid"]] = None
                cs = e.get(rc["cstruct"]) if e else None
                if cs is None:
                    n_badcell += 1; continue
                rp, rq = rebase(rc["pos"], rc["quat"])
                # interior cells all share the BUILDING frame (offsets live in the vertices), so the
                # walkable rect comes from this cell's transformed vertex bounds, not its frame origin
                xs = []; ys = []; zs = []
                for v in cs["verts"].values():
                    w = env.qrot(rq, v["o"])
                    p = env.tx_pos((w[0]+rp[0], w[1]+rp[1], w[2]+rp[2]))
                    xs.append(p[0]); ys.append(p[1]); zs.append(p[2])
                if xs:
                    cellpos.append([round(min(xs), 2), round(min(zs), 2),
                                    round(max(xs), 2), round(max(zs), 2), round(min(ys), 2)])
                tris += env._emit_cell(cs, rp, rq, rc["surfaces"], groups, material)
                for sid, sp, sq in rc["stabs"]:
                    rsp, rsq = rebase(sp, sq)
                    p = env.tx_pos(rsp); qe = ame.tx_quat(rsq)
                    stabs_out.append(["%08X" % sid, p[0], p[1], p[2],
                                      round(qe[0], 4), round(qe[1], 4), round(qe[2], 4), round(qe[3], 4)])
                    stat_use[sid] += 1
            if not groups: continue
            gout = [dict(mat=g["mat"], v=g["verts"], n=g["norms"], uv=g["uvs"], i=g["idx"])
                    for _, g in sorted(groups.items()) if len(g["idx"]) >= 3]
            xs = [v for g in gout for v in g["v"][0::3]]
            ys = [v for g in gout for v in g["v"][1::3]]
            zs = [v for g in gout for v in g["v"][2::3]]
            bbox = [[round(min(xs), 2), round(min(ys), 2), round(min(zs), 2)],
                    [round(max(xs), 2), round(max(ys), 2), round(max(zs), 2)]]
            # entry = centre of the first (door-portal) cell's walk rect
            e0 = cellpos[0] if cellpos else [0, 0, 0, 0, 0]
            entry = [round((e0[0]+e0[2])/2, 2), e0[4], round((e0[1]+e0[3])/2, 2)]
            interiors[h] = dict(file="I%s.json" % h, cells=len(cells), tris=tris, uses=1,
                                mids={b["mid"]}, stabs=len(stabs_out), bbox=bbox,
                                entry=entry, sampleLb="%04X" % lb,
                                _groups=gout, _stabs=stabs_out, _cellpos=cellpos)
    print(f"{n_bld} buildings with interiors -> {n_int} instances -> {len(interiors)} unique layouts "
          f"({n_badcell} bad cells, {len(stat_use)} furniture setups)")

    # furniture setups not yet in the dungeon-statics pool: export + append to its index
    didx_path = os.path.join(DSTAT, "index.json")
    didx = json.load(open(didx_path))
    known = {int(s["id"], 16) for s in didx["setups"]}
    added = 0
    from ac_dstatics_export import qrot3
    tex_c = {}
    def surf_mat(sidd):
        s = ame.parse_surface(portal.read(sidd))
        if "color" in s: return dict(color=s["color"] & 0xFFFFFF)
        tid = ame.parse_surfacetexture(portal.read(s["tex"]))[-1]
        fn = "%08X.png" % tid
        pth = os.path.join(DSTAT, "tex", fn)
        if tid not in tex_c:
            if not os.path.isfile(pth):
                dec = ame.decode_texture(portal.read(tid), lambda pid: ame.parse_palette(portal.read(pid)),
                                         clip=bool(s.get("clip")))
                if dec is None: tex_c[tid] = None
                else:
                    w, hh, px = dec
                    im = Image.frombytes("RGBA", (w, hh), px)
                    if im.getchannel("A").getextrema()[0] == 255:
                        im = im.convert("RGB").quantize(colors=256, method=Image.MAXCOVERAGE)
                    im.save(pth, optimize=True); tex_c[tid] = fn
            else: tex_c[tid] = fn
        out = dict(tex=tex_c[tid]) if tex_c[tid] else dict(color=0x8a8478)
        if s.get("clip") and "tex" in out: out["clip"] = 1
        return out
    for sid in sorted(set(stat_use) - known):
        gout = []
        try:
            if sid >= 0x02000000:
                su = ame.parse_setup(portal.read(sid)); frames = su.get("frames") or []
                parts = [dict(gid=g, p=ame.tx_pos(frames[i]["p"]) if i < len(frames) else (0, 0, 0),
                              q=ame.tx_quat(frames[i]["q"]) if i < len(frames) else (0, 0, 0, 1))
                         for i, g in enumerate(su["parts"])]
            else:
                parts = [dict(gid=sid, p=(0, 0, 0), q=(0, 0, 0, 1))]
            for prt in parts:
                gfx = ame.parse_gfxobj(portal.read(prt["gid"]))
                for surfidx, g in sorted(ame.build_part(gfx).items()):
                    if len(g["idx"]) < 3: continue
                    ssid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                    v2 = []; n2 = []
                    for k in range(0, len(g["verts"]), 3):
                        w = qrot3(prt["q"], tuple(g["verts"][k:k+3]))
                        v2 += [round(w[0]+prt["p"][0], 4), round(w[1]+prt["p"][1], 4), round(w[2]+prt["p"][2], 4)]
                        rn = qrot3(prt["q"], tuple(g["norms"][k:k+3]))
                        n2 += [round(rn[0], 4), round(rn[1], 4), round(rn[2], 4)]
                    gout.append(dict(mat=(surf_mat(ssid) if ssid else dict(color=0x8a8478)),
                                     v=v2, n=n2, uv=g["uvs"], i=g["idx"]))
        except Exception:
            continue
        if not gout: continue
        json.dump(dict(groups=gout), open(os.path.join(DSTAT, "S%08X.json" % sid), "w"),
                  separators=(",", ":"))
        didx["setups"].append(dict(id="%08X" % sid, lum=0, uses=stat_use[sid]))
        added += 1
    if added:
        json.dump(didx, open(didx_path, "w"), separators=(",", ":"))
    print(f"furniture: {added} new setups appended to acdstatics (pool now {len(didx['setups'])})")

    for h, it in interiors.items():
        json.dump(dict(groups=it.pop("_groups"), stabs=it.pop("_stabs"),
                       rects=it.pop("_cellpos"),
                       cells=it["cells"], entry=it["entry"], bbox=it["bbox"]),
                  open(os.path.join(OUT, it["file"]), "w"), separators=(",", ":"))
        it["mids"] = sorted("%08X" % m for m in it["mids"])
    idx = sorted(interiors.values(), key=lambda x: -x["uses"])
    json.dump(dict(texBase="assets/acdungeons/tex/", statBase="assets/acdstatics/",
                   interiors=idx),
              open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    big = sum(1 for i in idx if i["cells"] >= 8)
    print(f"wrote {len(idx)} interiors ({big} of 8+ cells) -> assets/acinteriors/")

if __name__ == "__main__":
    main()
