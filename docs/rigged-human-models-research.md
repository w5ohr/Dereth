# Free rigged human models — research for replacing NPC/player models

*Researched 2026-07-18 via multi-agent deep-research sweep (22 sources fetched, 25 license/spec claims adversarially verified 3-0 each; 0 refuted). Constraint: genuinely free for commercial use inside a live browser MMO (assets are served raw to clients, so "no redistribution" licenses fail).*

## ✅ SPIKE TEST RESULTS (2026-07-18) — Quaternius stack VALIDATED

Ran the in-engine spike (open question 4) same day, using the free tiers downloaded from itch.io
(Universal Base Characters[Standard].zip 122 MB, upload 15861669; Universal Animation Library[Standard].zip 15 MB, upload 17958403).
Test page: `spike/spike.html` (gitignored along with the assets; serve repo root and open
`/spike/spike.html?n=<count>&gpu=1`).

**Key finding — no retargeting needed at all.** The Universal Animation Library GLB and the base
bodies share the *identical* skeleton (65 bones, UE-mannequin naming: pelvis, spine_01…03,
clavicle/upperarm/lowerarm/hand_l|r, full finger chains, ball/foot/calf/thigh). Every one of the
43 free clips' track targets bound by name via plain `THREE.AnimationMixer` on a
`SkeletonUtils.clone()` of the body — **0 unmatched tracks**. `SkeletonUtils.retargetClip` never needed.

**Works on both render paths** (this machine: M-series Mac, Chrome):
GLTFLoader loads bodies + clips in ~500 ms total; skinning correct under WebGL and WebGPU
(`vendor/three.module.js` / `vendor/three.webgpu.js` from the repo).

**Perf** (synchronous bench: mixer.update + render, unique SkeletonUtils clones, no instancing,
no LOD, 14,318 tris/body, 2560×1440):

| instances | WebGL ms/frame (fps) | WebGPU ms/frame (fps) |
|---|---|---|
| 60  | 3.3 (≈307) | 3.5 (≈287) |
| 300 | 18.3 (≈55) | 24.0 (≈42) |

Dozens of on-screen NPCs cost single-digit ms — fine without any optimization; hundreds would want
LOD/impostors. WebGPU is slightly *slower* per skinned draw at high counts (CPU-side backend overhead).

**Free-tier animation coverage is a strong AC fit:** Idle/Walk/Jog/Sprint, Jump_Start/Loop/Land,
Sword_Attack + Sword_Idle, Spell_Simple_Enter/Idle/Shoot/Exit, Hit_Chest/Hit_Head, Death01,
Crouch, Swim_Idle/Fwd, Sitting set, Dance, Interact, PickUp, Roll, Punches. (Pistol clips ignorable.)
`UAL1_Standard_RM.glb` variant carries root motion.

**Visual quality:** sculpted anatomy, real fingers/toes, modeled face with eyes+eyebrows —
far above the current procedural jointed body. Free tier bodies are the **Superhero** proportion
only (M+F); Regular/Teen proportions are in the paid ($19.99, still CC0) set.

**Gotchas found:** the glTFs reference two texture filenames that don't ship
(`T_Hair_1_Normal_png.png`, `T_Eye_Normal_png.png`) — copy/rename from the non-`_png` versions.
itch.io free downloads scriptable via POST `<game>/download_url` (csrf from page cookie) then
POST `<game>/file/<upload_id>?source=view_game` → CDN URL (60 s expiry).

**Still untested:** hairstyle attachment (head-bone-rigged glTFs are in the pack), the Modular
Fantasy Outfits pack, AC head attachment / `dressAvatar()` port, blending clips at Dereth's real
NPC counts inside the actual game loop.

## Recommended stack (all CC0, coherent ecosystem)

