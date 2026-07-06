# Plan: Authentic AC Character Heads

Goal: replace the procedural sculpted/painted heads with **Turbine's real head models**,
built exactly the way the AC client built them, and **attach them to the AC bodies**
(today the game deliberately skips the head part — `acJointFor` returns null for
pivots above y 1.5 — and keeps the stylized painted head floating on the neck joint,
which is why heads don't match or sit right on the retail bodies).

## How retail actually built a head (verified against acdata/client_portal.dat)

Everything lives in **CharGen (0x0E000002)**, per heritage × gender (`SexCG`):

- **Base body**: `BaseObjDesc` lists an AnimPartChange for every setup part —
  part **16 is the head**; Aluvian male default head GfxObj = `0x0100005A`,
  base palette `0x0400007E`, skin PalSet `0x0F000013`.
- **Hair styles** (51 for Aluvian male; each heritage/gender has its own list):
  each style is an ObjDesc that
  1. **swaps part 16's GfxObj** (`AnimPartChange (16, 0x10047F6…)`) — the hair is
     part of the head mesh, a different model per style, and
  2. **swaps the hair texture** on part 16 (`TextureMapChange 0x5000098 → style tex`).
  A `bald` flag selects which eye-strip variant applies.
- **Eye strips** (19): TextureMapChange on part 16, `0x500024C → strip texture`
  (plus a separate bald-variant texture). **Nose strips** (20): `0x50002F5 → tex`.
  **Mouth strips**: same pattern. So facial features are **texture swaps keyed by the
  OLD SurfaceTexture id**, not geometry.
- **Colors**: skin = one palette chosen from the PalSet (0x0F, ~10 tones) applied
  over the base palette's skin range; hairColors (4–8 palette DIDs) over the hair
  range; eyeColors (8) over the iris range. (ACE applies these as SubPalettes with
  fixed offset/length ranges — skin 0x00/0x18, hair 0x18/0x08, eyes 0x20/0x08, in
  8-color units; validate empirically at export time.)

All parsers needed already exist and are proven: `tools/ac_chargen_export.py` walks
CharGen **byte-exact** (it currently *discards* the ObjDesc payloads — the upgrade is
to return them), and `tools/ac_model_export.py` has GfxObj/Surface/Texture/Palette.

## STATUS

- **Phase 1 core — DONE (`8c2a4fd`).** `ac_chargen_export.py` upgraded (byte-exact
  CLEAN) to return every ObjDesc payload; `ac_head_export.py` bakes 170 head meshes
  (each group tagged with its original SurfaceTexture id) + 511 decoded facial
  textures + `index.json` (per heritage/gender: default head, hair styles, eye/nose/
  mouth strips, skin/hair/eye palette DIDs). 14MB. Verified: slot ids
  (hair 05000098, eyes 0500024C, nose 050002F5, mouth 0500025C) match mesh otex
  groups; textures decode to genuine AC facial art; indexed PNGs lossless (sources
  ≤256 colours).
- **Phase 1 stage 2 — DONE (runtime RGB-remap).** Every head texture indexes the
  master palette `0400007E`; each skin/hair/eye "colour" is a full palette that differs
  from base only in its feature's entries. So a colour = a compact **old-RGB → new-RGB
  remap**. `ac_head_export.py` emits `assets/acheads/palettes.json` (189 palette remaps,
  1.7MB) and records per heritage/gender the `skinTones`, `hairPals`, `eyeColors` choices
  in `index.json`. **Verified pixel-exact**: applying a remap to a default texture
  reproduces a direct re-decode with the colour palette to 0/65536 pixels. Whole head
  pack is 14MB. Pre-baking every (texture,palette) variant was tried and rejected
  (223MB) — runtime remap is the right architecture and matches how the client composited.
  - **Phase 2 uses this**: `acBuildHead` loads each group's default texture and, for the
    group's feature (skin = non-slot/face groups + nose/mouth; hair = otex 05000098;
    eyes = 0500024C), applies the chosen palette's remap via a canvas pass before use.
