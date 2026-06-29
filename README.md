# Dereth — A First-Person Asheron's Call Homage

A playable **first-person 3D** action-RPG inspired by **Asheron's Call** (Turbine, 1999),
built on Three.js (bundled locally).

It now plays two ways:

- **Online — the Dereth MMO.** Register an account, roll **up to 8 characters**, and enter a
  **shared, server-authoritative world** alongside other players: shared monsters, world bosses,
  ground loot, Incursions, parties, and chat. The server is a **dependency-free Python 3 program**
  (stdlib only — see `server/`) that runs anywhere and deploys to a small cloud box (`deploy/`).
- **Offline — solo.** The original **zero-install, fully offline** single-player game still works
  unchanged: open `index.html`, pick a heritage, and play with a local browser save. No toolchain,
  no compilation, no Rosetta, no internet required at runtime.

## How to play

- **Double-click `Play Dereth.command`** (or open `index.html` in Safari/Chrome).
- **Play online (the MMO):** in the title screen's **Log In / Register** panel, create an account
  (account name + password), then you land on the **character-select screen** — up to **8 character
  slots**. Create a character (name + heritage) or **Play** an existing one to enter the shared world.
  *(Online requires a running server — see [Multiplayer](#multiplayer--the-dereth-mmo) below. The
  client auto-connects to the same host it's served from.)*
- **Play offline (solo):** below the online panel, choose a **heritage** — **Aluvian** (Holtburg),
  **Sho** (Shoushi), or **Gharu'ndim** (Yaraq) — to begin in that culture's capital (or **Wanderer**
  for a balanced start), or **Continue Your Saga** from a local save.
- Then **click the screen to capture the mouse**.
- Your heritage shapes how you look in first person: the **Aluvian** wields a steel **gauntlet & sword**
  (knight), the **Sho** a dark leather glove and **bow** (thief), and the **Gharu'ndim** a **robe sleeve**
  and **wand** that flares with the spell's colour when you cast (mage).
- Press **V** to toggle between **first person** and a **third-person view** where you see your full
  character — a textured body (steel armour / leather / robe, matching your heritage) that walks, swings,
  shoots, and casts, complete with gloved hands, boots, shoulder pauldrons, a **mage's cape** or a
  **thief's quiver**. The camera orbits **smoothly** behind you and **pulls in** when you back against a
  wall or hill so the view never clips through scenery.
- **Buildings are enterable.** The culture-styled houses, shops, and halls are now hollow shells with a
  doorway facing the plaza — walk through the door to step inside; the walls block you, the doorway lets
  you pass.
- Press **Esc** to release the mouse at any time.

### Controls

| Action | Keys |
|---|---|
| Move | `W A S D` or Arrow keys |
| Look | Mouse (click to capture) |
| Sprint | Hold `Shift` |
| Attack (active weapon) | Left-click |
| Swap sword / bow | `Q` |
| Flame Bolt / Frost Bolt | `1` / `2` |
| Heal Self / Stamina→Mana | `3` / `4` |
| Lightning / Flame Storm (AoE) | `5` / `6` |
| Blade Lure / Quickening (self-buffs) | `7` / `8` |
| Elixirs of Might / Swiftness | `9` / `0` |
| Bestiary codex | `B` |
| World map | `Tab` |
| Settings (sensitivity / volume / FOV) | `O` |
| Character sheet (spend points) | `C` |
| Inventory / tinkering | `T` |
| Loot / bind / portal / talk / delve / open chest | `E` |
| Chat (online) — open the chat bar | `Enter` (then type, `Esc` to cancel) |
| Mute sound | `M` |
| Pause | `P` |

Online chat also takes slash commands: **`/who`** (who's online), **`/tell <name> <msg>`** (or `/w`)
and **`/r`** to reply, **`/party invite|accept|leave|list`** + **`/p <msg>`** for party chat, and
**emotes** like `/wave`, `/cheer`, `/dance`, `/bow`, `/me <action>`.

### World & atmosphere

- **Day/night cycle** — the sun arcs across Dereth over a few minutes, warming to gold at dawn/dusk
  and giving way to a star-filled night sky with dark fog. Lighting, shadows, and the glow of
  Lifestones and portals shift with the hour.
- **Safe towns** — creatures cannot enter the haven around a town center, so you can breathe,
  tinker, and bind in peace.
- **Critical hits** — every strike (melee, arrow, or bolt) can crit for double damage, with a flash,
  a louder ring, and a screen shake. Crit chance scales with **Coordination**.
- **World bosses** — named, name-plated giants roam the wilds: *Gnawvil, the Olthoi Queen*, the apex
  terror **Bael'Zharon, the Hopeslayer**, and the three **Shadow Generals** (Ler Rhan, Black Ferah,
  Isin Dule). Felling one grants huge XP, a pile of pyreals, and rare loot, then it rises again a
  while later. *(Online these are **shared** — every player fights the same boss, and a global
  announcement heralds each spawn and slaying.)*
- **Vitae** — dying weakens you (a damage penalty shown in the status panel) that you recover by
  earning XP — Dereth's price for death.
- **Music & sound** — a soft, procedurally-generated ambient score plays under the action (toggle
  with `M`), alongside footsteps as you move, a low growl when a creature notices you, UI clicks, and
  synthesized combat/spell/level-up effects — all generated in-browser, no audio files.
- **Weather** — the sky cycles through clear, overcast, and rain. Storms bring falling rain, thick
  fog banks that cut your sight, and the hiss of rainfall.
- **Vendor & potions** — a shopkeeper in Holtburg sells Health, Greater Health, and Mana potions for
  pyreals (and buys your spare loot). Quaff them in the field with `R` / `G` / `F`; counts show next
  to the spell bar.
- **Buff elixirs** — the vendor also stocks the **Elixir of Might** (`7`, +50% damage for 30s) and
  **Elixir of Swiftness** (`8`, +40% move speed for 30s); active buffs and timers show in the status panel.
  The vendor also offers an **attribute respec** for 100 pyreals — refunds all your allocated points
  so you can rebuild your character.

### Quests, tinkering & saving

- **Asheron's Emissary** stands in Holtburg (`!` over her head). Press `E` to accept a bounty
  ("Cull 5 Drudge Skulkers…"), fulfil it in the field, then return to turn it in for XP + pyreals.
  Quests rotate through varied objectives — **slay** specific creatures, **gather** resources,
  **clear a delve**, and finally **slay the world boss**.
- **Loot → satchel.** Slain creatures drop items; press `E` to pocket them, then open
  **Inventory & Tinkering** with `T` to **Equip** an item for its bonus or **Salvage** it into
  materials. Spend materials to **tinker** permanent +damage / +armor onto your character — a nod
  to AC's tinkering craft.
- **Sound.** Ambient wind plus synthesized combat, spell, level-up, and portal effects (generated
  in-browser via WebAudio — no audio files, still fully offline).
- **Auto-save.** Your character (attributes, level, XP, gold, gear, materials, bound Lifestone,
  position, quest progress) saves automatically — to the **server** when playing online, or to the
  **browser** offline. Offline, the title screen offers **Continue Your Saga** or **Begin Anew**;
  online, your characters wait on the character-select screen.

### Foes & combat depth

- **The bestiary** spans authentic Dereth creatures — Drudges, Mosswarts, Reedsharks, Tuskers, Shadow
  Casters, Banderlings, Aurochs, Gromnies, Olthoi, plus **Lugians** (stone brutes), **Tumeroks**
  (spear tribals), and **Mattekars** — spawned in lore-appropriate lands (Tumeroks & Lugians haunt the
  Direlands; Gromnies the desert) — plus undead **Skeletons** and floating, masked **Virindi** in the
  deepest wilds.
  Every species is a **distinct, detailed model** (limbs, snouts, fins, tusks, mandibles, horns,
  wraith robes, back-spikes, tails) rather than a generic shape.
- **Champions** — out in the deep wilds, some creatures rise as glowing, named **Champions** with
  far more health, harder hits, and richer loot (guaranteed drops + triple pyreals).
- **Status effects** — Flame Bolt sets foes **ablaze** for damage over time; Frost Bolt **chills**
  them, slowing their advance; **Lightning** stuns. Every hit can **critically** strike for double damage.
- **Elemental affinities** — creatures **resist** or are **weak** to fire / ice / shock (e.g.,
  Reedsharks shrug off frost but fry to Lightning; Mosswarts burn easily). Watch your damage numbers
  and pick the right spell. Open the **Bestiary** (`B`) to review the creatures you've slain and
  their weaknesses — undiscovered foes stay a mystery until you face them.
- **Combat feel** — "weak!/resist" callouts on elemental hits, impact light flashes, a
  damage-direction indicator showing where you're being hit, monster lunges and death animations,
  ranged casters that telegraph a wind-up you can dodge, and a **kill-streak** XP bonus for chaining
  kills quickly.
- **Eight spells across four schools** — *War* (Flame Bolt, Frost Bolt, Lightning, Flame Storm),
  *Life* (Heal, Stamina→Mana), *Item Enchantment* (**Blade Lure**, +damage), and *Creature
  Enchantment* (**Quickening**, +speed).
- **Equipment & skills** — loot/buy and **equip weapons and armour** (`T` to manage your satchel);
  train five **skills** (Melee, Missile, War, Life, Defense) on the character sheet (`C`), and once
  you reach level 5 you may **swear an Allegiance** to a patron for +15% XP.
- **Settings** (`O`) — adjust mouse sensitivity, master volume, field of view, and **difficulty**
  (Adept / Hero / Legend, scaling enemy health & damage); saved automatically.
- **Achievements & titles** — earn milestone achievements (First Blood, Slayer, Giant Killer,
  Delver, Queenslayer, Pyreal Magnate, Veteran, Archmage…). Each grants a **title** shown under your
  name, and the full list lives in the character sheet (`C`). Saved with your character.
- **Compass** — a strip across the top of the HUD shows cardinal directions and live bearing markers
  to the towns, the mine, and the world boss, so you can navigate at a glance.
- **Gathering** — ore veins (⛏) and herb patches (🌿) dot the wilds. Press `E` to harvest tinkering
  **materials** (and the occasional healing herb), then spend them at the tinker bench. Nodes replenish
  over time.

### Dungeons (delves)

**50 dungeon entrances** are scattered across Dereth, each leading to a distinct themed delve drawn
from environments like **Ice Caverns, Lava Forges, Fungal Grottos, Olthoi Hives, Forgotten Crypts,
Crystal Mines, Sand Tombs, Shadow Sanctums, Banderling Warrens, Frozen Tombs, Overgrown Ruins, and
The Deep** — each with its own floor/wall palette, glowing props (ice spikes, lava pools, mushrooms,
crystals, braziers, torches), guardian roster, and elite **Warden**. Entrances glow in their theme's
colour and are named (e.g. *Wailing Lava Forge*).

Each delve has a **tier (1–5)** — deeper, farther dungeons field more and tougher guardians and pay
out far richer hoards. Slay every guardian (the Warden included) to unseal the **treasure hoard**,
press `E` to plunder it (rare loot, pyreals, materials, big XP), then leave through the archway. Die,
and your spirit returns to your bound Lifestone. All 50 show on the full map.

### The world — the real Dereth (3 cultures + the Direlands)

The map mirrors the actual continent of **Dereth**, with towns placed at their **real in-game
coordinates** across the three cultures and the wild center:

- **Aluvia** (temperate green, north) — capital **Holtburg** (your home). Towns: Cragstone, Glenden
  Wood, Arwic, Eastham, Rithwic, Lytelthorpe, Tou-Tou.
- **the Sho Lands** (forest, east/southeast) — capital **Shoushi**. Towns: Yanshi, Sawato, Nanto,
  Baishi, Lin, Mayoi, Hebian-To.
- **Gharu'ndim** (desert, south/southwest) — capital **Yaraq**. Towns: Samsur, Zaikhal, Al-Arqas,
  Uziz, Khayyaban, Qalaba'r, Al-Jalima.
- **The Direlands** (barren center) — no towns, the deadliest spawns and the highest-tier delves.

24 towns in all; each has a bindable **Lifestone** and a cluster of large, **culture-styled buildings**
laid out around the town centre — **Aluvian** timber-frame houses with steep gabled roofs and chimneys,
**Gharu'ndim** domed sandstone halls, and **Sho** tiered pagodas. Each town has a paved **plaza** with a
culture feature (an Aluvian well, a Gharu'ndim fountain, a Sho torii gate) and **market stalls**;
**capitals are walled with a gated entrance** and marked with ★. The ground
is **biome-tinted** by land, and danger rises with distance from any capital (the Direlands are lethal).
It's a **vast** world linked by the **Town Network** — open the map (`Tab`) and **click any town to
fast-travel** there.

### Terrain

Dereth is no longer flat — the land rolls with **hills and valleys**, rises into **jagged mountain
ranges** (snow-capped at the peaks, tallest in the central Direlands), dips into **lakes**, and is
ringed by **ocean** at the map's edge (sandy shores where land meets sea). Towns rest on flattened
pads above the water, and you can't wade into deep ocean. Everything — you, creatures, buildings,
trees — sits on the contoured surface.

### The Town Network

Every town holds a **Town Network portal** (teal arch by the Lifestone). Press `E` to step into the
**Town Network** — an instanced hub ringed by a **portal to every town in Dereth**. Walk to any
portal and press `E` to travel there, or take the **Leave** portal back to where you came from. It's
the in-world counterpart to the map's click-to-travel.

### Sky

A living sky: a glowing **sun** (with a soft **god-ray halo**) and a cratered **moon** ride the
day/night arc, **clouds** drift overhead and fade at dusk, and a denser, twinkling **starfield** fills
the night. At dawn and dusk the horizon glows **warm orange** and the sun reddens. The sea is
**reflective** (specular highlights with a sun-glint streak and a gentle swell). Lighting, fog, and sky
colour all shift slowly with the hour (a full day takes about 70 minutes).

Travelling through any portal (capital portal, map fast-travel, or the Town Network) kicks up a
**swirl of arcane motes and a whoosh**; the Town Network's portals carry **large, readable name labels**
across the hall.

### Roads

**Cobblestone highways link the three great cities** (the capitals — Holtburg, Shoushi, Yaraq) to
one another; smaller towns have only their own **internal streets**. The roads are laid as ribbons
that **follow the contours of the land** (no floating), with a procedural cobblestone texture, and
you travel **+50% faster** on one. **Signposts** line the highways — a wooden post and arrow
pointing to the nearer town, labelled with its name. The major roads show on both maps, and the
capitals are also linked by **portals**.

## Multiplayer — the Dereth MMO

Online, Dereth is a **shared, server-authoritative world**. The server owns the truth so the world
stays consistent and can't be faked by a client:

- **Accounts & characters** — one account (login) holds **up to 8 characters**, each with its own
  name, heritage, and save. Characters persist server-side (sqlite), and you log back in **right
  where you left off**.
- **Shared world** — monsters, the five world bosses, ground loot, and **Incursions** (timed horde
  events that besiege a town with a beacon) are all simulated on the server and shared by everyone.
  Monsters cluster near the real towns; capital cores stay safe.
- **Cooperative loot & XP** — kills drop **first-come ground loot** anyone can grab; XP is shared
  among everyone who fought a monster, and **party members nearby share the kill** even if they
  didn't strike it (fellowship).
- **Social** — in-game **chat** (`Enter`), **`/who`**, private **whispers** (`/tell`, `/r`),
  **parties** of up to 6 (`/party …`, party chat `/p`), and **emotes**. Other players and the world
  bosses show on the minimap and world map; party members are highlighted and listed in a party HUD.

### Running a server

The server is pure **Python 3 standard library** — no pip installs, no Node. With Python 3 present:

```
python3 server/dereth_server.py        # listens on 0.0.0.0:8787
```

Serve `index.html` from the same host (any static server) and it connects automatically. To verify
the server, run the bundled end-to-end harness while it's running:

```
python3 server/test_client.py
```

`deploy/` has everything to host it on a small cloud box (DigitalOcean / Ubuntu 24.04): a hardened
`systemd` service, an `nginx` site that serves the client and proxies the WebSocket, a step-by-step
`DEPLOY.md` runbook (TLS via certbot, firewall, backups), and an `update.sh`.

### Files

- `index.html` — the entire game client.
- `three.min.js` — the 3D engine (bundled locally for offline use).
- `Play Dereth.command` — double-click launcher (offline solo).
- `server/` — the authoritative MMO server (`dereth_server.py`) + e2e test harness (`test_client.py`).
- `deploy/` — cloud deployment artifacts (systemd unit, nginx config, `DEPLOY.md`, `update.sh`).

## What it captures from Asheron's Call

- **Classless, attribute-driven progression** — the six AC attributes (Strength,
  Endurance, Coordination, Quickness, Focus, Self). You are what you train.
- **Vitals derived from attributes** — Health, Stamina, Mana, AC-style.
- **War & Life Magic** — flame/frost bolts and self-heals, with mana costs and cooldowns.
- **Lifestone respawn** — bind to a Lifestone; death returns you there at the cost of
  a slice of unspent XP, just like Dereth.
- **The monsters of Dereth** — Drudges, Mosswarts, Reedsharks, Tuskers, Shadow Casters,
  and the dreaded Olthoi, scaled by distance from town.
- **Loot & XP economy** — Pyreals (gold), attribute-boosting drops, and an XP→skill-point
  leveling loop.
- **A shared world** — the online mode brings AC's defining feature: many players in one persistent
  world, fighting shared monsters and bosses, grouping into fellowships, and chatting across Dereth.

## Scope note

The original Asheron's Call was a massive 3D MMORPG built by a studio over years. This is a faithful
**homage** to its signature systems — now both a complete **offline solo** game and a **shared-world
MMO** with its own small authoritative server, not a network-accurate recreation of the full world of
Dereth. The client is one self-contained file (`index.html`) and the server one dependency-free
Python file (`server/dereth_server.py`) — both readable, tweakable, and extensible.

The client deliberately keeps player **health/respawn** on the client side; the server owns the
shared, cheat-sensitive truth (monster positions/HP, combat resolution, loot, XP, world events).
