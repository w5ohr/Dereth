#!/usr/bin/env python3
"""Export AC character HEAD models + their facial-feature textures from client_portal.dat.

Retail heads (CharGen 0x0E000002, see ac_chargen_export.py) are built like this, per
heritage x gender (SexCG):
  * setup part 16 IS the head; a hair STYLE swaps part-16's GfxObj (the hair is baked
    into the head geometry) AND swaps the hair texture on that mesh.
  * eyes / nose / mouth are TextureMapChanges on part 16, keyed by the ORIGINAL
    SurfaceTexture id (hair 0x05000098, eyes 0x0500024C, nose 0x050002F5, mouth
    0x0500025C). A feature strip replaces the slot texture; the mesh is unchanged.
So a head = one GfxObj (per hair style) + a set of slot textures chosen for
hair/eyes/nose/mouth, with skin/hair/eye palettes layered for colour.

This tool bakes:
  assets/acheads/<gfxhex>.json     # {groups:[{otex, v,n,uv,i}]}  otex = the group's
                                   #   original SurfaceTexture id (the swap key)
  assets/acheads/tex/<sthex>.png   # every referenced SurfaceTexture decoded (default palette)
  assets/acheads/index.json        # per heritage/gender: defaultHead, hairStyles, eyes,
                                   #   noses, mouths, skin/hair/eye palette DIDs, slots

Colour (skin/hair/eye palette compositing) is a follow-up; Phase-1 textures use each
SurfaceTexture's own palette, and the palette-DID lists are carried in the index.
"""
import os, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)
OUT = os.path.join(ROOT, "assets", "acheads")
TEXOUT = os.path.join(OUT, "tex")
# the four facial slots, by the original SurfaceTexture id the client swaps against
SLOTS = {"05000098": "hair", "0500024C": "eyes", "050002F5": "nose", "0500025C": "mouth"}

