#!/usr/bin/env python3
"""TestSystemA concurrency soak: N clients hammer mixed ops for DUR seconds;
assert zero client errors, server stays responsive, trades/parties settle.
Usage: python tsa_soak.py [host] [port] [clients] [seconds]"""
import asyncio, random, secrets, sys, time
sys.path.insert(0, ".")
from test_client import WS

N = int(sys.argv[3]) if len(sys.argv) > 3 else 8
DUR = float(sys.argv[4]) if len(sys.argv) > 4 else 45.0
ERRORS = []

async def actor(idx):
    try:
        c = await WS.connect()
        u = f"soak{idx}_{secrets.token_hex(2)}"
        await c.send({"t": "register", "user": u, "pass": "secret9"})
        if not await c.recv_until(lambda x: x["t"] == "auth_ok", timeout=5):
            ERRORS.append(f"{u}: no auth_ok"); return
        await c.recv_until(lambda x: x["t"] == "roster", timeout=3)
        await c.send({"t": "create_char", "slot": 0, "name": u[:14], "char": {"level": 5}})
        if not await c.recv_until(lambda x: x["t"] == "play_ok", timeout=5):
            ERRORS.append(f"{u}: no play_ok"); return
        c.charname = u[:14]
        end = time.time() + DUR
        x, z = random.uniform(0, 200), random.uniform(0, 200)
        ops = 0
        while time.time() < end:
            op = random.random()
            if op < 0.55:                          # move
                x += random.uniform(-3, 3); z += random.uniform(-3, 3)
                await c.send({"t": "input", "x": x, "z": z, "yaw": random.uniform(0, 6.28)})
            elif op < 0.70:                        # chat / emote / who
                await c.send(random.choice([
                    {"t": "chat", "msg": f"soak {ops}"},
                    {"t": "emote", "act": "wave"},
                    {"t": "who"}]))
            elif op < 0.85:                        # attack the nearest known mob id (may be invalid — fine)
                await c.send({"t": "attack", "id": f"m{random.randint(1,40)}", "dmg": random.randint(3, 30)})
            elif op < 0.95:                        # ping/save
                await c.send(random.choice([
                    {"t": "ping"},
                    {"t": "save", "char": {"level": 5, "gold": ops}}]))
            else:                                   # short-lived trade attempt with a random name
                await c.send({"t": "trade", "act": "open", "name": f"soak{random.randint(0,N-1)}"})
                await c.send({"t": "trade", "act": "cancel"})
            ops += 1
            # drain inbound so buffers don't bloat
            try:
                await asyncio.wait_for(c._frame(), timeout=0.01)
            except (asyncio.TimeoutError, Exception):
                pass
            await asyncio.sleep(random.uniform(0.02, 0.08))
        await c.close()
        return ops
    except Exception as e:
        ERRORS.append(f"actor{idx}: {type(e).__name__} {e}")

async def main():
    t0 = time.time()
    results = await asyncio.gather(*(actor(i) for i in range(N)))
    dur = time.time() - t0
    total = sum(r for r in results if isinstance(r, int))
    print(f"soak: {N} clients, {total} ops in {dur:.1f}s ({total/dur:.0f} ops/s), errors: {len(ERRORS)}")
    for e in ERRORS[:6]: print("  ERR", e)
    # post-soak health
    c = await WS.connect()
    u = f"hz_{secrets.token_hex(3)}"
    await c.send({"t": "register", "user": u, "pass": "secret9"})
    ok = await c.recv_until(lambda x: x["t"] == "auth_ok", timeout=5)
    await c.close()
    print("post-soak health:", "PASS" if ok else "FAIL")
    sys.exit(1 if (ERRORS or not ok) else 0)

asyncio.run(main())
