# Ships — ownable, boardable water travel (Ultima-style)

Adds player-owned vessels for crossing Dereth's rivers and seas, modelled on the Ultima series'
ship ownership (a Shipwright sells deeds; a cheap shallow-water skiff up to a costly deep-water
frigate; you board the vessel and pilot it over the water).

## Ship tiers (period-correct)

| Ship | Cost | Draft (min water depth) | Notes |
|------|------|-------------------------|-------|
| **Skiff** | 600p | 0.4 | small open rowboat — shallow rivers & coves, cheapest |
| **Cog** | 6,500p | 1.6 | single-masted merchant cog with fore & stern castles — open water |
| **Caravel** | 24,000p | 3.0 | three-masted (square + lateen), fast & ocean-going — deep sea only |

Bigger hulls draw more water: a caravel can't enter the shallows a skiff can, exactly the Ultima
skiff-vs-frigate trade-off. Models are procedural `THREE.Group`s (hull, prow, transom, deck, rails,
masts, square + lateen sails, castles, rudder, bowsprit), matching the game's existing model style.

## How it works

- **Buy** — a **Shipwright** (⚓, `VENDOR_TYPES.shipwright`) is seated dockside at the most-coastal
  town. Buy a **deed** to gain ownership (`player.ship`); the vessel is floated at the nearest
  navigable water. Buying a new deed replaces the old ship.
- **Board** — walk up to your ship and press **E**. You climb aboard and take the helm.
- **Pilot** — **W/S** sail forward/back; **mouse** or **←/→** steer. The hull hugs the waterline
  and bobs on the swell; it's blocked from sailing onto land or into water too shallow for its
  draft. While aboard you don't drown, so a ship is how you cross deep water you'd otherwise sink in.
- **Disembark** — press **E** near shore to step onto dry land; the ship stays where it floats.
- **Persistence** — ownership and the ship's berth are saved (`player.ship = {type,x,z,yaw}`), and
  re-floated on load / after any world rebuild. Coordinates migrate across world-scale changes like
  other saved positions.

## Implementation seams (all in `index.html`)

- `SHIP_TYPES`, `buildShip`/`build{Skiff,Cog,Caravel}`, `shipNavigable`, `findWater`,
  `spawnOwnedShip`, `buyShip`, `boardShip`, `disembarkShip`, `updateShipPilot`, `updateShips`,
  `seatShipwright`, `shipDeedStock` — the ship module (just above `VENDOR_TYPES`).
- `player.ship` / `player.aboardShip` fields; `ships[]` world array.
- `update(dt)` branches to `updateShipPilot` while aboard (on-foot movement is otherwise unchanged).
- `interact()` boards a nearby ship / disembarks when aboard.
- Shop buy handler grants a deed (`it.shipdeed`); `saveGame`/`applySaveObj` persist `player.ship`;
  `buildWorld` seats the Shipwright and re-floats an owned ship.

## Verified (headless harness, real code paths)

Models build (skiff/cog/caravel), deed purchase deducts gold + grants + spawns on water, board +
pilot moves over water and rides the deck, land is a wall, deep-draft gating (caravel needs ≥3.0
depth), no drowning aboard, disembark ashore, and save→load re-floats the ship. On-foot movement
regression-clean.
