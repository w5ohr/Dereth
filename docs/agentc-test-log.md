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
