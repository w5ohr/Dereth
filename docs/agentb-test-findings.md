# AgentB — full-game test loop findings

Automated headless sweep of the whole game (Chrome/SwiftShader via `harness.js` + `playlib.js`),
driving every subsystem through real code paths and asserting boundary/guard behavior. **Log only —
findings here are not fixed by the test runner.**

Harness lives in `~/.claude/jobs/9abf7777/tmp/agentb/` (`ab_depth*.js`, reusing the shared
`harness.js`/`playlib.js`). Server: local static `python -m http.server 8795` serving the repo root
(distinct port from the concurrently-running :8799/:8787 so agents don't collide).

---

## Run 2026-07-07 (~03:35–), HEAD `cbce433`

### ✅ Baseline full sweep — CLEAN
`tsb_full.js`: **29 steps, 0 real findings, 0 page/console errors.** Elite + world boss (Gnawvil, the
Olthoi Queen) both killed with kill-counters incremented and loot dropped; create/vendor/save-load/
potions/magic/missile/travel/dungeon/tinker/buffs/allegiance/housing/quests/elemental/shield-block/
society/progression(→275+enlighten)/caps/skill-key-guard/icons all green. The only flag was a known
`info` (fresh light-train returns false when skill credits already spent — not a regression).

### ✅ Previously-documented backlog — ALL VERIFIED FIXED at this HEAD
The Lane A combat/magic merge (`#27 #28 #29 #30`) resolved every open bug from the prior logs:

| Prior finding | Status | Evidence |
|---|---|---|
| **[HIGH] dual-wield double-kill/double-loot** (testsystemb #1) | ✅ FIXED (#27) | `applyHit` now opens `if(!m||m.hp<=0) return;` (index.html:10024) — a corpse can't be re-hit. |
| **[LOW] `drinkPotion` negative `amt` drains a vital** (testsystemb #2) | ✅ FIXED (#28) | `amt=Math.max(0,+amt||0)` (index.html:21435). Re-verified: −999/NaN heals now no-op. |
| **weapon oil / Might elixir leak onto SPELL damage** (loop-findings pass 16–17) | ✅ FIXED (#29) | `buffMightT ×1.5` and oil (`itemBuff("oildmg")`) are gated behind `opts.phys` (index.html:10027–10028). |
| **Heart-Seeker weapon crit leaks onto SPELL crit** (loop-findings pass 18) | ✅ FIXED (#29) | crit line gates `gearFx.crit` behind `opts.phys?…:0` (index.html:10052). |
| **focus-mana battery not refunded on fizzle/abort** (loop-findings pass 21) | ✅ FIXED | `refundCast=()=>{ player.mn=_mn0; player.focusMana=_fm0; }` restores the battery (index.html:15086–15087). |

### Depth/boundary sweep #1 (`ab_depth.js`) — 1 hardening finding
Exercised guard clauses across potions, applyHit, playerHurt, inventory, XP, slash-commands, spell-cast,
save/load-corrupt, encumbrance. **All green except one input-sanitization gap (not reachable in normal
play):**

- **[LOW · defensive-hardening · NOT player-reachable] `gainXP(amt)` doesn't sanitize `amt`.**
  `gainXP` (index.html:14767) does `player.xp += amt; player.xpUnspent += amt;` with no guard:
  - `gainXP(NaN)` → `player.xp`/`player.xpUnspent` become **NaN**, which serializes to `null` in the
    save blob and poisons all downstream level math.
  - `gainXP(-5000)` → drives `xpUnspent` **negative**.
  - **Reachability: NOT reachable via normal play.** Audited all **61 BESTIARY kinds** and **324 QUESTS** —
    every one carries a finite numeric `xp`, so the kill path `(m.xp||b.xp)*sMult` and quest turn-ins can
    never feed NaN/undefined. `sMult` is always `≥1`. Network `msg.xp` sites (index.html:25011/25125) are
    guarded by `if(msg.xp)` (falsy NaN blocked) and `m.xp|0` (index.html:24965) coerces to int.
  - **Assessment:** exact sibling of the already-fixed `#28` `drinkPotion` clamp. Same class, same fix
    shape. Recommend the same one-line hardening for consistency:
    `amt = +amt; if(!isFinite(amt)||amt<0) return;` (or `amt=Math.max(0,+amt||0)`) at the top of `gainXP`.
  - **Severity: LOW** — robustness/defense-in-depth only; no live exploit or crash in normal play.

### Depth/boundary sweep #2 (`ab_depth2.js`) — CLEAN
11 subsystems, **0 findings, 0 errors:**
- `combineChance` clamps to [0,1] at −100 and 999999 difficulty (no NaN).
- **960 loot rolls** (tiers 1–8, elite+normal) — **0 malformed** (no bad/negative `v`, no empty names).
- Pets spawn + despawn (double-despawn safe). Ships: board API + 3 ship types present.
- Housing: refuses tier-3 with 100 gold (no negative-gold buy), bad deposit/withdraw indices safe.
- Society rep does not go negative on `grantSocietyRep(-99999)` (clamped at 100).
- **Achievements idempotent** — rechecking at 100k kills does not re-award (5 → 5 badges).
- Weather + day/night advance 50 ticks each without NaN.
- **Mob AI:** 15 drudges × 30 update ticks — **0 NaN positions**, no throws.

### Depth/boundary sweep #3 (`ab_depth3.js`) — CLEAN
9 subsystems, **0 findings:**
- **Vitae caps correctly at 0.40** (40% penalty) after 30 consecutive deaths — AC-authentic, no NaN, no runaway.
- Augment catalog (29 items), colosseum ticket well-formed, casino 100-play loop keeps gold finite.
- Portal storm keeps position finite; `releaseCharge` with no charge safe; trade window open/close safe.
- Dungeon enter (Holtburg, 9 mobs, chest) / exit clean.

### Re-verified suites (existing `tsb_*` harness) + investigated flags

- **Dungeon chest re-loot guard — SAFE (by inspection + code path).** `openChest()` (index.html:24440),
  `openVault()` (15862), `openLockedCache()` (15876) each open with `if(...opened) return; …opened=true`,
  and `interact()` gates on `!dungeonChest.opened && dungeonMobs<=0`. No second-open re-loot vector.

- **`tsb_depth5` `wieldRequirements` assert — TEST-HARNESS ARTIFACT, not a game bug (verified).**
  The flag was "a wieldable item failed to equip." Root cause: `equipItem` (index.html:14242) returns
  `false` on a wield-*refusal* but returns *nothing* (`undefined`) on *success* (weapon/worn/jewelry/focus
  all `return;`). The test asserts `equipItem(ok) === true`, which fails on the `undefined` success even
  though the item **does** equip. Confirmed empirically: `player.weapon` is correctly set to the equipped
  item in both the no-gate and level-met cases (`plainSwordEquipped:true`, `overlevelEquipped:true`).
  - The asymmetric return is **correctly consumed** by its only real caller, `applyItem`
    (index.html:14319): `return equipItem(it)===false?"keep":undefined` — a refusal keeps the item in the
    satchel, anything else counts as equipped. So no live caller is broken.
  - *Optional polish (not a bug):* have `equipItem` `return true` on the success paths for a symmetric
    contract, so future callers/tests can trust a truthy success.

### Existing `tsb_*` suites re-run against this HEAD — all flags investigated & cleared
Ran depth1–5, expand1–4 (67 more steps). Green except four flags, **all verified as test-harness
artifacts, not game bugs:**

| Suite flag | Verdict | Evidence |
|---|---|---|
| `tsb_depth3` drinkPotion(neg) "reduced hp" | **artifact** | Clean repro: hp 100→**101** (heal clamped to `healScale(0)=+1`), not dropped. The suite ran it after a step that had left `mhp<100`, so `Math.min(mhp,…)` capped hp down — a state-ordering artifact, not a drain. #28 clamp confirmed working. |
| `tsb_depth4` "spell components never consumed" | **artifact** | Probe used a nonexistent `player.comps` store / too few casts. Real store is `player.inv` items with `stat:"comp"`. Direct test: control **Flame Bolt II burned 335/1500**; **"Stamina to Mana Self I" (special `xfer`) burned 43/1500**. Burning works. |
| `tsb_depth4` "empty aetheria slot non-zero level" | **artifact** | `aetheriaSlotLevel(color)` is literally `return s?s[1]:75` — it returns the slot's *unlock-gate* char-level (75/150/225), by design, not the medallion's level. Test misread the API. |
| `tsb_expand2` `skyTime` threw "acClock is not defined" | **artifact** | No such global ever existed; the real time system (`gameTime`, `updateDayNight`, `DAYLEN`, `sun`) is all present and advanced 50 ticks cleanly in `ab_depth2`. |

**Bonus code-path audit (spell components):** `executeSpell`'s `special==="stam2mana"` branch
(index.html:15070) `return`s before `burnSpellComponents` (15111) — but the *only* spell using it
("Stam→Mana") has **0 reagents**, so nothing is skipped (`burnSpellComponents` no-ops on no-reagent
spells anyway). All other `special` handlers (xfer/revitalize/recall/corrupt/mnboost/infuse/regen)
run after the burn. No component-burn defect.

---

## Summary — HEAD `cbce433`

**0 live bugs across ~125 subsystem checks** (full sweep + 3 new depth sweeps + 9 existing suites).
Everything the prior logs flagged is fixed; every new/old flag this run either passed or was proven a
test-harness artifact. Two non-blocking, non-reachable hardening notes remain:

1. **[LOW] `gainXP(amt)` input clamp** — add `amt=+amt; if(!isFinite(amt)||amt<0) return;` for parity
   with the fixed `#28` `drinkPotion` clamp. Not reachable via any kill/quest/network path today.
2. **[polish] `equipItem` return symmetry** — `return true` on success (currently `undefined`) so the
   contract matches the `false`-on-refusal case. No live caller is affected (`applyItem` uses `===false`).

---

## Run 2026-07-07 (~04:55, loop pass 2), HEAD `cbce433` (unchanged)

`main` unchanged since pass 1. AgentC had pushed `agentc-finding-31`; per the standing directive I
**independently verified their finding** and extended coverage to previously-untouched mechanics.

### ✅ Corroborated AgentC finding #31 (attribute gems uncapped) — REAL & reachable
- **Confirmed:** `applyItem`'s catch-all `else if(player.attr[it.stat]!==undefined){player.attr[it.stat]+=it.v;…}`
  (index.html:14435) has **no ceiling**. 50 Strength Gems → Strength **70 → 320** (+250), far past
  `attrMaxRanks()` = 190, and it survives `derive()`.
- **Reachability quantified:** attribute-stat gems roll from normal loot at ~**11 per 4,000 rolls**
  (Focusing Stone, Coordination/Strength/Quickness/Willpower Gems). So a player can farm them to inflate
  an attribute without bound — a real balance exploit, exactly as AgentC filed. (Filed by AgentC as
  authoritative-list #31; logged here as independent confirmation, not a duplicate filing.)

### 🔎 Value-add: the uncapped-permanent-consumable *class* is otherwise closed
Audited every `applyItem` branch that writes a **permanent** stat. Only two do:
- **attribute gems** — the #31 exploit (real, reachable).
- **`it.stat==="armor"`** → `player.armor+=it.v` (index.html:14320): also uncapped, BUT **dead code** —
  the only `stat:"armor"` literal in the codebase (index.html:1109) is the *Impenetrability item-buff's*
  target field, not an inventory item. **0** `stat:"armor"` items in 4,000 loot rolls; no vendor/quest/
  named source. So this branch never fires in real play — latent, not an exploit.
- Everything else is bounded: hp/mn/st clamp to max; food/attrpot/attrgem/skillpot are **timed** buffs;
  augments are capped (`attrTotal`+50, `count` 60, per-field caps); aetheria is slot-bounded; packs
  capped at 7. **So #31 is the sole reachable member of its class** — fixing it (mirror `attrMaxRanks`
  in the gem path) closes the category.
- *Defensive note (not reachable):* a negative-`v` gem would *drain* an attribute (`+= it.v` with v<0),
  but no negative-`v` gem is generated — same non-reachable robustness class as the `gainXP` note.

### Depth sweep #4 (`ab_depth4.js`) — CLEAN (new mechanics)
- **Salvage-bag math:** work-average stays in [0,10] (9.9999 after 1e9 units; no NaN even from a
  0-unit/0-work bag).
- **Real combine engine:** recipe index loads **1,500** retail recipes; end-to-end craft
  (Smelting Pot + Iron Ore → Slag) consumes/produces correctly (inv 5→6), no throw.
- **Mana stone** charge/discharge and **dye picker** open without throwing.

**Net for loop pass 2: 0 new bugs; independently confirmed + bounded AgentC #31.** Loop continues.

---

## Run 2026-07-07 (~05:30, loop pass 3), HEAD `cbce433` (unchanged)

`main` still unchanged. Cross-checked other agents' fresh findings and extended client depth coverage.

### Cross-check: AgentA's server-suite findings — agree (harness false-alarms, server correct)
AgentA (`agentA-findings-20260707`) root-caused 4 failing `server/tsa_*` assertions as **test-harness
bugs, not server bugs** (stream desync in `recv_until`, event mobs spawned outside `ATTACK_RANGE`,
fellowship extreme-spread-equal by design, MOTD test swearing down-level). Their analysis is sound and
matches the game's documented rules; server protocol is green (`tsa_extended` 16/16, `tsa_fuzz` 9/9). I
did not re-file — the server side is well-covered by AgentA.

- ⚠ **Coordination note (not a game bug): two agents filed different findings both as `#31`** in
  `remaining-work-consolidated.md` — AgentC's #31 (attribute gems uncapped, a real game bug) and
  AgentA's #31 (server test-tooling false alarms). Their branches will conflict on merge; the numbers
  should be de-duplicated when consolidating.

### Depth sweep #5 (`ab_depth5.js`) — CLEAN
- **Corpse recovery:** death drops loot to a corpse; recovering it restores the item (inv 5→4→5), and
  a **second `recoverCorpse` does NOT duplicate** (no item dupe — the classic MMO exploit is guarded).
- **Vendor sell edges:** `gearSellPrice` is finite & non-negative for weapon/worn/trophy/stack/zero-value
  (137/62/8/3/3; a 0-value item floors at 3, no negative/NaN).
- **Item stacking:** 5× `addToInv` of a count-10 comp **merges into one stack of 50** (no phantom
  duplicate stacks, no NaN counts).
- **Emotes:** all 15 emote commands (incl. an invalid one) run through `handleSlash` without throwing.
- **Drop lifecycle:** 50 ground drops aged past the 45s expiry → **0 remain** (the #22 no-accumulation
  fix holds; meshes disposed, no unbounded litter).

**Net for loop pass 3: 0 new bugs.** Client + server now cross-verified across AgentA/B/C. The only
open game finding remains AgentC #31 (attribute-gem cap); my two hardening notes stand.
