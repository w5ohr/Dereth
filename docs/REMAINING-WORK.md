# Dereth — Remaining Authenticity Work (Handoff)

> **⚠️ STATUS UPDATE (2026-07-06): several items in this document are now COMPLETE.**
> The four Tier-1 flagship features shipped in PR #124 — **combine/tradeskill engine** (Alchemy/
> Cooking/Fletching/Dyeing live via 1,500 acrecipes.json combines), **Aetheria** (slots L75/150/225,
> leveled drops, set sigils, combat surges), **multi-level dungeons** ("Ye Olde Stair" descents,
> 153 two-floor + 31 three-floor delves, +35%/floor scaling), and **AC character creation +
> onboarding** (staged wizard incl. Viamontian, 270/52 credit pools, specialize-at-creation, ToD
> starter kit, Training Academy, greeter chains — verified end-to-end).
> Also complete from the 2026-07-06 fix wave: authentic MC/fizzle, retail quest lockouts, Town
> Crier rumor feed, monarchy & allegiance chat, dungeon lighting, window transparency, drop expiry,
> forehead-band fix, non-PCM music. **Do not re-plan these.** The maintained source of truth for
> what's left is `docs/remaining-work-consolidated.md`.

Status as of 2026-07-03, HEAD `e9bb109`. The master worklist (user directive: "make
everything exactly like AC, work through all of it") is largely complete. This file
lists what remains, with enough context for a fresh session to pick any item up cold.

## How work has been done (follow this loop)

1. **Extract** — a Python tool in `tools/` reads `acdata/client_portal.dat` /
   `acdata/client_cell_1.dat` (Turbine DAT parsers live in `tools/ac_model_export.py`,
   MotionTable/Animation parsers in `tools/ac_creature_export.py`) or the ACE world DB
   (`ACE-World-16PY-master/Database/3-Core/`, split SQL — parse with regex, strip
   `/* comments */` BEFORE splitting VALUES). Output a JSON pack under `assets/`.
2. **Wire** — `index.html` (single-file game) fetches the pack at boot and overrides
   the engine tables, always keeping the hand-tuned fallback when the fetch fails.
3. **Verify** — syntax: extract the script with awk into scratch and run `jsc` (no node
   installed). Runtime: Claude_Preview MCP against `Play Dereth.command`'s http.server;
   click `#continueBtn` first (world builds after the title screen), use `preview_eval`
   asserts + `logLines` for log checks. **rAF suspends headless** — the live loop does
   not run; drive functions manually and `renderer.render(scene,cam)` for screenshots.
   Third-person for avatar shots (`thirdPerson=true`).
4. **Commit per milestone, push.** `acdata/`, `ACE-World-16PY-master/`, `ace-world*.sql*`
   are gitignored (Turbine-owned, user-supplied) — derived JSON/PNG packs ARE committed.

## Remaining items, in rough priority order

### 1. Per-body-part combat (attack heights, per-part AL)
- Data: `weenie_properties_body_part` per creature weenie (3-Core) — per-part armor
  level (`base_Armor`), damage (`d_Val`, `d_Var`), quadrants. The reward exporter
  (`tools/ace_reward_export.py`) already reads max d_Val; extend it to emit the full
  per-part table per bestiary kind.
- Engine: melee has a power bar (`power` 0.6–1.6, Cb1) and attack animations with
  high/med/low variants in the MotionTable (`tools/ac_player_anims.py` exports; the
  ATTACK command list has Med/High/Low ids). Wire: chosen attack height → hits a body
  part bucket → use that part's AL instead of the flat `.al`, and creature attacks
  roll their part's d_Val/d_Var. HUD: a small high/med/low selector like retail's.

### 2. GameEventDefDB live events (monthly world events)
- Reference doc already researched: `docs/asherons-call-events-reference.md`.
- Data: `GameEventDefDB` in portal.dat (0x0E00000x region) names retail live events;
  the ACE DB `event` table has start/end states. Extract names+timing → seasonal
  rotation of world-state toggles (spawn waves, vendor stock, decorations) on the
  existing monthly-event framework (see [dereth-monthly-events] memory / the events
  code around `updateCalendar`/`currentCalEvent` in index.html).

### 3. CharGen full wiring
- `assets/acchargen.json` (already extracted by an earlier session — check `assets/`)
  carries heritage starting templates: attributes, trained skills, starting gear,
  starting towns, skin/hair palette ids per heritage.
- The creator (`openCharCreator` / `ccWork`, ~line 19900 in index.html) already has
  templates (`ccApplyTemplate`) and per-heritage looks (`HERITAGE_LOOK`). Wire the
  REAL palette ranges for skin/hair per heritage and the retail starting-gear grants
  in `startGame` (createAttr/createSkills/createTown already carry through).

### 4. Mana stone drain/store cycle (retail semantics)
- Current: `stat:"manastone"` recharges the equipped focus's battery (H15 system).
- Retail: use a stone on an enchanted item → DESTROYS the item, stores its mana;
  use the charged stone → refills the mana of items you wear that cast spells.
- Wire like the dye picker (`openDyePicker` is the pattern — an overlay listing
  eligible satchel items): drain target = any item with `acspells`/`itemMana`;
  stone gains `charge`; charged stone Use → tops up worn items' `itemMana`.

### 5. QuestDefDB timers
- Our quests already have repeat cooldowns (`cd` seconds, `taskCooldown`). Retail
  quest lockouts (e.g. 20-day timers) live in the ACE `quest` table
  (`min_Delta` seconds per quest key). Extraction is easy; the hard part is matching
  retail quest keys to our custom quest ids — match via the QUEST_GIVERS NPC-name
  bridge used by `tools/ace_reward_export.py` (it already matches 121 retail NPCs).
  Where a giver matches, replace the synthetic cooldown with the real min_Delta.

### 6. Chess (Game weenies)
- Retail had playable chess boards in taverns/houses. Weenies: `Game` /
  `GamePiece` types in 3-Core WeenieDefaults. A minimal faithful version: a board
  prop in inns + a 2D overlay chess UI (vs. a trivial AI or hot-seat). Low priority,
  high charm. No engine dependencies.

### 7. Smaller polish items
- **Dungeon lighting (real mode)**: *(pass 2, 2026-07-06)* amb raised 0.85→0.95, hemi
  0.35→0.45, and the torch placement reworked in `buildDungeonReal` for **even coverage**
  (≈1 per few cells, capped at 20 so the one big dungeon mesh isn't lit by an unbounded
  light count) instead of the old "every ~4th cell" that left far cells dark between
  torches; each dungeon prop-light now burns **brighter (×1.35) and reaches +6u further**
  than its overworld cousin (`buildProp`'s default is tuned for town torches). Verified by
  state (rig built correctly, ×1.35/+6 applied, the global daylight-kill does NOT clobber
  the dungeon's own local amb/hemi, carried torch on, 0 errors). **Still not visually
  verifiable headless** (SwiftShader renders dungeons black) — worth a final eyeball on the
  real machine; nudge amb/hemi or the ×1.35 factor if still dim or now too bright.
- **Item models for shields/clothing drops**: `tools/ac_item_models.py` currently
  exports MeleeWeapon/MissileLauncher/Caster setups (655 meshes). Add "Clothing"
  (shields live there) if shield drops should use real meshes too — mind pack size.
- **Book placement**: 898 retail books ship via the scrivener shelf (4/day,
  `bookStock()`); could also place named books at their true locations (libraries,
  quest dungeons) — `assets/acbooks.json` keys are lowercase display names.
- **Dye recipes**: dye pots are vendor-bought (outfitters, 850 py). Retail brewed
  them via alchemy from dye plants — add recipes to the crafting pack
  (`assets/acrecipes.json` merge or the alchemy craftables list).
- **Ammo-type strictness**: stacks exist (`stat:"ammo"`, `for:"bow"` etc.) as bonus
  damage; retail REQUIRED matching ammo to fire. If wanted: block bow fire at 0
  arrows (check how `player.bowDraw` fire path consumes ammo today).
- **Server parity**: `server/dereth_server.py` loads `assets/acrewards.json` for
  shared-mob hp/dmg/xp, but the item catalog/spell stats are client-side only. If
  the MMO mode matters, mirror `acitems.json` + `acspellstats.json` server-side.
- **Barber restrictions**: retail could not change heritage/gender at the barber —
  `openBarber` currently reopens the full creator. Hide the race row + gender in
  barber mode (`ccBarber` flag exists) if strictness is wanted.
- **44 unmapped dungeons**: canon names with no landblock id anywhere (mostly
  surface camps that may have no interior). List and reasoning in the
  dereth-session-checkpoint memory; verify individually before chasing.

## Key engine facts a new session needs

- **World coords**: canon AC lat/lng degrees × `COORD` (=80) → world; x=lng·80,
  z=−lat·80; world is 16000 units, `HALF=8000`. Landblock → canon: gx/240 − 101.95.
- **Three-space transform** from AC z-up: pos (x, z, −y), quat (x, z, −y, w),
  reversed winding (`tx_pos`/`tx_quat` in tools/ac_model_export.py).
- **Avatar**: model JSON part array index == setup part index; `acPartTemplates`
  filters small parts but carries `i`. `u.acDrive` maps joints→driver parts for
  MotionTable playback (`acMotionPose`/`acMotionTick`); rest transforms restored on
  handback. `player.bowDraw` can be undefined — use `!(x>0)`, never `x<=0`.
- **Housing**: `AC_HOUSES` (2,000 retail dwellings), streamed by `hsStreamHouses`
  (build ≤230u, drop >310u, cap 48); deed = `player.homestead={hid,type,hooks}`.
- **Dialogue**: `acDialogueLine(name,role)` — exact name → " the <title>" suffix →
  `AC_DIA_ROLE` trade pool.
- **Spells**: boot pass over `SPELLBOOK_LIST` from `assets/acspellstats.json`
  (mana/words/comps/dmg/boost/mod/dur/closs); `burnSpellComponents` consumes carried
  reagents at closs×CDM after the fizzle check.
- **14-light virtual pool is perf-critical** — never add unpooled dynamic lights in
  the world (dungeon builders use their own scoped lights, disposed on exit).
- **Concurrent sessions** work this repo — always `git pull` before pushing, and
  re-read/re-anchor before editing `index.html` if an edit unexpectedly fails.
