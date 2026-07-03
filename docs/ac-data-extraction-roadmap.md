# AC Data Extraction Roadmap — what's been pulled vs. what's still in the vault

Audit of everything extractable from the real AC client data (`acdata/client_portal.dat`,
`client_cell_1.dat`, `client_highres.dat` — user-supplied, gitignored) and the ACE-World
community database dump (`acdata/*.sql`), against what the repo has already extracted and wired.

## STATUS — branch `ac-data-full-extraction` (this pass swept all 18 items)

Legend: **WIRED** = extracted + hooked into gameplay · **ASSET** = extracted + drop-in asset, deeper
in-engine wiring deferred to avoid regressing a tuned system · **N/P** = extractor ran but produced
no usable output.

| # | Item | State | Notes |
|---|---|---|---|
| 1 | Creature attack/death/cast anims + roster | **WIRED** | 42 GLBs w/ Attack/Death/Cast clips; combat/aggro vocals play |
| 2 | Item icons | **WIRED** | 3960 icons on paper-doll + shop rows |
| 3 | Sounds | **WIRED** | 167 samples → SFX + per-creature vocals |
| 4 | SpellTable + components | **WIRED** | cast words spoken; reagents shown in spellbook |
| 5 | ClothingTable armor visuals | **ASSET** | 771 ClothingBase part-swaps; full avatar reskin needs the ClothingBase GfxObjs exported |
| 6 | NPC dialogue/quest emotes | **WIRED** | 2587 NPCs speak real retail lines |
| 7 | Retail loot tables | **WIRED** | creatures drop at authentic per-kind tier (item names kept ours to preserve material/tinker parsing) |
| 8 | Crafting recipes | **WIRED** | searchable Recipe Codex in the tinker panel (1500 recipes) |
| 9 | Town building placement | **ASSET** | 811 real structures w/ offsets+DIDs; rendering needs building Setup meshes exported |
| 10 | Region scenery + sky/fog | **ASSET** | per-terrain flora tables + day-night keyframes; kept as data — the tuned procedural sky/flora look better than raw client keyframes |
| 11 | Dungeon environment meshes | **ASSET** | REAL geometry+textures for 8 dungeons; in-engine render needs a geometry-driven collision pass (meshes are in AC coords, not the room-graph) |
| 12 | Chargen / particles / music / housing | **PARTIAL** | music **WIRED** (ambient loops); housing extracted (**ASSET**); chargen + particles **N/P** (format not cleanly parsed this pass) |

The ASSET-state items are the genuinely hard extractions (real meshes, clothing swaps, building
placement) — captured and committed as reusable drop-in data; their remaining work is engine
integration, not data recovery. Follow-up wiring candidates: export building/clothing Setup GfxObjs
through the ac_env pipeline, then a geometry-driven dungeon/town renderer.

## ALREADY EXTRACTED & WIRED ✅

| Data | Source | Tool | Wired into |
|---|---|---|---|
| Real Dereth terrain (2041×2041 height-index + terrain type + road bits) | cell.dat outdoor landblocks + Region LandHeightTable | `ac_dat_export.py` | `assets/acmap.png` → terrain sampler (~line 1641) |
| Dungeon interiors, all remaining 129 lairs (true room graphs from EnvCells + real spawns/levers/chests) | cell.dat EnvCells + ACE LandBlockExtendedData | `ac_dungeon_export.py` | `assets/dungeon-layouts.json` → DUNGEON_SCRIPTS merge |
| Real player bodies (human M/F) + a few creature meshes w/ textures | portal.dat Setup/GfxObj/Surface | `ac_model_export.py` | `assets/acmodels/` → avatar swap (~line 3115) |
| 33 real creature models with authentic Idle/Walk animations | portal.dat Setup + MotionTable cycles | `ac_creature_export.py` | `assets/models/monsters/ac/*.glb` → MONSTER_MODELS |
| Retail vendor rosters + item catalogs per town | ACE weenies + create_list + landblock_instance | `ace_world_export.py` + `ace_trim_towns.py` | `assets/acvendors.json` |
| Creature level/hp/xp bands, real spawn map (51×51), 1728 portals, 255 lifestones | ACE attribute/encounter/instance tables | `ace_world_more.py` | `assets/acworld.json` |

## NOT YET WORKED ON — ranked by impact

