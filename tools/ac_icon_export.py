#!/usr/bin/env python3
"""Export real AC item icons from client_portal.dat as PNGs.

Streams the ACE world database SQL dump for every weenie's icon DID
(weenie_properties_d_i_d propertyId 8), name (weenie_properties_string
propertyId 1) and item type (weenie_properties_int propertyId 1 -- the
ItemType flags enum). Icons for the useful item groups (weapons, armor,
clothing, jewelry, gems, spell components, food/potions, keys, scrolls,
salvage/craft materials) are decoded from the 0x06xxxxxx texture resources
in client_portal.dat (via the texture decoder in ac_model_export.py) and
written as PNGs, deduped by icon DID. The 40 most-common icons among plain
Misc items are included too.

Output:
  assets/acicons/<did-hex>.png        (lowercase 8-digit hex, e.g. 06001036.png)
  assets/acicons/index.json           {"icons": {didhex: file}, "names": {name: didhex}}

Stdlib only (own PNG encoder, zlib).
"""
import importlib.util
import json
import os
import re
import struct
import sys
import zlib
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SQL = os.path.join(ROOT, "acdata", "ACE-World-Database-v0.9.294.sql")
PORTAL = os.path.join(ROOT, "acdata", "client_portal.dat")
OUT = os.path.join(ROOT, "assets", "acicons")

spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ame)

# ── ItemType flags (ACE ItemType enum) ────────────────────────────────────
MELEE_WEAPON     = 0x00000001
ARMOR            = 0x00000002
CLOTHING         = 0x00000004
JEWELRY          = 0x00000008
FOOD             = 0x00000020   # includes potions
MISC             = 0x00000080
MISSILE_WEAPON   = 0x00000100
GEM              = 0x00000400
SPELL_COMPONENTS = 0x00000800   # scarabs / tapers / herbs / powders
WRITABLE         = 0x00001000   # scrolls, books
KEY              = 0x00002000
CASTER           = 0x00004000   # wands / orbs / staffs
CRAFT_COOKING    = 0x00200000
CRAFT_ALCHEMY    = 0x00400000
CRAFT_FLETCHING  = 0x01000000
CRAFT_ALCH_INT   = 0x02000000
CRAFT_FLETCH_INT = 0x04000000
TINKERING_TOOL   = 0x10000000
TINKERING_MAT    = 0x20000000   # salvage

INCLUDE_FLAGS = (MELEE_WEAPON | ARMOR | CLOTHING | JEWELRY | FOOD |
                 MISSILE_WEAPON | GEM | SPELL_COMPONENTS | WRITABLE | KEY |
                 CASTER | CRAFT_COOKING | CRAFT_ALCHEMY | CRAFT_FLETCHING |
                 CRAFT_ALCH_INT | CRAFT_FLETCH_INT | TINKERING_TOOL |
                 TINKERING_MAT)
MISC_TOP_N = 40
MAX_NAME_LEN = 48

TUPLE_NUM = re.compile(rb"\((\d+),(\d+),(\d+),(-?\d+)\)")
TUPLE_STR = re.compile(rb"\((\d+),(\d+),(\d+),'((?:[^'\\]|\\.)*)'\)")
UNESCAPE = re.compile(rb"\\(.)")
INSERT_RE = re.compile(rb"^INSERT INTO `(\w+)`")


def stream_sql():
    """One pass over the dump. Returns (icon_did, name, item_type) dicts keyed by wcid."""
    icon = {}      # wcid -> icon DID (property 8)
    name = {}      # wcid -> bytes name (property 1)
    itype = {}     # wcid -> ItemType flags (int property 1)
    want = {b"weenie_properties_d_i_d", b"weenie_properties_string", b"weenie_properties_int"}
    current = None
    with open(SQL, "rb") as f:
        for line in f:
            m = INSERT_RE.match(line)
            if m:
                current = m.group(1) if m.group(1) in want else None
            elif current is not None and line[:1].isalpha() and not line.startswith(b"INSERT"):
                # a new non-INSERT statement (CREATE/LOCK/...) ends the sticky table
                current = None
            if current is None:
                continue
            if current == b"weenie_properties_string":
                for t in TUPLE_STR.finditer(line):
                    if t.group(3) == b"1":
                        wcid = int(t.group(2))
                        if wcid not in name:
                            name[wcid] = UNESCAPE.sub(rb"\1", t.group(4))
            else:
                for t in TUPLE_NUM.finditer(line):
                    if t.group(3) != b"1" and current == b"weenie_properties_int":
                        continue
                    if t.group(3) != b"8" and current == b"weenie_properties_d_i_d":
                        continue
                    wcid = int(t.group(2))
                    val = int(t.group(4))
                    if current == b"weenie_properties_d_i_d":
                        icon[wcid] = val
                    else:
                        itype[wcid] = val
    return icon, name, itype


def write_png(path, w, h, rgba):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n"
                 + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                 + chunk(b"IDAT", zlib.compress(raw, 9))
                 + chunk(b"IEND", b""))


