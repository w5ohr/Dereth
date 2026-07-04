#!/usr/bin/env python3
"""Every real AC BUILDING that isn't in a town -> assets/acworldstructs.json (+ meshes).

The town buildings (assets/actowns.json / actownmodels, tools/ace_building_export.py +
ac_town_models.py) cover only structures within RADIUS of a town centre. This tool takes
the SAME client_cell_1.dat LandblockInfo "Buildings" list but for the WHOLE world, keeps
the ones NOT near any town (isolated houses, keeps, ruins, dungeon-surface halls...), and:

  * bakes any building mesh DID not already in assets/actownmodels/ into that shared pack
    (same {groups:[{mat,v,n,uv,i}],bb} schema + DXT/clip-aware textures)
  * emits assets/acworldstructs.json keyed by landblock -> the buildings' ABSOLUTE world
    positions (worldX = lng*COORD, worldZ = -lat*COORD, matching buildCityList) so the
    engine can stream them in per-landblock around the player.
"""
import os, re, struct, math, json, importlib.util
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")
OUT = os.path.join(ROOT, "assets", "actownmodels")           # shared mesh pack
TEXOUT = os.path.join(OUT, "tex")
ame_spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(ame_spec); ame_spec.loader.exec_module(ame)

# ── world coords (same convention as ace_building_export.py / index.html buildCityList) ──
html = open(os.path.join(ROOT, "index.html"), encoding="utf-8", errors="replace").read()
COORD = int(re.search(r"const COORD=(\d+)", html).group(1))
tblk = re.search(r"const TOWN_DATA=\[(.*?)\n\];", html, re.S).group(1)
TOWNS = [(float(m.group(2)), float(m.group(3)))
         for m in re.finditer(r'\["([^"]+)",\s*(-?[\d.]+),\s*(-?[\d.]+)', tblk)]
RADIUS = 2.0   # deg — a building this close to a town centre is a TOWN building (handled elsewhere)

def latlng(cell, ox, oy):
    gx = ((cell >> 24) & 0xFF) * 192.0 + ox
    gy = ((cell >> 16) & 0xFF) * 192.0 + oy
    return (gy / 24.0 - 1019.5) / 10.0, (gx / 24.0 - 1019.5) / 10.0
def yaw(qw, qz): return round(2.0 * math.atan2(qz, qw), 4)

class DatReader:
    def __init__(self, path):
        self.f = open(path, "rb"); self.f.seek(0x140); hdr = self.f.read(0x24)
        v = struct.unpack("<9I", hdr); self.block_size = v[1]; self.btree = v[8]
        self.files = {}; self._walk(self.btree)
    def _rc(self, offset, want=None):
        out = bytearray(); bs = self.block_size
        while offset:
            self.f.seek(offset); blk = self.f.read(bs)
            nxt = struct.unpack("<I", blk[:4])[0] & 0x7FFFFFFF
            out += blk[4:]; offset = nxt
            if want is not None and len(out) >= want: break
        return bytes(out[:want]) if want is not None else bytes(out)
    def _walk(self, offset):
        raw = self._rc(offset, 62*4 + 4 + 61*24)
        branches = struct.unpack("<62I", raw[:248]); count = struct.unpack("<I", raw[248:252])[0]; p = 252
        for _ in range(count):
            _, oid, off, size, _, _ = struct.unpack("<6I", raw[p:p+24]); p += 24
            self.files[oid] = (off, size)
        if branches[0]:
            for i in range(count + 1):
                if branches[i]: self._walk(branches[i])
    def read(self, oid):
        off, size = self.files[oid]; return self._rc(off, size)

def landblock_buildings(cell, lb):
    fid = (lb << 16) | 0xFFFE
    if fid not in cell.files: return []
    d = cell.read(fid)
    try:
        _, _, numObj = struct.unpack_from("<3I", d, 0); p = 12
        for _ in range(numObj): p += 32                      # skip Objects (scenery)
        numB, _pk = struct.unpack_from("<2H", d, p); p += 4
        out = []
        for _ in range(numB):
            mid, = struct.unpack_from("<I", d, p)
            bx, by, bz, qw, _, _, qz = struct.unpack_from("<7f", d, p + 4)
            _nl, numPortals = struct.unpack_from("<2I", d, p + 32); p += 40
            for _ in range(numPortals):
                _, _, _, ns = struct.unpack_from("<4H", d, p); p += 8 + 2 * ns
                if p % 4: p += 4 - (p % 4)
            out.append((mid, bx, by, qw, qz))
        return out
    except struct.error:
        return []

# ── mesh baking (shared with ac_town_models: 0x01 GfxObj / 0x02 Setup -> {groups,bb}) ──
def qrot(q, v):
    qv = (q[0], q[1], q[2]); t = [2*(qv[1]*v[2]-qv[2]*v[1]), 2*(qv[2]*v[0]-qv[0]*v[2]), 2*(qv[0]*v[1]-qv[1]*v[0])]
    return (v[0]+q[3]*t[0]+qv[1]*t[2]-qv[2]*t[1], v[1]+q[3]*t[1]+qv[2]*t[0]-qv[0]*t[2], v[2]+q[3]*t[2]+qv[0]*t[1]-qv[1]*t[0])

