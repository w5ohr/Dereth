#!/usr/bin/env python3
"""TestSystemA extended server scenarios (log-only harness; stdlib).
Protocol-corrected against dereth_server.py: secure trade w/ coin, 3-state PK
gating, allegiance swear/achat, corpse owner-gating.
Usage: python tsa_extended.py [host] [port]  (server already running)
"""
import asyncio, json, secrets, sys, time
sys.path.insert(0, ".")
from test_client import WS, HOST, PORT

RESULTS = []
def check(name, ok, note=""):
    RESULTS.append((name, bool(ok), note))
    print(f"  {'PASS' if ok else 'FAIL'} {name}{(' -- ' + str(note)) if note and not ok else ''}")

async def mk(name, level=10, inv=None):
    c = await WS.connect()
    u = f"{name}_{secrets.token_hex(3)}"
    await c.send({"t": "register", "user": u, "pass": "secret9"})
    await c.recv_until(lambda x: x["t"] == "auth_ok")
    await c.recv_until(lambda x: x["t"] == "roster")
    char = {"level": level, "gold": 1000}
    if inv:
        char["inv"] = inv   # adopted by enter_world -> load_econ as the char's authoritative inventory
    await c.send({"t": "create_char", "slot": 0, "name": u[:14], "char": char})
    po = await c.recv_until(lambda x: x["t"] == "play_ok")
    c.username, c.charname = u, u[:14]
    c.netid = po.get("netid") if po else None   # #438: opaque session id -- the wire never uses the account name as a target
    return c

