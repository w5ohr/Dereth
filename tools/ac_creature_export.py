#!/usr/bin/env python3
"""Export real AC creatures as GLB models with their ORIGINAL animations.

Reuses the portal.dat parsers from ac_model_export.py. For each bestiary kind, the ACE
world DB dump (acdata/ACE-World-Database-v0.9.294.sql) names a representative creature
weenie whose DIDs give the Setup (0x02 model) and MotionTable (0x09). The motion table's
cycles yield the authentic Idle (Ready) and Walk / Run loops; its links (keyed
style<<16|fromMotion -> full MotionCommand) yield the one-shot melee Attack, Death and
CastSpell animations (0x03 files: per-frame, per-part position+quaternion in setup
space, with animation hooks skipped via a fixed payload-size table).

Each creature becomes a self-contained GLB: one node per moving part (meshes with the
original textures embedded as PNGs), plus AnimationClips named "Idle", "Walk", "Attack",
"Death", "Cast" whose TRS channels replay the original keyframes. These drop straight
into the game's existing MONSTER_MODELS/GLTFLoader/AnimationMixer pipeline (clips are
matched by /idle/ /walk/ /attack/ /death|die/ /cast/).

Output: assets/models/monsters/ac/<kind>.glb
"""
import struct, os, re, json, sys, base64, importlib.util
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
Buf = ame.Buf
ACEDB = os.path.join(ROOT, "acdata", "ACE-World-Database-v0.9.294.sql")
OUT = os.path.join(ROOT, "assets", "models", "monsters", "ac")

# kind -> representative weenie name (plain retail creatures; ACE DIDs 1=Setup, 2=MotionTable)
KINDS = {
 "drudge":"Drudge Slinker","olthoi":"Olthoi Soldier","olthoisoldier":"Olthoi Soldier",
 "olthoiworker":"Olthoi Worker","skeleton":"Skeleton","virindi":"Virindi Servant",
 "tusker":"Tusker Guard","gromnie":"Gromnie","banderling":"Banderling Guard",
 "mosswart":"Mosswart","reedshark":"Reedshark","auroch":"Auroch","mattekar":"Mattekar",
 "lugian":"Lugian Commander","tumerok":"Tumerok Gladiator","rat":"Silver Rat",
 "zombie":"Zombie","golem":"Sandstone Golem","monouga":"Bloodthirsty Monouga",
 "shreth":"Carrion Shreth","wasp":"Phyntos Wasp","wisp":"Wisp","ursuin":"Ursuin",
 "moarsman":"Fetid Moarsman","sclavus":"Sata Sclavus","mite":"Mite Scamp",
 "niffis":"Niffis","carenzi":"Carenzi","armoredillo":"Armoredillo","siraluun":"Siraluun",
 "grievver":"Grievver","eater":"Eater","chittick":"Chittick","burun":"Burun Ruuk Lout",
 # second wave: remaining bestiary kinds with a real (non-human-setup) retail model
 "shadow":"Umbris Shadow","zefir":"Sufut Zefir","remoran":"Gold Remoran",
 "mukkir":"Mukkir","rabbit":"White Rabbit",
 # named bosses
 "olthoiqueen":"Olthoi Queen","lichlord":"Lich Lord","virindidirector":"Virindi Director",
 # third wave — the creature deep-dive: every remaining classic family with a real
 # (non-human-rig) retail setup, representatives verified against placement counts
 "wight":"Frozen Wight","lich":"Lich","hollow":"Hollow Minion","thrungus":"Thrungus",
 "ruschk":"Vile Ruschk","gurog":"Gurog Soldier","sleech":"Parfal Sleech",
 "fiun":"Frenzied Fiun","marionette":"Marionette","doll":"Pristine Doll",
 "penguin":"Aggressive Penguin","crystalwisp":"Crystalline Wisp","statue":"Statue",
 "margul":"Stalking Margul","phantom":"Phantom","elemental":"Water Elemental",
 "snowman":"Snowman","chicken":"Chicken","viamontian":"Viamontian Knight",
 "falatacot":"Falatacot Consort","tormented":"Tormented Attendant","hea":"Hea Hunter",
 "gearknight":"Gold Gear Knight","cow":"Cow",
}

HOOK_PAYLOAD = {0:0,1:4,2:4,3:28,4:0,6:4,7:16,8:12,9:16,10:12,11:16,12:8,13:40,14:4,
                15:4,16:4,17:0,18:4,19:8,20:12,21:16,22:12,23:8,24:12,25:4,26:40}
