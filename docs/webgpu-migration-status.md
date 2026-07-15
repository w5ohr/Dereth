# WebGPU migration — live status (shared scratch log)

Both machines/agents update this. Plan: [webgpu-migration-plan.md](webgpu-migration-plan.md).
**Branch: `webgpu`** (never work on `main` until Phase D cutover). Always `git pull --rebase origin webgpu`
before you touch anything.

---

## Phase A — ESM foundation  →  ✅ COMPLETE

**Done (machine 1, this commit):**
- Vendored Three.js **r159 ESM** into `vendor/` (SAME revision as the old `three.min.js` — pure loader
  swap, no behavioural change): `vendor/three.module.js`, `vendor/jsm/loaders/GLTFLoader.js`,
  `vendor/jsm/utils/SkeletonUtils.js`, `vendor/jsm/utils/BufferGeometryUtils.js` (GLTFLoader dep).
- **File-split** (the Phase-C parallelization prerequisite): the two inline `<script>` blocks moved to
  `js/game.js` (33k lines) and `js/craft.js`. `index.html` is now ~940 lines (HTML/CSS + boot only).
- **Boot bootstrap** (`index.html`): importmap + a `type=module` that imports THREE + addons, copies the
  frozen ESM namespace to a mutable `window.THREE`, re-attaches `THREE.GLTFLoader` / `THREE.SkeletonUtils`,
  then **injects** `js/game.js` + `js/craft.js` (`async=false`, order-preserving).
  - ⚠️ **Gotcha learned:** relying on defer/module *document-order* to run the module before a deferred
    classic script did NOT hold — the classic script ran first and its no-THREE guard wiped `<body>`
    (removing the module element) before it executed. Inject the game scripts from inside the module
    instead. Also: the ESM namespace is frozen → must copy before adding `.GLTFLoader`.
- **sw.js** (v7→v8): `js/game.js`+`js/craft.js` are network-first (they're the app code, change every
  deploy); `vendor/*` is stale-while-revalidate (pinned dep). Removed the `three.min.js` cache rule.
- Removed unused `three.min.js`, `vendor/GLTFLoader.js`, `vendor/SkeletonUtils.js` (UMD).
- **Verified** (Browser pane, real load): `THREE` is object r159; `THREE.GLTFLoader`/`SkeletonUtils.clone`
  present; game boots, world renders (avatar + NPCs via SkeletonUtils + 186 ore nodes), renderer still
  `WebGLRenderer` (parity), **zero console errors**.
  - Testing note: a stale service worker served old HTML — had to `unregister()` + clear caches + load a
    cache-busted `?nocache=N` URL in a fresh tab. Do the same when verifying.

**A.4 — version bump  →  DONE (machine 1):**
- Bumped **r159 → r185** (latest stable; `three/webgpu` + `three/tsl` are mature here). Vendored the
  split build: `vendor/three.module.js` (650 KB, re-exports `./three.core.js`) + `vendor/three.core.js`
  (1.44 MB, REVISION 185) + r185 jsm addons. Importmap unchanged.
- **Breaking-change handled:** `useLegacyLights` was removed (~r165) — r185 is physical-lights-only, so
  `renderer.useLegacyLights=true` is now a no-op. Verified this does NOT regress the look: a runtime
  toggle produces identical luminance (118.1 both ways), and both the Coronation instance and the sunlit
  overworld render visually identical to r159 (ACES tone-mapping absorbs the legacy→physical delta).
  Comment at game.js updated; a future light re-tune, if ever wanted, is an on-hardware eyeball pass.
- **Custom shaders survive the bump:** overworld render confirms the ground detail-blend, water reflection,
  foliage/wind-sway `onBeforeCompile` shaders and the analytic-sky `ShaderMaterial` all still compile and
  render correctly on r185 (they get rewritten to TSL in Phase C regardless).
- **Verified:** boots, zero console errors, addons work, instance + overworld render at parity. Still on
  `WebGLRenderer`.

## Phase B — WebGPURenderer scaffold  →  ✅ COMPLETE (machine 1)

- **Opt-in flag `?gpu=1`.** The bootstrap (`index.html`) only then dynamic-imports `three/webgpu`
  (2.17 MB) + `three/tsl`, exposing `window.WebGPURenderer` + `window.TSL`. Default WebGL path never loads
  them (verified: `webgpuGlobal:false`, WebGPU bundle absent without the flag). Importmap gained
  `three/webgpu` + `three/tsl` (both vendored: `vendor/three.webgpu.js`, `vendor/three.tsl.js`; webgpu
  re-exports the same `three.core`, so constructor identity is shared → `instanceof` holds).
- **`initThree` is now async.** With `window.WebGPURenderer` present it does
  `new WebGPURenderer(...)` → `await renderer.init()` → `IS_WEBGPU=true`; any failure falls back to
  `WebGLRenderer`. The boot `Promise.all().then(async()=>{ await initThree(); … })` awaits it.
