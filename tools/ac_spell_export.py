#!/usr/bin/env python3
"""Extract the authentic AC spell roster from acdata/client_portal.dat.

Parses the SpellTable (DID 0x0E00000E) and SpellComponentTable (DID 0x0E00000F)
per the ACEmulator DatLoader reference (BSD-licensed):
  - SpellBase: obfuscated (nibble-swapped) name/desc strings padded to DWORD
    boundaries, then school/icon/category/bitfield/mana/range/power/economy/
    formulaVersion/componentLoss/metaSpellType/metaSpellId, conditional
    duration (Enchantment=1, FellowEnchantment=12) or portalLifetime
    (PortalSummon=7), 8 encrypted formula component uints, then caster/target/
    fizzle effects, recovery, displayOrder, nonComponentTargetType, manaMod.
  - Formula decryption (SpellBase.DecryptFormula): key = hash(name) % 0x12107680
    + hash(desc) % 0xBEADCF45, where hash is the classic AC string hash over
    cp1252 *signed* bytes; comp = raw - key, and any result > 198 is masked
    with 0xFF (fixes spells with extended characters).
  - SpellComponentBase: name, category, icon, type (1 scarab, 2 herb, 3 powder,
    4 potion, 5 talisman, 6 taper), gesture, time, text (the spoken word), CDM.
  - Spell words (SpellComponentsTable.GetSpellWords): herb word + capitalized
    (powder word + lowercased potion word), e.g. "Zojak Quafeth".

Output assets/acspells.json:
  { "spells":     { id: {n, d(<=200ch), school, lvl(I..VIII tier from power),
                         mana, icon, comps:[ids], words} },
    "components": { id: {n, type, words} } }
"""
import importlib.util, json, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "acspells.json")

_spec = importlib.util.spec_from_file_location(
    "ac_model_export", os.path.join(ROOT, "tools", "ac_model_export.py"))
_ame = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_ame)
DatReader, Buf = _ame.DatReader, _ame.Buf

SPELL_TABLE_DID = 0x0E00000E
COMP_TABLE_DID = 0x0E00000F
HIGHEST_COMP_ID = 198          # "Essence of Kemeroi" (Void), highest valid comp id
TYPE_HERB, TYPE_POWDER, TYPE_POTION = 2, 3, 4
ENCHANTMENT, PORTAL_SUMMON, FELLOW_ENCHANTMENT = 1, 7, 12   # ACE SpellType

def f64(b):
    v = struct.unpack_from("<d", b.d, b.o)[0]; b.o += 8; return v

def obf_string(b):
    """ReadObfuscatedString + AlignBoundary: u16 length, nibble-swapped cp1252
    bytes, then pad the cursor to the next DWORD boundary. Returns (raw, text) —
    the *raw* deobfuscated bytes feed the formula hash (sbyte semantics)."""
    n = b.u16()
    raw = bytes(((c >> 4) | ((c << 4) & 0xF0)) for c in b.d[b.o:b.o + n])
    b.o = (b.o + n + 3) & ~3
    return raw, raw.decode("cp1252")

def ac_hash(raw):
    """SpellTable.ComputeHash over cp1252 bytes interpreted as *signed* chars."""
    r = 0
    for c in raw:
        r = (c - 256 if c >= 128 else c) + (r << 4)
        if r & 0xF0000000:
            r = (r ^ ((r & 0xF0000000) >> 24)) & 0x0FFFFFFF
    return r & 0xFFFFFFFF

def decrypt_formula(raw_comps, name_raw, desc_raw):
    key = (ac_hash(name_raw) % 0x12107680 + ac_hash(desc_raw) % 0xBEADCF45) & 0xFFFFFFFF
    comps = []
    for rc in raw_comps:
        c = (rc - key) & 0xFFFFFFFF
        if c > HIGHEST_COMP_ID:
            c &= 0xFF
        comps.append(c)
    return comps

