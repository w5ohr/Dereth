#!/usr/bin/env python3
"""#S2 forged-chargen regression. create_char used to adopt the client's character blob wholesale (via
sanitize_save, which only clamps to PLAUSIBLE values), so a modified client could seed 2e9 gold + level
275 + a forged 500-item inventory at creation — bypassing every post-load rate limiter. Now a newly
created slot is forced to a server economy baseline. Asserts:
  - a forged create (huge gold/level/xp + forged inv) persists as a STARTER (gold 0, level 1, xp 0, inv []),
  - the authoritative level is 1 (a peer sees level 1 even when the client input claims 275),
  - the SAVE path still persists legit progression (create is locked down, play+save still works),
  - a forged non-economy field (heritage) is preserved (only economy/progression is reset).
No special server flag — this fix is always on. Self-contained. Usage: python3 tsa_forged_chargen.py"""
import asyncio, os, secrets, socket, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

def _free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

PORT = _free_port()
DB = tempfile.NamedTemporaryFile(prefix="tsa_chargen_", suffix=".db", delete=False).name

import test_client
test_client.HOST = "127.0.0.1"; test_client.PORT = PORT
from test_client import WS

RESULTS = []
def check(name, ok, note=""):
    RESULTS.append(bool(ok))
    print(f"  {'PASS' if ok else 'FAIL'} {name}{(' -- ' + str(note)) if note and not ok else ''}")

FORGED = {"gold": 2_000_000_000, "level": 275, "xp": 1e12, "xpUnspent": 1e9,
          "materials": 1e8, "luminance": 1e6, "heritage": "gharu",
          "inv": [{"name": f"Forged Legendary {i}", "stat": "weapon", "v": 9999} for i in range(500)]}

async def register(c, name):
    u = f"{name}_{secrets.token_hex(3)}"
    await c.send({"t": "register", "user": u, "pass": "secret9"})
    await c.recv_until(lambda x: x["t"] == "auth_ok", timeout=5)
    await c.recv_until(lambda x: x["t"] == "roster", timeout=3)
    return u

async def main():
    a = await WS.connect()
    ua = await register(a, "forge")
    # ---- forged create -> starter --------------------------------------------------------------------
    await a.send({"t": "create_char", "slot": 0, "name": ua[:14], "char": FORGED})
    po = await a.recv_until(lambda x: x["t"] == "play_ok", timeout=5)
    ch = (po or {}).get("char") or {}
    aid = (po or {}).get("netid")
    check("forged create still enters world", bool(po) and po["t"] == "play_ok")
    check("forged gold reset to 0", ch.get("gold") == 0, f"gold={ch.get('gold')}")
    check("forged level reset to 1", ch.get("level") == 1, f"level={ch.get('level')}")
    check("forged xp reset to 0", ch.get("xp") == 0, f"xp={ch.get('xp')}")
    check("forged 500-item inventory dropped", ch.get("inv") == [], f"inv len={len(ch.get('inv') or [])}")
    check("forged materials/luminance reset", (ch.get("materials") in (0, None)) and (ch.get("luminance") in (0, None)),
          f"materials={ch.get('materials')}, luminance={ch.get('luminance')}")
    check("non-economy field (heritage) preserved", ch.get("heritage") == "gharu", f"heritage={ch.get('heritage')}")

    # ---- authoritative level is 1: a peer sees level 1 even if input claims 275 ---------------------
    b = await WS.connect(); ub = await register(b, "obs")
    await b.send({"t": "create_char", "slot": 0, "name": ub[:14], "char": None})
    await b.recv_until(lambda x: x["t"] == "play_ok", timeout=5)
    # both stand together; the forger's client claims level 275 in its input
    for _ in range(3):
        await a.send({"t": "input", "x": 0, "z": 0, "yaw": 0, "hp": 100, "level": 275})
        await b.send({"t": "input", "x": 0, "z": 0, "yaw": 0, "hp": 100})
        await asyncio.sleep(0.05)
    snap = await b.recv_until(lambda x: x["t"] == "snapshot" and any(p.get("id") == aid for p in x.get("players", [])), timeout=3)
    prec = next((p for p in (snap or {}).get("players", []) if p.get("id") == aid), None) if snap else None
    check("authoritative level bound to 1 (peer sees level 1, not 275)",
          prec is not None and prec.get("level") == 1, f"peer-observed record={prec}")

    # ---- the SAVE path still persists legit progression (create locked, play+save works) ------------
    await a.send({"t": "save", "char": {"level": 5, "kills": 42, "heritage": "gharu"}})
    await asyncio.sleep(0.4)
    await a.close()
    a2 = await WS.connect()
    await a2.send({"t": "login", "user": ua, "pass": "secret9"})
    await a2.recv_until(lambda x: x["t"] == "auth_ok", timeout=5)
    await a2.recv_until(lambda x: x["t"] == "roster", timeout=3)
    await a2.send({"t": "play_char", "slot": 0})
    pd = await a2.recv_until(lambda x: x["t"] == "play_ok", timeout=5)
    sc = (pd or {}).get("char") or {}
    check("SAVE path still persists progression (kills 42 saved after create-lockdown)", sc.get("kills") == 42,
          f"saved char={sc}")

    await a2.close(); await b.close()
    p = sum(1 for ok in RESULTS if ok); f = len(RESULTS) - p
    print(f"\n{p} passed, {f} failed")
    return f

def run():
    env = dict(os.environ)
    env.update(DERETH_PORT=str(PORT), DERETH_DB=DB, DERETH_EVENT_CD="99999", DERETH_HOST="127.0.0.1")
    env.pop("DERETH_ADMIN_PW", None)
    proc = subprocess.Popen([sys.executable, os.path.join(HERE, "dereth_server.py")],
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            try:
                s = socket.create_connection(("127.0.0.1", PORT), timeout=0.3); s.close(); break
            except OSError:
                time.sleep(0.1)
        else:
            print("server did not start"); return 1
        return asyncio.run(main())
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        try:
            os.unlink(DB)
        except OSError:
            pass

if __name__ == "__main__":
    sys.exit(1 if run() else 0)