- **WebGL-only paths gated by `IS_WEBGPU`:** `initPost()` skipped (the GLSL POST chain =
  WebGLRenderTarget + `ShaderMaterial` passes), `POST.on` forced false, and `renderComposite()` takes a
  direct `renderer.render(scene,cam)` early-return (also skips `renderReflection()`'s RT).
- **Verified:** `?gpu=1` → WebGPURenderer on the **native WebGPU backend**; the real game loop
  (`loop()`→`renderComposite()`→ sync `renderer.render`) drives 60 frames with **zero errors**; stock
  materials render (NPCs via SkeletonUtils, torches, floor). Default path unchanged (WebGLRenderer, full
  POST, `rtScene` built, no errors).

**Phase B's scaffold makes the Phase-C worklist concrete.** On WebGPU the console logs
`THREE.NodeBuilder: Material "ShaderMaterial" is not compatible` (non-fatal — skipped) for the **6 custom
`ShaderMaterial`s**, and the **6 `onBeforeCompile`** materials render as plain MeshStandard (patches
ignored). The scene renders **dimmer/wrong** on WebGPU because the sky shader + POST tone-map/bloom are
absent — that IS Phase C, exactly as planned. Nothing to "fix" in B.

## Phase C — TSL/NodeMaterial rewrites  →  IN PROGRESS (parallel; see plan §4 ownership table)

Concrete targets confirmed by the B scaffold: sky-dome / portal / aetheria / 2 misc `ShaderMaterial`s +
the ground-blend / water / wind-sway `onBeforeCompile`s + the whole POST chain (bloom/tonemap/vignette/
SSAO) + `renderReflection`. Split per the plan's ownership table. `window.TSL` is already wired.

**FX/post half (machine 1) — started:**
- ✅ **Tone-mapping on the WebGPU direct path.** WebGPURenderer clears `toneMapping`/`toneMappingExposure`
  back to defaults (NoToneMapping / 1) during its deferred backend init, so the direct render was
  un-tonemapped (in the WebGL build the POST composite owns tonemapping). `renderComposite()`'s WebGPU
  branch now re-asserts ACES + the brightness-graded exposure, guarded so it only writes on drift (no
  per-frame pipeline recompile). Verified: `toneMapping` holds at ACES(4) through the loop, no errors,
  WebGL path untouched; a positioned-camera luminance probe confirmed the right direction (ACES 133 vs
  none 110). `three/webgpu` `PostProcessing` + `QuadMesh` and 638 TSL nodes confirmed available.
- ✅ **WebGPU POST node graph (bloom + tonemap).** Vendored `vendor/jsm/tsl/display/BloomNode.js`; the
  bootstrap exposes `window.PostProcessing` + `window.bloomNode` (only under `?gpu=1`). `buildGpuPost()`
  builds a `PostProcessing` graph — `pass(scene,cam)` + `bloom(...)`, `outputNode = scenePass.add(bloom)` —
  built lazily in `renderComposite`'s WebGPU branch (after ACES is set, so ACES bakes into the output;
  exposure stays a runtime uniform → brightness setting works live). Replaces the plain direct render.
  Verified: game loop drives `GPUPOST.render()` (sync) with zero errors; richer output than direct
  (avgLum 24.8 vs 9.3; bloom spreads light); WebGL default path untouched (WebGPU POST modules not loaded).
  ⚠️ **Bloom params (strength/radius/threshold) are first-pass defaults — need an on-hardware eyeball.**
- ✅ **Full POST grade ported to the node graph.** `buildGpuPost()` now reproduces the WebGL `POST.comp`
  composite verbatim in TSL: saturation (1.18) → S-curve (0.16) → warm/cool tint → additive bloom →
  brightness/contrast grade → vignette (0.33) → dither. The grade MATH is copied from the GLSL, so it
  matches by construction; `applyGrade()` drives the `uBright`/`uContrast` uniforms (Settings sliders),
  and brightness is applied once (grade owns it; exposure stays at base, no double-apply). Verified: graph
  builds + loop-drives with zero errors; brightness/contrast uniforms respond live (brightness 0.6→1.6 =
  lum 8.8→26.7); WebGL path untouched (`applyGrade` GPUPOST branch is null-guarded).
- ✅ **CONFIRMED by Agent 1 (machine 2, real GPU, 2026-07-15): the resizePost fix holds.** Two full
  seeded overworld drives at ba9288d5: **0 × `used in submit while destroyed`, 0 × GPUValidationError**
  (vs 219–438× at the pre-fix tip on the same box/flow). Only remaining GPU-loss noise is the known
  Windows/AMD "valid external Instance reference no longer exists" environment flake at run teardown.
  The broader eviction-dispose audit (dungeon-exit bursts) is still open, but the GPUPOST trip is fixed.
- 🔧 **Candidate fix for the GPUPOST dispose-lifecycle trip (→ Agent 1, needs machine-2 A/B).** Per your
  note: the lazily-built `PostProcessing` pass RT was being disposed/rebuilt when `applyGfxTier`'s
  post-boot `setSize` fired → `used in submit while destroyed`. Moved the graph build OUT of the per-frame
  `renderComposite` and INTO `resizePost()` — which runs after every resize *settles* (applyGfxTier at
  boot + the window-resize handler) and between frames — with an explicit `GPUPOST.dispose()` before each
  rebuild. So the graph is now built once at the final size (no build→immediate-resize) and rebuilt only
  on a settled resize. Verified here (headless): GPUPOST builds at boot, survives a simulated `applyGfxTier`
  rebuild, grade uniforms intact, zero JS errors — but **this box can't reproduce the real-GPU
  `GPUValidationError`, so please A/B it on machine 2** (fresh char → seeded overworld, count `used in
  submit while destroyed`). If it still faults, it's the broader eviction-dispose audit, not the pass RT.
- ✅ **`readRenderTargetPixels` → async on WebGPU** (`snapshotToCanvas`, the appraisal/portrait snapshot).
  Sync read returned a blank portrait on WebGPU. Now dual-path: WebGL keeps sync read + bottom-up Y-flip;
  WebGPU does `await renderAsync` + `await readRenderTargetPixelsAsync(rt,x,y,w,h)` (r185: **no buffer arg,
  RETURNS the buffer**), then de-pads the 256-byte-aligned rows (stride = ceil(w*4/256)*256 = 768 for 150px)
  and does NOT flip (WebGPU readback is top-down). Verified on BOTH backends: portrait non-blank + correctly
  oriented (red-up subject → canvas top; WebGPU was blank before).
**Machine 1's headless-VERIFIABLE FX/post work is COMPLETE** (tonemap, bloom graph, full grade, dispose-race
candidate, async portrait read — all above). What's left is **inherently visual** — a procedural/particle
shader look or a screen-space effect that only means anything when you can SEE it, which this box can't
(hidden pane, no WebGPU pixel readback). Machine 1 deliberately stopped here rather than land blind ports it
can only prove "non-blank" (a wrong-looking fbm vortex is worse than the current skip). Handoff below.

