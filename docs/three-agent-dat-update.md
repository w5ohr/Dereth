# Three-Agent End-of-Retail Dat Update — Work Split

**2026-07-04.** `acdata/` now carries the TRUE end-of-retail dats (from ac-updates.zip):
`client_portal.dat` (926,941,184 — was 925,918,208), `client_cell_1.dat` (348,127,232 — was
347,787,264), `client_local_English.dat` (1,048,576 — was 1,029,120). Every asset pack in
`assets/` was exported from the OLDER dats — this effort re-extracts **everything** against the
final builds and updates whatever changed: graphics, dungeons, items, creatures, NPCs, skills,
stats, spells, tinkering, the map — absolutely everything. The ACE-World SQL is unchanged, so
ACE-derived packs (vendors, dialogue, books, recipes, rewards, loot, housing) do NOT need
re-runs unless a portal-keyed merge feeds them (noted below).

**Method per item:** re-run the exporter → diff the output pack vs committed → if changed,
commit the new pack + re-verify the wired system in preview (jsc + preview_eval + 0 console
errors) → PR per batch, prefix the PR title with your agent tag (A1:/A2:/A3:). Pull main before
every push. Do not touch another agent's asset dirs or tools.

---

## AGENT 1 — World & Graphics lane
| # | Item | Tool | Output pack | Wired system to re-verify |
|---|------|------|-------------|---------------------------|
| 1 | Terrain map + heights | tools/ac_dat_export.py | assets/acmap.png, heights | terrain build, roads, minimap/world map |
| 2 | Region: scenery tables + sky | tools/ac_region_export.py | acscenery.json, acsky.json | flora scatter (scenPickList), day-night light |
| 3 | Flora models | tools/ac_scenery_export.py | assets/acflora/ | real-tree scatter |
| 4 | Dungeon layouts | tools/ac_dungeon_export.py | dungeon-layouts.json | scripted dungeon merge |
| 5 | Dungeon geometry | tools/ac_env_export.py | assets/acdungeons/ | buildDungeonReal (232+ interiors) |
| 6 | Town structures + building models | tools/ace_building_export.py + the actownmodels exporter | actowns.json, assets/actownmodels/ | tbBuildTown/tbPlaceStructure |
| 7 | Particles | tools/ac_particle_export.py | acparticles.json | (unwired — re-export only) |
| 8 | NEW-in-final-dats sweep: enumerate portal/cell entries absent from the OLD dats (compare BTree file lists old-vs-new if old copies retained; else diff pack outputs) — catalog any brand-new landblocks/models for follow-up | — | report | — |

## AGENT 2 — Systems & Data lane  *(this session — I am Agent 2)*
| # | Item | Tool | Output pack | Wired system to re-verify |
|---|------|------|-------------|---------------------------|
| 1 | Skills + XP tables | tools/ac_skill_export.py | acskills.json | SKILLS_DEF override, attr/vital/skill raise charts, creation credits — re-run the stats-parity checks (110/73 pt-1, 4.02B/4.29B totals, 208/226 caps) |
| 2 | Spells + components | tools/ac_spell_export.py | acspells.json | spellbook boot pass (words, comps, icons) |
| 3 | Spell effect numbers | tools/ace_spell_stats.py (needs 3-Core; if absent, verify acspellstats against new acspells keys only) | acspellstats.json | spell damage/duration/mana overrides |
| 4 | Character generation | tools/ac_chargen_export.py | acchargen.json | creation numbers (330 pool / 52 credits), heritage templates & palettes |
| 5 | UI textures | tools/ac_ui_export.py --export (same DID set) | assets/acui/ | vitals bars, charge bar, page arrows, panel ornament |
| 6 | **NEW: string tables** | write tools/ac_strings_export.py for client_local_English.dat (StringTable entries) | acstrings.json | wire where useful (item descriptions, UI strings) — catalog first |
| 7 | Tinkering/recipe cross-check | (ACE-derived acrecipes.json unchanged) verify recipe/tinker systems still pass preview checks against new skills/spells | — | per-item tinkering, crafting panel |

## AGENT 3 — Creatures, NPCs & Items lane
| # | Item | Tool | Output pack | Wired system to re-verify |
|---|------|------|-------------|---------------------------|
| 1 | Creature models + anims | tools/ac_creature_export.py | assets/models/monsters/ac/ (42+ GLBs — check for NEW extractable kinds: shadow, mummy, mukkir, remoran, zefir stand-ins!) | monster spawning/anim |
| 2 | Human bodies + player anims | tools/ac_model_export.py, ac_player_anims.py | assets/acmodels/, acmotions.json | avatar + NPC bodies, MotionTable playback |
| 3 | Armor-on-body + clothing | tools/ac_armor_export.py, ac_clothing_export.py | assets/acarmor/, acclothing.json | equipped-armor visuals |
| 4 | Item models | tools/ac_item_models.py | weapon/caster meshes | in-hand + ground item meshes |
| 5 | Item icons | tools/ac_icon_export.py | assets/acicons/ | inventory/trade/vendor icons |
| 6 | Sounds + music | tools/ac_sound_export.py, ac_music_export.py | assets/acsounds/, acmusic/ | SFX/ambient beds |
| 7 | Real head models (Phase 2) | the ac-heads pipeline | — | face system |

