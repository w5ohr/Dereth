# Dereth Playtest — Issue Log (2026-07-05)

Autonomous headless playtest driving the real game (`index.html`) in headless Chrome
(SwiftShader), exercising real code paths (`meleeAttack`, `gainXP`, vendor buy handlers,
skill/attr raising, etc.). Verified by game state, not screenshots (per the known headless
thin-line rendering limitation).

Harness: `$CLAUDE_JOB_DIR/tmp/{harness.js, playlib.js, session_*.js}` — boots the game,
creates a fully-allocated character, and scripts play. All console/page errors are captured;
gameplay-relevant ones are logged below.

Severity: **P1** = crash/blocker or clearly wrong mechanic · **P2** = incorrect/confusing
behavior · **P3** = polish/minor · **INFO** = observation, not necessarily a bug.

---

## VERDICT (6 sessions, ~40 systems)

**The game is exceptionally solid — zero player-facing bugs found.** A full character was played from
creation to the level-100 endgame and back through Enlightenment, exercising real code paths the whole
way. Combat (melee/missile/magic/elemental/shield/elite/boss), the full progression economy
(XP → level/skill/attr/vital, caps), vendors (armor/weapon/jewelry), tinkering & salvage, save/load,
death/corpse/respawn/Vitae, potions, buffs, dungeons, quests, allegiance, housing, society, and the
Luminance/Aura/Enlightenment endgame all behave correctly.

