#!/usr/bin/env python3
"""Export the HUMAN MotionTable's gameplay animations for the player avatar.

The avatar wears the real AC body (assets/acmodels/human_*.json — array index IS
the setup part index). This tool reads the human MotionTable 0x09000001 and writes
assets/acmotions.json:

  { "nparts": N, "clips": { name: { "dur": secs, "t":[times...],
        "f":[ per-frame [ per-part [px,py,pz,qx,qy,qz,qw] ] ] } } }

Frames are SETUP-LOCAL (avatar-root-relative) in three-space (tx_pos/tx_quat),
exactly the same convention as the parts' placement frames in the model JSON —
the engine poses each part directly from them. Clips: idle/walk/run cycles,
sword & unarmed attack one-shots, the casting gesture, and death.
"""
import os, json, struct, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(ROOT, "tools", name + ".py"))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m
ame = load("ac_model_export")
ace = load("ac_creature_export")

HUMAN_MT = 0x09000001
MAX_FRAMES = 24

def main():
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    mt = ace.parse_motiontable(portal.read(HUMAN_MT))
    style = mt["default"] & 0xFFFF
    print("default style: 0x%X · cycles %d · link groups %d" % (style, len(mt["cycles"]), len(mt["links"])))

    def cycle_for(motion, styles):
        for st in styles:
            ad = mt["cycles"].get((st << 16) | motion)
            if ad: return ad
        for key, ad in mt["cycles"].items():
            if (key & 0xFFFF) == motion and ad: return ad
        return None
    def link_for(cmds, styles):
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
        times = []; seq = []; t = 0.0
        for ad in ad_list:
            try: frames = ace.parse_animation(portal.read(ad["a"]))
            except Exception: continue
            lo, hi = ad["lo"], ad["hi"]
            if hi < 0: hi = len(frames) - 1
            lo = max(0, min(lo, len(frames)-1)); hi = max(0, min(hi, len(frames)-1))
            sub = frames[lo:hi+1] if hi >= lo else list(reversed(frames[hi:lo+1]))
            fps = abs(ad["fps"]) or 15
            for fr in sub:
                seq.append(fr); times.append(round(t, 4)); t += 1.0/fps
        return times, seq
    def pack(ad_list):
        times, seq = seq_of(ad_list)
        if len(seq) < 2: return None
        step = max(1, (len(seq) + MAX_FRAMES - 1)//MAX_FRAMES)
        seq = seq[::step]; times = times[::step]
        dur = times[-1] + (times[1]-times[0] if len(times) > 1 else 0.1)
        f = []
        for fr in seq:
            row = []
            for p in fr:
                row.append([round(v, 4) for v in (ame.tx_pos(p["p"]) + ame.tx_quat(p["q"]))])
            f.append(row)
        return dict(dur=round(dur, 4), t=times, f=f)

    # MotionStyle preferences: 0x3E sword-and-shield ready, 0x3C unarmed, 0x47/0x49 magic
    ATTACK = [0x10000059, 0x10000058, 0x1000005A,            # weapon slash/backhand/thrust
              0x1000005B, 0x1000005C, 0x1000005D, 0x1000005E, 0x1000005F, 0x10000060]
    PUNCH  = [0x10000063, 0x10000062, 0x10000064, 0x10000066, 0x10000065, 0x10000067]
    clips = {}
    for name, motion, styles in (("idle", 0x03, [style]), ("walk", 0x05, [style]),
                                 ("run", 0x07, [style]),
                                 ("ready", 0x03, [0x3E, 0x3C])):     # combat stance cycle
        ad = cycle_for(motion, styles)
        if ad:
            c = pack(ad)
            if c: clips[name] = c
    for name, cmds, styles in (("attack", ATTACK, [0x3E, 0x40, 0x44, style]),
                               ("punch", PUNCH, [0x3C, style]),
                               ("cast", [0x400000D3], [0x47, 0x49, style]),
                               ("death", [0x40000011], [style, 0x3E, 0x3C])):
        ad = link_for(cmds, styles)
        if ad:
            c = pack(ad)
            if c: clips[name] = c
    nparts = max(len(c["f"][0]) for c in clips.values()) if clips else 0
    out = dict(nparts=nparts, clips=clips)
    path = os.path.join(ROOT, "assets", "acmotions.json")
    json.dump(out, open(path, "w"), separators=(",", ":"))
    for n, c in clips.items():
        print(f"  {n}: {len(c['f'])} frames · {c['dur']}s · {len(c['f'][0])} parts")
    print(f"wrote {path} ({os.path.getsize(path)//1024} KB)")

if __name__ == "__main__":
    main()
