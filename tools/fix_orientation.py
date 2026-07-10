#!/usr/bin/env python3
"""Re-orient triangle winding in generated AC mesh packs to agree with vertex normals.

The AC client culls polygons by their plane/normal, not vertex order — the dat's vertex
order is arbitrary and mixed between polygons of the same mesh. Any winding-based culler
(three.js FrontSide) therefore renders a random subset of polys inside-out: bodies read
as depth-mirrored / 180° off, ears/thumbs/elbows mirrored, face strips invisible.

ac_model_export.build_part now orients every triangle at export time. This script applies
the identical rule to ALREADY-GENERATED packs whose exporters can't currently be re-run
(missing ACE-World inputs): for each triangle, if its cross-product normal disagrees with
the sum of its vertex normals, swap two indices. Naturally idempotent.

Recognizes any dict with vertex data "v", normals "n" and an index list "i"/"idx".
"""
import json, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKS = [
    "assets/acmodels",         # creature meshes not in the human re-export
    "assets/acarmor",
    "assets/acmisc",
    "assets/acflora",
    "assets/actownmodels",
]
SKIP_NAMES = {"index.json", "palettes.json", "dyes.json", "hats.json"}

def orient(v, n, ix):
    flips = 0
    for t in range(0, len(ix) - 2, 3):
        a, b, c = ix[t], ix[t+1], ix[t+2]
        ax, ay, az = v[a*3], v[a*3+1], v[a*3+2]
        ux, uy, uz = v[b*3]-ax, v[b*3+1]-ay, v[b*3+2]-az
        wx, wy, wz = v[c*3]-ax, v[c*3+1]-ay, v[c*3+2]-az
        gx, gy, gz = uy*wz-uz*wy, uz*wx-ux*wz, ux*wy-uy*wx
        nx = n[a*3]+n[b*3]+n[c*3]; ny = n[a*3+1]+n[b*3+1]+n[c*3+1]; nz = n[a*3+2]+n[b*3+2]+n[c*3+2]
        if gx*nx + gy*ny + gz*nz < 0:
            ix[t+1], ix[t+2] = c, b; flips += 1
    return flips

def walk(node, stats):
    if isinstance(node, dict):
        v, n = node.get("v"), node.get("n")
        if isinstance(v, list) and isinstance(n, list) and len(v) == len(n) and v:
            for key in ("i", "idx"):
                ix = node.get(key)
                if isinstance(ix, list) and len(ix) >= 3 and len(ix) % 3 == 0:
                    stats[1] += orient(v, n, ix); stats[0] += 1
        for x in node.values():
            walk(x, stats)
    elif isinstance(node, list):
        for x in node:
            walk(x, stats)

def main():
    for rel in PACKS:
        d = os.path.join(ROOT, *rel.split("/"))
        if not os.path.isdir(d):
            print(f"  {rel}: absent, skipped"); continue
        nf = ng = nt = 0
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".json") or fn in SKIP_NAMES: continue
            p = os.path.join(d, fn)
            try:
                j = json.load(open(p, encoding="utf-8"))
            except Exception as e:
                print(f"    {rel}/{fn}: unreadable ({e})"); continue
            stats = [0, 0]
            walk(j, stats)
            if stats[1]:
                json.dump(j, open(p, "w", encoding="utf-8"), separators=(",", ":"))
                nf += 1
            ng += stats[0]; nt += stats[1]
        print(f"  {rel}: {nf} files rewritten · {ng} groups scanned · {nt} tris re-wound")

if __name__ == "__main__":
    main()
