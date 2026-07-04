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
            "assets/acbuild/tex"]
HEXNAME = re.compile(r"^([0-9a-fA-F]{8})(?:_.*)?\.png$")


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

    total_up, per_dir = 0, {}
    for rel in TEX_DIRS:
        d = os.path.join(ROOT, rel)
        if not os.path.isdir(d): continue
        ups = 0
        for fn in os.listdir(d):
            m = HEXNAME.match(fn)
            if not m: continue
            tid = int(m.group(1), 16)
            hi = lo2hi.get(tid)
            if not hi: continue
            try:
                dec = ame.decode_texture(hr.read(hi), palettes)
            except Exception:
                continue
            if dec is None: continue
            dw, dh, px = dec
            try: cw, ch = Image.open(os.path.join(d, fn)).size
            except Exception: cw = ch = 0
            if dw * dh <= cw * ch: continue          # not actually bigger
            if not dry:
                Image.frombytes("RGBA", (dw, dh), px).save(os.path.join(d, fn))
            ups += 1
        if ups: per_dir[rel] = ups
        total_up += ups
    for rel, n in per_dir.items(): print(f"  {rel}: {n} upgraded")
    print(f"{'DRY RUN — ' if dry else ''}upgraded {total_up} textures to high-res (same filenames — zero engine changes)")


if __name__ == "__main__":
    main()
