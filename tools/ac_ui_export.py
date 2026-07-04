#!/usr/bin/env python3
"""Scan/export AC user-interface art from client_portal.dat (0x06 textures).

Phase 1 (--scan): enumerate every 0x06 texture, peek headers, dump DECODABLE
candidates that are not item icons (w>=48 or h>=48, or non-square strips) as
thumbnails onto numbered CONTACT SHEETS (PIL) for visual identification.
Phase 2 (--export did,did,...): decode the chosen DIDs at full size into
assets/acui/<didhex>.png + index.json.

Usage:
  python tools/ac_ui_export.py --scan [outdir]
  python tools/ac_ui_export.py --export 06001234,06005678
"""
import importlib.util
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORTAL = os.path.join(ROOT, "acdata", "client_portal.dat")
OUT = os.path.join(ROOT, "assets", "acui")

spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ame)

# reuse the icon exporter's PNG writer
spec2 = importlib.util.spec_from_file_location("aie", os.path.join(ROOT, "tools", "ac_icon_export.py"))
aie = importlib.util.module_from_spec(spec2)
sys.modules["aie"] = aie
# (don't exec main; just lift write_png after a lightweight exec)
spec2.loader.exec_module(aie)
write_png = aie.write_png


def main():
    portal = ame.DatReader(PORTAL)
    pal_cache = {}

    def palettes(pid):
        if pid not in pal_cache:
            pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]

    dids = sorted(d for d in portal.files if 0x06000000 <= d <= 0x06FFFFFF)
    print(f"0x06 textures in portal.dat: {len(dids)}")

    if "--export" in sys.argv:
        want = [int(x, 16) for x in sys.argv[sys.argv.index("--export") + 1].split(",")]
        os.makedirs(OUT, exist_ok=True)
        idx_path = os.path.join(OUT, "index.json")
        index = json.load(open(idx_path)) if os.path.exists(idx_path) else {}
        for did in want:
            dec = ame.decode_texture(portal.read(did), palettes)
            if dec is None:
                print(f"  {did:08x}: undecodable"); continue
            w, h, px = dec
            fn = "%08x.png" % did
            write_png(os.path.join(OUT, fn), w, h, px)
            index["%08x" % did] = {"file": fn, "w": w, "h": h}
            print(f"  {did:08x}: {w}x{h} -> {fn}")
        json.dump(index, open(idx_path, "w"), indent=0, sort_keys=True)
        print(f"index: {len(index)} entries")
        return

    # --scan: dump candidate thumbnails onto contact sheets
    outdir = sys.argv[sys.argv.index("--scan") + 1] if sys.argv.index("--scan") + 1 < len(sys.argv) else os.path.join(ROOT, "acdata", "uiscan")
    os.makedirs(outdir, exist_ok=True)
    from PIL import Image
    have_icons = set()
    icon_idx = os.path.join(ROOT, "assets", "acicons", "index.json")
    if os.path.exists(icon_idx):
        have_icons = {int(k, 16) for k in json.load(open(icon_idx))["icons"]}

    cands = []
    for did in dids:
        if did in have_icons:
            continue
        raw = portal.read(did)
        if len(raw) < 24:
            continue
        w, h = struct.unpack_from("<ii", raw, 8)
        if not (0 < w <= 2048 and 0 < h <= 2048):
            continue
        # UI chrome: sizeable pieces or long strips (bars); skip the 32x32 icon swarm
        if (w >= 48 or h >= 48) or (w >= 4 * h) or (h >= 4 * w):
            cands.append((did, w, h))
    print(f"candidates: {len(cands)}")

    TH, COLS, ROWS = 72, 14, 10
    per = COLS * ROWS
    sheet_n = 0
    for start in range(0, len(cands), per):
        sheet = Image.new("RGBA", (COLS * (TH + 26), ROWS * (TH + 26)), (18, 16, 24, 255))
        from PIL import ImageDraw
        dr = ImageDraw.Draw(sheet)
        for j, (did, w, h) in enumerate(cands[start:start + per]):
            dec = ame.decode_texture(portal.read(did), palettes)
            if dec is None:
                continue
            tw, th, px = dec
            im = Image.frombytes("RGBA", (tw, th), px)
            im.thumbnail((TH, TH))
            cx, cy = (j % COLS) * (TH + 26), (j // COLS) * (TH + 26)
            sheet.paste(im, (cx + (TH - im.width) // 2, cy + (TH - im.height) // 2))
            dr.text((cx + 2, cy + TH + 2), "%08x" % did, fill=(220, 210, 180, 255))
            dr.text((cx + 2, cy + TH + 13), f"{w}x{h}", fill=(150, 140, 120, 255))
        sheet_n += 1
        p = os.path.join(outdir, f"sheet{sheet_n:02d}.png")
        sheet.save(p)
        print(f"  {p}")
    print(f"{sheet_n} contact sheets in {outdir}")


if __name__ == "__main__":
    main()
