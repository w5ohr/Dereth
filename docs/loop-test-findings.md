# loop-test-findings.md — RETIRED

This document has been consolidated into the **single authoritative to-do list**:

➡ **[remaining-work-consolidated.md](remaining-work-consolidated.md)**

All still-open items from this file were reconciled, code-verified, and moved there on 2026-07-06.
Its full historical content remains in git history. **Do not add new items here** — add them to the
authoritative list.

## 2026-07-06 17:44 — TestSystemC pass 1 (HEAD c3232dc)

**TestSystemC** · log-only (no fixes) · 30-min loop active.
- Syntax OK · servers up (web 200 / game UP) · **0 console errors** · MMO harness **47/47**.
- Spell catalog: 2,265 total (1,032 recovered, 498 Others) — 0 duplicate ids, 0 bad values. Vital
  transfer works (Stamina→Mana III drains+grants); recovered ward ("Lesser Corrosive Ward") applies.
- Systems present + functional: combine (Arrowheads+Shafts→Arrow ✓), aetheria, real hats, dye
  subpalettes, registered shield, weapon-skill mapping, quest lifecycle 3/3 turn-ins.
- Dungeons: enter/descent/exit OK. **TestSystemC observation (timing, not a defect):** on FIRST
  entry to an EnvCell dungeon the build is async (prefetch+resume) — dungeonDescent/chest read
  false for a beat before the resumed build lands. Automated tests must settle ~1s; players never
  see it. No other findings this pass.

## 2026-07-06 18:02 — TestSystemC pass 2 (HEAD c3232dc, unchanged since pass 1 — varied coverage)

- Syntax OK · servers up · 0 console errors.
- **Aetheria functional:** Growth L5 slots (+30 hp exact), forced Protection surge applies.
- **Recovered spells behave:** "Winter's Embrace" rolls valid frost damage (41); "Mana Drain Self I"
  correctly DRAINS 5 mana — retail's own boost is [-4,-6] ("Drains 4-6 points of the caster's
  Mana"); the materializer is faithful. Not a defect.
- **Save/load:** gold + the new aetheria slots persist through the save blob.
- **Vendor economy** (stock+sell price), **spell-landing FX** (spawn/dispose), **castle** (77 chests /
  35 mega), **ship** (board + disembark ashore) all pass.
- **No new findings this pass.**
- MMO harness: 47/47 passed (throwaway server :8799).

## 2026-07-06 18:34 — TestSystemC pass 3 (HEAD c3232dc, still unchanged — varied coverage round 3)

- Syntax unchanged since pass 1 · 0 console errors · MMO harness 47/47.
- Live game server :8787 healthy (WS handshake verified; an earlier zsh /dev/tcp probe false-negatived —
  harness issue, not the game). Cleaned up pass-2's leftover throwaway server on :8799.
- **Augment tree:** 29 AUGMENT_ITEMS + rollAugment present ("Gem of Reinforcement" et al.).
- **Tinkering:** openTinker/applyTinker/buildTinker all present.
- **Society:** 3 societies, 5 ranks, 4 test types; pledge + rank-info functions work.
- **Monthly events:** 9 EVENT_TYPES, 120-entry EVENT_CALENDAR; currentCalEvent resolves ("The Coronation").
- **Housing hooks:** hsHookCount/hsUseHook/hsHookAccepts/openHookPicker all present.
- **Enlightenment:** gate present (level-275 check in enlighten()).
- **Fellowship:** implemented server-side as parties — fellowship_xp range-split (150u) with size bonus,
  invites, party HUD, Fellowship chat channel. Genuine AC model.
- **To-hit/evade:** rollToHit(atk, monster, accMul) verified functional — 97.2% hit at +150 skill vs
  19.8% at −150 (clamps 0.98/0.20 both reached). Formula sane (72% at parity, 0.6%/point).
- **Trade window:** renderTradeWin/closeTradeWin/tradeSlotHTML present.
- **No new findings this pass.**

## 2026-07-06 18:51 — TestSystemC pass 4 (HEAD c3232dc, still unchanged — varied coverage round 4)

- 0 console errors · MMO harness 47/47 · no drift.
- **Allegiance:** swearToPatron/recruitVassal/breakFromPatron/allegianceRank all present; vassalCap()=275.
- **AC portal spells travel:** cast "Center Of Izji Qo's Temple" — player teleported to the retail
  destination exactly (arriveAt dest+3 offset confirmed).
- **Loot rolls (tier 8, 400 draws):** 0 malformed; healthy stat mix; ~9% legendary-class; the
  stat-less draws are spell scrolls by design ({scroll, spellId, name}) and ALL 20 sampled
  scroll spellIds resolve in SPELLBOOK.
- **Salvage:** salvageValue sane on rolled items.
- **Combine engine:** combineChance(recipe) curve ordered — 0.418 for difficulty-50 vs 0.03 floor
  for difficulty-500 (untrained alchemy), logistic as designed.
- **Multi-depth delves:** Aerlinthe Island reports maxDepth 3; enter/goDepth functions present
  (full traversal already proven in pass 1).
- **Chargen:** createChar present, CREATE_POOL=270 / CREATE_CREDITS=52 (ToD-authentic).
- **No new findings this pass.**