async def main():
    # #238: `a` needs REAL server-side inventory items -- handle_trade's take_owned() and death's
    # take_owned_filter() both validate against the authoritative inventory (adopted from the save at
    # enter_world) and drop anything fabricated. TSA_GEM is spent in the trade block below; TSA_DROP
    # survives to be dropped on the corpse further down, so that check exercises a real recovered item.
    TSA_GEM = {"name": "TSA Gem", "stat": "trophy", "v": 50}
    TSA_DROP = {"name": "TSA Drop", "stat": "trophy", "v": 9}
    a = await mk("tsaA", inv=[TSA_GEM, TSA_DROP]); b = await mk("tsaB")
    # stand together; PK states ride the input tick (pkState field)
    await a.send({"t": "input", "x": 100, "z": 100, "yaw": 0, "pkState": "pk"})
    await b.send({"t": "input", "x": 102, "z": 100, "yaw": 0, "pkState": "npk"})
    await asyncio.sleep(0.4)

    # ── secure trade with coin (open → accept_open → add/coin → ok+ok → done) ──
    await a.send({"t": "trade", "act": "open", "name": b.charname})
    inv = await b.recv_until(lambda x: x["t"] == "trade" and x.get("act") == "invite", timeout=3)
    check("trade invite delivered", inv)
    await b.send({"t": "trade", "act": "accept_open"})
    opened = await a.recv_until(lambda x: x["t"] == "trade" and x.get("act") == "sync", timeout=3)
    check("trade window syncs after accept_open", opened)
    await a.send({"t": "trade", "act": "add", "item": TSA_GEM})
    sync1 = await b.recv_until(lambda x: x["t"] == "trade" and x.get("act") == "sync"
                               and any((i or {}).get("name") == "TSA Gem" for i in (x.get("them") or [])), timeout=3)
    check("item offer syncs to partner", sync1)
    await b.send({"t": "trade", "act": "coin", "amount": 250})
    sync2 = await a.recv_until(lambda x: x["t"] == "trade" and x.get("act") == "sync", timeout=3)
    check("coin offer syncs to partner", sync2)
    await a.send({"t": "trade", "act": "ok"})
    await b.send({"t": "trade", "act": "ok"})
    doneA = await a.recv_until(lambda x: x["t"] == "trade" and x.get("act") == "done", timeout=3)
    doneB = await b.recv_until(lambda x: x["t"] == "trade" and x.get("act") == "done", timeout=3)
    check("trade completes both sides", doneA and doneB)
    check("coin lands with item-giver", bool(doneA) and doneA.get("coin", 0) == 250,
          f"got {doneA.get('coin') if doneA else None}")
    check("item lands with coin-giver", bool(doneB) and any(
        (i or {}).get("name") == "TSA Gem" for i in (doneB.get("give") or [])),
        f"give={doneB.get('give') if doneB else None}")

    # ── 3-state PK gating (#438: pvp hit targets the opaque netid, not the account name) ──
    await a.send({"t": "pvp", "target": b.netid, "dmg": 10})
    hit = await b.recv_until(lambda x: x["t"] == "pvp", timeout=2)
    check("PK->NPK hit does NOT land", hit is None, f"got {hit}")
    await b.send({"t": "input", "x": 102, "z": 100, "yaw": 0, "pkState": "pk"})
    await asyncio.sleep(0.4)
    await a.send({"t": "pvp", "target": b.netid, "dmg": 10})
    hit2 = await b.recv_until(lambda x: x["t"] == "pvp", timeout=2)
    check("PK->PK hit lands", hit2 and hit2.get("dmg") == 10)
    # PKL vs PK must NOT mix (AC rulesets fight their own)
    await b.send({"t": "input", "x": 102, "z": 100, "yaw": 0, "pkState": "pkl"})
    await asyncio.sleep(0.4)
    await a.send({"t": "pvp", "target": b.netid, "dmg": 10})
    hit3 = await b.recv_until(lambda x: x["t"] == "pvp", timeout=2)
    check("PK->PKL hit does NOT land", hit3 is None, f"got {hit3}")

    # ── allegiance: swear (field name=charname) + achat + alg_info ──
    await b.send({"t": "swear", "name": a.charname})
    sw = await b.recv_until(lambda x: x["t"] == "system" and "swear" in x.get("msg", "").lower(), timeout=3)
    check("swear acknowledged", sw)
    await a.send({"t": "alg_info"})
    info = await a.recv_until(lambda x: x["t"] in ("alg_info", "allegiance") or "vassals" in x, timeout=3)
    check("alg_info returns for patron", info)
    await b.send({"t": "achat", "msg": "hail patron"})
    line = await a.recv_until(lambda x: x.get("channel") == "allegiance" and "hail patron" in x.get("msg", ""), timeout=3)
    check("allegiance chat reaches patron", line)

    # ── corpse owner-gating ──
    # `a` is still flagged pkState:"pk" from the PvP block above; a PK death's corpse is created
    # open:true (AC rule: PK death is free loot for anyone), which would make B's non-owner recovery
    # succeed and A's later owner recovery find nothing -- both by design, not a bug. Reset `a` to
    # npk first so this corpse is ownership-gated like a normal (non-PK) death.
    await a.send({"t": "input", "x": 100, "z": 100, "yaw": 0, "pkState": "npk"})
    await asyncio.sleep(0.4)
    await a.send({"t": "death", "x": 100, "z": 100, "gold": 5, "items": [TSA_DROP]})
    corpse = await b.recv_until(lambda x: x.get("type") == "corpse" or
                                (x["t"] in ("drop", "drops") and "corpse" in json.dumps(x)), timeout=3)
    check("shared corpse broadcast to others", corpse)
    cid = None
    if corpse:
        cid = corpse.get("id") or next((d.get("id") for d in corpse.get("drops", [])
                                        if isinstance(d, dict) and d.get("type") == "corpse"), None)
    if cid:
        await b.send({"t": "recover", "id": cid})
        deny = await b.recv_until(lambda x: x["t"] in ("corpse_loot", "system"), timeout=2)
        check("non-owner recovery rejected", not (deny and deny.get("t") == "corpse_loot"), f"got {deny}")
        await a.send({"t": "recover", "id": cid})
        grant = await a.recv_until(lambda x: x["t"] == "corpse_loot", timeout=3)
        check("owner recovery grants bundle", grant and (grant.get("items") or grant.get("amt")),
              f"got {grant}")
    else:
        check("corpse id resolvable", False, f"corpse msg={corpse}")

    await a.close(); await b.close()
    p = sum(1 for _, ok, _ in RESULTS if ok); f = len(RESULTS) - p
    print(f"\n{p} passed, {f} failed")

asyncio.run(main())
