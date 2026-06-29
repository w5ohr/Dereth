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
import asyncio, base64, hashlib, hmac, json, math, os, random, secrets, sqlite3, struct, time

HOST = os.environ.get("DERETH_HOST", "0.0.0.0")
PORT = int(os.environ.get("DERETH_PORT", "8787"))
DB_PATH = os.environ.get("DERETH_DB", os.path.join(os.path.dirname(__file__), "dereth.db"))
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
TICK_HZ = 10
MAX_MSG = 1 << 20  # 1 MiB per message cap (character saves can be sizable)
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

def create_char_slot(account, slot, name, data):
    if not isinstance(slot, int) or slot < 0 or slot >= MAX_CHARS:
        return False, "Invalid character slot."
    with db() as c:
        if c.execute("SELECT 1 FROM characters WHERE account=? AND slot=?", (account, slot)).fetchone():
            return False, "That slot is already occupied."
        if c.execute("SELECT 1 FROM characters WHERE account=? AND name=?", (account, name)).fetchone():
            return False, "You already have a character with that name."
        c.execute("INSERT INTO characters(account,slot,name,data,created,seen) VALUES(?,?,?,?,?,?)",
                  (account, slot, name, json.dumps(data) if data is not None else None, int(time.time()), int(time.time())))
    return True, None

def save_char_slot(account, slot, data):
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
    while True:
        h = await reader.readline()
        if h in (b"\r\n", b"\n", b""):
            break
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
        if length == 126:
            length = struct.unpack(">H", await reader.readexactly(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", await reader.readexactly(8))[0]
        if length > MAX_MSG or len(data) + length > MAX_MSG:
            return None
        mask = await reader.readexactly(4) if masked else b"\x00\x00\x00\x00"
        payload = bytearray(await reader.readexactly(length))
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
         "hp": cl.hp, "mhp": cl.mhp, "level": cl.level, "heritage": cl.heritage, "title": cl.title}
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
# Monsters cluster near real Dereth towns (world coords = lon*80, -lat*80) so the shared
# population is where players actually spawn/travel — not out at the empty origin.
MOB_CLUSTERS = [
    (2640, -3488),  # Holtburg (Aluvian capital — common spawn)
    (3768, -2072),  # Cragstone
    (2048, -2424),  # Glenden Wood
    (5136, -1528),  # Eastham
    (4744, -880),   # Shoushi (Sho capital)
    (3704, 968),    # Yaraq (Gharu'ndim capital)
    (1472, 128),    # Samsur
    (4824, 2296),   # Sawato
]
MOBS = {}              # id -> mob dict
_mob_seq = 0
WORLD_LIMIT = 7000     # keep mobs inside the playfield
# capitals are safe havens — creatures are pushed out of the town core (mirrors the client)
CAPITALS = [(2640, -3488), (4744, -880), (3704, 968)]   # Holtburg, Shoushi, Yaraq
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
                await cl.send({"t": "event_reward", "xp": xp, "gold": gold, "name": name})
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
    + [f"war_{c}_{g}_{l}" for c in WAR_ELEMENTS for g in ("blast", "volley", "ring", "streak") for l in range(1, 9)]
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
) if s not in _STARTERS]

