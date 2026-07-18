# Hair & facial features for the Quaternius bodies — research

*Researched 2026-07-18 via multi-agent deep-research sweep (5 search angles, 21 sources fetched, 96 claims extracted, 25 adversarially verified with 3-voter panels: 23 confirmed, 2 refuted). Constraint: assets are served raw to browser MMO clients, so licenses must permit commercial redistribution (CC0 preferred, CC-BY acceptable). Companion to `rigged-human-models-research.md`; targets the 65-bone UE-named rig now committed at `assets/models/quaternius/`.*

## TL;DR — recommended stack

1. **Buy the Quaternius Universal Base Characters Source tier ($19.99 min, 600 MB)** — CC0 in both tiers, so all **20 mix-and-match hairstyles** plus the customizable eye/skin-color material setups can legally be served raw to clients. The free Standard tier we have ships only 2 bodies + 5 hairstyles + 1 beard (+ 2 eyebrow meshes).
2. **Skin/hair/eye color via shader tinting**, not baked texture variants — this is both Quaternius's own intended approach (Source ships tint shaders for Blender/UE/Unity/Godot; trivial to re-implement) and the established three.js pattern. Best fit for Dereth: **grayscale palette-index texture + palette uniform lookup**, which maps directly onto the existing acheads palette-RGB-remap pipeline.
3. **Face variation via morph targets embedded in the body glTF** (natively supported on SkinnedMesh in both our vendored WebGL and WebGPU builds), or swappable face textures on the shared head as the cheaper fallback.
4. **AC-head port = unproven stretch goal.** No claim about attaching the extracted acheads to the Quaternius head bone survived verification — head-bone attachment and palette-remap are individually proven patterns, but the neck seam and art-style clash are unresearched.

## 1. Hair / beard / eyebrow assets

