# Asheron's Call — Regions, Creatures & Respec Reference

Compiled 2026-07-01 from acpedia.org / asheron.fandom.com (+ Drunkapedia/Levistras/loresraat mirrors;
primaries block automated fetch). Drives the game's biome flora, per-region creature spawns, and the
XP-based attribute/skill raise-and-lower systems.

## Per-region biome (implemented in `biomeAt(x,z)` + `pickKind`)

| Region / culture | Biome | Vegetation | Typical creatures |
|---|---|---|---|
| **Aluvia** (Holtburg, Cragstone, Arwic, Glenden Wood, Rithwic…) | temperate **forest**, hilly | deciduous + pine, large trees, bushes | drudge, mosswart, rat, mite, banderling, reedshark, shreth, phyntos wasp, skeleton, zombie, ursuin |
| **Sho** (Hebian-To, Shoushi, Nanto, Baishi, Lin…) | **wooded hills** | broadleaf + conifer, bushes | shreth, drudge, phyntos wasp, banderling, mosswart, sclavus, wisp, mite |
| **Blackmire** (Sawato, Yanshi edge) | **swamp** | reeds, scraggly wetland trees | moarsman, mosswart, sclavus, gromnie, reedshark |
| **Gharu'ndim** (Yaraq, Zaikhal, Al-Arqas, Uziz, Xarabydun…) | arid **desert** | sparse: palms + cactus at oases, barren sand | skeleton, drudge, rat, shreth, monouga, mummy, zombie, tumerok, gromnie |
| **Direlands** (Fort Tethana, Stonehold, Plateau Village…) | barren **volcanic mountains** | boulders + dead trees, near-vegetation-free | tumerok, lugian, gromnie, virindi, golem, olthoi, sclavus, grievver, zombie, shadow |
| **Frozen frontier** (Fiun Outpost, Eastwatch, Kara, Linvak Tukal, Timaru, far N/S) | **snow/ice mountains** | snowy conifers, sparse, boulders | mattekar, gromnie, tusker, skeleton, wisp, golem, ursuin |

Notes: Aphus Lassel is *tropical* (palm/tuskers), not frozen, despite the "Lassel" name. Marae Lassel is a
snow-and-plains patchwork. Ground colour is set per-vertex by biome (sand/snow/barren-grey); the ground
texture was neutralised to a warm grey so biome tints read.

## Creature roster — added this pass

AC has a **76-class** creature enum (~3,000 individual creatures). The game had ~17 kinds; this pass added
16 more via procedural builders + BESTIARY + AFFINITY + PHYS_RESIST: **rat, shreth, mite, sclavus, zefir,
monouga, ursuin, moarsman, phyntos wasp, wisp, golem, zombie, mummy, grievver** (hostile) and **rabbit, cow**
(ambient — flee, never fight). Bosses present: Bael'Zharon, Olthoi Queen, Shadow Generals (Ler Rhan/Black
Ferah/Isin Dule), Gnawvil.

**Still missing (lower priority, if extending):** niffis, slithis, carenzi, snowman, gotrok lugian, hollow
minion, crystal/knathtead, chittick, shallows shark, elementals (fire/frost/acid/lightning as walking
foes — currently only spell elements), ruschk, thrungus, deru, burun, mukkir/margul, gear knight, siraluun,
and named bosses Gaerlan / Aerbax / Grael / Martine. (AC has **no** classic winged dragon base class — the
draconic line is Gromnie.)

## Attribute & skill raise / lower (implemented)

Authentic AC (Drunkapedia): attributes are **bought with unassigned XP** (not free level points), innate cap
**100** (+50 via augments, ~290 buffed); first point ~110 XP rising to ~3.26M cumulative at 100. Vitals are a
separate cheaper XP track (Health=End/2, Stam=End, Mana=Self). Skills: credits gate Train/Specialize; XP
raises value to **208 trained / 226 specialized (+10 spec bonus)**; spec-credit cap **70**. Respec: level-25+
gems (10-pt attribute swaps, per-skill unlearn refunding XP) + full resets (Chafulumisa attributes / Fianhe
skills — first free, then MMD+Luminance on long cooldowns; refund XP & credits).

**Game now:** attributes bought with XP (`attrCost` = ~25 at pt1 → ~52k near cap), 100 innate cap, lowering an
attribute refunds its XP, Chafulumisa reset refunds all attribute XP. Skills already raise with XP (208/226,
70-credit spec cap). Legacy saved skill-points remain spendable free for migration.

## Free 3D nature assets (task: "find artwork on the internet")

Chosen approach: **procedural original geometry** (palm/cactus/dead-tree/snow-pine/reeds/broadleaf/pine/bush/
boulder) — reliable, offline, matches the project's asset principle. If real imported models are wanted later,
verified **CC0 hot-linkable** Quaternius "Ultimate Stylized Nature" GLBs are available on the poly.pizza CDN
(CORS `*`, no Draco, load with r128 GLTFLoader like the existing character/monster models), e.g.:
Pine `https://static.poly.pizza/42a2a958-040d-4ce3-bae5-2332c1282cb5.glb`, Trees `…/53a83125-…glb`,
Bushes `…/11bcb3a1-…glb`, Rocks `…/01671e28-…glb`, Palm `…/88fb0209-…glb`. Best practice: mirror the CC0 GLBs
into the repo (raw.githubusercontent / jsdelivr) to avoid third-party CDN risk. Sources: poly.pizza bundle
`Ultimate-Stylized-Nature-Pack`, KhronosGroup glTF-Sample-Assets.
