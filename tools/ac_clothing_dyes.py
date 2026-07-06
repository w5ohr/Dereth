#!/usr/bin/env python3
"""Bake dye RGB-remaps for BASE CLOTHING (the ClothingSubPalEffects in acclothing.json).

Each garment's dye entry is a list of CloSubPalettes {palset, ranges}: the retail client picked a
palette from the PaletteSet (by shade) and overlaid it onto the master palette 0x0400007E across
`ranges`. Since every fabric texture indexes that master palette (same fact the head-colour system
relies on — verified pixel-exact there), a dye colour reduces to a compact {oldRGBhex: newRGBhex}
remap the engine can apply to the garment's baked textures with a cached canvas pass.

Output: assets/acclothing_mesh/dyes.json
  { "<base-hex>": { "<dyeIdx>": {oldhex: newhex, …}, … }, … }
"""
import os, json, struct, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)

def main():
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    base = ame.parse_palette(portal.read(0x0400007E))          # the master palette all fabric indexes

    def parse_palset(pid):                                     # u32 id, u32 n, n × u32 palette DIDs
        d = portal.read(pid)
        n = struct.unpack_from("<I", d, 4)[0]
        return [struct.unpack_from("<I", d, 8 + 4*i)[0] for i in range(n)]

    pal_cache = {}
    def palette(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]

    clo = json.load(open(os.path.join(ROOT, "assets", "acclothing.json")))
    # only the bases the engine's baked wardrobe actually renders (assets/acclothing_mesh/index.json)
    idx = json.load(open(os.path.join(ROOT, "assets", "acclothing_mesh", "index.json")))
    wanted = set(k.lower() for k in (idx.get("bases") or {}).keys())
    out = {}; garments = 0; colours = 0
    for basehex, e in clo["clothing"].items():
        if basehex.lower() not in wanted: continue
        dyes = e.get("dye")
        if not dyes or not (e.get("m") or e.get("f")): continue
        ent = {}
        for i, d in enumerate(dyes):
            remap = {}
            for sub in d.get("subs", []):
                try: pals = parse_palset(int(sub["palset"], 16))
                except Exception: continue
                if not pals: continue
                pal = palette(pals[len(pals)//2])              # mid-shade palette of the set
                for off, num in sub.get("ranges", []):
                    for k in range(off, min(off+num, len(base), len(pal))):
                        b, n = base[k] & 0xFFFFFF, pal[k] & 0xFFFFFF
                        if b != n: remap["%06x" % b] = "%06x" % n
            if remap: ent[str(i)] = remap
        if ent: out[basehex] = ent; garments += 1; colours += len(ent)
    path = os.path.join(ROOT, "assets", "acclothing_mesh", "dyes.json")
    json.dump(out, open(path, "w"), separators=(",", ":"))
    print(f"dyes: {garments} garments · {colours} colour remaps · {os.path.getsize(path)//1024} KB → {path}")

if __name__ == "__main__":
    main()
