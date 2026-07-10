"""#646 end-to-end: the `inst` flag must reach peers, and an instanced player must be untargetable.

Two clients. `dweller` reports inst:1 (it is inside a dungeon; its coords are the entrance's return
point). `camper` stands at those coords and tries to PK it.

    DERETH_DB=/tmp/t.db DERETH_PORT=8799 python server/dereth_server.py &
    python server/check_646_inst.py 127.0.0.1 8799
"""
import asyncio, sys, secrets
sys.path.insert(0, ".")
from test_client import WS, HOST, PORT

FAILED = []


def check(name, ok, note=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{(' -- ' + str(note)) if note else ''}")
    if not ok:
        FAILED.append(name)


async def latest_snapshot(c, seconds=1.5):
    """The MOST RECENT snapshot, not the next one off the socket.

    The server streams snapshots at 10Hz and they queue in the client's receive buffer, so
    `recv_until(t == "snapshot")` hands you the OLDEST one -- state from seconds ago. Read for a fixed
    window and keep the last. (Reading "until idle" never terminates: the stream never goes idle.)
    """
    import time as _t
    end = _t.time() + seconds
    last = None
    while _t.time() < end:
        try:
            msg = await asyncio.wait_for(c._frame(), timeout=max(0.01, end - _t.time()))
        except asyncio.TimeoutError:
            break
        if msg.get("t") == "snapshot" and msg.get("players"):
            last = msg
    return last


async def join(tag, uniq):
    c = await WS.connect()
    await c.send({"t": "register", "user": f"{tag}_{uniq}", "pass": "secret"})
    await c.recv_until(lambda x: x["t"] == "auth_ok")
    await c.recv_until(lambda x: x["t"] == "roster")
    await c.send({"t": "create_char", "slot": 0, "name": f"{tag}{uniq}", "char": {}})
    ok = await c.recv_until(lambda x: x["t"] == "play_ok")
    return c, ok


async def main():
    uniq = secrets.token_hex(3)
    dweller, _ = await join("dweller", uniq)
    camper, _ = await join("camper", uniq)

    ENTR = (7920.0, -11460.0)   # a dungeon return point

    # BOTH are PK, so a refused strike can only be the `inst` flag -- not a ruleset mismatch.
    # PK state rides the input tick (see tsa_extended.py).
    for _ in range(6):
        # dweller is INSIDE an instance: it reports the entrance coords + inst:1 (what the client sends)
        await dweller.send({"t": "input", "x": ENTR[0], "z": ENTR[1], "inst": 1, "yaw": 0,
                            "hp": 27, "mhp": 27, "level": 1, "pkState": "pk"})
        # camper stands at the same spot, in the overworld
        await camper.send({"t": "input", "x": ENTR[0], "z": ENTR[1], "inst": 0, "yaw": 0,
                           "hp": 100, "mhp": 100, "level": 10, "pkState": "pk"})
        await asyncio.sleep(0.12)

    snap = await latest_snapshot(camper)
    if not snap:
        check("camper receives a snapshot with players", False, "none arrived")
        return 1

    rec = next((p for p in snap["players"] if p.get("name", "").startswith("dweller")), None)
    check("peer record for the instanced player exists", rec is not None, rec)
    if rec:
        check("record carries inst=1 on the wire", rec.get("inst") == 1, f"inst={rec.get('inst')!r}")

    self_rec = next((p for p in snap["players"] if p.get("name", "").startswith("camper")), None)
    if self_rec:
        check("an overworld player carries no inst field", "inst" not in self_rec, self_rec.get("inst"))

    # camper (PK) tries to strike the phantom. The server must refuse to relay.
    tgt = rec.get("id") if rec else None
    if tgt:
        await camper.send({"t": "pvp", "target": tgt, "dmg": 50, "element": ""})
        hit = await dweller.recv_until(lambda x: x["t"] == "pvp", timeout=2.0)
        check("PK strike at the phantom is NOT relayed", hit is None, f"got {hit}")

    # the dweller must never be picked as a mob's nearest player: no unsolicited `dmg` while instanced
    dmg = await dweller.recv_until(lambda x: x["t"] == "dmg", timeout=3.0)
    check("no mob damage reaches a player inside an instance", dmg is None, f"got {dmg}")

    # POSITIVE CONTROL: the dweller steps OUT of the instance. Same two players, same coords, same
    # ruleset -- only `inst` changed. The strike must now land, or the refusal above proves nothing.
    for _ in range(6):
        await dweller.send({"t": "input", "x": ENTR[0], "z": ENTR[1], "inst": 0, "yaw": 0,
                            "hp": 27, "mhp": 27, "level": 1, "pkState": "pk"})
        await camper.send({"t": "input", "x": ENTR[0], "z": ENTR[1], "inst": 0, "yaw": 0,
                           "hp": 100, "mhp": 100, "level": 10, "pkState": "pk"})
        await asyncio.sleep(0.12)
    # Snapshots are queued: the first one we can read may predate the dweller's inst:0 input. Poll for
    # the FRESH state instead of trusting the next message off the socket.
    snap2 = await latest_snapshot(camper)
    rec2 = next((p for p in (snap2 or {}).get("players", []) if p.get("name", "").startswith("dweller")), None)
    check("peer loses the inst field once out of the instance", rec2 is not None and "inst" not in rec2,
          rec2.get("inst") if rec2 else "no record")
    if rec2:
        await camper.send({"t": "pvp", "target": rec2["id"], "dmg": 50, "element": ""})
        hit2 = await dweller.recv_until(lambda x: x["t"] == "pvp", timeout=3.0)
        check("CONTROL: the same strike DOES land once out of the instance", hit2 is not None,
              "no pvp relayed -- the refusal above may not be about `inst` at all" if hit2 is None else "")

    await dweller.close(); await camper.close()
    print(f"\n{'FAILED: ' + ', '.join(FAILED) if FAILED else 'all checks passed'}")
    return 1 if FAILED else 0


sys.exit(asyncio.run(main()))
