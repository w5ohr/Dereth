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
import asyncio, base64, hashlib, hmac, json, os, secrets, sqlite3, struct, time

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

def snapshot():
    return {"t": "snapshot", "players": [
        {"id": u, "name": u, "x": round(cl.x, 2), "z": round(cl.z, 2), "yaw": round(cl.yaw, 3),
         "hp": cl.hp, "mhp": cl.mhp, "level": cl.level, "heritage": cl.heritage, "title": cl.title}
        for u, cl in CLIENTS.items()]}

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
            await broadcast(snapshot())

async def main():
    db().close()  # ensure schema exists
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
