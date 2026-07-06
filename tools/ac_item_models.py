#!/usr/bin/env python3
"""Export the real 3D models of retail weapons & casters from client_portal.dat.

acitems.json (ace_item_export.py) now carries each item's Setup DID. This tool
bakes every unique weapon/launcher/caster setup into one mesh JSON: all parts'
GfxObjs transformed by their placement frames into a single group list (same
{groups:[{mat,v,n,uv,i}]} format the armor/avatar loaders use; textures shared
into assets/acmodels/tex/).

Output:
  assets/acitemmodels/<setuphex>.json
  assets/acitemmodels/index.json   { lowercase item name: setuphex }
"""
import os, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
OUT = os.path.join(ROOT, "assets", "acitemmodels")
# every catalogued class with a Setup bakes now — weapons were the pilot; clothing/armor,
# gems, food, keys, components, ammo, healing kits, mana stones and lockpicks join them
CATS = ("MeleeWeapon", "MissileLauncher", "Caster", "Clothing", "Gem", "Food", "Key",
        "SpellComponent", "Ammunition", "Missile", "Healer", "ManaStone", "Lockpick")

def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx,
            aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz)
def qrot(q, v):
    qv = (q[0], q[1], q[2]); t = [2*(qv[1]*v[2]-qv[2]*v[1]), 2*(qv[2]*v[0]-qv[0]*v[2]), 2*(qv[0]*v[1]-qv[1]*v[0])]
    return (v[0]+q[3]*t[0]+qv[1]*t[2]-qv[2]*t[1],
            v[1]+q[3]*t[1]+qv[2]*t[0]-qv[0]*t[2],
            v[2]+q[3]*t[2]+qv[0]*t[1]-qv[1]*t[0])

def main():
    os.makedirs(OUT, exist_ok=True)
    texdir = os.path.join(ROOT, "assets", "acmodels", "tex"); os.makedirs(texdir, exist_ok=True)
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    from PIL import Image
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]
    tex_written = {}
    def surface_material(sid):
        s = ame.parse_surface(portal.read(sid))
        if "color" in s: return dict(color=s["color"] & 0xFFFFFF)
        tid = ame.parse_surfacetexture(portal.read(s["tex"]))[-1]
        if tid not in tex_written:
            fn = "%08X.png" % tid
            if not os.path.isfile(os.path.join(texdir, fn)):
                dec = ame.decode_texture(portal.read(tid), palettes)
                if dec is None: tex_written[tid] = None
                else:
                    w, h, px = dec
                    Image.frombytes("RGBA", (w, h), px).save(os.path.join(texdir, fn))
                    tex_written[tid] = fn
            else: tex_written[tid] = fn
        out = dict(tex=tex_written[tid]) if tex_written[tid] else dict(color=0x888888)
        if s.get("clip") and "tex" in out: out["clip"] = 1
        return out

    items = json.load(open(os.path.join(ROOT, "assets", "acitems.json")))
    index = {}
    setups = {}
    for name, v in items.items():
        if v.get("setup") and v.get("t") in CATS:
            index[name] = v["setup"]
            setups.setdefault(v["setup"], name)
    # housing furnishings (assets/acfurnishings.json, tools/ace_furnishing_export.py):
    # keyed by the GAME's furniture names so hooks resolve them like any retail item
    furn_path = os.path.join(ROOT, "assets", "acfurnishings.json")
    if os.path.isfile(furn_path):
        for name, shex in json.load(open(furn_path)).items():
            index[name] = shex
            setups.setdefault(shex, name)
    # Shields — NOT in acitems.json (the upstream WeenieDefaults dump lacked them); their Setup DIDs
    # were recovered from the ACE-World DB (weenie_properties_d_i_d type 1). One mesh per shield TYPE,
    # keyed "shield:<type>" so the game resolves every named shield through its `shield:` field.
    SHIELDS = {"shield:round":"02000162","shield:buckler":"02000162","shield:kite":"02000164",
               "shield:large":"02000164","shield:tower":"02000161","shield:olthoi":"02000161",
               "shield:covenant":"02001AB6","shield:aegis":"02001AB6"}
    for name, shex in SHIELDS.items():
        index[name] = shex
        setups.setdefault(shex, name)
    done = fails = 0
    for shex in sorted(setups):
        path = os.path.join(OUT, shex + ".json")
        if os.path.isfile(path): done += 1; continue
        try:
            sid = int(shex, 16)
            if (sid >> 24) == 0x01:     # some items point straight at a GfxObj
                parts = [dict(gid=sid, p=(0,0,0), q=(0,0,0,1))]
            else:
                su = ame.parse_setup(portal.read(sid))
                frames = su.get("frames") or []
                parts = []
                for i, gid in enumerate(su["parts"]):
                    fr = frames[i] if i < len(frames) else None
                    p = ame.tx_pos(fr["p"]) if fr else (0,0,0)
                    q = ame.tx_quat(fr["q"]) if fr else (0,0,0,1)
                    parts.append(dict(gid=gid, p=p, q=q))
            gout = []
            for prt in parts:
                gfx = ame.parse_gfxobj(portal.read(prt["gid"]))
                groups = ame.build_part(gfx)
                for surfidx, g in sorted(groups.items()):
                    if len(g["idx"]) < 3: continue
                    v2 = []; n2 = []
                    for k in range(0, len(g["verts"]), 3):
                        x, y, z = g["verts"][k:k+3]
                        rx, ry, rz = qrot(prt["q"], (x, y, z))
                        v2 += [round(rx+prt["p"][0], 4), round(ry+prt["p"][1], 4), round(rz+prt["p"][2], 4)]
                        nx, ny, nz = g["norms"][k:k+3]
                        rn = qrot(prt["q"], (nx, ny, nz))
                        n2 += [round(rn[0], 4), round(rn[1], 4), round(rn[2], 4)]
                    ssid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                    gout.append(dict(mat=(surface_material(ssid) if ssid else dict(color=0x888888)),
                                     v=v2, n=n2, uv=g["uvs"], i=g["idx"]))
            if not gout: fails += 1; continue
            json.dump(dict(groups=gout), open(path, "w"), separators=(",", ":"))
            done += 1
        except Exception:
            fails += 1
    # only index items whose mesh actually exported
    index = {n: s for n, s in index.items() if os.path.isfile(os.path.join(OUT, s + ".json"))}
    json.dump(index, open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print(f"item models: {done} exported (failed {fails}) · {len(index):,} item names indexed · pack {total//1024//1024} MB")

if __name__ == "__main__":
    main()
