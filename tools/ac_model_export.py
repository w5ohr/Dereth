#!/usr/bin/env python3
"""Extract real AC models (player bodies, creatures) from acdata/client_portal.dat.

portal.dat holds every 3D asset of the original game. A Setup (0x02...) assembles GfxObj
mesh parts (0x01...) with a parent hierarchy and a placement frame; each GfxObj carries
vertices (position/normal/UVs) and n-gon polygons grouped by Surface (0x08...), which
resolve through SurfaceTexture (0x05...) to paletted bitmaps (0x06... + palette 0x04...).
Formats per the ACEmulator DatLoader (BSD-licensed reference implementation).

Output (assets/acmodels/):
  <name>.json     — parts: parent index, placement frame (pos+quat, three.js axes:
                    AC z-up -> three y-up via (x,z,-y)), and per-surface triangle groups
                    {tex|color, verts, normals, uvs, idx}
  tex/<id>.png    — decoded textures (P8 palette / 16-bit / 32-bit formats)
  index.json      — model name -> file + source setup id

Model list: the human male/female base setups + iconic creatures, whose setup ids come
from the ACE world DB weenie DIDs (type 1 = Setup) when present.
"""
import struct, os, re, json, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")
ACE = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core")
OUT = os.path.join(ROOT, "assets", "acmodels")

class DatReader:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.f.seek(0x140)
        hdr = self.f.read(0x24)
        (self.file_type, self.block_size, self.file_size, self.data_set,
         self.data_subset, self.free_head, self.free_tail, self.free_count,
         self.btree) = struct.unpack("<9I", hdr)
        self.files = {}
        self._walk(self.btree)

    def _read_chain(self, offset, want=None):
        out = bytearray()
        bs = self.block_size
        while offset:
            self.f.seek(offset)
            block = self.f.read(bs)
            nxt = struct.unpack("<I", block[:4])[0] & 0x7FFFFFFF
            out += block[4:]
            offset = nxt
            if want is not None and len(out) >= want:
                break
        return bytes(out[:want]) if want is not None else bytes(out)

    def _walk(self, offset):
        raw = self._read_chain(offset, 62*4 + 4 + 61*24)
        branches = struct.unpack("<62I", raw[:248])
        count = struct.unpack("<I", raw[248:252])[0]
        p = 252
        for _ in range(count):
            bitflags, oid, off, size, date, it = struct.unpack("<6I", raw[p:p+24]); p += 24
            self.files[oid] = (off, size)
        if branches[0] != 0:
            for i in range(count + 1):
                if branches[i]:
                    self._walk(branches[i])

    def read(self, oid):
        off, size = self.files[oid]
        return self._read_chain(off, size)

class Buf:
    def __init__(s, d): s.d = d; s.o = 0
    def u8(s): v = s.d[s.o]; s.o += 1; return v
    def u16(s): v = struct.unpack_from("<H", s.d, s.o)[0]; s.o += 2; return v
    def i16(s): v = struct.unpack_from("<h", s.d, s.o)[0]; s.o += 2; return v
    def u32(s): v = struct.unpack_from("<I", s.d, s.o)[0]; s.o += 4; return v
    def i32(s): v = struct.unpack_from("<i", s.d, s.o)[0]; s.o += 4; return v
    def f(s, n=1):
        v = struct.unpack_from("<%df" % n, s.d, s.o); s.o += 4*n
        return list(v) if n > 1 else v[0]
    def tag(s): v = s.d[s.o:s.o+4][::-1].decode("ascii", "replace"); s.o += 4; return v
    def cuint(s):     # AC compressed uint (per ACE ReadCompressedUInt32)
        b0 = s.u8()
        if not b0 & 0x80: return b0
        b1 = s.u8()
        if not b0 & 0x40: return ((b0 & 0x7F) << 8) | b1
        return ((((b0 & 0x3F) << 8) | b1) << 16) | s.u16()

# ── GfxObj ────────────────────────────────────────────────────────────────
STIP_NO_POS_UV, STIP_NO_NEG_UV = 4, 8
CULL_NONE, CULL_CW = 1, 2

def read_polygon(r):
    n = r.u8(); stip = r.u8(); sides = r.i32(); pos = r.i16(); neg = r.i16()
    vids = [r.i16() for _ in range(n)]
    posuv = None
    if not stip & STIP_NO_POS_UV:
        posuv = [r.u8() for _ in range(n)]
    if sides == CULL_CW and not stip & STIP_NO_NEG_UV:
        [r.u8() for _ in range(n)]
    return dict(v=vids, uv=posuv, surf=pos, dbl=(sides == CULL_NONE))

