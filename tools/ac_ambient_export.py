#!/usr/bin/env python3
"""Extract the REAL per-terrain ambient soundscapes from the Region file (#770).

Chain (all inside client_portal.dat):
  Region SoundInfo  → 37 AmbientSTBDescs: a 0x20 SoundTable DID + entries
                      {SoundType, Volume, BaseChance, MinRate, MaxRate}
  Region SceneInfo  → SceneType.stbIndex → which STB a scene uses
  Region TerrainInfo→ terrain type → its SceneTypes

So every retail terrain type has an authored ambient set: which sounds play,
how loud, how often. We resolve each SoundType through the STB's own 0x20
SoundTable to real waves (ac_sound_export's proven parser/rewrapper), bucket
the retail terrain names into the client's six biomes, and bake:

  assets/acambient/<wave>.wav       shared, deduped PCM waves
  assets/acambient/index.json       {biomes:{forest:[{file,vol,chance,minRate,maxRate}...]}}

The client's AMBIENCE engine swaps its synth birdsong/owl blips for these
authored one-shots, scheduled with the retail chance/rate windows.
"""
import os, sys, json, re
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ac_model_export import DatReader
from ac_region_export import RBuf, read_landdefs, read_gametime, read_skyinfo
import importlib.util
spec = importlib.util.spec_from_file_location("snd", os.path.join(os.path.dirname(os.path.abspath(__file__)), "ac_sound_export.py"))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "acambient")

# retail terrain name → client biome (biomeAt keys); unmatched fall to forest
BIOME_RULES = [(r"snow|winter|frozen|ice", "cold"), (r"desert|sand|barren", "desert"),
               (r"swamp|marsh|mud", "swamp"), (r"volcan|obsidian|lava|dark|dire", "direlands"),
               (r"grass|plain|field|meadow", "forest"), (r"forest|tree|wood", "forest"),
               (r"mountain|rock", "cold")]


def main():
    import ac_sound_export as snd_mod  # noqa: F401 (imported for its parser below)
    from ac_sound_export import parse_soundtable, wave_to_wav
    portal = DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    r = RBuf(portal.read(0x13000000))
    r.u32(); r.u32(); r.u32()
    name = r.pstr(); r.align()
    read_landdefs(r); read_gametime(r)
    parts = r.u32()
    if parts & 0x10: read_skyinfo(r)
    assert parts & 0x01
    stbs = []
    for _ in range(r.u32()):
        stbid = r.u32()
        entries = []
        for _ in range(r.u32()):
            st = r.u32(); vol = r.f(); ch = r.f(); mn = r.f(); mx = r.f()
            entries.append(dict(t=st, vol=round(vol, 2), chance=round(ch, 3),
                                minRate=round(mn, 1), maxRate=round(mx, 1)))
        stbs.append(dict(stbid=stbid, entries=entries))
    assert parts & 0x02
    scenetypes = []
    for _ in range(r.u32()):
        stb = r.u32()
        scenes = [r.u32() for _ in range(r.i32())]
        scenetypes.append(stb)
    terrains = []
    for _ in range(r.u32()):
        tname = r.pstr(); r.align()
        r.u32()
        sidx = [r.u32() for _ in range(r.i32())]
        terrains.append((tname, sidx))
    print(f"region '{name}': {len(stbs)} STBs, {len(scenetypes)} scene types, {len(terrains)} terrains")

    os.makedirs(OUT, exist_ok=True)
    st_cache, written = {}, {}

    def resolve(stb):
        """STB → its entries with concrete wave files (best-probability wave per sound type)."""
        sid = stb["stbid"]
        if sid not in st_cache:
            try:
                st_cache[sid] = parse_soundtable(portal.read(sid))
            except Exception:
                st_cache[sid] = None
        table = st_cache[sid]
        if not table: return []
        out = []
        for e in stb["entries"]:
            waves = table.get(e["t"]) or table.get(0x80000000 | e["t"])
            if not waves: continue
            wid = max(waves, key=lambda w: w[1])[0]          # highest probability wave
            if wid not in written:
                try:
                    wav = wave_to_wav(portal.read(wid))
                except Exception:
                    wav = None
                if wav is None:
                    written[wid] = None
                else:
                    fn = "%08X.wav" % wid
                    open(os.path.join(OUT, fn), "wb").write(wav)
                    written[wid] = fn
            if written[wid]:
                out.append(dict(file=written[wid], vol=e["vol"], chance=e["chance"],
                                minRate=e["minRate"], maxRate=e["maxRate"]))
        return out

    biomes = OrderedDict((b, OrderedDict()) for b in ("forest", "cold", "desert", "swamp", "direlands", "sho"))
    for tname, sidx in terrains:
        biome = "forest"
        low = tname.lower()
        for pat, b in BIOME_RULES:
            if re.search(pat, low): biome = b; break
        for si in sidx[:2]:                                   # a terrain's primary scene flavours
            if si >= len(scenetypes): continue
            stb_i = scenetypes[si]
            if stb_i >= len(stbs): continue
            for e in resolve(stbs[stb_i]):
                if e["chance"] <= 0: continue                 # authored as never-plays
                key = (e["file"], e["chance"])
                if key not in biomes[biome]: biomes[biome][key] = e
    biomes["sho"] = dict(biomes["forest"])                    # Sho shares the temperate set
    outj = dict(region=name,
                biomes={b: list(v.values())[:10] for b, v in biomes.items()})
    json.dump(outj, open(os.path.join(OUT, "index.json"), "w"), separators=(",", ":"))
    nw = sum(1 for v in written.values() if v)
    for b, v in outj["biomes"].items():
        print(f"  {b:10s} {len(v)} ambient sounds")
    print(f"wrote {nw} waves + index → assets/acambient/")


if __name__ == "__main__":
    main()