- ✅ **Portal vortex → TSL** (`portalMaterial` / new `portalMaterialTSL`, ~5613). Faithful 1:1 port of the
  `_PORTAL_FRAG` GLSL (hash→value-noise→fbm swirl) to a `MeshBasicNodeMaterial.fragmentNode`. Dual-path:
  GLSL `ShaderMaterial` on WebGL, TSL on WebGPU. `_portalUTimeTSL` mirrors `PORTAL_U.uTime`, synced in the
  same update tick. Verified MEASURED BYTE-IDENTICAL to the GLSL on Metal (dual-renderer A/B — GLSL on a throwaway
  WebGLRenderer vs TSL on the WebGPU renderer, same uniforms/ortho cam: radial luminance profile identical,
  corr 1.0). No eyeball needed.
- ✅ **Aurora → TSL** (`buildAurora` / new `auroraMaterialTSL`, ~4009). Faithful port of the sin
  ripple/ray curtain. Dual-path; on WebGPU the `u` object holds TSL uniform NODES so the existing
  `for(const u of auroraU)` update tick is UNCHANGED (a UniformNode's `.value` is settable). Verified:
  WebGPU→NodeMaterial verified MEASURED BYTE-IDENTICAL to the GLSL on Metal (vertical band profile
  identical, corr 1.0); WebGL unchanged. (If Agent 1 considered aurora part of the sky/atmosphere set,
  flag it — machine 1 took it here; it's a self-contained function so low conflict risk.)
- ✅ **Lava-pool prop → TSL** (`buildProp` "lava" / new `lavaMaterialTSL`). Faithful port of the molten
  2-octave-noise shader INCLUDING the vertex heave (establishes the `positionNode` vertex-displacement
  pattern). Dual-path; a `.uniforms` shim ({uT,uCol}→TSL uniform nodes) keeps the PROPANIM tick
  (`mat.uniforms.uT.value=t`) unchanged. Verified MEASURED BYTE-IDENTICAL to the GLSL on Metal (dual-renderer
  A/B, per-pixel mean-abs-diff = 0 over 5929 px); WebGL keeps ShaderMaterial, tick works. No eyeball needed.
- ⬜ **Remaining FX/post — needs a VISIBLE browser (→ machine 2, already in the shader→TSL flow):**
  - **Portal particles** (`buildPortalFx` ~5650, `_PFX_VERT`/`_PFX_FRAG`) + the lava **embers**
    (`PointsMaterial`). **API is tractable (machine 1 confirmed):** `THREE.PointsNodeMaterial` +
    `THREE.SpriteNodeMaterial` exist, `TSL.pointUV` samples within the sprite, and `mat.sizeNode` +
    `mat.colorNode` render (a 50-point test drew). Port: `colorNode = texture(sprite).sample(pointUV) *
    attribute('aAlpha')`, `sizeNode = attribute('aSize') * 240/max(1,positionView.z.negate())` (the
    `gl_PointSize=aSize*(240/-mv.z)` formula). **Why machine 1 stopped here (not verifiable-by-construction
    like the fragment ports):** point SIZE has backend-different semantics — WebGPU's `sizeNode` unit vs
    WebGL's `gl_PointSize` pixels may not match, and a wrong size = giant blobs / invisible specks that a
    headless box can't catch. Needs a display to dial in. NOT urgent: the ported portal VORTEX already
    gives portals a complete look; particles are additive retail polish. (All fragment/vertex
    `ShaderMaterial`s ARE ported now: sky[A1], portal vortex, aurora, lava. `makePass` @~3665 = the WebGL
    POST pass, replaced by GPUPOST on WebGPU — not a target.)
    (Sky `ShaderMaterial` @2783 = Agent 1's WebGL fallback, done. `makePass` @3665 = the WebGL POST pass,
    unused on WebGPU since GPUPOST replaces it — not a target.)
  - ✅ **Anisotropy gap FIXED (machine 1, verified on Metal).** On WebGPU `renderer.capabilities` is absent,
    so the 11 `t.anisotropy=renderer.capabilities.getMaxAnisotropy()` sites were guarded no-ops → textures
    lost anisotropic filtering (grazing terrain/road/wall textures blurred to flat). Added a backend-aware
    `maxAnisotropy()` helper (WebGL: `capabilities.getMaxAnisotropy()`; WebGPU: 16, the sampler spec cap) and
    routed all 11 sites through it (dropped the `.capabilities` guard). VERIFIED on Metal: (a) WebGPU honors
    `texture.anisotropy` — a grazing tiled texture keeps hi-freq detail at 16 that it loses at 1 (variance
    0→5.1); (b) 4739 repeating textures now at aniso 16 on WebGPU (were 1), an EXACT match to WebGL's
    distribution (4739@16 / 4026@1 both backends — the 4026 are GLTF/inline textures that never set it on
    either backend, pre-existing/out of scope).
    - **MERGE RECONCILE (machine 1):** machine 2 independently fixed the same bug via a `renderer.capabilities={getMaxAnisotropy:()=>16}` monkey-patch (commit 3582083f). Both landed. Removed that shim — the `maxAnisotropy()` helper already returns 16 on WebGPU (its else-branch), no need to fake `renderer.capabilities` on the WebGPU renderer. Verified after removal: `renderer.capabilities` undefined, `maxAnisotropy()`==16, 4739 textures still @16, zero errors.
  - **SSAO** + **god-rays** as their own `pass`/nodes into the GPUPOST graph (currently WebGL-only).
  - **Tune the bloom** strength/radius/threshold (`GPUBLOOM`) to match the WebGL look.
  - Whoever picks these up: they slot into the existing `GPUPOST` graph / dual-path pattern already in
    `js/game.js`; nothing new architecturally.

> ⚠️ **Verification limitation (this dev box):** the Browser-pane preview is a HIDDEN pane
> (`document.hidden`, `innerWidth=0`, rAF throttled), and WebGPU canvas→drawImage readback is unreliable
> here — so WebGPU output can be checked programmatically (renderer state, no-throw, frame counts, region
> luminance) but NOT reliably pixel-verified. The remaining FX/post work is inherently VISUAL (bloom look,
> shader appearance), so it should be done/verified on a machine with a VISIBLE browser (or the Phase-D
> Apple-Silicon Safari pass). Don't land large shader visuals from this box on trust alone.

### Agent 1 (machine 2, world/terrain) — log
**Sky dome → TSL: DONE** (this commit). `MeshBasicNodeMaterial` + `fragmentNode`, same maths/uniforms;
`skyU` keeps its `.value` interface so `updateDayNight`/`updateWeather` are untouched. Verified headless +
headed Chrome on native WebGPU: world renders, sky gradient correct, `skyU` writes drive it live, zero
NodeBuilder errors for the sky. Agent 1 conversion notes: `docs/webgpu-agent1-tsl-notes.md`.

**Wind-sway → TSL: DONE** (810fb85c, 2026-07-15). Under `?gpu=1` `WIND.uT/uS` are shared TSL uniform
nodes (`.value` interface preserved → `updateWind` and the GLSL binding untouched); `addWindSway` gets a
TSL branch composing a height-masked two-sine sway onto `positionNode`. r185 ordering note: the instance
transform is applied to `positionLocal` BEFORE `positionNode` runs (`setupPosition`), so the height mask
reads `positionGeometry.y` and the phase reads instanced `positionLocal.xz`. Verified 3 ways on native
WebGPU: isolated r185 repro sways; injected in-game probe mesh sways (60k px pose delta); real birch
canopy visibly displaced between frozen wind poses with the trunk planted.
⚠️ **Verification lesson (cost hours): do NOT trust `player.yaw/pitch` writes to aim the camera** — the
smoothed over-shoulder rig ignores them enough that the subject drifts out of frame and pixel-diffs
read zero. Hard-set the camera INSIDE a `renderComposite` override instead (scratchpad `treetest.js`),
gate every run on an advancing `renderer.info.render.frame` (spawn-burst device-loss flake wedges ~50%
of tier-2 runs), and freeze `WIND.uT/uS` behind `defineProperty` getters for deterministic pose A/Bs.

**FOGX height/sun fog → TSL `scene.fogNode`: DONE** (0d1b39d8, 2026-07-15). One `buildGpuFog()` node
graph replaces the ShaderChunk surgery + `Material.prototype.onBuild` hack (both dead on WebGPU — fog
was silently absent there). Same closed-form maths; `positionWorld`/`cameraPosition` builtins replace
the `dFogCam` matrix (reflection pass will fog correctly for free — drop the `FOGX.cam` swap when water
lands); `scene.fog.near/far/color` tracked live via `reference()` nodes so all existing writers work
unchanged; `FOGX.sunDir/sun/h` are TSL uniform nodes under `?gpu=1` (`.value` interface preserved).
Verified: graph builds clean; aerial-perspective fog renders correctly (eyeballed screenshots); far
crush via the weather system's own `fogFar`/`targetFar` moves 219k px; height + inscatter uniforms
drive pixels; sky (fog:false) stays exempt; WebGL default path re-verified clean.
Testing note: `scene.fog.far` is rewritten EVERY frame by the weather lerp (game.js ~2925) AND AC_SKY
(~3091) — to test fog distance, drive `targetFar`/`fogFar` and null `AC_SKY`, don't write `fog.far`.

**2026-07-15 EOD (machine 2) — Phase C essentially complete; remaining items and environment note:**
- ✅ Water + planar reflection → TSL (31d88bab): MeshPhongNodeMaterial subclass, setupOutput override
  = the exact pre-fog injection point; userData.sh mimic keeps renderReflection + the water tick
  untouched; mirror renders on WebGPU. #691 true-column path stays WebGL-only (uDepthOn gated on
  POST.rtScene) — TODO: viewportSharedTexture feed.
- ✅ SSAO + god-rays in GPUPOST (9ecfcd45): Alchemy AO inline (getViewPosition handles the depth
  convention), 26-tap radial rays from the bloom field; wired per frame like the WebGL comp.
- ✅ Anisotropic filtering restored (722b7015): renderer.capabilities shim (WebGPU guarantees 16×).
- ✅ Dispose endgame: GPU resources are NEVER destroyed on WebGPU (prototype no-ops, b7de0352) —
  deferral can't fix Mode B1 (shared-unmarked resources: cached canvas textures AND merged-archetype
  geometry; destroying either freezes the canvas on spawn/teleport/exit). Bounded VRAM growth per
  session; the Phase-2 REFCOUNTED RESOURCE MANAGER is the real fix and the top Phase-D prerequisite.
