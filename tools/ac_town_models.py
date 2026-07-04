#!/usr/bin/env python3
"""Bake the REAL Asheron's Call town-building & scenery meshes from client_portal.dat.

assets/actowns.json (tools/ace_building_export.py) lists, per town, every real structure the
client places on that landblock — a GfxObj (0x0100..) or Setup (0x0200..) DID plus a local
x/z offset and a y-yaw. That file carries the PLACEMENTS; this tool bakes the GEOMETRY those
DIDs point at, so index.html can render the authentic AC buildings at their authentic spots
(the same pipeline the weapon/armor/house extractors use — build_part + Surface/Texture/Palette).

For each unique building/scenery/fixture/statue DID:
  * 0x01 DID  -> one GfxObj, placed at identity
  * 0x02 DID  -> a Setup: each part's GfxObj transformed by its placement frame, merged
Every group is emitted as {mat:{tex|color[,clip]}, v,n,uv,i} (the acmodels/acitemmodels schema),
so the existing hs-style loader can consume it unchanged.

Output:
  assets/actownmodels/<DIDHEX>.json      # {groups:[...], bb:[x0,y0,z0,x1,y1,z1]}
  assets/actownmodels/tex/<TID>.png      # decoded textures (shared)
  assets/actownmodels/index.json         # { did: {file, tris, bb} }  + meta/stats
"""
import os, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
OUT = os.path.join(ROOT, "assets", "actownmodels")
TEXOUT = os.path.join(OUT, "tex")
KINDS = ("building", "scenery", "fixture", "statue")

def qrot(q, v):
    qv = (q[0], q[1], q[2]); t = [2*(qv[1]*v[2]-qv[2]*v[1]), 2*(qv[2]*v[0]-qv[0]*v[2]), 2*(qv[0]*v[1]-qv[1]*v[0])]
    return (v[0]+q[3]*t[0]+qv[1]*t[2]-qv[2]*t[1],
            v[1]+q[3]*t[1]+qv[2]*t[0]-qv[0]*t[2],
            v[2]+q[3]*t[2]+qv[0]*t[1]-qv[1]*t[0])

def main():
    os.makedirs(TEXOUT, exist_ok=True)
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    from PIL import Image
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]
    tex_written = {}
    def surface_material(sid):
        s = ame.parse_surface(portal.read(sid))
        if "color" in s: return dict(color=s["color"] & 0xFFFFFF)
        tid = ame.parse_surfacetexture(portal.read(s["tex"]))[-1]
        if tid not in tex_written:
            fn = "%08X.png" % tid
            if not os.path.isfile(os.path.join(TEXOUT, fn)):
                dec = ame.decode_texture(portal.read(tid), palettes)
                if dec is None: tex_written[tid] = None
                else:
                    w, h, px = dec
                    im = Image.frombytes("RGBA", (w, h), px)
                    # opaque wall/roof textures (no alpha) → indexed PNG, ~1/4 the bytes
                    if im.getchannel("A").getextrema()[0] == 255:
                        im = im.convert("RGB").quantize(colors=256, method=Image.MAXCOVERAGE)
                    im.save(os.path.join(TEXOUT, fn), optimize=True)
                    tex_written[tid] = fn
            else: tex_written[tid] = fn
        out = dict(tex=tex_written[tid]) if tex_written[tid] else dict(color=0x8a8478)
        if s.get("clip") and "tex" in out: out["clip"] = 1
        return out

    # gather every distinct DID the towns actually place
    towns = json.load(open(os.path.join(ROOT, "assets", "actowns.json")))["towns"]
    dids = {}
    for objs in towns.values():
        for o in objs:
            if o.get("kind") in KINDS and o.get("did"):
                dids[o["did"].upper()] = True

    index = {}; done = fails = 0
    for didhex in sorted(dids):
        sid = int(didhex, 16)
        path = os.path.join(OUT, "%08X.json" % sid)
        try:
            if (sid >> 24) == 0x01:
                parts = [dict(gid=sid, p=(0, 0, 0), q=(0, 0, 0, 1))]
            else:
                su = ame.parse_setup(portal.read(sid)); frames = su.get("frames") or []
                parts = []
                for i, gid in enumerate(su["parts"]):
                    fr = frames[i] if i < len(frames) else None
                    p = ame.tx_pos(fr["p"]) if fr else (0, 0, 0)
                    q = ame.tx_quat(fr["q"]) if fr else (0, 0, 0, 1)
                    parts.append(dict(gid=gid, p=p, q=q))
            gout = []; tris = 0
            bb = [1e9, 1e9, 1e9, -1e9, -1e9, -1e9]
            for prt in parts:
                gfx = ame.parse_gfxobj(portal.read(prt["gid"]))
                groups = ame.build_part(gfx)
                for surfidx, g in sorted(groups.items()):
                    if len(g["idx"]) < 3: continue
                    v2 = []; n2 = []
                    for k in range(0, len(g["verts"]), 3):
                        x, y, z = g["verts"][k:k+3]
                        rx, ry, rz = qrot(prt["q"], (x, y, z))
                        wx, wy, wz = rx+prt["p"][0], ry+prt["p"][1], rz+prt["p"][2]
                        v2 += [round(wx, 4), round(wy, 4), round(wz, 4)]
                        bb[0] = min(bb[0], wx); bb[1] = min(bb[1], wy); bb[2] = min(bb[2], wz)
                        bb[3] = max(bb[3], wx); bb[4] = max(bb[4], wy); bb[5] = max(bb[5], wz)
                        nx, ny, nz = g["norms"][k:k+3]
                        rn = qrot(prt["q"], (nx, ny, nz))
                        n2 += [round(rn[0], 4), round(rn[1], 4), round(rn[2], 4)]
                    ssid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                    gout.append(dict(mat=(surface_material(ssid) if ssid else dict(color=0x8a8478)),
                                     v=v2, n=n2, uv=g["uvs"], i=g["idx"]))
                    tris += len(g["idx"]) // 3
            if not gout:
                fails += 1; continue
            bb = [round(b, 3) for b in bb]
            json.dump(dict(groups=gout, bb=bb), open(path, "w"), separators=(",", ":"))
            index[didhex] = dict(file="%08X.json" % sid, tris=tris, bb=bb)
            done += 1
        except Exception:
            fails += 1
    json.dump(dict(meta=dict(source="client_portal.dat via actowns.json placements",
                             kinds=list(KINDS)), models=index),
              open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    total = 0
    for r, _, fs in os.walk(OUT):
        for f in fs: total += os.path.getsize(os.path.join(r, f))
    ntex = sum(1 for v in tex_written.values() if v)
    print(f"town models: {done} baked (failed {fails}) of {len(dids)} unique DIDs")
    print(f"textures: {ntex} decoded ({sum(1 for v in tex_written.values() if not v)} failed)")
    print(f"pack: {total//1024//1024} MB ({total//1024} KB)")

if __name__ == "__main__":
    main()
