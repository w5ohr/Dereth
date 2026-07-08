#!/usr/bin/env python3
"""
Dereth MMO server — authoritative, dependency-free (Python 3 stdlib only).

- WebSocket transport (RFC6455) hand-rolled over asyncio (no `websockets`/`aiohttp` needed).
- Accounts: sqlite3 + scrypt password hashing + in-memory session tokens (resumable).
- Persistence: each account stores an opaque character-state JSON blob (the client's save).
- Realtime: presence (join/leave), chat relay, and a 10 Hz world snapshot of all players.

Run:   python3 server/dereth_server.py            (listens on 0.0.0.0:8787)
Env:   DERETH_HOST, DERETH_PORT, DERETH_DB         (override defaults)

This is Phase M1 (foundation). Monsters/combat become server-authoritative in M3.
"""
import asyncio, base64, hashlib, hmac, json, math, os, random, re, secrets, sqlite3, struct, time

HOST = os.environ.get("DERETH_HOST", "0.0.0.0")
PORT = int(os.environ.get("DERETH_PORT", "8787"))
DB_PATH = os.environ.get("DERETH_DB", os.path.join(os.path.dirname(__file__), "dereth.db"))
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
TICK_HZ = 10
MAX_MSG = 1 << 20  # 1 MiB per message cap (character saves can be sizable)
# WSCALE — world spatial scale; MUST match the client's WSCALE. At 3 the map is true 1:1 AC metre
# spacing (towns 3× farther apart than the old compact map). Only POSITIONS scale — ranges stay metres.
WSCALE = 3
SCRYPT = dict(n=16384, r=8, p=1, dklen=32)
PROTOCOL_VERSION = 2   # v2: accounts own up to 8 character slots (roster/play_char/create_char)

# ---------------------------------------------------------------- persistence
# An *account* (users row: username+password) owns up to MAX_CHARS *characters*
# (characters rows, keyed by account+slot). The login name is the account; each
# character has its own in-world name and save blob.
MAX_CHARS = 8

def db():
    c = sqlite3.connect(DB_PATH)
    c.execute("""CREATE TABLE IF NOT EXISTS users(
        username TEXT PRIMARY KEY, salt TEXT NOT NULL, pw TEXT NOT NULL,
        char TEXT, created INTEGER, seen INTEGER)""")
    c.execute("""CREATE TABLE IF NOT EXISTS characters(
        account TEXT NOT NULL, slot INTEGER NOT NULL, name TEXT,
        data TEXT, created INTEGER, seen INTEGER,
        PRIMARY KEY(account, slot))""")
    c.execute("""CREATE TABLE IF NOT EXISTS allegiance(
        charname TEXT PRIMARY KEY, patron TEXT, motd TEXT,
        sworn_at INTEGER, pending_xp INTEGER DEFAULT 0)""")
    return c

def hash_pw(password: str, salt: bytes) -> str:
    return hashlib.scrypt(password.encode("utf-8"), salt=salt, **SCRYPT).hex()

def create_user(username, password):
    salt = secrets.token_bytes(16)
    with db() as c:
        try:
            c.execute("INSERT INTO users(username,salt,pw,char,created,seen) VALUES(?,?,?,?,?,?)",
                      (username, salt.hex(), hash_pw(password, salt), None, int(time.time()), int(time.time())))
            return True
        except sqlite3.IntegrityError:
            return False

def verify_user(username, password):
    with db() as c:
        row = c.execute("SELECT salt,pw FROM users WHERE username=?", (username,)).fetchone()
    if not row:
        return False
    salt_hex, pw_hex = row
    return hmac.compare_digest(hash_pw(password, bytes.fromhex(salt_hex)), pw_hex)

# ── default admin: always keep an "Admin" account; seed a maxed "Kilmer" character in slot 0 ──
ADMIN_USER = "Admin"
ADMIN_PW = "Tatia0623!"
ADMIN_CHAR_PATH = os.path.join(os.path.dirname(__file__), "admin_kilmer.json")

def seed_admin():
    """Enforce the default Admin account + password on every start, and place the maxed Kilmer
    character in slot 0 if that slot is empty (so any in-game progress the admin makes persists)."""
    kilmer = None
    try:
        with open(ADMIN_CHAR_PATH, encoding="utf-8") as f:
            kilmer = f.read()
            json.loads(kilmer)  # validate it parses before we store it
    except (OSError, ValueError) as e:
        print(f"seed: admin_kilmer.json unavailable ({e}) — Admin account only, no character")
        kilmer = None
    now = int(time.time())
    salt = secrets.token_bytes(16)
    with db() as c:
        if c.execute("SELECT 1 FROM users WHERE username=?", (ADMIN_USER,)).fetchone():
            c.execute("UPDATE users SET salt=?, pw=? WHERE username=?",
                      (salt.hex(), hash_pw(ADMIN_PW, salt), ADMIN_USER))          # keep the password current
        else:
            c.execute("INSERT INTO users(username,salt,pw,char,created,seen) VALUES(?,?,?,?,?,?)",
                      (ADMIN_USER, salt.hex(), hash_pw(ADMIN_PW, salt), None, now, now))
        if kilmer is not None and not c.execute(
                "SELECT 1 FROM characters WHERE account=? AND slot=0", (ADMIN_USER,)).fetchone():
            c.execute("INSERT INTO characters(account,slot,name,data,created,seen) VALUES(?,?,?,?,?,?)",
                      (ADMIN_USER, 0, "Kilmer", kilmer, now, now))
            print("seed: Admin account ensured; Kilmer (max) seeded in slot 0")
        else:
            print("seed: Admin account ensured" + ("; Kilmer already present" if kilmer else ""))

def migrate_legacy(account):
    """Seed slot 0 from a pre-multichar users.char blob (once), so old saves survive."""
    with db() as c:
        if c.execute("SELECT COUNT(*) FROM characters WHERE account=?", (account,)).fetchone()[0]:
            return
        row = c.execute("SELECT char FROM users WHERE username=?", (account,)).fetchone()
        if row and row[0]:
            c.execute("INSERT INTO characters(account,slot,name,data,created,seen) VALUES(?,?,?,?,?,?)",
                      (account, 0, account, row[0], int(time.time()), int(time.time())))

def _char_summary(name, data_str):
    try:
        d = json.loads(data_str) if data_str else {}
    except Exception:
        d = {}
    return {"name": name, "level": d.get("level", 1), "title": d.get("title", ""),
            "heritage": d.get("heritage", "aluvian"), "kills": d.get("kills", 0)}

def roster(account):
    migrate_legacy(account)
    with db() as c:
        rows = c.execute("SELECT slot,name,data FROM characters WHERE account=? ORDER BY slot", (account,)).fetchall()
    return [dict(slot=r[0], **_char_summary(r[1], r[2])) for r in rows]

def load_char_slot(account, slot):
    with db() as c:
        row = c.execute("SELECT name,data FROM characters WHERE account=? AND slot=?", (account, slot)).fetchone()
    if not row:
        return None
    try:
        data = json.loads(row[1]) if row[1] else None
    except Exception:
        data = None
    return {"name": row[0], "data": data}

RESERVED_NAMES = {"kilmer"}   # #354: admin/castle names only the ADMIN_USER account may hold (the client grants Castle Val Halla ownership purely on player.name === "Kilmer")

def create_char_slot(account, slot, name, data):
    if not isinstance(slot, int) or slot < 0 or slot >= MAX_CHARS:
        return False, "Invalid character slot."
    # #354: reserve admin/system names — otherwise any account could create a "Kilmer" character and
    # the client's castleAccess() would hand it full Castle Val Halla ownership + its materialized hoard.
    if name.strip().lower() in RESERVED_NAMES and account != ADMIN_USER:
        return False, "That name is reserved."
    with db() as c:
        if c.execute("SELECT 1 FROM characters WHERE account=? AND slot=?", (account, slot)).fetchone():
            return False, "That slot is already occupied."
        # #354/#310: character names are globally unique — a name held by ANY other account is taken
        # (allegiance/skill state is keyed by charname, and castle ownership keys on it).
        if c.execute("SELECT 1 FROM characters WHERE LOWER(name)=LOWER(?) AND account!=?", (name, account)).fetchone():
            return False, "That name is already taken."
        if c.execute("SELECT 1 FROM characters WHERE account=? AND name=?", (account, name)).fetchone():
            return False, "You already have a character with that name."
        c.execute("INSERT INTO characters(account,slot,name,data,created,seen) VALUES(?,?,?,?,?,?)",
                  (account, slot, name, json.dumps(sanitize_save(data)) if data is not None else None, int(time.time()), int(time.time())))
    return True, None

def _svnum(v, lo, hi, default):
    """parse a number and clamp to [lo,hi]; NaN/Inf/garbage -> default."""
    try:
        f = float(v)
    except (TypeError, ValueError):
        return default
    if not math.isfinite(f):
        return default
    return max(lo, min(hi, f))

# #238: bound the economy-critical fields of a client-supplied character save so the crudest
# forgeries — infinite/NaN gold, level>275, maxed skills, oversized inventory, giant stacks — can't be
# persisted. This is anti-tamper hardening, NOT full server authority: a cheater can still save
# PLAUSIBLE forged values (that needs server-side simulation, tracked in #238). Clamps in place, never
# raises, and leaves cosmetic / progression / unknown fields untouched.
_SV_INT = {"level": (1, 275), "skillPts": (0, 1e9), "kills": (0, 1e9), "bossKills": (0, 1e9),
           "championKills": (0, 1e9), "delves": (0, 1e9), "materials": (0, 1e8), "luminance": (0, 1e6)}
_SV_FLOAT = {"xp": (0, 1e13), "xpUnspent": (0, 1e13), "gold": (0, 2e9), "vitae": (0.0, 0.40)}

def sanitize_save(data):
    if not isinstance(data, dict):
        return data
    d = data
    try:
        for k, (lo, hi) in _SV_INT.items():
            if k in d:
                d[k] = int(_svnum(d[k], lo, hi, lo))
        for k, (lo, hi) in _SV_FLOAT.items():
            if k in d:
                d[k] = _svnum(d[k], lo, hi, lo)
        sk = d.get("skills")
        if isinstance(sk, dict):
            for key, e in list(sk.items()):
                if isinstance(e, dict):
                    e["t"] = int(_svnum(e.get("t", 0), 0, 2, 0))          # untrained/trained/specialized
                    e["xp"] = _svnum(e.get("xp", 0), 0, 1e11, 0)
                else:
                    sk.pop(key, None)
        elif sk is not None:
            d["skills"] = {}
        for akey in ("attr", "attrInnate"):
            at = d.get(akey)
            if isinstance(at, dict):
                for a in list(at.keys()):
                    at[a] = _svnum(at[a], 1, 1000, 10)
        vit = d.get("vitals")
        if isinstance(vit, dict):
            for a in list(vit.keys()):
                vit[a] = int(_svnum(vit.get(a, 0), 0, 1e6, 0))
        inv = d.get("inv")
        if isinstance(inv, list):
            if len(inv) > 500:
                d["inv"] = inv = inv[:500]                                 # cap satchel + packs generously
            for it in inv:
                if isinstance(it, dict) and "count" in it:
                    it["count"] = int(_svnum(it["count"], 1, 100000, 1))   # no giant stacks
        elif inv is not None:
            d["inv"] = []
        packs = d.get("packs")
        if isinstance(packs, list) and len(packs) > 7:
            d["packs"] = packs[:7]                                         # AC caps side packs at 7
    except Exception as e:
        print(f"[sanitize_save] {e}")
    return d

# #238: bound an untrusted item dict that will be relayed to ANOTHER player (trade offers, corpse
# loot). Not schema-aware — legendaries/tinkered items carry custom stats — so it just clamps every
# numeric leaf to a sane range, truncates strings, and caps depth/key/list sizes, so a fabricated item
# can't ship absurd stats or giant strings to an honest client. Returns a fresh sanitized copy.
_ITEM_STRMAX = 64
def _clampleaf(v, key=None):
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        f = float(v)
        if not math.isfinite(f):
            return 0
        if key == "count":
            return int(max(1, min(100000, f)))
        if key == "tinks":
            return int(max(0, min(10, f)))
        return max(-1e6, min(1e6, f))
    if isinstance(v, str):
        return v[:_ITEM_STRMAX]
    return None

def sanitize_item(it, _depth=0):
    if not isinstance(it, dict) or _depth > 4:
        return {} if isinstance(it, dict) else it
    out = {}
    for k, v in list(it.items())[:60]:                      # cap key count
        if not isinstance(k, str) or len(k) > 40:
            continue
        if isinstance(v, dict):
            out[k] = sanitize_item(v, _depth + 1)
        elif isinstance(v, list):
            out[k] = [sanitize_item(x, _depth + 1) if isinstance(x, dict) else _clampleaf(x)
                      for x in v[:60]]
        else:
            lv = _clampleaf(v, k)
            if lv is not None:
                out[k] = lv
    return out

# ── M3 (#238) authoritative economy helpers ──────────────────────────────────────────────────────
def load_econ(cl, data):
    """Adopt coin + inventory from a character save as the server's authoritative baseline."""
    d = data if isinstance(data, dict) else {}
    cl.coin = int(_svnum(d.get("gold", 0), 0, 2e9, 0))
    inv = d.get("inv")
    cl.inv = [sanitize_item(it) for it in inv if isinstance(it, dict)][:500] if isinstance(inv, list) else []
    cl.econ_ready = True

async def push_coin(cl):
    """Tell a client its authoritative pyreal balance (client mirrors it into player.gold)."""
    if cl.econ_ready:
        await cl.send({"t": "coin", "coin": int(cl.coin)})

def _item_sig(it):
    """A coarse identity for matching an offered item against the authoritative inventory."""
    if not isinstance(it, dict):
        return ("", "", 0)
    return (str(it.get("name", "")), str(it.get("stat", "")), int(_svnum(it.get("v", 0), -1e9, 1e9, 0)))