- ⚠️ WIP gated OFF (window.__PFX_TSL=1 to enable): portal particles as manually-billboarded instanced
  quads (b7de0352). Native WebGPU has NO sized points: pointUV emits gl_PointCoord (invalid WGSL) and
  SpriteNodeMaterial+InstancedMesh silently draws nothing — machine 1's PointsNodeMaterial scout must
  have hit the WebGL2-fallback backend. UNVERIFIED because the box degraded (below).
- ✅ Portal/aurora/lava looks: machine 1 measured the TSL ports BYTE-IDENTICAL on Metal (3840b42c) —
  supersedes the machine-2 eyeball. Portals on WebGPU render the ANIMATED TSL VORTEX (verified on
  D3D12: glowing purple spiral, 50k px animation delta) until the particle port is proven.
- 📌 **Bloom tune is NOT the parity gap.** Measured with identical hard-camera framing through both
  full composites: WebGPU renders ~0.76× WebGL luminance (mid-terrain 62,66,66 vs 82,89,80), and the
  gap is LARGER (~0.5×) with post disabled on both — so it's the BASE LIGHTING (r185 node-material
  light integration vs the WebGL program path), not bloom/grade. scene.environment exists on both.
  This is the "on-hardware light re-tune" A.4 anticipated → Phase-D item. GPUBLOOM defaults stay.
- (Resolved: the earlier "GPU degraded" note was transient contention + a too-tight 60s transit
  timeout in the harness — the seeded world build now takes ~20s; verification runs fine again.)
