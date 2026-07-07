#!/usr/bin/env python3
"""TestSystemA persistence & lifecycle scenarios: deep save/load fidelity, slot
lifecycle, token resume, event start->end. Usage: python tsa_persist.py [host] [port]"""
import asyncio, json, secrets, sys, time
sys.path.insert(0, ".")
from test_client import WS

RESULTS = []
def check(name, ok, note=""):
    RESULTS.append((name, bool(ok)))
    print(f"  {'PASS' if ok else 'FAIL'} {name}{(' -- ' + str(note)) if note and not ok else ''}")

COMPLEX_CHAR = {
    "level": 42, "gold": 123456, "xp": 999999,
    "attr": {"Strength": 55, "Endurance": 60, "Coordination": 45, "Quickness": 40, "Focus": 70, "Self": 75},
    "skills": {"war": {"t": 2, "xp": 168758}, "meleed": {"t": 1, "xp": 5000}},
    "inv": [{"name": "Fine Shadow Atlan Staff", "stat": "weapon", "wt": "staff", "v": 8,
             "acspells": [1602, 1589], "spellcraft": 226, "itemMana": 380.5, "itemManaMax": 400,
             "reqArcane": 113},
            {"name": "Blue Aetheria of Growth", "stat": "aetheria", "color": "blue", "alvl": 3,
             "aset": "Growth", "surge": "Regeneration"}],
    "aetheria": {"blue": {"name": "Red Aetheria of Fury (level 2)", "color": "red", "alvl": 2,
                          "aset": "Fury", "surge": "Destruction"}, "yellow": None, "red": None},
    "academy": {"done": 1}, "knownSpells": ["war1", "life_heal_1"],
    "unicode": "Tëst Ünïcödé — 日本語 ♥",
    "nested": {"deep": {"deeper": [1, 2, {"three": 3.14159}]}},
}

async def mk(name, char=None):
    c = await WS.connect()
    u = f"{name}_{secrets.token_hex(3)}"
    await c.send({"t": "register", "user": u, "pass": "secret9"})
    auth = await c.recv_until(lambda x: x["t"] == "auth_ok", timeout=5)
    await c.recv_until(lambda x: x["t"] == "roster", timeout=3)
    await c.send({"t": "create_char", "slot": 0, "name": u[:14], "char": char or {}})
    await c.recv_until(lambda x: x["t"] == "play_ok", timeout=5)
    c.username, c.token = u, (auth or {}).get("token")
    return c

async def main():
    # ── deep save/load fidelity ──
    a = await mk("per", COMPLEX_CHAR)
    await a.send({"t": "save", "char": COMPLEX_CHAR})
    await asyncio.sleep(0.4)
    await a.close()
    b = await WS.connect()
    await b.send({"t": "login", "user": a.username, "pass": "secret9"})
    await b.recv_until(lambda x: x["t"] == "auth_ok", timeout=5)
    roster = await b.recv_until(lambda x: x["t"] == "roster", timeout=3)
    check("roster lists the character", roster and any(s for s in roster.get("chars", []) if s))
    await b.send({"t": "play_char", "slot": 0})
    pd = await b.recv_until(lambda x: x["t"] == "play_ok", timeout=5)
    got = (pd or {}).get("char") or {}
    check("play_ok returns the save blob", bool(got))
    check("deep-equal round trip", got == COMPLEX_CHAR,
          "diff keys: " + str([k for k in COMPLEX_CHAR if got.get(k) != COMPLEX_CHAR[k]][:5]))
    check("unicode survives", got.get("unicode") == COMPLEX_CHAR["unicode"])
    check("float itemMana survives", got.get("inv", [{}])[0].get("itemMana") == 380.5)

    # ── token resume ──
    tok = None
    await b.send({"t": "ping"})
    # token came from the login auth_ok — reconnect fresh and resume with it
    c2 = await WS.connect()
    await c2.send({"t": "login", "user": a.username, "pass": "secret9"})
    auth = await c2.recv_until(lambda x: x["t"] == "auth_ok", timeout=5)
    tok = (auth or {}).get("token")
    await c2.close()
    if tok:
        c3 = await WS.connect()
        await c3.send({"t": "resume", "token": tok})
        r = await c3.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"), timeout=4)
        check("token resume restores session", r and r["t"] == "auth_ok")
        await c3.close()
    else:
        check("token present in auth_ok", False)
    c4 = await WS.connect()
    await c4.send({"t": "resume", "token": "bogus-token-123"})
    r = await c4.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"), timeout=4)
    check("bogus resume token rejected", r and r["t"] == "auth_err")
    await c4.close()
    await b.close()

    # ── slot lifecycle: fill several, delete middle, recreate ──
    d = await mk("slots")
    okc = True
    for s in (1, 2, 3):
        await d.send({"t": "create_char", "slot": s, "name": f"alt{s}_{secrets.token_hex(2)}"[:14], "char": {"level": s}})
        r = await d.recv_until(lambda x: x["t"] in ("play_ok", "roster", "char_ok", "create_ok", "system"), timeout=4)
        okc = okc and bool(r)
    check("create 3 extra slots", okc)
    await d.send({"t": "delete_char", "slot": 2})
    await asyncio.sleep(0.3)
    await d.send({"t": "create_char", "slot": 2, "name": f"re2_{secrets.token_hex(2)}"[:14], "char": {"level": 99}})
    r = await d.recv_until(lambda x: x["t"] in ("play_ok", "roster", "char_ok", "create_ok"), timeout=4)
    check("deleted slot is reusable", r)
    await d.send({"t": "play_char", "slot": 2})
    pd2 = await d.recv_until(lambda x: x["t"] == "play_ok", timeout=4)
    check("recreated slot plays with new blob", pd2 and (pd2.get("char") or {}).get("level") == 99,
          f"char={pd2.get('char') if pd2 else None}")
    await d.close()

    # ── event lifecycle: with DERETH_EVENT_CD=2 an Incursion starts; kill its mobs -> event_end ──
    e = await mk("evt")
    ev = await e.recv_until(lambda x: x["t"] == "event_start", timeout=8)
    check("Incursion starts", ev and ev.get("count", 0) > 0)
    if ev:
        # snapshot mobs tagged to the event, walk to them, kill them all
        await e.send({"t": "input", "x": ev["x"], "z": ev["z"], "yaw": 0})
        await asyncio.sleep(0.6)
        killed = 0
        for i in range(1, 200):
            await e.send({"t": "attack", "id": f"m{i}", "dmg": 2000})
        deadline = time.time() + 12
        ended = None
        while time.time() < deadline and not ended:
            m = await e.recv_until(lambda x: x["t"] in ("event_end", "mob_die"), timeout=3)
            if m and m["t"] == "event_end":
                ended = m
            elif m is None:
                # sweep again — respawns/other ids
                for i in range(1, 200):
                    await e.send({"t": "attack", "id": f"m{i}", "dmg": 2000})
        check("event ends after clearing mobs", ended, "no event_end within window")
        if ended:
            check("event_end reports success", ended.get("success") in (True, 1))
    await e.close()

    p = sum(1 for _, ok in RESULTS if ok); f = len(RESULTS) - p
    print(f"\n{p} passed, {f} failed")

asyncio.run(main())
