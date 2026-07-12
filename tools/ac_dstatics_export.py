#!/usr/bin/env python3
"""Extract every dungeon's PLACED STATICS from the EnvCell stab lists (#766).

ac_env_export.py stops parsing an EnvCell at its frame — but the record continues:
CellPortals, the visible-cell list (whose count is the misleadingly-named `numStabs`
u16 in the header), and then — behind flag bit 2 — the REAL static objects: a u32
count of Stab records {SetupId u32, Frame(pos 3f + quat 4f)}. These are the tables,
cages, altars, statues, braziers and doors Turbine placed in every room.

Outputs (mesh schema identical to acmisc/acflora so the client loader is trivial):
  assets/acdstatics/S<hex>.json     groups[{v,n,uv,i,mat{tex|color[,clip]}}] per unique setup
  assets/acdstatics/tex/*.png       shared decoded textures
  assets/acdstatics/index.json      {setups:[{id,lum,uses}]} — lum=1 marks a LIGHT-EMITTING
                                    object (any surface with luminosity > 0.05; data-driven)
  assets/dungeon-statics.json       dungeonName -> [[setupIdx, x,y,z, qx,qy,qz,qw]...]
                                    in ENGINE space (tx_pos / tx_quat, same as the walls)

Stab frames share the cell frames' landblock-local space, so the same transform used
for the wall geometry drops them exactly where the walls are.
"""
import os, sys, json, struct
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_model_export as ame
from ac_model_export import DatReader, Buf
import ac_dungeon_export as dung
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")
OUT = os.path.join(ROOT, "assets", "acdstatics")
TEXOUT = os.path.join(OUT, "tex")

def tx_pos(v): return [round(v[0], 4), round(v[2], 4), round(-v[1], 4)]

def qrot3(q, v):
    """rotate three-space vector by three-space quaternion [x,y,z,w]."""
    x, y, z, w = q
    tx = 2*(y*v[2] - z*v[1]); ty = 2*(z*v[0] - x*v[2]); tz = 2*(x*v[1] - y*v[0])
    return (v[0] + w*tx + (y*tz - z*ty),
            v[1] + w*ty + (z*tx - x*tz),
            v[2] + w*tz + (x*ty - y*tx))


def parse_cell_stabs(data):
    """Full EnvCell walk to the stab list → [(setupId, pos, quat_ac), ...]."""
    r = Buf(data)
    r.u32()                                   # Id
    flags = r.u32()
    r.u32()                                   # CellId repeat
    nsurf = r.u8(); nport = r.u8()
    nvis = r.u16()                            # header u16 counts VISIBLE CELLS, not stabs
    for _ in range(nsurf): r.u16()
    r.u16(); r.u16()                          # EnvironmentId, CellStructure
    r.f(3); r.f(4)                            # cell frame (unused here — stabs are landblock-local)
    for _ in range(nport): r.u16(); r.u16(); r.u16(); r.u16()
    for _ in range(nvis): r.u16()
    stabs = []
    if flags & 2:
        n = r.u32()
        for _ in range(n):
            sid = r.u32()
            pos = r.f(3); quat = r.f(4)
            stabs.append((sid, pos, quat))
    return stabs


