#!/usr/bin/env python3
"""TestSystemA robustness fuzz: malformed input must never kill the server or
poison other sessions. Usage: python tsa_fuzz.py [host] [port]"""
import asyncio, json, secrets, struct, sys
sys.path.insert(0, ".")
from test_client import WS

RESULTS = []
def check(name, ok, note=""):
    RESULTS.append((name, bool(ok)))
    print(f"  {'PASS' if ok else 'FAIL'} {name}{(' -- ' + str(note)) if note and not ok else ''}")

async def raw_frame(payload_bytes):
    """Open a socket, complete the WS handshake, send raw bytes as one text frame."""
    w = await WS.connect()
    mask = secrets.token_bytes(4)
    masked = bytes(b ^ mask[i & 3] for i, b in enumerate(payload_bytes))
    n = len(payload_bytes); out = bytearray([0x81])
    if n < 126: out.append(0x80 | n)
    elif n < (1 << 16): out.append(0x80 | 126); out += struct.pack(">H", n)
    else: out.append(0x80 | 127); out += struct.pack(">Q", n)
    out += mask + masked
    w.w.write(bytes(out)); await w.w.drain()
    return w

async def healthy():
    """Can a fresh, well-behaved client still register and enter the world?"""
    c = await WS.connect()
    u = f"hz_{secrets.token_hex(4)}"
    await c.send({"t": "register", "user": u, "pass": "secret9"})
    ok = await c.recv_until(lambda x: x["t"] == "auth_ok", timeout=4)
    if ok:
        await c.recv_until(lambda x: x["t"] == "roster", timeout=3)
        await c.send({"t": "create_char", "slot": 0, "name": u[:14], "char": {}})
        ok = await c.recv_until(lambda x: x["t"] == "play_ok", timeout=4)
    await c.close()
    return bool(ok)

async def main():
    check("baseline healthy client", await healthy())

    # 1. non-JSON text frame
    w = await raw_frame(b"this is not json {{{{")
    await asyncio.sleep(0.3); await w.close()
    check("survives non-JSON frame", await healthy())

    # 2. JSON but not an object / missing t / unknown t / wrong types
    for payload in (b"[1,2,3]", b"42", b"{}", b'{"t": 12345}', b'{"t":"nosuchtype","x":1}',
                    b'{"t":"input","x":"NaN","z":{},"yaw":[]}'):
        w = await raw_frame(payload)
        await asyncio.sleep(0.2); await w.close()
    check("survives malformed JSON shapes (6 kinds)", await healthy())

    # 3. oversized name / huge chat / absurd numbers via proper client
    c = await WS.connect()
    await c.send({"t": "register", "user": "A" * 5000, "pass": "secret9"})
    r1 = await c.recv_until(lambda x: x["t"] == "auth_err", timeout=3)
    check("oversized register name rejected", r1)
    await c.close()
    c = await WS.connect()
    u = f"fz_{secrets.token_hex(4)}"
    await c.send({"t": "register", "user": u, "pass": "secret9"})
    await c.recv_until(lambda x: x["t"] == "auth_ok")
    await c.recv_until(lambda x: x["t"] == "roster")
    await c.send({"t": "create_char", "slot": 0, "name": u[:14], "char": {}})
    await c.recv_until(lambda x: x["t"] == "play_ok")
    await c.send({"t": "chat", "msg": "x" * 100000})
    await c.send({"t": "input", "x": 1e308, "z": -1e308, "yaw": float("inf") if False else 9e99})
    await c.send({"t": "attack", "id": "m" * 10000, "dmg": -5})
    await c.send({"t": "pickup", "id": None})
    await c.send({"t": "trade", "act": "coin", "amount": "not-a-number"})
    await c.send({"t": "save", "char": "not-a-dict"})
    await asyncio.sleep(0.5); await c.close()
    check("survives hostile in-world fields", await healthy())

    # 4. pre-auth world messages (no register/login first)
    c = await WS.connect()
    for m in ({"t": "attack", "id": "x", "dmg": 99}, {"t": "chat", "msg": "hi"},
              {"t": "save", "char": {"gold": 1e9}}, {"t": "trade", "act": "ok"}):
        await c.send(m)
    await asyncio.sleep(0.3); await c.close()
    check("pre-auth world messages ignored", await healthy())

    # 5. rapid-fire burst (500 msgs)
    c = await WS.connect()
    u = f"burst_{secrets.token_hex(3)}"
    await c.send({"t": "register", "user": u, "pass": "secret9"})
    await c.recv_until(lambda x: x["t"] == "auth_ok")
    await c.recv_until(lambda x: x["t"] == "roster")
    await c.send({"t": "create_char", "slot": 0, "name": u[:14], "char": {}})
    await c.recv_until(lambda x: x["t"] == "play_ok")
    for i in range(500):
        await c.send({"t": "input", "x": i % 200, "z": (i * 7) % 200, "yaw": 0})
    await asyncio.sleep(0.5); await c.close()
    check("survives 500-message burst", await healthy())

    # 6. abrupt disconnect mid-trade (no cancel)
    a = await WS.connect(); b = await WS.connect()
    for c2, nm in ((a, "dcA"), (b, "dcB")):
        u = f"{nm}_{secrets.token_hex(3)}"
        await c2.send({"t": "register", "user": u, "pass": "secret9"})
        await c2.recv_until(lambda x: x["t"] == "auth_ok")
        await c2.recv_until(lambda x: x["t"] == "roster")
        await c2.send({"t": "create_char", "slot": 0, "name": u[:14], "char": {}})
        await c2.recv_until(lambda x: x["t"] == "play_ok")
        c2.charname = u[:14]
        await c2.send({"t": "input", "x": 50, "z": 50, "yaw": 0})
    await asyncio.sleep(0.3)
    await a.send({"t": "trade", "act": "open", "name": b.charname})
    await b.recv_until(lambda x: x["t"] == "trade", timeout=3)
    a.w.close()   # yank the socket, no goodbye
    await asyncio.sleep(0.8)
    ended = await b.recv_until(lambda x: x["t"] in ("trade", "system"), timeout=3)
    check("partner notified after mid-trade disconnect", ended)
    await b.close()
    check("final health", await healthy())

    p = sum(1 for _, ok in RESULTS if ok); f = len(RESULTS) - p
    print(f"\n{p} passed, {f} failed")

asyncio.run(main())
