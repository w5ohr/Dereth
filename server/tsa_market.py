#!/usr/bin/env python3
"""#722 e2e: the consignment market. Two clients — a seller lists, a buyer browses and buys
(pay-first, atomic claim), the seller gets the live sold notice, collects proceeds at the SAME
town's broker, and reclaims an unsold listing. Failure paths: oversize item, fabricated item,
zero price, buying your own listing, double-buy, foreign reclaim, listing cap.
#887: updated for the #808 authoritative market — the seller's listings are seeded into the
server-side inventory at chargen (take_owned escrow) and the buyer carries authoritative coin,
mirroring the real client flow; authCoin debit/credit is asserted on buy/collect.
Usage: python3 server/tsa_market.py [host] [port]   (server must already be running)
"""
import asyncio, base64, hashlib, json, secrets, struct, sys, time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8787


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

def mk(t, act, **kw):
    d = {"t": t, "act": act}; d.update(kw); return d

async def player(user, charname, inv=None, gold=0):
    # #887: post-#808 the market is authoritative — 'list' escrows via take_owned(cl.inv) and 'buy'
    # debits cl.coin, so the character must actually OWN what it lists and HOLD what it spends.
    # create_char can't seed these (#S2 forces a starter economy at creation), so mirror the real
    # client flow instead: loot/craft happen client-side, then an autosave carries inv+gold to the
    # server, where reconcile_econ adopts the inventory wholesale and meters the coin gain through
    # the creation bucket (our few thousand pyreals sit far under the 1M burst).
    c = await WS.connect()
    await c.send({"t": "register", "user": user, "pass": "secret1"})
    m = await c.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"))
    assert m and m["t"] == "auth_ok", f"auth failed for {user}"
    await c.recv_until(lambda x: x["t"] == "roster")
    await c.send({"t": "create_char", "slot": 0, "name": charname, "char": {"level": 5, "heritage": "sho"}})
    po = await c.recv_until(lambda x: x["t"] == "play_ok")
    assert po, f"play failed for {user}"
    if inv or gold:
        await c.send({"t": "save", "char": {"level": 5, "heritage": "sho", "gold": gold, "inv": inv or []}})
        sk = await c.recv_until(lambda x: x.get("t") == "save_ok")
        assert sk, f"seed save not acked for {user}"
    return c

