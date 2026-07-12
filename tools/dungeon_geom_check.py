#!/usr/bin/env python3
"""Dungeon geometry alignment check — flags "scrambled walls, good map" delves.

A real-geometry (EnvCell) delve stores its walkable cells in `cellPos` (which also drives the
minimap — so the MAP looks right) and its visible walls in `groups[].verts`. When those two are
in mismatched coordinate frames for a dungeon, the map is correct but the 3D walls render
scrambled. This tool measures, per dungeon, how well the wall vertices hug the cell grid and
flags the ones whose geometry does NOT line up with their cells.

Runs entirely on the committed data in assets/acdungeons/ — no browser, no game, no #686.

Usage:
    python3 tools/dungeon_geom_check.py                 # audit every dungeon, print flagged ones
    python3 tools/dungeon_geom_check.py --all           # print the metric for every dungeon
    python3 tools/dungeon_geom_check.py --json out.json # write full results as JSON
    python3 tools/dungeon_geom_check.py "A Ruin" ...     # only the named dungeon(s)
"""
import json, os, sys, math

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DDIR = os.path.join(ROOT, "assets", "acdungeons")
CELL_HALF = 5.6          # dungeonRects half-extent per cell (index.html buildDungeonReal)
MARGIN    = 3.2          # doorframes/skirting can sit a little outside the cell footprint
ONGRID    = CELL_HALF + MARGIN   # a vertex is "on grid" if within this of the nearest cell centre (Chebyshev, x/z)

# thresholds (calibrated across the pack): flag a dungeon if its walls don't hug its cells
OK_ONGRID_MIN   = 0.80   # ≥80% of wall verts should sit on the cell grid
OK_CTR_OFFSET   = 8.0    # mesh centre vs cell centre (x/z) should be within ~one cell
OK_SPAN_RATIO   = 2.2    # mesh span vs cell span should be within ~2.2× either way


def metrics(d):
    cells = d.get("cellPos") or []
    if not cells:
        return {"verdict": "NO_CELLS", "cells": 0, "verts": 0}
    cx = [c[0] for c in cells]; cz = [c[2] for c in cells]
    cminx, cmaxx, cminz, cmaxz = min(cx), max(cx), min(cz), max(cz)
    cspan = max(cmaxx - cminx, cmaxz - cminz) + 2 * CELL_HALF
    cctrx, cctrz = (cminx + cmaxx) / 2, (cminz + cmaxz) / 2

    vx = []; vz = []
    for g in d.get("groups") or []:
        v = g.get("verts") or []
        for i in range(0, len(v) - 2, 3):
            vx.append(v[i]); vz.append(v[i + 2])
    if not vx:
        return {"verdict": "NO_VERTS", "cells": len(cells), "verts": 0}

    vminx, vmaxx, vminz, vmaxz = min(vx), max(vx), min(vz), max(vz)
    vspan = max(vmaxx - vminx, vmaxz - vminz)
    vctrx, vctrz = (vminx + vmaxx) / 2, (vminz + vmaxz) / 2

    # on-grid fraction: share of wall verts within one cell footprint of SOME cell centre
    on = 0
    for x, z in zip(vx, vz):
        best = min(max(abs(x - c[0]), abs(z - c[2])) for c in cells)
        if best <= ONGRID:
            on += 1
    ongrid = on / len(vx)

    ctr_off = math.hypot(vctrx - cctrx, vctrz - cctrz)
    span_ratio = (vspan / cspan) if cspan > 1e-6 else 999
    span_bad = span_ratio > OK_SPAN_RATIO or span_ratio < (1 / OK_SPAN_RATIO)

    reasons = []
    if ongrid < OK_ONGRID_MIN: reasons.append(f"only {ongrid*100:.0f}% of walls on the cell grid")
    if ctr_off > OK_CTR_OFFSET: reasons.append(f"mesh centre {ctr_off:.0f}u off the cell centre")
    if span_bad: reasons.append(f"mesh span {span_ratio:.2f}× the cell span")
    verdict = "SCRAMBLED" if reasons else "ok"
    return {"verdict": verdict, "cells": len(cells), "verts": len(vx),
            "ongrid": round(ongrid, 3), "ctrOffset": round(ctr_off, 1),
            "spanRatio": round(span_ratio, 2), "reasons": reasons}


def main():
    args = sys.argv[1:]
    show_all = "--all" in args
    json_out = None
    if "--json" in args:
        i = args.index("--json"); json_out = args[i + 1]; args = args[:i] + args[i + 2:]
    args = [a for a in args if not a.startswith("--")]

    idx_path = os.path.join(DDIR, "index.json")
    index = json.load(open(idx_path)) if os.path.exists(idx_path) else {}
    # map dungeon display-name -> file
    files = {name: meta["file"] for name, meta in index.items()} if index else {}
    if args:
        files = {n: files[n] for n in args if n in files}

    results = {}
    for name, fn in sorted(files.items()):
        try:
            d = json.load(open(os.path.join(DDIR, fn)))
        except Exception as e:
            results[name] = {"verdict": "READ_ERROR", "err": str(e)}; continue
        results[name] = metrics(d)

    flagged = {n: r for n, r in results.items() if r["verdict"] not in ("ok",)}
    print(f"Checked {len(results)} real-geometry dungeons.")
    print(f"  aligned (ok): {sum(1 for r in results.values() if r['verdict']=='ok')}")
    print(f"  FLAGGED:      {len(flagged)}\n")
    for n, r in sorted(flagged.items(), key=lambda kv: kv[1].get("ongrid", 1)):
        why = "; ".join(r.get("reasons", [])) or r["verdict"]
        print(f"  [{r['verdict']:9}] {n:38} cells={r.get('cells','?'):>3}  {why}")
    if show_all:
        print("\n--- all dungeons (ongrid / ctrOffset / spanRatio) ---")
        for n, r in sorted(results.items(), key=lambda kv: kv[1].get("ongrid", 1)):
            print(f"  {r['verdict']:9} {n:38} ongrid={r.get('ongrid','?')} ctr={r.get('ctrOffset','?')} span={r.get('spanRatio','?')}")
    if json_out:
        json.dump(results, open(json_out, "w"), indent=1)
        print(f"\nWrote {json_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
