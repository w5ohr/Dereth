# Asheron's Call — Verified Mechanics Reference

Authoritative numbers gathered 2026-07-01 from acpedia.org / asheron.fandom.com (via the
Drunkapedia `drunkenfell.com` and Levistras `acportalstorm.com` verbatim mirrors, since the
primaries block automated fetch). Figures reflect **end-of-retail AC** (post-Throne of Destiny /
Master of Design). Use this when *building* the gaps listed in
[asherons-call-authenticity-gaps.md](asherons-call-authenticity-gaps.md) so numbers are real, not guessed.
Confidence notes at each section; browser-verify anything marked speculative before hard-coding.

---

## Character progression

- **Attributes:** creation pool = **270** distributable (each floors at 10, so planners showing
  "330" include the 6×10 baseline). Innate cap **100**; **+50** from augmentation gems; ~**290**
  fully buffed ("Max Base"). Primary raise cost: point 1 = **110 XP**, point 100 ≈ 236,629 XP/pt
  (~3.26 M cumulative per attribute).
- **Vitals (secondary attributes):** Health = **End/2**, Stamina = **End** (1:1), Mana = **Self**
  (1:1). Separately XP-raisable on a *cheaper* track: point 1 = **73 XP**, point 100 ≈ 156,176 XP/pt
  (~2.15 M cumulative). Endurance also gives a stamina-regen bonus.
- **Skills:** ~37. Trained raises to **208**; Specialized raises to **226** (includes a **+10**
  inherent bonus) and is cheaper/pt (pt1 = 23 spec vs 58 trained). **Spec credit cap = 70.**
- **Skill credits:** **52** at creation + ~50 from level milestones (L2–46) + 1 at L32 + **2 quest**
  (Hunting Aun Ralirea ~L35, Chasing Oswald ~L90) + **2 luminance** (post-L200) ≈ **102** lifetime.
  Unused credits carry over. Redistributable via Gems of Forgetfulness (L25+).
- **Free-to-train skills** (usable untrained): Arcane Lore, Jump, Loyalty, Magic Defense, Run, Salvaging.
- **Specialize-via-augmentation-only:** the 4 tinkering skills + Salvaging.
- **Level cap = 275** (raised from original 126 by Throne of Destiny). **Total XP to L275 =
  191,226,310,247** (~191.2 B); L126 = 4.29 B; single level 275 alone costs 3.39 B. (The larger
  ~226 B figure sometimes cited = *lifetime* XP incl. maxing all attributes/skills/augs, not the level
  threshold.) Single XP pool: earned XP auto-advances level once totals cross thresholds AND sits in an
  "available/unassigned" pool you spend on attributes/skills/augs.
- **Max-out costs (per the client XP tables):** primary attribute +190 → 4.02 B each · secondary +196 →
  4.29 B each · trained skill +208 → 4.20 B · specialized skill +226 → 4.10 B.
- **Two-currency model:** spend *credits* to open a skill (Train → Specialize); spend *XP* to raise
  its rank. Same XP pool also raises attributes + vitals (all progression competes for one budget).
- **Creation gives 50 credits** to spend (the ~52 figure = 50 + 2 later quest credits). Unused credits
  carry into the game (since *The Iron Coast*).
- **Heritage authenticity nuance:** the *Master of Arms* event (Feb 2012) **normalized all heritages to
  identical starting skills**; afterward heritage is essentially cosmetic (look/lore/rank-titles +
  Viamontian blue skin), differing only by starting Weapon Masteries + a free aug (later universal too).
  A homage giving heritages distinct starting stats emulates **pre-2012** AC — valid, but not final-state.
- **Professions (templates, not classes):** Bow Hunter, Life Caster, War Mage, Wayfarer, Soldier,
  Swashbuckler + Custom. Deviating too far from a template → Custom, title "Adventurer". Respec via the
  Attribute/Skill Redistribution quest at L25.

