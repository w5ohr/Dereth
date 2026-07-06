# TestSystemA — overnight automated test log

Recurring full-game test passes (every 30 min) while the user sleeps. Each run tests the
latest `origin/main`, logs anything not working as expected, and **fixes nothing** — findings
here are queued for a later fixer session. Tag: **TestSystemA**.

Harness notes: headless preview tab (rAF-throttled — the main loop freezes when unfocused, so
loop-driven behaviour is driven with synthetic `dt`; 3D screenshots unavailable). Findings that
depend on visuals are marked as needing on-hardware confirmation.

---

## TestSystemA — Run 1 (2026-07-06 late night, main @ c3232dc)

**Result: CLEAN — no defects found.** Coverage this run:

- Boot → fresh recruit wakes in the Training Academy interior ✓
- **All-spells (PR #142)**: SPELLBOOK carries 2,265 spells across 7 schools (war/life/creature/
  item/void/summon/aoe); cast Summon Wisp, Nether Bolt I, Flame Bolt I — all consume mana and
  execute ✓
- **Item icons (PR #143)**: 100/100 rolled items resolve to real AC icons when called with the
  item object (all live call sites do). *Investigated a false alarm: probing `itemIconHTML`
  name-only (without the 4th `it` arg) falls back to emoji — that form isn't used by real call
  sites.*
- **Ye Olde Stair**: Aerlinthe Island (scripted grid) → "Take Ye Olde Stair down — depth 2 (E)"
  → depth 2 builds (12 cells, mobs respawned), climb back to depth 1, exit clean ✓
- **Combine engine**: acrecipes pack = 1,500 combines (wrapper object `{recipes:[...]}`) +
  28 in-game brews; live combine Smelting Pot × Iron Ore → **Slag** (alchemy 38) consumed the
  ore and produced the result ✓
- Combat: melee kill via the real damage path ✓; death → corpse holds the dropped item →
  E-recover restores inventory and consumes the corpse ✓
- Save round-trip carries aetheria/academy/knownSpells ✓
- **Zero console errors.**

False alarms cleared (not logged as defects): icons probe signature; recipes wrapper count.
