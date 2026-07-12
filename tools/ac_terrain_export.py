#!/usr/bin/env python3
"""Extract the REAL terrain ground textures from the AC Region file (#693's promised atlas swap).

The Region (0x13000000 in client_portal.dat) ends its TerrainDesc with LandSurf → TexMerge,
which maps every LandDefs terrain type to its ground SurfaceTexture (TMTerrainDesc →
TerrainTex.texGID). ac_region_export.py deliberately stopped before this block; here we parse
straight through it (field order per the ACE DatLoader reference, BSD) and decode each type's
base texture to PNG.

Outputs:
  assets/acterrain/tex/t<NN>_<name>.png   the type's ground texture, full colour, as authored
  assets/acterrain/index.json             {types:{idx:{name,tex,tiling,gid}}, unique GID count}

The client swaps these into the #693 splat atlas at boot when the index is present (tiles are
luminance-neutralised there so the per-vertex biome tint / seasons keep working) — the splat
shader itself is untouched, exactly as promised in #693.
"""
import os, sys, json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ac_model_export import DatReader, parse_surfacetexture, parse_palette, decode_texture
from ac_region_export import (RBuf, read_landdefs, read_gametime, read_skyinfo,
                              read_soundinfo, read_sceneinfo, read_terraininfo)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "acterrain")
REGION_ID = 0x13000000


def read_texmerge(r):
    """LandSurf → TexMerge → the per-terrain-type TerrainTex table (plus the alpha maps we skip)."""
    is_pal_shift = r.u32()
    if is_pal_shift:
        raise SystemExit("PalShift land surfaces (pre-ToD data?) — this exporter expects TexMerge")
    base_tex_size = r.u32()
    corner = [(r.u32(), r.u32()) for _ in range(r.i32())]        # TerrainAlphaMap {tcode, texGID}
    side = [(r.u32(), r.u32()) for _ in range(r.i32())]
    road = [(r.u32(), r.u32()) for _ in range(r.i32())]          # RoadAlphaMap {rcode, roadTexGID}
    terr = []
    for _ in range(r.i32()):                                     # TMTerrainDesc — 11 words, verified
        ttype = r.u32()                                          # against the live dat: type, gid,
        tex_gid = r.u32()                                        # tiling, SIX shading params, then a
        tex_tiling = r.u32()                                     # trailing {code, detailGID} pair the
        shade = [r.u32() for _ in range(6)]                      # short reference docs omit
        trail_code, trail_gid = r.u32(), r.u32()
        terr.append(dict(type=ttype, gid=tex_gid, tiling=tex_tiling, shade=shade,
                         detail=dict(code=trail_code, gid=trail_gid)))
    return dict(baseTexSize=base_tex_size, corner=corner, side=side, road=road, terr=terr)


def save_png(path, w, h, rgba):
    import struct, zlib
    raw = b"".join(b"\x00" + bytes(rgba[y*w*4:(y+1)*w*4]) for y in range(h))
    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    portal = DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    r = RBuf(portal.read(REGION_ID))
    # walk the header exactly as ac_region_export does
    r.u32(); r.u32(); r.u32()               # Id, RegionNumber, Version
    name = r.pstr(); r.align()
    read_landdefs(r); read_gametime(r)
    parts = r.u32()
    if parts & 0x10: read_skyinfo(r)
    if parts & 0x01: read_soundinfo(r)
    if parts & 0x02: read_sceneinfo(r)
    terrains = read_terraininfo(r)
    tm = read_texmerge(r)
    print(f"region '{name}': {len(terrains)} terrain types, {len(tm['terr'])} TerrainTex entries, "
          f"{len(tm['corner'])} corner + {len(tm['side'])} side alpha maps, {len(tm['road'])} road maps")

    os.makedirs(os.path.join(OUT, "tex"), exist_ok=True)
    palettes = lambda pid: parse_palette(portal.read(pid))
    types, done_gid = {}, {}
    for e in tm["terr"]:
        ti = e["type"]
        tname = terrains[ti]["name"] if ti < len(terrains) else f"type{ti}"
        if e["gid"] not in done_gid:
            texids = parse_surfacetexture(portal.read(e["gid"]))     # 0x05 SurfaceTexture → 0x06 list
            w, h, rgba = decode_texture(portal.read(texids[-1]), palettes)
            fn = f"t{ti:02d}_{tname.replace(' ', '_')}.png"
            save_png(os.path.join(OUT, "tex", fn), w, h, rgba)
            done_gid[e["gid"]] = fn
            print(f"  type {ti:2d} {tname:22s} gid 0x{e['gid']:08X} → {fn} ({w}×{h}, tiling {e['tiling']})")
        types[str(ti)] = dict(name=tname, tex=done_gid[e["gid"]], tiling=e["tiling"], gid=e["gid"])
    json.dump(dict(region=name, types=types, unique=len(done_gid)),
              open(os.path.join(OUT, "index.json"), "w"), indent=1)
    print(f"wrote {len(done_gid)} unique ground textures for {len(types)} terrain types → assets/acterrain/")


if __name__ == "__main__":
    main()
