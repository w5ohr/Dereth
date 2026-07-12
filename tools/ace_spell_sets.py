#!/usr/bin/env python3
"""Extract the equipment-set spell bonuses -> assets/acspellsets.json

The DAT SpellTable (client_portal.dat, 0x0E00000E) has TWO packed hash tables:
  1. Spells    -> SpellBase  (already -> acspells.json via ac_spell_export.py)
  2. SpellSet  -> per EquipmentSet: SpellSetTiers { tier(pieces) -> List<spellId> }

So the "wear N pieces of a set -> gain these spells" mapping IS in the client dat,
right after the spells. We reuse ac_spell_export to consume the Spells table (which
leaves the cursor at the SpellSet table), then unpack SpellSet per the ACE DatLoader:
  SpellTable.SpellSet : Dictionary<uint setId, SpellSet>            (PackedHashTable)
  SpellSet            : SortedDictionary<uint tier, SpellSetTiers>  (PackedHashTable)
  SpellSetTiers       : List<uint> Spells                          (Int32 count + u32s)

setId is the ACE EquipmentSet enum (Soldiers=13, Adepts=14 …); spellId joins the roster.
Output: { "sets": { "<SetName>": { id, tiers: { "<pieces>": [ {id, n} ... ] } } } }
"""
import importlib.util, os, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "acspellsets.json")

_spec = importlib.util.spec_from_file_location("ase", os.path.join(ROOT, "tools", "ac_spell_export.py"))
ase = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(ase)

# ACE.Entity/Enum/EquipmentSet.cs (fetched from ACEmulator) — id -> name
EQUIP_SET = {
    4: "Carraida's Benediction", 5: "Noble Relic", 6: "Ancient Relic", 7: "Alduressa Relic",
    8: "Ninja", 9: "Empyrean Rings", 10: "Arm/Mind/Heart", 11: "Perfect Light", 12: "Perfect Light II",
    13: "Soldier's", 14: "Adept's", 15: "Archer's", 16: "Defender's", 17: "Tinker's", 18: "Crafter's",
    19: "Hearty", 20: "Dexterous", 21: "Wise", 22: "Swift", 23: "Hardened", 24: "Reinforced",
    25: "Interlocking", 26: "Flameproof", 27: "Acidproof", 28: "Coldproof", 29: "Lightningproof",
    30: "Society Armor", 31: "Colosseum Clothing", 32: "Graveyard Clothing", 33: "Olthoi Clothing",
    34: "Noobie Armor", 35: "Aetheria Defense", 36: "Aetheria Destruction", 37: "Aetheria Fury",
    38: "Aetheria Growth", 39: "Aetheria Vigor", 40: "Rare Damage Resistance", 41: "Rare Damage Boost",
    42: "Olthoi Armor (DRed)", 43: "Olthoi Armor (CRat)", 44: "Olthoi Armor (CRed)", 45: "Olthoi Armor (DRat)",
    46: "Alduressa Relic Upgrade", 47: "Ancient Relic Upgrade", 48: "Noble Relic Upgrade",
    49: "Alchemy Cloak", 50: "Arcane Lore Cloak", 51: "Armor Tinkering Cloak", 52: "Assess Person Cloak",
    53: "Light Weapons Cloak", 54: "Missile Weapons Cloak", 55: "Cooking Cloak", 56: "Creature Ench. Cloak",
    57: "Crossbow Cloak", 58: "Finesse Weapons Cloak", 59: "Deception Cloak", 60: "Fletching Cloak",
    61: "Healing Cloak", 62: "Item Ench. Cloak", 63: "Item Tinkering Cloak", 64: "Leadership Cloak",
    65: "Life Magic Cloak", 66: "Loyalty Cloak", 67: "Mace Cloak", 68: "Magic Defense Cloak",
    69: "Magic Item Tinkering Cloak", 70: "Mana Conversion Cloak", 71: "Melee Defense Cloak",
    72: "Missile Defense Cloak", 73: "Salvaging Cloak", 74: "Spear Cloak", 75: "Staff Cloak",
    76: "Heavy Weapons Cloak", 77: "Thrown Weapon Cloak", 78: "Two Handed Cloak", 79: "Unarmed Cloak",
}


def parse_sets(b):
    """b positioned at the SpellSet PackedHashTable (right after the Spells table)."""
    set_count = b.u16(); b.u16()                       # PackedHashTable header
    sets = {}
    for _ in range(set_count):
        set_id = b.u32()
        tier_count = b.u16(); b.u16()                  # SpellSet: SpellSetTiers PackedHashTable
        tiers = {}
        for _ in range(tier_count):
            tier = b.u32()
            n = b.u32()                                # List<uint>.Unpack: Int32 count
            tiers[tier] = [b.u32() for _ in range(n)]
        sets[set_id] = tiers
    return sets


def main():
    portal = ase.DatReader(os.path.join(ROOT, "acdata", "client_portal.dat"))
    b = ase.Buf(portal.read(ase.SPELL_TABLE_DID))
    b.u32()                                            # table DID
    count = b.u16(); b.u16()                           # Spells PackedHashTable header
    spells = {}
    for _ in range(count):
        sid = b.u32(); spells[sid] = ase.parse_spell(b)   # consume -> advance to SpellSet table
    raw = parse_sets(b)

    def sname(sid):
        s = spells.get(sid); return s["name"] if s else f"Spell {sid}"

    out = {"sets": {}}
    for set_id, tiers in sorted(raw.items()):
        name = EQUIP_SET.get(set_id, f"Set {set_id}")
        out["sets"][name] = {
            "id": set_id,
            "tiers": {str(t): [{"id": s, "n": sname(s)} for s in tiers[t]] for t in sorted(tiers)},
        }
    json.dump(out, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, separators=(",", ":"))
    json.load(open(OUT, encoding="utf-8"))

    ns = len(out["sets"]); nb = sum(len(v["tiers"]) for v in out["sets"].values())
    print(f"{ns} equipment sets, {nb} tier-bonuses -> {OUT} ({os.path.getsize(OUT)//1024} KB)")
    for want in ("Soldier's", "Adept's", "Hearty", "Defender's"):
        v = out["sets"].get(want)
        if not v: continue
        print(f"  {want} (id {v['id']}):")
        for t, spl in v["tiers"].items():
            print(f"    {t} pieces -> {', '.join(x['n'] for x in spl)}")


if __name__ == "__main__":
    main()
