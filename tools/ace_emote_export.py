#!/usr/bin/env python3
"""NPC dialogue/quest/chatter extraction -> assets/acdialogue.json

Pulls the actual spoken text of Dereth's named NPCs out of the retail ACE world DB.
v2: reads the per-weenie SQL tree (ACE-World-16PY-master/Database/3-Core/9 WeenieDefaults/SQL)
— the combined acdata/*.sql dump the first version parsed is no longer kept — and adds a
third bucket, `chatter`: category-5 HeartBeat lines, i.e. what an NPC says UNPROMPTED.
That's the authentic ambient town chatter (town criers hawking rumors, barkeeps muttering).

Per-weenie SQL structure (values may carry /* enum-name */ comments):
  INSERT INTO `weenie_properties_emote`  (object_Id, category, probability, weenie_Class_Id,
      style, substyle, quest, vendor_Type, min_Health, max_Health)
  SET @parent_id = LAST_INSERT_ID();
  INSERT INTO `weenie_properties_emote_action` (emote_Id, order, type, delay, extent, motion,
      message, ...)                    -- actions belong to the emote insert just above

`message` is only human-readable speech for certain EmoteTypes; for the rest it's an internal
quest label (e.g. "TuskerMask"). Types that are real dialogue (same set the v1 tool proved out):
  1 act-text, 8 creature speak, 10 tell, 13 "you think", 16 world broadcast,
  17 local narrative, 18 whisper, 64 fellow tell, 65 fellow broadcast, 68 tutorial

Buckets per NPC (keyed by lowercase proper name, from the weenie filename "12345 Name.sql"):
  chatter   — HeartBeat (category 5), non-quest: ambient unprompted speech
  quest     — any emote carrying a quest flag string
  greetings — every other spoken category (Refuse / Vendor / Give / ReceiveTalk / ...)

Output: { npcs: { "<lowercase npc name>": { greetings:[..], quest:[..], chatter:[..] } } }
"""
import os, json, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SQLDIR = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core", "9 WeenieDefaults", "SQL")
OUT = os.path.join(ROOT, "assets", "acdialogue.json")

DIALOGUE_TYPES = {1, 8, 10, 13, 16, 17, 18, 64, 65, 68}
HAS_LETTER = re.compile(r"[A-Za-z]")
SCRIPT_NOISE = re.compile(r"InqQuest|UpdateQuest|Quest(?:Success|Failure)|^\((?:Inq|Update)")

# ---- proper-name filters (unchanged from v1: drop generic monsters & readable objects) ----
GENERIC = re.compile(
    r"\b(drudge|olthoi|banderling|mosswart|tumerok|lugian|virindi|golem|zombie|"
    r"skeleton|shadow|gromnie|mattekar|shreth|reedshark|reed shark|rat|mite|"
    r"sclavus|zefir|monouga|ursuin|moarsman|wisp|mummy|mumiyah|grievver|rabbit|"
    r"auroch|carenzi|niffis|remoran|burun|mukkir|tusker|armoredillo|phyntos|"
    r"guard|monster|creature|generic|template|test|statue|door|chest|portal|"
    r"lifestone|altar|target|dummy|effigy|servant|spectre|specter|wight|"
    r"undead|slave|thrall|minion|spirit|beast|hollow)\b", re.I)
BAD_CHARS = re.compile(r"[<>{}@#]")
OBJECT = re.compile(
    r"\b(note|paper|prism|book|scroll|letter|parchment|tablet|plaque|sign|rock|"
    r"crystal|orb|page|journal|tome|diary|missive|map|key|coffer|corpse|remains|"
    r"body|pile|pool|cache|shrine|fountain|well|lever|switch|pedestal|obelisk|"
    r"gateway|barrel|crate|sack|bag|pouch|urn|jar|bottle|vial|flask|head|hand|"
    r"eye|heart|skull|bone|essence|fragment|shard|relic|totem|idol|brazier|"
    r"candle|torch|lantern|painting|portrait|mirror|banner|tapestry)\b", re.I)
ARTICLE = re.compile(r"^(a|an|the)\s", re.I)


def is_proper(nm):
    if not nm: return False
    if len(nm) < 2 or len(nm) > 48: return False
    if not nm[0].isupper(): return False
    if BAD_CHARS.search(nm): return False
    if ARTICLE.match(nm): return False
    if GENERIC.search(nm): return False
    if OBJECT.search(nm): return False
    return True


def split_top(body):
    """Split a VALUES(...) body into top-level comma fields; quoted text keeps a \\x00 marker."""
    out, cur, q, i, n = [], [], False, 0, len(body)
    while i < n:
        ch = body[i]
        if q:
            if ch == "\\" and i + 1 < n:                 # backslash escape
                cur.append(body[i + 1]); i += 2; continue
            if ch == "'":
                if i + 1 < n and body[i + 1] == "'":     # doubled-quote escape
                    cur.append("'"); i += 2; continue
                q = False; i += 1; continue
            cur.append(ch); i += 1; continue
        if ch == "'": q = True; cur.append("\x00"); i += 1; continue
        if ch == ",": out.append("".join(cur).strip()); cur = []; i += 1; continue
        cur.append(ch); i += 1
    out.append("".join(cur).strip())
    return out


def field_int(f):
    m = re.match(r"(\d+)", re.sub(r"/\*.*?\*/", "", f).strip())
    return int(m.group(1)) if m else None


def field_str(f):
    """quoted field -> its text (split_top marks quoted content with \\x00), else None."""
    if "\x00" not in f: return None
    return f.replace("\x00", "").strip()


