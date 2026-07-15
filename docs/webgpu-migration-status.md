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
  same update tick. Verified: WebGPU→NodeMaterial renders the animated purple vortex non-blank; WebGL
  unchanged. ⚠️ exact look eyeball-pending (machine 2).
- ✅ **Aurora → TSL** (`buildAurora` / new `auroraMaterialTSL`, ~4009). Faithful port of the sin
  ripple/ray curtain. Dual-path; on WebGPU the `u` object holds TSL uniform NODES so the existing
  `for(const u of auroraU)` update tick is UNCHANGED (a UniformNode's `.value` is settable). Verified:
  WebGPU→NodeMaterial renders the teal/violet curtain + animates (uT advances via the untouched tick);
  WebGL unchanged. ⚠️ look eyeball-pending. (If Agent 1 considered aurora part of the sky/atmosphere set,
  flag it — machine 1 took it here; it's a self-contained function so low conflict risk.)
- ✅ **Lava-pool prop → TSL** (`buildProp` "lava" / new `lavaMaterialTSL`). Faithful port of the molten
  2-octave-noise shader INCLUDING the vertex heave (establishes the `positionNode` vertex-displacement
  pattern). Dual-path; a `.uniforms` shim ({uT,uCol}→TSL uniform nodes) keeps the PROPANIM tick
  (`mat.uniforms.uT.value=t`) unchanged. Verified both backends: WebGPU→NodeMaterial renders molten orange
  + animates (shim wires the tick); WebGL keeps ShaderMaterial, tick works. ⚠️ look eyeball-pending.
- ⬜ **Remaining FX/post — needs a VISIBLE browser (→ machine 2, already in the shader→TSL flow):**
  - **Portal particles** (`buildPortalFx` ~5650) + the lava **embers** (`PointsMaterial`) — point-sprite
    rendering on WebGPU is structurally different (`gl_PointSize`/`gl_PointCoord` don't translate; needs
    `PointsNodeMaterial`/`SpriteNodeMaterial` + a size node). Deferred — the point path wants a display to
    confirm size/placement. (All the fragment/vertex `ShaderMaterial`s are now ported: sky[A1], portal
    vortex, aurora, lava. `makePass` @~3665 is the WebGL POST pass, replaced by GPUPOST on WebGPU.)
    (Sky `ShaderMaterial` @2783 = Agent 1's WebGL fallback, done. `makePass` @3665 = the WebGL POST pass,
    unused on WebGPU since GPUPOST replaces it — not a target.)
  - **Anisotropy gap (cross-cutting, both agents):** on WebGPU `renderer.capabilities` is absent, so every
    `t.anisotropy=renderer.capabilities.getMaxAnisotropy()` (guarded → safe no-op) is skipped → all textures
    lack anisotropic filtering (blurrier at grazing angles; not a crash). WebGPU supports it via a different
    max query — set `texture.anisotropy` from the WebGPU backend limit once, apply everywhere. Minor polish,
    needs a real GPU to confirm.
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

## Phase D — fallback QA / perf / cutover  →  NOT STARTED

---

### For the other machine, to sync now
```
git fetch origin && git checkout webgpu && git pull --rebase origin webgpu
# then hard-reload the preview past the service worker: unregister SW + clear caches + ?nocache=N
```
