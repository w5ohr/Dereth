#!/usr/bin/env python3
"""Extract the AUTHENTIC AC portal particle effect -> assets/acportalfx.json + sprite PNGs.

Retail portals are not a textured disc: the portal weenie's Setup (0x02...) carries a
DefaultScript / DefaultScriptTable whose PhysicsScript (0x33) fires CreateParticle hooks,
each pointing at a ParticleEmitterInfo (0x32) that streams sprites of a particle GfxObj
(0x01) textured from client_portal.dat. This tool walks that exact chain for the portal
setups used by the world's portal weenies:

    0x020001B3  the classic purple portal (1,708 weenies)
    0x020005D2..D6  the colored variants (Lightning/other ziggurat portals etc.)

SetupModel tail (after the placement frames parse_setup already handles): cyl-spheres,
spheres, height/radius/stepUp/stepDown, sorting+selection spheres, lights, then
DefaultAnimation, DefaultScript, DefaultMotionTable, DefaultSoundTable, DefaultScriptTable.

PhysicsScript (0x33): u32 id, u32 count of { double startTime; AnimationHook }.
AnimationHook: u32 type (masked 0x7FFFFFFF), i32 direction, payload; CreateParticle (13)
payload = emitterInfoId u32, partIndex u32, offset Frame (vec3 + quat WXYZ), emitterId u32.
PhysicsScriptTable (0x34): u32 id, u32 count of { u32 key; u32 n of { float mod; u32 scriptId } }.

ParticleEmitterInfo (0x32) is parsed IN FULL (the acparticles.json export dropped the
offset/velocity vectors): emitterType, particleType, gfxObj, birthrate, lifespan, offsetDir
+ min/maxOffset, A/B/C basis vectors + ranges (velocity/accel), scales, transparencies.

Output:
  assets/acportalfx.json   { "<setup-hex>": { script, emitters:[{...params, sprite, offset, quat}] } }
  assets/acportal/<gfxobj-hex>.png   the real particle sprite textures
"""
import importlib.util, os, json, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTJ = os.path.join(ROOT, "assets", "acportalfx.json")
OUTD = os.path.join(ROOT, "assets", "acportal")

_s = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(_s); _s.loader.exec_module(ame)
Buf = ame.Buf

PORTAL_SETUPS = [0x020001B3, 0x020005D2, 0x020005D3, 0x020005D5, 0x020005D6]

HOOK_PAYLOAD = {0:0,1:4,2:4,3:28,4:0,6:4,7:16,8:12,9:16,10:12,11:16,12:8,13:40,14:4,
                15:4,16:4,17:0,18:4,19:8,20:12,21:16,22:12,23:8,24:12,25:4,26:40}


def setup_tail(d):
    """Parse a full SetupModel; return the trailing default-script ids."""
    r = Buf(d)
    r.u32(); flags = r.u32()
    n = r.u32()
    [r.u32() for _ in range(n)]                          # parts
    if flags & 1: [r.u32() for _ in range(n)]            # parents
    if flags & 2: [r.f(3) for _ in range(n)]             # scales
    for _ in range(r.u32()): r.u32(); r.i32(); r.f(7)    # holding locations
    for _ in range(r.u32()): r.u32(); r.i32(); r.f(7)    # connection points
    for _ in range(r.i32()):                             # placement frames
        r.i32()
        for _ in range(n): r.f(7)                        # per-part frame
        nh = r.u32()
        if nh: _skip_hooks(r, nh)
    for _ in range(r.u32()): r.f(5)                      # cyl-spheres (origin, radius, height)
    for _ in range(r.u32()): r.f(4)                      # spheres
    r.f(4)                                               # height, radius, stepUp, stepDown
    r.f(4); r.f(4)                                       # sorting + selection spheres
    for _ in range(r.u32()): r.i32(); r.f(7); r.u32(); r.f(3)   # lights: id + frame + color + intensity/falloff/cone
    default_anim = r.u32(); default_script = r.u32()
    r.u32(); r.u32()                                     # motion table, sound table
    default_script_table = r.u32()
    return default_anim, default_script, default_script_table


def _skip_hooks(r, count):
    for _ in range(count):
        ht = r.u32() & 0x7FFFFFFF; r.i32()
        if ht == 5:
            r.u16()
            if r.u16() & 0x8000: r.u16()
        else:
            sz = HOOK_PAYLOAD.get(ht)
            if sz is None: raise ValueError("hook %d @%d" % (ht, r.o))
            r.o += sz


def parse_script(d):
    """PhysicsScript (0x33) -> [(startTime, emitterInfoId, partIndex, offset, quat, emitterId)]"""
    r = Buf(d)
    r.u32()
    out = []
    for _ in range(r.u32()):
        t = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8
        ht = r.u32() & 0x7FFFFFFF; r.i32()
        if ht == 13:
            emi = r.u32(); part = r.i32()
            pos = r.f(3); quat = r.f(4)
            eid = r.u32()
            out.append(dict(t=round(t, 3), emitter="%08X" % emi, part=part,
                            pos=[round(v, 3) for v in pos], quat=[round(v, 3) for v in quat], eid=eid))
        elif ht == 5:
            r.u16()
            if r.u16() & 0x8000: r.u16()
        else:
            sz = HOOK_PAYLOAD.get(ht)
            if sz is None: raise ValueError("script hook %d" % ht)
            r.o += sz
    return out