def main():
    drop_misc = "--no-misc" in sys.argv
    print("streaming SQL ...", flush=True)
    icon, name, itype = stream_sql()
    print(f"  weenies with icon: {len(icon)}, names: {len(name)}, item types: {len(itype)}")

    # pick weenies in the useful groups
    group_wcids = set()
    misc_icon_count = Counter()
    for wcid, did in icon.items():
        it = itype.get(wcid, 0)
        if it & INCLUDE_FLAGS:
            group_wcids.add(wcid)
        elif it & MISC and not drop_misc:
            misc_icon_count[did] += 1
    misc_dids = {did for did, _ in misc_icon_count.most_common(MISC_TOP_N)}
    for wcid, did in icon.items():
        if did in misc_dids and itype.get(wcid, 0) & MISC:
            group_wcids.add(wcid)

    dids = {icon[w] for w in group_wcids}
    print(f"  selected weenies: {len(group_wcids)}, unique icon DIDs: {len(dids)}")

    # Union in every icon DID the GAME actually references (assets/acitems.json), so the export
    # is complete regardless of the ItemType-flag group selection above. Without this, items whose
    # weenie ItemType isn't in INCLUDE_FLAGS (torches, wands, sceptres, staves, ingots, shafts, …)
    # reference an icon DID that never gets exported and fall back to a generic category icon.
    items_path = os.path.join(ROOT, "assets", "acitems.json")
    ref_extra = set()
    try:
        with open(items_path, encoding="utf-8") as fh:
            for e in json.load(fh).values():
                ic = (e.get("icon") or "").strip()
                if ic:
                    try:
                        ref_extra.add(int(ic, 16))
                    except ValueError:
                        pass
        added = ref_extra - dids
        dids |= ref_extra
        print(f"  + {len(added)} extra icon DIDs referenced by acitems.json ({len(dids)} total)")
    except FileNotFoundError:
        print("  (assets/acitems.json not found — exporting weenie-group icons only)")

    # #339: ALSO union the spell icon DIDs (acspellstats.json + acspells.json). The acitems union above
    # covers items only, so without this the spellbook renders nearly every spell with a missing icon.
    spell_extra = set()
    def _walk_icons(o):
        if isinstance(o, dict):
            for k, v in o.items():
                if k == "icon" and v not in (None, ""):
                    try:
                        spell_extra.add(int(str(v).replace("0x", "").replace("0X", ""), 16))
                    except ValueError:
                        pass
                else:
                    _walk_icons(v)
        elif isinstance(o, list):
            for x in o:
                _walk_icons(x)
    for spath in ("acspellstats.json", "acspells.json"):
        try:
            with open(os.path.join(ROOT, "assets", spath), encoding="utf-8") as fh:
                _walk_icons(json.load(fh))
        except FileNotFoundError:
            pass
    added_sp = spell_extra - dids
    dids |= spell_extra
    print(f"  + {len(added_sp)} extra spell icon DIDs referenced by acspellstats/acspells ({len(dids)} total)")

    portal = ame.DatReader(PORTAL)
    pal_cache = {}
    def palettes(pid):
        if pid not in pal_cache:
            pal_cache[pid] = ame.parse_palette(portal.read(pid))
        return pal_cache[pid]

    os.makedirs(OUT, exist_ok=True)
    exported = {}          # did -> filename
    missing, bad_fmt = [], Counter()
    for did in sorted(dids):
        if not (0x06000000 <= did <= 0x06FFFFFF) or did not in portal.files:
            missing.append(did)
            continue
        raw = portal.read(did)
        dec = ame.decode_texture(raw, palettes)
        if dec is None:
            fmt = struct.unpack_from("<I", raw, 16)[0]
            bad_fmt[fmt] += 1
            continue
        w, h, px = dec
        fn = "%08x.png" % did
        write_png(os.path.join(OUT, fn), w, h, px)
        exported[did] = fn

    # name -> icon-did lookup (lowercase, <= MAX_NAME_LEN chars, first wins)
    names_map = {}
    for wcid in sorted(group_wcids):
        did = icon[wcid]
        if did not in exported or wcid not in name:
            continue
        n = name[wcid].decode("utf-8", "replace").strip().lower()
        if not n or len(n) > MAX_NAME_LEN or n in names_map:
            continue
        names_map[n] = "%08x" % did

    index = {
        "icons": {"%08x" % did: fn for did, fn in sorted(exported.items())},
        "names": names_map,
    }
    with open(os.path.join(OUT, "index.json"), "w", encoding="utf-8") as fh:
        json.dump(index, fh, indent=0, ensure_ascii=False, sort_keys=True)

    total = sum(os.path.getsize(os.path.join(OUT, fn)) for fn in exported.values())
    print(f"exported {len(exported)} icons, {total/1e6:.2f} MB pngs; names mapped: {len(names_map)}")
    if missing:
        print(f"  DIDs not in portal.dat / out of 0x06 range: {len(missing)} e.g. {['%08x' % d for d in missing[:5]]}")
    if bad_fmt:
        print(f"  undecodable formats: {dict(bad_fmt)}")


if __name__ == "__main__":
    main()
