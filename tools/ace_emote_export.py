#!/usr/bin/env python3
"""NPC dialogue/quest text extraction -> assets/acdialogue.json

Pulls the actual spoken text of Dereth's named NPCs out of the retail ACE world DB.

  weenie_properties_emote          — per-NPC emote sets (object_Id = NPC weenie class,
                                     category = EmoteCategory, quest = quest flag string)
  weenie_properties_emote_action   — the individual actions; `message` holds the text an
                                     NPC actually speaks / the text shown to the player
  weenie_properties_string type 1  — the NPC's proper name

Every emote_action with a non-empty message is collected against its NPC. If the parent
emote carries a `quest` flag the lines land in the NPC's quest bucket, otherwise they're
treated as greetings/idle speech. Generic monsters (Drudge, Olthoi, ...) are filtered out
so only proper-named NPCs survive.

Output: { npcs: { "<lowercase npc name>": { greetings:[..], quest:[..] } } }
"""
import re, os, json
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")
SQL = next(os.path.join(ACD, f) for f in os.listdir(ACD) if f.endswith(".sql"))
data = open(SQL, encoding="utf-8", errors="replace").read()
print(f"sql {len(data)/1e6:.0f} MB")

# ---- streaming SQL parser (reused from ace_world_more.py) ----
def stmts(table):
    for m in re.finditer(r"INSERT INTO `%s`(?: \([^)]*\))? VALUES (.*?);\n" % table, data, re.S):
        yield m.group(1)

def rows(body):
    """Split a VALUES body into top-level (...) row strings, keeping quoted
    content (incl. backslash escapes) intact so message text survives."""
    out, depth, cur, q = [], 0, [], False
    i, n = 0, len(body)
    while i < n:
        ch = body[i]
        if q:
            cur.append(ch)
            if ch == "\\" and i + 1 < n:
                cur.append(body[i+1]); i += 2; continue
            if ch == "'": q = False
            i += 1; continue
        if ch == "'":
            q = True
            if depth >= 1: cur.append(ch)
            i += 1; continue
        if ch == "(":
            depth += 1
            if depth == 1: cur = []; i += 1; continue
        if ch == ")":
            depth -= 1
            if depth == 0: out.append("".join(cur)); i += 1; continue
        if depth >= 1: cur.append(ch)
        i += 1
    return out

def fields(row):
    out, cur, q = [], [], False
    i, n = 0, len(row)
    while i < n:
        ch = row[i]
        if q:
            if ch == "\\":                      # SQL escape: keep next char literally
                if i + 1 < n: cur.append(row[i+1]); i += 2; continue
                i += 1; continue
            if ch == "'": q = False
            else: cur.append(ch)
        else:
            if ch == "'": q = True
            elif ch == ",": out.append("".join(cur)); cur = []
            else: cur.append(ch)
        i += 1
    out.append("".join(cur))
    return out

# ---- NPC names (weenie_properties_string type 1) + fallback class names ----
names, cls_names = {}, {}
for b in stmts("weenie"):
    for r in rows(b):
        f = fields(r); cls_names[int(f[0])] = f[1]
for b in stmts("weenie_properties_string"):
    for r in rows(b):
        f = fields(r)
        if int(f[2]) == 1: names[int(f[1])] = f[3]
print(f"named weenies {len(names):,}")

# ---- emote sets: emote_id -> (npc weenie id, has_quest) ----
# emote cols: 0=id 1=object_Id 2=category 3=probability 4=wcid 5=style 6=substyle 7=quest
emote_meta = {}
for b in stmts("weenie_properties_emote"):
    for r in rows(b):
        f = fields(r)
        eid = int(f[0]); npc = int(f[1])
        quest = f[7] if len(f) > 7 else ""
        has_q = quest not in ("", "NULL")
        emote_meta[eid] = (npc, has_q)
print(f"emote sets {len(emote_meta):,}")

# ---- emote actions with spoken text ----
# action cols: 0=id 1=emote_Id 2=order 3=type ... 7=message
# EmoteType (col 3) tells us whether `message` is human-readable dialogue or an internal
# quest/branch label. Only these types hold text the player actually sees/hears:
#   1 emote-act text, 8 creature speak, 10 tell, 13 "you think", 16 world broadcast,
#   17 local narrative, 18 whisper-to-ear, 64 fellow tell, 65 fellow broadcast, 68 tutorial
# (all other types store quest names like "TuskerMask" / "EmoteTestSwordQuest@..").
DIALOGUE_TYPES = {1, 8, 10, 13, 16, 17, 18, 64, 65, 68}
HAS_LETTER = re.compile(r"[A-Za-z]")
SCRIPT_NOISE = re.compile(r"InqQuest|UpdateQuest|Quest(?:Success|Failure)|^\((?:Inq|Update)")
per_npc = defaultdict(lambda: {"greetings": [], "quest": []})
msg_rows = 0
for b in stmts("weenie_properties_emote_action"):
    for r in rows(b):
        f = fields(r)
        if len(f) < 8: continue
        if int(f[3]) not in DIALOGUE_TYPES: continue
        msg = f[7].strip()
        if msg in ("", "NULL") or len(msg) < 3 or not HAS_LETTER.search(msg): continue
        if SCRIPT_NOISE.search(msg): continue
        eid = int(f[1])
        meta = emote_meta.get(eid)
        if not meta: continue
        npc, has_q = meta
        msg_rows += 1
        bucket = "quest" if has_q else "greetings"
        per_npc[npc][bucket].append(msg)
