# Dereth — Remaining Work (THE authoritative list)

**This is the single source of truth for outstanding work.** It supersedes and retires every other
to-do / gaps / roadmap document — those files now redirect here:

> `wrapup-remaining-work.md` · `REMAINING-WORK.md` · `ac-remaining-gaps.md` · `fidelity-split.md` ·
> `ac-newplayer-split.md` · `ac-data-extraction-roadmap.md` · `plan-ac-heads.md` ·
> `asherons-call-authenticity-gaps.md` · `asherons-call-longtail-gaps.md` · `three-agent-dat-update.md` ·
> `playtest-2026-07-05.md` · `loop-test-findings.md` · `building-entry-audit.md` · `../ROADMAP.md`

The `asherons-call-*-reference.md`, `kilmer-saga-*`, `ships.md`, and `asherons-call-housing-reference.md`
files are **design reference**, not to-do lists — they stay as-is.

**Live tracker — reconciled + code-verified, statuses current as of 2026-07-06.** This is a working
list: each item carries a **Status** (`open` / `WIP:<agent>` / `done` / `blocked`). Anything not listed
below (and everything marked `done`) is shipped — see **Confirmed shipped** at the bottom. Effort key:
**S** ≈ 1–2 days · **M** ≈ 1–2 weeks · **L** ≈ 2+ weeks / architectural risk.