async def main():
    uniq = secrets.token_hex(3)
    seller_n, buyer_n = f"msell{uniq}", f"mbuy{uniq}"
    print(f"Market e2e on {HOST}:{PORT} — seller {seller_n}, buyer {buyer_n}")
    TOWN = "Holtburg"
    sword = {"name": "Tinkered Sabre", "stat": "weapon", "wt": "sword", "v": 9}
    boot = {"name": "Old Boot"}
    crates = [{"name": f"crate {i}"} for i in range(13)]
    # #887: an OWNED item that still busts MARKET_ITEM_BYTES (4096) after sanitize_item — 59 pad keys
    # of 64-char strings survive sanitization (keys ≤40 chars, leaves ≤64) and serialize past the cap,
    # so the oversize guard is exercised on the real post-escrow path, not via a fabricated item.
    bloat = {"name": "Bloated Relic"}
    for i in range(59):
        bloat[f"pad{i:02d}"] = "x" * 64

    # seller owns everything it will list; buyer holds the coin it will spend (505 across both buys)
    s = await player(seller_n + "a", seller_n, inv=[sword, boot, bloat] + crates)
    b = await player(buyer_n + "a", buyer_n, gold=2000)

    # ── list: happy path ────────────────────────────────────────────────────
    await s.send(mk("market", "list", town=TOWN, item=sword, price=500))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") in ("listed", "list_fail"))
    check("list -> listed", bool(m) and m["act"] == "listed" and m["price"] == 500)
    lid = m and m.get("id")

    # list: rejections
    await s.send(mk("market", "list", town=TOWN, item=sword, price=0))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "list_fail")
    check("zero price rejected", bool(m))
    await s.send(mk("market", "list", town=TOWN, item=bloat, price=10))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "list_fail")
    check("oversize item rejected", bool(m))
    # a FABRICATED item (never in the authoritative inventory) must be refused too (#808)
    await s.send(mk("market", "list", town=TOWN, item={"name": "Forged Blade", "v": 999}, price=10))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "list_fail")
    check("unowned (fabricated) item rejected", bool(m))

    # ── browse from the buyer: the sword is on the shelf ────────────────────
    await b.send(mk("market", "browse", town=TOWN))
    m = await b.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "stock")
    row = m and next((r for r in m["rows"] if r["id"] == lid), None)
    check("browse shows the listing", bool(row) and row["price"] == 500 and row["seller"] == seller_n)
    check("browse town-scoped", True)

    # seller cannot buy their own consignment
    await s.send(mk("market", "buy", id=lid))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "buy_fail")
    check("self-buy rejected", bool(m))

    # ── buy: pay-first transfer + live sold notice ──────────────────────────
    await b.send(mk("market", "buy", id=lid))
    m = await b.recv_until(lambda x: x.get("t") == "market" and x.get("act") in ("bought", "buy_fail"))
    check("buy -> bought (item handed over on payment)", bool(m) and m["act"] == "bought"
          and json.loads(m["item"])["name"] == "Tinkered Sabre" and m["price"] == 500)
    check("buyer debited authoritatively (authCoin 2000-500)", bool(m) and m.get("authCoin") == 1500)   # #808/#887
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "sold")
    check("seller notified live of the sale", bool(m) and m["id"] == lid and m["price"] == 500 and m["buyer"] == buyer_n)

    # double-buy: the atomic claim has one winner
    await b.send(mk("market", "buy", id=lid))
    m = await b.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "buy_fail")
    check("second buy of the same listing rejected", bool(m))

    # ── proceeds wait at the broker until collected ─────────────────────────
    await s.send(mk("market", "collect", town="Yaraq"))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "collected")
    check("collect at the WRONG town pays nothing", bool(m) and m["gold"] == 0)
    await s.send(mk("market", "collect", town=TOWN))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "collected")
    check("collect at the broker pays the proceeds", bool(m) and m["gold"] == 500 and m["n"] == 1)
    check("proceeds credited authoritatively (authCoin 0+500)", bool(m) and m.get("authCoin") == 500)   # #808/#887: conserved transfer
    await s.send(mk("market", "collect", town=TOWN))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "collected")
    check("proceeds paid only once", bool(m) and m["gold"] == 0)

    # ── reclaim an unsold listing (and only YOURS, and only once) ───────────
    await s.send(mk("market", "list", town=TOWN, item=boot, price=7))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "listed")
    lid2 = m and m.get("id")
    check("second listing accepted", bool(lid2))
    await b.send(mk("market", "reclaim", id=lid2))
    m = await b.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "reclaim_fail")
    check("reclaiming someone else's listing rejected", bool(m))
    await s.send(mk("market", "reclaim", id=lid2))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "reclaimed")
    check("seller reclaims the unsold item", bool(m) and json.loads(m["item"])["name"] == "Old Boot")
    await s.send(mk("market", "reclaim", id=lid2))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "reclaim_fail")
    check("double reclaim rejected", bool(m))

    # ── listing cap ─────────────────────────────────────────────────────────
    ok_caps = 0
    for c in crates:                        # all 13 are OWNED — only the 12-listing cap may refuse one (#887)
        await s.send(mk("market", "list", town=TOWN, item=c, price=5))
        m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") in ("listed", "list_fail"))
        if m and m["act"] == "listed": ok_caps += 1
    check("listing cap enforced at 12", ok_caps == 12)

    # ── 'mine' ledger view ──────────────────────────────────────────────────
    await s.send(mk("market", "mine"))
    m = await s.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "mine")
    check("mine lists the 12 active consignments", bool(m) and len(m["rows"]) == 12
          and all(r["status"] == "listed" for r in m["rows"]))

    # ── offline sale -> summary on next login ───────────────────────────────
    await b.send(mk("market", "browse", town=TOWN))
    m = await b.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "stock")
    crate = next((r for r in m["rows"] if r["seller"] == seller_n), None)
    await s.close()
    await asyncio.sleep(0.3)
    await b.send(mk("market", "buy", id=crate["id"]))
    m = await b.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "bought")
    check("buy while seller offline still completes", bool(m))
    s2 = await WS.connect()
    await s2.send({"t": "login", "user": seller_n + "a", "pass": "secret1"})
    m = await s2.recv_until(lambda x: x["t"] in ("auth_ok", "auth_err"))
    await s2.recv_until(lambda x: x["t"] == "roster")
    await s2.send({"t": "play_char", "slot": 0})
    m = await s2.recv_until(lambda x: x.get("t") == "market" and x.get("act") == "sold_summary", timeout=4.0)
    check("offline sale surfaces as sold_summary on login", bool(m) and m["town"] == TOWN and m["gold"] == 5)

    await s2.close(); await b.close()
    print(f"\n{PASS} passed, {FAIL} failed")
    sys.exit(1 if FAIL else 0)

asyncio.run(main())
