# TestSystemB — automated full-game playtest log

Recurring headless sweep of the whole game (cron `7,37 * * * *`, every 30 min). Each pass boots the
current `main` in headless Chrome/SwiftShader, drives **every subsystem** through the real code paths
via `playlib`, captures page + console errors (these are treated as bugs), and applies pass/fail
assertions so anything not working as expected is auto-flagged.

**This is a LOG ONLY — findings here are not fixed by the test runner.** Fixes are a separate task.
Harness: `$CLAUDE_JOB_DIR/tmp/tsb_full.js` (main sweep) + `tsb_expand.js` (extra subsystems) +
`tsb_verify.js`/`tsb_brand.js` (isolating ambiguous flags).

**Standing directive (user, pass 4):** while `main` is unchanged and the existing sweep is clean, each
new pass should **extend coverage** to subsystems not yet tested rather than repeat a clean run. When an
agent merges a change, run the sweep against the changed area too.

**Still-untested targets for upcoming passes:** walking into an overworld *portal* to transit (recall
set is done) · portal storms · Portal/Lifestone *Sending* spells (special:"sending") · creator head
choices (barber done). *(Covered through pass 6: + recall set, drowning, Colosseum arena, Town Crier,
barber, Aetheria surges. Fellowship dropped — chat channel only.)*

Every entry is tagged **[TestSystemB]**. Verify a flag before trusting it — several "failures" are
test-ordering / setup artifacts, not game bugs (see the standing notes at the bottom).

---

## [TestSystemB] Pass 1 — 2026-07-06 ~23:01

**Result: CLEAN.** 28 subsystems exercised, **0 real game bugs**, **0 page/console errors**, 0 uncaught
exceptions. Four assertions tripped; all four verified in isolation (`tsb_verify.js`) as
**test-harness artifacts, not game defects**:

| Flag | Verdict | Root cause |
|---|---|---|
| `systems: no shops seeded` | **not a bug** | `probeSystems` ran *before* character creation. The world seeds on entering the game — post-create there are **206 shops, 218 portals, 185 lifestones, 694 NPCs, 56 cities**. |
| `missile: ammo not consumed / fired when empty` | **not a bug** | Soldiers start with a **Steel Quarrel** stack (crossbow ammo, count 30). The sweep's `findIndex(stat==='ammo')` (no `for` filter) measured/zeroed *that* stack, not its own bow-arrow stack. In isolation, bow ammo goes 10→9 per shot, drains to 0, and firing while empty is correctly blocked. Consumption + no-ammo guard both work. |
| `progression: enlightenment did not apply` | **not a bug** | `enlighten()` correctly requires character level **275** (`index.html:18385`); the progression test only climbed to 100, so the gate refused as designed. **Test assertion used the wrong threshold** — corrected expectation to 275 for future passes. |
| `skillKeyGuard: valid skill train regressed` | **not a bug** | `trainSkill('light')` succeeds fine early (credits 34, tier→1). It failed only in the full sweep because it ran *after* `progression` maxed the character and an earlier step already trained `light` — skill credits exhausted / already trained. State-ordering artifact. |

### Subsystems confirmed working (28)
systems inventory · character creation · base melee damage · melee grind + XP gain · XP spend
(attrs/skills/vitals) · vendor buy + equip + armor gain · save/load roundtrip · potions (heal + empty
guard) · War magic (cast bar + projectile) · direct-cast war/life/attr · missile (bow fire, ammo
consume, no-ammo block) · fast-travel · dungeon enter/combat/exit · salvage · weapon+armor tinker ·
attr/skill/prot/item buffs · allegiance swear/pass-up/break · housing buy/deposit/withdraw · quest
accept/progress/turn-in · elemental weak/resist/neutral mults · shield block reduction · elite +
boss kills (championKills/bossKills) · society pledge/rep/second-pledge-block · progression to L50
(Luminance) & L100 · attr/vital/tink caps · invalid-skill-key guard · **item icon coverage (0 emoji
fallback across named/epic/quest/all-tier loot)**.