def main():
    portal = DatReader(os.path.join(ACD, "client_portal.dat"))
    cell = DatReader(os.path.join(ACD, "client_cell_1.dat"))
    lbs = json.load(open(os.path.join(ROOT, "assets", "dungeon-landblocks.json")))
    os.makedirs(TEXOUT, exist_ok=True)

    palettes = lambda pid: ame.parse_palette(portal.read(pid))
    tex_written, lum_cache = {}, {}

    def surface_material(sid):
        s = ame.parse_surface(portal.read(sid))
        if "color" in s:
            return dict(color=s["color"] & 0xFFFFFF)
        clip = bool(s.get("clip"))
        tid = ame.parse_surfacetexture(portal.read(s["tex"]))[-1]
        if tid not in tex_written:
            fn = "%08X.png" % tid
            if not os.path.isfile(os.path.join(TEXOUT, fn)):
                dec = ame.decode_texture(portal.read(tid), palettes, clip=clip)
                if dec is None:
                    tex_written[tid] = None
                else:
                    w, h, px = dec
                    im = Image.frombytes("RGBA", (w, h), px)
                    if im.getchannel("A").getextrema()[0] == 255:
                        im = im.convert("RGB").quantize(colors=256, method=Image.MAXCOVERAGE)
                    im.save(os.path.join(TEXOUT, fn), optimize=True)
                    tex_written[tid] = fn
            else:
                tex_written[tid] = fn
        out = dict(tex=tex_written[tid]) if tex_written[tid] else dict(color=0x8a8478)
        if s.get("clip") and "tex" in out:
            out["clip"] = 1
        return out

    def setup_scripted(d):
        """Walk the SetupModel to its 6-u32 tail; slot 3 is the default PhysicsScript (0x33).
        Surfaces in this dat are 8-byte records with NO luminosity — the honest fire signal is a
        self-animating script: torches, braziers, candelabra carry one (verified: 201 scripted
        setups → 11,871 dungeon placements)."""
        try:
            r = Buf(d)
            r.u32(); flags = r.u32(); n = r.u32()
            for _ in range(n): r.u32()
            if flags & 1:
                for _ in range(n): r.u32()
            if flags & 2:
                for _ in range(n): r.f(3)
            for _ in range(r.u32()): r.u32(); r.i32(); r.f(7)
            for _ in range(r.u32()): r.u32(); r.i32(); r.f(7)
            for _ in range(r.i32()):
                r.i32()
                for _ in range(n): r.f(7)
                if r.u32(): return False
            for _ in range(r.u32()): r.f(5)
            for _ in range(r.u32()): r.f(4)
            r.f(4); r.f(4); r.f(4)
            if (len(r.d) - r.o) // 4 < 6: return False
            t = [r.u32() for _ in range(6)]
            return (t[2] >> 24) == 0x33
        except Exception:
            return False

    # pass 1: every dungeon's stab placements
    placements, use = {}, Counter()
    bad_cells = 0
    for name, hexid in lbs.items():
        lb = int(hexid, 16)
        cells = dung.dungeon_cells(cell, lb)
        if not cells:
            continue
        rows = []
        for cid in sorted(cells):
            try:
                stabs = parse_cell_stabs(cell.read((lb << 16) | cid))
            except Exception:
                bad_cells += 1
                continue
            for sid, spos, squat in stabs:
                if not (0x01000000 <= sid <= 0x02FFFFFF):
                    continue                   # renderable Setup/GfxObj stabs only
                use[sid] += 1
                p = tx_pos(spos); q = ame.tx_quat(squat)
                rows.append([sid, p[0], p[1], p[2], q[0], q[1], q[2], q[3]])
        if rows:
            placements[name] = rows
    print(f"stabs: {sum(len(v) for v in placements.values())} placements across "
          f"{len(placements)} dungeons, {len(use)} unique objects ({bad_cells} unreadable cells)")

    # pass 2: export each unique object's mesh (setup or bare gfxobj), acmisc-style
    setups = []
    for sid in sorted(use):
        shex = "%08X" % sid
        lum = 0
        gout = []
        try:
            if sid >= 0x02000000:
                raw = portal.read(sid)
                if setup_scripted(raw):
                    lum = 1
                su = ame.parse_setup(raw); frames = su.get("frames") or []
                parts = []
                for i, gid in enumerate(su["parts"]):
                    fr = frames[i] if i < len(frames) else None
                    parts.append(dict(gid=gid, p=ame.tx_pos(fr["p"]) if fr else (0, 0, 0),
                                      q=ame.tx_quat(fr["q"]) if fr else (0, 0, 0, 1)))
            else:
                parts = [dict(gid=sid, p=(0, 0, 0), q=(0, 0, 0, 1))]
            for prt in parts:
                gfx = ame.parse_gfxobj(portal.read(prt["gid"]))
                groups = ame.build_part(gfx)
                for surfidx, g in sorted(groups.items()):
                    if len(g["idx"]) < 3:
                        continue
                    ssid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                    v2 = []; n2 = []
                    for k in range(0, len(g["verts"]), 3):
                        w = qrot3(prt["q"], tuple(g["verts"][k:k+3]))
                        v2 += [round(w[0]+prt["p"][0], 4), round(w[1]+prt["p"][1], 4), round(w[2]+prt["p"][2], 4)]
                        rn = qrot3(prt["q"], tuple(g["norms"][k:k+3]))
                        n2 += [round(rn[0], 4), round(rn[1], 4), round(rn[2], 4)]
                    gout.append(dict(mat=(surface_material(ssid) if ssid else dict(color=0x8a8478)),
                                     v=v2, n=n2, uv=g["uvs"], i=g["idx"]))
        except Exception:
            continue
        if not gout:
            continue
        json.dump(dict(groups=gout), open(os.path.join(OUT, "S" + shex + ".json"), "w"),
                  separators=(",", ":"))
        setups.append(dict(id=shex, lum=lum, uses=use[sid]))
    idx = {s["id"]: i for i, s in enumerate(setups)}

    out_pl = {}
    for name, rows in placements.items():
        keep = [[idx["%08X" % r[0]]] + r[1:] for r in rows if ("%08X" % r[0]) in idx]
        if keep:
            out_pl[name] = keep
    json.dump(dict(setups=setups), open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    json.dump(out_pl, open(os.path.join(ROOT, "assets", "dungeon-statics.json"), "w"),
              separators=(",", ":"))
    lums = sum(1 for s in setups if s["lum"])
    print(f"exported {len(setups)} unique statics ({lums} light-emitting), "
          f"{len(tex_written)} textures, placements for {len(out_pl)} dungeons")


if __name__ == "__main__":
    main()