def read_bsp(r, tree):
    t = r.tag()
    if t == "LEAF":
        r.i32()
        if tree == "P":
            solid = r.i32(); r.f(4)
            n = r.u32(); [r.u16() for _ in range(n)]
        return
    if t == "PORT":
        r.f(4)
        read_bsp(r, tree); read_bsp(r, tree)
        r.f(4)
        if tree == "D":
            n = r.u32(); np = r.u32()
            [r.u16() for _ in range(n)]
            [(r.u16(), r.u16()) for _ in range(np)]
        return
    r.f(4)                       # splitting plane
    # tag case encodes children: 'P' (2nd char) = positive child, 'N' (4th char) = negative child
    if not (len(t) == 4 and t[0] in "Bb"):
        raise ValueError("BSP tag %r @%d" % (t, r.o))
    if t != "BPOL":              # BPOL is terminal: plane + sphere + polys, no children
        if t[1] == "P": read_bsp(r, tree)
        if t[3] == "N": read_bsp(r, tree)
    r.f(4)                       # bounding sphere
    if tree == "D":
        n = r.u32(); [r.u16() for _ in range(n)]

def parse_gfxobj(d):
    r = Buf(d)
    gid = r.u32(); flags = r.u32()
    surfs = [r.u32() for _ in range(r.cuint())]
    vtype = r.u32(); nv = r.u32()
    verts = {}
    for _ in range(nv):
        idx = r.u16(); nuv = r.u16()
        o = r.f(3); nrm = r.f(3)
        uvs = [(r.f(), r.f()) for _ in range(nuv)]
        verts[idx] = dict(o=o, n=nrm, uv=uvs)
    polys = {}
    if flags & 1:
        for _ in range(r.cuint()):
            r.u16(); read_polygon(r)
        read_bsp(r, "P")
    r.f(3)                       # sort center
    if flags & 2:
        for _ in range(r.cuint()):
            k = r.u16(); polys[k] = read_polygon(r)
        read_bsp(r, "D")
    if flags & 8: r.u32()
    if r.o != len(d):
        raise ValueError("gfxobj 0x%08X: consumed %d of %d" % (gid, r.o, len(d)))
    return dict(surfs=surfs, verts=verts, polys=polys)

# ── Setup ─────────────────────────────────────────────────────────────────
def parse_setup(d):
    r = Buf(d)
    sid = r.u32(); flags = r.u32()
    n = r.u32()
    parts = [r.u32() for _ in range(n)]
    parents = [r.u32() for _ in range(n)] if flags & 1 else [0xFFFFFFFF]*n
    scales = [r.f(3) for _ in range(n)] if flags & 2 else None
    for _ in range(r.u32()):     # holding locations: key + (part i32 + frame)
        r.u32(); r.i32(); r.f(7)
    for _ in range(r.u32()):     # connection points
        r.u32(); r.i32(); r.f(7)
    frames = None
    for _ in range(r.i32()):     # placement frames: key + AnimationFrame
        key = r.i32()
        fr = [dict(p=r.f(3), q=r.f(4)) for _ in range(n)]   # quat stored W,X,Y,Z
        nhooks = r.u32()
        if nhooks:
            raise ValueError("setup 0x%08X placement hooks unsupported" % sid)
        if frames is None or key == 0:
            frames = fr
    return dict(parts=parts, parents=parents, scales=scales, frames=frames)

# ── surfaces & textures ───────────────────────────────────────────────────
def parse_surface(d):
    r = Buf(d)
    st = r.u32()
    if st & 0x6:                 # Base1Image (0x2) | Base1ClipMap (0x4)
        tex = r.u32(); pal = r.u32()
        return dict(tex=tex, pal=pal, clip=bool(st & 0x4))
    color = r.u32()              # Base1Solid (0x1): ARGB color value
    return dict(color=color)

def parse_surfacetexture(d):
    r = Buf(d)
    r.u32(); r.i32(); r.u8()
    return [r.u32() for _ in range(r.u32())]

def parse_palette(d):
    r = Buf(d)
    r.u32()
    return [r.u32() for _ in range(r.u32())]