def parse_script_table(d):
    """PhysicsScriptTable (0x34) -> {key: [scriptIds by highest mod]}"""
    r = Buf(d)
    r.u32()
    table = {}
    for _ in range(r.u32()):
        key = r.u32()
        scripts = [(r.f(), r.u32()) for _ in range(r.u32())]
        table[key] = [sid for _, sid in scripts]
    return table


def parse_emitter(d):
    """ParticleEmitterInfo (0x32), full field set per ACE ParticleEmitterInfo.cs."""
    r = Buf(d)
    r.u32()
    unk = r.u32()                                        # emitter version/unknown
    etype = r.i32(); ptype = r.i32()
    gfx = r.u32(); hwgfx = r.u32()
    birthrate = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8
    maxp = r.i32(); initp = r.i32(); totp = r.i32()
    tsec = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8
    life = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8
    lifer = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8
    offdir = r.f(3); minoff = r.f(); maxoff = r.f()
    a = r.f(3); mina = r.f(); maxa = r.f()
    b = r.f(3); minb = r.f(); maxb = r.f()
    c = r.f(3); minc = r.f(); maxc = r.f()
    sscale = r.f(); fscale = r.f(); scaler = r.f()
    strans = r.f(); ftrans = r.f(); transr = r.f()
    rnd = lambda v: [round(x, 4) for x in v]
    return dict(etype=etype, ptype=ptype, gfx="%08X" % gfx, hwgfx="%08X" % hwgfx,
                birthrate=round(birthrate, 4), maxParticles=maxp, initialParticles=initp,
                totalSeconds=round(tsec, 2), lifespan=round(life, 3), lifespanRand=round(lifer, 3),
                offsetDir=rnd(offdir), minOffset=round(minoff, 3), maxOffset=round(maxoff, 3),
                vel=rnd(a), minVel=round(mina, 3), maxVel=round(maxa, 3),
                accB=rnd(b), minB=round(minb, 3), maxB=round(maxb, 3),
                accC=rnd(c), minC=round(minc, 3), maxC=round(maxc, 3),
                startScale=round(sscale, 3), finalScale=round(fscale, 3), scaleRand=round(scaler, 3),
                startTrans=round(strans, 3), finalTrans=round(ftrans, 3), transRand=round(transr, 3))


def sprite_png(portal, gfx_did, pal_cache):
    """Decode the particle GfxObj's first texture to a PNG in assets/acportal/."""
    try:
        g = ame.parse_gfxobj(portal.read(gfx_did))
    except Exception:
        return None
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]
    for sdid in g.get("surfs") or []:
        try:
            s = ame.parse_surface(portal.read(sdid))
        except Exception:
            continue
        if "tex" not in s: continue
        try:
            texids = ame.parse_surfacetexture(portal.read(s["tex"]))
            raw = portal.read(texids[-1])
            dec = ame.decode_texture(raw, palettes)
            if dec is None: continue
            w, h, px = dec
            fn = "%08x.png" % gfx_did
            _png(os.path.join(OUTD, fn), w, h, px)
            return fn
        except Exception:
            continue
    return None


def _png(path, w, h, px):
    import zlib
    def chunk(t, d): c = t + d; return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    raw = b"".join(b"\x00" + bytes(px[y*w*4:(y+1)*w*4]) for y in range(h))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    os.makedirs(OUTD, exist_ok=True)
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    pal_cache = {}
    out = {}
    for sid in PORTAL_SETUPS:
        try:
            da, ds, dst = setup_tail(portal.read(sid))
        except Exception as e:
            print("setup %08X: tail parse failed: %s" % (sid, e)); continue
        script_ids = []
        if ds: script_ids.append(ds)
        if dst:
            try:
                for k, sids in parse_script_table(portal.read(dst)).items():
                    script_ids += sids
            except Exception as e:
                print("  scriptTable %08X failed: %s" % (dst, e))
        hooks = []
        for scid in script_ids:
            try:
                hooks += parse_script(portal.read(scid))
            except Exception as e:
                print("  script %08X failed: %s" % (scid, e))
        emitters = []
        seen = set()
        for hk in hooks:
            did = int(hk["emitter"], 16)
            if did in seen: continue
            seen.add(did)
            try:
                em = parse_emitter(portal.read(did))
            except Exception as e:
                print("  emitter %08X failed: %s" % (did, e)); continue
            gfx_did = int(em["gfx"], 16) or int(em["hwgfx"], 16)   # ptype-9 emitters carry only the HW gfxobj
            em["sprite"] = gfx_did and sprite_png(portal, gfx_did, pal_cache) or None
            em["t"] = hk["t"]; em["pos"] = hk["pos"]; em["quat"] = hk["quat"]
            emitters.append(em)
        out["%08x" % sid] = dict(defaultScript="%08X" % ds if ds else None,
                                 scriptTable="%08X" % dst if dst else None,
                                 emitters=emitters)
        print("setup %08X: script=%08X table=%08X -> %d CreateParticle hooks, %d emitters"
              % (sid, ds, dst, len(hooks), len(emitters)))
    json.dump(out, open(OUTJ, "w"), separators=(",", ":"))
    print("wrote", OUTJ, "(%d KB)" % (os.path.getsize(OUTJ)//1024), "+ sprites in", OUTD)
    # show the classic purple portal's emitters
    key = "%08x" % PORTAL_SETUPS[0]
    for em in out.get(key, {}).get("emitters", [])[:6]:
        print("  ", json.dumps(em)[:220])


if __name__ == "__main__":
    main()
