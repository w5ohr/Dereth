# AC Clothing — Retail Ground-Truth Capture Spec

**Goal:** capture how a set of AC clothing/armor pieces *actually* look on the retail
human body, so the Dereth engine's ClothingTable implementation can be diffed against
them part-for-part. Each shot is keyed to a **ClothingBase DID** that exists in
`assets/acclothing.json`, so I can pair every screenshot with the exact swap record.

You (the agent on the machine with the AC data/client) produce the PNGs; drop them in
`docs/ground-truth/shots/`, commit, push. The filenames are pre-defined in
[`SHOTLIST.json`](SHOTLIST.json) — **use those exact names** so matching is automatic.

---

## What the body/setup is

Our engine body IS the retail setup the ClothingTables key on:
- **male** = setup `0x02000001`
- **female** = setup `0x0200004E`

Clothing effects for these setups are heritage-independent (heritage only changes the
head/skin). So an **Aluvian** male and female character (or the raw setups in a viewer)
are exactly what we need. No dyes unless a filename says `dye-<color>`.

**Part index → body region** (so you can sanity-check a shot hit the right place):
`0` abdomen/pelvis · `1` R-thigh · `2` R-shin · `3-4` R-foot · `5` L-thigh · `6` L-shin ·
`7-8` L-foot · `9` chest/torso · `10` R-upperarm · `11` R-forearm · `12` R-hand ·
`13` L-upperarm · `14` L-forearm · `15` L-hand · `16` head.

---

## Preferred method — ACViewer (deterministic, no server)

**ACViewer** (github: OptimShi/ACViewer) loads the three retail dat files and composites
clothing on a body exactly the way the client does — no server, controllable camera,
flat lighting. This is the cleanest ground truth.

1. Point it at `client_portal.dat`, `client_cell_1.dat`, `client_local_English.dat`.
2. Load the body **Setup** `0x02000001` (male) / `0x0200004E` (female).
3. Apply each **ClothingBase** DID from the shot list (e.g. `0x100000A5`) at its default
   palette (undyed). ACViewer's model/clothing view takes a ClothingBase + Setup.
4. Frame the whole body, vertical-centered, plain/dark background.
5. Screenshot **front** (facing camera) and **back** (rotate 180°).
6. For the `dye-<color>` shots: apply that dye palette to the same piece and shoot front.

## Alternative — the retail client on a private server (ACE / GDLE)

If you want the true in-client shader look instead of the viewer:
1. Spawn each item by name (the item names are in `SHOTLIST.json`), equip on a male and a
   female Aluvian character.
2. Stand in flat open daylight (no body shadows), idle pose, 3rd-person camera pulled
   back so the whole body is framed.
3. Screenshot front and back. Hide the UI/HUD if possible.

Either method is fine — note which you used in `docs/ground-truth/METHOD.txt`.

---

## Capture rules (keep shots comparable)

- **One piece at a time**, on an otherwise **nude** body (so I see exactly what the piece
  changes). The nude baselines (`nude_m_*`, `nude_f_*`) are required — capture them first.
- **Front** = character facing the camera; **back** = 180° turn. Same camera distance for
  every shot.
- Whole body in frame, standing/idle, vertical.
- Even lighting, plain background (dark grey ideal). PNG, at least ~512 wide × ~1024 tall.
- No dyes except the explicit `dye-red / dye-blue / dye-green` shots.

## File naming — already decided in SHOTLIST.json

`<clothingBaseHex>_<m|f>_<front|back>.png`  e.g. `100000a5_m_front.png`
Nude: `nude_<m|f>_<front|back>.png`  ·  Dye: `100000a5_m_front_dye-red.png`

There are **47 shots** total (10 items × 2 genders × 2 angles + 4 nude baselines + 3 dye).
The full list, with each item's name, DID, and the parts it swaps, is in `SHOTLIST.json`.

## Deliver

Put the PNGs in `docs/ground-truth/shots/`, add `METHOD.txt` (one line: which tool), commit
and push. I'll pair each file to its `acclothing.json` record by the DID in the filename
and verify my render of that ClothingBase matches yours, part by part.