### Minor observations (not bugs — for a human glance)
- **Aluvian soldiers spawn with "Steel Quarrel" (crossbow ammunition) but no crossbow.** Harmless, but
  slightly odd starting gear — worth a look at whether the starting-kit table intends this.
- Character boot under headless SwiftShader is slow (~19–23 s) and each `page.evaluate` round-trip
  can take 15–28 s while the render loop is saturated. This is an environment/measurement note, not a
  game issue — the game itself is responsive.

---

## [TestSystemB] Pass 2 — 2026-07-06 ~23:10

**Result: CLEAN.** Same code under test (`main` unchanged at `c3232dc` since pass 1). 28 subsystems,
**0 real game bugs**, **0 page/console errors**, 0 uncaught exceptions. Five assertions tripped: the
same four artifacts from pass 1 (world-probe-before-create, pre-existing crossbow ammo, L275
enlighten gate, exhausted skill credits), plus **one new flake**:

| Flag | Verdict | Root cause |
|---|---|---|
| `elemental: weak did not take more elemental dmg` | **not a bug** | Unit mults are exactly correct (mosswart fire-**weak** 1.5×, tusker fire-**resist** 0.5×, neutral 1.0×) and the melee hit path *does* apply them (`index.html:10097` — `dmg += opts.brandDmg * em`). The integration flake compares **two different creatures** (mosswart avg 516 vs tusker 527) whose differing physical armor dominates; a fire-brand's elemental part is only ~25% of the hit, so 1.5× vs 0.5× on that slice is swamped at n=5. Test methodology, not a defect. |

Combat, magic, missile, and all other subsystems reproduced pass 1's clean state. Randomized paths
(loot rolls, per-swing variance, icon audit) showed no flakiness beyond the elemental-integration
noise above.

---

## [TestSystemB] Pass 3 — 2026-07-06 ~23:25

**Result: CLEAN.** `main` still unchanged (`c3232dc`). Rather than log the same artifacts a third time,
I **hardened the harness** to kill the false positives and — importantly — investigated the one flag
that looked like it might be real. After hardening: **29 subsystems, 0 assertion findings, 0
page/console errors, 0 uncaught exceptions** (only 1 benign `info` note remains, by design).

### Investigated and cleared (was NOT a bug)
- **`elemental: fire-brand added no elemental dmg`** (pass 3 first run showed brand 480 ≈ plain 481).
  Ran a dedicated isolation probe (`tsb_brand.js`) across three paths — **all confirm the brand works**:
  - direct `applyHit`: 160 → **353** with brand (fire-weak ×1.5, brandDmg=100 adds ~+193)
  - hand-assigned weapon via `meleeAttack`: 406 → **457**
  - real `equipItem` path via `meleeAttack`: 308 → **436**
  - `meleeAttack` correctly translates the affix (`index.html:15540`) and `applyHit` applies `elemMult`
    to it (`index.html:10097`). The earlier "flake" was **crit variance** (2× on ~11% of hits) swamping
    a 25%-of-hit signal at n=8. Fixed the test to average `applyHit` over N=120 → now a clean
    **123 → 316** signal every run.

### Newly exercised end to end (previously never reached)
- **Enlightenment endgame** now fully driven: the test satisfies all real prerequisites
  (level **275** + Society **Master** + all Luminance Auras maxed) and `enlighten()` fires correctly —
  **level 275 → reset to 1**, `enlightenment` 0 → 1, title **"Awakened"**, 5 auras retained. The full
  prestige-reset path is confirmed working; earlier passes only reached L100 and hit the gate as designed.

### Harness improvements applied (test tooling only — no game code touched)
- `probeSystems` world counts (shops/portals/lifestones/NPCs) now checked **after** create (`worldSeeded`
  step) — post-create the world is fully populated (206/218/185/694).
- Missile test strips pre-existing ammo (soldiers carry Steel Quarrels) and empties the stack for the
  no-ammo guard the way `takeFromInv` really does (removes the stack at 0, not a lingering count:0).
