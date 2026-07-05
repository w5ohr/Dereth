#!/usr/bin/env python3
"""Bake ClothingTable base outfits into ready-to-render body-part meshes.

The equipped-ARMOR look already works (ac_armor_export.py → assets/acarmor): a retail
piece clones the swapped GfxObj onto the body part. Base CLOTHING (the loose shirt /
trousers / robe / shoes NPCs and the player wear by default) needs the SAME treatment,
but a plain shirt barely changes the mesh silhouette — its whole look is the TEXTURE
SWAP (the bare-torso skin texture is replaced by shirt fabric). The armor meshes bake in
only a gfxobj's DEFAULT texture and don't record the original SurfaceTexture per group,
so the swap can't be reapplied at runtime.

This tool resolves each garment's CloTextureEffect at export time: for every part it
builds the swapped GfxObj, and per surface group looks up the group's original
SurfaceTexture in the garment's oldTex→newTex list, decoding the NEW texture when a swap
matches (else the gfxobj's own texture). The result is a per-garment, per-gender mesh
pack the engine clones onto the AC body exactly like armor — authentic ClothingTable
dressing, no procedural cloth.

Output:
  assets/acclothing_mesh/index.json
    { partJoint:{partIdx:jointKey}, byName:{name:baseHex},
      bases:{ baseHex:{name, m:bool, f:bool} } }
  assets/acclothing_mesh/<baseHex>_<m|f>.json
    { parts:[ {p:partIdx, joint:jointKey, groups:[{mat,v,n,uv,i}]} ] }
  swap/original textures shared into assets/acmodels/tex/<RenderSurfaceHex>.png
"""
import os, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
OUT = os.path.join(ROOT, "assets", "acclothing_mesh")
TEXDIR = os.path.join(ROOT, "assets", "acmodels", "tex")

SETUPS = {"m": 0x02000001, "f": 0x0200004E}   # human male / female — the wearer body the table keys on

def joint_for(p):        # mirror of ac_armor_export / the engine's acJointFor classification
    ax, ay = p[0], p[1]
    if abs(ax) >= 0.17: return ("sh" if ay > 1.3 else "el" if ay > 1.02 else "wr") + ("R" if ax < 0 else "L")
    if ay > 1.5: return None
    if ay > 1.02: return "torso"
    if ay > 0.84: return ("hip" + ("R" if ax < 0 else "L")) if abs(ax) >= 0.06 else "pelvisG"
    return ("hip" if ay > 0.55 else "kn" if ay > 0.2 else "an") + ("R" if ax < 0 else "L")

# The base wardrobe every human NPC / the player draws from. Names resolve via byName.
# Keep this list to genuine BASE clothing — armor uses acarmor, legendaries aren't default wear.
WARDROBE = [
    "shirt", "trousers", "shoes", "breeches", "pantaloons", "tunic", "sandals",
    "cowl", "cap", "qafiya", "turban", "fez", "kasa",
    "faran robe", "suikan robe", "gelidite robe", "sleek dress", "sasalia's dress",
    "empyrean robe", "kiyafa robe", "envoy's robe",
]

def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(TEXDIR, exist_ok=True)
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    from PIL import Image

    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]

    tex_written = {}                                        # SurfaceTexture DID -> png filename | None
    def resolve_surfacetexture(stid):
        """SurfaceTexture DID -> decoded RenderSurface png filename (shared, deduped by RS id)."""
        if stid in tex_written: return tex_written[stid]
        try:
            tid = ame.parse_surfacetexture(portal.read(stid))[-1]   # highest-res RenderSurface
            fn = "%08X.png" % tid
            if not os.path.isfile(os.path.join(TEXDIR, fn)):
                dec = ame.decode_texture(portal.read(tid), palettes)
                if dec is None:
                    tex_written[stid] = None; return None
                w, h, px = dec
                Image.frombytes("RGBA", (w, h), px).save(os.path.join(TEXDIR, fn))
            tex_written[stid] = fn
        except Exception:
            tex_written[stid] = None
        return tex_written[stid]

    def group_material(surf_did, swaps):
        """gfxobj surface DID + the garment's [oldStHex,newStHex] list -> resolved material."""
        s = ame.parse_surface(portal.read(surf_did))
        if "color" in s: return dict(color=s["color"] & 0xFFFFFF)
        st = s["tex"]                                       # this group's ORIGINAL SurfaceTexture DID
        for old, new in swaps:                              # apply the CloTextureEffect if it matches
            if int(old, 16) == st: st = int(new, 16); break
        fn = resolve_surfacetexture(st)
        if not fn: return dict(color=0x9a8f7a)
        out = dict(tex=fn)
        if s.get("clip"): out["clip"] = 1
        return out

    # part index -> joint from each gender's placement frame
    part_joint = {}
    for g, sid in SETUPS.items():
        setup = ame.parse_setup(portal.read(sid))
        for i, fr in enumerate(setup["frames"] or []):
            j = joint_for(ame.tx_pos(fr["p"]))
            if j is not None: part_joint[i] = j

    clo = json.load(open(os.path.join(ROOT, "assets", "acclothing.json")))
    C, BN = clo["clothing"], clo["byName"]
    index_bases, index_names = {}, {}

    def bake(base, gkey, effects):
        parts = []
        for pe in effects:                                  # each = {part, gfxobj, tex:[[old,new],...]}
            pidx, gid = pe["part"], pe["gfxobj"]
            joint = part_joint.get(pidx)
            if joint is None: continue                      # part 16 = head, skipped (AC head system owns it)
            swaps = pe.get("tex", [])
            gfx = ame.parse_gfxobj(portal.read(int(gid, 16)))
            groups = ame.build_part(gfx)
            gout = []
            for surfidx, grp in sorted(groups.items()):
                if len(grp["idx"]) < 3: continue
                surf_did = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                mat = group_material(surf_did, swaps) if surf_did else dict(color=0x9a8f7a)
                gout.append(dict(mat=mat, v=grp["verts"], n=grp["norms"], uv=grp["uvs"], i=grp["idx"]))
            if gout: parts.append(dict(p=pidx, joint=joint, groups=gout))
        if parts:
            json.dump(dict(parts=parts), open(os.path.join(OUT, "%s_%s.json" % (base, gkey)), "w"),
                      separators=(",", ":"))
        return bool(parts)

    for name in WARDROBE:
        base = BN.get(name)
        if not base:
            print("  (skip, not in byName):", name); continue
        e = C.get(base)
        if not e: continue
        rec = dict(name=name)
        for gkey in ("m", "f"):
            rec[gkey] = bake(base, gkey, e.get(gkey, []))
        index_bases[base] = rec
        index_names[name] = base
        print(f"  {name:20} {base}  m={rec['m']} f={rec['f']}")

    json.dump(dict(partJoint=part_joint, byName=index_names, bases=index_bases),
              open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print(f"\nwardrobe baked: {len(index_bases)} bases · pack {total//1024} KB · tex {len(tex_written)} resolved")

if __name__ == "__main__":
    main()