def roll_item(rare=False, tier=1):
    if SCROLL_SPELLS and random.random() < (0.16 if rare else 0.10):   # sometimes a spell scroll
        return {"scroll": True, "spellId": random.choice(SCROLL_SPELLS), "name": "Spell Scroll"}
    base = random.choice(ITEM_BASE)
    tier = max(1, min(5, int(tier) or (3 if rare else 1)))
    if rare:
        tier = max(1, min(5, tier + 2))
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
    else:
        o["item"] = d["item"]
    return o

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
    if math.hypot(cl.x - d["x"], cl.z - d["z"]) > PICKUP_RANGE:
        return
    DROPS.pop(did, None)
    await broadcast({"t": "drop_gone", "id": did, "by": cl.username})
    if d["type"] == "gold":
        await cl.send({"t": "loot", "type": "gold", "amt": d["amt"]})
    else:
        await cl.send({"t": "loot", "type": "item", "item": d["item"]})

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
        # shared XP: everyone who damaged it earns full XP; the killer's nearby
        # party members share it too (fellowship), even without tagging the mob.
        recipients = set(m.get("dealt", {cl.username: dmg}).keys())
        if cl.party in PARTIES:
            for acc in PARTIES[cl.party]["members"]:
                c = CLIENTS.get(acc)
                if c and c.in_world and math.hypot(c.x - m["x"], c.z - m["z"]) <= FELLOW_RANGE:
                    recipients.add(acc)
        for u in recipients:
            c = CLIENTS.get(u)
            if c:
                await c.send({"t": "reward", "xp": m["xp"], "kind": m["kind"], "boss": is_boss})
        # gold + items drop on the ground as shared, first-come loot
        await spawn_loot(m, is_boss)

# ---------------------------------------------------------------- parties (fellowships)
EMOTES = {"wave": "waves.", "cheer": "cheers!", "dance": "breaks into a dance.", "bow": "bows solemnly.",
          "laugh": "laughs.", "point": "points.", "salute": "salutes.", "flex": "flexes.",
          "kneel": "kneels.", "clap": "applauds."}
PARTIES = {}           # pid -> {"leader": account, "members": [accounts]}
_party_seq = 0
PARTY_MAX = 6

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

# ---------------------------------------------------------------- auth + dispatch
def valid_name(u):
    return isinstance(u, str) and 3 <= len(u) <= 16 and all(ch.isalnum() or ch in "_-" for ch in u)

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
    await cl.send({"t": "play_ok", "slot": slot, "name": name, "char": data})
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
        return
    if t == "input":
        cl.x = float(msg.get("x", cl.x)); cl.z = float(msg.get("z", cl.z))
        cl.yaw = float(msg.get("yaw", cl.yaw)); cl.hp = int(msg.get("hp", cl.hp))
        cl.mhp = int(msg.get("mhp", cl.mhp)); cl.level = int(msg.get("level", cl.level))
        cl.heritage = str(msg.get("heritage", cl.heritage))[:16]
        cl.title = str(msg.get("title", cl.title))[:40]
    elif t == "chat":
        text = str(msg.get("msg", ""))[:240].strip()
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
    elif t == "who":
        players = [{"name": c.charname or u, "level": c.level} for u, c in CLIENTS.items() if c.in_world]
        await cl.send({"t": "who", "players": players})
    elif t == "emote":
        if cl.in_world:
            act = str(msg.get("act", ""))
            if act == "me":
                text = str(msg.get("text", ""))[:80].strip()
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
    elif t == "tell":
        name = str(msg.get("name", ""))
        text = str(msg.get("msg", ""))[:240].strip()
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
    elif t == "pchat":
        text = str(msg.get("msg", ""))[:240].strip()
        if cl.in_world and cl.party in PARTIES and text:
            for acc in PARTIES[cl.party]["members"]:
                c = CLIENTS.get(acc)
                if c:
                    await c.send({"t": "chat", "from": cl.charname, "msg": text, "channel": "party", "ts": int(time.time())})
        elif cl.in_world:
            await cl.send({"t": "system", "msg": "You are not in a party (/party invite <name>)."})
    elif t == "ping":
        await cl.send({"t": "pong"})

# ---------------------------------------------------------------- connection
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
            if opcode == 0x9:  # ping -> pong
                writer.write(ws_frame(payload, 0xA)); await writer.drain(); continue
            if opcode == 0xA:
                continue
            try:
                msg = json.loads(payload.decode("utf-8"))
            except Exception:
                continue
            if isinstance(msg, dict):
                await dispatch(cl, msg)
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