def _dxt_colors(block, o):
    # two RGB565 endpoints + a 4-entry palette (mode depends on c0>c1); returns (colors, c0>c1)
    c0 = struct.unpack_from("<H", block, o)[0]; c1 = struct.unpack_from("<H", block, o+2)[0]
    def rgb(c): return ((c >> 11 & 31)*255//31, (c >> 5 & 63)*255//63, (c & 31)*255//31)
    r0, g0, b0 = rgb(c0); r1, g1, b1 = rgb(c1)
    cols = [(r0, g0, b0, 255), (r1, g1, b1, 255), None, None]
    if c0 > c1:
        cols[2] = ((2*r0+r1)//3, (2*g0+g1)//3, (2*b0+b1)//3, 255)
        cols[3] = ((r0+2*r1)//3, (g0+2*g1)//3, (b0+2*b1)//3, 255)
    else:
        cols[2] = ((r0+r1)//2, (g0+g1)//2, (b0+b1)//2, 255)
        cols[3] = (0, 0, 0, 0)           # DXT1 punch-through transparent
    return cols

def _decode_dxt(data, w, h, dxt5):
    px = bytearray(w*h*4); bstride = 16 if dxt5 else 8; bi = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            b = data[bi:bi+bstride]; bi += bstride
            if len(b) < bstride: break
            if dxt5:
                a0, a1 = b[0], b[1]
                al = [a0, a1]
                if a0 > a1: al += [((7-i)*a0 + i*a1)//7 for i in range(1, 7)]
                else:       al += [((5-i)*a0 + i*a1)//5 for i in range(1, 5)] + [0, 255]
                abits = int.from_bytes(b[2:8], "little")
                cols = _dxt_colors(b, 8); lut = int.from_bytes(b[8:12], "little")
            else:
                cols = _dxt_colors(b, 0); lut = int.from_bytes(b[4:8], "little")
            for py in range(4):
                for pxx in range(4):
                    x, y = bx+pxx, by+py
                    if x >= w or y >= h: continue
                    idx = py*4+pxx
                    col = cols[(lut >> (idx*2)) & 3]
                    a = al[(abits >> (idx*3)) & 7] if dxt5 else col[3]
                    o = (y*w+x)*4
                    px[o:o+4] = bytes((col[0], col[1], col[2], a))
    return bytes(px)

def decode_texture(d, palettes, clip=False):
    r = Buf(d)
    tid = r.u32(); r.i32()
    w = r.i32(); h = r.i32(); fmt = r.u32(); ln = r.i32()
    data = r.d[r.o:r.o+ln]; r.o += ln
    pal_id = r.u32() if fmt in (40, 41, 101) else None
    px = bytearray(w*h*4)
    if fmt in (0x31545844, 0x35545844):   # FourCC "DXT1" / "DXT5"
        return w, h, _decode_dxt(data, w, h, fmt == 0x35545844)
    elif fmt == 41 or fmt == 101:          # P8 / INDEX16 paletted
        pal = palettes(pal_id)
        step = 2 if fmt == 101 else 1
        for i in range(w*h):
            v = data[i] if step == 1 else struct.unpack_from("<H", data, i*2)[0]
            if clip and v == 0:            # AC clip/alpha convention: palette index 0 is the transparent key
                continue                   # leave this pixel (0,0,0,0)
            c = pal[v] if v < len(pal) else 0
            a = (c >> 24) & 0xFF
            px[i*4:i*4+4] = bytes(((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, a if a else 255))
    elif fmt == 20:                       # R8G8B8
        for i in range(w*h):
            b, g, rr = data[i*3], data[i*3+1], data[i*3+2]
            px[i*4:i*4+4] = bytes((rr, g, b, 255))
    elif fmt in (21, 22):                 # A8R8G8B8 / X8R8G8B8
        for i in range(w*h):
            c = struct.unpack_from("<I", data, i*4)[0]
            a = (c >> 24) & 0xFF if fmt == 21 else 255
            px[i*4:i*4+4] = bytes(((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, a or 255))
    elif fmt == 23:                       # R5G6B5
        for i in range(w*h):
            c = struct.unpack_from("<H", data, i*2)[0]
            px[i*4:i*4+4] = bytes((((c >> 11) & 31)*255//31, ((c >> 5) & 63)*255//63, (c & 31)*255//31, 255))
    elif fmt == 26:                       # A4R4G4B4
        for i in range(w*h):
            c = struct.unpack_from("<H", data, i*2)[0]
            px[i*4:i*4+4] = bytes((((c >> 8) & 15)*17, ((c >> 4) & 15)*17, (c & 15)*17, ((c >> 12) & 15)*17 or 255))
    else:
        return None
    return w, h, bytes(px)

# ── mesh building ─────────────────────────────────────────────────────────
def tx_pos(v):  return [round(v[0], 4), round(v[2], 4), round(-v[1], 4)]     # AC z-up -> three y-up
def tx_quat(q): return [round(q[1], 5), round(q[3], 5), round(-q[2], 5), round(q[0], 5)]  # (x,z,-y,w)

def build_part(gfx):
    """polygons -> per-surface triangle groups with (vertexId, uvIndex)-split vertices."""
    groups = defaultdict(lambda: dict(vmap={}, verts=[], norms=[], uvs=[], idx=[]))
    for poly in gfx["polys"].values():
        g = groups[poly["surf"]]
        corner = []
        for i, vid in enumerate(poly["v"]):
            uvi = poly["uv"][i] if poly["uv"] else 0
            key = (vid, uvi)
            if key not in g["vmap"]:
                v = gfx["verts"][vid & 0xFFFF]
                g["vmap"][key] = len(g["verts"]) // 3
                g["verts"] += tx_pos(v["o"])
                g["norms"] += tx_pos(v["n"])
                uv = v["uv"][uvi] if uvi < len(v["uv"]) else (0, 0)
                g["uvs"] += [round(uv[0], 4), round(uv[1], 4)]
            corner.append(g["vmap"][key])
        for i in range(1, len(corner) - 1):          # fan-triangulate (reverse winding for the axis flip)
            g["idx"] += [corner[0], corner[i+1], corner[i]]
    return groups

def main():
    portal = DatReader(os.path.join(ACD, "client_portal.dat"))
    os.makedirs(os.path.join(OUT, "tex"), exist_ok=True)
    from PIL import Image

    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache:
            pal_cache[pid] = parse_palette(portal.read(pid))
        return pal_cache[pid]

    tex_written = {}
    def surface_material(sid):
        s = parse_surface(portal.read(sid))
        if "color" in s:
            return dict(color=s["color"] & 0xFFFFFF)
        texids = parse_surfacetexture(portal.read(s["tex"]))
        tid = texids[-1]                    # highest-res mip chain entry
        if tid not in tex_written:
            dec = decode_texture(portal.read(tid), palettes)
            if dec is None:
                tex_written[tid] = None
            else:
                w, h, px = dec
                Image.frombytes("RGBA", (w, h), px).save(os.path.join(OUT, "tex", "%08X.png" % tid))
                tex_written[tid] = "%08X.png" % tid
        out = dict(tex=tex_written[tid])
        if s.get("clip"): out["clip"] = 1
        return out

    # model list: humans + creatures whose setup ids live in the ACE weenie DIDs
    models = {"human_male": 0x02000001, "human_female": 0x0200004E}
    WANT = {"Drudge Slinker": "drudge", "Olthoi Soldier": "olthoi", "Skeleton": "skeleton",
            "Virindi Servant": "virindi", "Lugian": "lugian", "Tumerok": "tumerok"}
    cdir = os.path.join(ACE, "9 WeenieDefaults", "SQL", "Creature")
    if os.path.isdir(cdir):
        for root, _, files in os.walk(cdir):
            for fn in files:
                m = re.match(r"\d+ (.+)\.sql$", fn)
                if not m or m.group(1) not in WANT: continue
                src = open(os.path.join(root, fn), encoding="utf-8", errors="replace").read()
                d = re.search(r"VALUES \(\d+,\s*1,\s*(0x0200[0-9A-Fa-f]{4})\)", src)
                if d:
                    models[WANT.pop(m.group(1))] = int(d.group(1), 16)

    index = {}
    for name, sid in sorted(models.items()):
        try:
            setup = parse_setup(portal.read(sid))
        except Exception as e:
            print(f"  {name}: setup 0x{sid:08X} FAILED ({e})"); continue
        parts_out, gfx_fail = [], 0
        for i, gid in enumerate(setup["parts"]):
            fr = setup["frames"][i] if setup["frames"] else dict(p=[0,0,0], q=[1,0,0,0])
            try:
                gfx = parse_gfxobj(portal.read(gid))
            except Exception:
                gfx_fail += 1; continue
            if not gfx["polys"]: continue    # empty attachment-slot parts
            groups = build_part(gfx)
            gout = []
            for surfidx, g in sorted(groups.items()):
                sid_ = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                mat = surface_material(sid_) if sid_ else dict(color=0x888888)
                gout.append(dict(mat=mat, v=g["verts"], n=g["norms"], uv=g["uvs"], i=g["idx"]))
            parts_out.append(dict(
                parent=(setup["parents"][i] if setup["parents"][i] != 0xFFFFFFFF else -1),
                p=tx_pos(fr["p"]), q=tx_quat(fr["q"]),
                s=(setup["scales"][i] if setup["scales"] else None),
                groups=gout))
        fn = name + ".json"
        json.dump(dict(setup="%08X" % sid, parts=parts_out), open(os.path.join(OUT, fn), "w"),
                  separators=(",", ":"))
        index[name] = fn
        tris = sum(len(g["i"])//3 for pt in parts_out for g in pt["groups"])
        print(f"  {name}: setup 0x{sid:08X} -> {len(parts_out)} parts, {tris} tris"
              + (f" ({gfx_fail} parts failed)" if gfx_fail else ""))
    json.dump(index, open(os.path.join(OUT, "index.json"), "w"), indent=0)
    total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(OUT) for f in fs)
    print(f"\nwrote {OUT}: {len(index)} models, {len([t for t in tex_written.values() if t])} textures, {total//1024} KB")

if __name__ == "__main__":
    main()