def take_owned(cl, offered):
    """Try to remove each offered item from cl.inv by signature (one entry per offered item).
    Returns (ok, removed_items). On failure (an offered item isn't owned = fabricated) removes
    nothing. When econ isn't loaded, allows the offer unchanged (legacy)."""
    if not cl or not getattr(cl, "econ_ready", False):
        return True, list(offered)
    work = list(cl.inv)
    removed = []
    for off in offered:
        sig = _item_sig(off)
        idx = next((i for i, it in enumerate(work) if _item_sig(it) == sig), -1)
        if idx < 0:
            return False, []          # not in the authoritative inventory → fabricated
        removed.append(work.pop(idx))
    cl.inv = work
    return True, removed

def take_owned_filter(cl, items):
    """Forgiving variant for death drops: keep only the items actually in cl.inv (removing them),
    silently drop any that aren't owned (fabricated). Returns the kept list."""
    if not cl or not getattr(cl, "econ_ready", False):
        return list(items)
    work = list(cl.inv)
    kept = []
    for it in items:
        sig = _item_sig(it)
        idx = next((i for i, x in enumerate(work) if _item_sig(x) == sig), -1)
        if idx >= 0:
            kept.append(work.pop(idx))
    cl.inv = work
    return kept

def catalog_val(it):
    """Best-effort ceiling on an item's worth, to cap the pyreals a vendor pays for it (so a client
    can't claim a huge sell price). Uses the retail catalog value when the item is known, else a
    bound derived from the item's own (already-sanitized, <=1e6) value field."""
    try:
        a = AC_ITEMS.get(str(it.get("name", "")).lower()) if AC_ITEMS else None
        if a and a.get("val"):
            return max(1, int(a["val"]))
    except Exception:
        pass
    return max(1, int(_svnum(it.get("v", 0), 0, 1_000_000, 0)) * 4)

def save_char_slot(account, slot, data):
    data = sanitize_save(data)
    with db() as c:
        c.execute("UPDATE characters SET data=?, seen=? WHERE account=? AND slot=?",
                  (json.dumps(data), int(time.time()), account, slot))

def delete_char_slot(account, slot):
    with db() as c:
        c.execute("DELETE FROM characters WHERE account=? AND slot=?", (account, slot))