- ⚠️ Non-fatal warnings to chase: 3× "Vertex attribute uv not found" on WebGPU (suspect: split canopy
  sub-geometries or a flora pool sampling map without uv), and r185 renamed PostProcessing→RenderPipeline
  (update at next bump).

**2026-07-15 (machine 2, now owning ALL of Phase C): WebGPU actually RENDERS — 3 root causes fixed**
(a6ff53ac). The seeded tier-≥1 overworld was a PERMANENTLY FROZEN canvas (every submit rejected); any
prior pixel check of a dispose-active WebGPU flow was a frozen frame with the live DOM HUD on top —
treat pre-a6ff53ac WebGPU pixel "verification" of such flows as void (harness runs that no-op'd
dispose, e.g. the sway/fog work, remain valid). Root causes + fixes:
1. **Array-material shadow casters churn the renderObject cache** (deciduous [trunk,leaf] pools →
   ~100 uniform-buffer destroys/s in the shadow pass). Fixed: pools split into trunk+canopy meshes
   sharing one instanceMatrix on WebGPU.
2. **Two shadow-casting DirectionalLights ping-pong the cache** (63 faults/s; either alone ~0).
   Fixed: single cascade on WebGPU (SHAD2.on gated) — upstream r185 bug, re-check at next bump.
3. **The mid-tunnel buildWorld burst destroys shared-but-unmarked resources (Mode B1)** → every later
   submit faults. Fixed: ALL dispose routes through a 2-tick deferral queue (prototype patch, WebGPU
   only), HELD during transits and DROPPED at transit end (bounded leak; Phase-2 refcount audit is
   the real fix). exitDungeon repro now 0 faults (was 219–438).
**Ground detail-blend splat → TSL: DONE** (same commit) — full splat/cliff/snow/anti-tiling/relief as
colorNode/roughnessNode/normalNode; verified vs WebGL through both composites (structure/hue match;
uniform brightness delta pending bloom/grade tune). **The "streak artifact" below is RESOLVED** — it
was never a rendering artifact, just the frozen canvas showing the last transit-tube frame.

**RESOLVED (see above — was: pre-existing static radial "streak" artifact on WebGPU
at gfx tier ≥2.** Bright white/blue lines radiating from a point near screen center, pixel-STATIC
across frames, localized around the player (a hard camera aimed at a tree 240m away shows none).
Present with the sway change stashed, so it's an existing tier-2 WebGPU rendering bug — suspects are
the player-area grass/wheat field or one of the ShaderMaterial-fallback objects (WebGPU renders raw
`ShaderMaterial` as a blank `NodeMaterial` fallback, it does NOT skip them: found 1× cylinder mesh with
`uT,uA,uHueOff` uniforms + ~20 `Points` with `map`). A visibility-bisect run (scratchpad
`streakhunt.js`) returned all-zero diffs but had no liveness gate — rerun it WITH the gate to identify
the object. Doesn't block Phase C work; pollutes player-POV screenshots.

**Cross-cutting fixes landed with it (both were breaking EVERY WebGPU overworld run):**
1. **Point lights were 100% dark on WebGPU.** The virtual-light-pool wrapper (game.js ~3266) subclasses
   `THREE.PointLight` anonymously, and r185's node library maps light→node by EXACT constructor → all
   805 point lights skipped with per-frame `LightsNode: Light node not found` spam. Fix: one
   `renderer.library.addLight(THREE.PointLightNode, THREE.PointLight)` after `renderer.init()`.
   (This is why Phase-B academy shots had black NPC silhouettes.)
2. **`renderer.capabilities.isWebGL2` crash** in `terSplatBuild`/`terRealUpgrade` — WebGPURenderer has
   no `.capabilities`; the TypeError aborted `initThree` midway on any run where ACMAP is live. Both
   sites now treat WebGPU as modern (RedFormat).
3. `index.html` boot: under `?gpu=1`, `THREE` is now rebased onto the `three/webgpu` namespace
   (`Object.assign`) so node materials/TSL node classes are reachable from game code. Same three.core →
   instanceof holds (verified).

**KNOWN ISSUE (open, cross-cutting, likely Phase-D scale): GPU-resource lifecycle on WebGPU.**
> **Scoped by machine 1 → [webgpu-dispose-lifecycle-scoping.md](webgpu-dispose-lifecycle-scoping.md)**: root cause (WebGL lazy-re-upload vs WebGPU destroy-now + cached bind groups), 2 failure modes (in-flight burst / still-live ref), and a phased fix (defer destroy through the `_dispGeo`/`_dispMat` choke until `onSubmittedWorkDone`; no-op `releaseObjectGPU` on WebGPU). Needs machine-2 GPU to implement+verify.
The game's dispose-then-reattach eviction (#232 `releaseObjectGPU`, dungeon-exit `disposeObject3D`
bursts, the portal tube's own dispose at arrival) assumes WebGL's lazy re-upload of disposed-but-live
resources. r185 WebGPU instead keeps cached bind groups pointing at destroyed buffers →
`GPUValidationError: [Buffer …] used in submit while destroyed` spam and (sometimes) device loss with a
frozen canvas. Repro: fresh char → `exitDungeon(true)` under `?gpu=1`. Confirmed by A/B: no-op'ing all
dispose → zero faults; a 3-frame deferred-dispose shim does NOT fix it (still-live objects re-bind the
destroyed buffer). Real fix = audit which disposals target still-referenced resources (or dirty the
material/renderObject cache on WebGPU when eviction fires). Until then avoid dungeon-exit flows when
testing WebGPU.

**Notes for Agent 2 (FX/post):**
- ⚠️ **Your GPUPOST commit (1d808e42) trips the dispose-lifecycle bug below on every seeded overworld
  run on machine 2's real-GPU box** (219–438× `used in submit while destroyed`; A/B'd at your tip vs
  before it — the pre-GPUPOST direct path was clean on the same drive). Likely the lazily-built
  `PostProcessing` pass target being disposed/rebuilt when `applyGfxTier` resizes right after boot.
  Machine 2 can pixel-verify on a visible browser — ping when you want a look at a fix.
