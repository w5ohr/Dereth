#!/usr/bin/env python3
"""Simulated player for integration testing: connects, registers/logs in, and streams
input at a fixed position for a few seconds so a real browser client can render it.
Usage: python3 server/ghost.py <name> [seconds] [x] [z]"""
import asyncio, os, sys
sys.path.insert(0, os.path.dirname(__file__))
import test_client as tc
tc.HOST = os.environ.get("DERETH_HOST", "127.0.0.1")
tc.PORT = int(os.environ.get("DERETH_PORT", "8787"))

async def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "ghost1"
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
    x = float(sys.argv[3]) if len(sys.argv) > 3 else 120.0
    z = float(sys.argv[4]) if len(sys.argv) > 4 else 60.0
    w = await tc.WS.connect()
    await w.send({"t": "register", "user": name, "pass": "ghostpw"})
    m = await w.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"))
    if m and m["t"] == "auth_err":
        await w.send({"t": "login", "user": name, "pass": "ghostpw"})
        await w.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"))
    print(f"ghost {name} online at ({x},{z}) for {secs}s")
    for _ in range(int(secs * 5)):
        await w.send({"t": "input", "x": x, "z": z, "yaw": 1.0, "hp": 77, "mhp": 100, "level": 4, "heritage": "sho"})
        await asyncio.sleep(0.2)
    await w.close()

if __name__ == "__main__":
    asyncio.run(main())
