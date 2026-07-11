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

## 2026-07-06 20:32 — TestSystemC pass 11 (HEAD c3232dc, still unchanged — varied coverage round 11)

- 0 console errors · MMO harness 47/47 · no drift. Fresh level-1 character (post-reload) used.
- **Natural leveling:** 50k XP takes the fresh char level 1→8; xpUnspent accrues 1:1.
- **XP spending:** vitalCost curve AC-authentic (73 at rank 0 → 4,708 at 50 → 329M at 195,
  hard cap 196); attribute raise (cost 110) works; raiseSkill invests xp and grants rank 1
  ("heavy" @ 58 xp). 37 skills in SKILL_BY_KEY. ("axe" is a weapon TYPE not a skill key —
  probe error en route, logged for reference.)
- **Food buffs:** +5 Strength roast chicken applies timed attr buff.
- **Mana stones:** Use opens the openManaStone chooser (release/draw/consume) — panel verified.
- **Keyrings:** stow loose keys and picks (1+1 stowed on Use).
- **Greeters:** fresh char starts with no greeter state — populated by chargen flow (by design).
- **No new findings this pass.**

## 2026-07-06 21:02 — TestSystemC pass 12 (HEAD c3232dc, still unchanged — varied coverage round 12)

- 0 console errors · MMO harness 47/47 · no drift.
- **Melee kill chain (functional):** damageMonster applies exact damage; killing a drudge grants
  XP, increments the kill counter, and leaves loot/corpse.
- **Armor:** wornArmorV 0 → 120 with a breastplate equipped.
- **Bow (functional):** fireArrow() is the ranged entry — 10 stamina per bow profile, cooldown +
  draw animation set, exactly 1 arrow consumed per shot, refuses to fire without matching ammo.
- **War projectiles:** Flame Bolt I make() yields dmg 24, fire element, projectile speed.
- **Quest slay-credit:** questEvent("slay","drudge") ticks the matching objective in activeQuests
  (alfrin prog [0,1]). (Objective type is "slay", not "kill" — earlier miss was the probe.)
- **No new findings this pass.**

## 2026-07-06 21:10 — TestSystemC pass 13 (HEAD c3232dc, still unchanged — varied coverage round 13)

- MMO harness 47/47 · no drift · no console errors.
- **Harness state discovery:** since the pass-10 reload the game had been sitting at the TITLE
  MENU (running=false), so update()-gated systems read as dead in passes 11-12 probes that
  relied on update() — direct-call tests (damageMonster, fireArrow, applyItem, raiseSkill etc.)
  were unaffected and remain valid. Re-entered the world via startGame(true). NOTE (harness,
  not game): preview_click on #continueBtn reported success but never fired its onclick in
  headless Chrome — invoke the handler directly in future passes.
- **Vital regen (functional, in-world):** hp/st/mn all tick every 0.5s per the End/Self
  formulas and filled their pools over 10 simulated seconds.
- **Day/night:** updateDayNight advances gameTime at dt/DAYLEN (7620s AC-authentic day).
- **Slash commands:** handleSlash expects the leading "/" (probe initially sent "where" which
  parsed as "/here" — non-defect). /where reports region + lore + nearest settlements with
  bearings ("Gharu'ndim… Al-Jalima NE ~6 leagues · Zaikhal N ~9"); in-dungeon it correctly
  says no region reads; /emote clap and /who run clean; unknown commands are flagged.
- **Colosseum tickets:** rollColosseumTicket yields a valid stat:"ticket" item.
- **Char select:** renderCharSelect/createChar present; client keeps a single local save slot
  (multi-slot lives server-side — proven by the MMO harness roster tests).
- **No new findings this pass.**

## 2026-07-06 21:15 — TestSystemC pass 14 (HEAD c3232dc — new 15-min loop cadence)

- 0 console errors · MMO harness 47/47 · no drift.
- **Portal storm:** triggerPortalStorm scatter-teleports the player (~44u toward town outskirts).
- **Drowning:** submerged 20s — breath depletes then health drains; surfacing restores breath to full.
- **Charge attack:** power accumulates with held charge; releaseCharge present.
- **Achievements:** checkAchievements grants on thresholds (1 → 3 badges at 1,000 kills).
- **Dash:** dashT/dashCd state fields tracked on the player.
- **No new findings this pass.**

