# Agent 1 (world/terrain) — Phase-C TSL conversion notes

Prep survey done while waiting for the Phase-A checkpoint (2026-07-14). Line numbers are
pre-file-split `index.html` (r159). Companion to `docs/webgpu-migration-plan.md` §3/§4.
This machine owns: sky dome, height/sun fog injector, ground detail-blend, water + reflection RT,
wind-sway. (Aurora ShaderMaterial at ~4824 is Agent 2's, per the ownership table.)

## Worklist, easiest → hardest

### 1. Sky dome (~3676) — EASY
`ShaderMaterial` BackSide, `fog:false`, `depthWrite:false`. Uniforms `uZen/uHor/uGnd/uSunDir/uSunCol/uGlow`,
written per-frame through the `skyU` handle by `updateDayNight` and `updateWeather` (`skyU.uGlow`).
- TSL: `MeshBasicNodeMaterial` + `colorNode`. View dir = `positionWorld.sub(cameraPosition).normalize()`
  (exactly what the GLSL `vDir` computes; both are TSL builtins).
- Gradient: `mix(uHor,uZen, pow(clamp(d.y,0,1), 0.62))`, below-horizon branch via `select()`/`If`,
  sun disc = three `pow(dot)` terms — all directly expressible in TSL.
- Keep the `skyU` handle working unchanged: TSL `uniform(new Color(...))` nodes expose `.value`,
  so `skyU={uZen:uniform(...),...}` preserves the `skyU.uZen.value.setHex(...)` call sites.

### 2. Wind-sway `addWindSway` (~4775) — SMALL but has THE instancing gotcha
Wraps/chains `onBeforeCompile`, injects after `begin_vertex`: smoothstep height mask (yLo..yHi),
two-sine sway on `transformed.xz`, per-instance phase from `instanceMatrix[3].xz`.
Callers (5): grass 7385, plant billboards 7564 (×2), leafy canopies 7729, misc pools 7736.
Shared uniforms `WIND.uT/uS` — becomes two shared TSL `uniform()` nodes bound into every sway material.
- TSL: `material.positionNode = positionLocal.add(offset)` where offset uses the height mask.
- GOTCHA: `positionNode` is LOCAL space, before the instance matrix is applied. The GLSL phase read
  `instanceMatrix[3].xz` — verify how `three/tsl` exposes the instance matrix in current r17x
  (`instancedMesh` node / `instanceMatrix` TSL accessor / worst case an `instancedBufferAttribute`
  of the translation). Test on an InstancedMesh forest FIRST; a wrong space makes whole forests shear.
- The chain-with-previous-onBeforeCompile trick disappears: materials that need sway + something else
  compose nodes instead (sway is just an `.add()` into positionNode).

### 3. Height/sun fog "base injector" FOGX (~3882) — REWRITE, gets SIMPLER
Currently: global `THREE.ShaderChunk.fog_*` rewrites + `Material.prototype.onBuild` binding shared
uniforms `dFogCam/dFogSunDir/dFogSun/dFogH` into every fog-enabled material. Both hacks are dead on
WebGPU (no GLSL chunks, no onBuild contract) — it will silently vanish, so it MUST be ported.
- TSL: one custom scene fog node (`scene.fogNode` in `three/webgpu`) implementing the closed-form
  exponential-height ratio + far-guard + sun-azimuth inscatter. ONE definition instead of chunk surgery.
- SIMPLIFICATION: `dFogCam` existed only to rebuild world-space direction inside the fog chunk
  (mediump-safe). TSL has `positionWorld`/`cameraPosition` per render pass — the manual matrix uniform
  and the mirror-pass `FOGX.cam.value.copy(rc.matrixWorld)` swap (4712, 4721) should both go away.
  Verify the reflection pass gets correct fog for free.
- Keep the uniform semantics (`h` = falloff/min/strength/far-guard, `sun.w` = tint strength) so
  `updateDayNight`'s writes port 1:1.

### 4. Ground detail-blend (~4255) — HARD
`MeshStandardMaterial.onBeforeCompile`, three injection points:
- `<map_fragment>`: 4-corner bilinear splat over the AC cell grid (`terT` id decode ×36.4286,
  6×6 atlas `terS` with inset, per-cell dihedral UV rotation `terV`), mirrored triangle-wave tiling,
  cliff-slope bare-rock + high-peak snow overrides, far-scale anti-tiling multiply, non-splat
  fallback path (uTerOn=0: near/far detail blend of `map`).
- `<roughnessmap_fragment>`: roughness scaled by splat luminance.
- `<normal_fragment_maps>`: Mikkelsen screen-space surface-gradient relief from splat luminance
  (dFdx/dFdy), distance-faded.
TSL mapping: `MeshStandardNodeMaterial` with `colorNode` (splat replaces map — remember to still
multiply `vertexColors`; material has `vertexColors:true`), `roughnessNode`, `normalNode`.
- `dFdx`/`dFdy` exist in TSL; view-space normal→world trick becomes `transformedNormalWorld`.
- Uniforms are the shared `TER` singleton (3296) — becomes shared TSL uniform/texture nodes;
  `TER.uOn` is flipped at runtime (3945) so keep it a uniform, not a compile-time branch.
- The shared state matters: ONE groundMat instance is used by all 256 terrain chunks.
- Suggest converting the uTerOn=0 fallback path first (small), then the splat path.

### 5. Water (~4416) + reflection RT (~4678) — HARDEST, most cross-system
`MeshPhongMaterial.onBeforeCompile`. Vertex: two crossed travelling swells on `transformed.z`
(+`vWXZ`, projective `vReflC=uTexMat*modelMatrix*pos`). Fragment (injected before `<fog_fragment>`):
baked-heightfield depth, true per-pixel water column from LAST frame's resolved scene depth
(`uSceneTex/uSceneDepth/uAB/uRes`, gl_FragCoord→UV), turquoise shallows, fresnel deep↔sky,
planar-reflection sample with ripple distortion, Beer-Lambert refraction, sun glitter,
multi-octave shoreline foam/surf, alpha shore fade. 16 uniforms; `userData.sh` handle is what
`renderReflection` + the per-frame updater write into — those call sites must be re-pointed at the
TSL uniform nodes.
- TSL: `MeshPhongNodeMaterial`; swells → `positionNode`; the big fragment block → `colorNode`/
  `outputNode` (it rewrites gl_FragColor.rgb AND .a — use outputNode or colorNode+opacityNode
  carefully; the injection point is before fog, and with the custom scene fogNode (#3) ordering
  should match: output → fog applies after).
- `gl_FragCoord.xy/uRes` → TSL `screenUV`/viewport nodes (drops the uRes uniform).
- Depth compare: `uAB` perspective-depth decode can port verbatim, or use TSL depth utilities.
  NOTE WebGPU depth range is [0,1] not [-1,1] — the `*2-1` NDC decodes (water 4457, and the SSAO/
  aoBlur equivalents on Agent 2's side) need a backend-aware depth helper. Flag this to Agent 2.
- Reflection RT: `WebGLRenderTarget(512,288)` → `RenderTarget`; the oblique-near-plane projection
  math (4697-4709) is pure matrix surgery on `projectionMatrix.elements` — backend-agnostic, keep.
  `renderReflection`'s render-to-target flow maps to WebGPURenderer directly; drop the FOGX.cam swap
  once #3 lands.
- The "last frame's scene colour/depth" feed comes from Agent 2's post pipeline (POST.rtScene) —
  COORDINATION POINT: agree on the replacement source (WebGPU PostProcessing pass texture or
  viewportSharedTexture/viewportDepthTexture) before either side hard-codes one.

## Cross-cutting
- Shared-uniform singletons (`WIND`, `TER`, `FOGX`, `skyU`) all become shared TSL `uniform()` nodes —
  same object shared across materials keeps the one-write-updates-all behaviour.
- All four sites keep their `.value`-write call sites if we wrap TSL uniforms in the same-named
  handles — aim for zero changes outside the shader definitions.
- Verify each conversion on BOTH backends (WebGPU + forceWebGL) per plan §3C.
- Order of attack: sky → wind-sway → fog node → ground → water (each lands as its own commit on
  `webgpu`, game runnable throughout).