### Skill table (attr formula · train/spec credits)
Melee: Heavy (Str+Coord)/3 6/6 · Light (Str+Coord)/3 4/4 · Finesse (Coord+Quick)/3 4/4 ·
Two Handed (Str+Coord)/3 8/8 · Dual Wield (Coord+Coord)/3 2/2 · Dirty Fighting 2/2 ·
Recklessness (Str+Quick)/3 4/2 · Shield (Str+Coord)/2 2/2.
Missile: Missile Weapons (Coord)/2 6/6 · Missile Defense (Quick+Coord)/5 6/4.
Magic: War (Focus+Self)/4 16/12 · Life 12/8 · Void 16/12 · Creature Ench 8/8 · Item Ench 8/8 ·
Mana Conversion (Focus+Self)/6 6/6.
Defense: Melee Defense (Quick+Coord)/3 10/10 · Magic Defense (Focus+Self)/7 Free/12.
Trade/Tinker: Armor Tink (Focus+End)/2 4/AUG · Item Tink (Focus+Coord)/2 2/AUG · Weapon Tink
(Focus+Str)/2 4/AUG · Magic Item Tink (Focus) 4/AUG · Salvaging Free/AUG · Alchemy 6/6 · Cooking 4/4 ·
Fletching 4/4 · Healing (Focus+Coord)/3 6/4 · Lockpick 6/4.
Lore/Utility: Arcane Lore (Focus)/3 Free/2 · Assess Creature 4/2 · Assess Person 2/2 · Deception 4/2 ·
Leadership 4/2 · Loyalty Free/2 · Sneak Attack (Coord+Quick)/3 4/2 · Summoning (End+Self)/3 8/4 ·
Jump (Str+Coord)/2 Free/4 · Run (Quick) Free/4.

---

## Combat

- **Power/accuracy bar:** slider trades attack **speed ↔ power**, scaling damage ~**50%–150%**;
  mid-bar gives an accuracy bonus. At 0 stamina the bar forces the lowest setting. Missile has an
  equivalent accuracy bar affecting hit chance (degrades with range).
- **Attack heights:** high / medium / low targeting zones vs armor coverage.
- **To-hit:** attack skill vs defense skill contest → misses/evades (defense primarily makes attacks
  *miss*, not just soften).
- **Damage types:** slashing, piercing, bludgeoning + fire, cold, acid, lightning (7). Each armor
  piece has a per-element modifier (% of base AL, cap ~2.0×).
- **Ratings (late era):** Damage Rating `(100+DR)/100` multiplier; Critical Damage Rating; Damage
  Reduction Rating; **Crushing Blow** (bigger crit mult); **Biting/Critical Strike** imbue (crit
  chance); **Sneak Attack = +20 DR from behind**.
- **Crits:** base **10%**, **×2**; imbues raise chance or multiplier.
- **Dual Wield:** grants off-hand extra swings.
- **Stamina:** swing cost from weapon burden + shield burden + power-bar; you also lose stamina when
  missed or hit in melee.
- **Burden/encumbrance:** Burden Units vs Strength-derived capacity. >100% burden degrades Run/Jump/
  Melee & Missile Defense; **at 200% you can't jump and drain stamina even walking** (the "drown when
  overloaded" coupling to no-swim).

---

## World & exploration

- **Scale:** island continent of **Dereth** on planet **Auberean**; **>1,300 km²**, fully seamless
  (no zones/loading on the surface). Landblock grid = **256×256 = 65,536** blocks; terrain heightfield
  2041×2041 vertices (~10 m spacing).
- **No swimming, no mounts** — travel = run + portals; deep water = sink/drown (tied to burden).
  **Fall damage** scales with drop height. Jump is a real skill (Coord/Str, penalized by encumbrance).
- **Regions:** **Osteth** (settled heartland, spawns ≤~L40) · **Direlands** (frontier, L80–100+;
  West Direlands 80+, Valley of Death 100+) · **Aphus Lassel** (tropical isle chain, free tuskers) ·
  **Marae Lassel** (Dark Majesty isle) · **Linvak Tukal** (Lugian fortress, L40+). Islands: Singularity
  Caul, Halaetan Isles, Aerlinthe, Vesayen Isles, Vissidal, Dark Isle. (Ispar = human *homeworld*, not
  in-game.)
- **Starter towns:** Aluvian → Holtburg/Lytelthorpe/Rithwic · Sho → Shoushi/Nanto/Yanshi · Gharu'ndim
  → Yaraq/Samsur/Al-Arqas. Currently selectable: Holtburg, Yaraq, Shoushi + **Sanamar** (ToD).
- **Portal hubs:** Town Network (portal to nearly every town, back-portal from each) · **Marketplace**
  (universal recall for every character) · **Facility Hub** (innate gem; East wing 10/15/20/25+, West
  30/35/40/45+, North side-quests + L15–40 hunting) · the "Subway"/Abandoned Mine (mostly gutted by the
  *Shifting Gears* event).
