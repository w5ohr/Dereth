#!/usr/bin/env python3
"""Export the REAL AC scenery models (trees, plants, rocks) referenced by acscenery.json.

assets/acscenery.json (portal.dat Region 0x13) lists, per terrain type, the scenery
objects the retail client scattered — each a Setup DID with freq/minScale/maxScale.
This tool exports every unique Setup as ONE baked mesh JSON (all its parts' placement
frames applied to the vertices — scenery is static), textures shared:

  assets/acflora/<setuphex>.json   {groups:[{mat:{tex|color},v,n,uv,i}]}
  assets/acflora/tex/<tid>.png
  assets/acflora/index.json        {"setups": {hex: file}}

The engine scatters these per terrain type at the client's own frequencies — the
trees of Dereth, exactly as retail grew them.
"""
import os, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
OUT = os.path.join(ROOT, "assets", "acflora")


def qrot(q, v):  # rotate vector v by quaternion q=(x,y,z,w)
    x, y, z, w = q
    vx, vy, vz = v
    tx = 2 * (y * vz - z * vy); ty = 2 * (z * vx - x * vz); tz = 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx))


def main():
    os.makedirs(OUT, exist_ok=True)
    texdir = os.path.join(OUT, "tex"); os.makedirs(texdir, exist_ok=True)
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
            if not os.path.isfile(os.path.join(texdir, fn)):
                dec = ame.decode_texture(portal.read(tid), palettes)
                if dec is None: tex_written[tid] = None
                else:
                    w, h, px = dec
                    Image.frombytes("RGBA", (w, h), px).save(os.path.join(texdir, fn))
                    tex_written[tid] = fn
            else: tex_written[tid] = fn
        out = dict(tex=tex_written[tid]) if tex_written[tid] else dict(color=0x6a7a4a)
        if s.get("clip") and "tex" in out: out["clip"] = 1
        return out

    scen = json.load(open(os.path.join(ROOT, "assets", "acscenery.json")))
    setups = set()
    for t in scen["terrains"].values():
        for sc in t.get("scenes", []):
            for o in sc.get("objects", []):
                s = o.get("setup")
                if s and s != "00000000": setups.add(s)
    print(f"unique scenery setups: {len(setups)}")

    index, done, fails = {}, 0, 0
    for shex in sorted(setups):
        path = os.path.join(OUT, shex + ".json")
        if os.path.isfile(path): index[shex] = shex + ".json"; done += 1; continue
        try:
            did = int(shex, 16)
            gout = []
            if (did >> 24) == 0x02:                      # a Setup of parts
                su = ame.parse_setup(portal.read(did))
                parts, frames = su["parts"], su["frames"] or []
                jobs = [(parts[i], frames[i] if i < len(frames) else None) for i in range(len(parts))]
            else:                                        # a bare GfxObj (0x01)
                jobs = [(did, None)]
            for gid, fr in jobs:
                gfx = ame.parse_gfxobj(portal.read(gid))
                groups = ame.build_part(gfx)
                p = ame.tx_pos(fr["p"]) if fr else (0, 0, 0)
                q = ame.tx_quat(fr["q"]) if fr and fr.get("q") else None
                for surfidx, g in sorted(groups.items()):
                    if len(g["idx"]) < 3: continue
                    v, n = g["verts"], g["norms"]
                    bv, bn = [], []
                    for k in range(0, len(v), 3):
                        vv = (v[k], v[k+1], v[k+2])
                        if q: vv = qrot(q, vv)
                        bv += [vv[0] + p[0], vv[1] + p[1], vv[2] + p[2]]
                    for k in range(0, len(n), 3):
                        nn = (n[k], n[k+1], n[k+2])
                        if q: nn = qrot(q, nn)
                        bn += [nn[0], nn[1], nn[2]]
                    sid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                    gout.append(dict(mat=(surface_material(sid) if sid else dict(color=0x6a7a4a)),
                                     v=[round(x, 4) for x in bv], n=[round(x, 3) for x in bn],
                                     uv=g["uvs"], i=g["idx"]))
            if gout:
                json.dump(dict(groups=gout), open(path, "w"), separators=(",", ":"))
                index[shex] = shex + ".json"; done += 1
            else: fails += 1
        except Exception as e:
            fails += 1
    json.dump(dict(setups=index), open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT) if f.endswith(".json"))
    ttex = sum(os.path.getsize(os.path.join(texdir, f)) for f in os.listdir(texdir))
    print(f"exported {done} setups (failed {fails}) · mesh pack {total/1e6:.1f} MB · tex {ttex/1e6:.1f} MB")


if __name__ == "__main__":
    main()