- **Phase 2 core — DONE (`0758127` attach, `b2ecae7` per-character variety).**
  `acBuildHead(av)` builds the chosen head from the acheads pack and attaches it to the
  neck joint with the same placement math as the AC body parts (part 16, p=[0,1.587,
  -0.013] q=[0,-1,0,0]) — no more floating head; it faces forward and rides the body.
  Per group it picks the texture by facial slot (hair style mesh + hair tex, eye/nose/
  mouth strips, else face/skin) and recolours it via `acHeadTexture` (cached canvas RGB
  remap from palettes.json). Procedural painted head hidden; `refreshAvatarAppearance`
  rebuilds on any change (barber/creator), token-guarded against stale async loads.
  `acHeadChoices(app)` resolves choices: explicit `app.acHead` wins, else DERIVED from
  the existing creator appearance (skin/hair/eye colours map proportionally + seeded
  style/eye/nose/mouth) so every character gets a stable varied head and it saves/loads
  via player.appearance. Verified: distinct appearances → distinct heads, clean AC face
  seated on the body, 0 console errors.
- **Phase 2c — DONE (`352e39c`): WYSIWYG creator.** The character-creator bust
  (`ccBust_build`) now swaps in `acHeadGroup(app)` — the real AC head — and rebuilds on
  every option change, so creation shows the actual head. `acHeadTexture` takes an
  ORDERED remap list (skin first, then feature colour) because the AC skin/hair/eye
  palettes overlap in index range and a merged RGB remap is ambiguous; sequential
  application recolours a strip's skin ground then its iris/hair.
- **✅ FIXED (2026-07-06) — female forehead band.** Was: the eye group FILLS the eye-region hole in
  the face mesh, and the eye-strip texture's top ~40% is a dark-brown under-hair forehead band (jagged
  hairline edge) meant to sit beneath the hair; female styles don't cover it, so it showed across the
  brow. **Fix = option (c):** `acHeadTexture` now takes a `foreheadFix` flag (passed for the eyes slot in
  `acHeadGroup`); after the palette remap it samples the strip's own forehead-skin tone from the temples
  (edge columns at brow height) and repaints the *dark* pixels in the top ~40% with it. Males are
  unaffected (hair still covers that region); the lower 60% (eyes/brows) is untouched. Verified on the
  real eye strips: the top-band dark fraction drops from **0.70–0.98 → 0.00** and its average luma rises
  to match the sampled skin, with the lower region byte-identical. (`acHeadTexture`/`acHeadGroup`.)
- **Phase 2 remaining — POLISH (optional):** (a) the character-creator PREVIEW (ccBust)
  still shows the procedural painted head, so the creator isn't WYSIWYG — build the AC
  head in the preview scene too. (b) Explicit creator/barber rows to pick from the full
  AC lists (50 hair styles, eye/nose/mouth strips, exact skin/hair/eye colours) writing
  `app.acHead`, instead of only deriving from the legacy rows. (c) Optional: give the
  ~40 nearest NPCs seeded AC heads (they use procedural `buildPerson`, so this needs the
  AC body+head on NPCs — larger). (d) Under Dereth's bright top-down lighting light skin
  washes pale (the known r128 linear-pipeline issue that drove the old painted-face
  shading) — could add a subtle baked ambient-occlusion / tone on the face material.

### Superseded design notes below (kept for reference)

## Phase 1 — Extraction (`tools/ac_head_export.py` + chargen upgrade)  ✅ core done

1. Upgrade `ac_chargen_export.py`: `objdesc()`/`hairstyle()`/`eyestrip()`/`facestrip()`
   RETURN their data (anim part swaps, texture old→new pairs, subpalette ranges,
   bald flags, icons). Emit into `assets/acchargen.json` under each gender:
   `hairStyles:[{gfx, tex:{old:new}, bald}]`, `eyes:[{tex, texBald}]`, `noses:[...]`,
   `mouths:[...]`, plus `basePalette`, `skinPalSet`, `defaultHead`.
