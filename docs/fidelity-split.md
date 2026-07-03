# AC-Fidelity Final Pass — Two-Client Work Split

**Context (2026-07-03).** A four-domain audit + code verification found the game is already
**~95% faithful to retail Asheron's Call** — the earlier gap docs were stale. Most audited
"gaps" are in fact already implemented (Trade Notes/MMD, PvP flag + combat, the attack-vs-defense
to-hit contest, monster physical mitigation, power/accuracy bar, attack heights, crits, sneak,
dual-wield, full recall suite, real skill/XP/vendor/spawn/spell data). Two fidelity increments
already shipped to `main`: **PR #22** (per-hit damage variance, twin moons, taller lifestone,
serif UI) and **PR #23** (AC concentric casting rings + additive projectile glow).

This doc divides the **genuinely-remaining** fidelity items between two concurrent Claude sessions,
**balanced by level of effort** (~19 effort-points total, ~9–10 each).

## Effort ledger

| Item | Effort | Primary code region |
|------|:------:|---------------------|
| Allegiance player-pyramid + monarchy + XP pass-up | 6 | `server/dereth_server.py` + allegiance UI panel + kill-XP award |
| Monster signature attacks | 5 | monster AI loop (`updateMonster`, ~line 12290) + `BESTIARY` |
| Authentic vitals model | 3 | `derive()` (~801) + character sheet UI + save migration |
| Corpse + item-loss on death | 3 | `die()` / death flow + a lootable-corpse entity |
| Fellowship cap-9 + XP-split modes | 2 | party code (`server/dereth_server.py`) + party UI + kill-XP split |

---

## List A — "Allegiance & Death"  → **other client**  (effort ≈ 9)

### A1. Allegiance player-pyramid + monarchy + XP pass-up  (effort 6)
Replace the solo `player.patron` boolean + NPC-vassal `+15% XP` with AC's real structure:
- A **patron/vassal graph** (swear to an equal-or-higher character; Monarch at the top of the tree).
- **XP pass-up** is *extra* free XP driven by vassal **Loyalty** (Generated%) and patron **Leadership**
  (Received%): `Passup% = Generated% · Received% / 100` (see `docs/asherons-call-mechanics-reference.md`
  for the exact BLoy/BLdr/RT/IG formula; direct pass-up min 25% / max ~90%).
- **Allegiance chat channel** (`/a`), MOTD, per-race gendered rank titles (tables already in the
  mechanics-reference doc). Allegiance-rank ladder + min-followers-per-rank.
- Server-authoritative in the online world; a solo-world fallback that still shows the tree/titles.

### A2. Corpse + item-loss on death  (effort 3)
Replace "lose 10% unspent XP + Vitae" with AC's iconic risk mechanic:
- On death, spawn a **lootable corpse** holding `ceil(level/10)` items (min 1; L10+ adds a 1/3 chance
  of +0/+1/+2 more). Drop **highest-value first**; starting wand + clothing exempt; trade notes exempt.
- Lose **½ your pyreals**. Corpse **decays** after `5 min/level` (min 1 hr); recover by walking to it.
- **Vitae** should also reduce **all skills** (not just vitals — the hook at line ~741 already exists;
  confirm it's applied everywhere). Res invulnerability ~1 min; restore to 75% of new max.
- **Recommend a difficulty toggle** (authentic item-loss ON/OFF) — item-loss is divisive; default
  can stay soft, with an "AC-authentic death" option that enables full corpse looting.

---

## List B — "Creatures & Character"  → **this session (in progress)**  (effort ≈ 10)

### B1. Monster signature attacks  (effort 5)
The AI (`updateMonster`, ~12290) currently does a generic melee lunge or a per-kind element bolt with
a curse. Add each family's *signature* move, each with a wind-up tell → effect → cooldown → VFX:
- **Tusker / Auroch charge-rush** (a telegraphed melee dash that knocks back + hits hard; the "gore" clip exists).
- **Lugian / golem boulder throw** (a lobbed arcing rock — ranged attack for otherwise-melee giants).
- **Olthoi acid-spray cone** + **web-root** (short-range acid burst; a snare that slows/roots).
- **Undead (skeleton/mummy/zombie) level-drain** (a hit that briefly saps a vital/attribute).
- **Armoredillo spin** (the "devastating spin" already flavored in `BESTIARY`).

### B2. Authentic vitals model  (effort 3)
`derive()` uses `mhp=60+End*2+level*6` etc. Move to AC's model: **Health = End/2, Stamina = End,
Mana = Self** as bases, plus a **separately XP-raisable vital track** (Health/Stamina/Mana as secondary
attributes on the cheaper 73-XP-pt-1 curve). Add vital-raising to the character sheet + save migration.
**Balance-verify in preview** — the game's monsters are retail-statted, so retail-scaled vitals should
hold, but confirm survivability before shipping.

### B3. Fellowship cap-9 + XP-split modes  (effort 2)
Raise `PARTY_MAX` 6→9. Implement AC XP share by level spread from founder: within 5 levels → **equal
split + bonus**; within 10 → **proportional**; all 50+ → **equal, no cap**. Share-bonus table
(2 → 75%+150% … 9 → 30%+270%, full-9 = 300% total). Optional non-XP-share mode.

---

## Coordination

- **Shared file:** both lists edit `index.html`; List A also edits `server/dereth_server.py` and
  List B's B3 touches it too. **`git pull` before every push; re-anchor edits if one fails.**
- **Hotspot:** the **kill-XP award path** is touched by A1 (allegiance pass-up) and B3 (fellowship
  split). Whoever lands second should re-read that hunk before editing.
- **Ship discipline (both):** jsc syntax check → preview `preview_eval` asserts → `preview_console_logs`
  error check → one commit per item → PR → merge → pull `main`. Small, frequent PRs merge cleanest.
- Out of scope / not in either list: the ~5 CC0 monster stand-ins and `client_highres.dat` textures
  (the extraction lane / blocked on the user supplying the dat).
