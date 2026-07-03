#!/usr/bin/env python3
"""Export the authentic AC skill system from acdata/client_portal.dat.

  0x0E000004 SkillTable — every skill's attribute formula (attr1 [+attr2]) / divisor
             and its training / specialization skill-credit costs.
  0x0E000003 XpTable    — the real advancement charts: cumulative XP to raise each
             attribute point, vital point, trained skill rank, specialized skill
             rank, plus the character level XP ladder and skill credits per level.

Output: assets/acskills.json
  { "skills": { <gameKey>: {"a":[attrs], "d":divisor, "tc":train, "sc":spec} },
    "xp": { "attr":[...], "vital":[...], "trained":[...], "spec":[...],
            "level":[...], "credits":[...] } }

AC skill ids are mapped to the game's SKILLS_DEF keys; formats per ACEmulator.
"""
import struct, os, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
Buf = ame.Buf

ATTR = {1: "Strength", 2: "Endurance", 3: "Quickness", 4: "Coordination", 5: "Focus", 6: "Self"}
# AC skill id -> game SKILLS_DEF key
SKILL_KEY = {
    6:"meleedef", 7:"missiledef", 14:"arcane", 15:"magicdef", 16:"manaconv", 18:"itemtink",
    19:"assessperson", 20:"deception", 21:"healing", 22:"jump", 23:"lockpick", 24:"run",
    27:"assesscreature", 28:"weapontink", 29:"armortink", 30:"magictink", 31:"creature",
    32:"item", 33:"life", 34:"war", 35:"leadership", 36:"loyalty", 37:"fletching",
    38:"alchemy", 39:"cooking", 40:"salvaging", 41:"twohand", 43:"void", 44:"heavy",
    45:"light", 46:"finesse", 47:"missile", 48:"shield", 49:"dualwield", 50:"recklessness",
    51:"sneak", 52:"dirty", 54:"summon",
}

def pstring(r):
    n = r.u16()
    if n == 0xFFFF: n = r.u32()
    s = r.d[r.o:r.o+n].decode("latin-1"); r.o += n
    while r.o % 4: r.o += 1
    return s

def parse_skill_table(d):
    r = Buf(d); r.u32()
    count = r.u16(); r.u16()
    out = {}
    for _ in range(count):
        key = r.u32()
        pstring(r); name = pstring(r)
        r.u32(); tc = r.i32(); sc = r.i32(); r.u32(); r.u32(); r.u32()
        W, X, Y, Z, a1, a2 = [r.u32() for _ in range(6)]
        r.o += 24                                    # upper/lower bound, learn mod (doubles)
        attrs = [ATTR[a] for a in (a1, a2) if a in ATTR]
        gk = SKILL_KEY.get(key)
        if gk:
            out[gk] = dict(n=name, a=attrs, d=(Z if attrs else 0), tc=tc, sc=sc)
    return out

def parse_xp_table(d):
    r = Buf(d); r.u32()
    na, nv, nt, ns = r.i32(), r.i32(), r.i32(), r.i32()
    nl = r.u32()
    attr    = [r.u32() for _ in range(na + 1)]
    vital   = [r.u32() for _ in range(nv + 1)]
    trained = [r.u32() for _ in range(nt + 1)]
    spx     = [r.u32() for _ in range(ns + 1)]
    level   = [struct.unpack_from("<Q", r.d, r.o + 8*i)[0] for i in range(nl + 1)]; r.o += 8*(nl + 1)
    credits = [r.u32() for _ in range(nl + 1)]
    return dict(attr=attr, vital=vital, trained=trained, spec=spx, level=level, credits=credits)

def main():
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    skills = parse_skill_table(portal.read(0x0E000004))
    xp = parse_xp_table(portal.read(0x0E000018))   # EoL client: XpTable moved from 0x0E000003 (now a stub)
    out = dict(skills=skills, xp=xp)
    path = os.path.join(ROOT, "assets", "acskills.json")
    json.dump(out, open(path, "w"), separators=(",", ":"))
    print(f"skills: {len(skills)} mapped")
    print(f"xp charts: attr {len(xp['attr'])} · vital {len(xp['vital'])} · trained {len(xp['trained'])}"
          f" · spec {len(xp['spec'])} · levels {len(xp['level'])} · credits sum {sum(xp['credits'])}")
    print(f"trained rank 1/10/100: {xp['trained'][1]}/{xp['trained'][10]}/{xp['trained'][100] if len(xp['trained'])>100 else '-'}")
    print(f"level 2/10/50 XP: {xp['level'][2]:,}/{xp['level'][10]:,}/{xp['level'][50]:,}")
    print(f"wrote {path} ({os.path.getsize(path)//1024} KB)")

if __name__ == "__main__":
    main()