- Elemental integration → deterministic large-N direct `applyHit` comparison.
- Progression climbs to the real L275 cap with large XP chunks and sets up every enlighten prerequisite.
- `skillKeyGuard` asserts the true P3-1 invariants (no throw / no save-pollution / credits intact /
  bad-key returns false / valid `heavy` still resolves); the fresh-train-returns-false case is logged
  as `info`, since it only reflects skill credits already spent by the `progression` step.

### Minor observation (not a bug — for a human glance)
- `fireArrow`'s no-ammo guard checks for the **existence** of a matching ammo item, not `count > 0`
  (`index.html:15475`). In normal play this is safe because `takeFromInv` splices a stack when it hits
  0, so a `count:0` stack never lingers. But if any future code path ever leaves a zero-count ammo
  stack in inventory, a shot could fire "for free." A defensive `&& x.count > 0` in the guard would
  harden it. Very low risk today.

---

## [TestSystemB] Pass 4 — 2026-07-06 ~23:46

**Result: CLEAN.** `main` still unchanged (`c3232dc`) and the hardened full sweep was already proven
clean in pass 3, so re-running it would only repeat "clean." Instead I **expanded coverage**
(`tsb_expand.js`) to six subsystems the main sweep never touched — raising the odds of catching a
latent bug. **All six pass: 0 findings, 0 page/console errors, 0 uncaught exceptions.**

