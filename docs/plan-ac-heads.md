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
- **Phase 1 stage 2 — TODO: palette colour compositing.** The default textures carry
  their own baked skin/hair tone, so heads render correctly now, but choosing skin/
  hair/eye COLOUR variants needs subpalette compositing (see Risks). The palette DIDs
  are already carried in `index.json`. Approach: PaletteSet 0x0F expands to a skin-tone
  palette list; re-decode the skin-bearing face texture (`050030F0`-family) with the
  chosen tone over the base range; hair textures × hairColors, eye strips × eyeColors.
  Emit `<tex>_<pal>.png` variants, or fall back to per-group material-colour multiply.
- **Phase 2 — TODO: engine wiring / attachment** (see below).

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
