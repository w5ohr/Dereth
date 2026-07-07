# AgentC — game test loop log

Recurring 30-min test sweep (cron `*/30 * * * *`). Real defects get filed to the authoritative
list (`remaining-work-consolidated.md`) as numbered rows + a PR so every agent sees them; this file
is AgentC's per-pass evidence trail. Verification: `jsc` syntax + Claude_Preview headless runtime +
the 47-test MMO harness (`server/test_client.py`).

## 2026-07-06 22:36 — AgentC pass 1 (HEAD cbce433) — baseline after Lane A/B/C merges: ALL GREEN

Focus: regression-verify the freshly-merged Lane A combat fixes (#27/#28/#29/#30, commit 223a46f)
and confirm Lane C (#20/#21/#22) survived the merges.

- `jsc` SYNTAX OK · 0 console errors · MMO harness **47/47**.
- **#27 dual-wield double-kill guard** (`applyHit` `if(!m||m.hp<=0) return`): killing blow = 1 kill;
  off-hand follow-up on the corpse = still 1 kill (no double count). AoE unaffected — still damages
  all 4 live mobs (guard is per-corpse, not per-hit). ✓
- **#29 weapon buffs no longer leak onto spells**: oil (now `itemBuffs.oildmg`, writer+reader match)
  and Might elixir (`buffMightT`) both gated on `opts.phys`. Runtime: oil_spell = 1000, might_spell =
  1000 (NO leak); physical hits boosted; oil still works. Aetheria Destruction stays universal by
  design. ✓
- **#30 focus-mana battery restored on fizzle/abort**: new `refundCast()` restores pool AND battery
  from pre-cast snapshots (`_mn0`/`_fm0`), wired into all 15+ abort sites. Runtime forced-fizzle:
  pool 2→2, battery 1400→1400 (fully restored). ✓
- **Lane C intact**: `craftResultItem` single-def (#20); on-disk `acicons/index.json` = 4148 icons
  incl. the 292-item fix (#21, torch 06001072 present); all 20 deleted functions still gone (#22).
- Spell catalog 2265 (1032 recovered). Core loops present.
- **Note (non-defect):** the preview's `AC_ICONS.icons` read 3960 — a stale browser cache of the
  pre-merge index.json; the on-disk/served file is 4148. Fresh sessions load the current file.

**No issues found this pass.**

## AgentC pass 2 (deep spell-catalog integrity) — ALL GREEN, no defects

Exhaustive scan of the entire 2,265-spell catalog — the kind of brute-force pass that surfaces
latent edge cases.

- **Generators:** invoked **625** projectile `make()` and **63** `heal()` generators — **0 throws,
  0 NaN/Infinity** damage/heal. Every `cost` finite & non-negative; every `dur` finite. No spell has
  an unknown school→skill mapping that breaks casting.
- **Map integrity:** all **2,265** `SPELLBOOK_LIST` entries are present in the `SPELLBOOK` id-map
  (0 missing) — so no spell is silently uncastable via `executeSpell`'s `if(!s) return`.
- **Two investigations resolved as NON-defects (documented so future passes don't re-chase):**
  1. *"Summon Wisp — school 'summon' not in MAGIC_SCHOOLS."* Intentional: summon spells are gated by
     their `special:"summon"` handler + the Summoning skill (index.html:15302), not the standard
     school→skill map; `canCast` returns true for an unmapped school by design. Not a defect.
  2. *"Summon Wisp cast summoned no pet."* Purely a TEST-HARNESS artifact: the fresh test character's
     max mana (`mmn`) is 10 but the spell costs 30, so it correctly hit the "not enough mana" branch
     and refunded (0 mana spent, no pet). With max mana raised it spends 30 and summons a valid pet
     (dmg>0). Working as intended.
  - **Harness note for future passes:** `derive()` recomputes `player.mmn` from `player.vitals.mn` +
    attributes, so setting `player.mn`/`player.mmn` and *then* calling `derive()` wipes it. To give a
    test char mana, set `player.vitals.mn` BEFORE `derive()`, then `player.mn=player.mmn`.

**No issues found this pass.** (Two combat/spell subsystems now deep-verified; the applyHit/executeSpell
merges from Lane A are clean.)

## AgentC pass 4 (deep save/load roundtrip) — ALL GREEN, no defects

Loaded a character touching **every persisted subsystem** with distinctive values, saved via
`saveGame()`, corrupted live state, reloaded via `applySave()`, and diffed field-by-field.

- **Roundtrip: 100% CLEAN** (0 changed fields) once test values use the real shapes. Verified
  persisted intact: level/xp/gold/kills, attributes, vitals (raised), vitae, heritage, title,
  **aetheria** (slot obj), **augment** (hp/st/mn/attrTotal/count/crit/owned all survive the load
  normalizer), **society** (id + rep + ribbons-count + testsDone), equipped **weapon** with its
  brand affix + `bp`, **inventory** incl. scrolls, pkState, focusMana, enlightenment. Save blob = 92 keys.
- **Investigation note (non-defect):** a first pass flagged 4 "changed" fields — all were the TEST
  using wrong value SHAPES, not save bugs: `society` is stored as a validated **id string**
  (`SOCIETIES.some(o=>o.id===s.society)`, index.html:24815), not an object; `societyRibbons` is a
  **count number** coerced with `|0` (index.html:24817), not an array; and the `homestead` "change"
  was the legacy-hid **migration** (`hsPackInit`) correctly remapping a fake `"h123"` to a real
  cottage. Re-run with correct shapes → 0 diffs. Save/load is sound.
- jsc clean · 0 console errors · MMO harness 47/47.

**No issues found this pass.** (Finding #31 from pass 3 remains open in PR #157, pending review.)

## AgentC pass 6 (status-effect / buff-timer lifecycle) — ALL GREEN, no defects

Applied every timed effect, drove `update()` past expiry, and asserted the character returns to
EXACT baseline (the classic leak: a buff that expires without un-applying its delta).

- **Attribute buffs:** apply (+40 Str), fully expire back to base; **re-apply does NOT double-stack**
  (`applyAttrBuff` drops the old stack first); no residual after reapply→expire.
- **Skill buffs:** raise the skill, expire clean, no residual (derive re-runs).
- **prot / banes / itemBuffs (dmg+oildmg) / buffMightT / buffSwiftT:** all decrement and delete on
  expiry — 0 leaks.
- **HoTs:** tick + heal, don't overheal past max, then expire.
- **Monster burn DoT:** damages, stops at `burnT<=0`, no NaN.
- **Enemy debuffs on the player (slow/vuln/imperil):** decremented + deleted at index.html:16602
  (2→0 confirmed). Not missed by the update loop.
- Final character state = exact baseline (Strength 77→77, mhp unchanged). No drift, no leak, no NaN.
- jsc clean · 0 console errors · MMO harness 47/47.

**No issues found this pass.** (The buff/debuff/DoT/HoT lifecycle is sound — symmetric apply/expire
throughout.)

## AgentC pass 10 (net-handler completeness sweep + housing persistence) — ALL GREEN, no new defects

Systematic audit of ALL remaining client net-message handlers (to close out the surface behind
#32/#33/#34/#35) + housing/vault storage persistence.

- **Net-handler surface — now fully audited.** Read + malformed-message-tested every remaining
  state-mutating handler; **no NEW player-state corruption** beyond the three already filed
  (#32 onMobDmg, #33 onLoot, #35 trade-done):
  - **Well-guarded (good):** `onSpellFx` is EXEMPLARY — clamps/coerces every field (`+m.x||0`,
    `clamp(+m.r||0.3,…)`, `typeof m.c==="number"`); `onDropGone`/`onRemoteBuff`/`onCorpseLoot`/
    `onReward`/`onWho`/`onEmote`(esc'd) all validate or are display-only.
  - **Cosmetic-only unvalidated (low):** `onMobHit` (sets shared-mob hp → display NaN only),
    `onDrop` (stores amt in a drop; surfaces via `onLoot`=#33), `onMobDie` (guarded `b?b.c:…`).
  - `onPvp` clamps NaN dmg; any string-dmg edge is **subsumed by the #32 `playerHurt` fix**.
  - **Takeaway:** onSpellFx proves the codebase knows how to validate net input, so #32/#33/#35 are
    genuine omissions, not a systemic absence. No new finding warranted here.
- **Housing / vault persistence: CLEAN.** A hooked item (`homestead.hooks["1"]`) survives
  save→wipe→load; `hsHookCount`=1; all hook fns present. `kcVault` (castle hoard) item also survives.
- jsc clean · 0 console errors · MMO harness 47/47.

**No issues found this pass.**

## AgentC pass 11 (loot-tier scaling + targeting) — ALL GREEN, no defects (caught a false alarm)

- **Loot-tier scaling: CORRECT & well-designed.** Avg gear value rises **21.8 → 84.3** across tiers
  1→9 (**3.87×**), climbing steeply through tier 5 then plateauing (the AC_POOLS have 5 value bands).
  Legendaries are gated: **0 at tiers 1–4**, appearing only at tier 5+ (38/47/39/29/50). Progression
  loot works as intended.
- **⚠ Near-miss (my error, NOT a game bug):** a first run showed FLAT value across tiers and I almost
  filed "loot doesn't scale." Root cause was a TEST bug: `rollItem`'s signature is
  **`rollItem(rare, tier, noRare)`** — tier is the SECOND arg. I'd called `rollItem(tier, false)`, so
  every roll used `rare=<my tier>` (truthy) and `tier=false` (0). Corrected to `rollItem(false, tier,
  true)` → clean 3.87× scaling. **Lesson for future passes: rollItem tier is arg #2.** (Earlier passes'
  malformed-item checks are unaffected — those are valid for any args; finding #31 stands.)
- **Targeting: CLEAN.** `pickMonsterTarget` returns null with no mobs, picks the monster in the aim
  direction (not the one behind), and does not throw on a NaN-position mob.
- jsc clean · 0 console errors · MMO harness 47/47.

**No issues found this pass.**
