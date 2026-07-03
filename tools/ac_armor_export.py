#!/usr/bin/env python3
"""Export the armor-on-body meshes referenced by the ClothingTable.

assets/acclothing.json maps item names -> per-setup body-part swaps (part index ->
GfxObj). This tool exports every unique armor GfxObj as a mesh JSON (same group format
as the avatar parts, textures shared into assets/acmodels/tex) and writes an index:

  assets/acarmor/index.json
    { "partJoint": { partIndex: jointKey },      # human setup part -> avatar joint
      "items":     { itemNameLower: [{p, g}] } } # equip name -> mesh swaps
  assets/acarmor/<gfxhex>.json                   # {groups:[{mat,v,n,uv,i}]}

The game replaces the AC body part under the matching joint with the armor mesh when
a retail piece is equipped — armor visibly changes the body exactly as it did.
"""
import os, re, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
OUT = os.path.join(ROOT, "assets", "acarmor")

def joint_for(p):        # mirror of the engine's acJointFor classification
    ax, ay = p[0], p[1]
    if abs(ax) >= 0.17: return ("sh" if ay > 1.3 else "el" if ay > 1.02 else "wr") + ("R" if ax < 0 else "L")
    if ay > 1.5: return None
    if ay > 1.02: return "torso"
    if ay > 0.84: return ("hip" + ("R" if ax < 0 else "L")) if abs(ax) >= 0.06 else "pelvisG"
    return ("hip" if ay > 0.55 else "kn" if ay > 0.2 else "an") + ("R" if ax < 0 else "L")

def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(os.path.join(ROOT, "assets", "acmodels", "tex"), exist_ok=True)
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    from PIL import Image
    import io
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]
    texdir = os.path.join(ROOT, "assets", "acmodels", "tex")
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
        out = dict(tex=tex_written[tid]) if tex_written[tid] else dict(color=0x888888)
        if s.get("clip") and "tex" in out: out["clip"] = 1
        return out

    # part index -> joint from the male setup's placement frame
    setup = ame.parse_setup(portal.read(0x02000001))
    part_joint = {}
    for i, fr in enumerate(setup["frames"] or []):
        j = joint_for(ame.tx_pos(fr["p"]))
        if j: part_joint[i] = j
    print("partJoint:", {k: part_joint[k] for k in sorted(part_joint) if k < 17})

    clo = json.load(open(os.path.join(ROOT, "assets", "acclothing.json")))
    items = {}
    gfx_needed = set()
    for name, cid in clo["byName"].items():
        e = clo["clothing"].get(str(cid)) or clo["clothing"].get(cid)
        if not e: continue
        swaps = []
        for p in e.get("parts", []):
            if p.get("setup") != "02000001": continue
            for s in p.get("swaps", []):
                if "gfxobj" in s and s.get("part") in part_joint:
                    swaps.append(dict(p=s["part"], g=s["gfxobj"]))
                    gfx_needed.add(s["gfxobj"])
        if swaps: items[name.lower()] = swaps
    print(f"items with body swaps: {len(items):,} · unique meshes: {len(gfx_needed):,}")

    done = fails = 0
    for gid in sorted(gfx_needed):
        path = os.path.join(OUT, gid + ".json")
        if os.path.isfile(path): done += 1; continue
        try:
            gfx = ame.parse_gfxobj(portal.read(int(gid, 16)))
            groups = ame.build_part(gfx)
            gout = []
            for surfidx, g in sorted(groups.items()):
                if len(g["idx"]) < 3: continue
                sid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                gout.append(dict(mat=(surface_material(sid) if sid else dict(color=0x888888)),
                                 v=g["verts"], n=g["norms"], uv=g["uvs"], i=g["idx"]))
            json.dump(dict(groups=gout), open(path, "w"), separators=(",", ":"))
            done += 1
        except Exception:
            fails += 1
    json.dump(dict(partJoint=part_joint, items=items),
              open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print(f"meshes exported: {done} (failed {fails}) · pack {total//1024//1024} MB")

if __name__ == "__main__":
    main()