**Quaternius Source tier is the only verified drop-in source.** *(high confidence, 3-0×6)*
- CC0 across free and paid tiers — "Free to use in personal, educational and commercial projects. (CC0 License)" verbatim on both the [official page](https://quaternius.com/packs/universalbasecharacters.html) and [itch.io](https://quaternius.itch.io/universal-base-characters); CC0 waives all copyright so raw client-side serving is unrestricted.
- Paid Source zip ($19.99 minimum, 600 MB): all 20 hairstyles, all base bodies (sources inconsistently say 6 vs 8 — Superhero/Regular/Teen × M/F — verify after purchase), rigged .blends, engine projects with customizable skin/eye/hair.
- Free tier reconciliation: our on-disk `assets/models/quaternius/hair/` = 5 hairstyles + 1 beard + 2 eyebrow meshes.

**Third-party packs — no verified drop-in exists.** *(high confidence)*
- **OverScore "Proxy"** ([OpenGameArt](https://opengameart.org/content/overscore-proxy-modular-low-poly-female-character-creation-set)): best CC0 candidate — 30 named female hairstyles (A-Line Bob, Afro, Cornrows, Pixie Cut…), 11 natural + 11 dye presets. But: **unrigged, no UVs, untextured, .blend-only** — needs a Blender export/parenting pass, and palette/texture coloring is impossible until UV-unwrapped. No beards/eyebrows. Bench it unless we want a Blender pipeline.
- **Quaternius Ultimate Modular Men**: whole-character archetypes in 4 body segments; no standalone hair/facial attachments (page-level check; GLB submeshes not inspected).
- **Poly Pizza was NOT ruled out** — the claim that it has no standalone hair assets was refuted 1-2 (insufficient evidence either way). A targeted re-search of its People & Characters/prop categories is worth an hour.

## 2. Face variation on a shared body

**Morph targets (blend shapes) — the native path.** *(high confidence, 3-0×2)*
- three.js SkinnedMesh exposes `morphTargetInfluences` (per-target 0–1 weights); verifiers confirmed the pipeline exists in **our own vendored** `three.core.js` (~line 23378) **and** `three.webgpu.js`, wired up by GLTFLoader.
- Maintainer guidance (Mugen87, [discourse #27606](https://discourse.threejs.org/t/facial-morphs-on-a-skinnedmesh-character/27606)): **embed morphs in the same glTF as the skinned character**, don't ship separately. Texture-based morph storage (since ~r133) removed the old 8-target limit.
- The official `webgl_animation_skinning_morph` example is itself a modified Quaternius CC0 model.
- Cost: we author the face morphs ourselves in Blender on the Quaternius head (chin, nose, brow, jaw sliders) — one-time art task, then chargen gets real face sliders.

**Swappable/atlased face textures** — cheaper alternative: N painted face variants on the shared head UVs (classic low-poly approach, starcubelabs). Combines freely with morphs.

**AC heads (acheads) onto the Quaternius head bone — unproven.** Head-bone mesh attachment is a proven three.js pattern (discourse #6182, #61189: identically-rigged parts can share one skeleton; rigid props parent to a bone), and palette-remap is proven (below) — but no evidence survived on the specific port: neck-seam strategy (delete vs hide the Quaternius head vertices), skin-tone matching across the seam, and the art-style clash are all open. Treat as a spike, not a plan.

## 3. Skin-tone / color variation techniques

Three verified patterns, in order of fit for Dereth:

**A. Palette-index lookup (RECOMMENDED — matches acheads pipeline).** *(medium confidence, 3-0×3)*
Store the character texture as grayscale palette indices; resolve in the fragment shader: `Color = Palette[int(texture(tex, uv).r * 255)]` against a palette uniform array ([pvigier](https://pvigier.github.io/2019/10/06/palette-swapping-with-shaders.html), corroborated by webglfundamentals + ColorMod). Colors swap at runtime with zero texture reloads. Constraints (verified): **nearest filtering, no compression/mips** on the index texture; add +0.5 before the int cast; needs WebGL2/WGSL dynamic indexing; in PBR the lookup runs before lighting.

**B. RGBA-mask tint via onBeforeCompile.** *(medium confidence, 3-0×3)*
Keep MeshStandardMaterial, inject GLSL after `#include <color_fragment>`: one RGBA mask texture = 4 independently tintable regions (skin/hair/etc.) driven by 4 vec3 uniforms, one draw call ([rohinknight — domain currently dead, cite Wayback 20260306144254](https://web.archive.org/web/20260306144254/https://rohinknight.com/posts/Color-Swapping-ThreeJS/); mechanism corroborated by official onBeforeCompile docs + cyanilux). Demonstrated on a CC0 KayKit character: swappable parts' UVs sit on **white gradient swatches** so multiplicative tint preserves baked shading. Gotchas: breaks `material.clone()` unless `customProgramCacheKey` is set; **GLSL-only — our WebGPU path needs a TSL equivalent** (open question).

**C. Vertex-color lerping (Relm).** *(medium confidence, 3-0)*
Production three.js game: Blender vertex groups exported as `_vg_hair`/`_vg_skin` custom glTF attributes; engine lerps annotated vertices' colors toward the player's choice. Less applicable to Quaternius's texture-atlas models. (Companion claim that Face Maps beat Vertex Groups was **refuted 0-3** — ignore that detail.)

Quaternius's own intent *(high confidence, 3-0)*: the Source tier ships skin/eye tint shaders ("hundreds of unique combinations") for Blender/UE/Unity/Godot — i.e. runtime tinting layered on BaseColor, which we re-implement in three.js as a color multiply/ramp. The shipped Light/Dark BaseColor PNGs are just two baked presets.

## 4. Character-creator architecture (three.js practice)

- One shared skeleton per character; body/hair/outfit parts as separate skinned or bone-parented meshes bound to it (discourse #61189, #18536 — overwrite each part's skeleton with the shared one; rigid hair parents straight to the head bone, which is exactly how the pack's `hair/` glTFs are authored).
- Modular-seam practice (UE modular-characters doc, polycount): keep shared border vertices identical in position/normals/weights across parts; hide seams under attachment geometry (hairline, collar) — relevant when we cut the head or mix outfit pieces.
- Chargen flow: pick body proportion + face (morph weights / face texture) + hair mesh + palette indices for skin/hair/eyes → serialize as a few bytes in the character record (fits our existing chargen save shape).

## Refuted claims (do not build on these)

1. "Poly Pizza has no standalone hair/facial assets" — 1-2, insufficient evidence; re-search warranted.
2. "Face Maps > Vertex Groups for clean color boundaries" — 0-3.

## Open questions

1. AC-head port feasibility spike: attach one acheads head to the Quaternius head bone, delete vs hide the native head, match palette-remap skin to the tinted body, judge the style clash on screen.
2. After buying Source: are the 20 hairstyles separate ready-to-attach glTFs like the free 5? 6 or 8 bodies?
3. Targeted Poly Pizza sweep for CC0 standalone hair/beard/eyebrow meshes.
4. Single-source tint shader for both render paths: TSL/node-material implementation of the mask-tint or palette lookup that compiles to WebGL *and* WebGPU (avoids duplicated shader logic).

## Integration plan for Dereth

1. **Now (free tier, $0):** wire the 5 hairstyles + beard + eyebrows as head-bone attachments in the #1008 body swap; hair color via palette/tint shader (start with technique A on the hair texture only).
2. **Buy Source ($19.99)** once #1008 lands: 20 hairstyles + Regular/Teen proportions → chargen variety. Snapshot its license file next to the assets like the current two.
3. **Face pass:** author 4–8 face morphs on the shared head in Blender, embed in the body glTFs; add chargen sliders. Optionally add N face-texture variants first as a quick win.
4. **Skin-tone pass:** convert body BaseColor to palette-index (reusing acheads remap tooling), expose skin palettes in chargen; same shader handles hair + eyes.
5. **Stretch spike:** AC-head attachment test (open question 1) — timebox it; if the style clash is bad, morphs + face textures are the answer.

## Caveats

Pricing/tier contents are live itch.io listings (verified 2026-07-18; $19.99 is a minimum). Source counts for paid bodies (6 vs 8) conflict. Two free-tier counting claims passed 2-1. The two key shader write-ups are blog-quality (one dead domain → Wayback, one mirror-only), corroborated on mechanism but not full pipeline. Source-tier tint shaders are not drop-in for three.js. onBeforeCompile covers WebGL only.
