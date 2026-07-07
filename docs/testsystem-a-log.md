# TestSystemA — overnight automated test log

Recurring full-game test passes (every 30 min) while the user sleeps. Each run tests the
latest `origin/main`, logs anything not working as expected, and **fixes nothing** — findings
here are queued for a later fixer session. Tag: **TestSystemA**.

Harness notes: headless preview tab (rAF-throttled — the main loop freezes when unfocused, so
loop-driven behaviour is driven with synthetic `dt`; 3D screenshots unavailable). Findings that
depend on visuals are marked as needing on-hardware confirmation.

---

## TestSystemA — Run 1 (2026-07-06 late night, main @ c3232dc)

**Result: CLEAN — no defects found.** Coverage this run:

- Boot → fresh recruit wakes in the Training Academy interior ✓
- **All-spells (PR #142)**: SPELLBOOK carries 2,265 spells across 7 schools (war/life/creature/
  item/void/summon/aoe); cast Summon Wisp, Nether Bolt I, Flame Bolt I — all consume mana and
  execute ✓
- **Item icons (PR #143)**: 100/100 rolled items resolve to real AC icons when called with the
  item object (all live call sites do). *Investigated a false alarm: probing `itemIconHTML`
  name-only (without the 4th `it` arg) falls back to emoji — that form isn't used by real call
  sites.*
- **Ye Olde Stair**: Aerlinthe Island (scripted grid) → "Take Ye Olde Stair down — depth 2 (E)"
  → depth 2 builds (12 cells, mobs respawned), climb back to depth 1, exit clean ✓
- **Combine engine**: acrecipes pack = 1,500 combines (wrapper object `{recipes:[...]}`) +
  28 in-game brews; live combine Smelting Pot × Iron Ore → **Slag** (alchemy 38) consumed the
  ore and produced the result ✓
- Combat: melee kill via the real damage path ✓; death → corpse holds the dropped item →
  E-recover restores inventory and consumes the corpse ✓
- Save round-trip carries aetheria/academy/knownSpells ✓
- **Zero console errors.**

False alarms cleared (not logged as defects): icons probe signature; recipes wrapper count.

## TestSystemA — Run 2 (2026-07-07 early, main @ c3232dc)

**Harness change:** the browser preview MCP disconnected mid-night — no in-browser runs possible
from here on. Run 2 pivoted to what stays testable headless: the multiplayer server, asset-pack
integrity, and static analysis of the game script. (In-browser coverage resumes if the preview
harness returns; Run 1's in-game pass stands as the browser baseline for this build.)

### FINDINGS (log only — nothing fixed)

- **TSA-1 (defect, low/latent) — `craftResultItem` declared twice at top level** (merge
  artifact): index.html lines **14355** and **14528**. Two DIFFERENT bodies — the older one
  classifies combines via raw AC_ITEMS fields; the newer one (line 14528) handles retail **dye
  pots as real dye items** and routes through the `acItemRow` vendor classifier. JS hoisting
  means the LATER definition silently wins, so the game currently runs the newer/richer one and
  the older is dead code — behaviour is correct today (Run 1's Slag combine used it fine), but
  the dead twin invites confusion/divergence. Fix later: delete the line-14355 body.

### Clean checks

- **Server suite: 47/47 PASS** (fresh instance, port 18899, throwaway DB) — auth, persistence,
  8-char roster, create/delete/save slots, chat, presence, movement, shared mobs (out-of-range
  attack rejected, lethal → mob_die, shared XP, gold drop broadcast), drops (late-joiner sync,
  range-gated pickup, no double loot), Incursion event sync to late joiner, wrong-password
  rejection. *False alarm cleared: the Incursion test requires the documented `DERETH_EVENT_CD`
  (short) env on the server; without it, 46/47 with that one "failure" — a precondition, not a
  server bug. Harness note: always launch the test server with `DERETH_EVENT_CD=2`.*
- **Game script syntax gate**: extracted 2.29 MB main script → `node --check` PASS.
- **Asset integrity**: every JSON pack under assets/ parses; all 232 indexed dungeon files
  exist on disk; no duplicate top-level const/let (1 duplicate function = TSA-1 above; 1,162
  top-level functions total).

## TestSystemA — Run 3 (2026-07-07 early, main @ c3232dc)

**Result: CLEAN — 16/16 extended server scenarios pass; no new defects.**

The chrome-browser MCP reconnected briefly but the extension itself stayed unreachable (Chrome
closed), so in-browser coverage remains paused. Run 3 went DEEP on the multiplayer server with a
new scenario harness (`server/tsa_extended.py`, kept in-repo for reuse) covering flows the stock
suite skips:

- **Secure trade with coin (full lifecycle)**: open → invite delivered → accept_open → window
  sync → item offer syncs to partner → coin offer syncs (and clears both accepts, AC rule) →
  ok+ok → done both sides → **coin lands with the item-giver (250p), item lands with the
  coin-giver** ✓
- **3-state PK gating over the wire**: PK→NPK hit does NOT land · PK→PK lands (dmg intact) ·
  PK→PKL does NOT land (rulesets fight their own) ✓
- **Allegiance**: swear (equal-level rule honoured) → patron notified → `alg_info` returns →
  `/a` allegiance chat reaches the patron on the allegiance channel ✓
- **Shared corpses**: death stands up a broadcast corpse; **non-owner recovery rejected**;
  owner recovery grants the full bundle (items + gold) ✓

Harness notes (my errors, fixed in the harness — NOT server bugs): register requires a
password ≥ 4 chars (short password → silent-looking auth_err and everything downstream times
out); trade protocol is act:"open"/"ok" with field `amount`; PvP hits target the account
username and PK state rides the `input` tick (`pkState`). First drafts of the harness produced
11-13 false failures from these — all resolved to protocol fidelity, zero server defects.

Also re-ran the stock suite against the same instance mid-diagnosis: 47/47.
