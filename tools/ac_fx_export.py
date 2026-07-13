#!/usr/bin/env python3
"""Bake AUTHORED spell/portal particle profiles from the dats (#769).

The visual chain for a war bolt is: spell (acspellfx projectile wcid) → the
ProjectileSpell weenie's DIDs — Setup (the glowing bolt model), int 30
PhysicsScript (the ProjectileCollision impact script), DID 22
PhysicsEffectTable (0x34: scriptType → power-graded 0x33 variants) — then each
0x33 PhysicsScript's CreateParticle hooks name 0x32 emitters, whose particle
COLOUR lives in the referenced GfxObj's surfaces.

This tool walks that chain for one representative retail projectile per client
element (+ the town Portal weenie and the SpellBase caster/target one-shots for
heal/buff/portal-cast) and reduces every emitter to the compact profile the
client's fxSparks/ELEM_FX system consumes:

  { color, n, size0, size1, life, fade0, fade1, spread, up }

Output assets/acfx.json:
  { elements: { fire|ice|shock|acid|force: {color, bolt, impact:[parts], flight:[parts]} },
    portal:   { color, use:[parts] },
    oneshots: { heal|buff|portalcast: {color, parts:[...]} } }

Colours are the mean RGB of each emitter GfxObj's surfaces (solid colour or
decoded texture average) — the authentic palette, not our guesses.
"""
import os, sys, json, struct, re, glob

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ac_model_export as ame
from ac_model_export import DatReader, Buf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEENIE = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core", "9 WeenieDefaults", "SQL")
OUT = os.path.join(ROOT, "assets", "acfx.json")

# client element -> retail projectile weenie (ProjectileSpell class, verified names)
ELEMENTS = {"fire": "Flame Bolt", "ice": "Frost Bolt", "shock": "Lightning Bolt",
            "acid": "Acid Stream", "force": "Force Bolt"}

HOOK_PAYLOAD = {0:0,1:4,2:4,3:28,4:0,6:4,7:16,8:12,9:16,10:12,11:16,12:8,13:40,14:4,
                15:4,16:4,17:0,18:4,19:8,20:12,21:16,22:12,23:8,24:12,25:4,26:40}


def parse_script(portal, did):
    """0x33 PhysicsScript → [(startTime, emitterDID)] from CreateParticle hooks."""
    r = Buf(portal.read(did))
    r.u32(); n = r.u32()
    out = []
    for _ in range(n):
        t = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8
        ht = r.u32() & 0x7FFFFFFF; r.i32()
        if ht == 13:
            em = r.u32(); r.u32(); r.f(7); r.u32()
            out.append((t, em))
        elif ht == 5:
            r.u16()
            if r.u16() & 0x8000: r.u16()
        else:
            sz = HOOK_PAYLOAD.get(ht)
            if sz is None: raise ValueError("hook %d" % ht)
            r.o += sz
    return out


def parse_script_table(portal, did):
    """0x34 PhysicsScriptTable → {scriptType: [(mod, scriptDID)]}."""
    r = Buf(portal.read(did))
    r.u32(); n = r.u32()
    out = {}
    for _ in range(n):
        st = r.u32(); nm = r.u32()
        out[st] = [(r.f(), r.u32()) for _ in range(nm)]
    return out


EMITTER_REC = 176
def parse_emitter(portal, did):
    d = portal.read(did); r = Buf(d)
    r.u32(); r.u32(); etype = r.i32(); ptype = r.i32()
    gfx = r.u32(); r.u32()
    birth = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8
    maxp = r.i32(); r.i32(); r.i32()
    total = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8
    life = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8
    struct.unpack_from("<d", r.d, r.o); r.o += 8              # lifespanRand
    r.f(3); r.f(); maxoff = r.f()
    a = r.f(3); r.f(); amax = r.f()
    r.f(3); r.f(); r.f(); r.f(3); r.f(); r.f()
    s0 = r.f(); s1 = r.f(); r.f()
    t0 = r.f(); t1 = r.f(); r.f(); r.i32()
    return dict(gfx=gfx, birth=birth, maxp=maxp, total=total, life=life,
                maxoff=maxoff, up=a[2] * amax, s0=s0, s1=s1, t0=t0, t1=t1)


