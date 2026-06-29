# Dereth — Build Roadmap

Master execution plan for the game in `index.html`. Ordered by dependency and impact.
Each item: **what**, **why/source**, and **verify**. Workflow for every change =
`jsc` syntax check → preview reload → `preview_eval` asserts → `preview_console_logs level:error`
→ screenshot → open in browser. Facts/lore only from AC research (see the plan file's Phase 6);
all assets stay original geometry + procedural textures.

Legend: ✅ done · ⏳ next · ☐ planned · ◇ optional/later · 🔎 needs research first

---

## ✅ DONE (shipped & verified)
- First-person 3D engine (Three.js r128, local, offline), huge world (16000u) on real AC coordinates.
- 6 attributes + vitals, War/Life magic + Creature/Item self-buffs, melee/bow/wand, crits/status.
- Lifestone bind/respawn, XP→skills, loot→satchel→tinkering/salvage, quests, vendors/potions.
- 24 real towns, 3 heritages (Aluvian/Sho/Gharu'ndim), Direlands, Town Network hub, 50 dungeons.
- Bestiary incl. Olthoi/Lugian/Tumerok/Mattekar/Virindi/Skeleton + Gnawvil & Bael'Zharon bosses.
- Terrain (mountains/valleys/ocean), cobblestone contour roads, sky (sun+halo, moon, clouds, stars,
  warm horizon), reflective water, day/night (slow).
- Vitae death penalty, allegiance/patron, achievements/titles.
- Enterable buildings, class-specific viewmodels, character material textures, realistic swing/shoot/
  cast animations.
- Visible avatar + 1st/3rd-person toggle (V), smooth camera follow + camera collision, body detail
  (hands/feet/pauldrons/cape/quiver).
- **Portal proximity fix** (run-into-them now works).

---

## MILESTONE A — Authenticity quick wins  ✅ DONE
Low risk, high faithfulness; all from verified research (Phase 6.6).
- ✅ **A1. Real vital formulas** in `derive()`: Health = Endurance/2, Stamina = Endurance, Mana = Self
  (confirmed in ACEmulator source). *Verify:* eval attributes→HP/Stam/Mana match formulas; rebalance
  enemy damage if needed so it stays playable.
- ✅ **A2. Name the four magic schools** in the spell bar/character sheet/codex: War (Arm), Life (Heart),
  Creature Enchantment (Left Hand), Item Enchantment (Right Hand). *Verify:* UI shows groupings.
- ✅ **A3. Bestiary threat tiers** (Feeble/Weak/Dangerous/Deadly/Lethal) as a codex label per creature.
  *Verify:* codex (B) shows tier; tiers map sensibly to hp/dmg.
- ✅ **A4. Lore flavor** in intro + codex: Dereth = volcanic isle of the planet **Auberean**;
  Bael'Zharon = the fallen Empyrean **Ilservian Palacost**. *Verify:* text appears, no overflow.

## MILESTONE B — Bestiary & world-lore accuracy  ✅ DONE
- ✅ **B1. Named Shadow generals** as mini-bosses: Ler Rhan, Black Ferah, Isin Dule (Direlands spawns,
  tougher than elites, unique loot). *Verify:* spawn + kill + reward.
- ✅ **B2. Olthoi variants**: Soldier (acid ranged attack) + Worker; per-family **chief/champion** elites
  for Drudge/Mosswart/Banderling/Tumerok. *Verify:* builders + AFFINITY + spawn.
- ✅ **B3. Lore-accurate region clustering**: Lugians → southern **Linvak** mountains; Tumeroks/Virindi →
  **Direlands**; Mosswarts → a **Blackmire** swamp band between Aluvian & Sho lands. *Verify:* `pickKind`
  region pools + a swamp biome tint/water patch.

## MILESTONE C — Graphics-fidelity pass  ◑ (C1–C3 done; C4 optional) (continues the "improve graphics" thread)
- ✅ **C1. Textured ground**: procedural grass/dirt/rock/sand that blends by biome + slope (replace flat
  vertex color). Highest visual ROI. *Verify:* screenshots in each region; fps ok.
- ✅ **C2. Foliage**: grass tufts (instanced/billboard) + trees/bushes clustered around hubs; sway.
  *Verify:* density near towns, none in water, fps ok.
- ✅ **C3. Interior furnishings** for enterable buildings: tables, hearths, shelves, beds, vendor counters,
  rugs, a light source that reads as warm. *Verify:* walk inside, props present, still passable.
- ◑ **C4. Render polish** (mostly done): ✅ softer shadow edges (`sun.shadow.radius`), ✅ blade motion-trail
  swoosh on slash, ✅ FOV kick on sword hit & bow release (eased back to base FOV). ◇ ambient-occlusion fake
  deferred (muddies more than it helps without a real SSAO pass).
- ✅ **C5. Cinematic render pipeline** (jsc + preview verified, 0 console errors). Decision: stay on
  HTML+local Three.js (a C++ engine would break the zero-install/offline constraint and discard a deep,
  working codebase; WebGL2 was nowhere near its ceiling). Added **ACES Filmic tone-mapping** +
  **sRGBEncoding** output + exposure 1.04 (correct modern colour pipeline; every procedural colour
  texture tagged `encoding=sRGBEncoding` via `markSceneSRGB()`, called at init/startGame/enterDungeon/
  enterNetwork). Built a **self-contained inline post-processing chain** (no extra files): render scene →
  full-res target → luminance bright-pass (threshold 0.84) → two 9-tap separable Gaussian blur iterations
  at half-res → composite (additive **bloom** strength 0.7 + mild saturation lift + cool-shadow/warm-
  highlight grade + **vignette** 0.36) to screen. `POST` globals; `initPost`/`resizePost`/`renderComposite`
  drive it; the rAF loop, resize handler, and title render all call `renderComposite()`. Result: glowing
  magic/stars/lights/clouds, filmic contrast, framed vignette — a dramatic upgrade from the old flat look.
- ✅ **C6. Surface & water detail** (jsc + preview verified, 0 console errors). `makeNormalTex(size,hf,
  strength,repeat)` builds tiling tangent-space normal maps from a height function (central-difference
  gradient; kept LINEAR, never sRGB-tagged). **Ground** → `MeshPhongMaterial` (was Lambert) with a
  procedural `groundNormalTex()` + low specular so terrain catches raking sunlight (per-fragment shading;
  colour map still sRGB). **Water** → animated `waterNormalTex()` ripples (normalMap `offset` scrolled each
  frame in `updateDayNight`) + brighter specular sheen. **Foliage** density 2400 → 3600 tufts. Also lifted
  near-black roof palette (Sho `0x2c3a2c`→`0x46604a`, Aluvian `0x5a2a20`→`0x7a3a2c`) that the C5
  tone-mapper was crushing to black. Verified midday grass relief, scrolling water normals, readable roofs.
  ◇ Further (optional): real fresnel water shader, screen-space reflections, MeshStandard PBR everywhere.

## MILESTONE D — Third-person polish  ◇
- ✅ **D1.** Over-the-shoulder camera offset — pivot shifted along camera-right (`SHOULDER=0.7`) so the
  avatar sits to one side and the crosshair stays clear (verified: avatar projects off-center, on-screen).
- ◇ **D2.** Idle breathing/sway when standing; **D3.** foot/leg plant on uneven ground (light IK).

## MILESTONE E — Deeper RPG systems  🔎 (research-gated)
- ✅ **E0. Research pass #2** (105-agent deep-research, adversarially verified, 2026-06-29). Verified &
  appended to plan **Phase 6.7**: skill→attribute base formulas, train/specialize credit costs, 52→102
  credits, Trained-vs-Specialized XP curves, fellowship cap 9 + XP-share tiers. **Still open** (do not
  guess): per-level XP curve & max level, attribute-raise XP, allegiance passup %, all itemization.
- ✅ **E1. Skill system expansion** (full retail ~37 skills, authentic two-currency economy; jsc+preview
  verified, 0 console errors). `SKILLS_DEF` grouped by family (Magic/Defense/Melee/Missile/Trade/
  Tinkering/Lore) with verified `(attrA+attrB)/N` base formulas + train/specialize credit costs.
  Untrained/Trained/Specialized tiers; **skill credits** (52→~100 by level + L35/L90 quest credits;
  70-credit spec cap) to train/specialize; **unspent XP** spent to raise ranks on the verified Trained-
  vs-Specialized curves (pt1=58/23 confirmed). Magic schools require Trained to cast; Specialized adds
  +10 value. Combat rewired to skill values (melee=best weapon skill, missile, war, life+healing,
  meleed+shield defense, Mana Conversion cost cut, Run speed). Heritage creation loadout. Old saves
  migrate (melee→heavy, defense→meleed, etc.). New character-sheet UI with Train/Spec/raise controls.
- ✅ **E0b. Research pass #3** (106-agent deep-research, adversarially verified, 2026-06-29) — appended to
  plan **Phase 6.8**. Un-gated E2 (tinkering/salvage/workmanship mechanics) and E3 (allegiance pass-up
  formula confirmed verbatim). XP-per-level (classic 126 / cumulative 4.28B) and attribute-raise curves
  verified but judged **reference-only** — MMO-grind magnitudes unfit for a single-player homage; keep our
  scaled curves. Still gated: full loot-tier ladder, tinkering success-chance formula, retail-275 table,
  innate attr cap >100.
- ◑ **E2. Itemization depth — workmanship & salvage/tinker core SHIPPED** (jsc + preview verified, 0 console
  errors). Every loot item now rolls a **material** (first word of its name, AC convention) + **workmanship
  1–10** within a loot-tier range (`WORK_TIER`, `MATERIALS`, `rollItem(rare,tier)`; drop sites pass tier by
  source — overworld 1 / elite 3 / dungeon = delve tier / boss & Incursion 5). **Salvaging** gear yields
  units of its material with **units-weighted-average workmanship** (`player.salvage{mat:{units,work}}`;
  Salvaging skill boosts yield). **Tinkering Application**: 100 units of one material + the matching
  tinkering skill trained applies a workmanship-scaled bonus to weapon damage / armor (feeds existing
  `weaponTink`/`armorTink`), with a success chance (our own formula — real one gated) that rises in
  difficulty as bonuses stack; failure costs the salvage (gear spared — player-friendly deviation from
  AC's destroy-both). New Tinker-panel "Salvaged Materials" section; satchel shows workmanship; saved/
  loaded. ◑ *Remaining for full E2:* loot-tier ladder, imbue/rare item classes — still partly gated.
- ✅ **E2b. Distinct weapon types** (jsc + preview verified, 0 console errors). `WEAPON_TYPES` (dagger/
  sword/spear/axe/mace = melee; bow/crossbow = missile), each with a speed (cd mult) / reach / damage /
  stamina profile so the equipped weapon's TYPE drives a real DPS-vs-burst-vs-reach tradeoff (dagger
  fastest+weakest, mace slow+crushing, spear long-reach, crossbow ranged burst). `ITEM_BASE` weapons
  tagged with `wt`; `rollItem` carries it; `equipItem` sets stance from the type (melee→sword viewmodel,
  missile→bow) and logs the blurb; `meleeAttack`/`fireArrow` read `weaponProfile()` for cd/reach/dmg/stam
  (bare fists & mismatched stance fall back to sword/bow defaults). Verified in live combat: spear hits a
  target at 4.6u that a sword (reach 3.4) misses; crossbow fires & auto-switches to bow stance. (Profile
  numbers are our design; AC's exact per-type values stay research-gated.)
- ✅ **E2c. Rare item affixes** (jsc + preview verified, 0 console errors). `WEAPON_AFFIXES` (elemental
  brands fire/frost/shock/nether → element + status + ~25% bonus dmg; crit +12%; lifesteal 22%) and
  `WORN_AFFIXES` (+28 max HP "of Vigor" / +34 stam / +28 mana / +9 armor "of Warding"). `rollItem` rolls
  one affix on weapon/worn/armor drops (85% on rares, 40% on tier≥4) and appends the suffix to the name.
  `applyHit(m,base,opts)` now takes {element,brandDmg,crit,lifesteal} — applies elemMult + weak/resist
  floaters + burn/chill/stun + crit-bonus + life-steal heal; `meleeAttack` builds opts from the weapon's
  affix; `fireArrow` rides the projectile's existing element/burn/slow/stun/drain channels. Worn vital
  affixes fold into `derive()` (non-cumulative); `wornArmorV()` + armor affix; equipping worn gear calls
  `derive()`. Verified: fire-brand burns a fire-weak mosswart for boosted dmg, lifesteal heals, "of Vigor"
  raised max HP 86→114, "of Warding" stacked +9 armor.
- ✅ **E2d. Armor types** (jsc + preview verified, 0 console errors). `ARMOR_TYPES` light/medium/heavy with
  armor × speed × stamina-regen tradeoffs; worn bases tagged `at`; `rollItem` carries it; `wornArmorV()`
  scales protection by `armorMul`, `derive()` scales `player.speed` by `speedMul`, stamina regen scales by
  `stamMul`. Verified: heavy = +40% armor / −10% speed / −28% stam-regen; light = −20% armor / +6% speed /
  +25% stam-regen. (Profile numbers our design.)
- ✅ **C7. Fresnel water** (jsc + preview verified, 0 console errors). Kept MeshPhong (preserving C6 ripple
  normals + specular + shadows + FOG) but injected a fresnel term via `onBeforeCompile` before
  `<fog_fragment>`: deep/dark looking straight down, bright sky-reflection at grazing angles, fog-correct
  at distance. Verified at an ocean shore — clear depth gradient.
- ✅ **E2e. Imbue tinkering** (jsc + preview verified, 0 console errors). `IMBUE_MAT` (Jet→nether,
  Amber→fire, Diamond→shock): applying 100 units of an imbue-type salvage bag (with Weapon Tinkering)
  **brands your equipped weapon** with that element instead of adding flat damage — ties the salvage,
  affix, and weapon-type systems together. Strips any prior brand suffix, sets `weapon.affix`, renames the
  weapon; tinker-panel bag row shows "→ imbue weapon (element)". Verified: plain Steel Sword + Jet →
  "Steel Sword of the Void", lands nether hits.
- ◇ **E3. Allegiance/monarchy depth** — pass-up formula now VERIFIED (plan 6.8) and ready to build, BUT in a
  single-player game pass-up to a patron has no payoff loop. Build only if we add an **NPC-vassal allegiance
  tree** (player as patron receiving pass-up income). Current Loyalty-tier XP bonus in `gainXP` stays as the
  flavor stand-in.

## MILESTONE F — Optional later-era / stretch content  ◇
- ✅ **F1. Void magic** — "Nether Bolt" spell (key Z, school `void`, requires Void Magic trained):
  corruption projectile, burn DoT + 35% life-drain, scales with the Void skill. Also shipped the
  **Summoning** spell ("Summon Wisp", key X, requires Summoning trained): conjures up to 2 allied wisps
  that float beside the player and fire nether bolts at nearby foes; damage/lifetime scale with the
  Summoning skill; despawn on expiry/instance-change/death. (Both close the loop on the E1 skills that
  had no behaviour yet. jsc+preview verified, 0 console errors.)
- ◇ **F2. Spell components** as collectible boosters (scarab/herb/powder/potion/talisman/taper) + Foci;
  **Augmentation/Transfer gems** for attributes. 🔎 research-gated (covered by the running pass #3).
- ◇ **F3. Outlying islands** (e.g. Aerlinthe-style) as special high-tier zones.
- ✅ **F4. Live-event flavor — Incursions** (jsc + preview verified, 0 console errors): every ~3 min a
  themed horde (Shadow Incursion / Olthoi Swarm / Undead Rising / Banderling Raid) besieges one of the
  towns nearest the player. A pulsing additive light **beacon pillar** marks the town (visible from afar)
  plus an orange compass marker and a HUD "Incursion" countdown row (town · kills/total · seconds). Clear
  all invaders before the 240s timer to **repel** it for a scaled bounty (gold + XP + 2 rolled items);
  let it expire and the horde fades. Mob HP/XP scale with player level. Events pause inside dungeons/Town
  Network and reset on new game/load. `worldEvent`/`eventCd` globals; `EVENT_TYPES`, `startEvent`,
  `eventSuccess`, `eventFail`, `endEvent`, `updateEvents` (called in `update()`).

---

## Suggested order of execution
**A (all) → C1 → C2 → B1 → B2 → B3 → C3 → A-leftovers polish → E0 (research) → E1/E2/E3 → D → F.**
Rationale: finish the cheap authenticity wins first (A); then the biggest visual upgrades (C1/C2) since
"improve graphics" is a standing request; interleave bestiary/lore (B) which is mostly data; do interiors
(C3) once furnishings have a lit space; gate the deep RPG systems (E) behind a second research pass so we
build them on real numbers, not guesses; camera/stretch polish (D/F) last.
