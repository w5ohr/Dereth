#!/usr/bin/env python3
"""Extract real AC sound effects from acdata/client_portal.dat.

Audio lives in 0x0A resources: [id u32][headerSize i32][dataSize i32][header][data]
where header is a WAVEFORMATEX starting at wFormatTag (usually 18 bytes, PCM).
We rewrap as a standard RIFF/WAVE file with a 16-byte fmt chunk (per the ACE
DatLoader Wave.cs reference). One resource is MPEG layer 3 (tag 0x55) — skipped.

SoundTable resources (0x20) map Sound enum keys (ACE.Entity.Enum.Sound) to
weighted lists of 0x0A wave ids:
  [id u32][unknown u32][i32 n × 16B hash entries]
  [u16 count][u16 buckets] then count × { key u32, i32 n × (waveId u32,
  priority f32, probability f32, volume f32), unknown u32 }

Creature kind -> SoundTable comes from the ACE world DB dump: weenie class ids
by name prefix, then weenie_properties_d_i_d rows with type 3 (= SoundTable DID,
same convention as type 1 = Setup used by ac_model_export).

Output: assets/acsounds/<key>.wav + manifest.json {semantic key -> file}.
Waves shared by several keys are written once and referenced by each key.
"""
import importlib.util, json, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")
OUT = os.path.join(ROOT, "assets", "acsounds")
SQL = os.path.join(ACD, "ACE-World-Database-v0.9.294.sql")

spec = importlib.util.spec_from_file_location("acme", os.path.join(ROOT, "tools", "ac_model_export.py"))
acme = importlib.util.module_from_spec(spec)
spec.loader.exec_module(acme)

# ── Sound enum keys we care about (ACE.Entity.Enum.Sound) ─────────────────
S_SPEAK1, S_ATTACK1, S_WOUND1, S_DEATH1 = 0x01, 0x03, 0x0C, 0x0F
S_SWOOSH1, S_SWOOSH2, S_SWOOSH3 = 0x1E, 0x1F, 0x20
S_XBOW_PULL, S_XBOW_REL, S_BOW_PULL, S_BOW_REL = 0x29, 0x2A, 0x2B, 0x2C
S_HITFLESH, S_HITLEATHER, S_HITCHAIN, S_HITPLATE = 0x30, 0x31, 0x32, 0x33
S_FOOT1, S_FOOT2 = 0x37, 0x38
S_EAT, S_DRINK, S_OPEN, S_CLOSE = 0x40, 0x41, 0x42, 0x43
S_AMBIENT1 = 0x46
S_LIFESTONE, S_HEALTH_UP = 0x51, 0x56
S_ENCH_UP, S_ENCH_DOWN, S_FIZZLE, S_LAUNCH, S_EXPLODE = 0x5A, 0x5B, 0x5E, 0x5F, 0x60
S_LOCKPICK, S_ENTER_PORTAL, S_EXIT_PORTAL = 0x69, 0x6A, 0x6B
S_LEVELUP, S_WIELD, S_RECEIVE, S_PICKUP, S_DROP, S_RESIST = 0x8B, 0x8C, 0x8E, 0x8F, 0x90, 0x91

MASTER_TABLE = 0x20000001   # generic combat/UI sounds (human weenie DID 3)
PLAYER_TABLE = 0x2000004B   # the only table with UI_Enter/ExitPortal
FOOTSTEP_TABLE = 0x20000016 # generic footstep pair

# creature manifest key -> weenie class-name prefix (matches assets/models/monsters/ac/*.glb)
CREATURES = {
    "armoredillo": "armoredillo", "auroch": "auroch", "banderling": "banderling",
    "burun": "burun", "carenzi": "carenzi", "chittick": "chittick", "drudge": "drudge",
    "eater": "eater", "golem": "golem", "grievver": "grievver", "gromnie": "gromnie",
    "lugian": "lugian", "mattekar": "mattekar", "mite": "mite", "moarsman": "moarsman",
    "monouga": "monouga", "mosswart": "mosswart", "niffis": "niffis", "olthoi": "olthoi",
    "olthoisoldier": "olthoisoldier", "olthoiworker": "olthoiworker", "rat": "rat",
    "reedshark": "reedshark", "sclavus": "sclavus", "shreth": "shreth",
    "skeleton": "skeleton", "tumerok": "tumerok", "tusker": "tusker", "ursuin": "ursuin",
    "virindi": "virindi", "phyntoswasp": "phyntoswasp", "wisp": "wisp", "zombie": "zombie",
    # the creature deep-dive: every remaining kind's SoundTable (class-name substring)
    "shadow": "umbrisshadow", "zefir": "zefir", "remoran": "remoran", "mukkir": "mukkir",
    "rabbit": "rabbit", "siraluun": "siraluun", "mummy": "mumiyah",
    "wight": "frozenwight", "lich": "lich", "hollow": "hollowminion",
    "thrungus": "thrungus", "ruschk": "ruschk", "gurog": "gurog", "sleech": "sleech",
    "fiun": "fiun", "marionette": "marionette", "doll": "doll", "penguin": "penguin",
    "crystalwisp": "crystallinewisp", "margul": "margul", "phantom": "phantom",
    "elemental": "waterelemental", "snowman": "snowman", "chicken": "chicken",
    "viamontian": "viamont", "falatacot": "falatacot", "tormented": "tormented",
    "hea": "hea", "gearknight": "gearknight",
    "olthoiqueen": "olthoiqueen", "lichlord": "lichlord", "virindidirector": "virindidirector",
}
# war spell school -> projectile weenie class-name prefix (weenie type 33)
SPELLS = {"fire": "flamebolt", "frost": "frostbolt", "lightning": "lightningbolt",
          "acid": "acidstream", "force": "forcebolt", "blade": "whirlingblade"}

