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

## TestSystemA — Run 4 (2026-07-07, main @ c3232dc)

**Result: one minor cosmetic finding (TSA-2); server robustness and pack integrity otherwise clean.**
Chrome still closed — browser coverage paused; this run: server fuzzing + pack cross-reference audit.

### FINDINGS (log only)

- **TSA-2 (minor, cosmetic) — 292 of 4,338 acitems reference icon DIDs with no exported PNG**
  in `assets/acicons/` (3,961 icon files present). Breakdown: 281 misc items, 10 weapons
  (`torch`, `wand`, `sceptre`, `sceptre of syliph`, `branith's staff`, …), 1 armor. In-game
  impact is soft: `itemIconHTML` falls through to the item's CATEGORY icon (still a real AC
  icon, just generic) — no emoji regression (Run 1's 100/100 sample was band-limited loot,
  which is why it missed these). Fix later: re-run `ac_icon_export.py` for the missing DIDs
  (some may be palette-variant DIDs needing the base icon fallback in the exporter).

### Clean checks

- **Server fuzz suite 9/9** (`server/tsa_fuzz.py`, kept in-repo): survives non-JSON frames,
  6 malformed-JSON shapes, oversized register names (rejected politely), hostile in-world
  fields (1e308 coords, negative damage, null ids, string coin, non-dict save), pre-auth world
  messages ignored, a 500-message input burst, and an abrupt mid-trade socket yank (partner
  notified, server healthy after every probe).
- **Pack cross-references**: acvendors 16,185 stock refs → 0 dangling catalog ids ·
  acspells 6,266 spells → 0 dangling component ids (163 components) · CANON dungeons 331 =
  232 real-geometry + 99 scripted/procedural (matches the documented split) · actownmodels
  covers every mesh-rendered town object — the 537 placements without meshes are all
  portal/lifestone/bindstone kinds, which use the game's bespoke visuals by design (audit
  false alarm, cleared).

## TestSystemA — Run 5 (2026-07-07, main @ c3232dc)

**Result: one cleanup finding (TSA-3); soak + memory + static invariants otherwise clean.**
Coordination note: a sibling **TestSystemC** (another session, WITH a live browser) is running
in-game functional passes on `origin/testsystemc-log` (casino, pets, ships, dispel, rares,
imbues, loadouts — 9 passes, all green so far). TestSystemA therefore stays on its
complementary lane: server depth + static analysis + pack audits. No duplicated coverage.

### FINDINGS (log only)