2. New `tools/ac_head_export.py`:
   - Collect the union of all head GfxObjs across heritages/genders (they share
     setups, so dedupe by DID — expect ~a few hundred unique head+hair meshes).
   - Export each as mesh JSON in the avatar format (`{groups:[{mat,v,n,uv,i}]}`),
     **recording each group's original SurfaceTexture id** (`otex`) so the engine can
     swap hair/eyes/nose/mouth textures by old-id exactly as ObjDesc does.
   - Export textures: default-palette decodes of every referenced texture + every
     strip texture (they're small, 128–256px) into `assets/acheads/tex/`.
   - **Color variants** (exact palettes, stage 2 of this phase): for each skin tone
     in each PalSet, re-decode the skin-bearing textures with the composed palette
     (base palette overlaid by the subpalette ranges); same for hair textures × hair
     colors and eye strips × eye colors. Named `<tex>_<pal>.png`. If size balloons,
     fall back to material-color multiply per group (state clearly in the commit).
   - `assets/acheads/index.json`: per heritage/gender → default head, style list
     (mesh + texture ids), strip lists, palette variant names.

## Phase 2 — Engine: build + ATTACH the head

1. `acBuildHead(app)` (near `applyACBody`): fetch the chosen style's head mesh,
   apply texture swaps for the chosen eyes/nose/mouth (+ bald variants), pick
   palette-variant textures (or tint fallback), return a group.
2. **Attachment** (the fix for "heads are not attached"): part 16's placement frame
   is `p=[0, 1.587, -0.013]` in setup space — attach exactly like body parts:
   `pos = p*acScale − acRestPos(neck)`, placement quaternion, scale `acScale`, as a
   child of `u.neck`, tagged `userData.acBody` so armor/visibility passes treat it
   like the rest of the body. Hide the procedural head group (`u.head`) when the AC
   head is active; keep it as the fallback when the pack is missing.
3. **Animation**: add part 16 to `u.acDrive` mapped to the neck joint (remove the
   `ay>1.5 → null` skip **only** when an AC head is present) so the MotionTable
   clips drive real head motion; the procedural glance/blink (`tickFace`) is skipped
   for AC heads — retail heads were rigid, this IS the authentic behavior.
4. **Helms**: `refreshACArmor` already swap-hides body parts by joint; verify head
   armor pieces (part-16 swaps in acclothing) replace the head mesh + that hair
   hides under a helm (retail used the bald strip variants under headgear — use
   `bald` textures when a helm is worn).
5. **Creator**: replace the face/hair rows with the retail option lists — hair style
   (real count per heritage/gender), hair color, eyes/nose/mouth strips, eye color,
   skin tone (PalSet) — previewed on the real head in the existing `ccBust` preview.
   Map old saves: keep `player.appearance` keys, add `acHead:{style,eyes,nose,mouth,
   skinTone,hairColor,eyeColor}`; absent → seeded defaults from the old choices.
   The Barber picks all of this up for free (it reopens the creator).
6. **NPCs** (follow-up, optional first pass): townsfolk keep painted heads for perf;
   or give the ~40 nearest NPCs seeded AC heads via the same builder.

## Phase 3 — Verification

- Export sanity: every indexed mesh file exists; strip old-ids match the head
  meshes' `otex` sets; byte-exact CharGen consumption still CLEAN.
- Preview: creator busts per heritage × gender × several styles (screenshot);
  eyes/nose/mouth swap visibly; skin/hair/eye color variants; in-world third-person
  with `acMotionPose('run')` — head rides the neck through the clip with no gap at
  the collar; helm equip hides hair (bald strips); 0 console errors.
- Old-save load migrates appearance without breaking `refreshAvatarAppearance`.

## Risks / notes

- **Palette-range compositing** is the only genuinely new format work (PalSet 0x0F is
  trivial: count + DIDs). Validate the subpalette offsets empirically by decoding a
  known face and eyeballing skin/hair/iris boundaries; ACE's CharacterFactory is the
  reference if ranges look wrong.
- The painted-face identity system (freckles/scars/age/beauty, blinking) is
  non-retail; it stays for NPCs and as fallback, but the player's AC head drops it —
  that's the point ("match the heads in AC").
- Female setups (`0x02000002`-family) have their own style lists — export both, no
  shared assumptions.
- Keep per-file sizes small: head meshes are ~400–800 tris; textures 128–256px;
  the whole pack should land well under 20MB even with color variants.