- **Level-gated portals:** e.g. Olthoi Chasm **Shallow 40+ / Deep 60+ / Cavernous 80+ / Abyssal 100+**;
  Crystalline Portals L70/L110; Eastwatch 80+, Timaru 35+.
- **Weather:** rain, snow, fog, clear (region-tied — snow north, arid desert). Day/night ≈ **2h07m** real.
- **Recall spells (Item Enchantment):** Lifestone Recall · Primary/Secondary Portal Recall (+ Primary/
  Secondary Portal Tie — only non-restricted portals tie) · Portal Recall (last portal) · Portal
  Sending · destination recalls (Aphus Lassel, Bur, Marketplace, Sanctuary). Typed: `/lifestone` (no
  penalty), `@`/`/` interchangeable.

---

## Items & economy

- **Loot mutation:** every drop rolls a **wield requirement** (skill/level threshold); tinker/imbue
  *raise* it. Magical loot mutates **item-spells** (cantrips Minor/Major/Epic/Legendary), **mana** pool
  + **mana cost**, **spellcraft**, **armor level**, per-element resists. 8 loot tiers.
- **Tinkering:** 8 tink skills (Weapon/Armor/Magic Item/Item + Salvaging etc.). **100 units = 1 salvage
  bag**; item tinkered **up to 10×**, **imbued once**; units/tinks capped by **workmanship 1–10**;
  per-attempt success formula; **failure can destroy the item**.
- **Rares:** separate table ~**1/2,500** eligible kills with a **pity timer** (up to ~2 months =
  guaranteed), **6 tiers**, tradeable via Rare Exchangers for **MMD**, fixed **50,000** value.
- **Trophies:** creature-part drops (fixed/random rate, rolled at spawn) for quest/turn-in use.
- **Currency:** Pyreal (coins, have burden) · Trade Notes (redeemable certs) · **MMD** (trade currency).
- **Inventory:** 102-slot main pack + separate side packs you open. Every item has an individual Burden.

### Aetheria (endgame gear; "From Darkness, Light")
- 3 colors = 3 slots, level-gated: **Blue (Lyr) L75+ · Yellow (Kor) L150+ · Red (Tem) L225+**.
- Bathe raw Aetheria in an Aetheria Mana Stone → binds a **sigil** (set bonus) + a **surge** (proc).
- **Sigils:** Growth (healing/DoT-reduction) · Defense (dmg reduction) · Fury (crit dmg/endurance) ·
  Destruction (damage rating) · Vigor (HSM + drain reduction). Set caps at **level 10** with **15+
  combined Aetheria levels** → up to **+10**.
- **Surges** (~10–20 s, no cooldown, same-type extends not stacks): Destruction +20 DR · Protection +20
  dmg-reduction · Regeneration heal ~150 · Affliction target −150 · Festering −healing.
- **Leveling:** XP poured in; Blue starts at 750 M XP; level N ≈ 2^(N-1) B total XP.

### Luminance (endgame currency; unlock via **Nalicana's Test**)
- Second advancement currency; **hold cap = 1,000,000 lps** (spend down to bank more). From luminance
  creatures/quests/gems (blank aug gems → 10,000 lps each at Nalicana).
- **Five rival Seers, align with ONE** — switching forfeits all spent luminance + that seer's augs:
  Nalicana (Yalaini, neutral main line at Asheron's Castle) · Shade of Lady Adja (Light Falatacot) ·
  Ka'hiri (Dark Falatacot) · Liam of Gelid (Dericost) · Lord Tyragar (Habraen).
- **Auras (permanent, mostly ≤5 levels):** Valor +1 DR/lvl · Glory +1 crit dmg/lvl · Protection +1
  dmg-reduction/lvl · Temperance +1 crit-dmg-reduction/lvl · Mana Flow/Infusion · **World +1 all
  skills/lvl (≤10)** · Craftsman +1 tinkering/lvl · **Skill +1 credit/lvl (≤2)** · Purity +1 healing/lvl
  · Aetheric Vision (+surge chance). Rival seers add Specialization (+2 spec skills/lvl), Destruction,
  Invulnerability, etc. (350k–550k lps/lvl). *(Aura magnitudes: high-but-not-verbatim confidence.)*

---

### Augmentations (Throne of Destiny; bought with raw XP via Augmentation Gems; survive Enlightenment)
- **Innate Attribute** (+5, 500 M XP, max **10 total** across all six → caps innate at 100):
  Reinforcement of the Lugians (Str), Bleeargh's Fortitude (End), Oswald's Enhancement (Coord),
  Siraluun's Blessing (Quick), Enduring Calm (Focus), Steadfast Will (Self).
- **Innate Resistance** (+10%, 2 B, max **2 total**): Mace/Blade/Arrow Turner + Storm's/Fiery/Icy/
  Caustic Enhancement.
- **Skill** (2 B, non-repeat): Master of the Steel Circle (all melee) / Five Fold Path (all magic) /
  Focused Eye (all missile); **Jack of All Trades** (+5 all skills, 4 B).
- **Ratings/health:** Asheron's Lesser Benediction (+10% base health, 2 B) · Innate Renewal (+100%
  regen lying down, 1 B ×2) · Critical Protection (25% of enemy crits → normal, 1 B) · Frenzy of the
  Slayer (+3 DR, 2 B) · Iron Skin of the Invincible (+3 dmg-reduction, 2 B) · Eye of the Remorseless
  (+1% crit, 4 B) · Hand of the Remorseless (+3 crit-dmg rating, 4 B).
