# M3: Server-Authoritative Economy (closing #238)

The remaining work on #238. The **bounded hardening is done** (see "Landed" below); what's
left is architectural: the server must *own* the economy instead of trusting a client-authored
save blob. This doc is the staged plan so the build can be funded and reviewed piece by piece.

## Landed (safe, verified, non-breaking)
These bound the *crude*-forgery and malformed-input surface but do **not** make the server authoritative:

| PR | What it bounds |
|----|----------------|
| #270 | `input`: NaN/Inf position rejected; `hp`/`mhp` ≤1e6; `level` ∈ [1,275] |
| #271 | `save` / `create_char`: every persisted economy scalar clamped (gold ≤2e9, xp ≤1e13, skills `t`∈{0,1,2}/xp≤1e11, attrs ≤1000, inv ≤500, stacks ≤1e5, packs ≤7, vitae ≤0.40) |
| #275 | `trade`/`death` item dicts: numeric leaves ≤1e6, strings ≤64, depth/count capped |
| #277 | reject non-standard `NaN`/`Infinity` JSON literals at the parse boundary |

Also verified authoritative already: mob kill-XP/gold **values** live server-side (`MOB_BESTIARY`);
`resolve_attack` awards them; `do_pickup` range-checks and uses server-chosen amounts; the client
defends against malicious remote identity fields (`dressRemoteWield` is guarded + try/catch).

## The gap that keeps #238 open
The client computes **all** economy state locally (loot, vendor buy/sell, quest rewards, crafting,
kill XP) and periodically sends the whole character as a `save` blob, which the server persists.
So a tampered client can still:
1. Save **plausible** forged values (legit-looking gold/XP/items it never earned) — clamps can't catch it.
2. `trade` an item it doesn't actually own (no server inventory to validate against).
3. `death`-mint a corpse of items it doesn't own.
4. **Rollback-dupe**: save rich state A → trade items away → re-send old blob A to restore them.

None can be closed by clamping. The server needs an independent source of truth.

## Target architecture
Flip the model from *"client computes, server persists"* to *"client requests, server owns"*:

- **Server state per character**: `coin`, `xp`/`unspent`, `level`, `inventory[]`, `skills`, in RAM,
  loaded from DB on `enter_world`, persisted by the server (client can no longer write it directly).
- **Client sends intents, not results.** Every economy mutation becomes a validated request:
  - `vendor_buy {shop, wcid, qty}` → server checks price × qty ≤ coin, debits coin, adds item.
  - `vendor_sell {invIdx, qty}` → server verifies the item is in server inventory, credits coin.
  - `craft {recipe, invIdxs}` → server verifies inputs are owned, consumes them, adds output.
  - `equip/unequip/move` → server mutates server inventory.
  - loot pickup / kill XP already server-side (`do_pickup`, `resolve_attack`) — just credit server state.
- **Server broadcasts authoritative snapshots**; the client renders them and no longer trusts its own totals.
- **`save` becomes a server action** (server serializes its own authoritative state); the client `save`
  message is dropped for online characters.

Once inventory/coin/XP are server-owned:
- Trade validates each offered item against server inventory (escrow-remove → transfer on both-ok).
- Death corpse loot is drawn from server inventory, not client claims.
- Rollback-dupe is impossible (no client-authored blob).

## Staging (each stage independently shippable + verifiable)
1. **Coin** — the simplest scalar. Route every coin change through server intents
   (`vendor_buy/sell`, loot, trade, quest-reward-claim). Server owns `coin`; save can't raise it.
   *Verify:* forged `gold` in save is ignored; legit vendor/loot flows still work end-to-end.
2. **XP / level** — server owns `xp`/`unspent`/`level`, credited by `resolve_attack`, event bounties,
   pass-up, and a `quest_claim {questId}` intent (server holds the quest reward table).
3. **Inventory** — the big one. Server owns `inventory[]`; all item moves are intents. This is what
   makes trade/death fabrication impossible.
4. **Trade / death** — re-point at server inventory (escrow + conservation), retire the trusted paths.
5. **Cutover** — drop the client `save` for online chars; server persists on a timer + on logout.

## Migration / risk
- **Offline single-player is unaffected** — it keeps its local `localStorage` save; authority applies
  only to online characters. This is what makes the staging safe.
- Ship behind a per-stage server flag; keep the old path until each stage is validated with two live clients.
- Biggest risk is desync (client prediction vs. server truth) — mitigated by the server snapshot being
  the render source of truth and the client reconciling, not overriding.
- Harness note: full verification needs two live WS clients (`server/test_client.py`) driving the
  vendor/craft/trade intents against a running server — feasible but out of scope for the clamp PRs.

## Bottom line
#238's crude-forgery surface is hardened four ways and safe to leave as-is. **Closing** it is Stage 1–5
above — a real client+server build, not a clamp. Recommend starting with **Stage 1 (coin)** as the
first end-to-end proof of the intent/authority pattern.
