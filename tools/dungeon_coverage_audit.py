#!/usr/bin/env python3
"""Detect dungeons that render nearly EMPTY due to a truncated EnvCell export.

WHAT THIS FINDS
---------------
Real-geometry dungeons live in assets/acdungeons/<file>.json and store two things:
  * cellPos      : cell centres on a 10-unit grid -> drives WALKABILITY + the MINIMAP.
                   (This is why a broken dungeon still has a correct map and is walkable.)
  * groups[].verts / .idx : the actual WALL meshes, which index.html's buildDungeonReal()
                   assembles UNTRANSFORMED at those coordinates.

tools/ac_env_export.py caps/truncates big dungeons (stats field "capped": true). For the worst
cases the export contains almost NO real geometry: a few dozen vertices with the index buffer
padded by a tiny quad repeated thousands of times (e.g. idx = [0,2,1, 0,3,2] over and over). The
dungeon then renders mostly EMPTY -- you can walk it and the minimap is right, but the walls are
missing. A player hit this in the Highland/Direlands Tusker Temple (14% of cells had any wall).

This is a COMPLETENESS bug and is DISTINCT from the "scrambled geometry" audit (issues #754-#760),
which checks whether the walls that exist are placed correctly (they are). This checks whether the
walls exist at all.

HOW IT DETECTS IT (two signals per dungeon)
-------------------------------------------
  1. cell coverage = fraction of cellPos that have at least one wall vertex within ~8u in XZ.
                     Healthy dungeons ~0.5-1.0; broken/truncated ones drop toward 0.1.
  2. tris/verts    = per-group triangles : unique vertices. Legit geometry is ~0.5-3. A ratio far
                     above that (with a short repeating index run) = a padded/degenerate index
                     buffer -- the truncation signature.

THE FIX (already shipped for the Tusker Temples, see SYNTH_WALL_DUNGEONS in index.html)
--------------------------------------------------------------------------------------
For an affected dungeon, ignore the broken export and rebuild walls/floors/ceilings from the cell
grid: a floor under every cell, a wall on every open edge (no neighbouring cell), a ceiling where no
storey sits above -- one merged mesh skinned with the dungeon's own wall texture. Walkability is
unchanged (it already comes from cellPos), so the synthesized walls line up with where you can walk.
To generalise: change the SYNTH_WALL_DUNGEONS gate to trigger for any dungeon below the coverage
threshold, OR re-export with a higher cap / fix the exporter.

USAGE
-----
  python3 tools/dungeon_coverage_audit.py                 # print the report
  python3 tools/dungeon_coverage_audit.py --threshold 0.4 # flag dungeons under 40% coverage
  python3 tools/dungeon_coverage_audit.py --json docs/dungeon-coverage.json   # also dump machine-readable

Exit code is 0 always; the report + JSON are the product. Re-run after any export/fix change.
"""
import json, os, glob, math, argparse, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DGN  = os.path.join(ROOT, "assets", "acdungeons")
BIN  = 8.0   # XZ hash bin for the coverage test (cells are on a 10u grid; ~8u catches on-cell walls)

def analyze(path):
    j = json.load(open(path))
    cp = j.get("cellPos", []); groups = j.get("groups", [])
    if not cp or not groups:
        return dict(cells=len(cp), coverage=0.0, verts=0, tris=0, maxRatio=0.0, note="empty cellPos/groups")
    # signal 1: cell coverage
    wall = set(); nverts = 0
    for g in groups:
        v = g.get("verts", [])
        for i in range(0, len(v), 3):
            wall.add((round(v[i] / BIN), round(v[i + 2] / BIN))); nverts += 1
    covered = 0
    for c in cp:
        bx, bz = round(c[0] / BIN), round(c[2] / BIN); hit = False
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                if (bx + dx, bz + dz) in wall:
                    hit = True; break
            if hit: break
        if hit: covered += 1
    coverage = covered / len(cp)
    # signal 2: degenerate/padded index buffers (tris >> unique verts)
    tris = 0; maxRatio = 0.0
    for g in groups:
        nv = max(1, len(g.get("verts", [])) // 3)
        nt = len(g.get("idx", [])) // 3
        tris += nt
        maxRatio = max(maxRatio, nt / nv)
    return dict(cells=len(cp), coverage=round(coverage, 3), verts=nverts, tris=tris,
                maxRatio=round(maxRatio, 1), note="")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--threshold", type=float, default=0.4, help="flag dungeons below this cell coverage")
    ap.add_argument("--json", help="also write full results to this path")
    args = ap.parse_args()

    idx = {}
    ip = os.path.join(DGN, "index.json")
    if os.path.exists(ip): idx = json.load(open(ip))
    fileToName = {v["file"]: k for k, v in idx.items()} if idx else {}

    results = []
    for path in sorted(glob.glob(os.path.join(DGN, "*.json"))):
        base = os.path.basename(path)
        if base == "index.json": continue
        name = fileToName.get(base, base[:-5].replace("_", " "))
        a = analyze(path); a["name"] = name; a["file"] = base
        a["capped"] = bool(idx.get(name, {}).get("capped", False))
        results.append(a)

    results.sort(key=lambda r: r["coverage"])
    flagged = [r for r in results if r["coverage"] < args.threshold]

    print(f"Scanned {len(results)} dungeons in {DGN}")
    print(f"Flagged {len(flagged)} below {args.threshold:.0%} cell coverage "
          f"(severe <25%: {sum(1 for r in flagged if r['coverage'] < 0.25)}).\n")
    print(f"{'cover':>6} {'cells':>6} {'verts':>7} {'maxT/V':>7} {'capped':>6}  dungeon")
    for r in flagged:
        print(f"{r['coverage']*100:5.0f}% {r['cells']:6d} {r['verts']:7d} {r['maxRatio']:7.1f} "
              f"{str(r['capped']):>6}  {r['name']}")

    if args.json:
        json.dump(results, open(args.json, "w"), indent=1)
        print(f"\nFull results ({len(results)} dungeons) -> {args.json}")

if __name__ == "__main__":
    main()
