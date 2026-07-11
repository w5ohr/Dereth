# The Regalia of the Tenfold — grand quest chain design

*Approved by the operator 2026-07-11. One chapter per year of the Tenfold Crown saga
(docs/kilmer-saga-storyline.md). Each chapter awards one "of the Tenfold" piece —
the set that until now existed only on Kilmer's own character sheet
(server/admin_kilmer.json). Player pieces carry the same stats; the prestige is the
completed road, the look, and the Chapter X attunement — not power creep.*

**The premise.** Kilmer never had the set forged for himself — each piece was *earned* in
one of the ten years of his saga, quenched in that year's victory. At the gate of Castle
Val Halla he offers worthy adventurers the same road: *"The crown was never given. Walk
the ten years as I walked them, and the regalia will know you."*

**Starting it.** Whisper Kilmer (`/tell Kilmer regalia`) at the castle gate. Requires
level 40+ and the first Colosseum cleared — "prove the sand knows your name." Chapters
unlock strictly in order; state rides the quest journal + per-character flags (like the
Colosseum clear counters).

---

## The ten chapters

| # | Year / Arc | Piece | Signature mechanic | Climax |
|---|---|---|---|---|
| I | Embers — Crimson Remnant | **Sabatons** | Ember Field: standing still ignites you | Herald's Echo cast down |
| II | Whispers — Aerbax's Audit | **Gauntlets** | Doppelganger built from YOUR gear payload | Prodigal Simulacrum destroyed |
| III | Chitin — The Deep Brood | **Greaves** | Waxen slowing floors; burn 4 egg-clutches | Brood-Remnant Matron |
| IV | Banners — The Pretender's War | **Pauldrons** | Boarding actions; banner empowers the boss — tear it down mid-fight | Standard-Bearer duel |
| V | Blood — The Falatacot Root | **Shirt** | Blood-tithe: fought at half vitals; undercroft beneath the castle itself | Blood-Matriarch's revenant |
| VI | Storms — Gaerlan's Heirs | **Vambraces** | Full hostile weather; lightning hunts you on exposed peaks | Warden of the Four Winds (4 elemental phases) |
| VII | Bone — The Dericost Winter | **Breeches** | Stacking cold unless near brazier-light; relight 3 Hearths | Lich-King's Vertebra-Reliquary |
| VIII | Rifts — The Sundered Veil | **Aegis Cuirass** | Lone stand: no recall, no portal, no retreat — 3 waves | Rift stabilized from inside |
| IX | Tides — The Gathering Dark | **Diadem** | Fellowship-tuned (3-player; soloable high-level); rising flood | Herald's Shadow (reconstitutes twice) |
| X | The Crown — The Final Sending | **Attunement** | Requires all 9 pieces + Kilmer's Cape worn | T'thuun's Lingering Dream |

### Chapter flavor lines (Kilmer's presentation of each piece)
- I: "The first year taught me only this: keep moving. These boots remember."
- II: "A puppet's hands are moved. Yours, now, are your own."
- III: "I stood thigh-deep in that dark for a season. Let these carry you through yours."
- IV: "Shoulders are for standards. Bear the right one."
- V: "Worn nearest the heart, because that is where that year cut me."
- VI: "Raise your arms against the sky and mean it."
- VII: "Three seasons of winter, and I never once stopped walking."
- VIII: "An aegis is not armor. It is the decision not to move. The steel merely commemorates it."
- IX: "Ten points on this circlet. Not one of them stands alone."
- X: Kilmer bows to *you*.

## Chapter X — the Consecration
No new piece. Wearing all nine **and Kilmer's Cape** (the Advanced Colosseum capstone
stays the cape's only source — the Colosseum remains part of the road), stand with Kilmer
at the castle and face **T'thuun's Lingering Dream** in the undercroft's sealed heart.
Victory *attunes* the set for that character:
- the set gains its soft gold emissive glow (per-character flag),
- title **"Heir of the Tenfold"**,
- set bonus with all ten worn: +5% all elemental protections; the diadem's brow-gem
  glows at night,
- the in-game "A History of Dereth" book gains a final page naming the character.

## Systems notes (all within existing tech)
- Chapter state: quest journal + per-character flags (cf. `player.advColosseumClears`).
- Bosses: variants of existing creature kinds, one signature mechanic each.
- Pieces: the shipped tenfold meshes/textures/icons; stats mirror Kilmer's sheet.
- Grants: force-granted like Kilmer's Cape (`addToInv(it,true)`) — never dropped to sand.
- Kilmer's whisper-bot gains a `regalia` keyword reporting the asker's chapter
  in-character (bot lives in /home/dcochran/dereth-kilmer/, deployed separately).
- Chapters V, VIII, X reuse the dungeon/instance system; II uses the gear-payload
  doppelganger; IV uses ships; VI drives the weather systems.
- Levels ~40 → ~120 across the chain — a long-haul endgame pursuit.
- Ship incrementally: one issue/PR per chapter, each playable the day it merges.