### Tier 1 — extends tools we already have (low effort, high payoff)
1. **Creature ATTACK / DEATH / CAST animations.** `ac_creature_export.py` already parses
   MotionTables but only exports the Ready (idle) and Walk cycles. The same tables carry the
   full combat set: melee attack variants per stance, missile, spellcast, death, flinch, twitch/
   emote cycles. Export as extra GLB clips (`Attack`, `Death`, `Cast`) — the game's mixer already
   plays an attack clip when `m.actAtk` exists; death anims would replace the shrink-out.
2. **The rest of the creature roster.** 33 of the bestiary's kinds are done; the ACE DB maps
   ~7k weenies onto a few hundred unique Setups. Everything the BESTIARY still fakes procedurally
   (and named bosses with unique looks) can come out the same pipeline.
3. **Real item ICONS.** portal.dat icon bitmaps (0x06 textures referenced by weenie `PaletteBase`/
   icon DIDs) — the inventory/spellbook UI could show authentic AC icons instead of text/emoji.
4. **Real SOUNDS.** portal.dat 0x0A audio resources + the SoundTable (0x20) that maps
   creature/action → sound. Creature roars, weapon hits, spell casts, portal whoosh, footsteps,
   UI clicks — the game currently synthesizes all SFX in WebAudio. Export to OGG/WAV files.

### Tier 2 — new extractors over known formats
5. **SpellTable (portal.dat 0x0E00000E) + components (0x0E00000F).** The complete authentic spell
   roster: names, schools, levels, spell WORDS ("Zojak Quafeth…"), component formulas, icons.
   Would replace/verify the hand-built SPELLBOOK, and scribing could demand real components.
6. **ClothingTable (0x10).** How every armor/clothing item reskins the human Setup (part swaps +
   palette tints) — real armor VISUALS layered on the real avatar bodies per equipped item.
7. **NPC dialogue & quest logic (ACE `weenie_properties_emote` + `quest` tables).** Every retail
   NPC's actual dialogue lines, quest-item exchanges, rewards, and quest stamps. The QUEST_GIVERS
   system could carry authentic conversations instead of paraphrase.
8. **Loot system (ACE `treasure_death` / `treasure_wielded` / `treasure_gem_count` etc.).**
   Retail loot tiers per creature — real drop tables, real item names/mods per tier, wielded
   gear that drops. Would replace the hand-tuned loot generator.
9. **Crafting/tinkering recipes (ACE `recipe` + `cook_book` tables).** Every retail combine —
   salvage, tinkering percentages, alchemy, cooking, fletching — with real difficulty rolls.
10. **Town building placement (ACE `landblock_instance` building weenies + portal.dat building
    Setups).** Every town's actual structures at actual positions/rotations — Holtburg shaped
    like the real Holtburg. Buildings are currently procedural archetypes at approximate spots.

### Tier 3 — deeper world fidelity
11. **Region scenery tables (portal.dat Region 0x13).** Which trees/rocks/plants spawn on which
    terrain type, at what densities — authentic per-biome flora instead of hand-tuned clumping.
12. **Region sky/fog/lighting.** Day-night sky palettes, fog colors/distances, sun/moon tracks,
    star fields — the authentic Dereth light. (Currently hand-tuned to taste; could be verified.)
13. **Full 3D dungeon environments (portal.dat 0x0D Environment meshes).** Each EnvCell references
    an Environment (the actual room mesh — arches, pillars, stairs). Rendering those instead of
    the abstract box rooms = dungeons that LOOK like retail, not just route like it.
14. **Particle emitters (0x32/0x33) + spell FX mapping.** Authentic spell projectile/impact VFX,
    portal swirls, lifestone sparkles.
15. **CharGen data (portal.dat 0x0E000002).** Real heritage starting attributes/skill costs/
    templates, appearance option tables (hair/skin palettes per heritage) for character creation.
16. **Music & ambient (portal.dat 0x31 dat music / ambient sound tables).** Original background
    audio, region ambient loops (wind, swamp, town murmur).
17. **Housing (ACE `house` + slumlord data).** Real housing locations, types, purchase costs —
    the settlements are currently hand-placed approximations.
18. **client_highres.dat textures.** Higher-resolution versions of terrain/creature/armor textures
    for everything above.

## Notes
- Formats are all documented by ACEmulator's DatLoader / ACViewer (BSD) — the repo's tools
  already implement the hard parts (BTree reader, Setup/GfxObj/MotionTable parsing).
- `acdata/` is machine-local: whichever session runs an extractor must have the dat files.
- Everything lands as a drop-in asset with a graceful fallback, per the established pattern
  (procedural stays as the offline default).
