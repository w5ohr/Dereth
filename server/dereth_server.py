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
PROTOCOL_VERSION = 1

# ---------------------------------------------------------------- persistence
def db():
    c = sqlite3.connect(DB_PATH)
    c.execute("""CREATE TABLE IF NOT EXISTS users(
        username TEXT PRIMARY KEY, salt TEXT NOT NULL, pw TEXT NOT NULL,
        char TEXT, created INTEGER, seen INTEGER)""")
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

def load_char(username):
    with db() as c:
        row = c.execute("SELECT char FROM users WHERE username=?", (username,)).fetchone()
    if row and row[0]:
        try:
            return json.loads(row[0])
        except Exception:
            return None
    return None

def save_char(username, char_obj):
    with db() as c:
        c.execute("UPDATE users SET char=?, seen=? WHERE username=?",
                  (json.dumps(char_obj), int(time.time()), username))

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
        self.username = None
        self.token = None
        self.alive = True
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
    dead = []
    for u, cl in list(CLIENTS.items()):
        if cl is exclude:
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
        {"id": u, "name": u, "x": round(cl.x, 2), "z": round(cl.z, 2), "yaw": round(cl.yaw, 3),
         "hp": cl.hp, "mhp": cl.mhp, "level": cl.level, "heritage": cl.heritage, "title": cl.title}
        for u, cl in CLIENTS.items()],
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
# Monsters cluster near these overworld anchors (players spawn around origin).
MOB_CLUSTERS = [(0, 0), (320, 220), (-260, 180), (180, -300), (-300, -240), (260, 320)]
MOBS = {}              # id -> mob dict
_mob_seq = 0
WORLD_LIMIT = 7000     # keep mobs inside the playfield
ATTACK_RANGE = 16.0    # max client→mob distance accepted for an attack intent (melee+ranged+latency)

def spawn_mob(kind=None, near=None):
    global _mob_seq
    _mob_seq += 1
    if kind is None:
        kind = random.choice(list(MOB_BESTIARY))
    b = MOB_BESTIARY[kind]
    cx, cz = near if near else random.choice(MOB_CLUSTERS)
    a, rr = random.uniform(0, 6.28), random.uniform(20, 140)
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
    "queen":   {"name": "Gnawvil, the Olthoi Queen",   "kind": "olthoi", "hp": 4000, "dmg": 45, "spd": 5.6, "xp": 4000,  "gold": (140, 300),  "size": 2.0, "sense": 80, "atk": 1.4, "scale": 2.2, "home": (560, 560),     "respawn": 90.0},
    "apex":    {"name": "Bael'Zharon, the Hopeslayer",  "kind": "shadow", "hp": 9000, "dmg": 70, "spd": 5.0, "xp": 30000, "gold": (800, 1500), "size": 1.0, "sense": 90, "atk": 1.5, "scale": 3.6, "home": (-1200, -1200), "respawn": 150.0, "tint": 0x7a4fae},
    "genLer":  {"name": "Ler Rhan, Shadow General",     "kind": "shadow", "hp": 1800, "dmg": 42, "spd": 6.0, "xp": 11000, "gold": (260, 520),  "size": 1.0, "sense": 80, "atk": 1.4, "scale": 2.1, "home": (1100, -900),   "respawn": 120.0, "tint": 0x8a5fc8},
    "genFerah":{"name": "Black Ferah, Shadow General",  "kind": "shadow", "hp": 1800, "dmg": 42, "spd": 6.0, "xp": 11000, "gold": (260, 520),  "size": 1.0, "sense": 80, "atk": 1.4, "scale": 2.1, "home": (-1000, 1100),  "respawn": 120.0, "tint": 0x4a3a6a},
    "genIsin": {"name": "Isin Dule, Shadow General",    "kind": "shadow", "hp": 1800, "dmg": 42, "spd": 6.0, "xp": 11000, "gold": (260, 520),  "size": 1.0, "sense": 80, "atk": 1.4, "scale": 2.1, "home": (1300, 1300),   "respawn": 120.0, "tint": 0xc05fae},
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
EVENT_ANCHORS = [(900, -200), (-700, 400), (300, 900), (-900, -600), (700, 700)]
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

def roll_item(rare=False, tier=1):
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
        if cl.hp <= 0:
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
                step = min(m["spd"] * DT, dist - reach)
                m["x"] += dx / dist * step; m["z"] += dz / dist * step
            elif m["atkcd"] <= 0:
                m["atkcd"] = m["atkcd_max"]
                hits.append((pl.username, m["dmg"], m["kind"], m["x"], m["z"]))
        else:
            m["state"] = "wander"; m["target"] = None
            m["wt"] -= DT
            if m["wt"] <= 0:
                m["wt"] = random.uniform(1.5, 4.0); m["yaw"] = random.uniform(0, 6.28)
            sp = m["spd"] * 0.3
            m["x"] += math.sin(m["yaw"]) * sp * DT; m["z"] += math.cos(m["yaw"]) * sp * DT
        m["x"] = max(-WORLD_LIMIT, min(WORLD_LIMIT, m["x"]))
        m["z"] = max(-WORLD_LIMIT, min(WORLD_LIMIT, m["z"]))
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
            await broadcast({"t": "system", "msg": f"{cl.username} has slain {m['name']}! Glory echoes across Dereth."})
        # shared XP: every player who damaged it earns full XP
        dealt = m.get("dealt", {cl.username: dmg})
        for u in dealt:
            c = CLIENTS.get(u)
            if c:
                await c.send({"t": "reward", "xp": m["xp"], "kind": m["kind"], "boss": is_boss})
        # gold + items drop on the ground as shared, first-come loot
        await spawn_loot(m, is_boss)

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
    TOKENS[cl.token] = username
    CLIENTS[username] = cl
    await cl.send({"t": "auth_ok", "id": username, "name": username,
                   "token": cl.token, "char": load_char(username), "pv": PROTOCOL_VERSION})
    # replay current ground loot + any active Incursion so a late joiner is in sync
    for d in list(DROPS.values()):
        await cl.send(drop_pub(d))
    if EVENT.get("active"):
        await cl.send(event_pub())
    await broadcast({"t": "system", "msg": f"{username} has entered Dereth."}, exclude=cl)

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
    if t == "input":
        cl.x = float(msg.get("x", cl.x)); cl.z = float(msg.get("z", cl.z))
        cl.yaw = float(msg.get("yaw", cl.yaw)); cl.hp = int(msg.get("hp", cl.hp))
        cl.mhp = int(msg.get("mhp", cl.mhp)); cl.level = int(msg.get("level", cl.level))
        cl.heritage = str(msg.get("heritage", cl.heritage))[:16]
        cl.title = str(msg.get("title", cl.title))[:40]
    elif t == "chat":
        text = str(msg.get("msg", ""))[:240].strip()
        if text:
            await broadcast({"t": "chat", "from": cl.username, "msg": text, "ts": int(time.time())})
    elif t == "attack":
        mid = msg.get("id")
        if isinstance(mid, str):
            await resolve_attack(cl, mid, msg.get("dmg", 0))
    elif t == "pickup":
        did = msg.get("id")
        if isinstance(did, str):
            await do_pickup(cl, did)
    elif t == "save":
        char = msg.get("char")
        if isinstance(char, dict):
            save_char(cl.username, char)
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
            CLIENTS.pop(cl.username, None)
            await broadcast({"t": "system", "msg": f"{cl.username} has left Dereth."})
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