The **only** code issue is a latent, non-player-reachable robustness gap (**P3-1**, below). Everything
else initially flagged turned out to be a harness test-input artifact (documented under "Candidates
investigated → NOT bugs"). No crashes, no incorrect mechanics, no economy exploits observed.

---

## Environment notes (NOT game bugs)

- **`WrongDocumentError: ... not valid for pointer lock`** — thrown by `cEl.requestPointerLock()`
  in `startGame()` under headless (no user gesture). Harmless; in a real browser this runs
  inside a click handler. Ignored by the harness.
- Boot is heavy: `startGame()` → `buildWorld()` builds the ~4M-vertex terrain (GROUND_SEG 2000)
  synchronously, ~30s under software WebGL. Fine for real GPUs; only a harness cost.
- **Sparse headless frames + `dt` clamp skew time-based tests.** `loop()` clamps `dt=Math.min(dt,0.05)`.
  Under SwiftShader with a heavy scene (many monsters), frames are delivered slowly, so *game time*
  advances far slower than wall-clock (e.g. 2.5s of real waiting produced well under 0.4s of game
  time). This made a magic cast look "stuck" (`player.casting` still set, no mana spent, no
  projectile) — it simply hadn't reached `castTime`. NOT a game bug; the render loop is confirmed
  alive (`meleeCd` decays, `lastTs` advances). Lesson: verify spell/projectile *effects* via the
  instant path (`executeSpell`) and give generous waits for cast-bar/projectile travel.

---

## Systems verified WORKING (no bug)

Driven end-to-end through real code paths; game state observed:

- **Melee combat** — `meleeAttack` targeting/reach/hit-roll, damage scales with Strength + Heavy
  skill (296 → 598 → 954 dmg as attrs/skill rose), streak-XP multiplier, crit, enrage.
- **XP → progression** — `gainXP` level-ups, `raiseSkill`/`trainSkill`/attribute & vital raises via
  the real character-sheet handlers; XP/credit accounting exact; buttons disable when unaffordable;
  attribute raises correctly recompute dependent skill values.
- **Vendors** — armorer (armor 0→127, 8 distinct plate pieces equipped to correct slots), weaponsmith,
  jeweler (cantrip jewelry equipped). Gold deducted correctly; `equipItem` swaps + displaces.
- **Save/Load** — `saveGame`→`applySave` roundtrip restores level/xp/gold/attrs/inv/kills exactly.
- **Death** — corpse drop with items+gold, respawn at bound Lifestone at 75% vitals, +5% Vitae per
  death, 60s res-protection. Flawless.
- **Potions** — health/stam/mana heal + cap at max + empty-guard.
- **Magic (War + Life)** — `war_flame_1` fires a projectile & spends mana; `life_heal_1` restores HP.
- **Missile** — bow equip flips `weaponMode`, `fireArrow` spawns a ballistic projectile and consumes
  matching ammo (`takeFromInv`); no-ammo guard holds (a stack is removed at count 0, so a 0-count
  stack can't linger in real play).
- **Dungeon** — `enterDungeon` builds the instance (7 guardians, chest, exit), dungeon melee kills
  decrement the hoard counter (7→6), `exitDungeon` returns to overworld.
- **Salvage** — `salvageItem` accrues units into per-material bags with units-weighted workmanship.
- **Travel** — `arriveAt` map fast-travel repositions the player.
- **Buffs (all types)** — Creature attr buff (Str +10, stored in `spellBuffs` with a timer), skill-aptitude
  buff (`skillBuffs`), Life Protection (`lifeBuffs.prot`), Item enchant (`itemBuffs.dmg`). Each registers
  and has expiry logic in the update loop.
- **Tinkering** — with a *matching* material class: weapon tink (Battle Axe v7→14, tinks→1), armor tink
  (Cuirass v20→29, tinks→1). Trained tink skills raise success.
- **Allegiance** — swear to a patron (generates patron + monarch + court name), vassal pass-up accrues
  `vassalXP`, pass-up XP does NOT burn Vitae (AC-correct), break fealty clears the patron.
- **Housing** — buy an Apartment (tier 0→1), deposit an item into storage, withdraw it back.
- **Quest engine** — accept a slay bounty, `questEvent` progresses objectives, completion detected,
  `talkQuestGiver` turn-in pays XP (1400) + gold (90), marks done, drops from the journal.
- **Elemental weakness/resistance** — `elemMult` returns ×1.5 (weak), ×0.5 (resist), ×1 (neutral); a
  fire-brand weapon dealt avg 554 to a fire-weak mosswart vs 263 to a fire-resist tusker.
- **Shield block** — holding block cuts frontal melee damage sharply (36→11, ~70%), scaled by Shield skill.
- **Elite + boss** — elite spawn flag, `championKills`++; `spawnBoss` ("Gnawvil, the Olthoi Queen", 2000hp)
  killable, `bossKills`++, boss loot drops.
- **Society** — pledge to an order (Celestial Hand), earn standing via `grantSocietyRep`, second-order
  pledge correctly blocked (you may swear to only one).
- **Endgame progression** — leveled 1→50 (Luminance unlocks) → 100. Luminance caps at 1,000,000.
  Auras purchasable and stackable (Valor ×2, Protection ×1; 20k lum each). Enlightenment resets level
  100→1, grants prestige tier ("Awakened"), and **keeps auras** (AC-correct).
- **Progression caps all enforced** — attributes at **+190 ranks** (Str innate 70 → 260, then locked),
  vitals at **+196 ranks**, tinkering at **10 per item** (11th rejected).

## Candidates investigated → NOT bugs (test-input artifacts)

- **Missile ammo "not consumed" / 0-count fires** — my harness read the wrong ammo stack (soldier-kit
  crossbow quarrels vs. my bow arrows) and forced an impossible `count:0` stack. `fireArrow` does
  consume ammo; `takeFromInv` removes a stack when it can't decrement below 1.
- **Attr buff "no-op"** — the Creature school was never trained (ran out of *skill credits*, which come
  from leveling — the granted XP doesn't buy credits), so `canCast('creature')` correctly rejected
  the cast. Confirmed positively in session 4 by force-training the school.
- **Tinker "no-op" on the bow** — `MAT_APPLY['Steel']` resolves to **armor** (the armor-material loop
  runs last and wins), so applying a Steel bag to a weapon is a no-op by design. Weapon tinks need a
  weapon-class material (e.g. Titanium). Confirmed in session 4.

## Findings

<!-- newest first; append as found -->

### P3-1 — `skillState()` / `trainSkill()` don't guard against an invalid skill key — ✅ FIXED (2026-07-05)
`skillState(key)` does `player.skills[key] || (player.skills[key] = {t:0,xp:0})` — so calling it
with a key not in `SKILL_BY_KEY` silently creates a junk skill entry in the save. `trainSkill(key)`
then reads `SKILL_BY_KEY[key].tc` and throws `TypeError: Cannot read properties of undefined`.
Not reachable through the normal UI (buttons are built from `SKILLS_DEF`, so keys are always
valid), so **not player-facing** — but it's a latent robustness gap: a stale save, a renamed
skill key, or any future code path passing a bad key corrupts `player.skills` and can crash.
Cheap fix: early-return/guard in `skillState`/`trainSkill` when `!SKILL_BY_KEY[key]`.
(Found because my harness passed `'axe'`; the real weapon-skill keys are `heavy/light/finesse/
twohand/missile`, there is no per-weapon `axe` skill.)

**Fix applied:** guarded `skillState` (returns an inert, never-persisted `{t:0,xp:0}` for an unknown
key so the save can't be polluted) plus `skillBase`/`skillValue`/`skillEff`/`trainSkill`/`specSkill`
(each now checks `SKILL_BY_KEY[key]` before dereferencing `.d`/`.tc`/`.sc`/`.base0`/`.fam`). Verified:
an invalid key now returns safe defaults (tier/rank/base/value→0, eff→1×, train/spec/raise→false) with
no throw and no save pollution; valid keys are unaffected (heavy value 46 / eff 1.8× unchanged, train
& raise still work, full grind→spend-XP path regression-clean).

### INFO-1 — Kill→respawn has no global monster cap (self-limiting in normal play)
Each overworld kill schedules exactly one `spawnSomewhere` 2.5–6s later (`killMonster`, ~line 9005).
In normal play that's 1:1 replacement — you kill the ambient monster, one respawns → stable.
My grind inflated the count (5→57) only because it killed *throwaway spawned dummies*, so each
kill still queued a real ambient respawn with nothing removed on the ambient side. So the 57 is a
**test artifact, not a live bug.** Caveat worth a look: there is no hard ceiling on `monsters.length`,
so if a mechanic ever spawns faster than the player clears, the population can grow unbounded.
Low priority — flagging only for awareness.
