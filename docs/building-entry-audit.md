# Building Entry Audit (2026-07-05)

Verifying that buildings can be entered without clipping or other issues. Method: an
in-engine audit run against the REAL door/collision system (per streamed building: door
gap clear, entry centreline walkable for the player radius, door aligned to the model's
visible door marker, no step-up over `STEP_UP` without a ramp, terrain not piercing the
interior floor), swept across the towns; plus an offline `groundY` scan over every
world-structure building.

## Town buildings — GOOD (93% cleanly enterable)

Audited 26 major towns + Holtburg = **548 enterable buildings, 511 pass (93.2%), 37 fail**.
Every starter capital + a culturally-diverse spread (Aluvian / Sho / Gharu'ndim, keeps).

Residual issues, by category (all concentrated in tightly-packed blocks — real AC town
density at 1:1):
- **`path_blocked` (29)** — the straight entry centreline grazes a neighbour. Mostly at
  the *outer* approach (2–3 m out); the door itself is clear and usable at a slight angle.
  A handful block *just inside* the door (`@-0.5`/`-1.6`) where a neighbour's footprint
  overlaps the interior. Low severity — the door works.
- **`door_offmarker` (7)** — the real clip: the walkable opening was cut 5–13 m from the
  model's visible door, so you enter through a **blank wall** (the AC mesh's doorway hole
  is at the marker, not the cut). Verified visually at Cragstone (enter through solid
  stone). **Cause:** in `tbCutDoorway`, when the visible door faces a neighbour it's
  penalised and the cut falls back to a clear wall. These 7 are *threshold-blocked* cases
  (neighbour right at the door) — inherent to packing real footprints; a scoring tweak
  (`[0,1.4,2.6]`→`[0,1.0]`) was tried and **reverted** because it regressed Sanamar
  (35→33 pass) without fixing them. Needs a deeper fix (cut an actual mesh doorway hole at
  the fallback wall, or unpack the block).
- **`floor_poke` (2)** — terrain pierces the interior floor on a steep coastal site
  (Sanamar). Building floor sits at the highest footprint corner; a taller interior terrain
  bump poke through. Minor.

Fully clean towns include Fort Tethana, Nanto, Lytelthorpe, Uziz, Neydisa Castle, Shoushi.

## World structures — 18.5% are UNDERWATER (the real problem)

6076 buildings across 1431 landblocks (these do NOT get the town de-interpenetration solve).
Offline `groundY` scan of every one:

| | count | % |
|---|---|---|
| on land | 4948 | 81.4% |
| **deep sea (gy < −1.5)** | **1116** | **18.4%** |
| shallow sea | 12 | 0.2% |

**102 landblocks are FULLY submerged** — every building at floor y ≈ −6.5 (sea floor). You
drown swimming to them; they're unreachable. All cluster in the **western ocean**
(x ≈ −4000…−7000, z ≈ +3000…+7900) — AC's island region (Aphus Lassel, Aerlinthe, the
Osteth/Linvak isles). The heightmap has open sea there instead of the island landmasses, so
their structures sit on the sea floor. Worst blocks: `1203` (49 bldgs @ −6972,7932), `3D0B`
(33 @ −4220,7420), `2F2F` (31), `0D4A` (29), `3A11` (29), `3073` (28)…

Where world structures ARE on land, they audit like towns — the worst-overlap on-land block
sampled was 43/49 enterable. (An offline footprint-overlap screen flagged 429 landblocks,
but that over-counts intentional multi-part/adjacent composition, so it is NOT a reliable
clip indicator — the door audit is.)

## Severity / suggested fixes (for later)

1. 🔴 **Underwater world structures (1128 bldgs / 102 landblocks).** Biggest issue.
   Options: (a) raise terrain under these clusters so the islands exist, (b) seat submerged
   buildings on a raised foundation island above sea level, or (c) drop landblocks whose
   terrain is deep sea (if the island isn't in the heightmap, the structures don't belong).
2. 🟠 **`door_offmarker` (7 town bldgs).** Cut an actual doorway hole in the mesh at the
   fallback wall so entry doesn't clip through blank stone, or bias harder to the marker
   only when its threshold is truly clear.
3. 🟡 **`path_blocked` interior cases + `floor_poke` (few).** Tighten the interior floor to
   cover terrain bumps; nudge the ~3 buildings whose interior a neighbour overlaps.

Code was left UNCHANGED (the one scoring experiment was reverted after it regressed).
