#!/usr/bin/env python3
"""Extract the AC Region file (DID 0x13000000) scenery + sky/lighting tables.

The Region file in client_portal.dat is the single RegionDesc for Dereth. Per the
ACEmulator DatLoader reference (BSD), it packs, in order:
  Id, RegionNumber, Version, RegionName(pstr), LandDefs, GameTime, PartsMask,
  [SkyInfo], [SoundInfo], [SceneInfo], TerrainInfo, [RegionMisc]
(each [..] gated by a PartsMask bit). We walk the whole thing sequentially -- the
tables are variable-length, so exact field order (verified against ACE source) is the
only safe way to reach TerrainInfo, which sits after Sky/Sound/Scene.

Two authentic tables come out:
  assets/acscenery.json  per-terrain flora/rock spawn table. TerrainInfo gives 32
                         terrain types, each naming SceneType indices into SceneInfo;
                         each SceneType lists Scene DIDs (0x12xxxxxx); each Scene file
                         lists ObjectDescs (a Setup/GfxObj DID + spawn Freq + scale range).
  assets/acsky.json      day/night keyframes from SkyInfo: for the dominant DayGroup,
                         each SkyTimeOfDay keyframe's sun(dir light)/ambient/fog colors,
                         fog near/far, and a computed sun direction.

Reuses DatReader + Buf from ac_model_export.py.
"""
import os, sys, json, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ac_model_export import DatReader, Buf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD  = os.path.join(ROOT, "acdata")
OUT  = os.path.join(ROOT, "assets")

REGION_ID = 0x13000000


class RBuf(Buf):
    """Buf + the extra primitives the Region file needs (double, pstring, dword align)."""
    def f64(s):
        v = struct.unpack_from("<d", s.d, s.o)[0]; s.o += 8; return v

    def pstr(s):                      # ReadPString: u16 length, then bytes (Encoding.Default)
        n = s.u16()
        v = s.d[s.o:s.o + n]; s.o += n
        return v.decode("latin-1", "replace")

    def align(s):                     # AlignBoundary: pad to 4-byte boundary
        d = s.o % 4
        if d: s.o += 4 - d


