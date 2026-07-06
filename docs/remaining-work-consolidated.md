# Dereth — Remaining Work (THE authoritative list)

**This is the single source of truth for outstanding work.** It supersedes and retires every other
to-do / gaps / roadmap document — those files now redirect here:

> `wrapup-remaining-work.md` · `REMAINING-WORK.md` · `ac-remaining-gaps.md` · `fidelity-split.md` ·
> `ac-newplayer-split.md` · `ac-data-extraction-roadmap.md` · `plan-ac-heads.md` ·
> `asherons-call-authenticity-gaps.md` · `asherons-call-longtail-gaps.md` · `three-agent-dat-update.md` ·
> `playtest-2026-07-05.md` · `loop-test-findings.md` · `building-entry-audit.md` · `../ROADMAP.md`

The `asherons-call-*-reference.md`, `kilmer-saga-*`, `ships.md`, and `asherons-call-housing-reference.md`
files are **design reference**, not to-do lists — they stay as-is.

**Reconciled + code-verified 2026-07-06.** Every item below was re-checked against `index.html` /
`assets/` / `tools/` and confirmed *still undone*. Everything from the old lists that is **not** here is
shipped — see **Confirmed shipped** at the bottom. Effort key: **S** ≈ 1–2 days · **M** ≈ 1–2 weeks ·
**L** ≈ 2+ weeks / architectural risk.

---

## Lane assignments — 3 agents (claim before you build)

Lanes are split **by code domain** so the three agents rarely touch the same region of `index.html`
and never the same item. **Rules:** (1) work **only** your lane's items; (2) before starting an item,
change its **Status** in the tables below from `open` to `WIP:<agent>`, and to `done` when merged;
(3) if you finish your lane, take the next `open` item from the **shared backlog** (§ Blocked / Design)
and mark it WIP first; (4) never edit an item another agent has marked `WIP`.

| Lane | Agent | Domain (why these don't collide) | Items |
|------|-------|----------------------------------|-------|
| **A — Combat & item data** | Agent 1 | combat math + item/loot tables | **#1** Damage/Crit Rating + Crushing Blow · **#4** content data top-offs |
| **B — Avatar, clothing & rendering** | Agent 2 | avatar rig, mesh/clothing render, in-world visuals | **#2** hats as head-mesh · **#3** dye subpalettes · **#12** dungeon lighting tune · **#13** equipped-shield arm + doorway/window |
| **C — UI/HUD, town & tooling** | Agent 3 | HUD/CSS panels, town building placement, Python extractors | **#5** path_blocked packing · **#6** 0x21 StringTable decode · **#10** UI window-frame chrome · **#11** paperdoll layout |

**Not assigned to the 3 working agents:** items **#7–#9** are the **excluded Agent-1 terrain
re-extraction lane** (blocked — leave alone). Items **#14–#17** are **design decisions** — a human
product call, not agent work; do not implement without a decision recorded here.

---

## 1 · Buildable now (present data & tools)

| # | Lane | Item | What's undone | Size | Status |
|---|------|------|---------------|------|--------|
| 1 | A | **Damage Rating / Crit-Damage Rating / Crushing Blow** | Sneak Attack, Recklessness & Dirty Fighting shipped; the DR/CDR multipliers, Crushing Blow, and the rear-attack **+20 DR** are not (only a Luminance "Aura of Valor" damage-rating buff and a mace *blurb* exist). *(auth-gaps Cb5)* | S–M | open |
| 2 | B | **Hats as real part-16 head-mesh swaps** | Cowl/cap/qafiya/turban/fez/kasa are still procedural cloth props (`felt()`/coils), not real head-model swaps. Cosmetic. *(consolidated Tier4)* | S–M | WIP:AgentB |
| 3 | B | **Dye subpalettes on base clothing** | `acclothing.json` subpalettes aren't applied to base garments — no `subPalette`/`dyeSub` wiring. *(consolidated Tier4)* | S | WIP:AgentB |
| 4 | A | **Content top-offs** *(optional)* | ~23 more salvage materials, extra gems, hide variants. (Armor sets, named bosses, and the full creature roster are already present.) *(items/regions refs)* | S | open |
| 5 | C | **`path_blocked` interior-packing residual** | A handful of tightly-packed town buildings whose entry centreline a neighbour grazes (door still usable at an angle). `door_offmarker` & `floor_poke` already resolved. *(building-entry-audit)* | S | open |
| 6 | C | **`0x21` StringTable UI-layout decode** *(low value)* | 101 retail 800×600 panel-layout defs identified but not decoded; only useful for pixel-perfect panel placement. *(three-agent-dat-update)* | follow-up | open |

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
| 10 | C | **Real UI window-frame chrome** | Apply the large plates (`06001b14`/`060011bb`/`06001343`) as 9-slice `border-image` on the draggable panels (needs per-plate slice tuning). Vitals/quickbar/charge-bar chrome + the gold strut on vitals & quest panels are already wired (11 of 37 plates). *(wrapup §7)* | S–M | open |
| 11 | C | **Paperdoll panel layout** (PAGE-99 manual) | Icon-rail toggle + Examine box + jewelry/container slot arrangement. Contents exist; arrangement differs. *(ac-remaining-gaps A2)* | M | open |
| 12 | B | **Dungeon lighting final tune** | Eyeball torch/ambient intensity live; nudge amb/hemi or the ×1.35 factor. Pass 2 already landed. *(REMAINING-WORK)* | S | WIP:AgentB |
| 13 | B | **Equipped-shield arm mount + doorway/window transparency** | Real shield *meshes* now show on drops/examine, but the equipped-avatar shield stays procedural (arm orientation), the opaque-doorway recess is a warm-glow card, and windows are still baked-opaque — all need on-hardware iteration. *(playtest #21)* | S each | WIP:AgentB |

## 4 · Design decisions — a human product call, not code

| # | Item | The question |
|---|------|--------------|
| 14 | **Level-8 Incantation skill-300 gate** | A maxed *specialized* caster tops ~272, so L8 non-War incantations are uncastable unbuffed (War at 302 clears it). Intended (buff/gear to 300), or lower the req / raise the specialized max? *(the one standing playtest observation)* |
| 15 | **Full component-casting formula** | Keep the current soft scarab/taper model, or adopt the authentic exact-component "wrong → fizzle" formula (judged punishing for a homage)? *(auth-gaps Mg2)* |
| 16 | **Vitals-formula purity** | Move max HP/Stam/Mana to pure End/2 · End · Self (drop the +level terms)? Requires rebalancing every retail-statted creature. Deliberately deferred. *(auth-gaps C3)* |
| 17 | **Ratify the game-scaled timers** | Quest lockouts 2–10 min (vs 20h–7d), PK 5-min oath (vs 3-day key), mana-stone focus-battery (vs destroy-item-to-charge) — all intentional compressed-clock/model choices. Listed only for sign-off. |

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

*Maintenance note: when an item here is completed, move it into "Confirmed shipped" with a one-line code
reference — keep this the single, honest, reconciled list. Do not resurrect the retired docs.*
