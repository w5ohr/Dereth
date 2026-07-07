#!/usr/bin/env python3
"""TestSystemA fellowship/allegiance wire scenarios: party XP level-spread splits,
party chat, muster cap, monarch MOTD. Usage: python tsa_fellow.py [host] [port]"""
import asyncio, secrets, sys
sys.path.insert(0, ".")
from test_client import WS

RESULTS = []
def check(name, ok, note=""):
    RESULTS.append((name, bool(ok)))
    print(f"  {'PASS' if ok else 'FAIL'} {name}{(' -- ' + str(note)) if note and not ok else ''}")

async def mk(name, level):
    c = await WS.connect()
    u = f"{name}_{secrets.token_hex(3)}"
    await c.send({"t": "register", "user": u, "pass": "secret9"})
    await c.recv_until(lambda x: x["t"] == "auth_ok")
    await c.recv_until(lambda x: x["t"] == "roster")
    await c.send({"t": "create_char", "slot": 0, "name": u[:14], "char": {"level": level}})
    await c.recv_until(lambda x: x["t"] == "play_ok")
    c.username, c.charname, c.level = u, u[:14], level
    # level rides the input tick server-side
    await c.send({"t": "input", "x": 60, "z": 60, "yaw": 0, "level": level})
    return c

async def party_up(leader, member):
    await leader.send({"t": "party", "act": "invite", "name": member.charname})
    await member.recv_until(lambda x: "invites you" in x.get("msg", ""), timeout=3)
    await member.send({"t": "party", "act": "accept"})
    return await leader.recv_until(lambda x: "joined the party" in x.get("msg", ""), timeout=3)

async def kill_shared_mob(killer, others):
    """find a mob near the party via snapshot, kill it, collect xp rewards for everyone"""
    snap = await killer.recv_until(lambda x: x["t"] == "snapshot" and x.get("mobs"), timeout=8)
    mobs = (snap or {}).get("mobs", [])
    if not mobs:
        return None
    m = mobs[0]
    await killer.send({"t": "input", "x": m["x"], "z": m["z"], "yaw": 0, "level": killer.level})
    for c in others:
        await c.send({"t": "input", "x": m["x"] + 1, "z": m["z"], "yaw": 0, "level": c.level})
    await asyncio.sleep(0.3)
    for _ in range(6):
        await killer.send({"t": "attack", "id": m["id"], "dmg": 3000})
    rewards = {}
    for c in [killer] + others:
        r = await c.recv_until(lambda x: x["t"] in ("reward", "xp"), timeout=4)
        rewards[c.charname] = (r or {}).get("xp")
    return rewards

async def main():
    # tight band: 20/22 → equal split with size bonus (both should get the same)
    a = await mk("fa", 20); b = await mk("fb", 22)
    check("party invite/accept", await party_up(a, b))
    await a.send({"t": "pchat", "msg": "fellowship!"})
    pc = await b.recv_until(lambda x: x.get("channel") == "party" or
                            (x["t"] == "chat" and "fellowship" in x.get("msg", "")), timeout=3)
    check("party chat reaches member", pc)
    r = await kill_shared_mob(a, [b])
    check("both fellows receive XP", r and all(v for v in r.values()), r)
    if r and all(r.values()):
        vals = list(r.values())
        check("tight level band splits equally", vals[0] == vals[1], r)

    # wide band: add a level-80 → proportional; low-level must get LESS than high-level
    c = await mk("fc", 80)
    await party_up(a, c)
    r2 = await kill_shared_mob(c, [a, b])
    check("3-way fellowship XP delivered", r2 and all(v for v in r2.values()), r2)
    if r2 and all(r2.values()):
        hi = r2[c.charname]; lo = min(r2[a.charname], r2[b.charname])
        check("wide spread pays high level more", hi > lo, r2)

    # muster: level 5 refused; level 80 grants an NPC vassal
    low = await mk("mu", 5)
    await low.send({"t": "muster"})
    ref = await low.recv_until(lambda x: "renown" in x.get("msg", "").lower(), timeout=3)
    check("muster refused below level 12", ref)
    await c.send({"t": "muster"})
    got = await c.recv_until(lambda x: x["t"] == "system" and
                             ("swear" in x.get("msg", "").lower() or "banner" in x.get("msg", "").lower()
                              or "answers" in x.get("msg", "").lower()), timeout=3)
    check("muster grants an NPC vassal at level 80", got, )

    # monarch MOTD: b swears to a, a (monarch) sets MOTD, b receives it
    await b.send({"t": "party", "act": "leave"})
    await b.send({"t": "swear", "name": a.charname})
    await b.recv_until(lambda x: "swear" in x.get("msg", "").lower(), timeout=3)
    await a.send({"t": "alg_motd", "text": "TSA banner day"})
    motd = await b.recv_until(lambda x: "TSA banner day" in x.get("msg", ""), timeout=3)
    check("monarch MOTD pushed to sworn vassal", motd)

    for cl in (a, b, c, low):
        await cl.close()
    p = sum(1 for _, ok in RESULTS if ok); f = len(RESULTS) - p
    print(f"\n{p} passed, {f} failed")

asyncio.run(main())