- **Tone mapping on WebGPU is a whole-frame output blit** — `material.toneMapped` is ignored and even a
  custom `fragmentNode` gets the frame's ACES + output encode (measured byte-identical vs colorNode).
  Raw-GLSL-look parity for sky/portal/aetheria is only settleable when the POST/tonemap chain is ported;
  plan the node `PostProcessing` graph with that in mind.
- **NDC depth decode differs**: WebGPU depth is [0,1], WebGL is [-1,1] — every `*2.0-1.0` decode (water
  #691, SSAO, aoBlur) needs a backend-aware helper.
- Water's `uSceneTex/uSceneDepth` feed comes from POST.rtScene — agree on the replacement source
  (viewportSharedTexture / PostProcessing pass texture) before hard-coding either side.

**Harness (machine 2):** headless/headed Chrome driver + save-seed trick in the scratchpad (`drive.js`) —
seeds `dereth_save_v1` with `academy.done:true` and calls `startGame(true)` to spawn straight into the
overworld (avoids both the SW cache trap and the dungeon-exit dispose bug); waits on `!portalTransit`,
pins `gameTime`/weather for comparable screenshots. Beware: loading a save with `academy.done` falsy
warps back into the Academy (game.js ~30912). On this Windows/AMD box Chrome intermittently drops the
WebGPU instance ("A valid external Instance reference no longer exists", ~50% of runs, headless worse) —
environment flake, retry the run.

**2026-07-15 (machine 1, Metal) — base-lighting parity gap is D3D12-specific, NOT present on Apple Silicon.**
Dual-renderer A/B on this Metal box (throwaway WebGLRenderer vs the WebGPU renderer, SAME
MeshStandardMaterial sphere, NoToneMapping, ColorManagement off): every basic light type is BYTE-IDENTICAL
WebGL↔WebGPU — ambient 41/41, directional 42/42, hemisphere 39/39, ambient0.5+dir1 combo 62/62 (ratio 1.000
each). These dominate the game's lighting. So machine 2's measured ~0.76× luminance gap is backend-specific
to Windows/AMD/D3D12 (same pattern as the dispose bug) → the PRIMARY target (offline on Apple Silicon /
Metal) has NO base-lighting parity issue. The "on-hardware light re-tune" A.4 anticipated is therefore a
D3D12-only concern, not a target-platform blocker. (IBL/scene.environment couldn't be cleanly A/B'd — the
three/webgpu PMREMGenerator rejects a WebGLRenderer — but WebGPU IBL renders fine; basic lights being exact
makes an IBL-only gap unlikely to explain a whole-scene 0.76×.)

**2026-07-15 (machine 1, Metal) — native-WebGPU sized point sprites FIXED (stars, motes; portal-particle primitive ready).**
Root cause of the `Vertex attribute "uv" not found` warnings AND the harder-hidden regression: core WebGPU has
no gl_PointSize, so THREE.Points render at a fixed **1px** regardless of `size` (verified: size:40 → 1 lit px),
and a PointsMaterial with a `.map` compiles to WGSL that references `gl_PointCoord` → a hard
`unresolved value 'gl_PointCoord'` pipeline error. Both the starfield (2600, size 7 screen-space) and the
firefly/portal-motes (340, size 0.42 world) hit this — invisible specks + error spam on WebGPU.
FIX: new `makePointSprites(geo,opts)` helper (js/game.js ~2887). WebGL path is the original THREE.Points +
PointsMaterial, **byte-unchanged** (verified: `?`-less load still builds `Points/PointsMaterial`, 0 errors).
WebGPU path renders an `InstancedBufferGeometry` of camera-facing quads via `SpriteNodeMaterial` with
`positionNode = attribute('aCenter')` (per-instance billboard centre — SpriteNodeMaterial otherwise ignores
instanceMatrix, which is why machine 2's InstancedMesh+Sprite drew nothing), the sprite sampled at the quad
uv, per-instance `aColor`, and a `uniform()` opacity fade. buildStars/buildMotes rewired; the motes update
loop writes into the shared position array + `sprites.sync()`. VERIFIED on Metal: 0 uv warnings, 0
gl_PointCoord errors across multiple manual scene renders; offscreen measurement shows sized soft sprites
(stars 445 blobs ~12px, motes 210 blobs ~25px) with correct per-instance colour/position (isolated A/B:
red-left/blue-right). This is also the **verified primitive for the gated-off portal particles** (A.'s Phase-C
item) — same InstancedBufferGeometry+SpriteNodeMaterial billboard path. (Dungeon embers/spores at ~28.6k are
map-less THREE.Points → no warning, still 1px; low-priority ambient, can adopt the same helper later.)

**2026-07-15 (machine 1, Metal) — portal particles now ON by default on WebGPU (authentic retail replay).**
The gated-off WIP (`window.__PFX_TSL`) used a MeshBasicNodeMaterial + MANUAL billboard whose positionNode
returned a WORLD-space position — but positionNode is object-space (re-multiplied by modelViewMatrix) → the
quads landed off-screen ("silently draws nothing", 0 errors). Rewrote buildPortalFx's WebGPU branch to the
same VERIFIED SpriteNodeMaterial billboard the stars/motes use: InstancedBufferGeometry (shared quad) +
instanced iPos/aSize/aAlpha reusing the sim's Float32Arrays, `positionNode=iPos` (SpriteNodeMaterial does the
billboard + view xform), `scaleNode=aSize` (per-instance size — VERIFIED: spans scale 0.4:1.0:1.8 →
16:39:74px), `opacityNode=sprite.a × aAlpha`. updatePortalFx is untouched (still marks iPos/aSize/aAlpha).
Removed the `__PFX_TSL` gate — WebGPU portals now show the retail particle effect by default, gated only on
`window.TSL && THREE.SpriteNodeMaterial` (else fall back to the TSL vortex lens — never invisible Points).
VERIFIED on Metal via buildPortal("Holtburg") with NO flag: chose the FX path, SpriteNodeMaterial, 18378 lit
px / maxv 741 across 10 steady frames, 0 WGSL/pipeline errors. (First-appearance frame may be blank while the
WebGPU pipeline compiles async — normal, imperceptible at 60fps.) WebGL portal path byte-unchanged.

## Phase C — remaining items after machine-1's 2026-07-15 session  →  what's DONE vs LEFT

Machine 1 (Metal) closed the visible WebGPU rendering-parity gaps this session. Authoritative status:

- ✅ **Base-lighting / luminance parity — SOLVED by Agent 2 (2265843f), supersedes machine-1's "D3D12-specific".**
  My per-light A/B showed byte-identical lights on Metal ONLY because the probe set the output transform
  manually (NoToneMapping + explicit outputColorSpace on both renderers), which masked the real gap: with
  #695 ColorManagement OFF, r185's WebGPU output transform gates its sRGB ENCODE on `ColorManagement.enabled`
  while WebGL encodes regardless → the WebGPU frame was written LINEAR (darker) on ALL backends, not just
  D3D12. Agent 2's GPUPOST now tonemaps+encodes the scene pass manually (ACES → sRGB OETF) and grades in
  display space like the WebGL POST.comp. My sprites feed the scene pass like any other geometry, so this
  applies to them uniformly — re-verified on Metal against the merged tree: stars/motes/embers render, 0
  errors (their `toneMapped:false` is now moot since GPUPOST tonemaps the whole pass).
- ✅ **`Vertex attribute "uv" not found` warnings** — root cause was NOT canopy/flora (machine 2's suspicion);
  it was the starfield + firefly/portal-mote PointsMaterials carrying a `.map`. Fixed with the point-sprite
  work below. 0 warnings now.
- ✅ **Sized point sprites on native WebGPU** (stars, motes) — `makePointSprites()` helper; the general fix for
  "core WebGPU renders THREE.Points at 1px". WebGL path byte-unchanged.
- ✅ **Portal particles** — the gated WIP was rewritten to the verified SpriteNodeMaterial billboard and turned
  **ON by default** on WebGPU (authentic retail replay; vortex-lens only as a no-node-material fallback).

**LEFT (with owner + why):**
- ⬜ **Refcounted resource manager** (Phase-2 dispose) — *the top Phase-D prerequisite.* → **machine 2.** Its
  real failure mode (Mode B1: destroying a shared-but-unmarked resource FREEZES the canvas) only reproduces on
  D3D12, and machine 2 has both the repro and the A/B harness. Implementing it blind on Metal (where dispose
  faults don't even manifest) risks a machine-2-only regression. Current state: dispose is no-op'd on WebGPU
  → correct + stable but bounded VRAM growth per session. Scope in [webgpu-dispose-lifecycle-scoping.md] §Phase 3.
- ✅ **#691 water true-column / refraction on WebGPU — ON by default.** In `buildWaterMaterialTSL().waterOut`:
  true view-ray column `positionView.z.sub(perspectiveDepthToViewZ(viewportDepthTexture, cameraNear,
  cameraFar)).max(0)` (probe: seabed 12.00 m exact), refraction from `viewportSharedTexture(screenUV +
  N.xy·off)` (probe: reddish seabed transmits through shallow water), `_tv`-based shore fade on alpha; same
  constants as GLSL js/game.js ~3739-3781. Every depth block is gated at RUNTIME by the `U.uDepthOn` uniform
  (now a `t.uniform`), which the water tick sets to `cam.position.y > waterMesh.position.y+0.06 ? 1 : 0` —
  underwater it collapses to the byte-identical baked look (matches WebGL's cam-above-water gate). Escape
  hatch: `window.__WATER_NODEPTH=1`. VERIFIED on Metal: uDepthOn 0↔1 toggles baked↔refraction cleanly; the
  FULL in-game pipeline (loop→update→GPUPOST) drives 100+ frames with `uDepthOn=1` above water, **0 WGSL/
  pipeline errors**. ⚠️ One caveat: a real-shoreline aesthetic eyeball wasn't reachable (the headless pane
  spawns into the starter interior, no open water in view) — the maths is a faithful port of the shipping
  WebGL look and colours now match (Agent 2's #695 fix), so parity is expected, but a human glance at a
  coastline on either backend is the final confirmation.
- ✅ **Dungeon/torch embers & spores** (4 map-less `THREE.Points`: mkFire, mkMotes, brazier, mushroom) —
  converted to `makePointSprites()` (extended for single-color + a synthesized soft round sprite when no
  `map`). WebGPU now renders them as sized soft glows instead of 1px specks; WebGL byte-unchanged
  (`Points`/`PointsMaterial`, colour preserved, 0 errors). **No 1px point FX remain on WebGPU.**
- ✅ **`PostProcessing` → `RenderPipeline` rename** — done now (not deferred). `PostProcessing` is
  `@deprecated since r183` in the vendored r185 (`class PostProcessing extends RenderPipeline`) and fires a
  `warnOnce` on construction; `RenderPipeline` is exported and a straight drop-in (PostProcessing adds only
  the warning). Bootstrap now exposes `window.RenderPipeline = webgpu.RenderPipeline || webgpu.PostProcessing`
  (fallback kept); `buildGpuPost()` builds `new window.RenderPipeline(renderer)`. Verified on Metal: GPUPOST
  is a `RenderPipeline`, renders through the loop, **0 deprecation warnings**, `window.PostProcessing` global
  gone.

## Phase D — fallback QA / perf / cutover  →  IN PROGRESS

**Perf comparison (machine 1, Metal, 2026-07-15).** Method: same seeded tier-≥1 spawn (0,-9), a FIXED camera
pose in both loads, render the live scene to a 1280×720 target with **forced GPU completion each frame**
(`device.queue.onSubmittedWorkDone()` on WebGPU / `gl.finish()` on WebGL), warmup discarded. Result:

| metric | WebGPU (native/Metal) | WebGL2 (Metal) |
|---|---|---|
| full-frame mean | **81.9 ms** | 100.3 ms |
| full-frame median | **81.5 ms** | 90.0 ms |
| min | 80.6 ms | 88.5 ms |
| draw calls | 283 | 1,178 |
| triangles drawn | 10.87 M | 72 K |

**Takeaways:** (1) WebGPU is **not a regression — it's modestly faster** (~10% median, ~18% mean) *while
drawing ~150× more geometry in ¼ the draw calls.* The two paths differ by design: machine 2's WebGPU
batching (single shadow cascade + pooled instanced trunk/canopy meshes, `frustumCulled=false`) submits the
whole forest in a few instanced calls and lets the GPU eat it, whereas the WebGL path spends its frame on
CPU-side per-object frustum culling (confirmed CPU-bound: WebGL `render()` WITHOUT `gl.finish()` = 108 ms ≈
WITH = 109 ms → the GPU is idle, the cost is scene-graph traversal/cull + submission of ~200k objects).
(2) So WebGPU shifts work from CPU culling to GPU throughput — the right direction as scenes get draw-call-
heavy. **Caveat:** absolute numbers (~80–100 ms ⇒ 10–12 "fps") are massively inflated by the forced
per-frame GPU sync (no pipelining) + the throttled/backgrounded headless Browser pane; real pipelined
framerate in a foreground tab is far higher for both. The RATIO and DIRECTION are the reliable signal; a
definitive fps verdict needs a real foreground browser with GPU timer queries (`EXT_disjoint_timer_query` /
WebGPU timestamp-query) and natural rAF pacing. **Bottom line for cutover: no perf blocker — WebGPU ≥ WebGL
here, and its advantage widens with draw-call count.**

**Still LEFT in D:** WebGL2-fallback-backend QA (force `forceWebGL`/no-native-WebGPU and confirm the game
runs), then the cutover (flip default, retire `?gpu=1`, drop the classic dual-path, roll `webgpu`→`main`).

---

### For the other machine, to sync now
```
git fetch origin && git checkout webgpu && git pull --rebase origin webgpu
# then hard-reload the preview past the service worker: unregister SW + clear caches + ?nocache=N
```

## Phase D — integration/parity/cutover  →  IN PROGRESS

**Agent 2 / machine 2, 2026-07-15 (2265843f): COLOR-PIPELINE PARITY SOLVED.** The 0.5–0.76× luminance
gap was never lighting: the game runs with ColorManagement DISABLED (#695 legacy colors), and r185's
WebGPU output transform gates its sRGB encode on `ColorManagement.enabled` while WebGL encodes
regardless — the WebGPU frame was written LINEAR to the canvas (verified: sRGB_encode(WebGPU) ==
WebGL exactly). GPUPOST now tonemaps+encodes manually and grades in DISPLAY space like POST.comp;
bloom uses the WebGL curve (threshold .90/knee .2) with an eyeball-calibrated radius .05; the four
raw-authored display-referred materials (sky/portal vortex/aurora/lava) pre-compensate via
_rawFragTSL. Terrain + sky now match WebGL by eyeball with identical hard-camera framing.
Repro/diagnosis recipe: iso-light.html pattern — CM enabled = backends byte-identical; CM disabled =
the exact in-game divergence.
⚠️ For machine 1: re-run your byte-identical measurements for portal/aurora/lava — the _rawFragTSL
compensation intentionally changes their output (they were only "identical" against the old
non-encoding pipeline). The water #691 column feed (5a3942f4) may also want a look through the new
display-space GPUPOST chain.

Remaining Phase D: feature-detect auto-fallback + default flip, browser matrix (Safari on machine 1;
Chrome/Edge/Firefox here), perf pass, refcounted resource manager (or D3D12-gating of never-destroy),
far-cascade restore via three bump, __PFX_TSL billboards, uv warnings, PostProcessing rename, then
merge webgpu → main.

**Agent 2 / machine 2 (943b8de1): RESOURCE LIFECYCLE DONE — liveness-swept destruction.** The Phase-2
manager landed without hand-refcounting: dispose() enqueues; the drain (every ~30 frames, batches ≥1
batch old) traverses the scene once and destroys only geometries/materials NOTHING attached still
references — shared survivors stay resident (structural Mode-B1 fix). Textures stay permanently
resident (cached/shared class, bounded by content). The transit-window leak is removed. Verified on
D3D12: exitDungeon reclaims killed=2850/kept=327 with ZERO faults; seeded + long-teleport flows live;
WebGL untouched. Also confirmed: machine 1's SpriteNodeMaterial portal particles RENDER ON D3D12.
Remaining Phase D: auto-fallback + default flip, browser matrix, perf pass, three bump evaluation
(far cascade), machine-1 re-measure of portal/aurora/lava post-_rawFragTSL, water #691 flag flip,
merge → main.

**Agent 2 / machine 2 — D3D12 verification sweep (Phase D item: cross-machine verifies):**
- ✅ Water #691 true-column default-on (c4eeac6d) verified on D3D12 through the display-space GPUPOST
  chain: uDepthOn=1 live, water animates, mirror runs, ZERO faults. Observation (not a blocker): at a
  grazing shoreline angle WebGPU reads slightly more turquoise / less mirrored than WebGL — likely the
  shallow-tint vs fresnel balance under the true column; revisit only if it bothers the eyeball pass.
- ✅ SpriteNodeMaterial portal particles CONFIRMED on D3D12 (138k px animation at a live portal).
- ✅ Aurora post-_rawFragTSL: night-sky comparison matches WebGL (same curtain shape/colors); stars
  render slightly crisper on WebGPU (makePointSprites sizing — cosmetic).
- ✅ Sky + portal vortex post-compensation verified during the parity work.
- ⬜ Lava post-compensation: no lava prop spawns within this seed's streaming range on either backend —
  falls to machine 1's Metal re-measure (which is still the outstanding action from 2265843f).