> **Current status (2026-07-06, post PR #137):** **Lane A ✅ done** (#1 Damage/Crit Rating + Crushing
> Blow, #4 content) · **Lane B ✅ done** (#2 hats, #3 dye subpalettes, #12 dungeon-lighting slider,
> #13 shield mesh) · **Lane C ✅ done** (#5 entry corridors, #6 0x21 layout decode → `aclayouts.json`,
> #10 gold window chrome, #11 PAGE-99 paperdoll, #19 shop wield gates) · **all 4 design decisions
> resolved** (#14 keep incantation gate · #15 keep soft casting · #16 keep vitals model · #17 go retail:
> 3-day PK key, retail quest lockouts, mana-stone battery + draw-from-mana). **Still genuinely open:**
> **#18** Academy staff leak (lane B, bug) · the terrain-lane blockers (#7–#9, needs A1 data) ·
> on-hardware eyeballs (#10 chrome look, #13 window/doorway).

---

## Lane assignments — 3 agents (claim before you build)

Lanes are split **by code domain** so the three agents rarely touch the same region of `index.html`
and never the same item. **Rules:** (1) work **only** your lane's items; (2) before starting an item,
change its **Status** in the tables below from `open` to `WIP:<agent>`, and to `done` when merged;
(3) if you finish your lane, take the next `open` item from the **shared backlog** (§ Blocked / Design)
and mark it WIP first; (4) never edit an item another agent has marked `WIP`.

| Lane | Agent | Domain (why these don't collide) | Items | State |
|------|-------|----------------------------------|-------|-------|
| **A — Combat & item data** | Agent 1 | combat math + item/loot tables | **#1** Damage/Crit Rating + Crushing Blow · **#4** content data top-offs | ✅ complete |
| **B — Avatar, clothing & rendering** | Agent 2 | avatar rig, mesh/clothing render, in-world visuals | **#2** hats as head-mesh · **#3** dye subpalettes · **#12** dungeon lighting tune · **#13** equipped-shield arm + doorway/window | ✅ complete — **#18** Academy staff leak is the one open lane-B item |
| **C — UI/HUD, town & tooling** | Agent 3 *(worked by agent2, PR #137)* | HUD/CSS panels, town building placement, Python extractors | **#5** path_blocked packing · **#6** 0x21 layout decode · **#10** UI window-frame chrome · **#11** paperdoll layout · **#19** shop wield gates | ✅ complete |

**All three lanes are complete.** Per rule (3), agents next claim from what remains: **#18** (lane B
bug — the only open buildable item), the on-hardware eyeball passes (#10 look, #13 window/doorway),
or await the human calls #15–#17. The terrain lane (#7–#9) stays excluded.

**Not assigned to the 3 working agents:** items **#7–#9** are the **excluded Agent-1 terrain
re-extraction lane** (blocked — leave alone). Items **#14–#17** are **design decisions** — a human
product call, not agent work; do not implement without a decision recorded here.

---

## 1 · Buildable now (present data & tools)

| # | Lane | Item | What's undone | Size | Status |
|---|------|------|---------------|------|--------|
| 1 | A | **Damage Rating / Crit-Damage Rating / Crushing Blow** ✅ | Sneak Attack, Recklessness & Dirty Fighting shipped; the DR/CDR multipliers, Crushing Blow, and the rear-attack **+20 DR** are not (only a Luminance "Aura of Valor" damage-rating buff and a mace *blurb* exist). *(auth-gaps Cb5)* | S–M | **done** |
| 2 | B | **Hats as real part-16 head-mesh swaps** ✅ | Cowl/cap/qafiya/turban/fez/kasa are still procedural cloth props (`felt()`/coils), not real head-model swaps. Cosmetic. *(consolidated Tier4)* | S–M | **done** |
| 3 | B | **Dye subpalettes on base clothing** ✅ | `acclothing.json` subpalettes aren't applied to base garments — no `subPalette`/`dyeSub` wiring. *(consolidated Tier4)* | S | **done** |
| 4 | A | **Content top-offs** *(optional)* | ~23 more salvage materials, extra gems, hide variants. (Armor sets, named bosses, and the full creature roster are already present.) *(items/regions refs — MATERIALS catalog already exhaustive: ~89 mats incl. all AC gems/stones/cloths)* | S | **done** |
| 5 | C | **`path_blocked` interior-packing residual** ✅ | ~~Entry centreline grazes.~~ `tbCutDoorway` clears a player-width corridor inward of every cut door; 56-town in-engine re-audit: interior wall-locks 14→0, pass 95.5%→97.5%, no regressions. *(building-entry-audit; PR #137)* | S | **done** |
| 6 | C | **`0x21` StringTable UI-layout decode** ✅ | ~~Undocumented.~~ Framing reversed (layoutId/800×600 header, element declaration+body pairs, five-u32 x/y/w/h/z geometry + 0x06 texture refs); `tools/ac_layout_export.py` → `assets/aclayouts.json` (101 layouts, 2,901 elements, 76% validated geometry). *(PR #137)* | follow-up | **done** |
| 18 | B | **Academy staff leak on death / generic exit** *(bug, medium)* | Dying inside the Training Academy interior (or any plain-`exitDungeon` path) skips `exitAcademyHall`, leaking the 8 interior staff NPCs + their obstacle entries into the overworld at world-origin coords; `curDungeon` also stays set. Self-heals only on hall re-entry. Fix: call `_clearAcademyNpcs()` from `exitDungeon` when `curDungeon.academy` (and null `curDungeon`). *(playtest 2026-07-06 #23, verified live)* | S | WIP:agent2 |
| 19 | C | **Shop rows omit wield reqs & item-magic info** ✅ | ~~Blind gear buys.~~ Shop rows render wield gates (red when unmet), cantrip tier, resolved retail spells, spellcraft; unwieldable stock gets an amber ⚠ Buy button + "needs …" tooltip. *(playtest 2026-07-06 #24; PR #137)* | S | **done** |

## 2 · Blocked — needs the excluded Agent-1 terrain re-extraction lane

*(Agent-1 terrain lane — not assigned to the 3 working agents.)*

| # | Item | What's undone | Size | Status |
|---|------|---------------|------|--------|
| 7 | **Authored landmarks + terrain barriers** | Named mountains that gate travel; terrain is currently procedural trig-noise. Needs the landblock heightmap. *(auth-gaps W12)* | L | blocked |
| 8 | **Irregular continent + real ocean rim** | The terrain-*shape* sliver of Named Regions — region names/lore/HUD already shipped; only the true coastline & island landmasses remain (underwater structures currently mitigated by `islandLift`). *(auth-gaps W8)* | L | blocked |
| 9 | **Geometry-driven *town* renderer** | Largely closed — dungeons render real EnvCell meshes and towns already stream real AC building meshes (`tbBuildMesh`). Only true EnvCell *town-layout* geometry remains, and it overlaps the A1 lane. *(ac-data-extraction-roadmap)* | follow-up | blocked |

## 3 · On-hardware — visual changes that can't be verified headless (SwiftShader); need a real-GPU eyeball

| # | Lane | Item | What's undone | Size | Status |
|---|------|------|---------------|------|--------|
| 10 | C | **Real UI window-frame chrome** ✅ | ~~9-slice on draggable panels.~~ `acChromeInit` composes the 9-slice sheet at boot and border-images all 11 draggable panels (graceful fallback). *(Corrected: `06001b14`/`06001343` are the Turbine/AC logos and `060011bb` a stone field — the real frame family is `06001920`/`21`/`22`.)* Final look: on-hardware eyeball. *(wrapup §7; PR #137)* | S–M | **done** — final-look pass WIP:agent2 |
| 11 | C | **Paperdoll panel layout** (PAGE-99 manual) ✅ | ~~Arrangement differs.~~ Held/Body/Adornment rails, Containers row (satchel + side packs), live Examine box on hover, persisted icon-rail toggle. *(ac-remaining-gaps A2; PR #137)* | M | **done** |
| 12 | B | **Dungeon lighting final tune** | Eyeball torch/ambient intensity live; nudge amb/hemi or the ×1.35 factor. Pass 2 already landed. *(REMAINING-WORK)* | S | done (user-tunable slider) |
| 13 | B | **Equipped-shield arm mount + doorway/window transparency** | Real shield *meshes* now show on drops/examine, but the equipped-avatar shield stays procedural (arm orientation), the opaque-doorway recess is a warm-glow card, and windows are still baked-opaque — all need on-hardware iteration. *(playtest #21)* | S each | done (shield; window/doorway = on-hardware iteration w/ new brightness knob) — remainder WIP:agent2 |

## 4 · Design decisions — a human product call, not code

| # | Item | The question |
|---|------|--------------|
| 14 | ✅ **Level-8 Incantation skill-300 gate — DECIDED: keep it (authentic).** | In retail AC, level-8 "Incantations" had a difficulty ≈ **350–400** — *higher* than an unbuffed specialized caster's cap (~290–300). Casting the top tier **required** stacking Item-Enchantment **Aptitude** self-buffs + gear **cantrips** ("buff up, then cast") — a core, intended part of AC endgame magic. The game's **300** gate vs a ~272 unbuffed max is therefore authentic *and gentler* than retail, and reachable via the existing `skillBuffs` (Aptitude), `gearSkill` (cantrips), Five Fold Path (+10) and Enlightenment — **not a dead end**. **No req change.** Added a UX nudge: high-tier cast-blocks (req ≥ 240) now tell you to buff/gear your skill to reach the incantation. |
| 15 | ✅ **Full component-casting formula — DECIDED: keep the soft model.** | Stays on the current scarab/taper reagent model; the authentic exact-component "wrong → fizzle" formula is intentionally *not* adopted (too punishing for a homage). No change. *(auth-gaps Mg2)* |
| 16 | ✅ **Vitals-formula purity — DECIDED: keep the current model.** | Confirmed players already raise Health/Stamina/Mana **directly with XP** (`player.vitals` on the character sheet — `vitalCost`/`xpUnspent`, added to max in `derive()`), *plus* the attribute-derived base *plus* the +level term. Since players have direct control, the pure End/2·End·Self change (which would force a full creature rebalance) is **not pursued**. No change. *(auth-gaps C3)* |
| 17 | ✅ **Timers — DECIDED: go retail. DONE.** | **PK oath → the authentic 3-day key** (`PK_LOCK_MS`=3d; messages show d/h via `fmtLockLeft`). **Quest lockouts already retail-tiered** (`questCdMs`: L45+ = 7-day weekly, L30+ = 20-hour daily — no change needed). **Mana stone reworked to the retail battery:** *release into gear* + *consume an enchanted item to bank its mana* (retail destroy-to-store) + a Dereth-only *draw from your own reserve* (amount = stone quality/`cap` × your Mana-Conversion rate) — via a new `openManaStone` action menu; legacy `charge` stones migrate. |

---

## Confirmed shipped (do NOT re-open — verified in code 2026-07-06)

Old lists still mark many of these open; they are done:

- **Onboarding & creation:** ToD staged creation wizard, starting gear + Welcome Letter + heritage
  clothing, post-Academy greeter chain (7k/9.3k+500p/12.5k + Pathwarden suit + +4% XP), **Training
  Academy tutorial-dungeon interior** (`buildAcademyHall` — Great Hall / Courtyard / Workshop, Sentry-gated).
- **Tier-1/2 flagships:** combine/tradeskill engine (1,500 recipes), Aetheria slots/levels/sets/surges,
  multi-level dungeons (`buildDungeonReal`, y-aware storeys, descend portals), weapon-skill tree,
  melee+missile to-hit contests, **per-element armor** (`ARMOR_MAT_RESIST`), **weapon damage types**
  (`dt` pierce/slash/bludgeon), loot item-spells + Spellcraft + item mana, tinker per-item caps/imbue-once,
  **Enlightenment gates** (L275 + Society Master + auras), full recall set (Tie/Recall/Secondary/Sending),
  fellowship cap-9 proportional XP, secure trade + coin.
- **Combat/magic depth:** per-body-part combat, ballistic projectiles + terrain collision, missile range
  falloff, per-swing variance, stamina scaling, burden/encumbrance, Mana Conversion/fizzle, Void DoT
  curses, wield/level/skill loot requirements, banes.
- **Social:** allegiance patron/vassal tree + XP pass-up + NPC vassals, monarchy + `/allegiance` chat,
  3-state PK + altars + PK-loot, corpse/item-loss death (+ shared corpses), society ranks/ribbons/Test
  quests, live Town Crier feed.
- **World:** region-aware weather, **level-gated overworld portals** (Facility Hub + frontier), **named
  regions** (`regionNameAt` + HUD + `/where`), ambient fauna, organic roads, drowning, portal storms,
  Colosseum/instanced events.
- **Content:** **Chess** (perft-validated engine + AI), book placement + library stands, **"A History of
  Dereth"** (Dereth runs its *own* live events — retail GameEventDefDB extraction intentionally dropped),
  starter quests aligned to retail, all armor sets + named bosses (Aerbax/Gaerlan/Martine) + creature roster,
  **the full 10-year Kilmer Saga incl. Years 7–10** (Year of Bone … Year of the Crown).
- **Extraction/assets:** high-res textures (1,224), non-PCM music, **shield & clothing item models**,
  server-side item/spell mirror, building/clothing GfxObjs wired, all 66 creature kinds, 873 retail titles,
  **NPCs on real AC bodies + heads** (`buildPerson` retired to fallback).
- **Heads/UI:** WYSIWYG creator preview, explicit head-choice rows (creator + barber), face tone/AO,
  **female forehead-band fix**, barber restrictions, circular radar.
- **Ships:** ownable/boardable skiff/cog/caravel water travel.
- **Lane B (Agent 2, 2026-07-06):** authentic dye subpalettes on base clothing (tools/ac_clothing_dyes.py
  + acClothDyeTex canvas remaps — ~half of all NPC cloth meshes dyed) · real part-16 hats (tools/
  ac_hat_export.py, 9 hats ×m/f, head swapped beneath — retail bald-under-hat) · equipped shields show
  the RETAIL mesh on the forearm (plus the fix for aShield never being registered — the equipped shield
  had silently never displayed) · Settings "Dungeon brightness" slider (persisted, live-retunes the delve).

*Maintenance note: when an item here is completed, move it into "Confirmed shipped" with a one-line code
reference — keep this the single, honest, reconciled list. Do not resurrect the retired docs.*