print(f"emote_action messages {msg_rows:,} · npcs with text {len(per_npc):,}")

# ---- keep only proper-named NPCs (drop generic monsters/objects) ----
GENERIC = re.compile(
    r"\b(drudge|olthoi|banderling|mosswart|tumerok|lugian|virindi|golem|zombie|"
    r"skeleton|shadow|gromnie|mattekar|shreth|reedshark|reed shark|rat|mite|"
    r"sclavus|zefir|monouga|ursuin|moarsman|wisp|mummy|mumiyah|grievver|rabbit|"
    r"auroch|carenzi|niffis|remoran|burun|mukkir|tusker|armoredillo|phyntos|"
    r"guard|monster|creature|generic|template|test|statue|door|chest|portal|"
    r"lifestone|altar|target|dummy|effigy|servant|spectre|specter|wight|"
    r"undead|slave|thrall|minion|spirit|beast|hollow)\b", re.I)
BAD_CHARS = re.compile(r"[<>{}@#\d]")     # names with markup / codes are not proper NPCs
# readable objects (notes, prisms, corpses...) carry emote text but are not NPCs
OBJECT = re.compile(
    r"\b(note|paper|prism|book|scroll|letter|parchment|tablet|plaque|sign|rock|"
    r"crystal|orb|page|journal|tome|diary|missive|map|key|coffer|corpse|remains|"
    r"body|pile|pool|cache|shrine|fountain|well|lever|switch|pedestal|obelisk|"
    r"gateway|barrel|crate|sack|bag|pouch|urn|jar|bottle|vial|flask|head|hand|"
    r"eye|heart|skull|bone|essence|fragment|shard|relic|totem|idol|brazier|"
    r"candle|torch|lantern|painting|portrait|mirror|banner|tapestry)\b", re.I)
ARTICLE = re.compile(r"^(a|an|the)\s", re.I)

def is_proper(nm):
    if not nm or nm in ("NULL",): return False
    if len(nm) < 2 or len(nm) > 48: return False
    if not nm[0].isupper(): return False            # proper nouns start capitalized
    if BAD_CHARS.search(nm): return False
    if ARTICLE.match(nm): return False              # "A ...", "The ..." -> object/prop
    if GENERIC.search(nm): return False
    if OBJECT.search(nm): return False
    return True

def clean(msg):
    msg = msg.strip()
    if len(msg) > 400: msg = msg[:397].rstrip() + "..."
    return msg

npcs = {}
skipped = 0
for npc, buckets in per_npc.items():
    nm = names.get(npc) or cls_names.get(npc, "")
    if not is_proper(nm):
        skipped += 1
        continue
    key = nm.lower()
    entry = npcs.setdefault(key, {"greetings": [], "quest": []})
    for bkt in ("greetings", "quest"):
        seen = set(entry[bkt])
        for m in buckets[bkt]:
            cm = clean(m)
            if cm and cm not in seen:
                seen.add(cm); entry[bkt].append(cm)

# drop NPCs left with nothing after cleaning; cap message counts
for key in list(npcs):
    e = npcs[key]
    e["greetings"] = e["greetings"][:60]
    e["quest"] = e["quest"][:120]
    if not e["greetings"] and not e["quest"]:
        del npcs[key]

# cap total NPCs at ~4000, preferring those with the most dialogue
if len(npcs) > 4000:
    ranked = sorted(npcs.items(),
                    key=lambda kv: len(kv[1]["greetings"]) + len(kv[1]["quest"]),
                    reverse=True)[:4000]
    npcs = dict(ranked)

print(f"proper-named NPCs kept {len(npcs):,} · generic/unnamed skipped {skipped:,}")

out = {"npcs": npcs}
path = os.path.join(ROOT, "assets", "acdialogue.json")
json.dump(out, open(path, "w", encoding="utf-8"), ensure_ascii=False)
sz = os.path.getsize(path)
print(f"wrote {path} ({sz/1e6:.2f} MB)")

# sanity: reparse
json.load(open(path, encoding="utf-8"))
tot_g = sum(len(e["greetings"]) for e in npcs.values())
tot_q = sum(len(e["quest"]) for e in npcs.values())
print(f"total lines: greetings={tot_g:,} quest={tot_q:,}")
# prefer NPCs that have quest dialogue for the sample
sample = sorted(npcs, key=lambda k: len(npcs[k]["quest"]), reverse=True)[:3]
for k in sample:
    e = npcs[k]
    print(f"  [{k}] greetings={len(e['greetings'])} quest={len(e['quest'])}")
    for line in (e["greetings"][:1] + e["quest"][:2]):
        print(f"      {line[:110]}")
