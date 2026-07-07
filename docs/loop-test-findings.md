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

## 2026-07-06 19:03 — TestSystemC pass 5 (HEAD c3232dc, still unchanged — varied coverage round 5)

- 0 console errors · MMO harness 47/47 · no drift · PR #147 (log passes 1-4) open & mergeable.
- **Aetheria set stacking:** Growth pieces stack cleanly — +30/+60/+90 mhp for 1/2/3 slotted.
- **Chained delves:** 11 retail chains derive from ACW portal data (Mausoleum of the Fallen,
  Penguin Den, Colosseum, …); every chain target resolves to a real DUNGEONS def.
- **Valkyrie buff bot:** buffBotCast("me") applies the full castable list — verified all 5
  landed in their real stores (Strength→spellBuffs, Corrosive Ward→banes.acid, Quickening→swift,
  Blade Lure→might-type, Heal transient). Earlier "0 applied" readings were probe accounting
  (wrong store names + leftover buffs), not defects.
- **Quest givers:** all 323 QUEST_GIVERS resolve — towns exist in CITIES, quest ids pair up.
- **Live wardrobe:** 893 built avatars in scene, 416 hats attached (heads swapped).
- **No new findings this pass.**

## 2026-07-06 19:12 — TestSystemC pass 6 (HEAD c3232dc, still unchanged — varied coverage round 6)

- 0 console errors · MMO harness 47/47 · no drift.
- **Death & Vitae:** die() verified live — vitae +5%/death, creature/life enchantments fall,
  a named-loss corpse drops, respawn works, and gainXP burns vitae off (amt/40000, passup exempt).
- **Encumbrance:** encumbrance() returns {load,cap,ratio} with the AC ≥200% can't-jump ceiling
  modeled (cap from Str×18 + End×6); loading 60 anvils raised ratio 0→1.42 as expected.
- **Vendor pricing:** per-stat gearSellPrice formulas sane on rolled weapon/worn gear.
- **Portal tie/recall:** Tie Portal binds player.tiedPortal; Portal Recall present.
- **Monster aggro (functional):** in wilderness a drudge senses (34u), switches to chase and
  closes 11.3→1.6u driving update() directly. Two "failures" en route were the TOWN_SAFE ring
  (60×WSCALE=180u) correctly re-seating mobs out of capital safe zones — by design, not defects.
- **No new findings this pass.**

## 2026-07-06 19:14 — TestSystemC pass 7 (HEAD c3232dc, still unchanged — varied coverage round 7)

- 0 console errors · MMO harness 47/47 · no drift.
- **Casino:** 300 low-stake plays — 1 golden key, 32 pack dolls, 98 gold payouts, 169 misses;
  house nets 16,899 pyreals. Edge and prize tables behave. (First probe passed a number where
  casinoPlay wants a CASINO_STAKES key — probe error, not a defect.)
- **Pets:** spawnPet works, pet stays near the player under update(), despawnPets clears.
- **PK altars:** 2 altars present (one npk, one pk); player pkState tracked.
- **Ships:** skiff def resolves, findWater locates navigable water, shipNavigable(x,z,def) true
  there, pilot updater present (board/disembark already proven in pass 2).
- **Allegiance:** 1 vassal → rank 1; vassalXP pool tracked; pass-up path (gainXP passup-exempt)
  verified in code.
- **No new findings this pass.**

## 2026-07-06 19:25 — TestSystemC pass 8 (HEAD c3232dc, still unchanged — varied coverage round 8)

- 0 console errors · MMO harness 47/47 · no drift.
- **Dispel:** planted slow+imperil debuffs; Dispel Self I cleanses both.
- **Rares:** gate rareRoll() ≈ 1-in-900 (pity-timed); rollRare() materializes valid legendary
  rares from a 10-entry table ("Storm Amuli Coat" sampled). The 100%-hit first reading was
  probing the materializer, not the gate — non-defect.
- **Imbues:** tinker path verified live — Amber salvage (100 units) on an equipped sword with
  trained skill → "of Flame" fire brand, imbued flag, tink 1/10, bag consumed. Imbues are
  salvage-applied by design (never on raw loot rolls).
- **Weather:** state machine present (clear), updateWeather runs; portal-storm trigger present.
- **Quickbar:** spellbar panel present with 4 slots.
- **No new findings this pass.**

## 2026-07-06 19:32 — TestSystemC pass 9 (HEAD c3232dc, still unchanged — varied coverage round 9)

- 0 console errors · MMO harness 47/47 · no drift.
- **Loadouts:** quickbar loadouts (by design — they snapshot player.hotbar, not gear) verified:
  save → clear → load restores all slots; delete removes; 8-loadout cap enforced.
- **Bounty turn-in:** trophyBounty prices a Drudge Scalp at 40 gold + 350 xp — sane.
- **Academy:** enter/exit/kill-credit functions present; ACADEMY_POS placed.
- **Allegiance swearing:** swearToPatron() is a no-arg net-targeted op (aimed player online);
  surface + rank math verified in passes 4/7.
- **Housing hooks:** hsHookCount() counts hooks USED in your homestead — 0 for the houseless
  test character is correct, not a defect.
- **No new findings this pass.**

## 2026-07-06 20:04 — TestSystemC pass 10 (HEAD c3232dc, still unchanged — varied coverage round 10)

- 0 console errors · MMO harness 47/47 · no drift.
- **Healing kits:** verified end-to-end after a false alarm — a v:60 kit heals 60 untrained and
  97 with specialized Healing (healScale: rank×0.004, spec +10%, 0.6× in combat). The initial
  "heals only 8" reading was the probe's own doing: hours of level=275 hacks without raising
  vitals left the test character at mhp≈13, so heals were correctly capping at full health.
  Confirmed on a fresh reload + properly raised vitals. NOT a defect. (Probe lesson: raise
  player.vitals when faking level.)
- **Spell components:** burnSpellComponents live; 163 retail component defs (AC_COMP_CDM);
  rollComponent yields valid comps ("Motherwort").
- **Settings:** save/load roundtrip preserves values (dglight tested).
- **Minimap:** init + draw run clean; **dungeon locks:** hasNamedKey/unlockDungeonDoor work;
  **arena:** start/wave/win/fail/update machinery all present.
- **No new findings this pass.**