- **Convenience:** Clutch of the Miser (−5 items dropped on death, 2 B ×3) · Enduring Enchantment (keep
  buffs on death, 4 B) · Might of the Seventh Mule (+20% burden, 1 B ×5) · Shadow of the Seventh Mule
  (8th pack slot, 4 B) · Infused War/Life/Item/Creature/Void Magic (no foci, 2 B each) · Quick Learner
  (+5% kill XP, 4 B) · Archmage's Endurance (+20% spell duration, 1 B ×5) · Ciandra's Fortune (+25%
  salvage, 1 B ×4) · Charmed Smith (+5% imbue, 2 B). No hard total cap — limited by XP + per-aug repeats.

### Enlightenment (Spring 2014 — the prestige-reset endgame)
- **Requirements:** level 275 · **Master rank in a Society** · all Luminance auras maxed *except* the 2
  skill-credit auras (~19 M lum) · 25 free pack spaces · enlightened **< 5 times**.
- **Lose:** all XP (→ level 1), Luminance + auras (except skill-credit auras), Aetheria access,
  Luminance-gain (re-earn at L200 via Nalicana's Test), level/skill-gated gear use.
- **Keep:** spellbook/buffs, vitae, PKL status, **all Augmentation-Gem augs**, luminance/quest skill
  credits, all quest flags + kill-task progress, society membership.
- **Gain (permanent, per enlightenment):** **+2 vitality, +1 to ALL skills**, an attribute-reset cert,
  a title. **Max 5**, titles: **Awakened → Enlightened → Illuminated → Transcended → Cosmic Conscious.**

## Social / allegiance / fellowship

### Allegiance (patron/vassal tree; Monarch at top)
- Swear via Allegiance Panel to equal-or-higher level (revised: any level, but **no pass-up until the
  lower patron's level catches up**). **Vassal cap = your character level.**
- **XP pass-up is EXTRA free XP** (not subtracted from the vassal), driven by vassal **Loyalty**
  (Generated%) and patron **Leadership** (Received%). Post-2004 direct formula:
  ```
  Generated% = 50 + 22.5·(BLoy/291)·(1 + (RT/730)·(IG/720))
  Received%  = 50 + 22.5·(BLdr/291)·(1 + V·(RT2/730)·(IG2/720))
  Passup%    = Generated% · Received% / 100
  ```
  BLoy/BLdr = buffed Loyalty/Leadership (cap 291); RT = real days sworn (cap 730); IG = in-game hours
  sworn (cap 720); V = vassal factor (1→0.25, 2→0.50, 3→0.75, 4+→1.00). **Direct pass-up min 25%,
  max ~90%.** Grand-vassal→grand-patron min 0%, max ~10% *(grand-vassal eqns flagged speculative)*.
  Switching patrons **resets** accrued time (the only decay).
- **Allegiance Rank** = one higher than the lower of your two highest-ranked vassals (or your highest,
  whichever is greater). Min followers per rank: 1→0, 2→2, 3→6, 4→14, 5→30, **6→62**, 7→126, 8→254,
  9→510, 10→1022. Per-race **gendered rank titles** (male, rank 1→10):
  - **Aluvian:** Yeoman, Baronet, Baron, Reeve, Thane, Ealdor, Duke, Aetheling, King, High King.
  - **Gharu'ndim:** Sayyid, Shayk, Maulan, Mu'allim, Naqib, Qadi, Mushir, Amir, Malik, Sultan.
  - **Sho:** Jinin, Jo-Chueh, Nan-Chueh, Shi-Chueh, Ta-Chueh, Kun-Chueh, Kou, Taikou, Ou, Koutei.
  - **Viamontian:** Squire, Banner, Baron, Viscount, Count, Marquis, Duke, Grand Duke, King, High King.
  - (The generic relationship terms are just vassal / patron / monarch / follower — *not* a
    Follower→Adherent→Vassal ladder.)
- **Monarch** powers: assign speaker, bans, lock allegiance, custom officer titles. **Allegiance chat
  channel** (added *Mirror, Mirror* Jan 2004) + broadcasts/MOTD/co-vassal chat (*Chains of Command*).
- **Housing tie:** Villa/Mansion owned by a Monarch = allegiance recall point (mansions preferred —
  villas can't use portal devices). Mansion needs **level 50 + Rank-6 Monarch**, lost if rank drops to 5.

### Fellowship
- Cap **9** (founder + 8), single session. XP share by spread from **founder**: all within **5 levels
  → equal split + bonus**; within **10 → proportional** (no bonus); all **50+ → equal, no cap**.
  Non-XP-share option exists.
- **Bonus (share = base + bonus each):** 2 members 75%+150% · 3 60%+180% · 9 30%+270% (**full 9 =
  300% total**, 3× solo). Beyond the window → drop equal→proportional→out.
- **Share range:** same landblock + same indoor/outdoor context (no published yardage). Luminance share
  range is much shorter than XP.

### Loyalty / Leadership skills
- **Loyalty** (vassal → Generated%): base Trained, train 0 / spec 2. Buff *Fealty*, debuff *Faithlessness*.
- **Leadership** (patron → Received%): base Untrained, train 4 / spec 2. Buff *Leadership Mastery*,
  debuff *Leadership Ineptitude*; enables Leadership Horns. Both lost `Self/4` inheritance + caps in
  *The Madness of Men* (2004).

---

## Death & titles

### Death
- Resurrect at **last-attuned lifestone**. Drop items = **ceil(level/10)** (min 1); at **L10+** an equal
  1/3 chance of **+0/+1/+2** more (e.g. L20 → 2/3/4 items). Lose **half your pyreals**.
- **Drop order:** highest-value item first; then items of an already-dropped *type* count at half value
  when ranking the next — spreads loss across categories. **Exempt:** trade notes, starting wand +
  clothing (not food/components).
- **Corpse** persists **5 min/level (min 1 hr)**; must be recovered. Consent: `@permit add <name>` /
  `@consent on` → up to **20** players loot simultaneously; **resets on logout**.
- **Res protection:** ~**1 min invulnerability** (broken by offensive action); vitals restore to **75%**
  of new max.
- **Vitae:** **+5%/death, cap 40%**, reduces Health/Stamina/Mana **AND all skills**. Burned off by
  **earning XP** (allegiance pass-up does NOT count; more deaths = more XP per 1%). **No XP is lost** on
  death — you just owe extra to clear vitae.

### Titles
- Cosmetic, many held, **one displayed**. Managed in the Titles Panel (added *A Change in Tactics*) —
  switch to any earned since the panel. Some one-per-char (Creation Title "Adventurer"), most 1:1 with a
  quest/kill-task/event. Title *count* is visible to others via ID/assess (if opted in); allegiance
  rank/title prefixes your name. AC has **no modern global `/who` roster** — ID panel + displayed title
  is how others see standing.

---

## PKing

- **States:** NPK (default, protected) · **PKL** (consensual, low stakes: death = no vitae, no item
  drop, killer gets no XP; reverts to NPK on death/logout/NPK-altar) · **PK** ("go red", full stakes,
  red radar dot).
- **Go red:** use an **Altar of Bael'Zharon** or **Eye of Darkness** (Whispering Blade / Rossu Morta
  chapterhouses) → become "Bael'Zharon's Chosen." **Drop PK:** Altar of Asheron (Trothyr's Rest) /
  Adja's Memorial (Ithaenc Cathedral ruins). **PK keys** (switch cooldown) = **3 days** (was a week,
  changed Sept 2013). *(There is NO "Crimson Star" altar — that name doesn't exist as a PK mechanic.)*
- **PK death loot:** only you or your killer can loot (even third-party consent is ignored). Below L35
  item count scales with level; **always lose 50% pyreals + ALL Rares**; **wielded items are NOT
  excluded**.
- **Darktide** = dedicated PvP world; everyone starts PK and **cannot switch to NPK**; server-specific
  quests + arenas.
- **Arenas:** two added in *Taste of Twilight* (one PK-fight, one with spectating). **Derethian Combat
  Arena** at Baishi — pay **2 MMD**, earn Marks of a Kill (reward at 20), or survive using the Statue of
  Death every 5 min.

---

## Quests & the live story

- **Flags + repeat timers** per character (server-side, persistent). Standard timer = **20 h** (most
  reduced to this Sept 2013). Exceptions: **Augmentations 1 week** · Black-Market/Neftet gem turn-ins
  **3 days** · PK keys **3 days**. Some flags permanent (reopen a portal forever); some portal-entry
  flags persist until completion (Gaerlan's Citadel). No auto timer UI — Quest Journal is manual notes.
- **Quest XP** = a **% of your level's XP-to-next, capped** (e.g. Lightbringer = 100% capped at 750,000
  + Aphus Lassel gem + title). *Learning From Experience* raised the % (not the cap) so quests stay
  relevant across levels.
- **Great quests:** **Lady Aerfalle** (Aerlinthe Isle, L40+, 3 difficulties) · **Sword of Lost Light**
  (7 keys → Halls of Lost Light → Guardian → sword; upgrades to Sword of Lost Hope via Blood of the
  Hopeslayer) · **Colosseum** (18 arena rooms, 1-hr timer, ticket = 5 MMD or a Rare; Advanced entrance
  L80+; Vault = Tier-7 loot + Empyrean Ring) · **Aun Tumerok / Lightbringer** (Facility Hub chain;
  Living Weapons at Palenqual's Cavern).
- **Live story arcs** (free monthly patches that permanently reshaped the shared world):
  **Arc 1 Bael'Zharon's Return** (Dec 1999–Dec 2000) · **Arc 2 Virindi Schism** (2001) ·
  **Arc 3 Gaerlan's Release** (Dark Majesty, 2001–2002) · **Arc 4 Olthoi Queen's Revenge** (2002–2003).
  Recurring cast: **Asheron** (guide) vs **Bael'Zharon**, Olthoi, Shadows, Gaerlan, Virindi. Landmark
  event: **Bael'Zharon's Rampage/Defeat** (Dec 2000) — allegiance/monarchy-driven; monarch **Blood**
  swore to Bael, Anti-PKs to Asheron. *(There is NO event named "Bael'Zharon's Hope.")*

---

## Society (Ancient Powers era; join at **level 180+**, one only)

- Three orders + strongholds: **Celestial Hand** → Hebian-to · **Eldrytch Web** → Cragstone ·
  **Radiant Blood** → Zaikhal (12.8N 0.7E). Members-only recall scroll each.
- **Ranks:** Initiate → Adept → Knight → Lord → Master. Advance by turning in cumulative **Commendation
  Ribbons** (Adept 95, Knight 295, Lord 595, Master 995) **+ a rank Test quest**. Ribbons from society
  quests, handed in (lots of 5) to the Commendations Officer; per-period turn-in caps 50/100/150/200
  (period reduced to ~3 days in *Reforging the Past*).
- **Rewards:** progressive Society Armor sets via Armor Writs (25 Trade Tokens = 1 writ, max 1/day;
  Trade Tokens from Master-rank 50-ribbon exchange), L8 loot-chest pulls, Master-only stipends +
  Tactical Defense Game.

## Housing

| Tier | Level | Extra req | Cost | Chests | Hooks | Upkeep |
|------|-------|-----------|------|--------|-------|--------|
| Apartment (Residential Quarters) | 20 | — | nominal | 1 | 3 (2 wall+1 floor) | every 3 mo |
| Cottage | 20 | — | 1 Writ + 300k + trophy | 1–2 | 50 (25 active) | 30k/mo |
| Villa | 35 | — | 5 Writs + 2M + trophy | 5 | 70 (50 active) | monthly pyreals |
| Mansion | 50 | **Rank-6 Monarch** | 20 Writs + 10M + trophies | 5 + basement | 100+ | 1M + 10 Writs/mo |

One house per character; 15-day account age to buy; 30-day re-buy lockout on abandon (not apartments);
mansions never in a settlement. Chest = 26 loose items + 1 pack. Writs of Refuge from Zaikhal Arcanum.
Maintenance paid at the Covenant Crystal; `@house guest/storage add`, house barrier open/closed.
