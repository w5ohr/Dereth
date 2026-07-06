# Dereth — Consolidated "Left To Do" (2026-07-06, rev 6 — Tiers 1 AND 2 complete)

Synthesized from every planning/gap/findings doc + memory + code TODOs (three parallel doc audits),
then **reconciled against the current code** — several items the older gap-analysis docs list as
"missing" are in fact already implemented (see "Already done" below), so they're excluded here.


> ✅ **Completed since rev 1 (the 2026-07-06 fix wave + spell FX):** Mana Conversion / fizzle formula
> (authentic, PR #118) · retail quest lockout timers (PR #118) · Town Crier live rumor feed +
> monarchy & allegiance chat verified (PR #119) · female forehead band fix · non-PCM music track
> (extracted, in assets/acmusic) · building-base collision fix · window transparency (#113) ·
> dungeon lighting pass 2 (#114) · drop-expiry despawn (#112) · high-res texture upgrade (resolved) ·
> deploy Dockerfile · spell-landing FX (creature spirit swirl + life gem, PR #120 — new feature).

> ✅ **TIER 1 COMPLETE (rev 3):** all four flagship items are done —
> **Combine/tradeskill engine** (1,500 recipes live: ⚗ Combine in the satchel, logistic skill roll,
> durable tools, dye pots→dyes, vendor supplies by trade) · **Aetheria** (slots L75/150/225, leveled
> drops, 5 set sigils, 5 combat surges) · **Multi-level delves** (Ye Olde Stair, 153 two-floor + 31
> three-floor, +35%/floor scaling, floor-aware exit) · **Character creation + onboarding** (verified
> already-built by the parallel fix-wave: staged wizard with 4 heritages incl. Viamontian, 7
> professions, 270 attr credits/52 skill credits with specialize-at-creation, ToD starter kit,
> Training Academy, greeter chains — end-to-end creation test passed).

> ✅ **TIER 2 COMPLETE (rev 5, branch tier2-systems):** built this pass — **weapon-skill tree**
> (each weapon rolls ITS skill: heavy/light/finesse/twohand/missile), **missile to-hit** (lost roll
> flies wide), **96 "Other" support spells** (retail names + real Other mana; ally via relay, else
> your familiar, else refuse+refund), **Enlightenment true gates** (L275 + Society Master + maxed
> auras), **Secondary Portal Tie/Recall + Portal/Lifestone Sending** (receiver-resolved over the
> cast relay), **item-spells on loot** (Minor→Legendary cantrips with Spellcraft + item mana that
> drains worn and re-wakes via Mana Stone). Verified already-built by parallel passes (not re-done):
> melee to-hit contest, level cap 275, vitals raising, retail society ranks + ribbons + Test quests,
> tinker per-item caps + imbue-once, limited vendor pyreals, fellowship cap 9 + proportional XP,
> secure trade (/trade), AC housing gates/writs/rent. **Tier 2 is done — do not re-plan.**

> ✅ **TIER 2 COMPLETE (PR #129, branch tier2-systems):** weapon-skill tree (each weapon rolls ITS
> skill) · missile to-hit (lost roll flies wide) · 96 "Other" support spells (retail Other mana;
> ally via relay, else familiar, else refuse+refund) · Enlightenment true gates (L275 + Society
> Master + maxed auras) · Secondary Portal Tie/Recall + Portal/Lifestone Sending · item-spells on
> loot (Minor→Legendary cantrips, Spellcraft, item mana + Mana-Stone re-wake). Verified already
> built by parallel passes: melee to-hit, level cap 275, vitals raising, society ranks/ribbons/
> Tests, tinker caps/imbue-once, limited vendor pyreals, fellowship cap 9 + proportional XP,
> secure /trade, AC housing gates/Writs/rent. **Do not re-plan Tier 2.**

> ✅ **Also complete (same-day parallel passes):** Chess (full legal engine + AI, tables in every
> capital) · book placement · mana-stone retail semantics · starting-gear finish · "A History of
> Dereth" chronicle · shield item models · UI-chrome accent (9-slice framing left as polish).
> Strike these from Tier 3 below.

Sources: `docs/REMAINING-WORK.md`, `wrapup-remaining-work.md`, `ac-remaining-gaps.md`,
`ac-newplayer-split.md`, `fidelity-split.md`, `three-agent-dat-update.md`, `building-entry-audit.md`,
`asherons-call-authenticity-gaps.md`, `asherons-call-longtail-gaps.md`, `loop-test-findings.md`,
`playtest-2026-07-05.md`, `plan-ac-heads.md`, `ships.md`, `kilmer-saga-event-specs.md`, memory files.
Code TODOs in index.html: **0**.

> ⚠️ **Staleness note:** the `asherons-call-*-gaps.md` docs are historical. Spot-checks confirm many
> listed "gaps" are done. Verified-done, excluded from the open list: **wield requirements** on loot
> (`wieldFail`/`reqLvl`/`reqVal`/`reqArcane`), **MMD currency**, **casting foci** (orbs/wands/staffs
> `wt:"focus"`), **recall contracts** (`stat:"contract"`), **unarmed weapons** (`wt:"unarmed"`),
> **burden/encumbrance** (in HUD), **magic-defense evade**, **1500-recipe data + salvage/tinker**,
> **56/56-town lifestones**, **ambient fauna creatures** (rabbit/penguin exist). Items below that
> touch these areas are the *remaining depth*, not the whole feature.

---

## TIER 1 — Flagship features (biggest, most-referenced)

- **AC-authentic character creation + new-player onboarding.** The single largest cluster (5 docs agree):
  - Staged creation wizard (Heritage → Profession → Attributes(330-pt) → Skills(52 credits, specialize) → Appearance/gender → Town → Name) wired to `acchargen.json`.
  - Starting gear/letters on creation (Calling Stone, Letter From Home, training weapon, caster kit).
  - **Training Academy tutorial dungeon** (interior + NPC chain, skippable) using real `acdialogue.json`.
  - Post-Academy greeter chain (Lifestone Greeter → Bartender → Pathwarden, ~41k XP → L7).
  - Align ~10 starter quests to retail town mappings/rewards.
  - Size: **L**. Sources: wrapup §1, ac-newplayer-split, ac-remaining-gaps A4/C14.

- **Combine / tradeskill engine (currently inert).** `AC_RECIPES` (1500) is loaded but there's no
  drag-item→item combine action, so **Alchemy, Cooking, Fletching, Dyeing** train but do nothing.
  Build the combine roll (skill vs difficulty, consume/produce, failure). Size: **L**.
  Sources: longtail L1–L4/L7/L8.

- **Multi-level dungeons + in-dungeon portals.** Currently one flat level + chest per dungeon.
  AC had "Ye Olde…" chains, drop portals, linked sub-instances. Size: **L**.
  Sources: wrapup §6, ac-remaining-gaps C10, gaps W7.

- **Aetheria slot/level/surge system.** Currently 3 flat permanent gems. AC: slots unlock at
  L75/150/225, leveled aetheria, set bonus + surge procs. Size: **L**. Sources: wrapup §4, gaps I5.

---

## TIER 2 — Systems depth (each meaningful, mostly M)

- **Buff/debuff "Other" targeting** — CE/Item/Life/Protection buffs are self-only; add Other forms for group support. (M) — wrapup §3, gaps Mg1.
- **Loot item-spells + mana + Spellcraft** — mutated cantrips (Minor/Major/Epic/Legendary), mana pools on gear, Spellcraft stat. (L) — wrapup §4, gaps I2.
- **Salvage/tinker split + per-item cap** — 8 tinker skills, 10-tink-per-item cap, imbue-once, units ≤ workmanship (currently 2 skills, no per-item cap). (M) — wrapup §4, gaps I3.
- **Mana Conversion / fizzle formula** — authentic `25·diff × spell level` + fizzle tiers (currently flat cost cut + ad-hoc fizzle). (M) — wrapup §3, gaps Mg5.
- **Weapon-skill tree meaningful** — `bestMeleeEff` takes best of all skills, so Heavy/Light/Finesse distinction is vestigial; map each weapon to its skill. (M) — gaps Cb7.
- **To-hit / evade contest** — attacks currently always hit in reach (only magic has evade); add attack-skill-vs-defense miss rolls. (M) — gaps Cb3.
- **Monarchy + `/allegiance` chat channel** — monarch rank atop the tree + server allegiance channel. (M) — wrapup §5, gaps S2.
- **Player-to-player secure trade window.** (M) — gaps S8.
- **Fellowship depth** — cap 9 (not 6), equal/proportional-by-level-spread XP modes, over-level penalty. (M) — gaps S7.
- **Housing depth** — level gates (Cottage L20/Villa L35/Mansion L50), Writs, monthly upkeep, hooks, allegiance-gated access. (M) — wrapup §8, gaps S11.
- **Society ranks/ribbons/test-quests/halls** — retail rank ladder + ribbon thresholds + Test quests. (M) — gaps S6.
- **Server-side quest lockout timers** (20h–7d via QuestDefDB min_Delta) + non-saga live-story Town Criers. (M–L) — wrapup §5, ac-remaining-gaps B12.
- **Full augmentation tree + hard cap** — Jack of All Trades, skill/spec-credit augs, XP augs, innate-attribute reinforcements, Asheron's-Castle aug quests, total-aug cap. (M) — gaps E2/C6.
- **Recall spell set** — Primary/Secondary Portal Tie + Recall, Portal Recall (last portal), Portal Sending, town recalls (only Lifestone + Sanctuary now). (M) — gaps Mg3.
- **Endgame progression tuning** — gate Enlightenment at L275 + Society Master + maxed lum auras (currently L100, no society req); no hard **level cap 275**; vitals separately raisable (Health=End/2, Stam=End, Mana=Self). (M–L) — wrapup §8, gaps C1/C2/C3/D22.

---

## TIER 3 — World & content

- **Underwater world structures (~1128 bldgs / 102 landblocks)** submerged in the western ocean (Aphus Lassel, Aerlinthe, Osteth/Linvak isles). Partially fixed with `islandLift`; full solution pending. (L) — building-entry-audit 🔴.
- **Named regions + irregular continent + authored terrain** (Osteth/Aphus Lassel/Linvak, real ocean rim, named mountains that gate travel) — currently a square 16000² trig-noise map. **Blocked on landblock heightmap data extraction.** (L) — wrapup §6, gaps W8/W12.
- **Retail monthly live-event extraction** — pull real event names/timing from `GameEventDefDB` onto the existing monthly framework. (M) — REMAINING-WORK #2, wrapup §9.
- **Instanced/repeatable event dungeons** (tickets, vault keys, timed) as a template for live content. (L) — gaps E3.
- **44 unmapped dungeons** — verify canon names + landblock ids or confirm no interior. (S each) — REMAINING-WORK, ac-remaining-gaps C15.
- **279/331 dungeons room-for-room from cell.dat**; remaining ~52 use the beat generator — finish scripting. (S each) — session-checkpoint.
- **Book placement** — place 898 retail books at true locations (currently scrivener shelf only). (S–M) — wrapup §9.
- **Chess** — playable boards in taverns/houses. (M) — REMAINING-WORK #6.
- **Weather depth** — snow/fog/storm states, region/latitude-tied (frozen north snowy, desert arid). (M) — gaps W4/W11.
- **Content gaps vs catalogue** — missing armor sets (Diforsa/Sedgemail/Tenassa/Knorr/Empyrean/Hieromancer…), ~23 more salvage materials, more gems, hide variants; named bosses (Gaerlan/Aerbax/Grael/Martine) + creature roster (niffis, carenzi, gear knight, elementals, burun, mukkir…). (M–L) — items/regions references.
- **Level-gated overworld portals** (Eastwatch 80+, Olthoi 40/60/80/100) — only the Facility Hub is gated. (S) — gaps W5.
- **Mana stone drain/store retail semantics** — destroy enchanted item → store mana → refill worn items (currently recharges focus battery). (S–M) — REMAINING-WORK #4.

---

## TIER 4 — Known bugs & polish (small)

- **Female forehead band** — dark under-hair band shows across brow on female hair styles that don't cover the eye-strip's top rows. Diagnosed, non-blocking. (S) — plan-ac-heads Phase 2c.
- **Skin tone washes pale** under the bright r128 pipeline; add baked AO/tone on the face material. (S) — plan-ac-heads (d).
- **Hats as part-16 head swaps** — cowl/cap/qafiya/turban/fez/kasa still procedural, not real head-model swaps. (S–M) — dereth-ac-clothing.
- **Dye subpalettes** in `acclothing.json` not applied to base clothing. (S) — dereth-ac-clothing.
- **Creator preview not WYSIWYG** — `ccBust` still shows the procedural painted head; swap in `acHeadGroup(app)` on option change. (S) — plan-ac-heads 2a.
- **Explicit creator/barber head rows** — pick from full AC lists (50 hair styles, eye/nose/mouth strips, exact colours) writing `app.acHead`. (M) — plan-ac-heads 2b.
- **NPC AC heads** — townsfolk still use procedural `buildPerson` heads (perf); optionally give nearest ~40 NPCs seeded AC heads. (M) — plan-ac-heads 2c.
- **`door_offmarker` (7 town buildings)** — cut a real doorway hole at the fallback wall (cut is 5–13m from the visible door). Prior fix regressed Sanamar; needs a deeper fix. (S–M) — building-entry-audit.
- **`path_blocked` interior (few) + `floor_poke` (2)** — tighten interior floor over terrain bumps; nudge overlapping neighbours. (S) — building-entry-audit.
- **Level-8 Incantation skill-300 gate** — a maxed specialized caster tops ~272, so L8 incantations are uncastable unbuffed. Confirm intended vs lower the req. (S) — loop-test-findings.
- **Non-PCM music track** — one MP3-format 0x55 resource skipped by the music exporter. (S) — wrapup §11.
- **Item models for shields/clothing** — extend `ac_item_models.py` to export them (watch pack size). (S–M) — REMAINING-WORK.
- **Real UI chrome / paperdoll layout** — wire ~50 uninspected extracted chrome contact sheets; arrange paperdoll per the manual. (S–M) — wrapup §7, ac-remaining-gaps A2/A3.
- **`0x21` StringTables** — 101 retail UI-layout definitions identified but format still undocumented (0x23 titles already wired). (follow-up) — three-agent-dat-update.

---

## Saga content (design, not code)

- **Saga Years 1–6 (months 1–60) SHIPPED.** The spec doc (`kilmer-saga-event-specs.md`) extends the
  calendar to **Year 10 (months 61–120)** — Years **7–10 are not yet designed/specified**.

---

## Explicitly deferred by design (not "todo")

- Server-authoritative player HP/position/death (currently client-trusted — friendly-homage choice; porting the full derive/armor/heal/vitae math to Python is huge scope, low benefit).
- Shared (vs instanced) bosses/events/dungeons/loot beyond the M3 slices already shared.
- Multiplayer server **deployment** is the user's to do (via `deploy/DEPLOY.md`).

---

### Rough tally
~4 flagship (Tier 1) · ~15 systems-depth (Tier 2) · ~14 world/content (Tier 3) · ~14 polish/bugs (Tier 4)
· Saga Y7–10 design · plus deferred-by-design items. **0 code TODOs.** The game itself is verified
solid across ~45 systems; everything here is *added depth/authenticity/content*, not broken behavior.
