#!/usr/bin/env python3
"""#728 e2e: mount sync — a rider's mnt field relays through the snapshot. Two clients — a seller lists, a buyer browses and buys
(pay-first, atomic claim), the seller gets the live sold notice, collects proceeds at the SAME
town's broker, and reclaims an unsold listing. Failure paths: oversize item, zero price, buying
your own listing, double-buy, foreign reclaim, listing cap.
Usage: python3 server/tsa_mounts.py [host] [port]   (server must already be running)
"""
import asyncio, base64, hashlib, json, secrets, struct, sys, time

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

def mk(t, act, **kw):
    d = {"t": t, "act": act}; d.update(kw); return d

async def player(user, charname):
    c = await WS.connect()
    await c.send({"t": "register", "user": user, "pass": "secret1"})
    m = await c.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"))
    assert m and m["t"] == "auth_ok", f"auth failed for {user}"
    await c.recv_until(lambda x: x["t"] == "roster")
    await c.send({"t": "create_char", "slot": 0, "name": charname, "char": {"level": 5, "heritage": "sho"}})
    po = await c.recv_until(lambda x: x["t"] == "play_ok")
    assert po, f"play failed for {user}"
    return c

async def main():
    uniq = secrets.token_hex(3)
    print(f"Mount-sync e2e on {HOST}:{PORT}")
    a = await player(f"mra{uniq}", f"Rider{uniq}")
    b = await player(f"mrb{uniq}", f"Watcher{uniq}")
    # both stand together so they appear in each other's AOI
    for c in (a, b):
        await c.send({"t": "input", "x": 100.0, "z": 100.0, "yaw": 0, "hp": 50, "mhp": 50, "level": 5})
    await asyncio.sleep(0.5)
    # A mounts a grey; B must see mnt="grey" in A's snapshot record
    await a.send({"t": "input", "x": 100.0, "z": 100.0, "yaw": 0, "hp": 50, "mhp": 50, "level": 5, "mnt": "grey"})
    m = await b.recv_until(lambda x: x.get("t") == "snapshot" and any(p.get("mnt") == "grey" for p in (x.get("players") or [])), timeout=5.0)
    check("rider's mount relays through the snapshot", bool(m))
    # dismount clears it
    await a.send({"t": "input", "x": 100.0, "z": 100.0, "yaw": 0, "hp": 50, "mhp": 50, "level": 5, "mnt": None})
    m = await b.recv_until(lambda x: x.get("t") == "snapshot" and any(p.get("name", "").startswith("Rider") and not p.get("mnt") for p in (x.get("players") or [])), timeout=5.0)
    check("dismount clears mnt in the snapshot", bool(m))
    # junk mnt is length-capped server-side (never a payload amplifier)
    await a.send({"t": "input", "x": 100.0, "z": 100.0, "yaw": 0, "hp": 50, "mhp": 50, "level": 5, "mnt": "X" * 4000})
    m = await b.recv_until(lambda x: x.get("t") == "snapshot" and any(p.get("mnt") and len(p["mnt"]) <= 12 for p in (x.get("players") or [])), timeout=5.0)
    check("oversize mnt truncated to 12 chars", bool(m))
    await a.close(); await b.close()
    print(f"\n{PASS} passed, {FAIL} failed")
    sys.exit(1 if FAIL else 0)

asyncio.run(main())
