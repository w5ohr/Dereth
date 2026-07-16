# Dereth — An Asheron's Call Homage

A playable **3D action-RPG** inspired by **Asheron's Call** (Turbine, 1999), built on Three.js
(bundled locally). It renders a large slice of the real continent of **Dereth** — its three cultures,
its towns at their true in-game coordinates, its creatures, magic, and death-and-Lifestone loop — from
a single self-contained HTML file, with an optional dependency-free Python server that turns it into a
shared-world MMO.

It plays two ways:

- **Online — the Dereth MMO.** Register an account, roll **up to 8 characters**, and enter a
  **shared, server-authoritative world** alongside other players: shared monsters, world bosses,
  ground loot, Incursions, fellowships, allegiances, secure trade, and chat. The server is a
  **dependency-free Python 3 program** (standard library only — see `server/`) that runs anywhere and
  deploys to a small cloud box (`deploy/`).
- **Offline — solo.** The original **zero-install, fully offline** single-player game still works
  unchanged: open `index.html`, pick a heritage, and play with a local browser save. No toolchain, no
  compilation, no internet required at runtime.

Much of the world's art is **extracted from the real Asheron's Call client data** — creature and NPC
models, heads, item icons, armour and clothing meshes, spell effects, textures, and terrain — so what
you see is Dereth, not a stand-in. (Extraction tooling lives in `tools/`; the client itself ships with
the baked assets and needs none of it at runtime.)

## How to play

