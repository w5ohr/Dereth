#!/usr/bin/env python3
"""Extract AC particle emitter definitions from acdata/client_portal.dat.

The 0x32xxxxxx range holds ParticleEmitterInfo records (fixed 176-byte structs),
per the ACEmulator DatLoader FileTypes/ParticleEmitterInfo.cs reference:

  Id u32, unknown u32, EmitterType i32, ParticleType i32, GfxObjId u32, HwGfxObjId u32,
  Birthrate f64, MaxParticles i32, InitialParticles i32, TotalParticles i32,
  TotalSeconds f64, Lifespan f64, LifespanRand f64,
  OffsetDir vec3f, MinOffset f, MaxOffset f,
  A vec3f, MinA f, MaxA f,  B vec3f, MinB f, MaxB f,  C vec3f, MinC f, MaxC f,
  StartScale f, FinalScale f, ScaleRand f,
  StartTrans f, FinalTrans f, TransRand f,  IsParentLocal i32   (= 176 bytes)

NOTE on colours/gravity: AC's ParticleEmitterInfo does NOT store start/final colour or
a gravity vector — particle colour comes from the referenced GfxObj/Surface, and motion
is expressed as the three basis vectors A/B/C (each with min/max scalars) interpreted per
EmitterType. We therefore emit the authentic fields: lifespan (per-particle + total),
birthrate, start/final scale, start/final transparency (the real fade), and the A vector
(the closest thing to an acceleration/gravity term). Colour is left null with a note.

The 0x34xxxxxx PhysicsScriptTable + 0x33xxxxxx PhysicsScript + CreateParticleHook chain
map effects (spell/portal/impact) to these emitter DIDs, but the raw emitter-info records
are the requested deliverable; we extract all of them.

Output assets/acparticles.json:
  { note, count, emitters:{ "<did>": {emitterType, particleType, gfxObj, birthrate,
      totalSeconds, lifespan, lifespanRand, startScale, finalScale, scaleRand,
      startTrans, finalTrans, accel:[x,y,z], accelMin, accelMax,
      startColor:null, finalColor:null, gravity:null} } }
"""
import importlib.util, json, os, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "acparticles.json")
spec = importlib.util.spec_from_file_location("acme", os.path.join(ROOT, "tools", "ac_model_export.py"))
acme = importlib.util.module_from_spec(spec); spec.loader.exec_module(acme)

REC = 176

def r4(f): return round(f, 5)

def parse_emitter(d):
    if len(d) < REC:
        return None
    u = struct.unpack_from("<I I i i I I", d, 0)
    _id, _unk, etype, ptype, gfx, hwgfx = u
    birthrate = struct.unpack_from("<d", d, 24)[0]
    maxp, initp, totp = struct.unpack_from("<iii", d, 32)
    total_s, lifespan, lifespan_rand = struct.unpack_from("<ddd", d, 44)
    # vectors + scalars
    offdir = struct.unpack_from("<fff", d, 68)
    min_off, max_off = struct.unpack_from("<ff", d, 80)
    A = struct.unpack_from("<fff", d, 88); minA, maxA = struct.unpack_from("<ff", d, 100)
    # B at 108, C at 128 (not surfaced individually)
    start_scale, final_scale, scale_rand = struct.unpack_from("<fff", d, 148)
    start_trans, final_trans, trans_rand = struct.unpack_from("<fff", d, 160)
    is_local = struct.unpack_from("<i", d, 172)[0]
    return dict(emitterType=etype, particleType=ptype,
                gfxObj="%08X" % gfx if gfx else None,
                birthrate=r4(birthrate), totalSeconds=r4(total_s),
                lifespan=r4(lifespan), lifespanRand=r4(lifespan_rand),
                maxParticles=maxp, initialParticles=initp, totalParticles=totp,
                startScale=r4(start_scale), finalScale=r4(final_scale), scaleRand=r4(scale_rand),
                startTrans=r4(start_trans), finalTrans=r4(final_trans), transRand=r4(trans_rand),
                accel=[r4(A[0]), r4(A[1]), r4(A[2])], accelMin=r4(minA), accelMax=r4(maxA),
                startColor=None, finalColor=None, gravity=None)

def main():
    portal = acme.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    emitters, bad = {}, 0
    for oid in sorted(portal.files):
        if not (0x32000000 <= oid <= 0x32FFFFFF):
            continue
        try:
            e = parse_emitter(portal.read(oid))
        except Exception:
            e = None
        if e is None:
            bad += 1; continue
        emitters["%08X" % oid] = e

    out = {"note": ("AC ParticleEmitterInfo (0x32) stores no per-emitter colour or gravity; "
                    "colour derives from the referenced gfxObj and motion from basis vectors. "
                    "startColor/finalColor/gravity are null by design; startTrans/finalTrans are "
                    "the real particle fade and 'accel' is the A basis vector (accelMin/Max scalars)."),
           "count": len(emitters), "emitters": emitters}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(out, fh, separators=(",", ":"))
    print(f"emitters parsed: {len(emitters)}  malformed/skipped: {bad}")
    print(f"wrote {OUT} ({os.path.getsize(OUT)//1024} KB)")
    for did in list(emitters)[:3]:
        print(f"  {did}: {json.dumps(emitters[did])}")

if __name__ == "__main__":
    main()
