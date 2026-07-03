#!/usr/bin/env python3
"""Extract AC character-generation data from acdata/client_portal.dat.

Parses CharGen (DID 0x0E000002) per the ACEmulator DatLoader reference
(FileTypes/CharGen.cs + the Entity/*CG.cs classes):

  CharGen: Id u32; skip 4; StarterAreas SmartArray; skip 1; HeritageGroups SmartArray
           (key u32 -> HeritageGroupCG).
  StarterArea: Name(.NET BinaryReader string = LEB128 length + UTF-8); Locations
           SmartArray of Position (u32 cell + 7 floats = 32 bytes).
  HeritageGroupCG: Name, IconImage, SetupID, EnvironmentSetupID, AttributeCredits,
           SkillCredits, PrimaryStartAreas(List<int>), SecondaryStartAreas(List<int>),
           Skills(List<SkillCG>), Templates(List<TemplateCG>), skip 1, Genders(Dict<int,SexCG>).
  SexCG: Name, Scale, SetupID, SoundTable, IconImage, BasePalette, SkinPalSet, PhysicsTable,
           MotionTable, CombatTable, BaseObjDesc, HairColorList(List<uint>),
           HairStyleList(List<HairStyleCG>), EyeColorList(List<uint>), EyeStripList,
           NoseStripList, MouthStripList, HeadgearList, ShirtList, PantsList, FootwearList,
           ClothingColorsList(List<uint>).
  Counts on SmartArrays are AC compressed-uint; ObjDesc aligns to 4 bytes.

The heritage/gender appearance tables are: SkinPalSet (a PaletteSet DID expanding to the
skin-tone range), HairColorList (hair palette DIDs), EyeColorList (eye palette DIDs), and
HairStyleList (hair-style setups). Correctness is asserted by exact byte consumption.

Output assets/acchargen.json:
  { starterAreas:[names],
    heritages:[{id, name, startAttr, skillCredits, primaryStartAreas:[names],
                secondaryStartAreas:[names], skills:[{skill,normalCost,primaryCost}],
                templates:[names],
                genders:[{name, scale, setupId, skinPalSet, hairColors:[dids],
                          eyeColors:[dids], hairStyles:n}]}],
    skin:[skinPalSet dids...], hair:[hair palette dids...] }
"""
import importlib.util, json, os, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "acchargen.json")
CHARGEN_DID = 0x0E000002
spec = importlib.util.spec_from_file_location("acme", os.path.join(ROOT, "tools", "ac_model_export.py"))
acme = importlib.util.module_from_spec(spec); spec.loader.exec_module(acme)
Buf = acme.Buf

def f64(r):
    v = struct.unpack_from("<d", r.d, r.o)[0]; r.o += 8; return v

def rstr(r):                     # .NET BinaryReader.ReadString: LEB128 length + UTF-8
    length = shift = 0
    while True:
        b = r.u8()
        length |= (b & 0x7F) << shift
        if not (b & 0x80): break
        shift += 7
    s = r.d[r.o:r.o + length].decode("utf-8", "replace"); r.o += length
    return s

def align4(r): r.o = (r.o + 3) & ~3

def rdid(r, known):              # ReadAsDataIDOfKnownType
    v = r.u16()
    if v & 0x8000:
        lower = r.u16(); higher = (v & 0x3FFF) << 16
        return known + (higher | lower)
    return known + v

def smart_count(r): return r.cuint()

def objdesc(r):
    align4(r)
    r.u8()                                   # discarded (0x11)
    npal, ntex, nanim = r.u8(), r.u8(), r.u8()
    pal = rdid(r, 0x04000000) if npal > 0 else 0
    for _ in range(npal): rdid(r, 0x04000000); r.u8(); r.u8()       # SubPalette
    for _ in range(ntex): r.u8(); rdid(r, 0x05000000); rdid(r, 0x05000000)  # TextureMapChange
    for _ in range(nanim): r.u8(); rdid(r, 0x01000000)             # AnimationPartChange
    align4(r)
    return pal

def hairstyle(r):
    r.u32(); r.u8(); r.u32(); objdesc(r)          # IconImage, Bald(byte), AlternateSetup, ObjDesc

def eyestrip(r):
    r.u32(); r.u32(); objdesc(r); objdesc(r)     # IconImage, IconImageBald, ObjDesc, ObjDescBald

def facestrip(r):
    r.u32(); objdesc(r)                          # IconImage, ObjDesc

def gear(r):
    rstr(r); r.u32(); r.u32()                     # Name, ClothingTable, WeenieDefault

def skillcg(r):
    return dict(skill=r.u32(), normalCost=r.i32(), primaryCost=r.i32())