def parse_spell(b):
    name_raw, name = obf_string(b)
    desc_raw, desc = obf_string(b)
    school = b.u32(); icon = b.u32(); category = b.u32(); bitfield = b.u32()
    mana = b.u32()
    b.f(2)                                   # baseRangeConstant, baseRangeMod
    power = b.u32()
    b.f()                                    # spellEconomyMod
    b.u32()                                  # formulaVersion
    b.f()                                    # componentLoss
    meta_type = b.u32(); b.u32()             # metaSpellType, metaSpellId
    if meta_type in (ENCHANTMENT, FELLOW_ENCHANTMENT):
        f64(b); b.f(2)                       # duration, degradeModifier, degradeLimit
    elif meta_type == PORTAL_SUMMON:
        f64(b)                               # portalLifetime
    raw_comps = [b.u32() for _ in range(8)]
    formula = decrypt_formula([c for c in raw_comps if c], name_raw, desc_raw)
    b.u32(); b.u32(); b.u32()                # caster/target/fizzle effects
    f64(b); b.f()                            # recoveryInterval, recoveryAmount
    b.u32(); b.u32(); b.u32()                # displayOrder, nonComponentTargetType, manaMod
    return dict(name=name, desc=desc, school=school, icon=icon, category=category,
                bitfield=bitfield, mana=mana, power=power, formula=formula)

def parse_spell_table(data):
    b = Buf(data)
    b.u32()                                  # table DID
    count = b.u16(); b.u16()                 # PackedHashTable: count, bucket size
    spells = {}
    for _ in range(count):
        sid = b.u32()
        spells[sid] = parse_spell(b)
    return spells

def parse_component(b):
    _, name = obf_string(b)
    b.u32(); b.u32()                         # category, icon
    ctype = b.u32()
    b.u32(); b.f()                           # gesture, time
    _, text = obf_string(b)
    b.f()                                    # CDM
    return dict(name=name, type=ctype, text=text)

def parse_component_table(data):
    b = Buf(data)
    b.u32()                                  # table DID
    count = b.u16()
    b.o = (b.o + 3) & ~3                     # AlignBoundary
    comps = {}
    for _ in range(count):
        cid = b.u32()
        comps[cid] = parse_component(b)
    return comps

def spell_words(formula, comps):
    """ACE SpellComponentsTable.GetSpellWords: herb word, then powder+potion
    words joined and capitalized as one token."""
    herb = powder = potion = ""
    for cid in formula:
        c = comps.get(cid)
        if not c:
            continue
        if c["type"] == TYPE_HERB:
            herb = c["text"]
        elif c["type"] == TYPE_POWDER:
            powder = c["text"]
        elif c["type"] == TYPE_POTION:
            potion = c["text"]
    second = powder + potion.lower()
    if second:
        second = second[0].upper() + second[1:]
    return (herb + " " + second).strip()

def power_tier(power):
    """Retail power tiers: I<50, II<100 ... VII<350, VIII>=350."""
    return min(8, max(1, power // 50 + 1))

def main():
    portal = DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    comps = parse_component_table(portal.read(COMP_TABLE_DID))
    spells = parse_spell_table(portal.read(SPELL_TABLE_DID))

    out_spells = {}
    bad_comps = 0
    for sid, s in sorted(spells.items()):
        formula = [c for c in s["formula"] if 0 < c <= HIGHEST_COMP_ID]
        if len(formula) != len(s["formula"]):
            bad_comps += 1
        out_spells[sid] = dict(
            n=s["name"], d=s["desc"][:200], school=s["school"],
            lvl=power_tier(s["power"]), mana=s["mana"], icon=s["icon"],
            comps=formula, words=spell_words(formula, comps))
    out_comps = {cid: dict(n=c["name"], type=c["type"], words=c["text"])
                 for cid, c in sorted(comps.items())}

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(dict(spells=out_spells, components=out_comps), fh,
                  separators=(",", ":"), ensure_ascii=False)

    print(f"spells: {len(out_spells)}  components: {len(out_comps)}  "
          f"spells with out-of-range comps: {bad_comps}")
    print(f"wrote {OUT} ({os.path.getsize(OUT) // 1024} KB)")

    def show(sid):
        s = out_spells[sid]
        cnames = [out_comps[c]["n"] for c in s["comps"] if c in out_comps]
        print(f"  [{sid}] {s['n']} (school {s['school']}, lvl {s['lvl']}, "
              f"mana {s['mana']}) words='{s['words']}'")
        print(f"        comps: {', '.join(cnames)}")
        print(f"        desc: {s['d'][:90]}")

    samples = []
    for sid, s in out_spells.items():
        if s["n"] == "Flame Bolt I":
            samples.append(sid); break
    for want in ("Flame Bolt VII", "Heal Self VII", "Strength Self VII"):
        for sid, s in out_spells.items():
            if s["n"] == want:
                samples.append(sid); break
    for sid, s in out_spells.items():
        if len(samples) >= 5:
            break
        if sid not in samples and s["lvl"] in (3, 8) and s["comps"]:
            samples.append(sid)
    print("\nsamples:")
    for sid in samples[:5]:
        show(sid)

if __name__ == "__main__":
    main()
