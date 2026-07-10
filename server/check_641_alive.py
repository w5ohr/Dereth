"""#641 end-to-end: the server's snapshot must carry an authoritative `alive` for the active event.

Run a server with a short event cooldown, join, wait for the Incursion, and read the snapshot.

    DERETH_DB=/tmp/t.db DERETH_PORT=8799 DERETH_EVENT_CD=2 python server/dereth_server.py &
    python server/check_641_alive.py 127.0.0.1 8799
"""
import asyncio, sys, secrets
sys.path.insert(0, ".")
# test_client parses HOST/PORT from OUR sys.argv, so `python check_641_alive.py 127.0.0.1 8799` works.
from test_client import WS, HOST, PORT


async def main():
    uniq = secrets.token_hex(3)
    c = await WS.connect()
    await c.send({"t": "register", "user": f"e641_{uniq}", "pass": "secret"})
    await c.recv_until(lambda x: x["t"] == "auth_ok")
    await c.recv_until(lambda x: x["t"] == "roster")
    await c.send({"t": "create_char", "slot": 0, "name": f"e641{uniq}", "char": {}})
    await c.recv_until(lambda x: x["t"] == "play_ok")

    ev = await c.recv_until(lambda x: x["t"] == "event_start", timeout=15.0)
    if not ev:
        print("FAIL  no event_start (is DERETH_EVENT_CD short?)"); return 1
    print(f"  event_start: {ev.get('name')} count={ev.get('count')}")

    snap = await c.recv_until(lambda x: x["t"] == "snapshot" and x.get("event"), timeout=10.0)
    if not snap:
        print("FAIL  no snapshot carrying an event"); return 1
    e = snap["event"]
    print(f"  snapshot event payload: {e}")

    ok = True
    if "alive" not in e:
        print("FAIL  snapshot event has no 'alive' field"); ok = False
    else:
        print(f"PASS  snapshot carries alive={e['alive']}")
        if e["alive"] != e.get("total"):
            print(f"FAIL  fresh event should have alive == total ({e['alive']} vs {e.get('total')})"); ok = False
        else:
            print(f"PASS  fresh event: alive == total == {e['alive']} (no phantom kills)")
    await c.close()
    return 0 if ok else 1


sys.exit(asyncio.run(main()))