def main():
    os.makedirs(TEXOUT, exist_ok=True)
    portal = ame.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    from PIL import Image
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache: pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]

    # decode a SurfaceTexture (0x05) DID to a PNG in tex/, return filename (cached, None on failure)
    st_written = {}
    def write_st(sthex):
        if sthex in st_written: return st_written[sthex]
        try:
            renders = ame.parse_surfacetexture(portal.read(int(sthex, 16)))
            tid = renders[-1]
            dec = ame.decode_texture(portal.read(tid), palettes)
            if dec is None: st_written[sthex] = None
            else:
                w, h, px = dec
                fn = sthex + ".png"
                im = Image.frombytes("RGBA", (w, h), px)
                # AC head textures are paletted (<=256 colours). If fully opaque, store as an
                # indexed PNG (~1/4 the size); otherwise keep RGBA but PNG-optimize.
                if im.getchannel("A").getextrema()[0] == 255:
                    im = im.convert("RGB").quantize(colors=256, method=Image.MAXCOVERAGE)
                im.save(os.path.join(TEXOUT, fn), optimize=True)
                st_written[sthex] = fn
        except Exception:
            st_written[sthex] = None
        return st_written[sthex]

    # the SurfaceTexture id behind a Surface (0x08) DID, or None for a solid-colour surface
    surf_st = {}
    def st_of_surface(surfdid):
        if surfdid in surf_st: return surf_st[surfdid]
        try:
            s = ame.parse_surface(portal.read(surfdid))
            surf_st[surfdid] = ("%08X" % s["tex"]) if "tex" in s else None
        except Exception:
            surf_st[surfdid] = None
        return surf_st[surfdid]

    cg = json.load(open(os.path.join(ROOT, "assets", "acchargen.json")))

    # gather every unique head GfxObj + every slot texture we'll need to swap in
    head_gfx, slot_texs = set(), set()
    def note_od(od):
        for t in od.get("tex", []): slot_texs.add(t["new"])
        for a in od.get("anim", []):
            if a["part"] == 16: head_gfx.add(a["gfx"])
    for h in cg["heritages"]:
        for g in h["genders"]:
            if g.get("defaultHead"): head_gfx.add(g["defaultHead"])
            for hs in g["hairStyles"]: note_od(hs["od"])
            for e in g["eyes"]: note_od(e["od"]); note_od(e.get("odBald", {}))
            for n in g["noses"]: note_od(n["od"])
            for m in g["mouths"]: note_od(m["od"])

    # 1) export each unique head mesh, tagging every group with its original SurfaceTexture
    ok = fail = 0
    for ghex in sorted(head_gfx):
        path = os.path.join(OUT, ghex + ".json")
        if os.path.isfile(path): ok += 1; continue
        try:
            gfx = ame.parse_gfxobj(portal.read(int(ghex, 16)))
            groups = ame.build_part(gfx)
            gout = []
            for surfidx, g in sorted(groups.items()):
                if len(g["idx"]) < 3: continue
                surfdid = gfx["surfs"][surfidx] if 0 <= surfidx < len(gfx["surfs"]) else None
                otex = st_of_surface(surfdid) if surfdid else None
                if otex: write_st(otex)                      # the mesh's own default slot texture
                gout.append(dict(otex=otex, v=g["verts"], n=g["norms"], uv=g["uvs"], i=g["idx"]))
            json.dump(dict(groups=gout), open(path, "w"), separators=(",", ":"))
            ok += 1
        except Exception as e:
            fail += 1
    # 2) export every feature-strip / hair-style slot texture
    for st in sorted(slot_texs): write_st(st)

    # 3) index: per heritage/gender, the option lists in engine-ready form
    def strip_tex(od):        # first TextureMapChange's (old slot, new texture)
        for t in od.get("tex", []):
            if t["old"] in SLOTS: return SLOTS[t["old"]], t["new"]
        return None, None
    index = {}
    for h in cg["heritages"]:
        gens = {}
        for g in h["genders"]:
            styles = []
            for hs in g["hairStyles"]:
                slot, tex = strip_tex(hs["od"])                       # hair texture
                gfx = next((a["gfx"] for a in hs["od"]["anim"] if a["part"] == 16), g.get("defaultHead"))
                styles.append(dict(gfx=gfx, hairTex=tex, bald=hs["bald"]))
            eyes = []
            for e in g["eyes"]:
                _, t = strip_tex(e["od"]); _, tb = strip_tex(e.get("odBald", {}))
                eyes.append(dict(tex=t, texBald=tb))
            noses = [dict(tex=strip_tex(n["od"])[1]) for n in g["noses"]]
            mouths = [dict(tex=strip_tex(m["od"])[1]) for m in g["mouths"]]
            gens[g["name"].lower()] = dict(
                setupId=g["setupId"], defaultHead=g.get("defaultHead"),
                basePalette=g.get("basePalette"), skinPalSet=g.get("skinPalSet"),
                hairColors=g["hairColors"], eyeColors=g["eyeColors"],
                hairStyles=styles, eyes=eyes, noses=noses, mouths=mouths)
        index[h["name"].lower()] = gens
    json.dump(dict(slots=SLOTS, heritages=index), open(os.path.join(OUT, "index.json"), "w"),
              separators=(",", ":"))

    # ── stage 2: colour recolouring (runtime RGB-remap, not pre-baked) ────────
    # Every head texture indexes the master palette 0400007E, and each skin/hair/eye
    # "colour" is a full palette variant that differs from base only in its feature's
    # entries. So a colour = a compact old-RGB -> new-RGB remap: the engine loads the
    # default texture and re-maps the feature group's pixels (verified offline that a
    # remap reproduces a direct re-decode). Tiny data, no combinatorial explosion.
    base_pal = palettes(0x0400007E)
    def rgb(c): return ((c >> 16) & 0xFF) << 16 | ((c >> 8) & 0xFF) << 8 | (c & 0xFF)
    def remap_of(palhex):                # {oldRGBhex: newRGBhex} for entries that differ from base
        try: p = palettes(int(palhex, 16))
        except Exception: return None
        m = {}
        for i in range(min(len(base_pal), len(p))):
            if base_pal[i] != p[i]:
                m["%06x" % rgb(base_pal[i])] = "%06x" % rgb(p[i])
        return m or None
    def pset(did):
        d = portal.read(int(did, 16)); r = ame.Buf(d); r.u32(); n = r.u32()
        return ["%08X" % r.u32() for _ in range(n)]

    remaps = {}                          # palette DID -> remap dict (shared across features)
    for hn, gens in index.items():
        for gn, g in gens.items():
            g["skinTones"] = pset(g["skinPalSet"])                    # skin palette choices
            g["hairPals"] = [pset(c)[0] for c in g["hairColors"]]     # first shade of each hair-colour set
            for pd in g["skinTones"] + g["eyeColors"] + g["hairPals"]:
                if pd not in remaps:
                    rm = remap_of(pd)
                    if rm: remaps[pd] = rm
    json.dump(remaps, open(os.path.join(OUT, "palettes.json"), "w"), separators=(",", ":"))
    # rewrite index now that it carries skinTones/hairPals
    json.dump(dict(slots=SLOTS, heritages=index), open(os.path.join(OUT, "index.json"), "w"),
              separators=(",", ":"))
    psize = os.path.getsize(os.path.join(OUT, "palettes.json"))
    print(f"colour remaps: {len(remaps)} palettes, palettes.json {psize//1024} KB")

    ntex = sum(1 for v in st_written.values() if v)
    total = 0
    for root, _, files in os.walk(OUT):
        for f in files: total += os.path.getsize(os.path.join(root, f))
    print(f"head meshes: {ok} exported (failed {fail}) of {len(head_gfx)} unique")
    print(f"slot textures: {ntex} decoded ({len(st_written)-ntex} failed)")
    print(f"pack: {total//1024//1024} MB")

if __name__ == "__main__":
    main()
