# Free weapon models for Dereth — research

*Researched 2026-07-18. Method note: the multi-agent deep-research harness hit the session usage limit, so this was an inline single-researcher pass with direct primary-source verification (official pack pages, itch.io listings, and — for KayKit — the actual LICENSE.txt and per-file glTF inventory in the publisher's GitHub repos). License claims marked ✅ were read from a primary source; anything weaker is flagged. Constraint: free to download AND free for commercial redistribution (CC0 preferred, CC-BY acceptable) — assets are served raw to browser clients.*

## TL;DR — recommended free stack

1. **KayKit Adventurers (free tier)** — the core weapon set. Individual glTF files per weapon, single gradient atlas, style-matches the Quaternius bodies, ✅ CC0 (LICENSE.txt in the GitHub repo). Covers swords (1h/2h), axes (1h/2h), dagger, **crossbows (1h/2h)**, staff, **wand**, spellbook, 4 shield shapes (+ color variants), quiver/arrows, smokebomb.
2. **KayKit Skeletons (free tier)** — ✅ CC0, adds Skeleton_Blade/Axe/Crossbow/Staff + 4 more shields — instant visual variety for undead mobs' drops.
3. **Quaternius Medieval Weapons Pack** — 24 CC0 models ("swords, daggers, **bows**, shields, **hammers** and many more") — fills bow + mace/hammer. Caveat: **untextured, FBX/OBJ/Blend only** (no glTF export listed) → needs a one-time convert + our tint shader anyway (untextured is actually fine for palette tinting).
4. **Quaternius Fantasy Props MegaKit** — ✅ CC0, **glTF, textured**, includes "weapons and tools" among market/furniture props; 60–70% free tier. Fills crafting-tool and prop gaps.
5. **Poly Pizza singles (CC0 filter)** — Quaternius spears, pickaxes (wood/stone/gold/diamond), shovels, axes; Kenney pickaxe; several maces. Verify the license label on each model page (Quaternius/Kenney uploads = CC0; "Poly by Google" = CC-BY 3.0, attribution required).
6. **Kenney Survival Kit** — 80 models, ✅ CC0 (site-wide policy + pack page) — likely axe/pickaxe/campfire-style tools; itemize after download. Kenney's only dedicated 3D weapon pack is sci-fi (Blaster Kit) — not useful.

Excluded: Poly Pizza's "pack" results are mostly Unity Asset Store cross-listings (paid or store-EULA — not redistributable); Mixamo/Sketchfab-Standard EULAs fail the raw-serving test (see rigged-human-models-research.md).

## AC taxonomy coverage

| AC weapon class | Covered by | Status |
|---|---|---|
| Sword (1h/2h) | KayKit Adventurers | ✅ glTF ready |
| Dagger | KayKit Adventurers | ✅ glTF ready |
| Axe (1h/2h) | KayKit Adventurers (+Skeletons) | ✅ glTF ready |
| Mace/club | Quaternius Medieval Weapons (hammers); Poly Pizza maces | ⚠️ convert / per-model license check |
| Spear | Quaternius spears on Poly Pizza (CC0, 2 models) | ⚠️ download + convert |
| Staff/quarterstaff | KayKit Adventurers + Skeletons | ✅ glTF ready |
| Unarmed/cesti | **gap** | 🔨 trivial custom model (knuckle/claw), or invisible + fist anims |
| Bow | Quaternius Medieval Weapons | ⚠️ convert; **no bow-shoot clip in free UAL** (see below) |
| Crossbow | KayKit Adventurers (1h + 2h!) | ✅ glTF ready |
| Thrown/atlatl | smokebomb (thrown) exists; atlatl is a **gap** | 🔨 custom (a spear-thrower stick is a ~50-tri model) |
| Wand/orb/scepter | wand + spellbook (KayKit); orb/scepter **gap** | 🔨 orb = sphere-on-handle, scepter = retinted mace |
| Shield | 8+ shapes across both KayKit packs | ✅ glTF ready |
| Tools: pickaxe/hatchet/shovel | Quaternius singles (Poly Pizza), Kenney Survival Kit | ⚠️ download + verify |
| Tools: fishing rod, smith hammer, sewing kit | Fantasy Props MegaKit "tools" + Poly Pizza hammers; fishing rod unverified | 🔍 itemize MegaKit; fishing rod may be custom |

## Verified pack details

**KayKit Adventurers 1.0** ([itch](https://kaylousberg.itch.io/kaykit-adventurers), [GitHub](https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0))
- ✅ CC0: repo LICENSE.txt — "License: (Creative Commons Zero, CC0) … free to use in personal, educational and commercial projects."
- Weapons ship as **individual .gltf/.bin pairs** (verified file list): arrow, arrow_bundle, axe_1handed, axe_2handed, crossbow_1handed, crossbow_2handed, dagger, quiver, shield_badge/round/spikes/square (+`_color` variants), smokebomb, spellbook_open/closed, staff, sword_1handed, sword_2handed (+color), wand. Note: the itch blurb says "bow and crossbows" but the 1.0 repo contains **no bow**.
- Single 1024² gradient atlas (downsamples to 128²) → one shared material for every weapon = cheap instancing; same art language as our Quaternius bodies.

**KayKit Skeletons 1.0** ([itch](https://kaylousberg.itch.io/kaykit-skeletons), [GitHub](https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Skeletons-1.0)) — ✅ CC0; verified glTF weapons: Skeleton_Arrow (+broken/half variants), Skeleton_Axe, Skeleton_Blade, Skeleton_Crossbow, Skeleton_Quiver, Skeleton_Shield_Large_A/B + Small_A/B, Skeleton_Staff.

**Quaternius Medieval Weapons** ([official](https://quaternius.com/packs/medievalweapons.html)) — ✅ CC0, fully free, 24 models, swords/daggers/bows/shields/hammers+; **untextured**, FBX/OBJ/Blend (no glTF) → convert once (Blender or FBX2glTF) and rely on tint shader for materials.

**Quaternius Fantasy Props MegaKit** ([official](https://quaternius.com/packs/fantasypropsmegakit.html)) — ✅ CC0, glTF, textured; "weapons and tools" + market stalls/chests/furniture; 60–70% free tier (same free/Source split as the base-characters pack). Itemize the free zip for: fishing rod, smith hammer, sewing kit, scepters/orbs.

**Kenney Survival Kit** ([kenney.nl](https://kenney.nl/assets/survival-kit)) — ✅ CC0, 80 models, animation-flagged; expected axe/pickaxe/tools (page doesn't itemize — verify on download). Kenney 3D has no medieval weapon pack.

**KayKit Dungeon Remastered** — checked: props only (chests/barrels/traps), **no weapons** — still useful for loot-chest visuals.

## Animation coverage warning (free UAL tier)

Our 43 free Universal Animation Library clips include Sword_Attack/Idle, Spell_Simple_*, Punch_Jab/Cross — but **no bow/crossbow-specific clips**. Pistol_Aim_* / Pistol_Shoot can pass for crossbow at low fidelity; a real Bow_Draw/Shoot needs either the paid UAL tier or KayKit's own (all-free, CC0) 161-clip animation pack — which targets KayKit rigs and would need retargeting onto the UBC skeleton (author warns quality "might not look good"). Recommendation: ship crossbow first (models + Pistol-clip stand-in), defer bow until an anim source is chosen.

## Integration plan

1. **Attachment**: weapons are rigid meshes — `handBone.add(weaponMesh)` on the UBC rig's `hand_r` (shield → `lowerarm_l`/`hand_l`). KayKit models are authored grip-at-origin for their own characters; expect a small per-class offset/rotation table (grip → palm alignment), tuned once against Sword_Idle. Same mechanism the AC engine work already uses for held items.
2. **Material tiers (9 metals, iron→legendary)**: reuse the palette-index tint shader planned for skin/hair (docs/hair-facial-features-research.md — same nearest-filter/no-mips constraints). KayKit's single gradient atlas makes this trivial: one mask/palette per weapon material region; 9 palettes = 9 tiers with zero extra textures. Quaternius Medieval Weapons being untextured is a feature here — assign palette indices directly. Legendary tiers: add `emissive`/emissiveIntensity accent (edge glow) + optional point light on the 14-light virtual pool's budget.
3. **Licensing hygiene**: snapshot each pack's LICENSE/license-page text next to the assets (as done for `assets/models/quaternius/`); for Poly Pizza singles record the model URL + license label per file; prefer CC0-only — if any CC-BY (Poly by Google) sneaks in, add the attribution to the site credits page.
4. **Trails/impact VFX (brief)**: standard three.js approach is a ribbon/trail mesh — a triangle strip rebuilt each frame from the blade-tip bone's recent world positions with an additive, alpha-fading material (community TrailRenderer implementations exist; also easy hand-rolled). Impact = a one-shot billboard sprite flash + a few instanced particles; fits the existing acfx/particles systems. Not researched in depth (flagged nice-to-have).

## Open questions

1. Itemized contents of Fantasy Props MegaKit free zip and Kenney Survival Kit (fishing rod? smith hammer? sewing kit?) — resolve by downloading both (~free, CC0).
2. Do KayKit weapon glTFs need axis/scale fixes vs the UBC rig (KayKit authors at 1 unit = 1m like Quaternius, but grip orientation differs per pack)?
3. Bow animation source decision: paid UAL tier vs KayKit anim retarget vs hand-authored single clip.
4. Re-run the full adversarial deep-research harness on this topic if stronger verification is wanted (this pass was single-researcher; primary-source license checks were done for every ✅ above).