def main():
    os.makedirs(TEXOUT, exist_ok=True)
    from PIL import Image
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    cell = DatReader(os.path.join(ACD, "client_cell_1.dat"))
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]
    tex_written = {}
    def surface_material(sid):
        s = ame.parse_surface(portal.read(sid))
        if "color" in s: return dict(color=s["color"] & 0xFFFFFF)
        clip = bool(s.get("clip"))
        tid = ame.parse_surfacetexture(portal.read(s["tex"]))[-1]
        if tid not in tex_written:
            fn = "%08X.png" % tid
            if not os.path.isfile(os.path.join(TEXOUT, fn)):
                dec = ame.decode_texture(portal.read(tid), palettes, clip=clip)
                if dec is None: tex_written[tid] = None
                else:
                    w, h, px = dec; im = Image.frombytes("RGBA", (w, h), px)
                    if im.getchannel("A").getextrema()[0] == 255:
                        im = im.convert("RGB").quantize(colors=256, method=Image.MAXCOVERAGE)
                    im.save(os.path.join(TEXOUT, fn), optimize=True); tex_written[tid] = fn
            else: tex_written[tid] = fn
        out = dict(tex=tex_written[tid]) if tex_written[tid] else dict(color=0x8a8478)
        if s.get("clip") and "tex" in out: out["clip"] = 1
        return out

    index = json.load(open(os.path.join(OUT, "index.json")))
    models = index["models"]

    def bake(didhex):
        if didhex in models: return True
        sid = int(didhex, 16)
        path = os.path.join(OUT, "%08X.json" % sid)
        try:
            if (sid >> 24) == 0x01:
                parts = [dict(gid=sid, p=(0, 0, 0), q=(0, 0, 0, 1))]
            else:
                su = ame.parse_setup(portal.read(sid)); frames = su.get("frames") or []
                parts = []
                for i, gid in enumerate(su["parts"]):
                    fr = frames[i] if i < len(frames) else None
                    parts.append(dict(gid=gid, p=ame.tx_pos(fr["p"]) if fr else (0, 0, 0),
                                      q=ame.tx_quat(fr["q"]) if fr else (0, 0, 0, 1)))
            gout = []; tris = 0; bb = [1e9, 1e9, 1e9, -1e9, -1e9, -1e9]
            for prt in parts:
                gfx = ame.parse_gfxobj(portal.read(prt["gid"])); groups = ame.build_part(gfx)
                for surfidx, g in sorted(groups.items()):
                    if len(g["idx"]) < 3: continue
                    v2 = []; n2 = []
                    for k in range(0, len(g["verts"]), 3):
                        x, y, z = g["verts"][k:k+3]; rx, ry, rz = qrot(prt["q"], (x, y, z))
                        wx, wy, wz = rx+prt["p"][0], ry+prt["p"][1], rz+prt["p"][2]
                        v2 += [round(wx, 4), round(wy, 4), round(wz, 4)]
                        bb[0]=min(bb[0],wx);bb[1]=min(bb[1],wy);bb[2]=min(bb[2],wz)
                        bb[3]=max(bb[3],wx);bb[4]=max(bb[4],wy);bb[5]=max(bb[5],wz)
                        nx, ny, nz = g["norms"][k:k+3]; rn = qrot(prt["q"], (nx, ny, nz))
                        n2 += [round(rn[0], 4), round(rn[1], 4), round(rn[2], 4)]
                    ssid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                    gout.append(dict(mat=(surface_material(ssid) if ssid else dict(color=0x8a8478)),
                                     v=v2, n=n2, uv=g["uvs"], i=g["idx"])); tris += len(g["idx"])//3
            if not gout: return False
            bb = [round(b, 3) for b in bb]
            json.dump(dict(groups=gout, bb=bb), open(path, "w"), separators=(",", ":"))
            models[didhex] = dict(file="%08X.json" % sid, tris=tris, bb=bb)
            return True
        except Exception:
            return False

    # ── scan every landblock, keep non-town buildings, bake meshes, group by landblock ──
    lbs = sorted(set((fid >> 16) for fid in cell.files if (fid & 0xFFFF) == 0xFFFE))
    blocks = {}; n_kept = n_town = 0; baked = 0; fails = set()
    for lb in lbs:
        blds = landblock_buildings(cell, lb)
        if not blds: continue
        recs = []
        for mid, bx, by, qw, qz in blds:
            lat, lng = latlng(lb << 16, bx, by)
            if any(math.hypot(lat - tl, lng - tn) <= RADIUS for tl, tn in TOWNS):
                n_town += 1; continue                        # a town building — handled by actowns
            didhex = "0x%08X" % mid
            if not bake(didhex):
                fails.add(didhex); continue
            recs.append([didhex, round(lng * COORD, 1), round(-lat * COORD, 1), yaw(qw, qz)])
            n_kept += 1
        if recs:
            clat, clng = latlng(lb << 16, 96, 96)            # landblock centre (for streaming distance)
            blocks["%04X" % lb] = dict(cx=round(clng * COORD, 1), cz=round(-clat * COORD, 1), b=recs)
    json.dump(dict(models=models), open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    json.dump(dict(meta=dict(source="client_cell_1.dat LandblockInfo buildings, non-town",
                             unit=COORD, radius_deg=RADIUS), blocks=blocks),
              open(os.path.join(ROOT, "assets", "acworldstructs.json"), "w"), separators=(",", ":"))
    total = 0
    for r, _, fs in os.walk(OUT):
        for f in fs: total += os.path.getsize(os.path.join(r, f))
    print(f"non-town buildings kept: {n_kept:,} in {len(blocks):,} landblocks (town-skipped {n_town:,})")
    print(f"distinct DIDs now in pack: {len(models)}  (bake fails {len(fails)})")
    print(f"shared actownmodels pack: {total//1024//1024} MB")

if __name__ == "__main__":
    main()
