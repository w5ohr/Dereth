# Dereth — Playtest & Graphics QA Log

Extended playthrough (2026-07-05) exercising every system a player touches, on the 1:1-scale world. Each issue is logged with severity, where it was seen, and enough detail to fix later. Graphics issues are the priority, but gameplay bugs are logged too.

**Severity:** 🔴 blocking · 🟠 major · 🟡 minor · 🔵 polish

**Test harness note:** the Claude_Preview render loop throttles in headless mode, so screenshots are driven by moving the player and letting the loop frame them (third-person). Colors/values are cross-checked with `preview_inspect`/`eval` where a screenshot is ambiguous.

---

## Issues found — ALL FIXED (2026-07-05, commit pending)

### ✅ G1 — Lifestone read as an oversized "insectoid" crystal — FIXED
- **Where:** every lifestone. First seen dead-centre of Holtburg.
- **Real cause (on inspection):** the crystal geometry was actually fine (native 3.6m tall, base at y=0 — a proper upright spire). The playtest's "9.2 × 4.8 × 9.1, wider than tall" was the **group bounding box contaminated by the flat clearing disc** (radius 4.6 → 9.2 diameter), which dwarfed the crystal and produced the wide "splayed" footprint. The crystal itself was never sideways.
- **Fix (`buildLifestone`):** normalise the `acMiscMesh("lifestone")` crystal to a consistent ~2.6m landmark spire standing base-on-ground (`s=min(1,2.6/height); cg.scale=s; cg.position.y=-minY*s`), and **shrink the clearing disc from radius 4.6 → 3.0** so it no longer dominates the footprint. **Verified:** crystal now 2.15w × **2.6h** × 2.02d (taller than wide), base exactly on groundY; visually a correctly-sized blue crystal beside a 2-storey building. (The two dark angular base shards are part of the authentic AC model.)

### ✅ G2 — Overworld monsters rendered buried ~90u underground — FIXED
- **Where:** everywhere — **19 of 32** far monsters sat ~88–95u below the terrain (mesh-y ≈ 0), rising from the ground as you crossed the 95u sim threshold.
- **Cause:** a monster's mesh-y was only set to `groundY` while *simulated* (`SIM_RADIUS`=95); un-simulated ones kept spawn-y = 0. Masked at the old ⅓ scale (terrain ~26u), exposed at 1:1 (terrain ~90u).
- **Fix:** spawn monsters on the terrain — `spawnMonster` now `mesh.position.set(x, groundY(x,z), z)` (was `,0,`) — plus a safety net in the sim-skip guard that plants far idle mobs at `groundY` each tick (covers mobs spawned before the heightmap settled). **Verified:** 32 mobs, 22 beyond sim-radius, **0 buried** (was 19).

### ✅ G3 — PK altars buried underground — FIXED
- **Where:** Altar of Asheron / Altar of Bael'Zharon near Holtburg (~28u below ground).
- **Cause:** `addPKAltar` did `mesh.position.set(x, 0, z)` — y=0 instead of `groundY`.
- **Fix:** `mesh.position.set(x, groundY(x,z), z)`. **Verified:** both altars now sit at `off: 0` (gy 28.5 and 83.3).

### ✅ GP1 — Health = Endurance/2 — VERIFIED AUTHENTIC, NOT A BUG (no change)
- Level-25 char with Endurance 100 → mhp 50, mst 100. This is **the real retail AC formula** (Health = Endurance/2, Stamina = Endurance, Mana = Self), cited in the `derive()` comment from the AC client / ACEmulator. Changing it would break the project's authenticity mandate, so **left as-is**. Players raise HP the AC way: the **Health vital** gives +1 mhp/rank (verified 50→53), and Endurance itself climbs with XP. Working as intended.

### ✅ G4 — Untextured glaring-white building panel at Shoushi — FIXED
- **Where:** a Sho gate/building — a large blank pure-white rectangular panel among textured buildings.
- **Cause:** some AC building surfaces carry a bright "unset texture" placeholder colour (near-white, no `.map`); under PBR sun it rendered as a blank glaring panel. (`_tbMarker` only intercepts pure-red doors / pure-blue windows, so a white placeholder fell through to a flat white material.)
- **Fix (`tbBuildMesh`):** tone any untextured near-white surface (all channels > 190) down to muted stone/plaster `0x9c9078`, roughness 0.94. **Verified:** 0 glaring-white surfaces remain at Shoushi; the former panel is now a muted grey sliding-door screen that fits the Sho architecture.

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