# ---------------------------------------------------------------- websocket
async def ws_handshake(reader, writer) -> bool:
    """Read the HTTP upgrade request and reply 101 Switching Protocols."""
    try:
        line = await asyncio.wait_for(reader.readline(), timeout=10)
    except asyncio.TimeoutError:
        return False
    if not line.startswith(b"GET"):
        return False
    headers = {}
    hdr_count = 0
    while True:
        try:
            h = await asyncio.wait_for(reader.readline(), timeout=10)   # #298: bound each header read — a client that dribbles headers (never the blank line) could otherwise pin a task forever
        except asyncio.TimeoutError:
            return False
        if h in (b"\r\n", b"\n", b""):
            break
        hdr_count += 1
        if hdr_count > 60 or len(h) > 8192:   # #298: cap header count + line length
            return False
        k, _, v = h.decode("latin1").partition(":")
        headers[k.strip().lower()] = v.strip()
    key = headers.get("sec-websocket-key")
    if not key:
        return False
    accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
    writer.write(("HTTP/1.1 101 Switching Protocols\r\n"
                  "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())
    await writer.drain()
    return True

async def ws_read(reader):
    """Read one WebSocket message (handles fragmentation, masking, control frames).
    Returns (opcode, bytes) or None on close/error. Control frames return their own opcode."""
    data = bytearray()
    first_opcode = None
    while True:
        hdr = await reader.readexactly(2)
        b0, b1 = hdr[0], hdr[1]
        fin = b0 & 0x80
        opcode = b0 & 0x0F
        masked = b1 & 0x80
        length = b1 & 0x7F
        # #298: once a frame header has arrived, the rest of that frame must arrive promptly — a
        # slowloris that advertises a large length then feeds the body one byte at a time would
        # otherwise pin the connection. (No timeout on the 2-byte header read above: an idle
        # connection legitimately blocks there waiting for the next message.)
        try:
            if length == 126:
                length = struct.unpack(">H", await asyncio.wait_for(reader.readexactly(2), timeout=30))[0]
            elif length == 127:
                length = struct.unpack(">Q", await asyncio.wait_for(reader.readexactly(8), timeout=30))[0]
            if length > MAX_MSG or len(data) + length > MAX_MSG:
                return None
            if opcode >= 0x8 and length > 125:   # #299: RFC6455 caps control frames (close/ping/pong) at 125 bytes — reject oversized ones so a ~1MiB "ping" can't be reflected as a ~1MiB pong
                return None
            mask = (await asyncio.wait_for(reader.readexactly(4), timeout=30)) if masked else b"\x00\x00\x00\x00"
            payload = bytearray(await asyncio.wait_for(reader.readexactly(length), timeout=30))
        except asyncio.TimeoutError:
            return None
        if masked:
            for i in range(length):
                payload[i] ^= mask[i & 3]
        if opcode == 0x8:  # close
            return (0x8, bytes(payload))
        if opcode in (0x9, 0xA):  # ping/pong are standalone control frames
            return (opcode, bytes(payload))
        if opcode != 0x0:  # new data frame
            first_opcode = opcode
        data += payload
        if fin:
            return (first_opcode if first_opcode is not None else opcode, bytes(data))

def ws_frame(payload: bytes, opcode=0x1) -> bytes:
    n = len(payload)
    out = bytearray([0x80 | opcode])
    if n < 126:
        out.append(n)
    elif n < (1 << 16):
        out.append(126); out += struct.pack(">H", n)
    else:
        out.append(127); out += struct.pack(">Q", n)
    out += payload
    return bytes(out)

# ---------------------------------------------------------------- game state
TOKENS = {}            # token -> username
CLIENTS = {}           # username -> Client (one active session per account)

# #239: rate-limit config (token buckets, per connection)
RL_GEN_RATE = 30.0    # sustained messages/sec (input/attack/etc.)
RL_GEN_BURST = 60.0   # burst capacity
RL_CHAT_RATE = 2.0    # sustained "chatty" broadcast messages/sec
RL_CHAT_BURST = 6.0   # burst capacity for chat/emote/tell/party/allegiance
CHATTY = {"chat", "emote", "tell", "pchat", "achat", "alg_motd"}   # messages that fan out to others

class Client:
    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self.username = None    # account (login) name
        self.token = None
        self.alive = True
        self.charname = None    # active character's in-world name
        self.slot = None        # active character slot (0..MAX_CHARS-1)
        self.in_world = False    # True once a character is selected/created (else: at char-select)
        self.party = None       # party id (or None)
        self.invite_from = None  # pending party invite (inviter account)
        # presence state (last reported by the client; M3 will make this authoritative)
        self.x = 0.0; self.z = 0.0; self.yaw = 0.0; self.hp = 100; self.mhp = 100
        self.level = 1; self.heritage = "aluvian"; self.title = ""
        self.wt = None; self.wmode = "sword"; self.shield = None   # wield: weapon type, stance, offhand shield type
        # #239: per-connection token buckets (refilled by elapsed time in the read loop). One general
        # bucket caps total message rate; a tighter one caps "chatty" broadcast messages (chat/emote/
        # tell/party/allegiance) that fan out to every client. Start full so a normal session never trips.
        self.rl_gen = RL_GEN_BURST
        self.rl_chat = RL_CHAT_BURST
        self.rl_t = time.time()
        self.rl_warned = 0.0     # last time we told this client it was thottled (rate-limit the warning too)
        # M3 (#238) server-authoritative economy state. Loaded from the character's save on enter_world,
        # credited by server-controlled events (loot pickup, kill gold, trade), resynced from the save
        # while the intent stages are still being built, and (at cutover) the sole source of truth the
        # server persists. `coin` = pyreals; `inv` = the authoritative inventory list.
        self.coin = 0
        self.inv = []
        self.econ_ready = False   # True once loaded from a save; gates authoritative bookkeeping

    async def send(self, obj):
        if not self.alive:
            return
        try:
            self.writer.write(ws_frame(json.dumps(obj).encode("utf-8")))
            await self.writer.drain()
        except Exception:
            self.alive = False

async def broadcast(obj, exclude=None):
    # only players who have entered the world (picked a character) get world traffic;
    # accounts sitting at the character-select screen are skipped.
    dead = []
    for u, cl in list(CLIENTS.items()):
        if cl is exclude or not cl.in_world:
            continue
        await cl.send(obj)
        if not cl.alive:
            dead.append(u)
    for u in dead:
        CLIENTS.pop(u, None)

def mob_pub(m):
    d = {"id": m["id"], "kind": m["kind"], "x": round(m["x"], 2), "z": round(m["z"], 2),
         "yaw": round(m["yaw"], 3), "hp": round(m["hp"], 1), "mhp": m["mhp"], "st": m["state"]}
    if m.get("boss"):
        d["boss"] = True; d["name"] = m["name"]; d["scale"] = m["scale"]
        if m.get("tint") is not None:
            d["tint"] = m["tint"]
    if m.get("event"):
        d["event"] = m["event"]
    return d

def snapshot():
    snap = {"t": "snapshot", "players": [
        {"id": u, "name": cl.charname or u, "x": round(cl.x, 2), "z": round(cl.z, 2), "yaw": round(cl.yaw, 3),
         "hp": cl.hp, "mhp": cl.mhp, "level": cl.level, "heritage": cl.heritage, "title": cl.title,
         "pk": getattr(cl, "pk", False), "pkState": getattr(cl, "pkState", "npk"),
         "wt": getattr(cl, "wt", None), "wmode": getattr(cl, "wmode", "sword"), "shield": getattr(cl, "shield", None)}
        for u, cl in CLIENTS.items() if cl.in_world],
        "mobs": [mob_pub(m) for m in MOBS.values() if m["hp"] > 0]}
    if EVENT.get("active"):
        snap["event"] = {"id": EVENT["id"], "name": EVENT["name"], "x": EVENT["x"], "z": EVENT["z"],
                         "col": EVENT["col"], "total": EVENT["total"]}
    return snap

# ---------------------------------------------------------------- world: shared monsters
# Server-authoritative monster sim (M3). Stats mirror the client BESTIARY subset so the
# browser can render the same creature by `kind`. Positions, HP, AI and combat are owned
# here so every player shares one world (and damage can't be faked by a client).
DT = 1.0 / TICK_HZ
MOB_BESTIARY = {
    "drudge":    {"hp": 34,  "dmg": 7,  "spd": 5.0, "xp": 120, "gold": (2, 9),   "size": 0.9, "sense": 34, "atk": 1.3},
    "mosswart":  {"hp": 52,  "dmg": 11, "spd": 4.3, "xp": 200, "gold": (4, 14),  "size": 1.0, "sense": 36, "atk": 1.4},
    "reedshark": {"hp": 70,  "dmg": 14, "spd": 8.0, "xp": 300, "gold": (6, 18),  "size": 1.1, "sense": 42, "atk": 1.2},
    "banderling":{"hp": 120, "dmg": 20, "spd": 6.0, "xp": 520, "gold": (12, 32), "size": 1.3, "sense": 44, "atk": 1.5},
    "skeleton":  {"hp": 80,  "dmg": 16, "spd": 6.0, "xp": 360, "gold": (6, 18),  "size": 1.0, "sense": 44, "atk": 1.4},
    "tusker":    {"hp": 160, "dmg": 24, "spd": 5.0, "xp": 620, "gold": (14, 40), "size": 1.7, "sense": 43, "atk": 1.6},
}
# Authentic pack parity: when assets/acrewards.json exists (extracted from the player's own
# AC data by tools/ace_reward_export.py), shared mobs use the same retail hp/dmg/kill-XP the
# offline client uses — online and offline kills pay identically.
try:
    import json as _json
    _rw = _json.load(open(os.path.join(os.path.dirname(__file__), "..", "assets", "acrewards.json")))
    for _k, _e in _rw.get("killxp", {}).items():
        if _k in MOB_BESTIARY:
            MOB_BESTIARY[_k]["xp"] = _e["xp"]
            if _e.get("hp"): MOB_BESTIARY[_k]["hp"] = _e["hp"]
            if _e.get("dmg"): MOB_BESTIARY[_k]["dmg"] = _e["dmg"]
    print("acrewards pack: retail hp/dmg/xp applied to %d shared mob kinds"
          % sum(1 for k in _rw.get("killxp", {}) if k in MOB_BESTIARY))
except Exception:
    pass
# Monsters cluster near real Dereth towns (world coords = lon*80, -lat*80) so the shared
# population is where players actually spawn/travel — not out at the empty origin.
MOB_CLUSTERS = [(x * WSCALE, z * WSCALE) for x, z in [
    (2640, -3488),  # Holtburg (Aluvian capital — common spawn)
    (3768, -2072),  # Cragstone
    (2048, -2424),  # Glenden Wood
    (5136, -1528),  # Eastham
    (4744, -880),   # Shoushi (Sho capital)
    (3704, 968),    # Yaraq (Gharu'ndim capital)
    (1472, 128),    # Samsur
    (4824, 2296),   # Sawato
]]
MOBS = {}              # id -> mob dict
_mob_seq = 0
WORLD_LIMIT = 7000 * WSCALE   # keep mobs inside the (now 1:1) playfield
# capitals are safe havens — creatures are pushed out of the town core (mirrors the client)
CAPITALS = [(x * WSCALE, z * WSCALE) for x, z in [(2640, -3488), (4744, -880), (3704, 968)]]   # Holtburg, Shoushi, Yaraq
TOWN_SAFE = 60.0
ATTACK_RANGE = 16.0    # max client→mob distance accepted for an attack intent (melee+ranged+latency)
FELLOW_RANGE = 150.0   # party members within this range of a kill share its XP

def spawn_mob(kind=None, near=None):
    global _mob_seq
    _mob_seq += 1
    if kind is None:
        kind = random.choice(list(MOB_BESTIARY))
    b = MOB_BESTIARY[kind]
    cx, cz = near if near else random.choice(MOB_CLUSTERS)
    a, rr = random.uniform(0, 6.28), random.uniform(90, 440)   # ring around the hub, clear of the town core
    x = max(-WORLD_LIMIT, min(WORLD_LIMIT, cx + math.cos(a) * rr))
    z = max(-WORLD_LIMIT, min(WORLD_LIMIT, cz + math.sin(a) * rr))
    mid = "m%d" % _mob_seq
    MOBS[mid] = {
        "id": mid, "kind": kind, "x": x, "z": z, "hx": cx, "hz": cz, "yaw": a,
        "hp": float(b["hp"]), "mhp": b["hp"], "dmg": b["dmg"], "spd": b["spd"], "xp": b["xp"],
        "gold": b["gold"], "r": b["size"] * 0.8, "sense": b["sense"], "atkcd_max": b["atk"],
        "state": "wander", "target": None, "atkcd": 0.0, "wt": 0.0, "respawn_at": 0.0}
    return MOBS[mid]

def populate_world():
    if MOBS:
        return
    for c in MOB_CLUSTERS:
        for _ in range(6):
            spawn_mob(near=c)
    for key in BOSS_DEFS:
        spawn_boss(key)

# Shared world bosses — every online player fights the same named bosses. The browser
# renders any boss generically from the snapshot (boss/name/scale/tint), so adding bosses
# here is purely server content. `kind` only needs to exist in the client BESTIARY.
BOSS_DEFS = {
    # lairs sit out in the wilds between towns (reachable, away from the safe town cores)
    "queen":   {"name": "Gnawvil, the Olthoi Queen",   "kind": "olthoi", "hp": 4000, "dmg": 45, "spd": 5.6, "xp": 4000,  "gold": (140, 300),  "size": 2.0, "sense": 80, "atk": 1.4, "scale": 2.2, "home": (4200, -1400), "respawn": 90.0},
    "apex":    {"name": "Bael'Zharon, the Hopeslayer",  "kind": "shadow", "hp": 9000, "dmg": 70, "spd": 5.0, "xp": 30000, "gold": (800, 1500), "size": 1.0, "sense": 90, "atk": 1.5, "scale": 3.6, "home": (3200, 3200),  "respawn": 150.0, "tint": 0x7a4fae},
    "genLer":  {"name": "Ler Rhan, Shadow General",     "kind": "shadow", "hp": 1800, "dmg": 42, "spd": 6.0, "xp": 11000, "gold": (260, 520),  "size": 1.0, "sense": 80, "atk": 1.4, "scale": 2.1, "home": (1800, -1200), "respawn": 120.0, "tint": 0x8a5fc8},
    "genFerah":{"name": "Black Ferah, Shadow General",  "kind": "shadow", "hp": 1800, "dmg": 42, "spd": 6.0, "xp": 11000, "gold": (260, 520),  "size": 1.0, "sense": 80, "atk": 1.4, "scale": 2.1, "home": (5200, 600),   "respawn": 120.0, "tint": 0x4a3a6a},
    "genIsin": {"name": "Isin Dule, Shadow General",    "kind": "shadow", "hp": 1800, "dmg": 42, "spd": 6.0, "xp": 11000, "gold": (260, 520),  "size": 1.0, "sense": 80, "atk": 1.4, "scale": 2.1, "home": (2800, 1800),  "respawn": 120.0, "tint": 0xc05fae},
}
for _b in BOSS_DEFS.values():   # boss anchor positions scale to the 1:1 world too
    _hx, _hz = _b["home"]; _b["home"] = (_hx * WSCALE, _hz * WSCALE)

def spawn_boss(key):
    b = BOSS_DEFS[key]
    cx, cz = b["home"]
    MOBS[key] = {
        "id": key, "kind": b["kind"], "x": cx, "z": cz, "hx": cx, "hz": cz, "yaw": 0.0,
        "hp": float(b["hp"]), "mhp": b["hp"], "dmg": b["dmg"], "spd": b["spd"], "xp": b["xp"],
        "gold": b["gold"], "r": b["size"] * 0.8 * b["scale"], "sense": b["sense"], "atkcd_max": b["atk"],
        "state": "wander", "target": None, "atkcd": 0.0, "wt": 0.0, "respawn_at": 0.0,
        "boss": True, "bosskey": key, "name": b["name"], "scale": b["scale"]}
    if "tint" in b:
        MOBS[key]["tint"] = b["tint"]
    return MOBS[key]

# Shared world events (Incursions): a finite horde besieges a location; every online
# player races to clear it before the beacon fades for a shared bounty.
EVENT_TYPES = [
    {"name": "Shadow Incursion", "kinds": ["skeleton", "mosswart"], "col": 0x9b30ff, "blurb": "Shadows pour from a tear in the world above"},
    {"name": "Olthoi Swarm", "kinds": ["reedshark", "tusker"], "col": 0xff5a2a, "blurb": "An Olthoi hive boils over"},
    {"name": "Banderling Raid", "kinds": ["banderling", "drudge"], "col": 0xffc14a, "blurb": "A savage warband descends"},
]
EVENT_ANCHORS = [(2640, -3488), (3768, -2072), (4744, -880), (3704, 968), (2048, -2424), (5136, -1528)]  # Incursions besiege towns
EVENT_TTL = 240.0          # seconds to clear before the Incursion fades
EVENT_COUNT = 8
EVENT = {"active": False}
_event_seq = 0
event_cd = float(os.environ.get("DERETH_EVENT_CD", "60"))   # seconds until the first Incursion once players are present

def event_pub():
    return {"t": "event_start", "id": EVENT["id"], "name": EVENT["name"], "x": EVENT["x"], "z": EVENT["z"],
            "col": EVENT["col"], "blurb": EVENT["blurb"], "count": EVENT["total"],
            "ttl": max(0, round(EVENT["deadline"] - time.time()))}

def start_event():
    global _event_seq
    _event_seq += 1
    et = random.choice(EVENT_TYPES)
    ax, az = random.choice(EVENT_ANCHORS)
    EVENT.clear()
    EVENT.update({"active": True, "id": "e%d" % _event_seq, "name": et["name"], "x": ax, "z": az,
                  "col": et["col"], "blurb": et["blurb"], "deadline": time.time() + EVENT_TTL, "total": EVENT_COUNT})
    for _ in range(EVENT_COUNT):
        m = spawn_mob(kind=random.choice(et["kinds"]), near=(ax, az))
        m["event"] = EVENT["id"]
        m["mhp"] = int(m["mhp"] * 1.5); m["hp"] = float(m["mhp"]); m["xp"] = int(m["xp"] * 1.4)
    return EVENT

def event_alive():
    eid = EVENT.get("id")
    return sum(1 for m in MOBS.values() if m.get("event") == eid and m["hp"] > 0)

async def end_event(success):
    if not EVENT.get("active"):
        return
    eid = EVENT["id"]; ex, ez = EVENT["x"], EVENT["z"]; name = EVENT["name"]
    await broadcast({"t": "event_end", "id": eid, "success": success, "x": ex, "z": ez, "name": name})
    if success:
        xp = 900; gold = 220
        await broadcast({"t": "system", "msg": f"The {name} has been repelled! Defenders share a bounty of {xp} XP."})
        for cl in list(CLIENTS.values()):
            if cl.in_world:
                if cl.econ_ready:
                    cl.coin = min(2_000_000_000, cl.coin + gold)   # #297: credit the reward to the authoritative balance so a later absolute coin push (e.g. picking up the ground spoils) can't erase it
                await cl.send({"t": "event_reward", "xp": xp, "gold": gold, "name": name,
                               "authCoin": (int(cl.coin) if cl.econ_ready else None)})
        # spoils on the ground at the breach
        for d in (make_drop(ex, ez, "gold", amt=gold),
                  make_drop(ex + 2, ez, "item", item=roll_item(True, 5)),
                  make_drop(ex - 2, ez, "item", item=roll_item(True, 5))):
            await broadcast(drop_pub(d))
    else:
        await broadcast({"t": "system", "msg": f"The {name} faded back into the wilds before it could be stopped."})
        for m in [x for x in MOBS.values() if x.get("event") == eid]:
            MOBS.pop(m["id"], None)
    EVENT.clear(); EVENT["active"] = False

async def step_events():
    global event_cd
    if EVENT.get("active"):
        if event_alive() <= 0:
            await end_event(True)
        elif time.time() >= EVENT["deadline"]:
            await end_event(False)
    elif CLIENTS:
        event_cd -= DT
        if event_cd <= 0:
            event_cd = random.uniform(170, 280)
            start_event()
            await broadcast(event_pub())
            await broadcast({"t": "system", "msg": f"Incursion! {EVENT['blurb']}. Repel it before the beacon fades!"})

# ---------------------------------------------------------------- loot (server-authoritative)
# Items are plain JSON matching the client schema {name,stat,v,work,mat,tier,wt?,at?,affix?},
# so the browser renders/equips/salvages them unchanged. roll_item mirrors index.html's rollItem.
ITEM_PREFIX = ["Rusty", "Sturdy", "Keen", "Blessed", "Singularity", "Olthoi", "Acid-Etched", "Royal"]
ITEM_BASE = [
    {"n": "Buckler", "stat": "armor", "v": [3, 9]},
    {"n": "Health Kit", "stat": "hp", "v": [30, 70]}, {"n": "Mana Stone", "stat": "mn", "v": [30, 70]},
    {"n": "Coordination Gem", "stat": "Coordination", "v": [1, 1]}, {"n": "Focusing Stone", "stat": "Focus", "v": [1, 1]},
    {"n": "Dagger", "stat": "weapon", "wt": "dagger", "v": [4, 10]}, {"n": "Sword", "stat": "weapon", "wt": "sword", "v": [5, 13]},
    {"n": "Spear", "stat": "weapon", "wt": "spear", "v": [6, 14]}, {"n": "Axe", "stat": "weapon", "wt": "axe", "v": [7, 16]},
    {"n": "War Mace", "stat": "weapon", "wt": "mace", "v": [8, 18]}, {"n": "Long Bow", "stat": "weapon", "wt": "bow", "v": [5, 13]},
    {"n": "Crossbow", "stat": "weapon", "wt": "crossbow", "v": [8, 18]},
    {"n": "Leather Jerkin", "stat": "worn", "at": "light", "v": [6, 15]}, {"n": "Quilted Robe", "stat": "worn", "at": "light", "v": [4, 12]},
    {"n": "Chainmail", "stat": "worn", "at": "medium", "v": [8, 22]}, {"n": "Scale Mail", "stat": "worn", "at": "medium", "v": [10, 24]},
    {"n": "Plate Hauberk", "stat": "worn", "at": "heavy", "v": [14, 34]}]
ITEM_MATERIALS = {"weapon": ["Iron", "Pyreal", "Jet", "Diamond"], "armor": ["Bronze", "Granite", "Leather", "Steel"], "other": ["Amber", "Quartz", "Marble"]}
WORK_TIER = [None, [1, 4], [2, 6], [3, 7], [3, 9], [4, 10]]
WEAPON_AFFIXES = [
    {"k": "brand", "element": "fire", "suffix": "of Flame", "desc": "fire damage + burn"},
    {"k": "brand", "element": "frost", "suffix": "of Frost", "desc": "frost damage + chill"},
    {"k": "brand", "element": "shock", "suffix": "of Storms", "desc": "shock damage + stun"},
    {"k": "brand", "element": "nether", "suffix": "of the Void", "desc": "nether damage + drain"},
    {"k": "crit", "v": 0.12, "suffix": "of Precision", "desc": "+12% critical chance"},
    {"k": "lifesteal", "v": 0.22, "suffix": "of the Leech", "desc": "22% life steal"}]
WORN_AFFIXES = [
    {"k": "vital", "stat": "mhp", "v": 28, "suffix": "of Vigor", "desc": "+28 max health"},
    {"k": "vital", "stat": "mst", "v": 34, "suffix": "of Endurance", "desc": "+34 max stamina"},
    {"k": "vital", "stat": "mmn", "v": 28, "suffix": "of the Magus", "desc": "+28 max mana"},
    {"k": "armorbonus", "v": 9, "suffix": "of Warding", "desc": "+9 armor"}]

def _mat_class(stat):
    return "weapon" if stat == "weapon" else ("armor" if stat in ("worn", "armor") else "other")

# Spell-scroll ids must match the client's generated SPELLBOOK ids exactly. The client
# resolves the spell's display name from the id, so the server only needs the id.
# AC has 8 spell levels; the client generates every line I..VIII. Mirror that here,
# then drop the starter spells the player already knows (client's DEFAULT_KNOWN / SCROLL_POOL).
WAR_ELEMENTS = ("flame", "frost", "light", "acid", "force", "blade", "pierce")
_STARTERS = {"war_flame_1", "war_frost_1", "war_light_1", "war_storm_1", "life_heal_1",
             "life_stam2mana", "item_might_1", "creature_swift_1", "void_nether_1", "summon_wisp"}
SCROLL_SPELLS = [s for s in (
    [f"creature_{c}_{l}" for c in ("str", "end", "coord", "quick", "focus", "will") for l in range(1, 9)]
    + [f"creature_{c}_{l}" for c in ("weak", "slow") for l in range(1, 9)]
    + [f"war_{c}_{l}" for c in WAR_ELEMENTS for l in range(1, 9)]
    + [f"war_{c}_{g}_{l}" for c in WAR_ELEMENTS for g in ("blast", "volley", "ring", "streak", "arc", "wall") for l in range(1, 9)]
    + [f"war_storm_{l}" for l in range(1, 9)]
    + [f"life_heal_{l}" for l in range(1, 9)]
    + [f"life_revit_{l}" for l in range(1, 9)]
    + [f"life_drain_{l}" for l in range(1, 9)]
    + [f"life_harm_{l}" for l in range(1, 9)]
    + [f"life_prot_{l}" for l in range(1, 9)]
    + [f"life_vuln_{l}" for l in range(1, 9)]
    + [f"void_{c}_{l}" for c in ("nether", "streak", "blast") for l in range(1, 9)]
    + [f"item_{c}_{l}" for c in ("blood", "heart", "impen", "swift") for l in range(1, 9)]
    + [f"creature_apt_{k}_{l}" for k in ("war", "life", "creature", "item", "void", "mana",
        "heavy", "light", "finesse", "twohand", "missile", "meleed", "missiled", "magicd",
        "healing", "arcane", "run", "summon") for l in range(1, 9)]
    + [f"life_renew_{p}_{l}" for p in ("hp", "st", "mn") for l in range(1, 9)]
    + [f"life_dispel_{l}" for l in range(1, 5)]
    + [f"life_vuln_{el}_{l}" for el in ("fire", "ice", "shock", "acid", "bludgeon", "slash", "pierce") for l in range(1, 9)]
    + ["item_recall_lifestone", "item_recall_sanctuary"]
) if s not in _STARTERS]

# ---- Authentic retail catalog mirror (assets/acitems.json + acspellstats.json) ----
# The SAME packs the browser loads, mirrored server-side so shared loot is real retail gear
# with exact stats (damage/variance/speed/armor/value/burden/item-mana/spells/icon) — the
# online drop is byte-identical to what the offline client would roll. If a pack is absent
# we quietly fall back to the simplified ITEM_BASE generator below.
AC_ITEMS = None
AC_POOLS = None
AC_SPELLSTATS = None
try:
    _ai = json.load(open(os.path.join(os.path.dirname(__file__), "..", "assets", "acitems.json"), encoding="utf-8"))
    _cls = {"weapon": [], "armor": [], "caster": []}
    for _n, _a in _ai.items():
        if _a.get("t") in ("MeleeWeapon", "MissileLauncher") and _a.get("dmg"):
            _cls["weapon"].append((_n, _a.get("val", 0)))
        elif _a.get("t") == "Clothing" and _a.get("al"):
            _cls["armor"].append((_n, _a.get("val", 0)))
        elif _a.get("t") == "Caster":
            _cls["caster"].append((_n, _a.get("val", 0)))
    AC_ITEMS = _ai
    AC_POOLS = {}
    for _c, _arr in _cls.items():
        _s = [x[0] for x in sorted(_arr, key=lambda x: x[1])]      # by Value, cut into 5 tier bands
        AC_POOLS[_c] = [_s[len(_s) * t // 5:len(_s) * (t + 1) // 5] for t in range(5)]
    print("acitems pack: %d retail items mirrored (%d weapons, %d armor, %d casters in loot pools)"
          % (len(_ai), len(_cls["weapon"]), len(_cls["armor"]), len(_cls["caster"])))
except Exception as _e:
    AC_ITEMS = AC_POOLS = None
try:
    AC_SPELLSTATS = json.load(open(os.path.join(os.path.dirname(__file__), "..", "assets", "acspellstats.json"), encoding="utf-8"))
    print("acspellstats pack: %d retail spells mirrored" % len(AC_SPELLSTATS))
except Exception:
    AC_SPELLSTATS = None

_AC_TITLE_SMALL = {"of", "the", "a", "an", "and", "in", "to"}
def _ac_title(n):
    return " ".join(w if (i > 0 and w in _AC_TITLE_SMALL) else (w[:1].upper() + w[1:])
                    for i, w in enumerate(n.split(" ")))
AC_WT_BY_SKILL = {1: "axe", 4: "dagger", 5: "mace", 9: "spear", 10: "staff", 11: "sword",
                  12: "atlatl", 2: "bow", 3: "crossbow", 8: "atlatl", 41: "twohand",
                  44: "sword", 45: "dagger", 46: "dagger", 47: "bow"}

def ac_itemize(it):   # stamp the exact retail specs onto an item built from a catalog name (mirrors index.html acItemize)
    if not AC_ITEMS or not it or not it.get("name"):
        return it
    a = AC_ITEMS.get(it["name"].lower())
    if not a:
        return it
    it["ac"] = 1
    if a.get("dmg") and it.get("stat") == "weapon" and it.get("wt") != "focus":
        it["v"] = a["dmg"]
        if a.get("dvar") is not None: it["dvar"] = a["dvar"]
        if a.get("spd"): it["spd"] = a["spd"]
    if a.get("al") and it.get("stat") == "worn":
        it["v"] = a["al"]
    if a.get("val"): it["acval"] = a["val"]
    if a.get("bur") is not None: it["bur"] = a["bur"]
    if a.get("mana"):
        it["itemMana"] = a["mana"]
        if a.get("mrate"): it["manaRate"] = a["mrate"]
    if a.get("spells"): it["acspells"] = a["spells"]
    if a.get("icon"): it["acicon"] = a["icon"]
    if a.get("wield") and it.get("stat") == "weapon" and it.get("wt") != "focus" and not it.get("reqVal"):
        it["reqVal"] = a["wield"]        # the retail wield difficulty (skill gate)
    return it

def roll_ac_item(rare=False, tier=1):   # a REAL retail item of the right tier band (mirrors index.html rollACItem)
    if not AC_POOLS:
        return None
    t = max(1, min(5, int(tier) or 1))
    r = random.random()
    cls = "weapon" if r < 0.55 else ("armor" if r < 0.9 else "caster")
    band = AC_POOLS[cls][t - 1]
    if not band:
        return None
    low = random.choice(band)
    a = AC_ITEMS[low]
    wr = WORK_TIER[t]
    it = {"name": _ac_title(low), "tier": t, "work": random.randint(wr[0], wr[1])}
    if cls == "weapon":
        wt = AC_WT_BY_SKILL.get(a.get("skill"), "sword")
        if re.search(r"bow\b", low) and "crossbow" not in low: wt = "bow"
        elif re.search(r"crossbow|arbalest", low): wt = "crossbow"
        elif re.search(r"two.?hand|great|zweihander|quadrelle", low): wt = "twohand"
        it.update({"stat": "weapon", "wt": wt, "mat": "Steel", "v": a.get("dmg", 5)})
    elif cls == "armor":
        aslot = ("head" if re.search(r"helm|cap\b|basinet|coif|kabuton|crown|cowl", low)
                 else "feet" if re.search(r"boots|shoes|sollerets|sandal", low)
                 else "hands" if re.search(r"gauntlet|glove|mitt", low)
                 else "legs" if re.search(r"legging|greave|pant|breeches|tasset|chiran leg", low)
                 else "offhand" if re.search(r"shield|buckler|aegis", low)
                 else "chest")
        at = ("light" if re.search(r"leather|hide|cloth|silk|robe|quilt", low)
              else "medium" if re.search(r"chain|scale|mail\b", low) else "heavy")
        it.update({"stat": "worn", "at": at, "aslot": aslot, "mat": "Steel", "v": a.get("al", 6)})
        if aslot == "offhand":
            it["shield"] = "buckler" if "buckler" in low else ("tower" if "tower" in low else "kite")
    else:
        it.update({"stat": "weapon", "wt": "focus", "mat": "Diamond", "foc": 6 + t * 5, "v": 6 + t * 5})
    return ac_itemize(it)

def roll_item(rare=False, tier=1):
    if SCROLL_SPELLS and random.random() < (0.16 if rare else 0.10):   # sometimes a spell scroll
        return {"scroll": True, "spellId": random.choice(SCROLL_SPELLS), "name": "Spell Scroll"}
    tier = max(1, min(5, int(tier) or (3 if rare else 1)))
    if rare:
        tier = max(1, min(5, tier + 2))
    ac = roll_ac_item(rare, tier)       # authentic retail item when the catalog is mirrored
    if ac:
        return ac
    # ---- fallback: simplified ITEM_BASE generator (acitems pack absent) ----
    base = random.choice(ITEM_BASE)
    v = random.randint(base["v"][0], base["v"][1])
    if rare:
        v = math.ceil(v * 2.2)
    pool = ITEM_MATERIALS[_mat_class(base["stat"])]
    r = WORK_TIER[tier]
    it = {"name": "", "stat": base["stat"], "v": v, "work": random.randint(r[0], r[1]),
          "mat": random.choice(pool), "tier": tier}
    pre = (random.choice(ITEM_PREFIX[-3:]) + " ") if rare else ""
    it["name"] = f"{pre}{it['mat']} {base['n']}"
    if "wt" in base:
        it["wt"] = base["wt"]
    if "at" in base:
        it["at"] = base["at"]
    affixable = base["stat"] in ("weapon", "worn", "armor")
    if affixable and (rare or tier >= 4) and random.random() < (0.85 if rare else 0.4):
        it["affix"] = dict(random.choice(WEAPON_AFFIXES if base["stat"] == "weapon" else WORN_AFFIXES))
        it["name"] += " " + it["affix"]["suffix"]
    return it

DROPS = {}             # id -> drop dict; shared ground loot, first-come pickup
_drop_seq = 0
DROP_TTL = 90.0        # seconds a drop lingers before it decays
PICKUP_RANGE = 6.0

def make_drop(x, z, dtype, amt=0, item=None):
    global _drop_seq
    _drop_seq += 1
    did = "d%d" % _drop_seq
    DROPS[did] = {"id": did, "x": x, "z": z, "type": dtype, "amt": amt, "item": item, "expire": time.time() + DROP_TTL}
    return DROPS[did]

def drop_pub(d):
    o = {"t": "drop", "id": d["id"], "x": round(d["x"], 2), "z": round(d["z"], 2), "type": d["type"]}
    if d["type"] == "gold":
        o["amt"] = d["amt"]
    elif d["type"] == "corpse":
        o["items"] = d.get("items", []); o["amt"] = d.get("amt", 0)
        o["owner"] = d.get("owner", ""); o["open"] = d.get("open", False)
    else:
        o["item"] = d["item"]
    return o

CORPSE_TTL = 1800.0    # a fallen player's corpse lingers far longer than mob loot (30 min default)

async def handle_death(cl, msg):
    """A player died with AC-authentic death on: stand up a shared, ownership-gated corpse that
    every nearby player can see (and only the owner can loot). Mirrors the client's local corpse."""
    global _drop_seq
    items = msg.get("items")
    items = [sanitize_item(it) for it in items if isinstance(it, dict)][:24] if isinstance(items, list) else []   # #238: bound corpse loot relayed to other players
    items = take_owned_filter(cl, items)   # M3 (#238): a corpse can only hold items the player actually OWNED (removed from cl.inv); fabricated items are dropped
    try:
        gold = max(0, int(msg.get("gold", 0)))
    except (TypeError, ValueError):
        gold = 0
    if cl.econ_ready:
        gold = min(gold, int(cl.coin)); cl.coin -= gold   # M3 (#238): corpse gold is debited from the authoritative balance (can't drop coin you don't hold)
    if not items and not gold:
        return
    # #311: cooldown + per-player corpse cap so a client can't spam DROPS entries and broadcasts.
    _nowt = time.time()
    if _nowt - getattr(cl, "last_death", 0.0) < 8.0:
        return
    cl.last_death = _nowt
    _mine = [k for k, d in DROPS.items() if d.get("type") == "corpse" and d.get("owner_user") == cl.username]
    if len(_mine) >= 6:   # evict this player's oldest corpse rather than let them accumulate unboundedly
        _old = min(_mine, key=lambda k: DROPS[k].get("expire", 0))
        DROPS.pop(_old, None)
    try:
        x = float(msg.get("x", cl.x)); z = float(msg.get("z", cl.z))
    except (TypeError, ValueError):
        x, z = cl.x, cl.z
    try:
        ttl = min(7200.0, max(300.0, float(msg.get("ttl", CORPSE_TTL))))
    except (TypeError, ValueError):
        ttl = CORPSE_TTL
    _drop_seq += 1
    did = "c%d" % _drop_seq
    DROPS[did] = {"id": did, "x": x, "z": z, "type": "corpse", "items": items, "amt": gold,
                  "owner": cl.charname or cl.username, "owner_user": cl.username,
                  "open": (getattr(cl, "pkState", "npk") == "pk"), "expire": time.time() + ttl}   # #311: free-loot flag derives from the SERVER's PK state, not client-supplied — a PK can't set open:false to dodge the PK-death loot penalty
    await broadcast(drop_pub(DROPS[did]))

async def do_recover(cl, did):
    """Owner-only corpse recovery: hand the whole bundle back to its owner and clear it for everyone."""
    d = DROPS.get(did)
    if not d or d.get("type") != "corpse":
        return
    if not d.get("open") and d.get("owner_user") != cl.username:   # open (PK) corpses are free loot for anyone in range
        return await cl.send({"t": "system", "msg": "That corpse is not yours to loot."})
    if math.hypot(cl.x - d["x"], cl.z - d["z"]) > PICKUP_RANGE + 2:
        return
    DROPS.pop(did, None)
    await broadcast({"t": "drop_gone", "id": did})
    ritems = d.get("items", []); ramt = int(d.get("amt", 0))
    auth = None
    if cl.econ_ready:   # M3 (#238): the recovered loot enters the recoverer's authoritative state (conserved from the fallen player)
        cl.inv = (cl.inv + [it for it in ritems if isinstance(it, dict)])[:500]
        cl.coin = max(0, min(2_000_000_000, cl.coin + ramt)); auth = int(cl.coin)
    await cl.send({"t": "corpse_loot", "items": ritems, "amt": ramt, "owner": d.get("owner", ""), "authCoin": auth})

async def spawn_loot(m, is_boss):
    """Roll a corpse's shared ground loot and broadcast it to everyone."""
    out = [make_drop(m["x"], m["z"], "gold", amt=random.randint(m["gold"][0], m["gold"][1]))]
    if is_boss:
        for _ in range(3):
            out.append(make_drop(m["x"] + random.uniform(-2, 2), m["z"] + random.uniform(-2, 2), "item", item=roll_item(True, 5)))
    elif random.random() < 0.22:
        out.append(make_drop(m["x"] + random.uniform(-1, 1), m["z"] + random.uniform(-1, 1), "item", item=roll_item(False, 1)))
    for d in out:
        await broadcast(drop_pub(d))

async def do_pickup(cl, did):
    d = DROPS.get(did)
    if not d:
        return
    if d["type"] == "corpse":   # #251: corpses are ownership-gated — route through do_recover, never the open pickup path (which had no owner/type check and would DESTROY another player's corpse, plus KeyError on d["item"])
        return await do_recover(cl, did)
    if math.hypot(cl.x - d["x"], cl.z - d["z"]) > PICKUP_RANGE:
        return
    DROPS.pop(did, None)
    await broadcast({"t": "drop_gone", "id": did, "by": cl.username})
    if d["type"] == "gold":
        amt = int(d["amt"])
        cl.coin = min(2_000_000_000, cl.coin + amt)   # M3 (#238): server credits the authoritative balance
        await cl.send({"t": "loot", "type": "gold", "amt": amt, "coin": int(cl.coin)})
    else:
        if cl.econ_ready and len(cl.inv) < 500:
            cl.inv.append(sanitize_item(d["item"]))   # M3 (#238): the item enters the server's authoritative inventory
        await cl.send({"t": "loot", "type": "item", "item": d["item"]})

def pk_compatible(a, b):
    """AC PvP rulesets only fight their own: PK↔PK (full) or PKL↔PKL (no item loss). NPK never fights."""
    return (a == "pk" and b == "pk") or (a == "pkl" and b == "pkl")

def nearest_player(x, z, maxd):
    best, bd = None, maxd
    for u, cl in CLIENTS.items():
        if not cl.in_world or cl.hp <= 0:
            continue
        d = math.hypot(cl.x - x, cl.z - z)
        if d < bd:
            best, bd = cl, d
    return best, bd

async def world_step():
    """One AI tick: wander/chase, melee players (sends each victim a `dmg` event), respawn dead."""
    now = time.time()
    hits = []   # (username, amount, mob_kind) damage dealt to players this tick
    for m in list(MOBS.values()):
        if m["hp"] <= 0:
            if m.get("event"):
                MOBS.pop(m["id"], None)   # Incursion wave mobs are finite — no respawn
                continue
            if now >= m["respawn_at"]:
                if m.get("boss"):
                    key = m["bosskey"]; name = m["name"]
                    MOBS.pop(m["id"], None)
                    spawn_boss(key)
                    await broadcast({"t": "system", "msg": f"A dark omen shakes Dereth — {name} has risen anew."})
                else:
                    # respawn as a fresh (possibly different) creature at the same anchor
                    MOBS.pop(m["id"], None)
                    spawn_mob(near=(m["hx"], m["hz"]))
            continue
        if m["atkcd"] > 0:
            m["atkcd"] = max(0.0, m["atkcd"] - DT)
        # Creature-Enchantment debuffs (Slowness/Weakness) lower this mob's speed/damage while active
        spdmul = m["debSpd"] if now < m.get("debSpdUntil", 0) else 1.0
        dmgmul = m["debDmg"] if now < m.get("debDmgUntil", 0) else 1.0
        pl, pd = nearest_player(m["x"], m["z"], m["sense"]) if CLIENTS else (None, 0)
        # leash: stop chasing if dragged too far from home anchor
        if pl and math.hypot(m["x"] - m["hx"], m["z"] - m["hz"]) > 600:
            pl = None
        if pl:
            m["state"] = "chase"; m["target"] = pl.username
            dx, dz = pl.x - m["x"], pl.z - m["z"]
            dist = math.hypot(dx, dz) or 1e-6
            m["yaw"] = math.atan2(dx, dz)
            reach = m["r"] + 1.6
            if dist > reach:
                step = min(m["spd"] * spdmul * DT, dist - reach)
                m["x"] += dx / dist * step; m["z"] += dz / dist * step
            elif m["atkcd"] <= 0:
                m["atkcd"] = m["atkcd_max"]
                hits.append((pl.username, round(m["dmg"] * dmgmul, 1), m["kind"], m["x"], m["z"]))
        else:
            m["state"] = "wander"; m["target"] = None
            m["wt"] -= DT
            if m["wt"] <= 0:
                m["wt"] = random.uniform(1.5, 4.0); m["yaw"] = random.uniform(0, 6.28)
            sp = m["spd"] * spdmul * 0.3
            m["x"] += math.sin(m["yaw"]) * sp * DT; m["z"] += math.cos(m["yaw"]) * sp * DT
        m["x"] = max(-WORLD_LIMIT, min(WORLD_LIMIT, m["x"]))
        m["z"] = max(-WORLD_LIMIT, min(WORLD_LIMIT, m["z"]))
        if not m.get("boss"):   # keep creatures out of the capital safe zones
            for cx, cz in CAPITALS:
                dx, dz = m["x"] - cx, m["z"] - cz
                dc = math.hypot(dx, dz)
                if 0.01 < dc < TOWN_SAFE:
                    m["x"] = cx + dx / dc * TOWN_SAFE
                    m["z"] = cz + dz / dc * TOWN_SAFE
                    break
    # deliver monster melee damage to each victim (client applies it to player.hp)
    for username, amt, kind, mx, mz in hits:
        cl = CLIENTS.get(username)
        if cl:
            await cl.send({"t": "dmg", "amt": amt, "kind": kind, "x": round(mx, 2), "z": round(mz, 2)})
    # decay expired ground loot
    for did in [d for d, v in DROPS.items() if now >= v["expire"]]:
        DROPS.pop(did, None)
        await broadcast({"t": "drop_gone", "id": did})
    # NPC vassals trickle pass-up XP to their online patrons on a slow cadence
    global _npc_v_timer
    _npc_v_timer += DT
    if _npc_v_timer >= NPC_VASSAL_TICK:
        _npc_v_timer = 0.0
        await npc_vassal_tick()
    await step_events()

async def resolve_attack(cl, mid, dmg):
    """A client claims it hit mob `mid` for `dmg`. Validate range, apply authoritatively."""
    m = MOBS.get(mid)
    if not m or m["hp"] <= 0:
        return
    if math.hypot(cl.x - m["x"], cl.z - m["z"]) > ATTACK_RANGE:
        return
    dmg = max(0.0, min(float(dmg), m["mhp"] * 1.5))  # clamp absurd claims
    m["hp"] -= dmg
    dealt = m.setdefault("dealt", {})
    dealt[cl.username] = dealt.get(cl.username, 0) + dmg
    await broadcast({"t": "mob_hit", "id": mid, "hp": round(max(0.0, m["hp"]), 1), "dmg": round(dmg, 1), "by": cl.username})
    if m["hp"] <= 0:
        m["hp"] = 0.0
        is_boss = bool(m.get("boss"))
        m["respawn_at"] = time.time() + (BOSS_DEFS[m["bosskey"]]["respawn"] if is_boss else 8.0)
        die_msg = {"t": "mob_die", "id": mid, "by": cl.username, "kind": m["kind"],
                   "x": round(m["x"], 2), "z": round(m["z"], 2)}
        if is_boss:
            die_msg["boss"] = True; die_msg["name"] = m["name"]
        await broadcast(die_msg)
        if is_boss:
            await broadcast({"t": "system", "msg": f"{cl.charname or cl.username} has slain {m['name']}! Glory echoes across Dereth."})
        # shared XP. AC fellowship: the killer's party members within range split the kill with a
        # size bonus (equal split + bonus). Solo damage-dealers outside the fellowship still earn
        # full XP (tagging generosity), so no one is punished for helping.
        fellows = []
        if cl.party in PARTIES:
            for acc in PARTIES[cl.party]["members"]:
                c = CLIENTS.get(acc)
                if c and c.in_world and math.hypot(c.x - m["x"], c.z - m["z"]) <= FELLOW_RANGE:
                    fellows.append(acc)
        per = fellowship_xp(fellows, m["xp"])
        sent = set()
        for u in fellows:
            c = CLIENTS.get(u)
            if c:
                await c.send({"t": "reward", "xp": per[u], "kind": m["kind"], "boss": is_boss})
                sent.add(u)
        for u in set(m.get("dealt", {cl.username: dmg}).keys()):
            if u in sent:
                continue
            c = CLIENTS.get(u)
            if c:
                await c.send({"t": "reward", "xp": m["xp"], "kind": m["kind"], "boss": is_boss})
        # allegiance: each rewarded character's patron receives EXTRA pass-up XP
        rewarded = set(fellows) | set(m.get("dealt", {cl.username: dmg}).keys())
        await alg_passup_kill([CLIENTS[u].charname for u in rewarded
                               if CLIENTS.get(u) and CLIENTS[u].charname], m["xp"])
        # gold + items drop on the ground as shared, first-come loot
        await spawn_loot(m, is_boss)

# ---------------------------------------------------------------- allegiance (patron/vassal pyramid)
# The server owns the online graph: swear to an equal-or-higher character, the chain runs
# up to a Monarch. Kill XP passes UP as EXTRA XP (patron loses nothing, vassal loses
# nothing): direct pass-up starts at AC's 25% floor and deepens with time sworn (~90% cap).
# Offline patrons accrue pending XP, delivered when they next enter the world.

def alg_row(name):
    with db() as c:
        r = c.execute("SELECT patron,motd,sworn_at,pending_xp FROM allegiance WHERE charname=?", (name,)).fetchone()
    return {"patron": r[0], "motd": r[1], "sworn_at": r[2] or 0, "pending_xp": r[3] or 0} if r else None

def alg_set_patron(name, patron):
    with db() as c:
        c.execute("INSERT INTO allegiance(charname,patron,sworn_at,pending_xp) VALUES(?,?,?,0) "
                  "ON CONFLICT(charname) DO UPDATE SET patron=?, sworn_at=?",
                  (name, patron, int(time.time()), patron, int(time.time())))

def alg_vassals(name):
    with db() as c:
        return [r[0] for r in c.execute("SELECT charname FROM allegiance WHERE patron=?", (name,)).fetchall()]

def alg_monarch(name, _seen=None):
    """walk up the patron chain (cycle-guarded) to the tree's crown"""
    _seen = _seen or set()
    cur = name
    while cur not in _seen:
        _seen.add(cur)
        r = alg_row(cur)
        if not r or not r["patron"]:
            return cur
        cur = r["patron"]
    return cur

def alg_reaches_up(start, target):
    """True if `target` sits anywhere on `start`'s patron chain (start itself counts). Used to
    reject a swear that would close a cycle: swearing start->target is a circle iff target can
    already reach start going up — i.e. start is an ancestor of target (call alg_reaches_up(target,
    start)). The crown-only check (alg_monarch==) missed mid-chain ancestors."""
    seen = set()
    cur = start
    while cur and cur not in seen:
        if cur == target:
            return True
        seen.add(cur)
        r = alg_row(cur)
        cur = r["patron"] if r else None
    return False

def alg_followers(name, _seen=None):
    """size of the whole subtree under a character"""
    _seen = _seen or {name}
    n = 0
    for v in alg_vassals(name):
        if v in _seen:
            continue
        _seen.add(v)
        n += 1 + alg_followers(v, _seen)
    return n

ALG_MINF = [0, 0, 2, 6, 14, 30, 62, 126, 254, 510, 1022]

def alg_rank(name):
    vs = alg_vassals(name)
    if not vs:
        return 1 if alg_row(name) else 0
    ranks = sorted((alg_rank_shallow(v) for v in vs), reverse=True)
    r = ranks[0]
    if len(ranks) >= 2:
        r = max(r, min(ranks[0], ranks[1]) + 1)
    f = alg_followers(name)
    cap = 1
    for i in range(2, 11):
        if f >= ALG_MINF[i]:
            cap = i
        else:
            break
    return min(r, cap)

def alg_rank_shallow(name):
    """vassal rank from their follower count (avoids deep recursion on big trees)"""
    f = alg_followers(name)
    r = 1
    for i in range(2, 11):
        if f >= ALG_MINF[i]:
            r = i
        else:
            break
    return r

# last-known Loyalty/Leadership per character (sent each input tick) — feeds the AC pass-up formula.
# Runtime only: re-populated when a character logs in; an unseen patron falls back to untrained (0).
SKILL_CACHE = {}

def _alg_skill(name, which):
    e = SKILL_CACHE.get(name)
    return min(291, max(0, e.get(which, 0))) if e else 0

def alg_passup_pct(vassal_name):
    """AC direct pass-up: Generated%(vassal Loyalty) × Received%(patron Leadership), deepening with
    time sworn, clamped 25%..90% — matching the client's offline formula so online/offline agree."""
    r = alg_row(vassal_name)
    if not r or not r["patron"]:
        return 0.0
    tfac = min(1.0, max(0.0, (time.time() - (r["sworn_at"] or time.time())) / 86400.0 / 730.0))
    gen = 0.50 + 0.225 * (_alg_skill(vassal_name, "loyalty") / 291.0) * (1.0 + tfac)
    rec = 0.50 + 0.225 * (_alg_skill(r["patron"], "leadership") / 291.0) * (1.0 + tfac)
    return round(min(0.90, max(0.25, gen * rec)), 4)

def alg_add_pending(name, xp):
    with db() as c:
        c.execute("INSERT INTO allegiance(charname,pending_xp) VALUES(?,?) "
                  "ON CONFLICT(charname) DO UPDATE SET pending_xp=pending_xp+?", (name, xp, xp))

def alg_take_pending(name):
    with db() as c:
        r = c.execute("SELECT pending_xp FROM allegiance WHERE charname=?", (name,)).fetchone()
        if r and (r[0] or 0) > 0:
            c.execute("UPDATE allegiance SET pending_xp=0 WHERE charname=?", (name,))
            return r[0]
    return 0

async def _deliver_passup(patron, amount, from_name):
    """route pass-up XP to a patron: live if online, else banked as pending for their next login"""
    pc = next((c for c in CLIENTS.values() if c.in_world and c.charname == patron), None)
    if pc:
        await pc.send({"t": "passup", "xp": amount, "from": from_name})
    else:
        alg_add_pending(patron, amount)

async def alg_passup_kill(recipients_names, xp):
    """extra free XP up the chain for each rewarded character (patron loses nothing, vassal loses
    nothing). Generation 1 (the direct patron) gets the time-scaled share; generation 2 (the
    grand-patron) gets AC's small grand-vassal trickle — 0–10% of the kill, scaled by how long the
    intermediate patron has stayed sworn (a loyalty proxy)."""
    for nm in set(recipients_names):
        r = alg_row(nm)
        if not r or not r["patron"]:
            continue
        share = int(xp * alg_passup_pct(nm))
        if share > 0:
            await _deliver_passup(r["patron"], share, nm)
        pr = alg_row(r["patron"])                    # is there a grand-patron above the direct patron?
        if pr and pr["patron"]:
            gpct = min(0.10, alg_passup_pct(r["patron"]) * 0.12)   # grand-vassal trickle, capped at 10%
            gshare = int(xp * gpct)
            if gshare > 0:
                await _deliver_passup(pr["patron"], gshare, nm)

# ── NPC vassals: sworn adventurers who populate an allegiance and trickle a little XP up (AC's
#    populated allegiances). Runtime-only, capped by renown; they do NOT count toward rank/followers
#    (no rank-inflation), only pad the tree and pass up a modest share of simulated deeds. ──
NPC_VASSALS = {}   # patron charname -> [ {name, level, loyalty} ]
_NPC_V_FIRST = ["Aldous", "Bryn", "Cael", "Dara", "Eryn", "Finn", "Gwen", "Hale", "Iva", "Joss", "Kira",
                "Lund", "Mira", "Nyle", "Oren", "Pell", "Quinn", "Rhea", "Sten", "Tova", "Ulf", "Vesa", "Wyn", "Yara"]
_NPC_V_LAST = ["the Bold", "the Swift", "Ironhand", "of Holtburg", "Stormborn", "the Quiet", "Redblade",
               "of Yaraq", "the Sworn", "Hollowmoor", "Brightspear", "of Shoushi"]
NPC_VASSAL_TICK = 30.0   # seconds between NPC pass-up trickles
_npc_v_timer = 0.0

def npc_vassal_cap(level):
    return max(0, min(6, level // 12))   # a few sworn adventurers, growing with your renown (L12+)

def npc_passup_pct(npc, patron):
    """same shape as the player formula — the NPC's Loyalty × the patron's Leadership, mid time factor"""
    tfac = 0.5
    gen = 0.50 + 0.225 * (min(291, npc.get("loyalty", 0)) / 291.0) * (1.0 + tfac)
    rec = 0.50 + 0.225 * (_alg_skill(patron, "leadership") / 291.0) * (1.0 + tfac)
    return min(0.90, max(0.25, gen * rec))

async def handle_muster(cl):
    """call a sworn adventurer to your banner (an NPC vassal), capped by your renown"""
    if not cl.in_world or not cl.charname:
        return
    cap = npc_vassal_cap(cl.level)
    if cap <= 0:
        return await cl.send({"t": "system", "msg": "You lack the renown to call sworn adventurers yet (reach level 12)."})
    lst = NPC_VASSALS.setdefault(cl.charname, [])
    if len(lst) >= cap:
        return await cl.send({"t": "system", "msg": f"Your banner is full — {len(lst)}/{cap} sworn adventurers already follow you."})
    name = random.choice(_NPC_V_FIRST) + " " + random.choice(_NPC_V_LAST)
    lvl = max(1, cl.level - random.randint(2, 10))
    lst.append({"name": name, "level": lvl, "loyalty": random.randint(80, 200)})
    await cl.send({"t": "system", "msg": f"{name} (level {lvl}) swears to your banner — they adventure in your name, trickling XP up to you ({len(lst)}/{cap})."})
    await cl.send(alg_info_pub(cl.charname))

async def npc_vassal_tick():
    """each NPC vassal 'adventures' and trickles a modest pass-up to its online patron"""
    for patron, lst in list(NPC_VASSALS.items()):
        if not lst:
            continue
        pc = next((c for c in CLIENTS.values() if c.in_world and c.charname == patron), None)
        if not pc:
            continue
        total = sum(int(npc["level"] * 4 * npc_passup_pct(npc, patron)) for npc in lst)   # low, steady simulated take
        if total > 0:
            await pc.send({"t": "passup", "xp": total, "from": "your sworn adventurers"})

def alg_info_pub(name):
    r = alg_row(name) or {}
    mon = alg_monarch(name)
    vs = alg_vassals(name)
    online = {c.charname for c in CLIENTS.values() if c.in_world}
    mrow = alg_row(mon)
    vlist = [{"name": v, "online": v in online, "passup": round(alg_passup_pct(v) * 100)} for v in vs]
    for npc in NPC_VASSALS.get(name, []):   # sworn NPC adventurers ride along in the tree
        vlist.append({"name": npc["name"], "online": True, "passup": round(npc_passup_pct(npc, name) * 100), "npc": True})
    return {"t": "alg", "patron": r.get("patron"), "monarch": mon if (r.get("patron") or vs or NPC_VASSALS.get(name)) else None,
            "vassals": vlist, "rank": alg_rank(name), "followers": alg_followers(name),
            "motd": (mrow or {}).get("motd")}

# ---------------------------------------------------------------- parties (fellowships)
EMOTES = {"wave": "waves.", "cheer": "cheers!", "dance": "breaks into a dance.", "bow": "bows solemnly.",
          "laugh": "laughs.", "point": "points.", "salute": "salutes.", "flex": "flexes.",
          "kneel": "kneels.", "clap": "applauds."}
PARTIES = {}           # pid -> {"leader": account, "members": [accounts]}
_party_seq = 0
PARTY_MAX = 9          # AC fellowship cap (founder + 8)
# AC fellowship XP: an equal split with a size bonus, so the shared pool grows (~1.5x the kill's
# XP at 2 members up to ~3x at a full 9) — per-member fraction of the kill XP by fellowship size.
FELLOW_SHARE = {1: 1.00, 2: 0.75, 3: 0.60, 4: 0.50, 5: 0.45, 6: 0.40, 7: 0.37, 8: 0.34, 9: 0.33}

def fellowship_xp(fellows, base_xp):
    """AC fellowship XP by LEVEL SPREAD, not just size → {account: xp}. A tight band shares equally
    with the size bonus; any wider band splits the SAME pool proportionally by level so a low-level
    can't leech a high-level kill. (#249: the old `spread>=50` carve-out returned FULL base_xp to
    every member — it inverted the anti-leech split, handed a low-level the entire kill, minted n×
    inflationary XP, and made the low-level's share JUMP UP as the partner's level rose past the
    threshold. Removed so the proportional split governs every spread>5 and the low-level's share
    only ever decreases as the gap widens.)"""
    n = len(fellows)
    if n < 2:
        return {u: base_xp for u in fellows}                       # solo / single member: full XP
    levels = {u: max(1, getattr(CLIENTS.get(u), "level", 1) or 1) for u in fellows}
    spread = max(levels.values()) - min(levels.values())
    base = FELLOW_SHARE[min(n, 9)]
    if spread <= 5:
        return {u: int(round(base_xp * base)) for u in fellows}    # tight band: equal split + size bonus
    pool = base_xp * base * n                                      # same total pool, weighted by level
    tot = sum(levels.values()) or 1
    return {u: int(round(pool * levels[u] / tot)) for u in fellows}

def party_names(pid):
    p = PARTIES.get(pid)
    if not p:
        return []
    out = []
    for acc in p["members"]:
        c = CLIENTS.get(acc)
        nm = (c.charname if c and c.charname else acc)
        out.append(nm + (" (leader)" if acc == p["leader"] else ""))
    return out

async def party_notify(pid, msg):
    for acc in list(PARTIES.get(pid, {}).get("members", [])):
        c = CLIENTS.get(acc)
        if c:
            await c.send({"t": "system", "msg": msg})

async def party_sync(pid):
    """Push the structured member roster to every member (for map highlight / HUD)."""
    p = PARTIES.get(pid)
    if not p:
        return
    names = [CLIENTS[a].charname for a in p["members"] if a in CLIENTS and CLIENTS[a].charname]
    leader = CLIENTS[p["leader"]].charname if p["leader"] in CLIENTS else None
    for acc in list(p["members"]):
        c = CLIENTS.get(acc)
        if c:
            await c.send({"t": "pmembers", "names": names, "leader": leader})

async def party_leave(cl, quiet=False):
    pid = cl.party
    cl.party = None
    p = PARTIES.get(pid)
    if not p:
        if not quiet:
            await cl.send({"t": "system", "msg": "You are not in a party."})
        return
    who = cl.charname or cl.username
    if cl.username in p["members"]:
        p["members"].remove(cl.username)
    if p["members"]:                            # tell whoever remains that this member left
        await party_notify(pid, f"{who} has left the party.")
    if not quiet:
        await cl.send({"t": "pmembers", "names": [], "leader": None})   # clear the leaver's roster
    if len(p["members"]) <= 1:                 # a party of one disbands
        for acc in p["members"]:
            c = CLIENTS.get(acc)
            if c:
                c.party = None
                await c.send({"t": "system", "msg": "The party has disbanded."})
                await c.send({"t": "pmembers", "names": [], "leader": None})
        PARTIES.pop(pid, None)
        if not quiet:
            await cl.send({"t": "system", "msg": "You left the party."})
    else:
        if p["leader"] == cl.username:
            p["leader"] = p["members"][0]
        if not quiet:
            await cl.send({"t": "system", "msg": "You left the party."})
        await party_notify(pid, f"{who} has left the party.")
        await party_sync(pid)

# ---------------------------------------------------------------- secure trade (AC)
# Two players open a trade window; each offers items; BOTH must accept; any change
# to either offer clears both accepts (retail rule). Items are inventory blobs —
# the server brokers the swap, each client applies it to its own satchel.
TRADES = {}     # tid -> {"a":acc,"b":acc,"offers":{acc:[items]},"ok":{acc:False},"pending":bool}
_trade_seq = 0

def trade_of(acc):
    for tid, tr in list(TRADES.items()):
        if acc in (tr["a"], tr["b"]):
            return tid, tr
    return None, None

async def trade_sync(tr):
    for acc in (tr["a"], tr["b"]):
        c = CLIENTS.get(acc)
        other = tr["b"] if acc == tr["a"] else tr["a"]
        oc = CLIENTS.get(other)
        if c:
            coin = tr.get("coin", {})
            await c.send({"t": "trade", "act": "sync", "you": tr["offers"][acc], "them": tr["offers"][other],
                          "youOk": tr["ok"][acc], "themOk": tr["ok"][other],
                          "youCoin": coin.get(acc, 0), "themCoin": coin.get(other, 0),
                          "with": oc.charname if oc else "?"})

async def trade_cancel(acc, reason="The trade was cancelled."):
    tid, tr = trade_of(acc)
    if not tr:
        return
    del TRADES[tid]
    for a in (tr["a"], tr["b"]):
        c = CLIENTS.get(a)
        if c:
            await c.send({"t": "trade", "act": "cancel", "msg": reason})

async def handle_trade(cl, msg):
    global _trade_seq
    act = msg.get("act")
    if act == "open":
        name = str(msg.get("name", ""))[:24]
        target = next((c for c in CLIENTS.values() if c.in_world and c.charname == name), None)
        if not target or target is cl:
            return await cl.send({"t": "system", "msg": f"No player named {name} is here."})
        if math.hypot(target.x - cl.x, target.z - cl.z) > 15:
            return await cl.send({"t": "system", "msg": f"{name} is too far away to trade."})
        t1, _ = trade_of(cl.username); t2, _ = trade_of(target.username)
        if t1 or t2:
            return await cl.send({"t": "system", "msg": "One of you is already trading."})
        _trade_seq += 1
        TRADES["t%d" % _trade_seq] = {"a": cl.username, "b": target.username,
                                      "offers": {cl.username: [], target.username: []},
                                      "coin": {cl.username: 0, target.username: 0},
                                      "ok": {cl.username: False, target.username: False}, "pending": True}
        await cl.send({"t": "system", "msg": f"You offer to trade with {name}."})
        await target.send({"t": "trade", "act": "invite", "from": cl.charname})
    elif act == "accept_open":
        tid, tr = trade_of(cl.username)
        if not tr or not tr.get("pending"):
            return await cl.send({"t": "system", "msg": "No trade offer waiting."})
        tr["pending"] = False
        await trade_sync(tr)
    elif act in ("add", "remove"):
        tid, tr = trade_of(cl.username)
        if not tr or tr.get("pending"):
            return
        if act == "add":
            item = msg.get("item")
            if isinstance(item, dict) and len(tr["offers"][cl.username]) < 12:
                tr["offers"][cl.username].append(sanitize_item(item))   # #238: bound stats of an item offered to another player
        else:
            idx = int(msg.get("idx", -1))
            if 0 <= idx < len(tr["offers"][cl.username]):
                tr["offers"][cl.username].pop(idx)
        tr["ok"][tr["a"]] = tr["ok"][tr["b"]] = False        # AC: ANY change clears both accepts
        await trade_sync(tr)
    elif act == "coin":
        tid, tr = trade_of(cl.username)
        if not tr or tr.get("pending"):
            return
        try:
            amt = max(0, min(2_000_000_000, int(msg.get("amount", 0))))
        except (TypeError, ValueError):
            amt = 0
        if cl.econ_ready:
            amt = min(amt, int(cl.coin))   # M3 (#238): can't offer more pyreals than you actually hold
        tr.setdefault("coin", {})[cl.username] = amt
        tr["ok"][tr["a"]] = tr["ok"][tr["b"]] = False        # AC: changing the coin offer clears both accepts
        await trade_sync(tr)
    elif act == "ok":
        tid, tr = trade_of(cl.username)
        if not tr or tr.get("pending"):
            return
        tr["ok"][cl.username] = True
        if tr["ok"][tr["a"]] and tr["ok"][tr["b"]]:
            ca = CLIENTS.get(tr["a"]); cb = CLIENTS.get(tr["b"])
            # M3 (#238): validate offered ITEMS against the authoritative inventories and remove them
            # (escrow). If either side offered something it doesn't own (fabricated), roll back and
            # abort — no item can be minted into the other player's satchel.
            okA, remA = take_owned(ca, tr["offers"].get(tr["a"], []))
            okB, remB = take_owned(cb, tr["offers"].get(tr["b"], []))
            if not (okA and okB):
                if okA and ca and ca.econ_ready: ca.inv = ca.inv + remA
                if okB and cb and cb.econ_ready: cb.inv = cb.inv + remB
                return await trade_cancel(cl.username, "Trade aborted — an offered item could not be verified against your inventory.")
            # move pyreals through the authoritative balances. Re-clamp each offer to the offerer's
            # CURRENT coin (it may have changed since the offer) so nobody goes negative.
            coin_a = int(tr.get("coin", {}).get(tr["a"], 0))
            coin_b = int(tr.get("coin", {}).get(tr["b"], 0))
            if ca and ca.econ_ready: coin_a = max(0, min(coin_a, int(ca.coin)))
            if cb and cb.econ_ready: coin_b = max(0, min(coin_b, int(cb.coin)))
            if ca and ca.econ_ready: ca.coin = max(0, min(2_000_000_000, ca.coin - coin_a + coin_b))
            if cb and cb.econ_ready: cb.coin = max(0, min(2_000_000_000, cb.coin - coin_b + coin_a))
            # deposit received items into each authoritative inventory (A gets B's, B gets A's)
            if ca and ca.econ_ready: ca.inv = (ca.inv + remB)[:500]
            if cb and cb.econ_ready: cb.inv = (cb.inv + remA)[:500]
            # #313: retire the trade BEFORE the (awaiting) send loop. Everything above is synchronous,
            # so removing it here means a replayed {"act":"ok"} arriving while a send's drain() yields
            # under backpressure finds no live trade (trade_of → None) and returns — no double-escrow/
            # double-deposit. Build the payloads first, then delete, then send.
            done_msgs = []
            for acc in (tr["a"], tr["b"]):
                other = tr["b"] if acc == tr["a"] else tr["a"]
                c = CLIENTS.get(acc)
                if c:
                    done_msgs.append((c, {"t": "trade", "act": "done", "give": tr["offers"][other],
                                          "gave": tr["offers"][acc],   # #296: the offerer's OWN authoritative offer, so the client can reconcile against dropped add/remove messages instead of trusting its local list
                                          "coin": (coin_b if acc == tr["a"] else coin_a),
                                          "authCoin": (int(c.coin) if c.econ_ready else None)}))   # M3: authoritative balance to adopt
            del TRADES[tid]
            for c, msg in done_msgs:
                await c.send(msg)
        else:
            await trade_sync(tr)
    elif act == "cancel":
        await trade_cancel(cl.username)

async def handle_party(cl, msg):
    global _party_seq
    act = msg.get("act")
    if act == "invite":
        name = str(msg.get("name", ""))
        target = next((c for c in CLIENTS.values() if c.in_world and c.charname == name), None)
        if not target or target is cl:
            return await cl.send({"t": "system", "msg": f"No online character named '{name}'."})
        if cl.party and target.party == cl.party:
            return await cl.send({"t": "system", "msg": f"{name} is already in your party."})
        if target.party:
            return await cl.send({"t": "system", "msg": f"{name} is already in a party."})
        target.invite_from = cl.username
        await cl.send({"t": "system", "msg": f"You invite {name} to your party."})
        await target.send({"t": "system", "msg": f"{cl.charname} invites you to a party — type /party accept."})
    elif act == "accept":
        inviter = CLIENTS.get(cl.invite_from) if cl.invite_from else None
        cl.invite_from = None
        if not inviter or not inviter.in_world:
            return await cl.send({"t": "system", "msg": "You have no pending party invite."})
        if cl.party:
            return await cl.send({"t": "system", "msg": "Leave your current party first (/party leave)."})
        pid = inviter.party
        if not pid or pid not in PARTIES:
            _party_seq += 1; pid = "g%d" % _party_seq
            PARTIES[pid] = {"leader": inviter.username, "members": [inviter.username]}
            inviter.party = pid
        if len(PARTIES[pid]["members"]) >= PARTY_MAX:
            return await cl.send({"t": "system", "msg": "That party is full."})
        PARTIES[pid]["members"].append(cl.username); cl.party = pid
        await party_notify(pid, f"{cl.charname} has joined the party. Members: {', '.join(party_names(pid))}")
        await party_sync(pid)
    elif act == "leave":
        await party_leave(cl)
    else:  # list
        if cl.party and cl.party in PARTIES:
            await cl.send({"t": "system", "msg": f"Party ({len(PARTIES[cl.party]['members'])}): {', '.join(party_names(cl.party))}"})
        else:
            await cl.send({"t": "system", "msg": "You are not in a party. /party invite <name> to form one."})

# #238 (incremental): safe parse+clamp for client-reported presence values. These reject only
# IMPOSSIBLE inputs (NaN/Inf positions, hp/level far outside any legit range) — they don't make the
# server authoritative, but they stop a client from broadcasting a NaN position (which corrupts other
# clients' rendering) or a forged level>275 (which gates fealty/vassal-cap) or god-HP display values.
def _finitef(v, default):
    try:
        f = float(v)
        return f if math.isfinite(f) else default
    except (TypeError, ValueError):
        return default

def _clampi(v, lo, hi, default):
    try:
        return max(lo, min(hi, int(v)))
    except (TypeError, ValueError):
        return default

# ---------------------------------------------------------------- auth + dispatch
def valid_name(u):
    return isinstance(u, str) and 3 <= len(u) <= 16 and all(ch.isalnum() or ch in "_-" for ch in u)

def clean_relay(s, maxlen):
    """#240: server-side defense-in-depth for free text relayed to other clients (chat/emote/tell/
    party/MOTD). Coerce to str, drop control chars, neutralize markup by removing angle brackets,
    then cap + trim. The shipped client escapes on render, so stripping <> here is invisible to it
    but protects any other/older/future client that forgets to escape."""
    s = str(s).replace("<", "").replace(">", "")
    s = "".join(ch for ch in s if ch >= " ")   # drop control chars incl. newlines/tabs (single-line relay)
    return s[:maxlen].strip()

async def do_auth_success(cl, username):
    # one session per account: kick a prior connection
    old = CLIENTS.get(username)
    if old and old is not cl:
        old.alive = False
        try:
            old.writer.close()
        except Exception:
            pass
    cl.username = username
    cl.token = secrets.token_hex(24)
    cl.in_world = False; cl.charname = None; cl.slot = None
    TOKENS[cl.token] = username
    CLIENTS[username] = cl
    # Auth lands the player at the character-select screen, not the world.
    await cl.send({"t": "auth_ok", "id": username, "token": cl.token, "pv": PROTOCOL_VERSION})
    await cl.send({"t": "roster", "chars": roster(username), "max": MAX_CHARS})

async def enter_world(cl, slot, name, data):
    """Bring a selected/created character into the shared world."""
    cl.slot = slot; cl.charname = name; cl.in_world = True
    load_econ(cl, data)   # M3 (#238): adopt authoritative coin + inventory from the save
    await cl.send({"t": "play_ok", "slot": slot, "name": name, "char": data})
    # allegiance: the graph decides the /a channel (everyone under one Monarch); deliver
    # any pass-up XP your vassals earned while you were away
    r = alg_row(name)
    if r and (r["patron"] or alg_vassals(name)):
        cl.allegiance = alg_monarch(name)
    pending = alg_take_pending(name)
    if pending > 0:
        await cl.send({"t": "passup", "xp": pending, "from": "your vassals (while away)"})
    # sync current ground loot + any active Incursion to the entering player
    for d in list(DROPS.values()):
        await cl.send(drop_pub(d))
    if EVENT.get("active"):
        await cl.send(event_pub())
    await broadcast({"t": "system", "msg": f"{name} has entered Dereth."}, exclude=cl)

async def dispatch(cl, msg):
    t = msg.get("t")
    if t == "register":
        u, p = msg.get("user", ""), msg.get("pass", "")
        if not valid_name(u):
            return await cl.send({"t": "auth_err", "msg": "Name: 3-16 letters/numbers/_-."})
        if not isinstance(p, str) or len(p) < 4:
            return await cl.send({"t": "auth_err", "msg": "Password must be at least 4 characters."})
        if not create_user(u, p):
            return await cl.send({"t": "auth_err", "msg": "That name is already taken."})
        return await do_auth_success(cl, u)
    if t == "login":
        u, p = msg.get("user", ""), msg.get("pass", "")
        if not verify_user(u, p):
            return await cl.send({"t": "auth_err", "msg": "Wrong name or password."})
        return await do_auth_success(cl, u)
    if t == "resume":
        u = TOKENS.get(msg.get("token", ""))
        if not u:
            return await cl.send({"t": "auth_err", "msg": "Session expired — please log in."})
        return await do_auth_success(cl, u)
    # everything below requires auth
    if not cl.username:
        return
    if t == "play_char":
        slot = msg.get("slot")
        if not isinstance(slot, int):
            return
        ch = load_char_slot(cl.username, slot)
        if not ch:
            return await cl.send({"t": "play_err", "msg": "No character in that slot."})
        return await enter_world(cl, slot, ch["name"], ch["data"])
    if t == "create_char":
        slot, name, char = msg.get("slot"), msg.get("name", ""), msg.get("char")
        if not valid_name(name):
            return await cl.send({"t": "play_err", "msg": "Name: 3-16 letters/numbers/_-."})
        ok, err = create_char_slot(cl.username, slot, name, char if isinstance(char, dict) else None)
        if not ok:
            return await cl.send({"t": "play_err", "msg": err})
        await cl.send({"t": "roster", "chars": roster(cl.username), "max": MAX_CHARS})
        return await enter_world(cl, slot, name, char if isinstance(char, dict) else None)
    if t == "delete_char":
        slot = msg.get("slot")
        if isinstance(slot, int) and not (cl.in_world and cl.slot == slot):
            delete_char_slot(cl.username, slot)
            await cl.send({"t": "roster", "chars": roster(cl.username), "max": MAX_CHARS})
        elif isinstance(slot, int):
            # Refuse deleting the slot you're currently playing — but SAY SO, so the client can tell a
            # refusal from packet loss (create_char auto-plays the new slot, so create->delete hits this).
            await cl.send({"t": "play_err", "msg": "You can't delete the character you're currently playing — leave the world first."})
        return
    if t == "input":
        cl.x = _finitef(msg.get("x"), cl.x); cl.z = _finitef(msg.get("z"), cl.z)   # #238: reject NaN/Inf so a bad coord can't be broadcast to (and corrupt) other clients
        cl.yaw = _finitef(msg.get("yaw"), cl.yaw)
        cl.hp = _clampi(msg.get("hp"), 0, 1_000_000, cl.hp)      # sane ceilings — legit values are far below; blocks god-HP display
        cl.mhp = _clampi(msg.get("mhp"), 1, 1_000_000, cl.mhp)
        cl.level = _clampi(msg.get("level"), 1, 275, cl.level)   # AC hard cap — blocks forged level>275 that would gate unlimited vassals/fealty
        cl.heritage = str(msg.get("heritage", cl.heritage))[:16]
        cl.title = str(msg.get("title", cl.title))[:40]
        cl.wt = (str(msg.get("wt"))[:16] if msg.get("wt") else None)          # so remotes render the right weapon
        cl.wmode = str(msg.get("wmode", cl.wmode))[:8]
        cl.shield = (str(msg.get("shield"))[:16] if msg.get("shield") else None)
        ps = str(msg.get("pkState", "pk" if msg.get("pk") else getattr(cl, "pkState", "npk")))   # AC 3-state PvP
        cl.pkState = ps if ps in ("npk", "pkl", "pk") else "npk"
        cl.pk = cl.pkState != "npk"
        if cl.charname and ("loyalty" in msg or "leadership" in msg):   # feed the allegiance pass-up formula
            try:
                SKILL_CACHE[cl.charname] = {"loyalty": int(msg.get("loyalty", 0)), "leadership": int(msg.get("leadership", 0))}
            except (TypeError, ValueError):
                pass
    elif t == "chat":
        text = clean_relay(msg.get("msg", ""), 240)
        if text and cl.in_world:
            await broadcast({"t": "chat", "from": cl.charname or cl.username, "msg": text, "ts": int(time.time())})
    elif t == "attack":
        mid = msg.get("id")
        if isinstance(mid, str) and cl.in_world:
            await resolve_attack(cl, mid, msg.get("dmg", 0))
    elif t == "pickup":
        did = msg.get("id")
        if isinstance(did, str) and cl.in_world:
            await do_pickup(cl, did)
    elif t == "death":
        if cl.in_world:
            await handle_death(cl, msg)
    elif t == "muster":
        if cl.in_world:
            await handle_muster(cl)
    elif t == "recover":
        did = msg.get("id")
        if isinstance(did, str) and cl.in_world:
            await do_recover(cl, did)
    elif t == "vendor_sell":
        # M3 (#238): the server, not the client, credits the pyreals. The item must be OWNED (removed
        # from cl.inv) and the credit is capped at the retail catalog value — no phantom-sell minting.
        if cl.in_world and cl.econ_ready:
            item = msg.get("item")
            try:
                price = max(0, int(msg.get("price", 0)))
            except (TypeError, ValueError):
                price = 0
            if isinstance(item, dict):
                ok, removed = take_owned(cl, [item])
                if ok and removed:
                    # #288: cap the credit at the retail catalog value AND — for anything the player
                    # BOUGHT from a vendor — below its recorded buy price, so buy->sell can never net a
                    # profit (closes the vendor_buy@0 -> vendor_sell coin-mint loop). Use the SERVER's
                    # own inventory copy (removed[0]), never the client-supplied bp, which can be forged.
                    server_item = removed[0]
                    price = min(price, catalog_val(server_item))
                    bp = server_item.get("bp")
                    if isinstance(bp, (int, float)):
                        price = min(price, max(0, int(bp) - 1))
                    cl.coin = max(0, min(2_000_000_000, cl.coin + price))
                    await cl.send({"t": "vendor_ok", "act": "sell", "coin": int(cl.coin)})
                else:
                    await cl.send({"t": "vendor_ok", "act": "reject", "coin": int(cl.coin), "reason": "That item isn't in your inventory."})
    elif t == "vendor_buy":
        # M3 (#238): debit pyreals authoritatively; can't buy without the coin. (Item validation vs.
        # shop stock is Stage 3; here we take the client's item but bound its stats.)
        if cl.in_world and cl.econ_ready:
            try:
                cost = max(0, int(msg.get("cost", 0)))
            except (TypeError, ValueError):
                cost = 0
            item = msg.get("item")
            if cost <= cl.coin:
                cl.coin -= cost
                if isinstance(item, dict) and len(cl.inv) < 500:
                    bought = sanitize_item(item)
                    bought["bp"] = cost   # #288: stamp the authoritative buy price so vendor_sell can clamp resale below it — a bought item can never be sold back for a profit (kills the buy@0 coin-mint loop)
                    cl.inv.append(bought)
                await cl.send({"t": "vendor_ok", "act": "buy", "coin": int(cl.coin)})
            else:
                await cl.send({"t": "vendor_ok", "act": "reject", "coin": int(cl.coin), "reason": "Not enough pyreals."})
    elif t == "debuff":
        if cl.in_world:
            m = MOBS.get(msg.get("id")); eff = msg.get("eff")
            try:
                v = float(msg.get("v", 1)); dur = float(msg.get("dur", 10))
            except Exception:
                v, dur = 1.0, 0.0
            if m and m["hp"] > 0 and eff in ("dmg", "spd") and 0.1 <= v <= 1 and math.hypot(cl.x - m["x"], cl.z - m["z"]) <= ATTACK_RANGE + 6:
                until = time.time() + min(dur, 60)
                if eff == "dmg":
                    m["debDmg"] = v; m["debDmgUntil"] = until
                else:
                    m["debSpd"] = v; m["debSpdUntil"] = until
                await broadcast({"t": "mob_deb", "id": m["id"], "eff": eff})
    elif t == "save":
        char = msg.get("char")
        if isinstance(char, dict) and cl.slot is not None:
            save_char_slot(cl.username, cl.slot, char)
            load_econ(cl, char)   # M3 (#238): resync authoritative coin+inv from the save while the intent stages are still being built (client remains the source of truth until cutover)
    elif t == "who":
        players = [{"name": c.charname or u, "level": c.level} for u, c in CLIENTS.items() if c.in_world]
        await cl.send({"t": "who", "players": players})
    elif t == "emote":
        if cl.in_world:
            act = str(msg.get("act", ""))
            if act == "me":
                text = clean_relay(msg.get("text", ""), 80)
                line = f"{cl.charname} {text}" if text else None
            else:
                verb = EMOTES.get(act)
                line = f"{cl.charname} {verb}" if verb else None
            if line:
                await broadcast({"t": "emote", "id": cl.username, "from": cl.charname, "act": act, "msg": line})
    elif t == "cast":
        # relay a Creature/Life heal or buff to another in-world player near the caster
        if cl.in_world:
            tgt = CLIENTS.get(msg.get("target"))
            spell = msg.get("spell")
            if tgt and tgt.in_world and isinstance(spell, str) and math.hypot(cl.x - tgt.x, cl.z - tgt.z) <= 45:
                await tgt.send({"t": "rbuff", "spell": spell, "from": cl.charname})
    elif t == "pvp":
        # S3 PvP: relay a hit only if both players share a PvP ruleset (PK↔PK or PKL↔PKL) and are in range
        if cl.in_world and getattr(cl, "pk", False):
            tgt = CLIENTS.get(msg.get("target"))
            try: dmg = float(msg.get("dmg", 0))
            except Exception: dmg = 0
            if (tgt and tgt.in_world and pk_compatible(getattr(cl, "pkState", "npk"), getattr(tgt, "pkState", "npk"))
                    and 0 < dmg <= 2000 and math.hypot(cl.x - tgt.x, cl.z - tgt.z) <= 40):
                await tgt.send({"t": "pvp", "from": cl.charname, "dmg": round(dmg, 1), "element": str(msg.get("element", ""))[:12]})
    elif t == "spellfx":
        # relay a cosmetic spell visual to other in-world players (no damage authority here)
        if cl.in_world and str(msg.get("cat", "")) in ("proj", "ring", "aoe", "wall"):
            try:
                fx = {"t": "spellfx", "cat": str(msg["cat"]),
                      "x": float(msg.get("x", cl.x)), "z": float(msg.get("z", cl.z)),
                      "dx": float(msg.get("dx", 0)), "dz": float(msg.get("dz", 0)),
                      "c": int(msg.get("c", 0)) & 0xFFFFFF,
                      "r": max(0.1, min(8.0, float(msg.get("r", 0.3)))),
                      "sp": max(0, min(160, int(msg.get("sp", 0))))}
            except (TypeError, ValueError):
                return
            await broadcast(fx, exclude=cl)
    elif t == "tell":
        name = str(msg.get("name", ""))
        text = clean_relay(msg.get("msg", ""), 240)
        if cl.in_world and text:
            target = next((c for c in CLIENTS.values() if c.in_world and c.charname == name), None)
            if not target or target is cl:
                await cl.send({"t": "system", "msg": f"No online character named '{name}'."})
            else:
                await target.send({"t": "chat", "from": cl.charname, "msg": text, "channel": "tell", "ts": int(time.time())})
                await cl.send({"t": "chat", "from": name, "msg": text, "channel": "tell_out", "ts": int(time.time())})
    elif t == "party":
        if cl.in_world:
            await handle_party(cl, msg)
    elif t == "trade":
        if cl.in_world:
            await handle_trade(cl, msg)
    elif t == "pchat":
        text = clean_relay(msg.get("msg", ""), 240)
        if cl.in_world and cl.party in PARTIES and text:
            for acc in PARTIES[cl.party]["members"]:
                c = CLIENTS.get(acc)
                if c:
                    await c.send({"t": "chat", "from": cl.charname, "msg": text, "channel": "party", "ts": int(time.time())})
        elif cl.in_world:
            await cl.send({"t": "system", "msg": "You are not in a party (/party invite <name>)."})
    elif t == "allegiance":
        # set/clear this character's allegiance name (the client persists it in its save;
        # the server only needs it live to route /a chat). The sworn GRAPH channel wins.
        name = str(msg.get("name", ""))[:40].strip()
        r = alg_row(cl.charname or "")
        if not (r and (r["patron"] or alg_vassals(cl.charname))):
            cl.allegiance = name or None
    elif t == "swear":
        # swear fealty to an online equal-or-higher character (AC rule)
        name = str(msg.get("name", ""))[:32].strip()
        if not cl.in_world or not cl.charname:
            return
        target = next((c for c in CLIENTS.values() if c.in_world and c.charname == name), None)
        if not target or target is cl:
            return await cl.send({"t": "system", "msg": f"No online character named '{name}' to swear to."})
        if target.level < cl.level:
            return await cl.send({"t": "system", "msg": f"{name} (level {target.level}) is beneath your level {cl.level} — AC lets you swear only to an equal or higher."})
        if alg_reaches_up(name, cl.charname):   # #250: full upward-walk — reject if you're ANYWHERE on the target's chain (crown OR mid-chain), else a patron could swear to its own descendant and close a loop
            return await cl.send({"t": "system", "msg": "They stand beneath you in your own tree — that fealty would be a circle."})
        if len(alg_vassals(name)) >= max(1, target.level):
            return await cl.send({"t": "system", "msg": f"{name}'s patronage is full (AC caps vassals at character level)."})
        alg_set_patron(cl.charname, name)
        mon = alg_monarch(cl.charname)
        cl.allegiance = mon; target.allegiance = mon
        await cl.send({"t": "system", "msg": f"You swear fealty to {name}. Your allegiance stands under Monarch {mon}; your kill XP passes up as extra XP for your patron (you lose nothing)."})
        await target.send({"t": "system", "msg": f"{cl.charname} has sworn fealty to you — their deeds now pass XP up to you."})
        await cl.send(alg_info_pub(cl.charname))
        await target.send(alg_info_pub(name))
    elif t == "unswear":
        if cl.in_world and cl.charname:
            r = alg_row(cl.charname)
            if r and r["patron"]:
                old = r["patron"]
                alg_set_patron(cl.charname, None)
                await cl.send({"t": "system", "msg": f"You break fealty with {old}. Accrued sworn-time resets."})
                pc = next((c for c in CLIENTS.values() if c.in_world and c.charname == old), None)
                if pc:
                    await pc.send({"t": "system", "msg": f"{cl.charname} has broken fealty with you."})
            else:
                await cl.send({"t": "system", "msg": "You are sworn to no patron."})
    elif t == "alg_info":
        if cl.in_world and cl.charname:
            await cl.send(alg_info_pub(cl.charname))
    elif t == "alg_motd":
        text = clean_relay(msg.get("text", ""), 200)
        if cl.in_world and cl.charname:
            if alg_monarch(cl.charname) != cl.charname or not alg_vassals(cl.charname):
                return await cl.send({"t": "system", "msg": "Only a Monarch (the crown of a tree) sets the allegiance MOTD."})
            with db() as c:
                c.execute("INSERT INTO allegiance(charname,motd) VALUES(?,?) "
                          "ON CONFLICT(charname) DO UPDATE SET motd=?", (cl.charname, text, text))
            for c2 in CLIENTS.values():
                if c2.in_world and getattr(c2, "allegiance", None) == cl.charname:
                    await c2.send({"t": "system", "msg": f"[Allegiance MOTD] {text}"})
    elif t == "achat":
        text = clean_relay(msg.get("msg", ""), 240)
        alg = getattr(cl, "allegiance", None)
        if cl.in_world and text and alg:
            for c in CLIENTS.values():
                if c.in_world and getattr(c, "allegiance", None) == alg:
                    await c.send({"t": "chat", "from": cl.charname, "msg": text, "channel": "allegiance", "ts": int(time.time())})
        elif cl.in_world:
            await cl.send({"t": "system", "msg": "You are sworn to no allegiance (/allegiance join <name>)."})
    elif t == "houseboot":
        # /house boot <name>: notify the booted guest if they're online (the client already logged the
        # boot locally). Houses aren't server-instanced yet, so the real cross-player effect is the
        # notice; if the target isn't online, correct the client's optimistic "X is booted" log.
        name = str(msg.get("name", "")).strip()[:32]
        if cl.in_world and name:
            target = next((c for c in CLIENTS.values() if c.in_world and c.charname == name), None)
            if target and target is not cl:
                await target.send({"t": "system", "msg": f"{cl.charname} has removed you from their dwelling."})
            else:
                await cl.send({"t": "system", "msg": f"'{name}' is not online — no one was booted from your dwelling."})
    elif t == "ping":
        await cl.send({"t": "pong"})

# ---------------------------------------------------------------- connection
def _reject_const(_c):   # #238: json.loads calls this for NaN/Infinity/-Infinity literals — refuse them
    raise ValueError("non-finite literal")

async def handle(reader, writer):
    peer = writer.get_extra_info("peername")
    cl = Client(reader, writer)
    if not await ws_handshake(reader, writer):
        writer.close(); return
    try:
        while cl.alive:
            frame = await ws_read(reader)
            if frame is None:
                break
            opcode, payload = frame
            if opcode == 0x8:
                break
            # #239/#299: token-bucket rate limit applied to EVERY frame — including ping (0x9) and
            # unparseable payloads, which previously bypassed the limiter entirely (a ping/garbage flood
            # was reflected unbounded as pongs). Refill by elapsed time, spend one general token per
            # frame; drop (don't echo/parse) when empty.
            _nowt = time.time()
            _dt = max(0.0, _nowt - cl.rl_t); cl.rl_t = _nowt
            cl.rl_gen = min(RL_GEN_BURST, cl.rl_gen + _dt * RL_GEN_RATE)
            cl.rl_chat = min(RL_CHAT_BURST, cl.rl_chat + _dt * RL_CHAT_RATE)
            if cl.rl_gen < 1.0:
                if opcode not in (0x9, 0xA) and _nowt - cl.rl_warned > 3.0:
                    cl.rl_warned = _nowt
                    await cl.send({"t": "system", "msg": "You are sending messages too quickly — slow down."})
                continue
            cl.rl_gen -= 1.0
            if opcode == 0x9:  # ping -> pong (now rate-limited above)
                writer.write(ws_frame(payload, 0xA)); await writer.drain(); continue
            if opcode == 0xA:
                continue
            try:
                msg = json.loads(payload.decode("utf-8"), parse_constant=_reject_const)   # #238: reject non-standard NaN/Infinity literals (Python's json accepts them) so no float field can receive a non-finite value; legit clients never send them
            except Exception:
                continue
            if isinstance(msg, dict):
                _mt = msg.get("t")
                if _mt in CHATTY:   # broadcast-type messages also spend a (scarcer) chat token
                    if cl.rl_chat < 1.0:
                        if _nowt - cl.rl_warned > 3.0:
                            cl.rl_warned = _nowt
                            await cl.send({"t": "system", "msg": "You are sending messages too quickly — slow down."})
                        continue
                    cl.rl_chat -= 1.0
                try:
                    await dispatch(cl, msg)   # #251: per-message isolation — a single handler exception (bad field, edge case) logs and continues instead of tearing down the client's connection
                except (asyncio.IncompleteReadError, ConnectionResetError):
                    raise
                except Exception as de:
                    print(f"[dispatch err] {getattr(cl,'username',None)} t={msg.get('t')!r}: {de}")
    except (asyncio.IncompleteReadError, ConnectionResetError):
        pass
    except Exception as e:
        print(f"[conn err] {peer}: {e}")
    finally:
        cl.alive = False
        if cl.username and CLIENTS.get(cl.username) is cl:
            was_in_world = cl.in_world
            who = cl.charname or cl.username
            cl.in_world = False
            if cl.party:
                await party_leave(cl, quiet=True)
            await trade_cancel(cl.username, "Your trading partner left the world.")
            CLIENTS.pop(cl.username, None)
            if was_in_world:
                await broadcast({"t": "system", "msg": f"{who} has left Dereth."})
        try:
            writer.close()
        except Exception:
            pass

async def tick_loop():
    interval = 1.0 / TICK_HZ
    while True:
        await asyncio.sleep(interval)
        if CLIENTS:
            await world_step()
            await broadcast(snapshot())

async def main():
    db().close()  # ensure schema exists
    seed_admin()  # always keep the default Admin account + maxed Kilmer character
    populate_world()
    server = await asyncio.start_server(handle, HOST, PORT)
    asyncio.create_task(tick_loop())
    addr = ", ".join(str(s.getsockname()) for s in server.sockets)
    print(f"Dereth server listening on {addr} (db={DB_PATH}, tick={TICK_HZ}Hz)")
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nshutting down.")