## 2026-07-06 21:21 — TestSystemC pass 15 (HEAD c3232dc — item/magic depth)

- 0 console errors · MMO harness 47/47 · no drift.
- **Atlan stones:** Stinging Stone brands an equipped sword → "of Corrosion" (acid) permanently.
- **Void school:** 32 void spells incl. "Nether Corruption II"; monster DoT support present.
- **Weapon oils:** Honing Oil applies +12% weapon damage for 100s → player.itemBuffs.dmg;
  itemBuff("dmg") reads 0.12; 30 rolled oils all carry dmg/dur.
- **Worn item enchants:** a real Blood Drinker acspells id on gear populates player.gearFx.dmg
  (via wornMagicItems → itemSpellNames → itemSpellFx aggregation). Works with numeric acspells
  ids + itemMana pool — NOT the invented itemV field my first probe used (non-defect).
- **Set + jewelry bonuses:** setBonus() returns {count,set,armor,mhp,skill,skillV}; jewelryBonus present.
- **Scarabs:** isScarab/findScarabFor present; 9 SCARAB_LEVEL tiers (mana-substitute reagents).
- **No new findings this pass.**

## 2026-07-06 21:24 — TestSystemC pass 16 (HEAD c3232dc) — ⚠ FINDING: weapon oil leaks onto spell damage

**User-reported check:** "oil applies to bolts, arrows, darts, any projectile weapon."

- ✅ **CONFIRMED CORRECT — oil DOES apply to all projectile weapons.** Every projectile (arrow/
  bolt/dart) resolves its hit through `applyHit` (index.html:16442), which applies the oil buff
  `itemBuff("dmg")` at index.html:10084. Runtime proof: a 1000 physical arrow component → 1120
  with a +12% Honing Oil (ratio 1.12). No change needed for projectiles.

- ⚠ **BUG found while verifying: weapon oil ALSO boosts SPELL damage.** `applyHit` (index.html:10084)
  runs `base*=(1+itemBuff("dmg")+aetheriaBonus("dmg"))*(1-vitae)` UNCONDITIONALLY, before it
  branches on `opts.spell` (index.html:10087) vs `opts.phys`. So a weapon oil (a physical-weapon
  consumable, index.html:14448 → player.itemBuffs.dmg) multiplies war-magic bolt damage too.
  Runtime proof: a 1000 war-magic spell hit → 1120 with a +12% oil active (ratio 1.12). A weapon
  oil should never buff spellcasting.
  - **Repro:** apply any weapon oil (Honing Oil +12%), then cast a damage spell → spell hits ~12%
    harder for the oil's duration.
  - **Root cause:** `itemBuff("dmg")` is applied in the shared pre-branch multiplier, not the
    physical-only path. (Note: `gearFx.dmg` (Blood Drinker) is correctly gated behind `opts.phys`
    at index.html:10086 — oil should follow the same pattern.)
  - **Fix caution:** `player.itemBuffs.dmg` is ALSO set by the Aetheria "Destruction" surge
    (index.html:13594), which may be INTENDED to boost all damage. A fix should move oil to a
    weapon-only (opts.phys) application without disabling the Destruction surge's universality —
    or split oil into its own weapon-only buff slot. Flag for design decision, not a blind gate.
  - **Severity:** low-moderate (minor unintended power buff; balance, not a crash).
- NOT FIXED per loop policy — logged only.

## 2026-07-06 21:27 — TestSystemC pass 17 (HEAD c3232dc) — ⚠ SIBLING FINDING: Might elixir leaks onto spell damage