def skip_hooks(r, n):
    for _ in range(n):
        ht = r.u32() & 0x7FFFFFFF; r.i32()          # type + direction
        if ht == 5:                                  # ReplaceObject: u16 part + packed DID
            r.u16()
            if r.u16() & 0x8000: r.u16()
        else:
            sz = HOOK_PAYLOAD.get(ht)
            if sz is None: raise ValueError("hook %d @%d" % (ht, r.o))
            r.o += sz

def parse_animation(d):
    r = Buf(d)
    r.u32(); flags = r.u32(); nparts = r.u32(); nframes = r.u32()
    if flags & 1:
        for _ in range(nframes): r.f(7)              # root-motion PosFrames (unused here)
    frames = []
    for _ in range(nframes):
        fr = [dict(p=r.f(3), q=r.f(4)) for _ in range(nparts)]
        skip_hooks(r, r.u32())
        frames.append(fr)
    return frames

def parse_motiontable(d):
    r = Buf(d)
    r.u32(); default = r.u32()
    styledef = {}
    for _ in range(r.u32()):
        k = r.u32(); styledef[k] = r.u32()
    def motiondata():
        na = r.u8(); r.u8(); fl = r.u8()
        while r.o % 4: r.o += 1
        anims = [dict(a=r.u32(), lo=r.i32(), hi=r.i32(), fps=r.f()) for _ in range(na)]
        if fl & 1: r.f(3)
        if fl & 2: r.f(3)
        return anims
    cycles = {}
    for _ in range(r.u32()):
        k = r.u32(); cycles[k] = motiondata()
    for _ in range(r.u32()):                         # modifiers (unused)
        r.u32(); motiondata()
    links = {}                                       # (style<<16|fromMotion) -> {MotionCommand: anims}
    for _ in range(r.u32()):
        k = r.u32(); sub = {}
        for _ in range(r.u32()):
            sk = r.u32(); sub[sk] = motiondata()
        links[k] = sub
    return dict(default=default, styledef=styledef, cycles=cycles, links=links)

def creature_dids():
    """kind -> (setupId, motionTableId) from the ACE world DB dump: creature weenies
    (type 10) matched by display name — exact first, then 'Name ...' / '... Name'."""
    if not os.path.isfile(ACEDB):
        return creature_dids_split()               # fall back to the 3-Core split DB
    re_weenie = re.compile(r"\((\d+),'(?:[^'\\]|\\.)*',(\d+),'[^']*'\)")
    re_str = re.compile(r"\((\d+),(\d+),(\d+),'((?:[^'\\]|\\.)*)'\)")
    re_did = re.compile(r"\((\d+),(\d+),(\d+),(\d+)\)")
    creatures = set(); names = {}; setup = {}; mtab = {}
    with open(ACEDB, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("INSERT INTO `weenie` "):
                for wid, wtype in re_weenie.findall(line):
                    if wtype in ("10", "15"): creatures.add(int(wid))   # Creature + Cow
            elif line.startswith("INSERT INTO `weenie_properties_string` "):
                for _, oid, t, val in re_str.findall(line):
                    if t == "1": names[int(oid)] = val.replace("\\'", "'")
            elif line.startswith("INSERT INTO `weenie_properties_d_i_d` "):
                for _, oid, t, val in re_did.findall(line):
                    if t == "1": setup[int(oid)] = int(val)
                    elif t == "2": mtab[int(oid)] = int(val)
    byname = {}                                     # display name -> lowest-wcid (setup, mt)
    for wcid in sorted(creatures):
        n = names.get(wcid); s = setup.get(wcid); m = mtab.get(wcid)
        if not n or not s or not m: continue
        if s == 0x02000001: continue                # human setup = not a real creature model
        byname.setdefault(n, (s, m))
    allnames = sorted(byname)
    out = {}
    for kind, pat in KINDS.items():
        got = byname.get(pat)
        if not got:
            for n in allnames:
                if n.startswith(pat + " ") or n.endswith(" " + pat):
                    got = byname[n]; break
        if got: out[kind] = got
    if "olthoisoldier" in out and "olthoi" not in out: out["olthoi"] = out["olthoisoldier"]
    return out

def creature_dids_split():
    """Same resolution against the 3-Core split DB (per-weenie SQL files) when the
    monolithic dump isn't present: exact display-name file first, then prefix/suffix
    variants; human-setup impostors rejected."""
    base = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core",
                        "9 WeenieDefaults", "SQL", "Creature")
    allfiles = []
    for root, _, files in os.walk(base):
        for fn in files:
            m = re.match(r"\d+ (.+)\.sql$", fn)
            if m: allfiles.append((m.group(1), os.path.join(root, fn)))
    def dids_of(path):
        src = open(path, encoding="utf-8", errors="replace").read()
        blk = re.search(r"weenie_properties_d_i_d.*?;", src, re.S)
        d = dict(re.findall(r"\(\d+,\s*(\d+),\s*(0x[0-9A-Fa-f]+)\)", blk.group(0))) if blk else {}
        if d.get("1") and d.get("2"):
            s = int(d["1"], 16)
            if s != 0x02000001:
                return (s, int(d["2"], 16))
        return None
    byname = {n: p for n, p in allfiles}
    out = {}
    for kind, pat in KINDS.items():
        got = byname.get(pat) and dids_of(byname[pat])
        if not got:
            for n, p in sorted(allfiles):
                if n.startswith(pat + " ") or n.endswith(" " + pat):
                    got = dids_of(p)
                    if got: break
        if got: out[kind] = got
    if "olthoisoldier" in out and "olthoi" not in out: out["olthoi"] = out["olthoisoldier"]
    return out

