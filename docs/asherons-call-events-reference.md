# Asheron's Call — Monthly Events Reference & Planning Framework

Research reference for replicating AC's signature **monthly live-event model** in Dereth.
Companion to `asherons-call-world-reference.md`. Read this before designing any timed/seasonal event.

---

## 1. What made AC's model special

Asheron's Call (1999–2017) pioneered **free monthly content updates** that *advanced a continuous story*.
Each month, every subscriber got a patch ("event") that added quests, dungeons, monsters, items, and
sometimes whole landmasses — and pushed the world's narrative forward. Over **100+ monthly patches** shipped.

Key properties to imitate:

- **Continuous serialized story.** Months chain into **year-long story arcs**. Each arc has a villain, a
  build-up, a climax, and consequences that persist into the next arc.
- **The world changes.** Events were not just "new dungeon added" — seasons blanketed the land in snow,
  towns fell under siege, portals to new islands tore open, NPCs died or changed.
- **Curated, not just automated.** The live team role-played NPCs and ran the climaxes like tabletop GMs.
- **Town Criers are the newspaper.** Almost every town has a Crier who broadcasts the current rumor/clue.
  The story is *discovered* through them, library notes, and NPC dialogue — not a quest marker.
- **Limited, prestigious rewards.** Climaxes dropped **one-per-server uniques** and **titles** that proved
  you were there. Defeating a story boss was often a **one-time, world-first** moment.

> Canonical template — **Sudden Season / "The Great Work" (Dec 1999, the very first event):**
> 1. Town Criers in every town announce Sir Joffre Tremblant's expedition to a fabled mountain city is **overdue**.
> 2. Clue notes are seeded in the **libraries** of Cragstone, Hebian-To, Zaikhal.
> 3. Rumors lead adventurers to the hidden ice-city of **Frore**.
> 4. They fight through the **Gelidite** necromancers to a climax: a huge **sentient gemstone boss** + the
>    undead **Sir Joffre**, who drops **Tremblant's Ivory Staff** (one-per-server).
> 5. The world itself turns to **winter** (snow blanket, new ice dungeons) — a seasonal change all players feel.

This 5-beat shape — **Rumor → Clue trail → Destination → Climax boss → Unique reward + world change** —
is the reusable skeleton of nearly every AC event.

---

## 2. The seasonal wheel (Empyrean calendar)

AC's world cycled through seasons that events leaned on. Months (Empyrean calendar) include
**Frostfell** (deep winter), **Snowreap** (late winter), **Morningthaw** (thaw/early spring),
**Thistledown** (spring bloom), **Verdantine/Solclaim** (summer growth), **Leaffall**, and
**Harvestgain** (autumn/harvest). The felt cycle:

| Season | World dressing | Event flavor |
|---|---|---|
| Frost (winter) | snow blanket, brown grass, ice caves | Frostfell festival, ice-city sieges, undead |
| Thaw (spring) | new flowers, lush growth, festivals | renewal quests, Thistledown festival |
| Growth (summer) | green, long days | invasions, exploration, new lands |
| Harvest (autumn) | autumn colours, harvest festivals | Harvestgain festival, arc climaxes (many arcs ended in Harvestgain) |

Recurring **seasonal/holiday events** (run every year, not story-advancing): Frostfell (winter),
Thistledown (spring), Harvestgain, plus festival quests tied to **Festival Stones** placed in the world.

---

## 3. Taxonomy of event types

Plan future Dereth events by mixing these archetypes:

1. **Story-arc beat** — one month's chapter: new rumor, a quest or two, a mid-tier boss, a small world change.
2. **Invasion / Incursion** — a faction besieges towns; timed waves; repel for a shared reward. *(We have this.)*
3. **World-boss climax** — the arc's villain becomes killable; often **one-time world-first**, drops uniques.
4. **Seasonal/world-changing** — snow/thaw/harvest reskin + season-only dungeons/recipes; runs annually.
5. **Holiday festival** — Frostfell/Thistledown/Harvestgain; light fun, cosmetic/consumable rewards, festival stones.
6. **Item introduction** — a patch that seeds a new weapon/armor/material line via a quest (e.g. shields → a shield arc).
7. **Exploration / new land** — portals tear open to a new island/dungeon network (Aerlinthe, the ice-city of Frore).

