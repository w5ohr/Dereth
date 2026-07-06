# TestSystemB — automated full-game playtest log

Recurring headless sweep of the whole game (cron `7,37 * * * *`, every 30 min). Each pass boots the
current `main` in headless Chrome/SwiftShader, drives **every subsystem** through the real code paths
via `playlib`, captures page + console errors (these are treated as bugs), and applies pass/fail
assertions so anything not working as expected is auto-flagged.

**This is a LOG ONLY — findings here are not fixed by the test runner.** Fixes are a separate task.
Harness: `$CLAUDE_JOB_DIR/tmp/tsb_full.js` (+ `tsb_verify.js` for isolating ambiguous flags).

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

## Standing notes for future passes (avoid re-flagging these)
- **Run `probeSystems` AFTER `create`** — the world (shops/portals/lifestones/NPCs) is empty until a
  character enters.
- **New characters start INSIDE the Training Academy tutorial dungeon** (`inDungeon=true`, intended).
  Combat tests spawning overworld dummies must set `inDungeon=false; inNetwork=false;` first, or the
  swing filter (`!!m.isDungeon!==inDungeon`) skips every test mob and combat loops burn to their guard.
- **Enlightenment gate is level 275**, not 100.
- **Strip pre-existing ammo** before missile tests (soldiers carry Steel Quarrels).
- **Run `skillKeyGuard` on a fresh/early character** with skill credits available, before `progression`.