**1. Quaternius — Universal Base Characters** — https://quaternius.com/packs/universalbasecharacters.html
- Male + female neutral base bodies in 3 proportions (Superhero/Regular/Teen), designed to be dressed/textured; 20 hairstyles.
- ~13k tris average, game-ready animation-optimized topology. OBJ/FBX/**glTF**.
- Humanoid rig built for retargeting in any engine; compatible with his Universal Animation Library. (No literal Mixamo skeleton.)
- **License: CC0** (verified on quaternius.com + itch.io mirror). Free tier = 2 bases (M+F) + 5 hairstyles; full 8-base/20-hair set with .blend rigs is $19.99 — still CC0 once obtained.

**2. Quaternius — Modular Character Outfits: Fantasy** — https://quaternius.com/packs/modularcharacteroutfitsfantasy.html
- 12 medieval-fantasy outfits from 62 modular parts, 3 texture variants each; same retargetable humanoid rig; fits the Universal Base Characters. Clothing only (no bodies/animations). FBX/glTF. **CC0.**

**3. Quaternius — Universal Animation Library** — https://quaternius.com/packs/universalanimationlibrary.html
- Universal humanoid rig, actively maintained (Jan 2026 retargeting devlog). Free tier ~45 of 120+ animations; full set paid, still CC0.
- Three.js integration is manual retarget: `SkeletonUtils.retargetClip` + bone map (no built-in humanoid retargeter). See also https://github.com/upf-gti/retargeting-threejs

## Stylised low-poly alternative (best crowd performance)

**KayKit — Adventurers** — https://kaylousberg.itch.io/kaykit-adventurers
- Free tier: 5 fully rigged/animated/textured fantasy characters + 25+ weapons/accessories. Single 1024² gradient atlas (downsamples to 128²) → one material, cheap batching/instancing. FBX/**glTF**. **CC0** (full CC0 1.0 LICENSE.txt in the GitHub repo).

**KayKit — Character Animations** — https://kaylousberg.itch.io/kaykit-character-animations
- **161 humanoid animations, all free, CC0** — idle/hit/death, walk/run/jump/crouch/dodge, melee, ranged/spellcasting, emotes, tools (fishing, lockpicking, digging — good UO-crafting fits). FBX/glTF.
- ⚠️ Targets KayKit's own Rig_Medium (~100+) and Rig_Large (25+) skeletons — retargeting onto other bodies works but "might not look good" per the author.

## Secondary / filler sources

- **Quaternius — Ultimate Modular Men** (https://quaternius.com/packs/ultimatemodularcharacters.html): 11 rigged males, 4 swappable modules each, 24 animations, FBX/OBJ/glTF/Blend. **CC0.** (Women counterpart pack exists but was not license-verified this pass.)
- **OpenGameArt — Low Poly Human Pack** (https://opengameart.org/content/low-poly-human-pack): 4 rigged + 2 unrigged humans, **CC0**, rig format unstated — inspect before use.
- **Poly Pizza** (https://poly.pizza/explore/People-and-Characters): discovery aggregator (thousands of low-poly characters, largely Quaternius mirrors). No inline license labels on cards — check each model page / use the Licence=CC0 filter.
- **Sketchfab**: "Human Models Set - Male/Female (Rigged)" by lzyassoul is CC-BY (attribution required). Sketchfab generally: filter to CC0/CC-BY and check per model.

## Flagged — NOT cleared for this game

- **Mixamo (Adobe)**: royalty-free in games, but terms forbid redistributing content "as standalone assets" — a browser MMO serves raw GLB/FBX files to every client, which plausibly violates that. Treat as **unverified/risky** until the EULA is read specifically for web-served assets. (https://helpx.adobe.com/creative-cloud/faq/mixamo-faq.html)
- **MakeHuman**: license page claims a CC0 exception for exported characters — promising for more-realistic AC-like bodies, but not adjudicated this pass. (https://static.makehumancommunity.org/about/license.html)

## Integration notes for Dereth

- All recommended packs load directly via GLTFLoader (WebGL + WebGPU paths).
- ~13k-tri skinned Quaternius bodies are fine for dozens of NPCs; large crowds need LOD/instancing (see https://discourse.threejs.org/t/optimization-of-large-amounts-100-1000-of-skinned-meshes-cpu-bottlenecks/58196). KayKit is far cheaper per head.
- No pack uses the AC skeleton — replacing the current jointed AC body means either retargeting `acMotionTick`/AC animations onto the new rig or adopting the pack's own animation library wholesale.
- `dressAvatar()` (clothing overlays) and the AC head attachment would need porting to the new bodies; Quaternius outfits are full rigged garments (swap, not overlay), which actually matches the authentic AC ClothingTable model-swap approach.
- Snapshot licenses: save each pack's license page/LICENSE.txt into the repo next to the assets (CC0 is irrevocable for downloaded copies, but tiers/pricing change).

## Open questions

1. Mixamo EULA vs. web-served raw assets (needs a dedicated read if ever wanted).
2. Ultimate Modular **Women** pack: CC0 + rig parity with the Men pack? (Very likely, unverified.)
3. MakeHuman/Sketchfab as sources for less-stylised, more AC-authentic body proportions.
4. In-engine spike test: Universal Animation Library clips retargeted onto Universal Base bodies under the WebGPU skinning path, at Dereth's real NPC counts.
