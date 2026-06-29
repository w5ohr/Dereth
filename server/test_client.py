#!/usr/bin/env python3
"""End-to-end test harness for dereth_server.py (stdlib only).
Spins up two WebSocket clients and asserts auth, persistence, chat, and presence.
Usage: python3 server/test_client.py [host] [port]   (server must already be running)
"""
import asyncio, base64, hashlib, json, os, secrets, struct, sys, time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8787
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

class WS:
    def __init__(self, reader, writer):
        self.r = reader; self.w = writer
    @classmethod
    async def connect(cls):
        r, w = await asyncio.open_connection(HOST, PORT)
        key = base64.b64encode(secrets.token_bytes(16)).decode()
        w.write((f"GET / HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nUpgrade: websocket\r\n"
                 f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                 "Sec-WebSocket-Version: 13\r\n\r\n").encode())
        await w.drain()
        # read until end of headers
        while True:
            line = await r.readline()
            if line in (b"\r\n", b"\n", b""):
                break
        return cls(r, w)
    async def send(self, obj):
        payload = json.dumps(obj).encode()
        mask = secrets.token_bytes(4)
        masked = bytes(b ^ mask[i & 3] for i, b in enumerate(payload))
        n = len(payload); out = bytearray([0x81])
        if n < 126: out.append(0x80 | n)
        elif n < (1 << 16): out.append(0x80 | 126); out += struct.pack(">H", n)
        else: out.append(0x80 | 127); out += struct.pack(">Q", n)
        out += mask + masked
        self.w.write(bytes(out)); await self.w.drain()
    async def _frame(self):
        hdr = await self.r.readexactly(2)
        ln = hdr[1] & 0x7F
        if ln == 126: ln = struct.unpack(">H", await self.r.readexactly(2))[0]
        elif ln == 127: ln = struct.unpack(">Q", await self.r.readexactly(8))[0]
        return json.loads((await self.r.readexactly(ln)).decode())
    async def recv_until(self, pred, timeout=3.0):
        """Return the first message matching pred(msg); ignores snapshots/others."""
        end = time.time() + timeout
        while time.time() < end:
            try:
                msg = await asyncio.wait_for(self._frame(), timeout=end - time.time())
            except asyncio.TimeoutError:
                return None
            if pred(msg):
                return msg
        return None
    async def close(self):
        self.w.close()

PASS = 0; FAIL = 0
def check(name, ok):
    global PASS, FAIL
    print(("  PASS " if ok else "  FAIL ") + name)
    if ok: PASS += 1
    else: FAIL += 1

