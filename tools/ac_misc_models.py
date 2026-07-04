#!/usr/bin/env python3
"""Bake a few named non-building AC fixtures (lifestone, bindstone) from client_portal.dat.

Same {groups:[{mat,v,n,uv,i}],bb} schema + DXT/clip-aware textures as ac_town_models. These
are world fixtures the game places itself (addLifestone etc.), so they get their own small pack
rather than going through the town/scenery streamers.

Output: assets/acmisc/<name>.json  + assets/acmisc/tex/<id>.png  + index.json
"""
import os, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
OUT = os.path.join(ROOT, "assets", "acmisc"); TEXOUT = os.path.join(OUT, "tex")

# name -> Setup/GfxObj DID  (from assets/actowns.json lifestone/bindstone weenie Setup DIDs)
FIXTURES = {"lifestone": 0x020002EE, "bindstone": 0x02000EAD}

def qrot(q, v):
    qv = (q[0], q[1], q[2]); t = [2*(qv[1]*v[2]-qv[2]*v[1]), 2*(qv[2]*v[0]-qv[0]*v[2]), 2*(qv[0]*v[1]-qv[1]*v[0])]
    return (v[0]+q[3]*t[0]+qv[1]*t[2]-qv[2]*t[1], v[1]+q[3]*t[1]+qv[2]*t[0]-qv[0]*t[2], v[2]+q[3]*t[2]+qv[0]*t[1]-qv[1]*t[0])

def main():
    os.makedirs(TEXOUT, exist_ok=True)
    from PIL import Image
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]
    tex_written = {}
    def surface_material(sid):
        s = ame.parse_surface(portal.read(sid))
        if "color" in s: return dict(color=s["color"] & 0xFFFFFF)
        clip = bool(s.get("clip"))
        tid = ame.parse_surfacetexture(portal.read(s["tex"]))[-1]
        if tid not in tex_written:
            fn = "%08X.png" % tid
            if not os.path.isfile(os.path.join(TEXOUT, fn)):
                dec = ame.decode_texture(portal.read(tid), palettes, clip=clip)
                if dec is None: tex_written[tid] = None
                else:
                    w, h, px = dec; im = Image.frombytes("RGBA", (w, h), px)
                    if im.getchannel("A").getextrema()[0] == 255:
                        im = im.convert("RGB").quantize(colors=256, method=Image.MAXCOVERAGE)
                    im.save(os.path.join(TEXOUT, fn), optimize=True); tex_written[tid] = fn
            else: tex_written[tid] = fn
        out = dict(tex=tex_written[tid]) if tex_written[tid] else dict(color=0x8a8478)
        if s.get("clip") and "tex" in out: out["clip"] = 1
        return out

    index = {}
    for name, sid in FIXTURES.items():
        su = ame.parse_setup(portal.read(sid)); frames = su.get("frames") or []
        parts = []
        for i, gid in enumerate(su["parts"]):
            fr = frames[i] if i < len(frames) else None
            parts.append(dict(gid=gid, p=ame.tx_pos(fr["p"]) if fr else (0, 0, 0),
                              q=ame.tx_quat(fr["q"]) if fr else (0, 0, 0, 1)))
        gout = []; bb = [1e9, 1e9, 1e9, -1e9, -1e9, -1e9]; tris = 0
        for prt in parts:
            gfx = ame.parse_gfxobj(portal.read(prt["gid"])); groups = ame.build_part(gfx)
            for surfidx, g in sorted(groups.items()):
                if len(g["idx"]) < 3: continue
                v2 = []; n2 = []
                for k in range(0, len(g["verts"]), 3):
                    x, y, z = g["verts"][k:k+3]; rx, ry, rz = qrot(prt["q"], (x, y, z))
                    wx, wy, wz = rx+prt["p"][0], ry+prt["p"][1], rz+prt["p"][2]
                    v2 += [round(wx, 4), round(wy, 4), round(wz, 4)]
                    bb[0]=min(bb[0],wx);bb[1]=min(bb[1],wy);bb[2]=min(bb[2],wz)
                    bb[3]=max(bb[3],wx);bb[4]=max(bb[4],wy);bb[5]=max(bb[5],wz)
                    nx, ny, nz = g["norms"][k:k+3]; rn = qrot(prt["q"], (nx, ny, nz))
                    n2 += [round(rn[0], 4), round(rn[1], 4), round(rn[2], 4)]
                ssid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                gout.append(dict(mat=(surface_material(ssid) if ssid else dict(color=0x8a8478)),
                                 v=v2, n=n2, uv=g["uvs"], i=g["idx"])); tris += len(g["idx"])//3
        bb = [round(b, 3) for b in bb]
        json.dump(dict(groups=gout, bb=bb), open(os.path.join(OUT, name + ".json"), "w"), separators=(",", ":"))
        index[name] = dict(did="0x%08X" % sid, tris=tris, bb=bb, texGroups=sum(1 for g in gout if "tex" in g["mat"]))
        print(f"{name}: {tris} tris, bb {bb}, {index[name]['texGroups']} textured groups")
    json.dump(index, open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    ntex = sum(1 for v in tex_written.values() if v)
    print(f"textures: {ntex} decoded")

if __name__ == "__main__":
    main()