# ── minimal GLB writer ────────────────────────────────────────────────────
class Glb:
    def __init__(s):
        s.bin = bytearray()
        s.j = dict(asset=dict(version="2.0", generator="ac_creature_export"),
                   scene=0, scenes=[dict(nodes=[0])], nodes=[], meshes=[], materials=[],
                   textures=[], images=[], samplers=[dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)],
                   accessors=[], bufferViews=[], buffers=[], animations=[])
    def view(s, data, target=None):
        while len(s.bin) % 4: s.bin.append(0)
        v = dict(buffer=0, byteOffset=len(s.bin), byteLength=len(data))
        if target: v["target"] = target
        s.bin += data
        s.j["bufferViews"].append(v)
        return len(s.j["bufferViews"]) - 1
    def acc(s, data, ctype, comp, count, target=None, minmax=None):
        a = dict(bufferView=s.view(data, target), componentType=comp, count=count, type=ctype)
        if minmax: a["min"], a["max"] = minmax
        s.j["accessors"].append(a)
        return len(s.j["accessors"]) - 1
    def floats(s, vals, ctype, target=None, minmax=False):
        n = {"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4}[ctype]
        data = struct.pack("<%df" % len(vals), *vals)
        mm = None
        if minmax:
            cols = [vals[i::n] for i in range(n)]
            mm = ([min(c) for c in cols], [max(c) for c in cols])
        return s.acc(data, ctype, 5126, len(vals)//n, target, mm)
    def write(s, path):
        s.j["buffers"] = [dict(byteLength=len(s.bin))]
        while len(s.bin) % 4: s.bin.append(0)
        jb = json.dumps(s.j, separators=(",", ":")).encode()
        while len(jb) % 4: jb += b" "
        out = struct.pack("<3I", 0x46546C67, 2, 28 + len(jb) + len(s.bin))
        out += struct.pack("<2I", len(jb), 0x4E4F534A) + jb
        out += struct.pack("<2I", len(s.bin), 0x004E4942) + bytes(s.bin)
        open(path, "wb").write(out)

def export_creature(kind, setup_id, mt_id, portal, texbytes, variant=None):
    # variant (breeds, tools/ac_breed_export.py): {"swaps":{partIdx:gfxDID},
    # "texmap":{oldSurfaceTex:newSurfaceTex}} — the palette recolor rides in via a
    # composed texbytes["_pal"] (ClothingTable CloSubPalEffects over the base palette)
    setup = ame.parse_setup(portal.read(setup_id))
    nparts = len(setup["parts"])
    glb = Glb()
    matcache = {}
    def material(sid):
        if sid in matcache: return matcache[sid]
        s = ame.parse_surface(portal.read(sid))
        if "color" in s:
            c = s["color"]
            mat = dict(pbrMetallicRoughness=dict(
                baseColorFactor=[((c>>16)&255)/255, ((c>>8)&255)/255, (c&255)/255, 1], metallicFactor=0.0, roughnessFactor=0.8),
                doubleSided=True)
        else:
            stex = s["tex"]
            if variant and variant.get("texmap"): stex = variant["texmap"].get(stex, stex)
            tid = ame.parse_surfacetexture(portal.read(stex))[-1]
            if tid not in texbytes:
                dec = ame.decode_texture(portal.read(tid), texbytes["_pal"])
                texbytes[tid] = None
                if dec:
                    from PIL import Image
                    import io
                    w, h, px = dec
                    bio = io.BytesIO(); Image.frombytes("RGBA", (w, h), px).save(bio, "PNG")
                    texbytes[tid] = bio.getvalue()
            png = texbytes[tid]
            if png is None:
                mat = dict(pbrMetallicRoughness=dict(baseColorFactor=[0.6,0.6,0.6,1], metallicFactor=0.0, roughnessFactor=0.8), doubleSided=True)
            else:
                img = glb.view(png)
                glb.j["images"].append(dict(bufferView=img, mimeType="image/png"))
                glb.j["textures"].append(dict(sampler=0, source=len(glb.j["images"])-1))
                mat = dict(pbrMetallicRoughness=dict(
                    baseColorTexture=dict(index=len(glb.j["textures"])-1), metallicFactor=0.0, roughnessFactor=0.8),
                    doubleSided=True)
                if s.get("clip"): mat["alphaMode"] = "MASK"; mat["alphaCutoff"] = 0.45
        glb.j["materials"].append(mat)
        matcache[sid] = len(glb.j["materials"]) - 1
        return matcache[sid]
    # nodes: 0 = root; part i -> node index (or None for empty/unparsable parts)
    glb.j["nodes"].append(dict(name="root", children=[]))
    part_node = [None]*nparts
    for i, gid in enumerate(setup["parts"]):
        if variant and variant.get("swaps") and i in variant["swaps"]: gid = variant["swaps"][i]
        try:
            gfx = ame.parse_gfxobj(portal.read(gid))
        except Exception:
            continue
        if not gfx["polys"]: continue
        groups = ame.build_part(gfx)
        prims = []
        for surfidx, g in sorted(groups.items()):
            if len(g["idx"]) < 3: continue
            sid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
            pos = glb.floats(g["verts"], "VEC3", 34962, minmax=True)
            nrm = glb.floats(g["norms"], "VEC3", 34962)
            uv = glb.floats(g["uvs"], "VEC2", 34962)
            idx = glb.acc(struct.pack("<%dH" % len(g["idx"]), *g["idx"]), "SCALAR", 5123, len(g["idx"]), 34963)
            prims.append(dict(attributes=dict(POSITION=pos, NORMAL=nrm, TEXCOORD_0=uv), indices=idx,
                              material=(material(sid) if sid else 0)))
        if not prims: continue
        glb.j["meshes"].append(dict(primitives=prims))
        fr = setup["frames"][i] if setup["frames"] else dict(p=[0,0,0], q=[1,0,0,0])
        p = ame.tx_pos(fr["p"]); q = ame.tx_quat(fr["q"])
        node = dict(name="part%d" % i, mesh=len(glb.j["meshes"])-1, translation=p, rotation=q)
        glb.j["nodes"].append(node)
        part_node[i] = len(glb.j["nodes"]) - 1
        glb.j["nodes"][0]["children"].append(part_node[i])
    if not glb.j["meshes"]:
        return None
    # animations from the motion table: Ready(3)=Idle, WalkForward(5)/RunForward(7)=Walk
    # from cycles; melee attacks / Dead / CastSpell from links (one-shot actions).
    try:
        mt = parse_motiontable(portal.read(mt_id))
    except Exception:
        mt = None
    def cycle_for(motion):
        if not mt: return None
        style = mt["default"] & 0xFFFF
        for key, anims in mt["cycles"].items():
            if (key & 0xFFFF) == motion and ((key >> 16) & 0xFFFF) == style and anims:
                return anims
        for key, anims in mt["cycles"].items():
            if (key & 0xFFFF) == motion and anims:
                return anims
        return None
    def link_for(cmds, styles):
        """MotionData for the first of cmds reachable from Ready, preferring styles order."""
        if not mt: return None
        for st in styles:
            sub = mt["links"].get((st << 16) | 0x0003)
            if not sub: continue
            for c in cmds:
                if sub.get(c): return sub[c]
        for key in sorted(mt["links"]):
            for c in cmds:
                if mt["links"][key].get(c): return mt["links"][key][c]
        return None
    def seq_of(ad_list):
        """AnimData chain -> (times, frames): each anim's lo..hi slice at its own fps."""
        times = []; seq = []; t = 0.0
        for ad in ad_list:
            try:
                frames = parse_animation(portal.read(ad["a"]))
            except Exception:
                continue
            lo, hi = ad["lo"], ad["hi"]
            if hi < 0: hi = len(frames) - 1
            lo = max(0, min(lo, len(frames)-1)); hi = max(0, min(hi, len(frames)-1))
            sub = frames[lo:hi+1] if hi >= lo else list(reversed(frames[hi:lo+1]))
            fps = abs(ad["fps"]) or 15
            for fr in sub:
                seq.append(fr); times.append(t); t += 1.0/fps
        return times, seq
    def add_clip(clip_name, ad_list):
        times, seq = seq_of(ad_list)
        if len(seq) < 2: return
        step = max(1, len(seq)//20)
        seq = seq[::step]; times = times[::step]
        tacc = glb.floats(times, "SCALAR", minmax=True)
        anim = dict(name=clip_name, samplers=[], channels=[])
        for i in range(nparts):
            node = part_node[i]
            if node is None or i >= len(seq[0]): continue
            tr = []; ro = []
            for fr in seq:
                tr += ame.tx_pos(fr[i]["p"]); ro += ame.tx_quat(fr[i]["q"])
            si = len(anim["samplers"])
            anim["samplers"].append(dict(input=tacc, output=glb.floats(tr, "VEC3"), interpolation="LINEAR"))
            anim["channels"].append(dict(sampler=si, target=dict(node=node, path="translation")))
            anim["samplers"].append(dict(input=tacc, output=glb.floats(ro, "VEC4"), interpolation="LINEAR"))
            anim["channels"].append(dict(sampler=si+1, target=dict(node=node, path="rotation")))
        if anim["channels"]:
            glb.j["animations"].append(anim)
    for clip_name, motions in (("Idle", (3,)), ("Walk", (5, 7))):
        ad = None
        for mo in motions:
            ad = cycle_for(mo)
            if ad: break
        if ad: add_clip(clip_name, ad)
    # full 32-bit MotionCommands as stored in link sub-keys
    ATTACK = [0x10000063, 0x10000062, 0x10000064,    # unarmed AttackMed/High/Low 1
              0x10000066, 0x10000065, 0x10000067,    # ...2
              0x10000069, 0x10000068, 0x1000006A,    # ...3
              0x10000059, 0x10000058, 0x1000005A,    # weapon swings (slash/backhand/thrust)
              0x1000005B, 0x1000005C, 0x1000005D, 0x1000005E, 0x1000005F, 0x10000060]
    style = (mt["default"] & 0xFFFF) if mt else 0
    MELEE = [0x3C, 0x3E, 0x40, 0x44, 0x3F, 0x41, style]     # stances: unarmed first
    for clip_name, cmds, styles in (("Attack", ATTACK, MELEE),
                                    ("Death", [0x40000011], [style] + MELEE),
                                    ("Cast", [0x400000D3], [0x47, style])):
        ad = link_for(cmds, styles)
        if ad: add_clip(clip_name, ad)
    if not glb.j["animations"]:
        del glb.j["animations"]
    path = os.path.join(OUT, kind + ".glb")
    glb.write(path)
    return path

def main():
    os.makedirs(OUT, exist_ok=True)
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]
    texbytes = {"_pal": palettes}
    dids = creature_dids()
    print(f"creature weenies resolved: {len(dids)}")
    ok = 0
    for kind in sorted(dids):
        sid, mid = dids[kind]
        try:
            path = export_creature(kind, sid, mid, portal, texbytes)
        except Exception as e:
            print(f"  {kind}: FAILED ({e})"); continue
        if path:
            j = json.loads(open(path, "rb").read()[20:20+struct.unpack('<I', open(path,'rb').read()[12:16])[0]].decode())
            anims = [a["name"] for a in j.get("animations", [])]
            print(f"  {kind}: setup 0x{sid:08X} mt 0x{mid:08X} -> {os.path.getsize(path)//1024}KB, clips {anims}")
            ok += 1
    print(f"\nwrote {ok} creatures to {OUT}")

if __name__ == "__main__":
    main()
