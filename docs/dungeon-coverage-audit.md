# Dungeon coverage audit — "renders nearly empty" (truncated EnvCell export)

**Tracking issue:** [#762](https://github.com/w5ohr/Dereth/issues/762) · **Detector:** [`tools/dungeon_coverage_audit.py`](../tools/dungeon_coverage_audit.py) · **Machine-readable results:** [`dungeon-coverage-audit.json`](dungeon-coverage-audit.json)

## The bug
A real-geometry dungeon (`assets/acdungeons/<file>.json`) stores its **walkability + minimap** in `cellPos` (cell centres on a 10-unit grid) and its **visible walls** in `groups[].verts/.idx`, which `buildDungeonReal()` (index.html) assembles *untransformed*. `tools/ac_env_export.py` truncated some dungeons (`"capped": true`): the worst ones contain almost no real geometry — a few dozen vertices with the index buffer padded by one tiny quad repeated thousands of times. Result: the dungeon is walkable with a correct map, but **most walls are missing** and it renders nearly empty. (Player-reported on the Highland/Direlands Tusker Temple.)

This is a **completeness** problem — different from the *scrambled-placement* audit (#754–#760), which correctly found the walls that DO exist are placed right.

## How to find it (reproducible)
```
python3 tools/dungeon_coverage_audit.py                 # print the report
python3 tools/dungeon_coverage_audit.py --threshold 0.4 # flag < 40% coverage
python3 tools/dungeon_coverage_audit.py --json docs/dungeon-coverage-audit.json
```
Two signals per dungeon:
1. **cell coverage** — fraction of `cellPos` with a wall vertex within ~8u (XZ). Healthy ≈ 0.5–1.0; broken drops toward 0.1.
2. **maxT/V** — worst per-group triangles ÷ unique-vertices. Legit ≈ 0.5–3; a padded/degenerate index buffer shows 50–150×.

## The fix (shipped for the Tusker Temples; generalise for the rest)
Ignore the broken export and rebuild walls/floors/ceilings from the cell grid: floor under every cell, wall on every open edge, ceiling where no storey sits above — one merged mesh skinned with the dungeon's own wall texture. See `synthDungeonWalls()` / `SYNTH_WALL_DUNGEONS` in index.html. Walkability is unchanged (it already comes from `cellPos`), so the walls line up. **To roll out:** switch the `SYNTH_WALL_DUNGEONS` gate to trigger for any dungeon below ~0.35–0.40 coverage, or re-export with a higher cap / fix the exporter. Re-run the detector after any change.

## Current results — 76 dungeons under 50% coverage (22 severe < 25%)
✅ = fix already shipped.

| coverage | cells | verts | maxT/V | capped | dungeon |
|---:|---:|---:|---:|:--:|---|
| 13% | 590 | 106 | 152× | yes | Linvak Tukal Entryway |
| 13% | 618 | 175 | 71× | yes | Gredaline Consulate |
| 14% | 1040 | 333 | 115× | yes | Greater Olthoi Brood Hive |
| 14% | 514 | 235 | 80× | yes | Direlands Tusker Temple ✅ |
| 14% | 514 | 235 | 80× | yes | Highland Tusker Temple ✅ |
| 14% | 514 | 235 | 80× | yes | Tusker Temple ✅ |
| 15% | 850 | 410 | 108× | yes | Olthoi Horde Nest |
| 16% | 625 | 376 | 122× | yes | Matron Hive East |
| 16% | 625 | 376 | 122× | yes | Matron Hive South |
| 16% | 625 | 376 | 122× | yes | Matron Hive West |
| 17% | 489 | 157 | 96× | yes | Singular Obsidian Repository |
| 17% | 509 | 269 | 88× | yes | Panopticon |
| 19% | 478 | 135 | 114× | yes | Martinate Holding |
| 20% | 461 | 158 | 89× | yes | Lugian Quarry |
| 21% | 966 | 410 | 123× | yes | Incunabula Vault |
| 23% | 718 | 440 | 123× | yes | Singularity Bore |
| 23% | 437 | 175 | 63× | yes | Tumideon Fortress |
| 24% | 491 | 344 | 126× | yes | Lugian Ice Tunnels |
| 24% | 444 | 324 | 112× | yes | Mossy Cave |
| 24% | 406 | 143 | 116× | yes | Singular Chorizite Repository |
| 25% | 387 | 141 | 86× | — | Lugian Mines |
| 25% | 555 | 407 | 63× | yes | Mosswart Maze |
| 25% | 398 | 220 | 69× | — | Aerbax Laboratory |
| 25% | 386 | 154 | 103× | — | Ridge Citadel |
| 25% | 731 | 235 | 80× | yes | Mammet Foundry |
| 26% | 359 | 422 | 59× | — | Dark Tree Crystal Mine |
| 26% | 1319 | 181 | 86× | yes | Black Death Catacombs |
| 27% | 780 | 583 | 132× | yes | Coral Tunnels |
| 28% | 505 | 537 | 38× | yes | Catacombs of the Forgotten |
| 28% | 593 | 337 | 133× | yes | Corrupted Catacombs |
| 28% | 799 | 574 | 53× | yes | Falcon Clan Camp |
| 28% | 768 | 467 | 58× | yes | Reedshark Clan Camp |
| 28% | 653 | 509 | 62× | yes | Black Spawn Den |
| 29% | 651 | 879 | 41× | yes | Hidden Dungeon |
| 29% | 417 | 395 | 52× | yes | Desert March |
| 30% | 132 | 223 | 24× | — | Spirit Cell |
| 32% | 1524 | 336 | 134× | yes | Freebooter Phyntos Wasp Hive |
| 32% | 927 | 532 | 47× | yes | Mask Clan Camp |
| 33% | 855 | 658 | 75× | yes | Frozen Library |
| 33% | 388 | 509 | 126× | — | Ancient Empyrean Grotto |
| 35% | 1201 | 939 | 69× | yes | Deep Mukkir Nest |
| 36% | 396 | 689 | 110× | — | Banderling Conquest Dungeon |
| 37% | 196 | 321 | 60× | — | Renegade Incursion |
| 38% | 56 | 63 | 23× | — | Lugian Outpost |
| 38% | 136 | 229 | 25× | — | Temple of Forgetfulness |
| 38% | 562 | 608 | 59× | yes | Stable Rift |
| 39% | 643 | 305 | 134× | yes | Burun Cavern |
| 40% | 841 | 573 | 54× | yes | Shreth Clan Camp |
| 40% | 322 | 483 | 32× | — | Northern Infiltrator Keep |
| 41% | 396 | 675 | 40× | — | Smugglers Hideaway |
| 41% | 368 | 761 | 43× | — | Southern Infiltrator Keep |
| 41% | 639 | 765 | 111× | yes | Egg Orchard |
| 41% | 716 | 542 | 44× | yes | Mountain Fortress |
| 41% | 934 | 666 | 66× | yes | Murk Warrens |
| 41% | 879 | 451 | 99× | yes | Mite Maze |
| 42% | 911 | 612 | 103× | yes | Olthoi Brood Hive |
| 42% | 433 | 145 | 78× | yes | Ravenous Vault |
| 42% | 457 | 597 | 61× | yes | Umbral Hall |
| 42% | 292 | 164 | 96× | — | Renegade Stronghold |
| 42% | 196 | 335 | 60× | — | Penguin Den |
| 43% | 1474 | 568 | 51× | yes | Northern Temple Catacombs |
| 43% | 114 | 359 | 21× | — | Arcanum Research Facility |
| 43% | 822 | 241 | 78× | yes | Colossus Foundry |
| 44% | 134 | 116 | 56× | — | Forking Trail |
| 44% | 735 | 749 | 39× | yes | Renegade Fortress |
| 46% | 447 | 1019 | 98× | yes | Back Tunnels |
| 46% | 467 | 873 | 80× | yes | Crumbling Empyrean Mansion |
| 46% | 449 | 372 | 106× | yes | Ruschk Iceberg |
| 46% | 599 | 360 | 122× | yes | Olthoi Chasm |
| 46% | 550 | 1050 | 69× | yes | Sand Shallows Cave |
| 47% | 747 | 534 | 48× | yes | Gromnie Clan Camp |
| 48% | 290 | 149 | 60× | — | Ruined Empyrean Vault |
| 48% | 867 | 800 | 46× | yes | Swamp Temple |
| 49% | 270 | 291 | 75× | — | Mattekar Cave |
| 49% | 178 | 259 | 18× | — | Thasali |
| 50% | 188 | 429 | 47× | — | Crater Pathway |
