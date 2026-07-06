#!/usr/bin/env python3
"""Decode the 101 `0x21` UI-LAYOUT tables from client_local_English.dat.

These were skipped by ac_strings_export.py ("undocumented layout"). Reversed here:

  header:  layoutId(u32) width(u32) height(u32)        -- always 800x600 (retail desktop)
  then interleaved element DECLARATIONS and BODIES, both keyed by an element DID
  (0x10xxxxxx). A BODY carries the element type (u32) and, after a zero run, its
  geometry as five u32s: x, y, w, h, z(depth). Bodies also embed the 0x06xxxxxx
  texture DIDs the element draws with (the window-chrome plates).

The parse is a validated heuristic (geometry must fit the 800x600 desktop with
slack); elements whose geometry can't be validated are kept with null geometry so
nothing is silently dropped. Output:

  assets/aclayouts.json  {"<layoutDid>": {"w":800,"h":600,
      "elements":[{"did":"0x10..","type":n,"x":..,"y":..,"w":..,"h":..,"z":..,
                   "tex":["0x06..",...]}]}}
"""
import os, json, struct, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)


def u32(d, o): return struct.unpack_from('<I', d, o)[0]


def parse_layout(raw):
    lid, W, H = u32(raw, 0), u32(raw, 4), u32(raw, 8)
    # index every occurrence of each element DID; first = declaration, second = body
    occ = {}
    for i in range(12, len(raw) - 3):
        v = u32(raw, i)
        if (v >> 24) == 0x10:
            occ.setdefault(v, []).append(i)
    elements = []
    for did, offs in sorted(occ.items(), key=lambda kv: kv[1][0]):
        body = offs[1] if len(offs) > 1 else offs[0]
        etype = u32(raw, body + 4)
        end = min(len(raw) - 3, body + 96)
        # geometry: the first run of five u32s after the body whose (w,h) fit the desktop
        geom = None
        o = body + 8
        while o + 20 <= end:
            g = [u32(raw, o + k * 4) for k in range(5)]
            if (g[2] and g[3] and g[0] <= W and g[1] <= H
                    and 0 < g[2] <= W and 0 < g[3] <= H and g[4] <= 100000):
                geom = g; break
            o += 4
        # texture plates the element draws with
        tex = []
        o = body + 4
        while o + 4 <= end:
            v = u32(raw, o)
            if (v >> 24) == 0x06 and hex(v) not in tex: tex.append(hex(v))
            o += 1
        e = {"did": hex(did), "type": etype}
        if geom: e.update(x=geom[0], y=geom[1], w=geom[2], h=geom[3], z=geom[4])
        if tex: e["tex"] = tex
        elements.append(e)
    return lid, {"w": W, "h": H, "elements": elements}


def main():
    loc = ame.DatReader(os.path.join(ROOT, "acdata", "client_local_English.dat"))
    out, placed, total = {}, 0, 0
    for did in sorted(loc.files):
        if (did >> 24) != 0x21: continue
        lid, lay = parse_layout(loc.read(did))
        out[hex(did)] = lay
        total += len(lay["elements"])
        placed += sum(1 for e in lay["elements"] if "x" in e)
    path = os.path.join(ROOT, "assets", "aclayouts.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, separators=(",", ":"))
    print(f"aclayouts.json: {len(out)} layouts, {total} elements, "
          f"{placed} with validated geometry ({placed*100//max(1,total)}%), "
          f"{os.path.getsize(path)//1024} KB")


if __name__ == "__main__":
    main()
