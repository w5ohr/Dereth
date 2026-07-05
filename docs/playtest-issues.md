# Dereth — Playtest & Graphics QA Log

Extended playthrough (2026-07-05) exercising every system a player touches, on the 1:1-scale world. Each issue is logged with severity, where it was seen, and enough detail to fix later. Graphics issues are the priority, but gameplay bugs are logged too.

**Severity:** 🔴 blocking · 🟠 major · 🟡 minor · 🔵 polish

**Test harness note:** the Claude_Preview render loop throttles in headless mode, so screenshots are driven by moving the player and letting the loop frame them (third-person). Colors/values are cross-checked with `preview_inspect`/`eval` where a screenshot is ambiguous.

---

## Issues found

### 🟠 G1 — Lifestones render oversized & mis-oriented (squat "insectoid" crystal)
- **Where:** every lifestone (Holtburg capital + wayside). First seen dead-centre of Holtburg — reads as a big blue crystalline "creature with splayed legs."
- **Evidence:** the lifestone mesh bounding box is **9.2w × 4.8h × 9.1d units** (human model is ~1.9u). It's *wider than tall*, yet `buildLifestone` comments call it "a blue-veined stone **spire**" — a spire should be tall & narrow. So it's both oversized (~5× too wide) and likely rotated onto its side (the AC z-up→y-up conversion may be off for this Setup).
- **Fix later:** in `buildLifestone`, normalize the `acMiscMesh("lifestone")` crystal — scale it to ~2–3m tall and correct the up-axis. The clearing circle (radius 4.6) was sized to match the oversized crystal, so shrink that too. Same likely applies to `bindstone`.

### 🟠 G2 — Most overworld monsters render buried ~90u underground (until you're close)
- **Where:** everywhere — **19 of 32** overworld monsters sit ~88–95u *below* the terrain (mesh-y ≈ 0 while ground is ~90). Only the ~13 within the sim radius of the player are on the surface. All the buried ones are in "wander" state.
- **Cause:** a monster's mesh-y is only set to `groundY` while it's being *simulated* (within `SIM_RADIUS` = 95u; the update loop `continue`s past far, non-chasing monsters). Un-simulated monsters keep a low/stale y ≈ 0. This was masked at the old ⅓ scale (terrain only ~26u high) but the **1:1 scale tripled terrain heights (~90u)**, so far monsters are now deeply underground and "rise from the ground" as you cross the 95u threshold.
- **Fix later:** set a monster's mesh-y to `groundY(m.x,m.z)` at **spawn** (and when it drifts while un-simulated), not only during simulation — so distant monsters sit on the terrain even before they're in sim range. Cheap: one `groundY` call per monster per spawn/wander-step.

### 🟠 G3 — PK altars are buried underground
- **Where:** the Altar of Asheron / Altar of Bael'Zharon near Holtburg. The altar base sits ~28u below ground.
- **Cause:** `addPKAltar` does `mesh.position.set(x, 0, z)` — y=0 instead of `groundY(x,z)`. Lifestones get a separate `ls.mesh.position.y = groundY(...)` pass; altars never got one, so they sit at y=0 (buried, and worse at 1:1 scale where terrain near Holtburg is ~28–78u).
- **Fix later:** in `addPKAltar`, set `mesh.position.set(x, groundY(x,z), z)` (my regression from the PK-altar feature).

### 🟡 GP1 — Health derives low relative to Stamina (verify balance)
- Level-25 char with Endurance 100 → **mhp 50** but **mst 100** (HP ≈ Endurance/2, half of stamina). At base attributes (all 10) a level-25 char derived **mhp 4** and was one-shot by a drudge. HP feeling low vs incoming damage (drudges 7, tuskers 24) — verify the `derive()` HP formula against AC (in AC, Health ≈ Endurance, comparable to Stamina, not half). *(Note: the test character "Bob" was also corrupted by eval mutation — attrInnate null — so treat the base-10 case cautiously.)*

---

## Systems exercised

- [ ] Spawn / character model / HUD
- [ ] Overland movement / terrain / roads / bridges / water
- [ ] Towns — buildings, NPCs, vendors, signage
- [ ] Doors / building interiors
- [ ] Combat — melee, ranged, spells, effects, monsters
- [ ] Portals / Town Network / dungeons
- [ ] Vendors / buying / selling
- [ ] XP / leveling / attribute + skill allocation
- [ ] Inventory / equipment / avatar armor & weapons
- [ ] Magic — spellbar, casting visuals, buffs
- [ ] Map / minimap overlay
- [ ] UI panels — sheet, quest log, tinker, housing, allegiance, society
- [ ] Lighting / day-night / weather