def parse_soundtable(d):
    r = acme.Buf(d)
    r.u32(); r.u32()
    for _ in range(r.i32()):
        r.o += 16
    count = r.u16(); r.u16()
    out = {}
    for _ in range(count):
        key = r.u32()
        n = r.i32()
        entries = []
        for _ in range(n):
            wid = r.u32(); r.f(); prob = r.f(); vol = r.f()
            entries.append((wid, prob, vol))
        r.u32()
        out[key] = entries
    if r.o != len(d):
        raise ValueError("soundtable: consumed %d of %d" % (r.o, len(d)))
    return out

def wave_to_wav(d):
    """0x0A resource -> standalone RIFF/WAVE bytes (None for non-PCM, e.g. mp3)."""
    _, hsz, dsz = struct.unpack_from("<Iii", d, 0)  # id, headerSize, dataSize
    header = d[12:12 + hsz]
    data = d[12 + hsz:12 + hsz + dsz]
    fmt_tag = struct.unpack_from("<H", header, 0)[0]
    if fmt_tag != 1:            # not PCM (0x55 = MPEG layer 3 music) — skip
        return None
    # standard 16-byte PCM fmt chunk: the AC header starts at wFormatTag, truncate extras
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
            + b"fmt " + struct.pack("<I", 16) + header[:16]
            + b"data" + struct.pack("<I", len(data)) + data)

