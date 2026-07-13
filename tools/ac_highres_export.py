#!/usr/bin/env python3
"""Upgrade every game texture from client_highres.dat (the high-detail pack).

The highres dat carries larger versions of portal.dat textures under the SAME texture IDs.
This sweeps every texture PNG the game's packs use — acflora/tex, acmodels/tex,
acdungeons/tex, actownmodels/tex, acui, acicons — and where the highres dat holds the same
TID at LARGER dimensions, re-decodes and overwrites the PNG in place. Zero engine changes:
every pack references the same filenames.

Usage: python tools/ac_highres_export.py [--dry]
"""
import os, sys, json, re, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)

TEX_DIRS = ["assets/acflora/tex", "assets/acmodels/tex", "assets/acdungeons/tex",
            "assets/actownmodels/tex", "assets/acui", "assets/acicons", "assets/acheads/tex",
            "assets/acbuild/tex",
            # dat-riches-era packs (statics/interiors share acdungeons/tex; these two are their own pools)
            "assets/acdstatics/tex", "assets/acterrain/tex"]
HEXNAME = re.compile(r"^([0-9a-fA-F]{8})(?:_.*)?\.png$")


def clip_flagged():
    """Every texture FILE referenced with clip=1 (colour-key transparency) anywhere in the
    mesh packs. Re-decoding one of these WITHOUT clip=True writes a fully-opaque alpha
    channel — in-game the grate/bars/web renders as a solid slab (the 2026-07-13 dungeon
    regression). Returns {(texdir_rel, filename)}."""
    import glob
    out = set()
    def scan(g, texdir):
        m = g.get("mat", g)
        if m.get("clip") and m.get("tex"): out.add((texdir, m["tex"]))
    for f in glob.glob(os.path.join(ROOT, "assets", "acdstatics", "S*.json")):
        for g in json.load(open(f))["groups"]: scan(g, "assets/acdstatics/tex")
    for f in glob.glob(os.path.join(ROOT, "assets", "acdungeons", "*.json")):
        if f.endswith("index.json"): continue
        for g in json.load(open(f)).get("groups", []): scan(g, "assets/acdungeons/tex")
    for f in glob.glob(os.path.join(ROOT, "assets", "acinteriors", "I*.json")):
        for g in json.load(open(f))["groups"]: scan(g, "assets/acdungeons/tex")
    for sub in ("acmodels", "acmisc", "acflora"):
        for f in glob.glob(os.path.join(ROOT, "assets", sub, "*.json")):
            try: d = json.load(open(f))
            except Exception: continue
            for g in (d.get("groups") or []): scan(g, f"assets/{sub}/tex")
    return out


def main():
    dry = "--dry" in sys.argv
    hr = ame.DatReader(os.path.join(ROOT, "acdata", "client_highres.dat"))
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    hrset = set(hr.files)
    # The client's own mapping: each SurfaceTexture (0x05) lists [highres_tid, lowres_tid];
    # every exporter resolved the LAST (lowres). Build lowres -> highres.
    lo2hi = {}
    for did in portal.files:
        if did >> 24 != 0x05: continue
        try: ids = ame.parse_surfacetexture(portal.read(did))
        except Exception: continue
        if len(ids) >= 2 and ids[0] in hrset and ids[-1] not in hrset:
            lo2hi[ids[-1]] = ids[0]
    print(f"lowres->highres pairs: {len(lo2hi)}")
    from PIL import Image
    pal_cache = {}
    def palettes(pid):   # palettes may live in either dat
        if pid not in pal_cache:
            src = hr if pid in hrset else portal
            pal_cache[pid] = ame.parse_palette(src.read(pid))
        return pal_cache[pid]

    # acterrain names its PNGs t<NN>_<name>.png; its index.json carries each type's
    # SurfaceTexture gid — build filename -> lowres TID for that pack
    terr_by_name = {}
    try:
        tidx = json.load(open(os.path.join(ROOT, "assets", "acterrain", "index.json")))
        for t in tidx.get("types", {}).values():
            ids = ame.parse_surfacetexture(portal.read(t["gid"]))
            terr_by_name[t["tex"]] = ids[-1]
    except Exception:
        pass

    clips = clip_flagged()
    print(f"clip-flagged texture files: {len(clips)}")

    total_up, per_dir, reclipped = 0, {}, 0
    for rel in TEX_DIRS:
        d = os.path.join(ROOT, rel)
        if not os.path.isdir(d): continue
        ups = 0
        for fn in os.listdir(d):
            m = HEXNAME.match(fn)
            if not m and rel.endswith("acterrain/tex") and fn in terr_by_name:
                tid = terr_by_name[fn]
            elif not m:
                continue
            else:
                tid = int(m.group(1), 16)
            hi = None
            if (tid >> 24) == 0x05:   # named by SURFACE-TEXTURE id (the head/face pack) — resolve directly
                try:
                    ids = ame.parse_surfacetexture(portal.read(tid))
                    if ids and ids[0] in hrset and (len(ids) < 2 or ids[-1] not in hrset): hi = ids[0]
                except Exception:
                    hi = None
            else:
                hi = lo2hi.get(tid)
            if not hi: continue
            is_clip = (rel, fn) in clips
            try:
                dec = ame.decode_texture(hr.read(hi), palettes, clip=is_clip)
            except Exception:
                continue
            if dec is None: continue
            dw, dh, px = dec
            try:
                cur = Image.open(os.path.join(d, fn))
                cw, ch = cur.size
                # repair pass: a clip texture whose on-disk alpha is fully opaque was written
                # by the clip-less run — rewrite it even though the size already matches
                needs_repair = is_clip and cur.convert("RGBA").getchannel("A").getextrema()[0] == 255
            except Exception:
                cw = ch = 0; needs_repair = False
            if dw * dh <= cw * ch and not needs_repair: continue     # not bigger and not damaged
            if not dry:
                Image.frombytes("RGBA", (dw, dh), px).save(os.path.join(d, fn))
            ups += 1
            if needs_repair: reclipped += 1
        if ups: per_dir[rel] = ups
        total_up += ups
    for rel, n in per_dir.items(): print(f"  {rel}: {n} upgraded")
    print(f"{'DRY RUN — ' if dry else ''}upgraded {total_up} textures to high-res "
          f"({reclipped} clip-alpha repairs — same filenames, zero engine changes)")


if __name__ == "__main__":
    main()