| New subsystem | Verified behavior |
|---|---|
| **Death / corpse / respawn** | Fatal hit → `alive:false`, hp 0 → **corpse drops** (0→1) → **vitae penalty** 0.05 applied → respawns **at lifestone**, hp 32/38, **invuln 60**. Full loop works. |
| **Damage Rating / Crit-DR / Crushing / rear** (Lane A #1) | Rear attack **+0.20 DR** (front 0 → rear 0.20), Aura of Valor 50 → **+0.50**, gear dr 0.15 added, Aura of Glory raises crit-damage, bludgeon crush 0.13 > blade 0.03, `isBehind` geometry correct. |
| **Mana Stone battery** (#17, incl. the custom player-mana→stone) | `manaConvRate` 0.30 (in [0.30,0.90]); **draw-from-own-mana banked exactly 60** of 200 reserve (200×0.30) into the stone — the "use a stone on nothing" conversion; discharge dispensed 150 into reserve; consume-item banked 120. |
| **Ships** | Bought a skiff → **spawned on open water** → boarded (correct deck height) → disembarked to shore. |
| **House recall** | Bought a tier-1 estate → `recallHome()` teleported the player across the map. |
| **Chess** | Board opens without error. |

**Cumulative coverage: 35 subsystems, still zero real game bugs.** (Main sweep 29 + expansion 6.)

---

## [TestSystemB] Pass 5 — 2026-07-07 ~00:11

**Result: CLEAN.** `main` still unchanged (`c3232dc`); continued extending coverage per the standing
directive. Batch 2 (`tsb_expand2.js`) adds **7 more subsystems — all pass, 0 findings, 0 errors.**

| New subsystem | Verified behavior |
|---|---|
| **3-state PK + 3-day lock** | `PK_LOCK_MS` = 3 days; `fmtLockLeft` → "3d 0h" / "1h 30m"; swearing at the Bael'Zharon altar sets `pkState=pk`, `pk=true`, lock; while bound the Asheron altar **refuses the switch to npk**; after the lock expires the switch succeeds. |
| **Sky / time / AC calendar** | 12 months, 16 tithes, `DAYLEN`=7620s; noon → **Midsong / 12:00**, midnight → **Darktide / 00:00**; `updateDayNight` advances time; **P.Y.**, 360-day year, zero-year 10, month "Morningthaw". |
| **Weather cycle** | `updateWeather` rotates through valid states (clear/cloudy/fog…); drowning breath meter present. |
| **Combine engine** | **1,500 recipes loaded**; Smelting Pot + Iron Ore → **Slag** combined on the first try (chance 0.98); `craftCanCombine` true for a real tool. |
| **Dye** | `applyDye` succeeds with Cooking + materials and sets `player.dye`; refused at 0 materials. |
| **Title system** | `acTitle` capitalizes ("Lord of the Northern Reach"); allegiance titles Yeoman→**High King**, female rank-10 → **High Queen** (distinct), rank-0 → null. |
| **Aetheria set bonuses** | Growth **+18 hp** (3×6), Vigor **+10 st/mn** (2×5), Defense **+32 armor** (4×8), Destruction **+0.10 dmg** (5×0.02); empty medallions → 0. |

**Cumulative coverage: 42 subsystems, still zero real game bugs.** (Sweep 29 + expand 6 + expand2 7.)

---

## [TestSystemB] Pass 6 — 2026-07-07 ~00:45

**Result: CLEAN.** `main` still unchanged (`c3232dc`); continued extending coverage (`tsb_expand3.js`).
Six more subsystems verified — **0 real game bugs**, **0 page/console errors**.

| New subsystem | Verified behavior |
|---|---|
| **Recall set** (Item Enchantment) | With Item magic trained (skill 344): **Lifestone Recall teleports**; **Portal Recall** refuses with no tie and **teleports to the tied portal** when one is set; **Secondary Portal Recall** refuses with no secondary tie. All four spells present (Lifestone/Portal/Secondary + Sending exists). |
| **Drowning** | Underwater (`y<-1.5`) breath drains to 0 in ~14 s → **drown damage** ticks (ignores armour) → **breath refills** at the surface. |
| **Colosseum arena** | `enterColosseum` sets `inDungeon`+`arenaActive=true`, 5 waves, spawns wave-1 mobs; `exitDungeon` resets both to false. *(Verified in an isolated session — see the artifact note below.)* |
| **Town Crier** | `crierRumors()` returns a live rumor list (count 4); the PK-flagged line appears when `pkState==="pk"`. |
| **Barber** | Opens without error. |
| **Aetheria surges** (proc side) | A Destruction-surge medallion **procs** an item damage buff (`itemBuffs.dmg` = 0.25); 5 surge defs present. |

### Two flags investigated → both NOT game bugs
- **`recallSet` first showed "Lifestone Recall did not teleport"** — **test-setup flaw.** Recall spells are
  `school:"item"`, `req` 30/95/150; `executeSpell` bails at `canCast("item")`/skill-req before the
  teleport, which also made the "no-tie blocked" checks pass *vacuously* (the player never moved for any
  reason). Fixed the probe to train Item Enchantment (skill→344) → all recalls teleport correctly.
- **A `crierRumors()` call HUNG when it ran right after the Colosseum** (batch-3 first attempt) — **headless
  artifact, NOT a logic loop.** Decisive isolation (`tsb_decide.js`): a **pure-compute eval** (no game
  code) *also* times out after the arena, and the *same* loop ran in 11 s *before* it — while
  enter/exit state stays correct and fast (arenaActive true→false in ~0.4 s). `buildArena`'s heavy scene
  saturates the SwiftShader render loop and starves every subsequent eval in that session. **Harness rule:
  run the Colosseum/arena step in its own short session, never before eval-dependent steps.**

**Cumulative coverage: 48 subsystems, still zero real game bugs.**

---

## Standing notes for future passes (avoid re-flagging these)

*(All of the following are now handled by the hardened harness as of pass 3 — kept here as the record
of WHY each test does what it does.)*

- **Run `probeSystems` AFTER `create`** — the world (shops/portals/lifestones/NPCs) is empty until a
  character enters.
- **New characters start INSIDE the Training Academy tutorial dungeon** (`inDungeon=true`, intended).
  Combat tests spawning overworld dummies must set `inDungeon=false; inNetwork=false;` first, or the
  swing filter (`!!m.isDungeon!==inDungeon`) skips every test mob and combat loops burn to their guard.
- **Enlightenment gate is level 275**, not 100.
- **Strip pre-existing ammo** before missile tests (soldiers carry Steel Quarrels).
- **Run `skillKeyGuard` on a fresh/early character** with skill credits available, before `progression`.
- **Elemental integration must compare the SAME creature** (fire-brand vs plain, or subtract a
  no-brand baseline) — comparing a fire-weak vs a fire-resist *different* creature conflates fire
  affinity with their differing physical armor. The unit `elemMult` check (1.5/0.5/1.0) is the
  authoritative signal; the cross-creature averaged integration is noise at small n.