async def main():
    uniq = secrets.token_hex(3)
    alice, bob = f"alice_{uniq}", f"bob_{uniq}"
    print(f"Testing {HOST}:{PORT} with {alice}/{bob}")

    a = await WS.connect()
    await a.send({"t": "register", "user": alice, "pass": "secret1"})
    m = await a.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"))
    check("register alice -> auth_ok", m and m["t"] == "auth_ok")
    check("auth_ok includes session token", bool(m and m.get("token")))
    ros = await a.recv_until(lambda x: x["t"] == "roster")
    check("new account roster empty (max 8)", bool(ros) and ros.get("chars") == [] and ros.get("max") == 8)

    # create 8 characters (slots 0..7); slot 0's create enters the world
    for i in range(8):
        await a.send({"t": "create_char", "slot": i, "name": f"{alice}{i}", "char": {"level": i + 1, "kills": i, "heritage": "sho"}})
        po = await a.recv_until(lambda x: x["t"] in ("play_ok", "play_err"))
        if i == 0:
            check("create_char -> play_ok (enters world)", bool(po) and po["t"] == "play_ok" and po.get("char", {}).get("level") == 1)
    check("created all 8 characters", True)
    await a.send({"t": "create_char", "slot": 0, "name": f"{alice}d", "char": {}})
    e1 = await a.recv_until(lambda x: x["t"] == "play_err")
    check("create into occupied slot rejected", bool(e1) and "occupied" in e1.get("msg", "").lower())
    await a.send({"t": "create_char", "slot": 8, "name": f"{alice}x", "char": {}})
    e2 = await a.recv_until(lambda x: x["t"] == "play_err")
    check("9th slot (index 8) rejected", bool(e2) and "slot" in e2.get("msg", "").lower())
    await a.send({"t": "ping"})
    check("ping -> pong", bool(await a.recv_until(lambda x: x["t"] == "pong")))

    # second player joins, creates+plays a character -> alice sees the join
    b = await WS.connect()
    await b.send({"t": "register", "user": bob, "pass": "secret2"})
    mb = await b.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"))
    check("register bob -> auth_ok", mb and mb["t"] == "auth_ok")
    await b.recv_until(lambda x: x["t"] == "roster")
    await b.send({"t": "create_char", "slot": 0, "name": f"{bob}0", "char": {"level": 3, "heritage": "aluvian"}})
    await b.recv_until(lambda x: x["t"] == "play_ok")
    check("alice sees bob join", bool(await a.recv_until(lambda x: x["t"] == "system" and bob in x.get("msg", ""))))

    # movement input -> snapshot reflects both players
    await a.send({"t": "input", "x": 100, "z": 200, "yaw": 1.5, "hp": 90, "level": 7})
    await b.send({"t": "input", "x": -50, "z": 50, "yaw": 0.2, "hp": 80, "level": 3})
    snap = await a.recv_until(lambda x: x["t"] == "snapshot" and len(x.get("players", [])) >= 2)
    ids = {p["id"]: p for p in (snap["players"] if snap else [])}
    check("snapshot lists both players", alice in ids and bob in ids)
    check("snapshot carries alice position", snap and abs(ids.get(alice, {}).get("x", 0) - 100) < 1)
    check("snapshot shows character name, not account", ids.get(bob, {}).get("name") == f"{bob}0")

    # chat from bob reaches alice, tagged with his character name
    await b.send({"t": "chat", "msg": "hail, adventurer!"})
    cm = await a.recv_until(lambda x: x["t"] == "chat")
    check("chat relays bob -> alice", cm and cm.get("from") == f"{bob}0" and "hail" in cm.get("msg", ""))

    # /who lists online in-world characters by name
    await a.send({"t": "who"})
    wh = await a.recv_until(lambda x: x["t"] == "who")
    names = {p["name"] for p in (wh.get("players", []) if wh else [])}
    check("/who lists online characters", bool(wh) and f"{bob}0" in names and len(names) >= 2)

    # emote: broadcast to other in-world players
    await b.send({"t": "emote", "act": "wave"})
    em = await a.recv_until(lambda x: x["t"] == "emote")
    check("emote broadcasts to others", bool(em) and em.get("from") == f"{bob}0" and "waves" in em.get("msg", ""))

    # cast a Creature/Life spell on another player -> they receive an rbuff (in range)
    await a.send({"t": "input", "x": 100, "z": 200, "yaw": 0, "hp": 100})
    await b.send({"t": "input", "x": 102, "z": 200, "yaw": 0, "hp": 100})
    await asyncio.sleep(0.2)
    await a.send({"t": "cast", "target": bob, "spell": "life_heal_1"})
    rb = await b.recv_until(lambda x: x["t"] == "rbuff")
    check("cast on ally relays an rbuff", bool(rb) and rb.get("spell") == "life_heal_1" and rb.get("from") == f"{alice}7")
    # out of range -> no relay
    await a.send({"t": "input", "x": 9000, "z": 200, "yaw": 0, "hp": 100})
    await asyncio.sleep(0.2)
    await a.send({"t": "cast", "target": bob, "spell": "life_heal_1"})
    rb2 = await b.recv_until(lambda x: x["t"] == "rbuff", timeout=1.0)
    check("out-of-range ally cast is rejected", rb2 is None)

    # whisper: /tell delivers privately to the named character + echoes to sender
    await a.send({"t": "tell", "name": f"{bob}0", "msg": "meet me at the lifestone"})
    tw = await b.recv_until(lambda x: x["t"] == "chat" and x.get("channel") == "tell")
    check("whisper delivered to target", bool(tw) and tw.get("from") == f"{alice}7" and "meet me" in tw.get("msg", ""))
    echo = await a.recv_until(lambda x: x["t"] == "chat" and x.get("channel") == "tell_out")
    check("whisper echoes to sender", bool(echo) and echo.get("from") == f"{bob}0")

    # --- party: invite / accept / party chat / leave ---
    await a.send({"t": "party", "act": "invite", "name": f"{bob}0"})
    inv = await b.recv_until(lambda x: x["t"] == "system" and "invites you" in x.get("msg", ""))
    check("party invite reaches target", bool(inv))
    await b.send({"t": "party", "act": "accept"})
    joined = await a.recv_until(lambda x: x["t"] == "system" and "joined the party" in x.get("msg", ""))
    check("party accept notifies members", bool(joined))
    pm = await a.recv_until(lambda x: x["t"] == "pmembers")
    check("party roster (pmembers) synced", bool(pm) and f"{alice}7" in pm.get("names", []) and f"{bob}0" in pm.get("names", []))
    await a.send({"t": "pchat", "msg": "group up"})
    pc = await b.recv_until(lambda x: x["t"] == "chat" and x.get("channel") == "party")
    check("party chat reaches members", bool(pc) and "group up" in pc.get("msg", "") and pc.get("from") == f"{alice}7")
    # fellowship XP: a partied member near a kill shares its XP without tagging the mob
    fsnap = await a.recv_until(lambda x: x["t"] == "snapshot" and x.get("mobs"))
    fmob = next((mm for mm in fsnap["mobs"] if not mm.get("boss")), None)
    if fmob:
        await a.send({"t": "input", "x": fmob["x"], "z": fmob["z"], "yaw": 0, "hp": 100})
        await b.send({"t": "input", "x": fmob["x"] + 4, "z": fmob["z"], "yaw": 0, "hp": 100})  # bob doesn't attack
        await asyncio.sleep(0.25)
        await a.send({"t": "attack", "id": fmob["id"], "dmg": 99999})
        brew = await b.recv_until(lambda x: x["t"] == "reward")
        check("party member shares kill XP (fellowship)", bool(brew) and brew.get("xp", 0) > 0)

    await b.send({"t": "party", "act": "leave"})
    left = await a.recv_until(lambda x: x["t"] == "system" and "left the party" in x.get("msg", ""))
    check("party leave notifies members", bool(left))

    # --- M3: server-authoritative shared monsters ---
    snap = await a.recv_until(lambda x: x["t"] == "snapshot" and x.get("mobs"))
    mobs = snap.get("mobs", []) if snap else []
    check("snapshot carries shared mobs", len(mobs) > 0)
    check("mob has id/kind/hp fields", bool(mobs) and all(k in mobs[0] for k in ("id", "kind", "hp", "mhp")))
    boss = next((x for x in mobs if x.get("boss")), None)
    check("snapshot carries a shared world boss", bool(boss) and boss.get("name") and boss.get("mhp", 0) >= 1000)

    if mobs:
        # teleport alice onto a mob so the attack passes the server range check
        target = mobs[0]
        await a.send({"t": "input", "x": target["x"], "z": target["z"], "yaw": 0, "hp": 100})
        await asyncio.sleep(0.25)
        # an out-of-range attack must be ignored (no mob_hit)
        await a.send({"t": "input", "x": target["x"] + 9000, "z": target["z"], "yaw": 0, "hp": 100})
        await asyncio.sleep(0.25)
        await a.send({"t": "attack", "id": target["id"], "dmg": 5})
        far = await a.recv_until(lambda x: x["t"] == "mob_hit" and x.get("id") == target["id"], timeout=1.0)
        check("out-of-range attack rejected", far is None)
        # back in range: a big hit should kill it and yield a reward + mob_die broadcast
        await a.send({"t": "input", "x": target["x"], "z": target["z"], "yaw": 0, "hp": 100})
        await asyncio.sleep(0.25)
        await a.send({"t": "attack", "id": target["id"], "dmg": 99999})
        hit = await a.recv_until(lambda x: x["t"] == "mob_hit" and x.get("id") == target["id"])
        check("in-range attack -> mob_hit", bool(hit))
        die = await a.recv_until(lambda x: x["t"] == "mob_die" and x.get("id") == target["id"])
        check("lethal attack -> mob_die", bool(die) and die.get("by") == alice)
        rew = await a.recv_until(lambda x: x["t"] == "reward")
        check("kill -> reward with shared xp", bool(rew) and rew.get("xp", 0) > 0)

        # the kill drops shared ground loot (gold pile, first-come)
        drop = await a.recv_until(lambda x: x["t"] == "drop" and x.get("type") == "gold")
        check("kill -> gold drop broadcast", bool(drop) and drop.get("amt", 0) > 0 and "id" in drop)
        if drop:
            # a player who joins AFTER the kill should be sent the still-on-ground loot
            carol = await WS.connect()
            await carol.send({"t": "register", "user": f"carol_{uniq}", "pass": "secret3"})
            await carol.recv_until(lambda x: x["t"] == "auth_ok")
            await carol.recv_until(lambda x: x["t"] == "roster")
            await carol.send({"t": "create_char", "slot": 0, "name": f"carol{uniq}", "char": {}})
            await carol.recv_until(lambda x: x["t"] == "play_ok")
            replay = await carol.recv_until(lambda x: x["t"] == "drop" and x.get("id") == drop["id"])
            check("late joiner receives existing drops", bool(replay))
            await carol.close()
        if drop:
            # out-of-range pickup is rejected; in-range yields a loot grant + drop_gone
            await a.send({"t": "input", "x": drop["x"] + 9000, "z": drop["z"], "yaw": 0, "hp": 100})
            await asyncio.sleep(0.2)
            await a.send({"t": "pickup", "id": drop["id"]})
            far = await a.recv_until(lambda x: x["t"] == "loot", timeout=1.0)
            check("out-of-range pickup rejected", far is None)
            await a.send({"t": "input", "x": drop["x"], "z": drop["z"], "yaw": 0, "hp": 100})
            await asyncio.sleep(0.2)
            await a.send({"t": "pickup", "id": drop["id"]})
            # drop_gone is broadcast before the direct loot grant, so look for it first
            gone = await a.recv_until(lambda x: x["t"] == "drop_gone" and x.get("id") == drop["id"])
            check("picked-up drop -> drop_gone", bool(gone))
            loot = await a.recv_until(lambda x: x["t"] == "loot")
            check("in-range pickup -> loot grant", bool(loot) and loot.get("type") == "gold" and loot.get("amt", 0) > 0)
            await a.send({"t": "pickup", "id": drop["id"]})  # second pickup must yield nothing
            again = await a.recv_until(lambda x: x["t"] == "loot", timeout=1.0)
            check("double pickup yields no second loot", again is None)

    # --- M3d: shared world event (Incursion) ---
    # the test server is started with a short DERETH_EVENT_CD, so an Incursion is active by
    # now; a freshly-joined player should be synced the active event, and alice should have
    # seen the event_start broadcast.
    dave = await WS.connect()
    await dave.send({"t": "register", "user": f"dave_{uniq}", "pass": "secret4"})
    await dave.recv_until(lambda x: x["t"] == "auth_ok")
    await dave.recv_until(lambda x: x["t"] == "roster")
    await dave.send({"t": "create_char", "slot": 0, "name": f"dave{uniq}", "char": {}})
    await dave.recv_until(lambda x: x["t"] == "play_ok")
    ev = await dave.recv_until(lambda x: x["t"] == "event_start", timeout=5.0)
    check("active Incursion synced to late joiner", bool(ev) and ev.get("name") and ev.get("count", 0) > 0)
    await dave.close()

    # bob leaves -> alice sees leave message
    await b.close()
    check("alice sees bob leave", bool(await a.recv_until(lambda x: x["t"] == "system" and "left" in x.get("msg", ""))))

    # reconnect: wrong password rejected; right password restores the full 8-char roster
    await a.close()
    c = await WS.connect()
    await c.send({"t": "login", "user": alice, "pass": "WRONG"})
    me = await c.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"))
    check("login wrong password -> auth_err", me and me["t"] == "auth_err")
    await c.send({"t": "login", "user": alice, "pass": "secret1"})
    mo = await c.recv_until(lambda x: x["t"] == "auth_ok")
    check("login correct password -> auth_ok", bool(mo))
    ros2 = await c.recv_until(lambda x: x["t"] == "roster")
    check("roster restores all 8 characters", bool(ros2) and len(ros2.get("chars", [])) == 8)
    # playing slot 5 returns its persisted data (created with level 6)
    await c.send({"t": "play_char", "slot": 5})
    p5 = await c.recv_until(lambda x: x["t"] in ("play_ok", "play_err"))
    check("play_char slot 5 restores its character (level 6)", bool(p5) and p5["t"] == "play_ok" and p5.get("char", {}).get("level") == 6)
    # save to the active slot, then verify it persists after another relogin
    await c.send({"t": "save", "char": {"level": 6, "kills": 999, "heritage": "sho"}})
    await c.send({"t": "ping"}); await c.recv_until(lambda x: x["t"] == "pong")
    await c.send({"t": "delete_char", "slot": 7})
    rosd = await c.recv_until(lambda x: x["t"] == "roster")
    check("delete_char removes a slot (8 -> 7)", bool(rosd) and len(rosd.get("chars", [])) == 7)
    await c.close()
    d = await WS.connect()
    await d.send({"t": "login", "user": alice, "pass": "secret1"})
    await d.recv_until(lambda x: x["t"] == "auth_ok")
    await d.recv_until(lambda x: x["t"] == "roster")
    await d.send({"t": "play_char", "slot": 5})
    pd = await d.recv_until(lambda x: x["t"] == "play_ok")
    check("save persists to the active slot (kills 999)", bool(pd) and pd.get("char", {}).get("kills") == 999)
    await d.close()

    print(f"\n{PASS} passed, {FAIL} failed")
    sys.exit(1 if FAIL else 0)

if __name__ == "__main__":
    asyncio.run(main())
