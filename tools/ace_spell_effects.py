#!/usr/bin/env python3
"""Extract per-spell EFFECT MAGNITUDES + projectile visuals -> assets/acspellfx.json

The DAT SpellTable (client_portal.dat) has the roster, mana, components and words
(-> assets/acspells.json), but NOT the numeric effect. That lives in ACE's `spell`
table, exported per-spell under 3-Core/2 SpellTableExtendedData/SQL. Each file is one
INSERT whose column list varies by spell type (a union schema). We keep the meaningful
effect columns and the /* enum */ labels the dump annotates them with.

Schema variants handled (keyed by the columns present):
  statmod    stat_Mod_Type, stat_Mod_Key, stat_Mod_Val      buffs/debuffs (+10 Strength, +Sword…)
  projectile e_Type, base_Intensity, variance, wcid,        War bolts/streams/blasts — wcid is the
             num_Projectiles, spread_Angle, crit_Freq,      projectile object (the VISUAL), plus
             crit_Multiplier, ignore_Magic_Resist,          slayer / crit / drain / damage_Ratio
             elemental_Modifier, slayer_*, drain_Percentage, damage_Ratio
  vital      damage_Type, boost, boost_Variance             heals / harms (Health/Stamina/Mana)
  dispel     min_Power, max_Power, dispel_School, align, number
  transfer   source, destination, proportion, loss_Percent, transfer_Cap
  portal     position_* (recall/summon) — recorded as kind only
  link/index misc — kind only

Spell ids match assets/acspells.json, so the game can join fx onto the roster.
Output: assets/acspellfx.json  { "<id>": { kind, <effect fields with decoded labels> } }
"""
import os, re, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core", "2 SpellTableExtendedData", "SQL")
OUT = os.path.join(ROOT, "assets", "acspellfx.json")

INSERT_RE = re.compile(r"INSERT INTO `spell`\s*\(([^)]*)\)\s*VALUES\s*\((.*?)\);", re.S)


def split_vals(s):
    """Split a VALUES body into fields, keeping /* comments */ and quoted names intact."""
    out, cur, depth, q, i, n = [], [], 0, False, 0, len(s)
    while i < n:
        ch = s[i]
        if q:
            cur.append(ch)
            if ch == "'":
                if i + 1 < n and s[i + 1] == "'": cur.append("'"); i += 2; continue
                q = False
            i += 1; continue
        if ch == "'": q = True; cur.append(ch); i += 1; continue
        if ch == "/" and i + 1 < n and s[i + 1] == "*":            # keep comment as one token chunk
            j = s.find("*/", i); cur.append(s[i:j + 2]); i = j + 2; continue
        if ch == "," and depth == 0:
            out.append("".join(cur).strip()); cur = []; i += 1; continue
        if ch == "(": depth += 1
        elif ch == ")": depth -= 1
        cur.append(ch); i += 1
    out.append("".join(cur).strip())
    return out


def num(v):
    v = re.sub(r"/\*.*?\*/", "", v).strip()
    if v in ("", "NULL"): return None
    if v.lower() == "true": return True
    if v.lower() == "false": return False
    try: return int(v)
    except ValueError:
        try: return round(float(v), 4)
        except ValueError: return v.strip("'")


def label(v):
    m = re.search(r"/\*\s*(.*?)\s*\*/", v)
    return m.group(1) if m else None


def main():
    fx = {}
    kinds = {}
    for fn in os.listdir(SRC):
        if not fn.endswith(".sql"): continue
        text = open(os.path.join(SRC, fn), encoding="utf-8", errors="replace").read()
        m = INSERT_RE.search(text)
        if not m: continue
        cols = [c.strip().strip("`") for c in m.group(1).split(",")]
        vals = split_vals(m.group(2))
        if len(cols) != len(vals): continue
        row = dict(zip(cols, vals))
        sid = num(row.get("id"))
        if sid is None: continue

        rec = {}
        cset = set(cols)
        if "stat_Mod_Type" in cset:
            kind = "statmod"
            rec["mod"] = label(row["stat_Mod_Type"]) or num(row["stat_Mod_Type"])
            rec["stat"] = label(row["stat_Mod_Key"]) or num(row["stat_Mod_Key"])
            rec["val"] = num(row["stat_Mod_Val"])
        elif "base_Intensity" in cset and "wcid" in cset:
            kind = "projectile"
            rec["elem"] = label(row["e_Type"]) or num(row["e_Type"])
            rec["intensity"] = num(row["base_Intensity"]); rec["variance"] = num(row["variance"])
            rec["proj"] = num(row["wcid"]); rec["projName"] = label(row["wcid"])
            rec["n"] = num(row["num_Projectiles"]); rec["spread"] = num(row.get("spread_Angle"))
            rec["crit"] = num(row.get("crit_Freq")); rec["critMult"] = num(row.get("crit_Multiplier"))
            rec["ignoreRes"] = num(row.get("ignore_Magic_Resist"))
            rec["elemMod"] = num(row.get("elemental_Modifier"))
            sl = label(row.get("slayer_Creature_Type", ""))
            if sl and sl not in ("Undef", "Invalid"):
                rec["slayer"] = sl; rec["slayerBonus"] = num(row.get("slayer_Damage_Bonus"))
            if "drain_Percentage" in cset:
                rec["drainPct"] = num(row["drain_Percentage"]); rec["dmgRatio"] = num(row["damage_Ratio"])
        elif "damage_Type" in cset and "boost" in cset:
            kind = "vital"
            rec["vital"] = label(row["damage_Type"]) or num(row["damage_Type"])
            rec["boost"] = num(row["boost"]); rec["boostVar"] = num(row["boost_Variance"])
        elif "min_Power" in cset:
            kind = "dispel"
            rec["minPower"] = num(row["min_Power"]); rec["maxPower"] = num(row["max_Power"])
            rec["school"] = label(row.get("dispel_School")) or num(row.get("dispel_School"))
            rec["align"] = num(row.get("align")); rec["number"] = num(row.get("number"))
        elif "proportion" in cset:
            kind = "transfer"
            rec["src"] = label(row["source"]) or num(row["source"])
            rec["dst"] = label(row["destination"]) or num(row["destination"])
            rec["proportion"] = num(row["proportion"]); rec["lossPct"] = num(row.get("loss_Percent"))
            rec["cap"] = num(row.get("transfer_Cap"))
        elif "position_Obj_Cell_ID" in cset:
            kind = "portal"
        else:
            kind = "misc"
        rec = {k: v for k, v in rec.items() if v is not None}
        rec["kind"] = kind
        fx[str(sid)] = rec
        kinds[kind] = kinds.get(kind, 0) + 1

    json.dump(fx, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, separators=(",", ":"))
    json.load(open(OUT, encoding="utf-8"))
    print(f"{len(fx)} spells -> {OUT} ({os.path.getsize(OUT)//1024} KB)")
    print("by kind:", dict(sorted(kinds.items(), key=lambda kv: -kv[1])))
    # samples
    for want in ("statmod", "projectile", "vital", "transfer", "dispel"):
        for sid, r in fx.items():
            if r["kind"] == want:
                print(f"  [{sid}] {json.dumps(r, ensure_ascii=False)[:150]}"); break


if __name__ == "__main__":
    main()
