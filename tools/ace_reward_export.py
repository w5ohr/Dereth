#!/usr/bin/env python3
"""Export the authentic AC reward economy from the ACE world DB (3-Core split SQL).

  killxp  — each bestiary kind's representative retail creature: Level (int 25) and
            XpOverride (int 146), the exact XP granted on its death.
  quests  — every NPC whose emote table awards quest rewards: the largest AwardXP
            amount, total Pyreal gives, and the item names it hands over.
  tiers   — treasure_death profiles aggregated per loot tier (1..8): item / magic-item
            counts and chances that drive chest & hoard yields.

Output: assets/acrewards.json — loaded by index.html at boot to override BESTIARY kill
XP, QUESTS xp/gold where the giver matches a real AC NPC, and chest/hoard loot counts.
"""
import os, re, json, statistics
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACE = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core")

KINDS = {   # bestiary kind -> representative retail creature (as in ac_creature_export)
 "drudge":"Drudge Slinker","olthoi":"Olthoi Warrior","olthoisoldier":"Olthoi Soldier",
 "olthoiworker":"Olthoi Worker","skeleton":"Skeleton","virindi":"Virindi Servant",
 "tusker":"Tusker","gromnie":"Gromnie","banderling":"Banderling Guard",
 "mosswart":"Mosswart","reedshark":"Reedshark","auroch":"Auroch","mattekar":"Mattekar",
 "lugian":"Lugian Commander","tumerok":"Tumerok Gladiator","rat":"Rat",
 "zombie":"Zombie","golem":"Sandstone Golem","monouga":"Bloodthirsty Monouga",
 "shreth":"Carrion Shreth","phyntoswasp":"Phyntos Wasp","wisp":"Wisp","ursuin":"Ursuin",
 "moarsman":"Fetid Moarsman","sclavus":"Sata Sclavus","mite":"Mite Scamp",
 "niffis":"Niffis","carenzi":"Carenzi","armoredillo":"Armoredillo","siraluun":"Siraluun",
 "grievver":"Grievver","eater":"Eater","chittick":"Chittick","burun":"Burun Ruuk Lout",
 "shadow":"Shadow","zefir":"Zefir","remoran":"Gold Remoran","mukkir":"Mukkir",
 "mummy":"Mu-miyah",
}

def walk_creatures():
    base = os.path.join(ACE, "9 WeenieDefaults", "SQL", "Creature")
    for root, _, files in os.walk(base):
        for fn in files:
            m = re.match(r"\d+ (.+)\.sql$", fn)
            if m: yield m.group(1), os.path.join(root, fn)

def main():
    # ── creature kill XP ──
    want = {}
    for kind, pat in KINDS.items():
        want.setdefault(pat, []).append(kind)
    prefix = {}
    killxp = {}
    files = list(walk_creatures())
    byname = dict(files)
    def stats_of(path):
        src = open(path, encoding="utf-8", errors="replace").read()
        lv = re.search(r"\(\d+,\s*25,\s*(\d+)\)\s*/\* Level \*/", src)
        xp = re.search(r"\(\d+,\s*146,\s*(\d+)\)\s*/\* XpOverride \*/", src)
        return (int(lv.group(1)), int(xp.group(1))) if lv and xp else None
    for pat, kinds in want.items():
        # names get reused for late-era bosses ("Skeleton" exists at L710) — our bestiary
        # kinds are the classic overworld breeds, so take the LOWEST-level match
        cands = []
        for n, p in files:
            if n == pat or n.startswith(pat + " ") or n.endswith(" " + pat):
                s = stats_of(p)
                if s and s[1] > 0: cands.append(s)
        if cands:
            got = min(cands)
            for k in kinds: killxp[k] = dict(lvl=got[0], xp=got[1])
    print(f"killxp: {len(killxp)} kinds")
    for k in sorted(killxp): print(f"  {k:14s} L{killxp[k]['lvl']:<3d} {killxp[k]['xp']:,} xp")

    # ── NPC quest rewards from emote tables ──
    quests = {}
    axp = re.compile(r"/\* AwardXP \*/[^\n]*?(\d{3,})")
    give_py = re.compile(r"/\* Give \*/[^\n]*?273 /\* Pyreal \*/,\s*(\d+)")
    give_it = re.compile(r"/\* Give \*/[^\n]*?\d+ /\* ([^*/][^*]*?) \*/,\s*\d+,")
    for name, path in files:
        src = open(path, encoding="utf-8", errors="replace").read()
        if "emote_action" not in src: continue
        xps = [int(x) for x in axp.findall(src)]
        pys = [int(x) for x in give_py.findall(src)]
        items = [i.strip() for i in give_it.findall(src) if i.strip() != "Pyreal"]
        if xps or pys:
            quests[name] = dict(xp=max(xps) if xps else 0, py=max(pys) if pys else 0,
                                items=sorted(set(items))[:6])
    print(f"quest-reward NPCs: {len(quests)}")

    # ── treasure_death tier profiles ──
    rows = []
    ddir = os.path.join(ACE, "3 TreasureTable", "SQL", "Death")
    for fn in os.listdir(ddir):
        src = open(os.path.join(ddir, fn), encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"VALUES \(([\d.,\s-]+),", src):
            v = [x.strip() for x in m.group(1).split(",")]
            if len(v) >= 16:
                rows.append([float(x) for x in v[:16]])
    tiers = {}
    for t in range(1, 9):
        tr = [r for r in rows if int(r[1]) == t]
        if not tr: continue
        med = lambda i: statistics.median(r[i] for r in tr)
        tiers[t] = dict(iC=round(med(4)/100, 2), i0=int(med(5)), i1=int(med(6)),
                        mC=round(med(8)/100, 2), m0=int(med(9)), m1=int(med(10)),
                        uC=round(med(12)/100, 2), u0=int(med(13)), u1=int(med(14)))
    print("treasure tiers:", {t: tiers[t] for t in sorted(tiers)})

    out = dict(killxp=killxp, quests=quests, tiers=tiers)
    path = os.path.join(ROOT, "assets", "acrewards.json")
    json.dump(out, open(path, "w"), separators=(",", ":"))
    print(f"wrote {path} ({os.path.getsize(path)//1024} KB)")

if __name__ == "__main__":
    main()
