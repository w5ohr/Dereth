# AC New-Player Experience + Physics — Two-Client Split

**2026-07-03.** Three research reports (verified vs the AC manual, ToD manual, wiki screenshots,
GameSpot/Sybex guides, Drunkapedia/Levistras) + a repo audit define the work below. Full details
live in the session research; key numbers are inlined here. Effort-balanced: ~11 pts each.
Standing rules: pull before push; jsc + preview verify + 0 console errors per item; small PRs.

## SLICE A — "Creation & Arrival" → other client (≈11)

### A1. AC-authentic character creation wizard (6)
Rebuild the creator as ToD's staged wizard: **Heritage → Profession → Attributes → Skills →
Appearance(+gender) → Town → Name/Summary**. Wire `assets/acchargen.json` (REAL client data,
currently 0% wired): 330-pt attribute pool (6×10 base + **270 credits**, cap 100, spend-all
prompt), **52 skill credits**, real per-heritage skin/hair/eye palettes + hairstyle counts +
setupIds. The SIX classic templates with exact values (Drunkapedia-verified, e.g. Bow Hunter
40/30/100/100/50/10, spec Arcane Lore/Bow/Melee Defense, trained Item Ench; War Mage
50/40/10/30/100/100, spec Mana Conv/War). **Specialize option at creation** (only time it's
allowed). Heritage free skills that can't be lowered (all: Jump/Loyalty/Magic Defense/Run/
Salvaging; Aluvian +Assess Person/Dagger; Gharu'ndim +Appraise Item/Staff; Sho +Unarmed;
Viamontian +Armor Tinkering, Loyalty spec). Heritage lore blurbs + naming-lore text (verbatim in
research/asheron.fandom "Character Creation/Racial Lore"). 4 towns (Holtburg/Shoushi/Yaraq/
Sanamar), any heritage any town. Live derived vitals (H=End/2, St=End, M=Self). Custom→title
"Adventurer", template→its name as title.

### A2. Starting gear + letters (2)
Grant on creation: **Calling Stone**, per-heritage **Letter From Home** (verbatim texts on wiki,
readable in-game), **Training Weapon** matching template combat skill (upgradeable to "Academy"
via Oil of Rendering), creation clothing, casters: wand + 3 school spells + components; Life
Caster +2 healing kits; Soldier crossbow +30 quarrels (1999 manual kit table in research).

### A3. Post-Academy town greeter chain (3)
Per starter town: **Lifestone Greeter** (Holtburg=Alcott 7,000 XP + first Contract) →
**Bartender Greeter** (Buckminster, 9,300 XP + 500p, explains Contracts) → **Pathwarden**
(12,500 XP + Pathwarden Supply Key → Chest: race armor suit + robe + **Pathwarden Trinket,
+4% XP wielded**). Pathwardens: Thorolf (Holtburg), Koro Ijida (Shoushi), Qanara bint Qolosh
(Yaraq), Thierra Molanti (Sanamar). Total arc ≈41,300 XP → ~L7.

## SLICE B — "Academy, Quests & Physics" → this session (≈12)

### B1. Training Academy tutorial dungeon (6)
New characters spawn in the ToD **Training Academy** (interior halls + 2 courtyard zones;
skippable). Stations: **Society Greeter** (movement/chat; hand her the Calling Stone) →
**Jonathan** skip-token (full rewards + 11,000 XP, one-way exit) → **Samuel** armor room (find
Leather Cap/Gauntlets/Leggings) → **Training Master** practice area + **Sparring Golem** that
lectures mid-fight (power bar, heights) → Academy Token 1,000 XP → **Foreman** Carpenter Wasp
2,000 XP → **Blacksmith** Thieving Thrungus + Bellows 3,000 XP + Library Key → **Researcher**
sells Oil of Rendering (Training→Academy Weapon) + Wordsmith/Crier/Shopkeep flavor → **Sentry**
finale: Olthoi Tunnels, Adolescent Olthoi, Protection Orb → 5,000 XP + Academy Coat + Facility
Hub Portal Gem → single exit portal to the chosen town. Use the REAL Academy NPC dialogue
already extracted in `assets/acdialogue.json` (greeter, tunlok weapons master, eaushi the
trainer, academy blacksmith/foreman/quartermaster/crier/shopkeep/researcher).

### B2. Starter quests aligned to retail (3)
Fix mappings (Drudge Hideout=HOLTBURG, Braid Mansion=SHOUSHI, Sea Temple=YARAQ) and align
rewards: Alfrin's Stolen Supplies (3 seed bags + Drudge Robber Baron head = 25,000 XP),
Worcer's Missing Heirlooms (8 items ×4,500), Brogord's Demise (2 ×15,000), Lou Ka (8 ×4,500),
Nen Ai (2 ×15,000), Four Stones of Jojii (4 ×9,375), Lubziklan (8 ×4,500 + Mara's Healing Pie),
Sanamar: The Beacon / Missing Necklace / Thrungus Hovels / Trade Alliance. Cross-town:
**Sword of Lost Light** (7 keys: Holtburg Dungeon, Yaraq Tunnels, Shoushi Grotto, Yanshi
Tunnels, Rithwic Crypt, Dungeon Manor, Carved Cave → Halls of Lost Light → Guardian → sword +
shield) and **Elysa's Favor** (Sir Rylanan/Sir Tenshin/Dame Tsaya; Fort Witshire; Dranith
Menacet; 50/100/200k XP + ring).

### B3. Physics per ACE (3)
Arrows/bolts: ballistic **gravity arc** (solved launch), launcher-scaled speed, AC max range,
terrain/wall collision. Spell projectiles: per-spell speeds (bolt/streak/arc differ; arcs
lobbed), authentic range cutoff. **Jump**: hold-to-charge bar; height from Jump skill × charge ×
burden; stamina cost scales with charge. **Fall damage**: ACE threshold/scaling calibration.
(ACE-formula research report pending — use its exact constants.)

## Coordination
Both slices touch `index.html`; A3/B2 both spawn town NPCs — whoever lands second re-anchors.
B1's Academy dungeon is new content (low collision). Physics (B3) touches projectile/movement
code only.