- 0 console errors · MMO harness 47/47 · no drift.
- ⚠ **BUG (same root cause as the pass-16 oil bug): the Might elixir boosts SPELL damage.**
  `buffMightT` (Elixir of Might / Blade-Lure "might" buff, intended +50% WEAPON damage) is applied
  at index.html:10084 (`if(player.buffMightT>0)base*=1.5;`) — UNCONDITIONALLY, before applyHit
  branches on `opts.spell` (index.html:10087). Runtime proof: a 1000 war-magic spell hit → 1500
  with Might active (ratio 1.50); physical hit also 1.50 (correct). A melee/weapon damage elixir
  should not buff spellcasting.
  - **Repro:** drink an Elixir of Might (or receive Blade Lure), then cast a damage spell → +50%.
  - **Consolidated root cause:** index.html:10084 applies BOTH weapon buffs (`buffMightT` ×1.5 AND
    `itemBuff("dmg")` = oil) in the shared pre-branch multiplier. Both leak onto `{spell:true}`
    hits. The correct-by-comparison sibling is `gearFx.dmg` (Blood Drinker) at index.html:10086,
    gated behind `opts.phys`. Fix direction: move the buffMightT ×1.5 and the oil portion of
    `itemBuff("dmg")` into the `opts.phys` path — but preserve the Aetheria "Destruction" surge
    (also writes player.itemBuffs.dmg, index.html:13594) and `aetheriaBonus("dmg")` universality
    if those are intended to boost all damage (design call).
  - **Severity:** low-moderate (unintended mage power buff from melee consumables; balance).
- **Also verified GREEN this pass:** antidote/cure cleanses debuffs; attribute philtre (+10 Str
  timed); skillpot (→ player.skillBuffs, not the skillPots field my probe guessed — non-defect);
  portal gem teleports + consumes; manastone discharge fn present; Aetheria on-hit surge (Fury/
  Festering) fires.
- Findings logged only — NOT fixed, per loop policy.

## 2026-07-06 21:31 — TestSystemC pass 18 (HEAD c3232dc) — applyHit audit: 3rd weapon→spell leak (crit)

Full line-by-line audit of `applyHit` (index.html:10083–10113). Most modifiers are correctly
gated: gearFx.dmg/Blood Drinker (:10086 opts.phys), magic-resist (:10088 opts.spell), physMult
(:10091 opts.phys), brand elemental (:10096 opts.brandDmg), Crushing Blow (:10108 opts.phys).
Damage Rating (:10085) and Life Vulnerability (:10111 `vgen`) apply to both — AC-correct (universal).

- ⚠ **3rd leak (same root class, LOW severity): weapon crit enchant boosts SPELL crit.** The crit
  line (index.html:10105) sums `gearFx.crit` into spell crit chance unconditionally. `gearFx.crit`
  includes **Heart Seeker / Hunter's Mark** (index.html:790, `{k:"crit",v:0.004*r}`), a WEAPON
  enchant. Runtime proof: with Math.random pinned at 0.30, a spell hit stays 1000 (no crit) with
  no gear, but crits to 2000 (×2) when a Heart Seeker gearFx.crit is present. So a weapon's
  Heart Seeker raises your spellcasting crit rate.
  - Milder than the oil/Might multiplier bugs: spells legitimately crit, gearFx.crit also holds
    Coordination item-spells (universal, fine), and Heart Seeker's contribution is small
    (0.004/rank). But it is the same weapon-enchant-leaks-onto-spells pattern.
  - `itemBuffs.crit` (also on this line) comes only from the Aetheria "Fury" surge (:13595) —
    plausibly intended universal, NOT flagged. `buffSwiftT` only affects movement (:16324),
    not spell/attack timing — NOT a leak.

**CONSOLIDATED — the applyHit weapon→spell leak family (all same root: modifiers applied before the
opts.spell/opts.phys branch):**
  1. Oil (`itemBuff("dmg")`, :10084) → +X% spell damage.        [pass 16, low-moderate]
  2. Might elixir (`buffMightT`×1.5, :10084) → +50% spell damage. [pass 17, low-moderate]
  3. Heart Seeker (`gearFx.crit`, :10105) → +spell crit chance.   [pass 18, low]
  Suggested fix: apply weapon-only offense (buffMightT, oil-portion of itemBuff("dmg"), the
  Heart-Seeker portion of gearFx.crit) inside the `opts.phys` path, mirroring gearFx.dmg at :10086;
  keep Damage Rating, Vulnerability, Aetheria surges/sets universal.

