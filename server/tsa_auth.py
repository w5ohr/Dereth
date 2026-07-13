#!/usr/bin/env python3
"""#S7 auth-surface hardening regression. Spins up its own server with a small per-IP auth budget, a
short window, and fast backup/maintenance, and asserts:
  - a per-IP flood of auth attempts is throttled ("Too many attempts") independent of username,
  - an over-long / garbage login name is rejected safely (no crash, auth_err),
  - the per-IP throttle is a WINDOW, not a permanent lock — a valid register succeeds after it resets,
  - the periodic DB snapshot (<db>.bak) is produced.
Self-contained. Usage: python3 tsa_auth.py"""
import asyncio, os, secrets, socket, sqlite3, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

def _free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

PORT = _free_port()
DB = tempfile.NamedTemporaryFile(prefix="tsa_auth_", suffix=".db", delete=False).name
IP_MAX = 8
IP_WINDOW = 2.0

import test_client
test_client.HOST = "127.0.0.1"; test_client.PORT = PORT
from test_client import WS

RESULTS = []
def check(name, ok, note=""):
    RESULTS.append(bool(ok))
    print(f"  {'PASS' if ok else 'FAIL'} {name}{(' -- ' + str(note)) if note and not ok else ''}")

async def main():
    # ---- 1. per-IP auth throttle -------------------------------------------------------------------
    # one connection (one IP), many login attempts with DISTINCT valid-format nonexistent names, so the
    # per-USERNAME lockout never trips — only the per-IP cap can. After IP_MAX attempts the IP is throttled.
    c = await WS.connect()
    msgs = []
    for i in range(IP_MAX + 5):
        await c.send({"t": "login", "user": f"nobody{i}_{secrets.token_hex(2)}", "pass": "whatever"})
        r = await c.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"), timeout=3)
        msgs.append((r or {}).get("msg", ""))
    throttled = [m for m in msgs if "Too many" in m]
    check("per-IP auth flood eventually throttled", len(throttled) >= 3,
          f"messages={msgs}")
    check("first attempts are NOT throttled (only after the cap)", "Too many" not in msgs[0],
          f"first={msgs[0]}")

    # ---- 2. over-long / garbage login name is handled safely ---------------------------------------
    # (still within the throttle window — a throttled reply is also fine here; the point is no crash and
    #  never auth_ok for a bogus name.)
    await c.send({"t": "login", "user": "x" * 300, "pass": "p"})
    r = await c.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"), timeout=3)
    check("over-long login name rejected safely (auth_err, never auth_ok)", bool(r) and r["t"] == "auth_err",
          f"resp={r}")
    await c.close()

    # ---- 3. the throttle is a WINDOW, not a permanent lock -----------------------------------------
    await asyncio.sleep(IP_WINDOW + 0.6)   # let the per-IP window lapse (+ the maintenance sweep prune)
    c2 = await WS.connect()
    u = f"legit_{secrets.token_hex(3)}"
    await c2.send({"t": "register", "user": u, "pass": "secret9"})
    r = await c2.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"), timeout=4)
    check("valid register succeeds after the throttle window resets", bool(r) and r["t"] == "auth_ok",
          f"resp={r}")

    # ---- 4. periodic DB snapshot produced ----------------------------------------------------------
    # the maintenance timer only advances while a client is connected (world_step runs), so keep c2
    # open and poll while it ticks.
    bak = DB + ".bak"
    ok_bak = False
    for _ in range(40):
        await c2.send({"t": "ping"}); await c2.recv_until(lambda x: x["t"] == "pong", timeout=1)
        if os.path.exists(bak):
            try:
                bc = sqlite3.connect(bak); bc.execute("SELECT 1 FROM users LIMIT 1"); bc.close()
                ok_bak = True; break
            except Exception:
                pass
        await asyncio.sleep(0.2)
    check("periodic DB snapshot (<db>.bak) produced and readable", ok_bak, f"expected {bak}")
    await c2.close()

    p = sum(1 for ok in RESULTS if ok); f = len(RESULTS) - p
    print(f"\n{p} passed, {f} failed")
    return f

def run():
    env = dict(os.environ)
    env.update(DERETH_PORT=str(PORT), DERETH_DB=DB, DERETH_EVENT_CD="99999", DERETH_HOST="127.0.0.1",
               DERETH_AUTH_IP_MAX=str(IP_MAX), DERETH_AUTH_IP_WINDOW=str(IP_WINDOW),
               DERETH_BACKUP_EVERY="1", DERETH_MAINT_TICK="0.3")
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
        for ext in ("", ".bak", ".bak.tmp"):
            try:
                os.unlink(DB + ext)
            except OSError:
                pass

if __name__ == "__main__":
    sys.exit(1 if run() else 0)