UI_NOISE = re.compile(r"double[- ]click|right[- ]click|examine window|@tell", re.I)   # retail-UI references read wrong in this client

def clean(msg):
    if msg is None: return None
    msg = msg.replace("\\n", " ").strip()
    if len(msg) < 3 or not HAS_LETTER.search(msg): return None
    if SCRIPT_NOISE.search(msg): return None
    if UI_NOISE.search(msg): return None
    if len(msg) > 400: msg = msg[:397].rstrip() + "..."
    return msg


def split_rows(stmt):
    """'(r1), (r2), ...' -> ['r1', 'r2', ...] — paren-depth + quote aware."""
    out, cur, depth, q, i, n = [], [], 0, False, 0, len(stmt)
    while i < n:
        ch = stmt[i]
        if q:
            cur.append(ch)
            if ch == "\\" and i + 1 < n: cur.append(stmt[i + 1]); i += 2; continue
            if ch == "'":
                if i + 1 < n and stmt[i + 1] == "'": cur.append("'"); i += 2; continue
                q = False
            i += 1; continue
        if ch == "'":
            q = True; cur.append(ch); i += 1; continue
        if ch == "(":
            depth += 1
            if depth == 1: cur = []; i += 1; continue
        if ch == ")":
            depth -= 1
            if depth == 0: out.append("".join(cur)); i += 1; continue
        if depth >= 1: cur.append(ch)
        i += 1
    return out


def parse_file(path):
    """-> (greetings, quest, chatter) spoken lines for one weenie file.

    A statement's VALUES may batch many rows — `VALUES (r1)\n , (r2)\n , (r3);` —
    so accumulate the whole statement, then split into rows paren-aware."""
    greet, quest, chat = [], [], []
    cat, has_q, mode, pend = None, False, None, None

    def handle(stmt):
        nonlocal cat, has_q
        for row in split_rows(stmt):
            fields = split_top(row)
            if len(fields) < 7: continue
            if mode == "emote":
                cat = field_int(fields[1])                 # category (5 = HeartBeat)
                has_q = field_str(fields[6]) is not None   # quest flag string
            else:
                if field_int(fields[2]) not in DIALOGUE_TYPES: continue   # speech types only
                msg = clean(field_str(fields[6]))          # message
                if msg:
                    (quest if has_q else (chat if cat == 5 else greet)).append(msg)

    for raw in open(path, encoding="utf-8", errors="replace"):
        s = raw.strip()
        if pend is not None:                               # statement continues
            pend += " " + s
            if s.endswith(");"): handle(pend); pend = None
            continue
        if s.startswith("INSERT INTO `weenie_properties_emote_action`"):
            mode = "action"; continue
        if s.startswith("INSERT INTO `weenie_properties_emote`"):
            mode = "emote"; continue
        if not s.startswith("VALUES") or mode is None:
            continue
        if s.endswith(");"): handle(s)
        else: pend = s
    return greet, quest, chat


def main():
    npcs = {}
    scanned = 0
    for dirpath, _, files in os.walk(SQLDIR):
        for fn in files:
            if not fn.endswith(".sql"): continue
            nm = re.sub(r"^\d+\s*", "", fn[:-4]).strip()
            if not is_proper(nm): continue
            path = os.path.join(dirpath, fn)
            with open(path, encoding="utf-8", errors="replace") as fh:   # cheap pre-filter
                if "weenie_properties_emote_action" not in fh.read():
                    continue
            scanned += 1
            g, q, c = parse_file(path)
            if not (g or q or c): continue
            e = npcs.setdefault(nm.lower(), {"greetings": [], "quest": [], "chatter": []})
            for src, bkt in ((g, "greetings"), (q, "quest"), (c, "chatter")):
                seen = set(e[bkt])
                for m in src:
                    if m not in seen:
                        seen.add(m); e[bkt].append(m)

    # caps (v1 parity + chatter)
    for k in list(npcs):
        e = npcs[k]
        e["greetings"] = e["greetings"][:60]
        e["quest"] = e["quest"][:120]
        e["chatter"] = e["chatter"][:40]
        if not (e["greetings"] or e["quest"] or e["chatter"]):
            del npcs[k]
    if len(npcs) > 4000:
        npcs = dict(sorted(npcs.items(), key=lambda kv: sum(len(v) for v in kv[1].values()),
                           reverse=True)[:4000])

    json.dump({"npcs": npcs}, open(OUT, "w", encoding="utf-8"), ensure_ascii=False)
    json.load(open(OUT, encoding="utf-8"))               # sanity reparse
    tg = sum(len(e["greetings"]) for e in npcs.values())
    tq = sum(len(e["quest"]) for e in npcs.values())
    tc = sum(len(e["chatter"]) for e in npcs.values())
    nc = sum(1 for e in npcs.values() if e["chatter"])
    print(f"weenies parsed {scanned:,} -> NPCs {len(npcs):,} "
          f"(greetings={tg:,} quest={tq:,} chatter={tc:,} · {nc:,} NPCs chatter) "
          f"-> {OUT} ({os.path.getsize(OUT)/1e6:.2f} MB)")
    for k in sorted(npcs, key=lambda k: len(npcs[k]["chatter"]), reverse=True)[:5]:
        e = npcs[k]
        print(f"  [{k}] chatter={len(e['chatter'])}")
        for line in e["chatter"][:2]: print(f"      {line[:100]}")


if __name__ == "__main__":
    main()