- **Also verified GREEN:** book reader fn present + Use never consumes the book (synthetic-book
  render inconclusive — probe used a raw `pages` field, likely needs a real book id; not a defect);
  recall contract recalls + is reusable (keep) + sets its cooldown. MMO harness 47/47, 0 console errors.
- Logged only — NOT fixed, per loop policy.

## 2026-07-06 21:33 — TestSystemC pass 19 (HEAD c3232dc) — defensive audit: playerHurt CLEAN (no leaks)

Symmetric follow-up to the offensive applyHit audit. Full read of `playerHurt`
(index.html:14857–14905): every mitigation is correctly type-gated —
- evade / flat Defense avoidance use the per-type skill (magicd/missiled/meleed);
- armor + shield-block are physical-favored (armor half-applies to magic, block is type!=="magic");
- Aegis magic-absorb is magic-only; Life Protection / Vulnerability / Recklessness / creature-crit
  are universal (AC-correct).

**Runtime verification (Math.random pinned to kill evade/crit noise, defenses stripped to isolate each):**
- Baseline: 1000 → ~985 phys / ~984 magic (small innate attribute-defense).
- **Life Protection 25%** cuts BOTH: phys 985→739 (×0.75) · magic 984→738 (×0.75). ✓ correctly universal.
- **Fire bane 30%**: vs fire 984→**689** (×0.70) ✓ · vs frost **984** (untouched) ✓ · vs physical **985**
  (untouched) ✓ — **NO cross-element leak** (the exact opposite of the offensive weapon→spell bug).
- **Imperil (vuln ×1.5)**: 985→**1478** ✓ correctly amplifies.

**Conclusion:** the weapon→spell leak family (passes 16–18) is ISOLATED to applyHit's offensive
pre-branch multiplier; the defensive mitigation chain does not mirror it. No new findings this pass.
MMO harness 47/47 · 0 console errors · no drift.

## 2026-07-06 21:38 — TestSystemC pass 20 (HEAD c3232dc) — economy arbitrage audit: CLEAN (no money loops)

Audited the vendor economy for buy-low/sell-high infinite-money loops (the historical exploit class —
a prior note-price bug is noted in-code at index.html:20731).

- **Systematic scan:** built the real stock of all 11 VENDOR_TYPES and, for every buyable item that
  becomes a sellable inventory item (2,306 across armorer/weaponsmith/outfitter/scrivener/jeweler/
  furnisher/provisioner/healer/shipwright/caravan), simulated the buy (attaching `bp` exactly as the
  buy handler does at index.html:21445) and compared `gearSellPrice` back. **Result: 0 arbitrage loops.**
- **The bp guard (index.html:20745) is robust:** `if(it.bp!=null) p=Math.min(p,bp-1)`. Verified — a
  Legendary Blade (natural sell 1542) bought for 50 resells for only 49. Every shop-bought gear
  carries bp, so its resale is always < cost.
- **The one un-bp'd buyable (scrolls, index.html:21447) is safe:** `scrollPrice = 90 + lvl*80` (≥170)
  vs a flat 30 sell — a large loss, no loop.
- **Trade notes (historical exploit spot):** redeem for v (250) but sell for v-1 (249), and are
  loot/reward-only (never vendor-buyable) — no loop.
- **Normal gear round-trip is a loss:** buy 400 → sell back 86.

No new findings. MMO harness 47/47 · 0 console errors · no drift.

## 2026-07-06 21:43 — TestSystemC pass 21 (HEAD c3232dc) — ⚠ FINDING: focus-mana battery not refunded on fizzle/abort

Audited the spell cast economy (mana cost / mana-conversion / fizzle / component burn / scarab / focus
battery). Mana-conversion cut, fizzle sigmoid, component burn, and the scarab path are all internally
consistent. **One real bug in the focus-mana battery refund:**