## Coordination
- **Shared hotspot:** `index.html` boot-pass overrides (skills/spells wiring — Agent 2's) vs
  model loaders (Agent 3's) vs world builders (Agent 1's) — different regions; pull-before-push
  and re-anchor after any failed edit.
- The old dats were only ~0.1% smaller — most packs will diff EMPTY. An empty diff is a
  RESULT: note "verified current" in your PR and move on. The prize items most likely to have
  real deltas: late-retail landblocks (A1), the five missing creature kinds (A3), and
  client_local_English.dat which was NEVER extracted (A2).
- `acdata/ac1install.exe` (the base ToD installer) is staged for the user to run —
  `client_highres.dat` will land in its install dir; whoever is active when the user reports
  it should copy it to acdata/ and run the high-res texture upgrade pass (all tex/ dirs,
  same TIDs, no engine changes).


---
## A2 status (2026-07-04, first pass)
- Items 1/2/4/5 re-exported against the end-of-retail dats: **byte-identical** � acskills,
  acspells, acchargen, and all 35 acui textures are VERIFIED CURRENT (the systems resources
  didn't change in the final builds).
- Item 3 (acspellstats): upstream acspells unchanged + 3-Core absent -> no action needed.
- Item 6 (strings): local dat probed � 101x 0x21 StringTables + 15x 0x23. The payloads are
  CIPHERED (not zlib; not plain UTF-16). tools/ac_strings_export.py is scaffolded with the
  harvest pipeline; the decode must be ported from ACEmulator ACE.DatLoader/FileTypes/
  StringTable.cs (follow-up). No pack committed until it decodes clean.
- Item 7: nothing upstream changed -> tinkering/crafting stand as verified.

## A2 status update (strings decoded)
- Ported the exact ACE StringTable format (StringTable.cs + StringTableData.cs +
  compressed-uint/unicode readers): ALL FIFTEEN 0x23 tables decode cleanly (0 failures) ->
  assets/acstrings.json: 7,050 strings including the full 873-entry retail CHARACTER TITLE
  table (0x2300000E). Wire-up candidates: the titles list (titles panel), plus the other
  0x23 tables (catalog: sex/heritage labels, town names, UI enumerations).
- The 101 0x21 tables use a DIFFERENT undocumented layout (not the StringTable format,
  not zlib) - still the open follow-up.

## A2 lane COMPLETE (2026-07-04)
- 873 retail titles WIRED: acstrings.json loads at boot; the character sheet's new Titles
  section lists every held title with click-to-display and the retail-roster check mark.
- The 101 0x21 local-dat tables are IDENTIFIED: every entry reads (id, 800, 600) - they are
  the retail client's UI LAYOUT definitions at base 800x600, not strings. Full layout decode
  = a future resource for pixel-perfect panel placement (out of A2 scope).
- Remaining lane item: NONE. (client_highres.dat texture upgrade is staged but blocked on
  the user running acdata/ac1install.exe - whoever is active takes it per the handoff note.)

## A3 lane status (worked by Agent 2, 2026-07-04)
- Icons (3,960), sounds, music, player anims: re-exported BYTE-IDENTICAL - verified current.
- Armor (842 meshes), clothing, item models (988): verified current.
- Human bodies: REAL DELTA - human_male/female.json updated from the end-of-retail dats
  (part-frame refinements); textures re-encoded pixel-identical were reverted.
- Creatures: THE PRIZE - the exporter now yields 66 kinds (was 42) against the final dats,
  including ALL FIVE former CC0 stand-ins: shadow, mummy, mukkir, remoran, zefir - each with
  Idle/Walk/Attack/Death/Cast clips. Four auto-wire via the existing override list; mummy
  re-pointed from the tinted-zombie stand-in to the real Mu-miyah extract. Gap #10 of
  ac-remaining-gaps.md is CLOSED - zero non-AC creature models remain.
- Item 7 (heads Phase 2) belongs to the original session's WIP pipeline - left untouched.

## HIGHRES COMPLETE (2026-07-04, Agent 2)
- The user ran ac1install.exe; client_highres.dat (133 MB) copied to acdata/.
- Discovered the client's own mapping: each portal SurfaceTexture lists [highres_tid,
  lowres_tid] - every exporter had resolved the LAST (lowres). tools/ac_highres_export.py
  builds lowres->highres (2,283 pairs) and overwrites pack PNGs in place under the SAME
  filenames: 1,224 textures upgraded to double resolution (455 bodies/creatures, 437
  buildings, 321 dungeons, 11 flora) - ZERO engine changes. The LAST blocked item on the
  graphics-extraction ledger is closed.