def argb(c):
    """AC packs colors as 0xAARRGGBB uint32 -> [r,g,b] 0..255."""
    return [(c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF]


# ── sub-table readers (field order per ACE DatLoader) ───────────────────────
def read_landdefs(r):
    # 8 scalars + 256-float LandHeightTable
    r.i32(); r.i32(); r.f(); r.i32(); r.i32(); r.f(); r.f(); r.f()
    for _ in range(256):
        r.f()


def read_gametime(r):
    r.f64(); r.u32(); r.f(); r.u32()      # ZeroTimeOfYear, ZeroYear, DayLength, DaysPerYear
    r.pstr(); r.align()                    # YearSpec
    for _ in range(r.u32()):               # TimesOfDay: u32 count
        r.f(); r.u32(); r.pstr(); r.align()   # Start, IsNight, Name
    for _ in range(r.u32()):               # DaysOfTheWeek: u32 count
        r.pstr(); r.align()
    for _ in range(r.u32()):               # Seasons: u32 count
        r.u32(); r.pstr(); r.align()          # StartDate, Name


def read_skyinfo(r):
    """SkyDesc -> list of DayGroups; each DayGroup has SkyTimeOfDay keyframes."""
    r.f64(); r.f64(); r.align()            # TickSize, LightTickSize
    daygroups = []
    for _ in range(r.u32()):               # DayGroups: u32 count
        chance = r.f()
        name = r.pstr(); r.align()
        for _ in range(r.u32()):           # SkyObjects: u32 count
            r.f(); r.f(); r.f(); r.f(); r.f(); r.f()   # Begin/End Time, Begin/End Angle, TexVel X/Y
            r.u32(); r.u32(); r.u32()                    # DefaultGFXObjectId, DefaultPESObjectId, Properties
            r.align()
        keys = []
        for _ in range(r.u32()):           # SkyTime: u32 count of SkyTimeOfDay
            k = {}
            k["begin"]      = r.f()
            k["dirBright"]  = r.f()
            k["dirHeading"] = r.f()
            k["dirPitch"]   = r.f()
            k["dirColor"]   = r.u32()
            k["ambBright"]  = r.f()
            k["ambColor"]   = r.u32()
            k["minFog"]     = r.f()
            k["maxFog"]     = r.f()
            k["fogColor"]   = r.u32()
            k["worldFog"]   = r.u32()
            r.align()
            for _ in range(r.u32()):       # SkyObjReplace: u32 count of SkyObjectReplace
                r.u32(); r.u32(); r.f(); r.f(); r.f(); r.f(); r.align()
            keys.append(k)
        daygroups.append(dict(chance=chance, name=name, keys=keys))
    return daygroups


def read_soundinfo(r):
    for _ in range(r.u32()):               # STBDesc: u32 count of AmbientSTBDesc
        r.u32()                            # STBId
        for _ in range(r.u32()):           # AmbientSounds: u32 count of AmbientSoundDesc
            r.u32(); r.f(); r.f(); r.f(); r.f()   # SType, Volume, BaseChance, Min/MaxRate


def read_sceneinfo(r):
    """SceneDesc -> list of SceneType, each = {stbIndex, scenes:[DID,...]}."""
    scenetypes = []
    for _ in range(r.u32()):               # SceneTypes: u32 count
        stb = r.u32()
        scenes = [r.u32() for _ in range(r.i32())]   # Scenes: List<uint> = i32 count, uint elems
        scenetypes.append(dict(stb=stb, scenes=scenes))
    return scenetypes


def read_terraininfo(r):
    """TerrainDesc.TerrainTypes -> [{name, color, sceneTypes:[idx,...]}]. Stops before LandSurfaces."""
    terrains = []
    for _ in range(r.u32()):               # TerrainTypes: u32 count
        name = r.pstr(); r.align()
        color = r.u32()
        scene_idx = [r.u32() for _ in range(r.i32())]   # SceneTypes: List<uint> = i32 count
        terrains.append(dict(name=name, color=color, sceneTypes=scene_idx))
    return terrains


def read_scene_objects(portal, did):
    """A Scene file (0x12xxxxxx): Id + List<ObjectDesc>."""
    r = RBuf(portal.read(did))
    r.u32()                                # Id
    objs = []
    for _ in range(r.u32()):               # Objects: u32 count of ObjectDesc
        oid = r.u32()
        r.f(3); r.f(4)                     # BaseLoc: Frame = Vector3 origin + quaternion (7 floats)
        freq   = r.f()
        r.f(); r.f()                       # DisplaceX, DisplaceY
        minsc  = r.f()
        maxsc  = r.f()
        r.f()                              # MaxRotation
        r.f(); r.f()                       # MinSlope, MaxSlope
        r.u32(); r.u32(); r.u32()          # Align, Orient, WeenieObj
        objs.append(dict(setup="%08X" % oid, freq=round(freq, 5),
                         minScale=round(minsc, 5), maxScale=round(maxsc, 5)))
    return objs


def main():
    portal = DatReader(os.path.join(ACD, "client_portal.dat"))
    r = RBuf(portal.read(REGION_ID))
    n = len(r.d)

    # ---- region header ----
    r.u32()                                # Id
    region_num = r.u32()
    version = r.u32()
    region_name = r.pstr(); r.align()

    read_landdefs(r)
    read_gametime(r)
    parts = r.u32()

    daygroups = []
    if parts & 0x10:  daygroups   = read_skyinfo(r)
    if parts & 0x01:  read_soundinfo(r)
    scenetypes = []
    if parts & 0x02:  scenetypes  = read_sceneinfo(r)
    terrains = read_terraininfo(r)         # always present

    print(f"region '{region_name}' num={region_num} ver={version} partsMask=0x{parts:X}")
    print(f"consumed {r.o}/{n} bytes of region file (LandSurfaces/RegionMisc left unparsed -- not needed)")
    print(f"daygroups={len(daygroups)}  sceneTypes={len(scenetypes)}  terrainTypes={len(terrains)}")

    # ---- acscenery.json ----
    scene_cache = {}
    def scene_objs(did):
        if did not in scene_cache:
            try:
                scene_cache[did] = read_scene_objects(portal, did)
            except Exception as e:
                scene_cache[did] = None
                print(f"  scene 0x{did:08X} FAILED: {e}")
        return scene_cache[did]

    terrains_out = {}
    for ti, t in enumerate(terrains):
        scenes_out = []
        for st_idx in t["sceneTypes"]:
            if st_idx >= len(scenetypes):
                continue
            for did in scenetypes[st_idx]["scenes"]:
                objs = scene_objs(did)
                if not objs:
                    continue
                scenes_out.append(dict(scene="%08X" % did, weight=1, objects=objs))
        entry = dict(name=t["name"], scenes=scenes_out)
        terrains_out[str(ti)] = entry

    json.dump(dict(region=region_name, terrains=terrains_out),
              open(os.path.join(OUT, "acscenery.json"), "w"), separators=(",", ":"))

    # ---- acsky.json ----
    # A DayGroup is a weather variant (clear, cloudy, ...) with a ChanceOfOccur; each holds
    # a SkyTimeOfDay keyframe list. We export the dominant (highest-chance) group as the
    # canonical day/night curve, and list the other groups' names for reference.
    times = []
    used_group = None
    if daygroups:
        used_group = max(range(len(daygroups)), key=lambda i: daygroups[i]["chance"])
        import math
        for k in daygroups[used_group]["keys"]:
            h, p = k["dirHeading"], k["dirPitch"]
            # sun/moon direction: heading about vertical (z-up), pitch above horizon
            sun_dir = [round(math.cos(p) * math.sin(h), 4),
                       round(math.cos(p) * math.cos(h), 4),
                       round(math.sin(p), 4)]
            times.append(dict(
                t=round(k["begin"], 5),
                # AC has no flat sky-background color (sky is a textured dome); fog color is
                # the authentic horizon/atmosphere tint, used here as the sky proxy.
                sky=argb(k["fogColor"]),
                fog=argb(k["fogColor"]),
                fogNear=round(k["minFog"], 3),
                fogFar=round(k["maxFog"], 3),
                sun=argb(k["dirColor"]),
                sunBright=round(k["dirBright"], 4),
                ambient=argb(k["ambColor"]),
                ambBright=round(k["ambBright"], 4),
                sunDir=sun_dir,
            ))
    sky_out = dict(
        times=times,
        dayGroups=[dict(name=d["name"], chance=round(d["chance"], 4), keyframes=len(d["keys"]))
                   for d in daygroups],
        usedDayGroup=(daygroups[used_group]["name"] if used_group is not None else None),
    )
    json.dump(sky_out, open(os.path.join(OUT, "acsky.json"), "w"), separators=(",", ":"))

    # ---- report ----
    with_scenes = sum(1 for e in terrains_out.values() if e["scenes"])
    print(f"\nacscenery.json: {len(terrains_out)} terrain types, {with_scenes} with scenes")
    for ti, e in terrains_out.items():
        nobj = sum(len(s["objects"]) for s in e["scenes"])
        print(f"  [{ti:>2}] {e['name']:<22} scenes={len(e['scenes']):>3}  objects={nobj}")
    print(f"\nacsky.json: {len(times)} keyframes in dominant group '{sky_out['usedDayGroup']}'")


if __name__ == "__main__":
    main()