### The 8 canonical story arcs (villain inspiration)
1. **Bael'Zharon's return** — the Hopeslayer freed from his crystal; the Shadows invade. *(We already field Bael'Zharon + Shadow Generals as bosses.)*
2. **The Virindi Schism** — a faction war inside Virindi society.
3. **Gaerlan** — a trapped Empyrean accidentally released.
4. **The Ancient Olthoi Queen's revenge** on Asheron. *(We field the Olthoi Queen / Gnawvil boss.)*
5. **Falatacot magic & the Burun invasion.**
6. **The Viamontian invasion** → release of Grael (tied to the *Throne of Destiny* expansion).
7. **Virindi Aerbax's plot** to take over Dereth. *(We already have "Aerbax's Prodigal …" quests.)*
8. **T'thuun the Slithis** awakening — the final year-long single-plot arc.

---

## 4. What Dereth already has (the building blocks)

We are well-positioned — most event primitives exist in `index.html`:

- **Live Incursions** — `EVENT_TYPES` (Shadow Incursion / Olthoi Swarm / Undead Rising / Banderling Raid),
  `startEvent`/`endEvent`/`eventSuccess`/`eventFail`, a beacon/beam at a town anchor, timed waves of
  boosted mobs, shared bounty. Server-authoritative online (`start_event`/`step_events` in `dereth_server.py`).
- **Shared world bosses** — `BOSS_DEFS`: Olthoi Queen (Gnawvil), **Bael'Zharon** (apex, ~9000 hp), 3 tinted
  **Shadow Generals**. Generic client rendering + slayer broadcasts + respawn announcements.
- **Town Criers** — NPC role `crier` with a `RUMORS` table; already the natural "newspaper" delivery vector.
- **Quests** — 23 authentic AC bounties, Emissary-brokered, multi-objective, Quest Log (J), item rewards.
- **Dungeons** — 89 canonical delves, multi-room layouts, darkness + carried light.
- **Portals** — town network + facility hub + foundry/residential; data-driven, easy to add event portals.
- **Day/night** lighting (no seasons yet).

### Gaps to build for a true monthly cadence
- **A scheduled event calendar** (month/season clock → which event is "live"), vs the current random Incursion timer.
- **Narrative delivery chain**: Crier rumor → clue note (library/NPC) → destination → climax. (Criers exist; need a
  rotating *current-event* rumor and clue items.)
- **World-state changes**: a season reskin (snow/autumn tint, grass colour, fog), a town "under siege" state.
- **Prestige rewards**: titles + limited uniques tied to event completion (we have items/achievements to extend).
- **Arc state**: a small saved "arc progress" so months chain and consequences persist.

---

## 5. Proposed framework for Dereth monthly events

A single **data-driven event calendar** drives everything. Sketch:

```
const EVENT_CALENDAR = [
  { id, month, season,            // when it's "live" (real-time month or an in-game clock)
    arc, chapter,                 // which story arc + beat number
    name, crierRumor,             // the headline Town Criers broadcast while live
    clue,                         // where the trail leads (town/dungeon/library note text)
    kind: "incursion"|"boss"|"seasonal"|"festival"|"item"|"explore",
    incursion?: EVENT_TYPES ref,  // reuse startEvent()
    boss?: BOSS_DEFS ref,         // reuse the shared-boss path
    worldState?: { season, siegeTown, portalTo },  // reskin / siege / new-land portal
    reward: { xp, gold, item?, title? },           // prestige reward on completion
  }, ...
];
```

Each live event:
1. **Criers** swap their top line to `crierRumor` (story discovery).
2. The **clue** points to a destination (existing town/dungeon, or a new event portal).
3. The **climax** reuses `startEvent()` (incursion) or the `BOSS_DEFS` path (world boss).
4. On success → grant `reward` (xp/gold/unique item/**title**) + apply any persistent `worldState`.
5. Advance `arc/chapter`; save arc progress so next month continues the thread.

**Cadence options:** tie "month" to the real calendar (one event live per real month) *or* to an in-game
season clock that rotates faster (e.g., a season every N days) so solo offline players still see the cycle.

### A sample 12-month arc we could build — *"The Fifth Sending of Darkness"*
Reuses our existing bosses/mobs/dungeons; each month is one shippable beat:

1. **Frostfell — Sudden Frost** (seasonal): snow reskin + Criers warn an expedition is overdue → ice-cave delve, undead climax, unique staff. *(our template event)*
2. **Snowreap — Whispers in the Cold**: Shadow Incursion intensifies on the nearest capital.
3. **Morningthaw — The Thaw**: world greens; a renewal festival; Thistledown-style fun quest.
4. **Solclaim — Hive Stirrings**: Olthoi Swarm incursions ramp; clues hint the Queen wakes.
5. **Verdantine — The Broken Seal**: a new event portal to a ruined island delve (exploration beat).
6. **Thistledown — Festival** (holiday): festival stone, cosmetic/consumable rewards.
7. **Leaffall — The Hopeslayer Stirs**: Bael'Zharon's heralds appear; Shadow Generals roam.
8. **Harvestgain — Reign of Shadow** (climax): **Bael'Zharon** becomes the live world boss; title "Hopeslayer's Bane".
9. **Frostfell — Aftermath**: world recovers; consequences (a town rebuilt/changed); new vendor goods.
10–12. Seed the next villain (Aerbax/Virindi) via Crier rumors → roll into the next arc.

---

## 6. Sources
- [Story Arcs — AC Community Wiki](https://asheron.fandom.com/wiki/Story_Arcs)
- [Events](https://asheron.fandom.com/wiki/Events) · [Live Events](https://asheron.fandom.com/wiki/Live_Events) · [Quests by Event](https://asheron.fandom.com/wiki/Quests_by_Event)
- [Sudden Season (1999/12) announcement](https://asheron.fandom.com/wiki/Announcements_-_1999/12_-_Sudden_Season) · [The Great Work Crystal](https://asheron.fandom.com/wiki/The_Great_Work_Crystal)
- [Town Crier](https://asheron.fandom.com/wiki/Town_Crier) · [Seasonal Quests and Events](https://asheron.fandom.com/wiki/Seasonal_Quests_and_Events) · [Festival Stone](https://asheron.fandom.com/wiki/Festival_Stone)
- [Fourth Sending of Darkness](https://asheron.fandom.com/wiki/Fourth_Sending_of_Darkness) · [Aerbax](https://asheron.fandom.com/wiki/Aerbax) · [Shadow Lore](https://asheron.fandom.com/wiki/Shadow_Lore)
- [Asheron's Call — Wikipedia](https://en.wikipedia.org/wiki/Asheron's_Call) · [Massively OP: Shard of the Herald retrospective](https://massivelyop.com/2018/01/30/re-examining-the-significance-of-asherons-calls-shard-of-the-herald-event-on-its-first-deathiversary/)