- ⚠ **Focus-mana battery (H15) is NOT refunded on a fizzle or a target-required abort** — its share is
  silently transferred into the personal mana pool instead.
  - **Where:** `executeSpell` (index.html:15128+). When `player.mn < cost`, the focus battery covers
    the shortfall: index.html:15152 does `player.focusMana -= need; focusPaid = need; player.mn = cost;`
    then index.html:15159 `player.mn -= cost` → pool 0. On a fizzle (index.html:15169) or any
    target-abort (the `player.mn += cost` sites at :15184/15192/15201/15228/15246-15260…), the code
    refunds the FULL `cost` to `player.mn` but never restores `player.focusMana`.
  - **Smoking gun:** `focusPaid` is captured (index.html:15152) but NEVER used anywhere, and there is
    NO `player.focusMana +=` refund anywhere in the file. The refund was clearly intended but unwired.
  - **Runtime proof:** focus equipped (battery 1400), war skill = spell req (fizzle 40%), Math.random
    pinned to force a fizzle, personal mana 2 < cost 5 (need 3 from battery). After the fizzle:
    personal mana **2 → 5** (+3), battery **1400 → 1397** (−3). Total conserved, but the battery lost
    its share and the pool gained it — on a cast that "consumes no mana" per the code's own comment
    (index.html:15165).
  - **Impact:** (1) violates the fizzle = no-mana-consumed invariant for the battery; (2) a free
    battery→pool mana transfer via deliberately-fizzled or no-target casts; low severity (mana-
    conserving, not infinite), but incorrect.
  - **Fix:** on every fizzle/abort refund, restore the battery first using the already-captured
    `focusPaid`: e.g. `if(focusPaid>0){ player.focusMana+=focusPaid; player.mn-=focusPaid; }`
    alongside (or folded into) the `player.mn+=cost` refunds — so the pool regains only what it
    actually paid and the battery regains its share.
- **Verified GREEN:** mana-conversion reduction, fizzle sigmoid (fz cap 0.40), component burn on both
  hit and fizzle, scarab substitution path (self-consistent: not deducted, not refunded).
- NOT FIXED per loop policy — logged only. MMO harness 47/47 · 0 console errors · no drift.

## 2026-07-10 16:40

- **git HEAD:** `c46ac357`
- **Syntax check:** `SYNTAX OK` (JavaScriptCore, all `<script>` blocks parsed clean).
- **Runtime:** headless Chrome via Browser MCP, server on :3000. Entered offline via `startGame(false,'aluvian')`, loaded Kilmer's full loadout from `server/admin_kilmer.json` into the live `player` (level 275, "of the Tenfold" set, Kilmer's Cape, 875 known spells), tested in third-person.
- **Console errors:** NONE across the entire session. **Network:** all requests 200 OK — **zero 404s** (acmodels / acheads / acflora / acclothing_mesh / acarmor / acitemmodels / acsounds all served).

### Issues found this run (all LOW severity — no functional/crash bugs)

