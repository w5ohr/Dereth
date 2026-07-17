#!/usr/bin/env python3
"""#960 e2e: jump sync — a player's vertical offset (jy) and jump-charge (jc) relay through the
snapshot so peers render the leap/crouch. Absent fields reset to 0 (jumps are transient); values
are finite-clamped and range-bounded server-side.
Usage: python3 server/tsa_jump.py [host] [port]   (server must already be running)
"""
import asyncio, base64, json, secrets, struct, sys, time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8787


class WS:
    def __init__(self, reader, writer):
        self.r = reader; self.w = writer
    @classmethod
    async def connect(cls):
        r, w = await asyncio.open_connection(HOST, PORT)
        key = base64.b64encode(secrets.token_bytes(16)).decode()
        w.write((f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nUpgrade: websocket\r\n"
                 f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                 "Sec-WebSocket-Version: 13\r\n\r\n").encode())
        await w.drain()
        while True:
            line = await r.readline()
            if line in (b"\r\n", b"\n", b""):
                break
        return cls(r, w)
    async def send(self, obj):
        payload = json.dumps(obj).encode()
        mask = secrets.token_bytes(4)
        masked = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
        n = len(payload); out = bytearray([0x81])
        if n < 126: out.append(0x80 | n)
        elif n < (1 << 16): out.append(0x80 | 126); out += struct.pack(">H", n)
        else: out.append(0x80 | 127); out += struct.pack(">Q", n)
        out += mask + masked
        self.w.write(bytes(out)); await self.w.drain()
    async def _frame(self):
        hdr = await self.r.readexactly(2)
        ln = hdr[1] & 0x7F
        if ln == 126: ln = struct.unpack(">H", await self.r.readexactly(2))[0]
        elif ln == 127: ln = struct.unpack(">Q", await self.r.readexactly(8))[0]
        return json.loads((await self.r.readexactly(ln)).decode())
    async def recv_until(self, pred, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            try:
                msg = await asyncio.wait_for(self._frame(), timeout=end - time.time())
            except asyncio.TimeoutError:
                return None
            if pred(msg):
                return msg
        return None
    async def close(self):
        self.w.close()


PASS = 0; FAIL = 0
def check(name, ok):
    global PASS, FAIL
    print(("  PASS " if ok else "  FAIL ") + name)
    if ok: PASS += 1
    else: FAIL += 1

async def player(user, charname):
    c = await WS.connect()
    await c.send({"t": "register", "user": user, "pass": "secret1"})
    m = await c.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"))
    assert m and m["t"] == "auth_ok", f"auth failed for {user}"
    await c.recv_until(lambda x: x["t"] == "roster")
    await c.send({"t": "create_char", "slot": 0, "name": charname, "char": {"level": 5, "heritage": "aluvian"}})
    po = await c.recv_until(lambda x: x["t"] == "play_ok")
    assert po, f"play failed for {user}"
    return c

def rec_of(msg, prefix):
    for p in (msg.get("players") or []):
        if (p.get("name", "") or "").startswith(prefix):
            return p
    return None

async def main():
    uniq = secrets.token_hex(3)
    print(f"Jump-sync e2e on {HOST}:{PORT}")
    a = await player(f"jpa{uniq}", f"Jumper{uniq}")
    b = await player(f"jpb{uniq}", f"Watcher{uniq}")
    base = {"yaw": 0, "hp": 50, "mhp": 50, "level": 5}
    for c in (a, b):
        await c.send({"t": "input", "x": 100.0, "z": 100.0, **base})
    await asyncio.sleep(0.4)

    # A is airborne: jy relays to B
    await a.send({"t": "input", "x": 100.0, "z": 100.0, "jy": 3.5, **base})
    m = await b.recv_until(lambda x: x.get("t") == "snapshot" and (rec_of(x, "Jumper") or {}).get("jy", 0) > 3.0, timeout=5.0)
    check("airborne jy relays through the snapshot", bool(m))

    # A charging: jc relays
    await a.send({"t": "input", "x": 100.0, "z": 100.0, "jc": 0.8, **base})
    m = await b.recv_until(lambda x: x.get("t") == "snapshot" and (rec_of(x, "Jumper") or {}).get("jc", 0) > 0.5, timeout=5.0)
    check("jump-charge jc relays through the snapshot", bool(m))

    # landed: absent jy/jc reset to 0 (transient, not cached like gear)
    await a.send({"t": "input", "x": 100.0, "z": 100.0, **base})
    m = await b.recv_until(lambda x: x.get("t") == "snapshot" and (r := rec_of(x, "Jumper")) is not None
                           and r.get("jy", 0) == 0 and r.get("jc", 0) == 0, timeout=5.0)
    check("landing resets jy/jc to 0 (not cached)", bool(m))

    # garbage / out-of-range is clamped, never NaN or an amplifier
    await a.send({"t": "input", "x": 100.0, "z": 100.0, "jy": 1e12, "jc": 99, **base})
    m = await b.recv_until(lambda x: x.get("t") == "snapshot" and (r := rec_of(x, "Jumper")) is not None
                           and -40 <= r.get("jy", 0) <= 40 and 0 <= r.get("jc", 0) <= 1, timeout=5.0)
    check("out-of-range jy/jc clamped", bool(m))

    await a.send({"t": "input", "x": 100.0, "z": 100.0, "jy": float("nan"), **base})
    m = await b.recv_until(lambda x: x.get("t") == "snapshot" and (r := rec_of(x, "Jumper")) is not None
                           and isinstance(r.get("jy"), (int, float)) and r.get("jy") == r.get("jy"), timeout=5.0)
    check("NaN jy rejected (finite)", bool(m))

    await a.close(); await b.close()
    print(f"\n{PASS} passed, {FAIL} failed")
    sys.exit(1 if FAIL else 0)

asyncio.run(main())
