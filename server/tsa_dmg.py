#!/usr/bin/env python3
"""#S1 combat-authority regression. Spins up its OWN dereth_server with DERETH_ENFORCE_COMBAT=1 (movement
enforcement left OFF so the harness can still teleport onto its target) and asserts against the world boss:
  - a single huge claimed hit (dmg:99999) does NOT one-shot the boss (per-hit cap),
  - the damage actually applied is ≤ mhp*HIT_FRAC (+rounding),
  - an immediate second hit on the same mob is rejected (per-mob attack cooldown),
  - the boss is still killable with a few properly-spaced hits (enforcement doesn't make it invincible).
Self-contained. Usage: python3 tsa_dmg.py"""
import asyncio, os, secrets, socket, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# mirrors dereth_server defaults (kept in sync intentionally — the test spawns the server with defaults)
HIT_FRAC = 0.34
ATTACK_CD = 0.10
WSCALE = 3
QUEEN_LAIR = (4200 * WSCALE, -1400 * WSCALE)   # Gnawvil the Olthoi Queen — a wilderness boss lair

def _free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

PORT = _free_port()
DB = tempfile.NamedTemporaryFile(prefix="tsa_dmg_", suffix=".db", delete=False).name

import test_client
test_client.HOST = "127.0.0.1"; test_client.PORT = PORT
from test_client import WS

RESULTS = []
def check(name, ok, note=""):
    RESULTS.append(bool(ok))
    print(f"  {'PASS' if ok else 'FAIL'} {name}{(' -- ' + str(note)) if note and not ok else ''}")

async def mk(name):
    c = await WS.connect()
    u = f"{name}_{secrets.token_hex(3)}"
    await c.send({"t": "register", "user": u, "pass": "secret9"})
    await c.recv_until(lambda x: x["t"] == "auth_ok", timeout=5)
    await c.recv_until(lambda x: x["t"] == "roster", timeout=3)
    await c.send({"t": "create_char", "slot": 0, "name": u[:14], "char": {}})
    po = await c.recv_until(lambda x: x["t"] == "play_ok", timeout=5)
    c.netid = (po or {}).get("netid")
    return c

async def boss_now(c, bid=None):
    """Latest snapshot record for the boss (or any boss if bid is None), after parking at the lair."""
    await c.send({"t": "input", "x": QUEEN_LAIR[0], "z": QUEEN_LAIR[1], "yaw": 0, "hp": 100})
    snap = await c.recv_until(
        lambda x: x["t"] == "snapshot" and any(m.get("boss") and (bid is None or m.get("id") == bid)
                                               for m in x.get("mobs", [])), timeout=6)
    if not snap:
        return None
    return next((m for m in snap.get("mobs", []) if m.get("boss") and (bid is None or m.get("id") == bid)), None)

async def strike(c, mob, dmg):
    """Teleport onto the mob's live position and claim a hit; return the mob_hit (or None)."""
    await c.send({"t": "input", "x": mob["x"], "z": mob["z"], "yaw": 0, "hp": 100})
    await asyncio.sleep(0.05)
    await c.send({"t": "attack", "id": mob["id"], "dmg": dmg})
    return await c.recv_until(lambda x: x["t"] == "mob_hit" and x.get("id") == mob["id"], timeout=1.5)

async def main():
    a = await mk("dmg")
    boss = await boss_now(a)
    check("world boss present at its lair", bool(boss) and boss.get("mhp", 0) >= 1000,
          f"boss={boss}")
    if not boss:
        return sum(1 for ok in RESULTS if not ok)
    bid, mhp = boss["id"], boss["mhp"]

    # ---- 1 & 2. a single 99999 hit is capped, not a one-shot ----------------------------------------
    hit = await strike(a, boss, 99999)
    check("huge claimed hit lands (mob_hit returned)", bool(hit), f"hit={hit}")
    if hit:
        # ---- 3. an immediate second hit is rejected (cooldown) — fire it NOW, before any blocking wait,
        # so it lands well inside ATTACK_CD of the hit we just got ------------------------------------
        await a.send({"t": "attack", "id": bid, "dmg": 99999})
        quick = await a.recv_until(lambda x: x["t"] == "mob_hit" and x.get("id") == bid, timeout=0.3)
        check("too-fast repeat hit on the same mob rejected (cooldown)", quick is None)

        applied = mhp - hit.get("hp", 0)
        check("single hit does NOT one-shot the boss (survives)", hit.get("hp", 0) > 0,
              f"hp after={hit.get('hp')} of {mhp}")
        check("applied damage capped at ~mhp*HIT_FRAC",
              applied <= mhp * HIT_FRAC + 2, f"applied={applied}, cap={mhp*HIT_FRAC:.0f}")
        died = await a.recv_until(lambda x: x["t"] == "mob_die" and x.get("id") == bid, timeout=0.4)
        check("no mob_die from the capped hit", died is None)

    # ---- 4. the boss is still killable with properly-spaced hits ------------------------------------
    died = None
    for i in range(8):
        await asyncio.sleep(ATTACK_CD + 0.06)               # respect the cooldown
        cur = await boss_now(a, bid) or boss
        await a.send({"t": "input", "x": cur["x"], "z": cur["z"], "yaw": 0, "hp": 100})
        await asyncio.sleep(0.05)
        await a.send({"t": "attack", "id": bid, "dmg": 99999})
        died = await a.recv_until(lambda x: x["t"] == "mob_die" and x.get("id") == bid, timeout=1.0)
        if died:
            break
    check("boss still killable with spaced hits (enforcement isn't invincibility)", bool(died),
          "no mob_die within 8 spaced hits")

    await a.close()
    p = sum(1 for ok in RESULTS if ok); f = len(RESULTS) - p
    print(f"\n{p} passed, {f} failed")
    return f

def run():
    env = dict(os.environ)
    env.update(DERETH_PORT=str(PORT), DERETH_DB=DB, DERETH_ENFORCE_COMBAT="1",
               DERETH_EVENT_CD="99999", DERETH_HOST="127.0.0.1")
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