1. **[LOW · UI completeness] The Alinco Buffs enchantment tracker omits Item-Enchantment buffs and Life buffs.**
   - **What I did:** Cast Blood Drinker VII, Swift Killer VII, Impenetrability VII, Protection Self VII, Flame Bane VII, Strength/Quickness Self VII, then opened the Alinco Buffs panel.
   - **What went wrong:** The panel listed only **Fire Bane 59:57**, **+70 Strength 74:57**, **+70 Quickness 74:57**. The Item-Enchantment buffs that landed in `player.itemBuffs` (Blood Drinker → `dmg` 0.66, Swift Killer → `haste` 0.64, both live at 3600 s) and the Life **Protection** buff (in `player.lifeBuffs`) never appear — a player buffing weapon/armour has no on-screen countdown for them.
   - **Origin:** `index.html:23512–23521` (`pluginOn("alinco")` render). It builds rows only from `player.spellBuffs`, `player.skillBuffs`, and `player.banes`; it never iterates `player.itemBuffs` or `player.lifeBuffs`. Banes/attribute/skill buffs show; the four weapon/armour item-spells and Protection do not.
   - **Severity:** low — cosmetic/UX (the buffs *apply* correctly, they're just not tracked in the one on-screen enchantment timer).

2. **[LOW · latent robustness] acspellstats duration merge can clobber Item-Enchant/Bane durations (currently harmless).**
   - `index.html:1612` — `if(a.dur&&s.buff){ s.dur=Math.round(a.dur); }` re-applies retail JSON durations to **every** buff, including `s.buff==="item"` and `"bane"`. The sibling magnitude merge one line up (`:1611`) deliberately guards these families (`s.buff!=="item"&&s.buff!=="bane"`), but the duration merge has no such guard.
   - **Currently no defect:** I read the live post-merge durations for all 4 item-enchant families + all 7 banes (I–V = 1800 s, VI = 2700 s, VII–VIII = 3600 s) and magnitudes (monotonic increasing) — every value is correct, because `acspellstats.json` doesn't supply `dur` for these names. But if a retail duration is ever added for a Blood Drinker/Bane entry, it would silently override the authentic `ITEM_ENCHANT_DUR` ladder. Recommend mirroring the `:1611` guard on `:1612`.

3. **[LOW · cosmetic] Minor bare foot skin below the Sabatons of the Tenfold.**
   - Inspecting the avatar rig: the feet slot has 4 `acArmor` (sabaton) meshes attached, yet 3 `acBody` foot/ankle meshes (worldY-fraction ~0.02–0.03) remain visible below them. Torso, pelvis, thighs and shins are all correctly hidden and clad, so this is a small toe/heel gap only, plausibly by design.

### Verified GREEN (no issues)

- **Avatar / armour mapping:** Kilmer's "of the Tenfold" set maps correctly onto the AC body — chest (2 meshes), legs (4), upper/lower arm, hands, feet all clad via the `platemail *` fallbacks (`acArmorFallback` resolved every legendary name; 16 armor meshes attached, 14 bare-body parts hidden). No torso/leg bare-skin gap (the "bare" look in early screenshots was washed-out cream lighting in the arrival hall, not missing armour). **Kilmer's Cape** renders (blue) and drapes; the head shows the procedural helm shell (Diadem has no retail mesh → intended fallback).
- **Magic — all schools cast with no errors:** War (bolt/streak), Life (heal/revit/prot/vuln), Creature (str/quick), Item (blood/swift/impen), Void (nether bolt/streak), Bane — 14 spells executed, 0 exceptions.
- **Item-Enchantment durations (flagged concern) — CORRECT:** Blood Drinker / Heart Seeker / Impenetrability / Swift Killer + all 7 Banes = 30 min (I–V), 45 min (VI), 60 min (VII–VIII); magnitudes monotonic. Buff HUD countdowns match (Fire Bane 59:57; +70 Str/Quick 74:57 for the 75-min attribute line).
- **Kilmer's Cape stats:** `+100 magicDef` folded into gearSkill (`index.html:14610`); `elemProt 100` drives all-element resist to the 0.70 cap (`resistVs`, `index.html:10756`).
- **Castle Val Halla storage:** `castleAccess()` true for Kilmer; all 3 mega-chest variants fill (base 90 / dye 98 / salvage 174 items, ≤ `KC_VAULT_CAP` 300) and all 18 labelled chests fill (mmd/jewelry/armor/weapons/salvage/dye/…) with no undefined items; the "vault" Estate-storage chest is intentionally empty (default case).
- **Movement/physics:** forward drive moved 9.71 m in ~1.2 s, stayed grounded (yΔ 0), no errors. **Combat:** melee swing + projectile casts stepped 30 frames, no errors. **Inventory/equip:** equip/unequip weapon (sword↔bow stance switch), armor, jewelry all clean. **Character Sheet** renders (Attributes/Skills/Titles tabs, buffed Str/Quick 170, vassal system).

> Environment note (NOT a game bug): the headless tab throttles `requestAnimationFrame`, so the arrival portal-transit tube (`portalTransit`, `maxHold` 10 s) does not accumulate wall-clock time and appears "stuck" until stepped. In a real focused browser it self-completes in ~1–2 s. Systems were driven by calling `update(dt)` with explicit dt to work around this.

- NOT FIXED per test-only policy — logged only. No code, commit, or push.
