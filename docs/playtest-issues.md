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
- **Mitigation exists:** raising the **Health vital** on the character sheet gives **+1 mhp per rank** (verified: 50→53 for 3 ranks, XP-paid). So players *can* buy HP up independent of Endurance — the AC-authentic path. GP1 is a base-formula/starting-feel concern, not a hard cap.

### 🔵 G4 — One untextured (flat-white) building at Shoushi
- **Where:** Shoushi (Sho capital). A pale, unlit pagoda-shaped building stands out against the textured buildings around it.
- **Evidence:** scanning 6,526 meshes within 400u of the player found exactly **1** with a white `MeshStandardMaterial` and **no `.map`** (texture) — the rest are textured. So it's an isolated building model that streamed its geometry but not its texture (bad/missing DXT decode for that one model, or a material built without its map).
- **Fix later:** identify which AC building model at Shoushi lacks a map; check its texture-decode path. Low priority — cosmetic, single instance.

---

## Systems verified WORKING (positive results — no issues)

The bulk of the game is solid. These were exercised end-to-end this pass and behave correctly:

- **Dungeons** (Drudge Hideout): interior renders with lit stone walls + torch PointLights (~1.3 intensity), **8/8 dungeon mobs correctly on the floor** (0 buried — so G2 is overworld-only), a hoard chest present, working dungeon minimap.
- **Vendors / shops** (Provisioner): shop UI renders cleanly (icons, wares, prices, Respec service, sell section). Buying works — gold deducted (110p) and consumables tallied (`potHealth`/`potGreat` counters; potions aren't `inv` entries by design).
- **Character sheet / XP spend**: sheet renders (attributes ±, vitals ±, skill Train/Spec buttons, unspent-XP readout). Raising an attribute (Focus 60→61) and vitals (HP 50→53) both correctly deduct `xpUnspent` and re-`derive()`.
- **Portals / teleport**: 216 overworld portals; took Holtburg→**Shoushi**, arrived on-ground at the destination, landblock streamed in, destination town rendered with correct **Sho architecture** (torii beams, pagoda roofs) distinct from Holtburg's European style.
- **Magic casting**: `executeSpell("war_flame_1")` consumes mana (60→58.2) and spawns the full FX set — bolt Group + meshes + glow Sprite + **dynamic PointLight** + the authentic AC cast-word floater ("*Zojak Quaguz*"). Spellbar populates from save.
- **Save / load round-trip**: reloaded to title screen → "Continue — Level 25 · the Archmage" → world rebuilt cleanly and **all playtest progress persisted**, including the +3 Health-vital raises (HP 53/53) and spellbar.
- **Title screen**: renders perfectly (branding, online login, offline Continue/Create).

---

## ⚠️ Test-harness caveat (affects visual-bug confidence)

The Claude_Preview headless loop runs at **~0.5 fps**, which trips the game's **adaptive graphics-quality system** — it logs "Frame rate dipped below 30 — stepped graphics down to High/Medium/Low" and drops to **Low**, degrading textures/lighting. **Some earlier "over-bright / washed-out wall" observations were likely captured in this downgraded state, not true bugs.** In real play (60 fps) quality stays High. Re-verify any *lighting/texture-wash* finding at a forced-High quality on real hardware before treating it as real. (Structural/geometry bugs — G1 scale, G2/G3 buried y — are unaffected by quality and remain valid.)

Also: freezing the loop (`running=false`) + calling `renderComposite()` out-of-loop corrupts the post-processing render targets → black viewport. That is a **test-harness artifact**, recovered by a page reload; not a game bug.

## Systems exercised

- [x] Spawn / character model / HUD — HUD refresh-on-load fixed; avatar renders
- [x] Overland movement / terrain / roads / bridges / water — 1:1 scale, real AC roads + bridges
- [x] Towns — buildings, NPCs, vendors, signage — Holtburg (European) + Shoushi (Sho) render distinctly
- [~] Doors / building interiors — *not yet exercised* (buildings are collision meshes; interior entry unverified)
- [x] Combat — melee, ranged, spells, effects, monsters — spell FX + mana + damage path verified
- [x] Portals / dungeons — 216 portals, teleport→Shoushi verified; Drudge Hideout dungeon verified
- [x] Vendors / buying / selling — Provisioner UI + buy flow verified
- [x] XP / leveling / attribute + vital allocation — raise/refund + derive verified
- [~] Inventory / equipment / avatar armor & weapons — weapon equipped (Steel Sword); full inv/armor UI not deep-tested
- [x] Magic — spellbar, casting visuals (bolt+glow+light+cast-words), mana cost verified
- [x] Map / minimap overlay — real-AC-road overlay verified earlier; minimap updates on teleport
- [~] UI panels — sheet ✓; quest log / tinker / housing / allegiance / society not this pass
- [x] Lighting / day-night / weather — weather events fired (fog/tempest/rain); *see harness caveat on quality*