- **Double-click `Play Dereth.command`** (or open `index.html` in Safari/Chrome).
- **Play online (the MMO):** in the title screen's **Log In / Register** panel, create an account
  (account name + password), then you land on the **character-select screen** — up to **8 character
  slots**. Create a character (name + heritage) or **Play** an existing one to enter the shared world.
  *(Online requires a running server — see [Multiplayer](#multiplayer--the-dereth-mmo) below. The
  client auto-connects to the same host it's served from.)*
- **Play offline (solo):** below the online panel, choose a **heritage** — **Aluvian** (Holtburg),
  **Sho** (Shoushi), or **Gharu'ndim** (Yaraq) — to begin in that culture's capital (or **Wanderer**
  for a balanced start), or **Continue Your Saga** from a local save. A staged **creation wizard**
  (heritage → attributes → appearance) walks you through a new character, and every new hero starts
  with heritage clothing, starting gear, and a Welcome Letter.
- Then **click the screen to capture the mouse**.
- Your heritage shapes how you look in first person: the **Aluvian** wields a steel **gauntlet & sword**
  (knight), the **Sho** a dark leather glove and **bow** (thief), and the **Gharu'ndim** a **robe sleeve**
  and **wand** that flares with the spell's colour when you cast (mage).
- Press **V** to toggle between **first person** and a **third-person view** where you see your full
  character on a **real AC body and head** — heritage armour (steel / leather / robe) that walks, swings,
  shoots, and casts, complete with gloved hands, boots, shoulder pauldrons, a **mage's cape** or a
  **thief's quiver**. The camera orbits smoothly behind you and pulls in when you back against a wall or
  hill so the view never clips through scenery.
- **Buildings are enterable.** Culture-styled houses, shops, and halls are hollow shells with a doorway
  facing the plaza — walk through the door to step inside; the walls block you, the doorway lets you pass.
- Press **Esc** to release the mouse at any time.

### Controls

| Action | Keys |
|---|---|
| Move | `W A S D` or Arrow keys |
| Look | Mouse (click to capture) |
| Sprint | Hold `Shift` |
| Jump | `Space` |
| Attack (active weapon) | Left-click |
| Swap sword / bow | `Q` |
| Cast the spell bar | `1`–`8`, `Z`, `X` (assign in the Spellbook) |
| Quaff potions | `R` / `G` / `F` |
| Bestiary codex | `B` |
| Spellbook (assign spells to the bar) | `K` |
| World map | `Tab` |
| Settings (sensitivity / volume / FOV / difficulty) | `O` |
| Character sheet (attributes, skills, vitals, titles) | `C` |
| Inventory / paperdoll / tinkering | `T` |
| Loot / bind / portal / talk / delve / harvest / open chest | `E` |
| Chat (online) — open the chat bar | `Enter` (then type, `Esc` to cancel) |
| Mute sound | `M` |
| Pause | `P` |

The spell bar is fully **rebindable**: open the **Spellbook** (`K`) to assign any spell you know to a
hotbar slot. Online **chat** also takes slash commands: **`/who`** (who's online), **`/tell <name> <msg>`**
(or `/w`) and **`/r`** to reply, **`/party invite|accept|leave|list`** + **`/p`** for party chat,
**`/allegiance`**, **`/where`** (your region), and **emotes** like `/wave`, `/cheer`, `/dance`, `/bow`,
`/me <action>`.

### World & atmosphere

- **Day/night cycle** — the sun arcs across Dereth over the **authentic AC day** (a full cycle runs
  about two hours), warming to gold at dawn/dusk and giving way to a star-filled night. A living sky
  carries a glowing **sun** with a god-ray halo, **two moons**, drifting clouds, and a twinkling
  starfield; lighting, fog, and the glow of Lifestones and portals all shift with the hour. A **12-month
  Palatine-Year calendar** and its tithes track the passing seasons.
- **Weather** — region-aware skies cycle through clear, overcast, and rain; storms bring falling rain,
  sight-cutting fog banks, and the hiss of rainfall, with occasional **portal storms**.
- **Safe towns** — creatures cannot enter the haven around a town centre, so you can breathe, tinker,
  trade, and bind in peace. Capitals are walled with gated entrances.
- **Music & sound** — a soft, procedurally-generated ambient score plays under the action (toggle with
  `M`), alongside footsteps, a low growl when a creature notices you, UI clicks, and synthesized
  combat/spell/level-up/portal effects — all generated in-browser via WebAudio, no audio files, fully
  offline. (Non-PCM music extracted from the client is available too.)
- **Ambient life** — fauna wander the wilds, a **Town Crier** feed reports live events, and vendors,
  quest-givers, and society NPCs populate the towns.

### Combat & magic

- **Classless, attribute-driven progression** — the six AC attributes (Strength, Endurance,
  Coordination, Quickness, Focus, Self), raised in ranks with XP (innate 10–100 at creation, up to +190
  more, each rank costing more than the last). Vitals (Health, Stamina, Mana) derive from attributes —
  Health from Endurance, Stamina from Endurance, Mana from Self — and can also be raised directly with XP
  on the character sheet. You are what you train.
- **AC's two-currency skill system** — **skill credits** (52 at creation, more each level) decide *which*
  skills you own: spend them to **train** a skill, or **specialize** a trained one (with a 70-credit spec
  cap) for a **+5 bonus**, cheaper XP per rank, and a higher ceiling (226 ranks vs 208). Then spend **XP**
  to **raise** a trained skill's ranks along the authentic AC per-rank cost curves. Untrained skills work
  only at their attribute base; Horse Riding and Sailing come free-trained.
- **A full spellbook of leveled spells** across the schools, each at multiple levels with rising skill
  requirements: **War Magic** elemental bolts and storms, **Life Magic** (heals, drains, protections,
  vulnerabilities), **Creature Enchantment** attribute self-buffs, **Item Enchantment** aptitudes and
  cantrips, and the full **recall set** (Tie / Recall / Secondary / Sending). You start knowing a
  handful and **learn the rest from scrolls** (loot or bought from a Scrivener) by scribing them.
- **Component casting** — spells draw on **tapers** and reagents; a **Prismatic Taper** empowers a
  cast, and a **focus-mana battery / mana stones** (release-into-gear, consume-to-bank, draw-from-reserve)
  extend your endurance in the field.
- **Status effects & elemental affinities** — Flame sets foes ablaze, Frost chills and slows, Lightning
  stuns; creatures **resist** or are **weak** to fire / ice / shock, with **per-element armour** and
  **weapon damage types** (pierce / slash / bludgeon). Watch your damage numbers and pick the right tool.
- **Deep melee & missile** — per-body-part hit contests, ballistic projectiles with terrain collision
  and range falloff, per-swing damage variance, stamina scaling, burden/encumbrance, **Damage / Crit
  Rating**, **Crushing Blow**, Sneak Attack, and a **rear-attack** bonus.
- **Critical hits** — every strike can crit for double damage, with a flash, a louder ring, and screen
  shake; crit chance scales with **Coordination**.
- **Combat feel** — "weak!/resist" callouts, impact light flashes, a damage-direction indicator,
  monster lunges and death animations, ranged casters that telegraph a dodgeable wind-up, and a
  **kill-streak** XP bonus for chaining kills.
- **Difficulty** — Settings (`O`) offers Adept / Hero / Legend, scaling enemy health and damage.

### Foes & the bestiary

- **The full creature roster of Dereth** — Drudges, Mosswarts, Reedsharks, Tuskers, Shadow Casters,
  Banderlings, Aurochs, Gromnies, Olthoi, **Lugians**, **Tumeroks**, **Mattekars**, undead **Skeletons**,
  floating masked **Virindi**, and more — **66 creature kinds** on real, distinct AC models (limbs,
  snouts, fins, tusks, mandibles, horns, wraith robes, spikes, tails), spawned in lore-appropriate
  lands and scaled by distance from town.
- **Champions** — out in the deep wilds some creatures rise as glowing, named **Champions** with far
  more health, harder hits, and richer loot (guaranteed drops + triple pyreals).
- **World bosses** — named, name-plated giants roam the wilds: *Gnawvil, the Olthoi Queen*, the apex
  terror **Bael'Zharon, the Hopeslayer**, and the three **Shadow Generals** (Ler Rhan, Black Ferah,
  Isin Dule), plus named retail bosses (Aerbax, Gaerlan, Martine). Felling one grants huge XP, a pile
  of pyreals, and rare loot, then it rises again later. *(Online these are **shared** — everyone fights
  the same boss, and a global announcement heralds each spawn and slaying.)*
- **The Bestiary** (`B`) records the creatures you've slain and their weaknesses; undiscovered foes
  stay a mystery until you face them.
- **Vitae** — dying weakens you (a damage penalty shown in the status panel) that you recover by
  earning XP — Dereth's price for death. Death drops a lootable **corpse** and returns your spirit to
  your bound Lifestone.

### Loot, items & crafting

- **Loot → satchel.** Slain creatures drop items; press `E` to pocket them, then open the **Inventory
  & paperdoll** (`T`) — a PAGE-99-style layout with Held / Body / Adornment rails and container packs.
  **Equip** weapons and armour for their bonuses, or **Salvage** items into materials.
- **Real item icons and stats** — every item renders its real AC icon (never an emoji), with wield
  requirements, item-spells, Spellcraft, item mana, and resolved retail spells shown on tooltips and
  shop rows. Loot carries **item-spells, banes, and wield/level/skill requirements**, drawn from a
  server-side mirror of the retail item and spell catalog.
- **Tinkering & the combine engine** — spend salvage to **tinker** permanent bonuses onto gear (per-item
  caps, imbue-once), and craft via a **~1,500-recipe combine/tradeskill engine**.
- **Aetheria** — slotted aetheria with levels, sets, and combat **surges**.
- **Augmentations & Enlightenment** — permanent attribute/skill augmentations (capped), and endgame
  **Enlightenment** gates (level 275 + Society Master + auras) that reset you for further growth.
- **Attribute gems** raise a core attribute (capped at its natural peak), and **buff elixirs** (Might,
  Swiftness) give temporary combat boosts shown in the status panel.
- **Gathering** — ore veins (⛏) and herb patches (🌿) dot the wilds; press `E` to harvest tinkering
  materials and the occasional healing herb. Nodes replenish over time.

### Vendors, quests & the Kilmer Saga

- **Vendors in every town** — a **Provisioner** (potions, elixirs, attribute respec), a **Weaponsmith**,
  an **Armorer**, and a **Scrivener** (spell scrolls, tapers, and the universal Prismatic Taper).
  Capitals host a full market of all four. They buy your spare loot, and shop rows show wield gates,
  cantrip tier, and spellcraft so you never buy blind.
- **Quests** — **Asheron's Emissary** and other givers hand out rotating bounties (slay specific
  creatures, gather resources, clear a delve, fell the world boss), turned in for XP + pyreals, with
  retail-tiered daily/weekly lockouts. Starter quests are aligned to retail.
- **The Kilmer Saga** — the full **10-year live event storyline** (the Year of Bone through the Year of
  the Crown) runs on its own schedule, alongside instanced events and a **Colosseum**.
- **Books & lore** — placed books and library stands, *"A History of Dereth,"* and heritage lore shown
  at character creation.

### Social, allegiance & PK (online)

- **Fellowships** — parties of up to 9 with **proportional shared XP**; nearby members share a kill
  even without landing a blow.
- **Allegiance & monarchy** — swear to a patron for an XP bonus, take vassals (including NPC vassals),
  pass XP up the pyramid, and build a monarchy, with `/allegiance` chat.
- **Society** — join a society, earn **ranks and ribbons**, and run Society Test quests.
- **Player-vs-Player** — a 3-state PK system with altars, PK-loot, and an authentic **3-day PK key**.
- **Secure trade** — face-to-face item-and-coin trading with confirmation.

### Housing

Own a **dwelling** in Dereth — cottages, villas, and manors placed at real house locations. Buy or claim
a home, decorate it with hand-placed **hooks** for furniture and trophies, set access (open / allegiance
/ guest list), and manage guests (including booting them). Housing persists with your character.

### Ships

**Ownable, boardable ships** — a **skiff, cog, and caravel** — let you pilot across Dereth's waters
Ultima-style, boarding and steering the vessel over the sea.

### Dungeons (delves)

**50 dungeon entrances** are scattered across Dereth, each a distinct themed delve — **Ice Caverns, Lava
Forges, Fungal Grottos, Olthoi Hives, Forgotten Crypts, Crystal Mines, Sand Tombs, Shadow Sanctums,
Banderling Warrens, Frozen Tombs, Overgrown Ruins, The Deep** — each with its own floor/wall palette,
glowing props, guardian roster, and an elite **Warden**. Many render **real AC EnvCell meshes** and span
**multiple y-aware storeys** with descend portals. Entrances glow in their theme's colour and are named
(e.g. *Wailing Lava Forge*).

Each delve has a **tier (1–5)**: deeper, farther dungeons field more and tougher guardians and pay out
far richer hoards. Slay every guardian (the Warden included) to unseal the **treasure hoard**, plunder
it (`E`), then leave through the archway. Die, and your spirit returns to your bound Lifestone. A guided
**Training Academy** tutorial dungeon (Great Hall / Courtyard / Workshop) teaches the ropes. All 50 show
on the full map.

### The world — the real Dereth

The map mirrors the actual continent of **Dereth**, with towns at their **real in-game coordinates**
across the three cultures and the wild centre, and **named regions** that surface on the HUD and via
`/where`:

- **Aluvia** (temperate green, north) — capital **Holtburg**. Towns: Cragstone, Glenden Wood, Arwic,
  Eastham, Rithwic, Lytelthorpe, Tou-Tou.
- **the Sho Lands** (forest, east/southeast) — capital **Shoushi**. Towns: Yanshi, Sawato, Nanto,
  Baishi, Lin, Mayoi, Hebian-To.
- **Gharu'ndim** (desert, south/southwest) — capital **Yaraq**. Towns: Samsur, Zaikhal, Al-Arqas, Uziz,
  Khayyaban, Qalaba'r, Al-Jalima.
- **The Direlands** (barren centre) — no towns, the deadliest spawns and the highest-tier delves.

**24 towns** in all, each with a bindable **Lifestone** and a cluster of large, **culture-styled
buildings** streamed from **real AC building meshes** — Aluvian timber-frame houses, Gharu'ndim domed
sandstone halls, Sho tiered pagodas — around a paved **plaza** with a culture feature (Aluvian well,
Gharu'ndim fountain, Sho torii gate) and market stalls. **Capitals are walled and marked ★.** The ground
is biome-tinted, and danger rises with distance from any capital (the Direlands are lethal).

**Terrain.** Dereth is contoured — the land rolls with hills and valleys, rises into snow-capped mountain
ranges (tallest in the central Direlands), dips into lakes, and is ringed by ocean at the map's edge with
sandy shores. Towns rest on flattened pads above the water; you can't wade into deep ocean, and you can
**drown**. Everything — you, creatures, buildings, trees — sits on the contoured surface.

**Getting around.** **Cobblestone highways** link the three capitals, following the contours of the land
(with signposts and a +50% speed bonus); smaller towns have internal streets. Every town holds a **Town
Network portal** (teal arch by the Lifestone): press `E` to enter an instanced hub ringed by a **portal to
every town in Dereth**, or open the map (`Tab`) and **click any town to fast-travel**. Level-gated
overworld portals reach frontier zones. Travelling through any portal kicks up a swirl of arcane motes and
a whoosh. A **compass** strip across the HUD and a circular **radar** keep you oriented; other players,
party members, and world bosses show on the minimap and world map online.

### Character progression & achievements

- **Skills** — spend **skill credits** to train (or specialize) weapon, magic, and trade skills on the
  character sheet (`C`), then pour **XP** into raising their ranks; once you reach the level gate you may
  swear an **Allegiance** for bonus XP.
- **Achievements & titles** — earn milestone achievements (First Blood, Slayer, Giant Killer, Delver,
  Queenslayer, Pyreal Magnate, Archmage…), each granting a **title** shown under your name; **873 retail
  titles** are available. The full list lives in the character sheet.
- **Auto-save** — your character (attributes, level, XP, gold, gear, materials, bound Lifestone,
  position, quest progress, housing, achievements) saves automatically: to the **server** when online,
  to the **browser** offline. Offline, the title screen offers **Continue Your Saga** or **Begin Anew**;
  online, your characters wait on the character-select screen.

### Pastimes

- **Chess** — a full, perft-validated chess engine with an AI opponent, playable in the world.

## Multiplayer — the Dereth MMO

Online, Dereth is a **shared, server-authoritative world**. The server owns the truth so the world stays
consistent and can't be faked by a client:

- **Accounts & characters** — one account holds **up to 8 characters**, each with its own name, heritage,
  and save. Characters persist server-side (SQLite), and you log back in **right where you left off**.
- **Shared world** — monsters, the five world bosses, ground loot, and **Incursions** (timed horde events
  that besiege a town with a beacon) are simulated on the server and shared by everyone. Monsters cluster
  near the real towns; capital cores stay safe.
- **Cooperative loot & XP** — kills drop **first-come ground loot** anyone can grab; XP is shared among
  everyone who fought a monster, and nearby fellowship members share the kill.
- **Social** — in-game **chat** (`Enter`), **`/who`**, private whispers (`/tell`, `/r`), **parties** of
  up to 9 (`/party …`, party chat `/p`), **allegiances**, **secure trade**, **housing**, and **emotes**.

### Running a server

The server is pure **Python 3 standard library** — no pip installs, no Node. With Python 3 present:

```
python3 server/dereth_server.py        # listens on 0.0.0.0:8787
```

Environment overrides: `DERETH_HOST` (default `0.0.0.0`), `DERETH_PORT` (`8787`), `DERETH_DB`
(`server/dereth.db` — use a persistent volume in the cloud). Serve `index.html` from the same host (any
static server) and it connects automatically.

Verify a running server with the bundled end-to-end harness (asserts auth, session tokens, character
persistence, chat relay, presence, and world snapshots; exits non-zero on failure):

```
python3 server/test_client.py [host] [port]
```

Additional Python harnesses cover persistence, fellowship, fuzzing, and soak testing
(`server/tsa_*.py`). `deploy/` has everything to host on a small cloud box (DigitalOcean / Ubuntu
24.04): a hardened **systemd** service, an **nginx** site that serves the client and proxies the
WebSocket, a **Dockerfile**, an `update.sh`, and a step-by-step `DEPLOY.md` runbook (TLS via certbot,
firewall, backups).

### Files

- `index.html` — the entire game client.
- `three.min.js` — the 3D engine (bundled locally for offline use).
- `Play Dereth.command` — double-click launcher (offline solo).
- `assets/` — extracted AC data baked for the client (icons, models, spells, clothing, layouts…).
- `server/` — the authoritative MMO server (`dereth_server.py`) + test harnesses.
- `deploy/` — cloud deployment artifacts (systemd unit, nginx config, Dockerfile, `DEPLOY.md`, `update.sh`).
- `tools/` — extraction tooling that pulls art and data from the real AC client `.dat` files.
- `docs/` — design references and the authoritative remaining-work tracker.

## What it captures from Asheron's Call

- **Classless, attribute-driven progression** — the six AC attributes; you are what you train.
- **The two-currency skill system** — skill credits train/specialize skills; XP raises their ranks along
  the authentic AC cost curves, with specialization's +5 bonus, cheaper ranks, and higher ceiling.
- **Vitals derived from attributes** — Health, Stamina, Mana, AC-style, plus direct XP investment.
- **The eight schools of magic** — War, Life, Creature/Item Enchantment, recall spells, tapers and mana
  stones, cantrips and aptitudes.
- **Lifestone respawn & Vitae** — bind to a Lifestone; death returns you there with a Vitae penalty and
  a lootable corpse, just like Dereth.
- **The monsters, towns, and geography of Dereth** — real coordinates, real models, real regions.
- **Loot & XP economy** — Pyreals, item-spells, tinkering, salvage, and an XP→skill loop.
- **A shared world** — the online mode brings AC's defining feature: many players in one persistent
  world, fighting shared monsters and bosses, grouping into fellowships, swearing allegiance, owning
  homes, and chatting across Dereth.

## Scope note

The original Asheron's Call was a massive 3D MMORPG built by a studio over years. This is a faithful
**homage** to its signature systems — now both a complete **offline solo** game and a **shared-world
MMO** with its own small authoritative server, drawing on genuine extracted AC art and data. It is not a
network-accurate recreation of the full world of Dereth. The client is one self-contained file
(`index.html`) and the server one dependency-free Python file (`server/dereth_server.py`) — both
readable, tweakable, and extensible.

The client keeps player **health/respawn** on the client side; the server owns the shared,
cheat-sensitive truth (monster positions/HP, combat resolution, loot, XP, world events, trade, and
persistence).
