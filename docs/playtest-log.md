# Long-Play Test Log — 2026-07-05 (Agent 2)

Live play-testing on merged main (post PR #72/#73/#74/#58). Headless preview drives the real
game loop (`update(dt)`) with simulated input — real walking, real deaths, real trades.

## GAMEPLAY PROBLEMS FOUND

### 1. ECONOMY — infinite money loop at trade-note vendors (HIGH) — **FIXED mid-session**
Fixed on branch `fix-money-exploit`: trade notes now sell at face−1 (buy 2p → sell 1p;
Trade Note 5000: buy 100p → sell 99p), and every vendor-bought item is stamped with its buy
price (`bp`) which hard-caps its sell price at bp−1 — no buy-sell loop can profit anywhere.
Verified live (3 cycles net −6) and at the pricing layer.
Buy **Trade Note (100)** for **2p**, sell it straight back for **3p** — net +1p per cycle,
repeatable forever. Verified 3 cycles at Barkeeper Wilomine (Holtburg): net +3p.
Cause: `acItemRow` prices the note at `clamp(round(amt/50),2,…)=2p` while the generic sell
formula values it higher (sell 3p > buy 2p). Any item whose buy price hits the 2p clamp floor
is suspect. Fix direction: sell price for a trade note = its redemption value or `buy*0.5`,
never above buy.

### 2. DEATH — dead player can act during the death fade (MEDIUM)
`interact()` has no `player.alive` guard. Dying and pressing E at the death spot during the
1.4 s fade **recovers your own corpse instantly** — pyreals/items come back before the spirit
even leaves, erasing the corpse-run penalty entirely. The respawn message then contradicts
itself ("You recover your corpse…" followed by "Your corpse holds 87 pyreals where you fell").
Fix direction: gate `interact()` (and other action entry points) on `player.alive`.

### 3. WORLD — town portals sit in the footpath (MEDIUM, UX)
Holtburg's two walk-in portals stand ~13u from the lifestone, directly on the natural walking
line to the tavern/shops. Blundered into them twice in one errand (each = surprise teleport to
Nanto, and at level 1 that meant death). Town compression (1/3-scale offsets) parks them in
the plaza; retail towns were full-size so walk-in portals weren't on footpaths.
Fix direction: nudge town portals to the plaza edge (same separation-solve spirit as the
buildings), or add a step-back-and-confirm ring.

## VERIFIED WORKING IN PLAY (no action needed)
- Fixed buildings: walked through Holtburg door(s) along the door axis, stood dead-centre at
  exact floor height; tavern entered the same way; vendor inside at his real spot.
- Vendor trade: E at 2u opens the shop; real ACE stock listed; buy deducts, sell adds,
  inventory updates (aside from issue 1).
- Full death loop ×4: portal → wilderness death → lifestone respawn, Vitae −5% per death
  (stacking 8%→…), 75% vitals, 1-min res protection, corpse with half pyreals. All correct.
- Melee combat: charged swing → hit roll → 33 damage → rat killed → XP granted.
- Mob streaming: 32-mob ring around the player outside town-safe (180u), region-appropriate
  kinds (drudges/banderlings/rats near Holtburg; viamontian deep wilds), despawn at 215u.
- Fast-travel: world-map lifestone click; portal walk-in with true destinations
  ("Mannikin Foundry → Nanto").
- Level-1 danger is authentic AC: 5 max HP (Endurance 10, unspent creation points) — every
  wilderness mistake is lethal. Not a bug, but see issue 3 for how easily a new player lands
  in deep wilds.

### 4. INVENTORY — quest items are sellable (LOW)
The vendor sell list offers QUEST items (Calling Stone — 3p, Letter From Home — 10p).
Selling one could strand its quest. Retail flagged quest items unsellable/attuned.
Fix direction: skip `stat==="quest"` in the sell list (like scrolls).

## OBSERVATIONS / MINOR
- Casting correctly refuses unknown spells / untrained schools with helpful chat messages
  (scroll-scribe loop not yet exercised — next burst).
- No console errors across ~4 hours of simulated play actions.

## BURST 2 (2026-07-05, after the exploit fix merged) — physics, dungeon

Verified working:
- **Jump**: tryJump(0.8) gains +3.2u and lands clean (charged-jump path; keydown → charge →
  release wiring confirmed separately).
- **Fall damage**: AC-authentic — safe threshold (17 + Jump×0.02) × WSCALE(3) ≈ 51.6u;
  a 72u fall cost 16 hp; a 25u fall correctly free.
- **Drowning**: breath drains ~14s submerged, then −6 hp/s ticks; verified through update()
  and direct calls. (One 20s continuous-run early anomaly did not reproduce — worth one
  eyeball on real hardware.)
- **Slidecasting (PR #74)**: path-speed base 4.48 / casting 2.24 (half) / casting+turn-key
  4.48 (full) — exactly as designed.
- **Dungeon (Hunter's Leap)**: entered via E at the entrance, 11 mobs on proper descending
  storey floors (−3.4/−6.8/−10.2 — NOT buried), hoard chest present, walkable, exited clean.
- Vendor-bought-item sell cap (bp) live-verified post-merge: cider buy 2p → sell 1p.

No new issues found in burst 2. (The other agent's playtest-issues.md fixes — buried
overworld mobs, PK altars, lifestone scale, white panels — were all merged before this
burst and held up.)

## BURST 3 (2026-07-05) — fixes shipped + magic loop

### 5. MAGIC — 881 vendor spell scrolls were unlearnable curios (HIGH) — **FIXED**
Retail scroll items ("Scroll of Bafflement Other V" …) in the ACE vendor stock fell through
`acItemRow` to 📦 trophy curios — 881 scroll items across all vendors, none scribeable, and
since acStock REPLACES generic stock, ACE-stocked towns sold no learnable scrolls at all
(magic progression only available from the wandering caravan).
Fix: (a) `acItemRow` maps scroll names to real scribeable scrolls when the spell exists in
SPELLBOOK; (b) every ACE archmage/scrivener also carries the standard 30-scroll shelf.
**Verified end-to-end in play**: Cindrue now lists 30 scroll rows; bought Strength Self II
(250p) → scribed → trained the school → cast: mana −70, +15 Strength buff (30 min).
Correct refusals verified along the way (unknown spell / untrained school / skill below req).

### Fixes #2 and #4 shipped (branch fix-playtest-findings)
- interact() gated on player.alive — dead E no longer insta-loots your own corpse.
- Quest items (stat==="quest") no longer appear in the vendor sell list.
Both verified live; living interact unchanged.

### bp sell-cap regression check
Ginseng (comp, v=4): buy 2p → sells 1p. Pre-fix this was another +2p/cycle loop —
the systemic bp cap caught an exploit class beyond the trade note that triggered it.

## NEXT BURSTS (queued)
- Quest giver dialog + turn-in; chest looting; corpse run (recover AFTER respawn).
- Tinkering/salvage, armor equip visuals, allegiance/plugin panels, Yaraq sweep.
- Footpath-portals finding (#3) still open — needs a placement pass, larger change.