def templatecg(r):
    name = rstr(r)
    for _ in range(8): r.u32()                    # icon,title,str,end,coord,quick,focus,self
    for _ in range(smart_count(r)): r.u32()       # NormalSkillsList
    for _ in range(smart_count(r)): r.u32()       # PrimarySkillsList
    return name

def sexcg(r):
    name = rstr(r)
    scale = r.u32(); setup = r.u32(); sound = r.u32(); icon = r.u32()
    base_pal = r.u32(); skin_pal_set = r.u32(); r.u32(); r.u32(); r.u32()  # phys,motion,combat
    objdesc(r)                                             # BaseObjDesc
    hair_colors = [r.u32() for _ in range(smart_count(r))]
    n_hair = smart_count(r)
    for _ in range(n_hair): hairstyle(r)
    eye_colors = [r.u32() for _ in range(smart_count(r))]
    for _ in range(smart_count(r)): eyestrip(r)            # EyeStripList
    for _ in range(smart_count(r)): facestrip(r)           # NoseStripList
    for _ in range(smart_count(r)): facestrip(r)           # MouthStripList
    for _ in range(smart_count(r)): gear(r)                # HeadgearList
    for _ in range(smart_count(r)): gear(r)                # ShirtList
    for _ in range(smart_count(r)): gear(r)                # PantsList
    for _ in range(smart_count(r)): gear(r)                # FootwearList
    clothing_colors = [r.u32() for _ in range(smart_count(r))]
    return dict(name=name, scale=scale, setupId="%08X" % setup,
                skinPalSet="%08X" % skin_pal_set,
                hairColors=["%08X" % h for h in hair_colors],
                eyeColors=["%08X" % e for e in eye_colors],
                hairStyles=n_hair, clothingColors=len(clothing_colors))

def heritagecg(r):
    name = rstr(r)
    icon = r.u32(); setup = r.u32(); env = r.u32()
    attr = r.u32(); skill = r.u32()
    primary = [r.i32() for _ in range(smart_count(r))]
    secondary = [r.i32() for _ in range(smart_count(r))]
    skills = [skillcg(r) for _ in range(smart_count(r))]
    templates = [templatecg(r) for _ in range(smart_count(r))]
    r.u8()                                                 # skip 1 byte (0x01)
    genders = []
    for _ in range(smart_count(r)):
        r.i32()                                            # gender key
        genders.append(sexcg(r))
    return dict(name=name, startAttr=attr, skillCredits=skill,
                primaryStartAreas=primary, secondaryStartAreas=secondary,
                skills=skills, templates=templates, genders=genders)

def parse_chargen(d):
    r = Buf(d)
    r.u32()                                                # Id
    r.o += 4                                               # skip 4
    starter = []
    for _ in range(smart_count(r)):
        nm = rstr(r)
        nloc = smart_count(r)
        r.o += 32 * nloc                                   # Positions
        starter.append(nm)
    r.u8()                                                 # skip 1 byte
    heritages = []
    for _ in range(smart_count(r)):
        r.u32()                                            # heritage key
        heritages.append(heritagecg(r))
    return starter, heritages, r.o

def main():
    portal = acme.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    d = portal.read(CHARGEN_DID)
    starter, heritages, consumed = parse_chargen(d)
    clean = (consumed == len(d))

    def area_names(idxs): return [starter[i] for i in idxs if 0 <= i < len(starter)]
    for h in heritages:
        h["primaryStartAreas"] = area_names(h["primaryStartAreas"])
        h["secondaryStartAreas"] = area_names(h["secondaryStartAreas"])

    skin = sorted({g["skinPalSet"] for h in heritages for g in h["genders"] if g["skinPalSet"] != "00000000"})
    hair = sorted({c for h in heritages for g in h["genders"] for c in g["hairColors"]})

    out = {"starterAreas": starter, "heritages": heritages, "skin": skin, "hair": hair,
           "clean": clean, "bytesConsumed": consumed, "fileSize": len(d)}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False, separators=(",", ":"))

    print(f"byte consumption: {consumed}/{len(d)}  {'CLEAN' if clean else 'MISALIGNED'}")
    print(f"starter areas: {starter}")
    print(f"heritages: {len(heritages)}")
    for h in heritages:
        g = h["genders"]
        print(f"  {h['name']:14s} startAttr={h['startAttr']} skillCredits={h['skillCredits']} "
              f"start={h['primaryStartAreas']} genders={[x['name'] for x in g]} "
              f"skills={len(h['skills'])} templates={len(h['templates'])}")
        for x in g:
            print(f"       {x['name']:8s} skinPalSet={x['skinPalSet']} hairColors={len(x['hairColors'])} "
                  f"eyeColors={len(x['eyeColors'])} hairStyles={x['hairStyles']}")
    print(f"wrote {OUT} ({os.path.getsize(OUT)//1024} KB)")

if __name__ == "__main__":
    main()