- **TSA-3 (cleanup, low) — 20 genuinely dead top-level functions** (never referenced from JS
  or HTML): `_acHeadTex, acClock, addFeature, addTattoo, armorTypeOf, bestMeleeEff,
  bestMeleeSkillValue, buildCrystal, ccRandomize, hsType, isCapitalName, kcInspectHook,
  primaryAttack, resetUIPos, rollCantrips, rollCrit, rollFurniture, salvageValue,
  spawnEmberEcho, talkEmissary`. Most are superseded remnants of this week's reworks
  (`bestMeleeSkillValue` → lane-A weapon-skill tree; `rollCantrips` → orphaned when the #127
  merge adopted main's cantrip system — same family as TSA-1). **Two worth a second look
  rather than blind deletion:** `ccRandomize` (did the character-creator lose its Randomize
  button?) and `resetUIPos` (HUD-reset entry point) — possible small UX regressions.

### Clean checks

- **Concurrency soak ×2** (`server/tsa_soak.py`, kept in-repo): 8 concurrent clients, mixed
  ops (movement broadcast fan-out, chat/emote/who, attacks, saves, ping, open/cancel trades)
  — 5,958 then 6,211 ops at ~130 ops/s, **0 client errors**, server responsive after both.
- **Memory observation (watch, not defect):** working set 45→104 MB during soak 1, held ~104
  through 20s idle, then **fell to 53 MB during/after soak 2** — non-monotonic across
  identical load cycles ⇒ allocator retention/reuse, not a leak. Re-check under longer soaks
  if the server ever runs for days.
- TODO/FIXME markers in the game script: 0. Console noise: 25 log / 7 warn (all boot-path
  informational, unchanged).

## TestSystemA — Run 6 (2026-07-07, main @ c3232dc)

**Result: one protocol-gap finding (TSA-4); persistence, sessions, events, and tooling clean.**

### FINDINGS (log only)

- **TSA-4 (protocol gap, minor) — `delete_char` of the actively-played slot is SILENTLY
  ignored.** dereth_server.py:1407-1412: the guard `not (cl.in_world and cl.slot == slot)`
  correctly refuses deleting the character you're playing, but the refusal branch sends
  NOTHING back — no error, no roster — so a client cannot distinguish refusal from lag/loss.
  (This also masked itself in testing: `create_char` auto-enters the world on the new slot,
  so a create→delete sequence on the same slot always hits the silent refusal.) Fix later:
  send `{"t":"system"|"play_err", msg:"You cannot delete the character you are playing."}`
  in the refusal branch.

### Clean checks (`server/tsa_persist.py`, kept in-repo)

- **Deep save/load fidelity**: a complex char blob (nested dicts/lists, floats like
  itemMana 380.5, unicode incl. CJK + symbols, aetheria/academy/knownSpells fields)
  round-trips **deep-equal** through save → disconnect → login → play_char.
- **Sessions**: auth token from login resumes a fresh socket (`resume` → auth_ok);
  bogus token politely rejected.
- **Slot lifecycle (correct order)**: create extra slots → switch to another slot →
  delete → recreate → plays the NEW blob (level 99). The two failures in the first
  scripted pass were harness ordering (auto-play-on-create + offset ack reads), triaged
  down to TSA-4 as the only real issue.
- **Event lifecycle end-to-end**: Incursion "Olthoi Swarm" starts (8 mobs, ttl 132s) →
  walked to each mob (attacks are range-gated, correctly) → 8/8 mob_die → **event_end
  success:true broadcast** ✓.
- **Python compile gate**: `py_compile` over all tools/*.py + server/*.py — pass.

## TestSystemA — Run 7 (2026-07-07, main @ c3232dc)

**Result: CLEAN — no new defects.** Fellowship/allegiance wire mechanics, remaining asset
packs, and a Python hazard scan all green.

- **Fellowship over the wire** (`server/tsa_fellow.py`, kept in-repo): party invite/accept,
  party chat channel, shared-kill XP for every fellow. **All three level-spread rules verified
  live**: tight band (L20/L22) splits equally with the size bonus (882/882) · proportional
  band (L20/L50, spread 30) pays by level — 48 vs 121, high > low ✓ · extreme spread
  (L20/L22/L80, spread 60 ≥ 50) reverts to equal full shares (882 each) — the first probe
  flagged this as a failure until re-read against the documented rule; harness expectation
  fixed, not the server.
- **Muster**: refused below level 12 with the renown message; grants a named NPC vassal at
  L80 ✓. **Monarch MOTD**: pushed to sworn vassals ✓ — after an initially ILLEGAL probe
  (L22 swearing to L20 is correctly refused; the refusal text contains the word "swear",
  which fooled the first regex — harness note).
- **Pack integrity (remaining)**: acworldstructs 1,431 blocks / 6,076 buildings → 0 missing
  meshes · achousing 5 mesh refs → all present · acbooks 898 entries parse.
- **AST hazard scan** (server + test client): no mutable default args, no bare excepts,
  no eval/exec.

## TestSystemA — Run 8 (2026-07-07, main @ c3232dc)

**Result: one feature-gap finding (TSA-5); relay matrix, frame caps, map data, and DB clean.**

### FINDINGS (log only)

- **TSA-5 (multiplayer feature gap, medium-low) — remote players' equipped weapon/shield never
  syncs.** The client's `input` tick sends position/vitals/level/heritage/title/pkState —
  no wield fields; the server snapshot carries none; `remoteApp`/`reconcileRemotes` build
  remote avatars with heritage-seeded clothing but no equipment. Net effect: **other players
  always render bare-handed and shield-less online**, whatever they wield. (The run-2 protocol
  census listed "weapon"/"offhand" message types — those matches were item-code strings, not
  dispatch handlers; corrected here.) Fix later: add `weapon`/`offhand` kind fields to the
  input tick + snapshot, and hand them to the remote-avatar dresser.

### Clean checks

- **Cosmetic relay matrix**: spellfx projectile relays to nearby players ✓ · emote broadcast ✓ ·
  who lists all in-world ✓ (completes the server message-type coverage).
- **Frame-size cap**: MAX_MSG 1 MiB enforced — a 2 MiB frame drops that connection only;
  server healthy after ✓.
- **acmap.png data sanity**: 2041² · height indexes ≤250 (table has 255) · terrain types ≤30
  (≤31 spec) · road bits present · 24 extreme-peak sample px (the named summits) ✓.
- **SQLite**: `integrity_check ok` on the twice-soaked DB (19 users / 17 chars) ✓.