def mean_color(portal, gfx_did, palettes, _cache={}):
    """mean RGB of a GfxObj's surfaces (solid colour or decoded-texture average)."""
    if gfx_did in _cache: return _cache[gfx_did]
    col = 0x9ab0ff
    try:
        gfx = ame.parse_gfxobj(portal.read(gfx_did))
        rs = gs = bs = n = 0
        for sid in gfx["surfs"]:
            try:
                s = ame.parse_surface(portal.read(sid))
                if "color" in s:
                    c = s["color"]
                    rs += (c >> 16) & 255; gs += (c >> 8) & 255; bs += c & 255; n += 1
                else:
                    tid = ame.parse_surfacetexture(portal.read(s["tex"]))[-1]
                    dec = ame.decode_texture(portal.read(tid), palettes)
                    if dec:
                        w, h, px = dec
                        step = max(4, (w * h) // 256) * 4
                        rr = gg = bb = m = 0
                        for i in range(0, w * h * 4, step):
                            if px[i + 3] < 32: continue
                            rr += px[i]; gg += px[i + 1]; bb += px[i + 2]; m += 1
                        if m: rs += rr // m; gs += gg // m; bs += bb // m; n += 1
            except Exception:
                continue
        if n:
            r2, g2, b2 = rs // n, gs // n, bs // n
            # these sprites render ADDITIVE in AC, so a texture mean reads far darker than
            # the on-screen glow — normalise hue-preserving (max channel → 255)
            mx = max(r2, g2, b2, 1)
            if mx < 250:
                r2 = min(255, r2 * 255 // mx); g2 = min(255, g2 * 255 // mx); b2 = min(255, b2 * 255 // mx)
            col = (r2 << 16) | (g2 << 8) | b2
    except Exception:
        pass
    _cache[gfx_did] = col
    return col


def clamp(v, lo, hi): return max(lo, min(hi, v))


def profile(portal, palettes, script_did):
    """0x33 script → list of compact client particle parts."""
    parts = []
    try:
        hooks = parse_script(portal, script_did)
    except Exception:
        return parts
    for t, em_did in hooks:
        try:
            em = parse_emitter(portal, em_did)
        except Exception:
            continue
        life = clamp(em["life"] if em["life"] > 0 else 0.6, 0.15, 2.0)
        window = em["total"] if 0 < em["total"] < 3 else life
        n = int(clamp(em["birth"] * window if em["birth"] > 0 else em["maxp"], 3, 48))
        parts.append(dict(t=round(t, 2), color=mean_color(portal, em["gfx"], palettes),
                          n=n, size0=round(clamp(em["s0"], 0.05, 3.0), 2),
                          size1=round(clamp(em["s1"] if em["s1"] > 0 else em["s0"], 0.02, 3.0), 2),
                          life=round(life, 2), fade0=round(clamp(em["t0"], 0, 1), 2),
                          fade1=round(clamp(em["t1"], 0, 1), 2),
                          spread=round(clamp(em["maxoff"], 0, 3.0), 2),
                          up=round(clamp(em["up"], -12, 12), 2)))
    return parts


def weenie_props(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    did = dict(re.findall(r"\(\d+,\s*(\d+),\s*(0x[0-9A-Fa-f]+|\d+)\)\s*/\*", src))
    return did


def find_weenie(cls, name):
    for p in glob.glob(os.path.join(WEENIE, cls, "**", "*.sql"), recursive=True):
        if re.match(r"\d+ %s\.sql$" % re.escape(name), os.path.basename(p)):
            return p
    return None


def main():
    portal = DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]

    out = dict(elements={}, portal=None, oneshots={})
    for el, wname in ELEMENTS.items():
        p = find_weenie("ProjectileSpell", wname)
        if not p:
            print(f"  {el}: weenie '{wname}' not found"); continue
        props = weenie_props(p)
        setup = int(props.get("1", "0"), 0); pscript = int(props.get("30", "0"), 0)
        efftab = int(props.get("22", "0"), 0)
        rec = dict(color=None, bolt=None, impact=[], flight=[])
        if setup:                                              # bolt tint = the model's own surfaces
            try:
                su = ame.parse_setup(portal.read(setup))
                cols = [c for c in (mean_color(portal, g, palettes) for g in su["parts"][:3]) if c]
                if cols:
                    rec["bolt"] = cols[0]
            except Exception:
                pass
        if pscript:
            rec["impact"] = profile(portal, palettes, 0x33000000 | pscript)
        if efftab:
            try:
                tab = parse_script_table(portal, efftab)
                # flight/trail: the non-collision entry at full power (mod 1.0)
                for st, mods in sorted(tab.items()):
                    if st == pscript: continue
                    full = [d for m, d in mods if m >= 0.99] or [mods[-1][1]]
                    fl = profile(portal, palettes, full[0])
                    if fl: rec["flight"] = fl; break
            except Exception:
                pass
        # element identity: the collision script is SHARED across elements (ProjectileCollision 90),
        # so the signature colour comes from the element's own flight emitters / bolt model
        pool = rec["flight"] or rec["impact"]
        rec["color"] = pool[0]["color"] if pool else rec["bolt"]
        if rec["bolt"] is None: rec["bolt"] = rec["color"]
        for pt in rec["impact"]: pt["color"] = rec["color"]     # tint the shared explosion per element
        out["elements"][el] = rec
        print(f"  {el:6s} {wname:15s} impact {len(rec['impact'])} parts, flight {len(rec['flight'])}, color #{(rec['color'] or 0):06X}")

    # the town portal's own swirl: portal weenies carry no script DIDs — the FX is the
    # Setup's DEFAULT PhysicsScript, stored in the SetupModel's 6-u32 tail (slot 2)
    p = find_weenie("Portal", "Town of Dryreach") or find_weenie("Portal", "Surface")
    if p:
        props = weenie_props(p)
        setup = int(props.get("1", "0"), 0)
        parts = []
        if setup:
            try:
                raw = portal.read(setup)
                # default PhysicsScript = slot 2 of the SetupModel's LAST 6 words (holds for
                # both the 6-word and 18-word tail variants — verified against the dat)
                t2 = struct.unpack_from("<I", raw, len(raw) - 16)[0]
                if (t2 >> 24) == 0x33:
                    parts = profile(portal, palettes, t2)
            except Exception:
                pass
        if parts:
            out["portal"] = dict(color=parts[0]["color"], use=parts)
            print(f"  portal {os.path.basename(p):32s} {len(parts)} parts, color #{parts[0]['color']:06X}")

    # SpellBase one-shot effects (enchantment-style): PScript enum id -> 0x33000000|id
    for key, sid_hex in (("heal", 0x33000000 | 0x1F), ("portalcast", 0x33000000 | 0x10)):
        try:
            parts = profile(portal, palettes, sid_hex)
        except Exception:
            parts = []
        if parts:
            out["oneshots"][key] = dict(color=parts[0]["color"], parts=parts)
            print(f"  {key}: {len(parts)} parts, color #{parts[0]['color']:06X}")

    json.dump(out, open(OUT, "w"), separators=(",", ":"))
    print(f"wrote {OUT} ({os.path.getsize(OUT)} bytes)")


if __name__ == "__main__":
    main()
