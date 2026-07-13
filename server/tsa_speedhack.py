#!/usr/bin/env python3
"""#S3 movement-guard regression. Spins up its OWN dereth_server with DERETH_ENFORCE_MOVE=1 on a
throwaway port+DB and asserts, via a stationary observer's AOI snapshots:
  - out-of-bounds coords are clamped to +/-WORLD_LIMIT (always on),
  - SUSTAINED supra-speed movement is snapped back to the last good position (enforce on),
  - a SINGLE teleport spike (portal/recall) is allowed through (no false-positive),
  - normal sub-threshold movement is never policed.
The enforce snap-back is gated OFF in production by default; this test proves the mechanism when enabled.
Self-contained: no external server needed. Usage: python3 tsa_speedhack.py"""
import asyncio, os, secrets, socket, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

def _free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

PORT = _free_port()
DB = tempfile.NamedTemporaryFile(prefix="tsa_speedhack_", suffix=".db", delete=False).name

import test_client                     # WS helper reads its module-global HOST/PORT at call time
test_client.HOST = "127.0.0.1"; test_client.PORT = PORT
from test_client import WS

WORLD_LIMIT = 7000 * 3                 # mirrors dereth_server WORLD_LIMIT (WSCALE=3)
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

async def observe_x(observer, target_netid, sends, obs_pos, ticks=12):
    """Drive `sends` (a list of (x,z) the target reports) one per ~tick while the observer holds still at
    obs_pos, and return the list of the target's x as the observer's snapshots report it."""
    ox, oz = obs_pos
    seen = []
    for i in range(max(len(sends), ticks)):
        if i < len(sends):
            await sends[i][0].send({"t": "input", "x": sends[i][1], "z": sends[i][2], "yaw": 0, "hp": 100})
        await observer.send({"t": "input", "x": ox, "z": oz, "yaw": 0, "hp": 100})
        snap = await observer.recv_until(lambda m: m["t"] == "snapshot", timeout=1.5)
        if snap:
            p = next((pp for pp in snap.get("players", []) if pp.get("id") == target_netid), None)
            if p is not None:
                seen.append(p.get("x"))
        await asyncio.sleep(0.02)
    return seen

async def main():
    # A stationary observer whose AOI snapshots reveal a mover's authoritative position. Each scenario
    # uses a FRESH mover: the enforce snap-back is deliberately sticky (that's what contains a sustained
    # hack), so a flagged mover stays pinned — a new client gives each scenario a clean baseline.
    obs = await mk("obs")
    movers = []

    async def fresh():
        m = await mk("hack"); movers.append(m); return m

    # ---- 1. normal sub-threshold movement is NOT policed --------------------------------------------
    # step +10 units/tick (~100 u/s < 150 ceiling) away from the observer, staying inside AOI (300)
    h1 = await fresh()
    sends = [(h1, 10 * (i + 1), 0) for i in range(8)]      # x = 10,20,...,80
    xs = await observe_x(obs, h1.netid, sends, (0, 0))
    check("normal movement reaches its destination (not policed)",
          xs and max(x for x in xs if x is not None) >= 60, f"observed xs={xs}")

    # ---- 2. SUSTAINED supra-speed is snapped back ---------------------------------------------------
    # step +40 units/tick (~400 u/s > 150 ceiling) EVERY tick -> after MOVE_STRIKES(3) the position
    # freezes at the last-good spot instead of climbing to the sent 400.
    h2 = await fresh()
    sends = [(h2, 40 * (i + 1), 0) for i in range(10)]     # x = 40,80,...,400
    xs = await observe_x(obs, h2.netid, sends, (0, 0))
    maxx = max((x for x in xs if x is not None), default=None)
    check("sustained supra-speed is snapped back (position frozen well below sent 400)",
          maxx is not None and maxx <= 160, f"observed max x={maxx}, xs={xs}")

    # ---- 3. a SINGLE teleport spike (portal/recall) is allowed --------------------------------------
    # one 200-unit jump (a spike), then normal small steps: the jump must land (not snapped), and the
    # target stays inside the observer's AOI (200 < 300).
    h3 = await fresh()
    sends = [(h3, 200, 0)] + [(h3, 200 + 5 * i, 0) for i in range(1, 6)]
    xs = await observe_x(obs, h3.netid, sends, (0, 0))
    check("a single teleport spike is allowed (reaches ~200)",
          xs and max(x for x in xs if x is not None) >= 195, f"observed xs={xs}")

    # ---- 4. out-of-bounds is clamped to the playfield ----------------------------------------------
    # both stand at the far corner; the mover tries to leave the world. If clamped it stays at the
    # boundary (distance 0 from the observer -> still in AOI); un-clamped it would fly 9000u out of view.
    h4 = await fresh()
    # establish the fresh mover in the observer's AOI at the boundary (its first input is always allowed)
    seen0 = await observe_x(obs, h4.netid, [(h4, WORLD_LIMIT, 0)], (WORLD_LIMIT, 0), ticks=4)
    # now try to breach the boundary (single input, clamped to the edge)
    seen1 = await observe_x(obs, h4.netid, [(h4, WORLD_LIMIT + 9000, 0)], (WORLD_LIMIT, 0), ticks=4)
    clamped = seen1 and all((x is not None and x <= WORLD_LIMIT + 1) for x in seen1)
    check("out-of-bounds coord clamped to WORLD_LIMIT (mover stays visible at the edge)",
          bool(seen0) and bool(clamped), f"pre={seen0}, post={seen1}")

    for c in [obs] + movers:
        await c.close()

    p = sum(1 for ok in RESULTS if ok); f = len(RESULTS) - p
    print(f"\n{p} passed, {f} failed")
    return f

def run():
    env = dict(os.environ)
    env.update(DERETH_PORT=str(PORT), DERETH_DB=DB, DERETH_ENFORCE_MOVE="1",
               DERETH_EVENT_CD="99999", DERETH_HOST="127.0.0.1")
    env.pop("DERETH_ADMIN_PW", None)
    proc = subprocess.Popen([sys.executable, os.path.join(HERE, "dereth_server.py")],
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        # wait for the port to accept
        for _ in range(100):
            try:
                s = socket.create_connection(("127.0.0.1", PORT), timeout=0.3); s.close(); break
            except OSError:
                time.sleep(0.1)
        else:
            print("server did not start"); return 1
        failed = asyncio.run(main())
        return failed
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
