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

## AgentC pass 3 (deep item-roll validation) — 1 FINDING (attribute gems uncapped)

Exhaustive item-roll scan: **4,000 rolls across all 10 tiers**, validating every field + running the
description/icon renderers on each.

- **Roll integrity: CLEAN.** 0 throws, 0 missing names, 0 NaN/negative values, 0 weapons missing
  damage, 0 unknown weapon types; `gearDesc`/`gearIco` never threw. Scrolls (619) all valid
  (scroll+spellId). MMO harness 47/47 · 0 console errors · jsc clean.
- ⚠ **FINDING (filed as authoritative-list #31, AgentC AC-1): attribute gems raise attributes with
  NO cap.** The 4,000-roll scan surfaced ~10 items whose `stat` is an attribute name (Strength Gem,
  Focusing Stone, Willpower Gem, …). They ARE handled — `applyItem` (index.html:14435) does
  `player.attr[it.stat]+=it.v` — but with **no ceiling**, unlike the XP-raise (`attrMaxRanks`) and
  augmentation (`attrTotal` +50) paths. They roll from normal loot (`ITEM_BASE` 11828–11831), so a
  player can farm them and inflate an attribute without bound. Runtime-proven: 50 gems → Strength
  13→263, survives `derive()`. Low-moderate. Full detail + fix in the authoritative list #31.
  - *Investigation note:* the gem first read as "does nothing on Use" — that was a TEST artifact
    (I checked `player.attr.Strength` while the sampled gem boosted Coordination). With the matching
    attribute it works; the real issue is the missing cap, not brokenness.
