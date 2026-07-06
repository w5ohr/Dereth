#!/usr/bin/env python3
"""Bake HAT head-meshes (ClothingTable part-16 swaps) the body exporter skipped.

A hat's ClothingBase swaps setup part 16 — the HEAD — for a hat gfxobj (usually hat + a plain
head form beneath, retail's "bald under the hat"), with the fabric texture swaps resolved like
any garment. We bake each hat per gender in head-local coords; the engine attaches it exactly
like acBuildHead attaches the real head (placement from u.acHeadPlace + the 180° head flip),
hiding the bare AC head while worn.

Output: assets/acclothing_mesh/hat_<base>_<m|f>.json  {groups:[{mat,v,n,uv,i}]}
        index update: assets/acclothing_mesh/hats.json {name: {base, m, f}}
"""
import os, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
OUT = os.path.join(ROOT, "assets", "acclothing_mesh")
TEXDIR = os.path.join(ROOT, "assets", "acmodels", "tex")

HATS = ["cowl", "cap", "qafiya", "turban", "fez", "kasa", "beret", "hood", "crown"]

def main():
    os.makedirs(TEXDIR, exist_ok=True)
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    from PIL import Image
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]
    tex_written = {}
    def resolve_st(stid):
        if stid in tex_written: return tex_written[stid]
        try:
            tid = ame.parse_surfacetexture(portal.read(stid))[-1]
            fn = "%08X.png" % tid
            if not os.path.isfile(os.path.join(TEXDIR, fn)):
                dec = ame.decode_texture(portal.read(tid), palettes)
                if dec is None: tex_written[stid] = None; return None
                w, h, px = dec
                Image.frombytes("RGBA", (w, h), px).save(os.path.join(TEXDIR, fn))
            tex_written[stid] = fn
        except Exception:
            tex_written[stid] = None
        return tex_written[stid]
    def material(surf_did, swaps):
        s = ame.parse_surface(portal.read(surf_did))
        if "color" in s: return dict(color=s["color"] & 0xFFFFFF)
        st = s["tex"]
        for old, new in swaps:
            if int(old, 16) == st: st = int(new, 16); break
        fn = resolve_st(st)
        if not fn: return dict(color=0x8a7a5a)
        out = dict(tex=fn)
        if s.get("clip"): out["clip"] = 1
        return out

    clo = json.load(open(os.path.join(ROOT, "assets", "acclothing.json")))
    C, BN = clo["clothing"], clo["byName"]
    index = {}
    for name in HATS:
        base = BN.get(name)
        if not base: print("  (skip, unnamed):", name); continue
        e = C.get(base)
        if not e: continue
        rec = dict(base=base)
        for gk in ("m", "f"):
            p16 = [pe for pe in (e.get(gk) or []) if pe["part"] == 16]
            if not p16: rec[gk] = False; continue
            pe = p16[0]
            gfx = ame.parse_gfxobj(portal.read(int(pe["gfxobj"], 16)))
            groups = ame.build_part(gfx)
            gout = []
            for surfidx, g in sorted(groups.items()):
                if len(g["idx"]) < 3: continue
                sd = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                gout.append(dict(mat=(material(sd, pe.get("tex", [])) if sd else dict(color=0x8a7a5a)),
                                 v=g["verts"], n=g["norms"], uv=g["uvs"], i=g["idx"]))
            rec[gk] = bool(gout)
            if gout:
                json.dump(dict(groups=gout),
                          open(os.path.join(OUT, "hat_%s_%s.json" % (base, gk)), "w"), separators=(",", ":"))
        index[name] = rec
        print(f"  {name:8} {base}  m={rec.get('m')} f={rec.get('f')}")
    json.dump(index, open(os.path.join(OUT, "hats.json"), "w"), separators=(",", ":"))
    print(f"hats: {len(index)} baked → {OUT}/hats.json")

if __name__ == "__main__":
    main()