def scan_world_db(creature_prefixes, spell_prefixes):
    """SQL dump -> {label: soundtable id} via weenie name prefix + DID type 3."""
    names = {}                                   # class_name -> (class_id, weenie_type)
    rx_w = re.compile(r"\((\d+),'([a-z0-9_\-]+)',(\d+),")
    rx_d = re.compile(r"\(\d+,(\d+),3,(\d+)\)")
    with open(SQL, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("INSERT INTO `weenie` VALUES"):
                for cid, cn, wt in rx_w.findall(line):
                    names.setdefault(cn, (int(cid), int(wt)))
    # candidate class ids per label, shortest name first (canonical base weenie)
    cands = {}
    for label, prefix in creature_prefixes.items():
        # ToD-era weenies are classnamed 'aceNNNNN-<name>' — strip that for matching
        cands[label] = [cid for cn, (cid, wt) in
                        sorted(names.items(), key=lambda x: (len(x[0]), x[0]))
                        if prefix in re.sub(r"^ace\d+-", "", cn) and wt in (10, 15)][:8]
    for label, prefix in spell_prefixes.items():
        cands["spell:" + label] = [cid for cn, (cid, wt) in
                                   sorted(names.items(), key=lambda x: (len(x[0]), x[0]))
                                   if cn.startswith(prefix) and wt == 33][:4]
    wanted = {cid for lst in cands.values() for cid in lst}
    dids = {}                                    # class_id -> soundtable id
    with open(SQL, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("INSERT INTO `weenie_properties_d_i_d`"):
                for oid, val in rx_d.findall(line):
                    oid, val = int(oid), int(val)
                    if oid in wanted and val >> 24 == 0x20:
                        dids[oid] = val
    out = {}
    for label, lst in cands.items():
        for cid in lst:
            if cid in dids:
                out[label] = dids[cid]
                break
    return out

def main():
    portal = acme.DatReader(os.path.join(ACD, "client_portal.dat"))
    os.makedirs(OUT, exist_ok=True)

    tables = {}
    def table(tid):
        if tid not in tables:
            tables[tid] = parse_soundtable(portal.read(tid))
        return tables[tid]

    st_by_label = scan_world_db(CREATURES, SPELLS)
    for label in sorted(set(CREATURES) - {l for l in st_by_label if not l.startswith("spell:")}):
        print("  no soundtable found for creature:", label)
    for school in sorted(set(SPELLS) - {l[6:] for l in st_by_label if l.startswith("spell:")}):
        print("  no soundtable found for spell school:", school)

    # semantic key -> (soundtable id, [sound enum keys in fallback order])
    plan = {
        # melee / missile combat (master table)
        "swing1": (MASTER_TABLE, [S_SWOOSH1]), "swing2": (MASTER_TABLE, [S_SWOOSH2]),
        "swing3": (MASTER_TABLE, [S_SWOOSH3]),
        "hit_flesh": (MASTER_TABLE, [S_HITFLESH]), "hit_leather": (MASTER_TABLE, [S_HITLEATHER]),
        "hit_chain": (MASTER_TABLE, [S_HITCHAIN]), "hit_plate": (MASTER_TABLE, [S_HITPLATE]),
        "bow_pull": (MASTER_TABLE, [S_BOW_PULL]), "bow_release": (MASTER_TABLE, [S_BOW_REL]),
        "crossbow_pull": (MASTER_TABLE, [S_XBOW_PULL]), "crossbow_release": (MASTER_TABLE, [S_XBOW_REL]),
        # player vocals (master table = human weenie's)
        "player_attack": (MASTER_TABLE, [S_ATTACK1]),
        "player_wound": (MASTER_TABLE, [S_WOUND1]), "player_death": (MASTER_TABLE, [S_DEATH1]),
        # magic
        "cast": (MASTER_TABLE, [S_LAUNCH]), "fizzle": (MASTER_TABLE, [S_FIZZLE]),
        "resist": (MASTER_TABLE, [S_RESIST]),
        "enchant_up": (MASTER_TABLE, [S_ENCH_UP]), "enchant_down": (MASTER_TABLE, [S_ENCH_DOWN]),
        "heal": (MASTER_TABLE, [S_HEALTH_UP]),
        # world / UI
        "portal_enter": (PLAYER_TABLE, [S_ENTER_PORTAL]), "portal_exit": (PLAYER_TABLE, [S_EXIT_PORTAL]),
        "lifestone": (MASTER_TABLE, [S_LIFESTONE]), "level_up": (MASTER_TABLE, [S_LEVELUP]),
        "pickup": (MASTER_TABLE, [S_PICKUP]), "receive_item": (MASTER_TABLE, [S_RECEIVE]),
        "drop_item": (MASTER_TABLE, [S_DROP]), "wield": (MASTER_TABLE, [S_WIELD]),
        "drink_potion": (MASTER_TABLE, [S_DRINK]), "eat": (MASTER_TABLE, [S_EAT]),
        "lockpick": (MASTER_TABLE, [S_LOCKPICK]),
        "footstep1": (FOOTSTEP_TABLE, [S_FOOT1]), "footstep2": (FOOTSTEP_TABLE, [S_FOOT2]),
    }
    for label, tid in sorted(st_by_label.items()):
        if label.startswith("spell:"):
            school = label[6:]
            plan["cast_" + school] = (tid, [S_LAUNCH])
            plan["explode_" + school] = (tid, [S_EXPLODE])
        else:
            plan[label + "_idle"] = (tid, [S_SPEAK1, S_AMBIENT1])
            plan[label + "_attack"] = (tid, [S_ATTACK1, 0x04, 0x05])
            plan[label + "_wound"] = (tid, [S_WOUND1, 0x0D, 0x0E])
            plan[label + "_death"] = (tid, [S_DEATH1, 0x10, 0x11])
    # door / chest open+close (fixed table ids: door weenie 278 -> 0x20000022,
    # chest weenie 143 -> 0x20000021, resolved from the same world DB)
    plan["door_open"] = (0x20000022, [S_OPEN]); plan["door_close"] = (0x20000022, [S_CLOSE])
    plan["chest_open"] = (0x20000021, [S_OPEN]); plan["chest_close"] = (0x20000021, [S_CLOSE])

    manifest, written, unresolved = {}, {}, []
    for key in sorted(plan):
        tid, sound_keys = plan[key]
        try:
            tbl = table(tid)
        except Exception as e:
            unresolved.append("%s (table 0x%08X: %s)" % (key, tid, e)); continue
        entries = next((tbl[sk] for sk in sound_keys if sk in tbl and tbl[sk]), None)
        if not entries:
            unresolved.append("%s (table 0x%08X lacks keys %s)" % (key, tid, sound_keys)); continue
        wid = max(entries, key=lambda e: e[1])[0]     # most probable variant
        if wid not in written:
            wav = wave_to_wav(portal.read(wid))
            if wav is None:
                unresolved.append("%s (wave 0x%08X non-PCM)" % (key, wid)); continue
            fn = key + ".wav"
            with open(os.path.join(OUT, fn), "wb") as f:
                f.write(wav)
            written[wid] = fn
        manifest[key] = written[wid]

    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)

    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print("wrote %d keys -> %d wav files, %.1f MB total" % (len(manifest), len(written), total / 2**20))
    for u in unresolved:
        print("  unresolved:", u)

if __name__ == "__main__":
    main()
