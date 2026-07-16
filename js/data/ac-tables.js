// AC leaf data tables — extracted from game.js (#778, first ES-module tranche).
// PURE DATA ONLY: this module must evaluate with no game or DOM environment (CI: node can import it).
// The boot module imports it and exposes every export as a global for the classic scripts.
export const ATTRS=["Strength","Endurance","Coordination","Quickness","Focus","Self"];
export const ATTR_DESC={Strength:"Melee damage & burden",Endurance:"Health, stamina & regen",Coordination:"Attack accuracy",
  Quickness:"Defense & run speed",Focus:"Spell skill & mana",Self:"Mana & spell skill"};
export const PRESETS={
  aluvian:{attr:{Strength:5,Coordination:5},armor:5,pot:2,title:"the Aluvian",town:"Holtburg"},
  sho:{attr:{Coordination:5,Quickness:3,Focus:2},pot:1,mana:1,title:"the Sho",town:"Shoushi"},
  gharundim:{attr:{Focus:6,Self:4},mana:3,title:"the Gharu'ndim",town:"Yaraq"},
  viamontian:{attr:{Strength:4,Coordination:4,Self:2},armor:5,pot:1,title:"the Viamontian",town:"Sanamar"},   // Throne of Destiny heritage
};
export const HERITAGE_LORE={
  aluvian:{name:"Aluvian",town:"Holtburg",affinity:"Blade & Bow",
    lore:"A hardy, self-reliant people from the island nation of Aluvia — farmers, hunters and soldiers whose blood runs to the frontier. The Aluvians favour cold steel and the longbow, taking readily to melee and missile skill. They begin their lives in the mountain garrison of Holtburg."},
  sho:{name:"Sho",town:"Shoushi",affinity:"Discipline & Finesse",
    lore:"A contemplative people of the eastern mountains who prize harmony, discipline and mastery of the self. Sho warriors are as deadly unarmed as armed, their scholars keen of mind. They favour finesse weapons and swift movement, and begin in the wooded town of Shoushi."},
  gharundim:{name:"Gharu'ndim",town:"Yaraq",affinity:"The Arcane",
    lore:"A learned people of the arid south who prize scholarship, tradition and the arts magical. The mages and merchants of the Gharu'ndim are renowned across Dereth. They favour the schools of magic, and begin in the desert city of Yaraq."},
  viamontian:{name:"Viamontian",town:"Sanamar",affinity:"Knightly Steel",
    lore:"A proud, chivalrous people ruled by a monarchy across the sea — knights, courtiers and crusaders sworn to blade and crown. The Viamontians favour heavy weapons and the sword, and arrive on Dereth's shores at the port of Sanamar."},
};
export const HERITAGE_SKILLS={   // default trained loadout when no creation choice was made
  aluvian:["heavy","meleed","shield"],sho:["missile","finesse","meleed"],
  gharundim:["war","life","creature"],viamontian:["heavy","meleed","shield"]};
export const TEMPLATE_TRAIN_WEAPON={   // ToD starter Training Weapon per profession template
  soldier:{name:"Training Battle Axe",wt:"axe",v:7},swashbuckler:{name:"Training Sword",wt:"sword",v:7},
  bowhunter:{name:"Training Shortbow",wt:"bow",v:7},wayfarer:{name:"Training Knife",wt:"dagger",v:6},
  warmage:{name:"Training Wand",wt:"focus",foc:12,v:8},lifecaster:{name:"Training Wand",wt:"focus",foc:12,v:8}};
export const HERITAGE_CLOTHING={   // creation shirt & breeches (retail arrivals weren't naked)
  aluvian:{sh:"Aluvian Shirt",pt:"Aluvian Breeches",tint:0x6a5a3a},
  gharundim:{sh:"Gharu'ndim Tunic",pt:"Gharu'ndim Trousers",tint:0x8a6a30},
  sho:{sh:"Sho Kimono Shirt",pt:"Sho Hakama",tint:0x3a4a5a},
  viamontian:{sh:"Viamontian Doublet",pt:"Viamontian Hose",tint:0x5a2a3a}};
export const HERITAGE_WELCOME={   // the Welcome Letter blessing line
  aluvian:"a kinsman's blessing from the green Aluvian shires",gharundim:"a blessing of Zaikhal and the shining sands",
  sho:"the quiet counsel of the Sho masters",viamontian:"a noble's charge from the courts of Viamont"};
export const CREATE_POOL=270;   // AC: 270 Attribute Credits above the six 10-point bases (330 total), max 100 each
export const CREATE_CREDITS=52; // AC: 52 starting skill credits (Train or, only at creation-time freely, Specialize)
export const TEMPLATES=[
  {k:"custom",  name:"Custom",       attr:{},                                    skills:[]},
  {k:"soldier", name:"Soldier",      attr:{Strength:22,Endurance:16,Coordination:8}, skills:["heavy","meleed","shield"]},
  {k:"swash",   name:"Swashbuckler", attr:{Coordination:22,Quickness:16,Strength:8}, skills:["light","meleed","dualwield"]},
  {k:"archer",  name:"Bow Hunter",   attr:{Coordination:26,Quickness:12,Strength:8}, skills:["missile","meleed"]},
  {k:"warmage", name:"War Mage",     attr:{Focus:26,Self:16,Endurance:4},         skills:["war","mana","meleed"]},
  {k:"lifemage",name:"Life Caster",  attr:{Self:26,Focus:16,Endurance:4},         skills:["life","creature","meleed"]},
  {k:"wayfarer",name:"Wayfarer",     attr:{Endurance:12,Coordination:12,Focus:10,Self:6}, skills:["heavy","life","meleed"]},
];
export const CANTRIP_TIERS=[["Minor",3],["Major",6],["Epic",9],["Legendary",12]];
export const SKILL_FAMS=["Magic","Defense","Melee","Missile","Trade","Tinkering","Lore"];
export const MAGIC_SCHOOLS={war:"war",aoe:"war",life:"life",item:"item",creature:"creature",void:"void"}; // spell.school → skill key
export const MELEE_WEAPON_KEYS=["heavy","light","finesse","twohand","dualwield","dirty","recklessness","sneak"];
export const WEAPON_SKILL={sword:"heavy",axe:"heavy",mace:"heavy",spear:"heavy",staff:"light",unarmed:"light",
  dagger:"finesse",twohand:"twohand",bow:"missile",crossbow:"missile",atlatl:"missile"};
export const AC_WT_BY_SKILL={1:"axe",4:"dagger",5:"mace",9:"spear",10:"staff",11:"sword",12:"atlatl",2:"bow",3:"crossbow",8:"atlatl",41:"twohand",44:"sword",45:"dagger",46:"dagger",47:"bow"};
export const _APT_SKILL={"heavy weapon":"heavy","light weapon":"light","finesse weapon":"finesse","two handed combat":"twohand","missile weapon":"missile","war magic":"war","life magic":"life","creature enchantment":"creature","item enchantment":"item","void magic":"void","melee defense":"meleed","missile defense":"missiled","invulnerability":"meleed","impregnability":"missiled","magic resistance":"magicd","healing":"healing","arcane lore":"arcane","mana conversion":"mana","lockpick":"lockpick","salvaging":"salvaging","shield":"shield"};
export const ROMAN=["","I","II","III","IV","V","VI","VII","VIII"];
export const CE_ATTRS=[["Strength","str","💪"],["Endurance","end","🛡️"],["Coordination","coord","🎯"],
  ["Quickness","quick","🌀"],["Focus","focus","🔆"],["Self","will","✦"]];
export const CE_TIERS=[{lvl:1,mag:5,cost:16,req:0,dur:60},{lvl:2,mag:9,cost:24,req:30,dur:75},{lvl:3,mag:14,cost:34,req:60,dur:90},
  {lvl:4,mag:20,cost:46,req:95,dur:105},{lvl:5,mag:27,cost:60,req:135,dur:120},{lvl:6,mag:35,cost:78,req:180,dur:150},
  {lvl:7,mag:45,cost:100,req:240,dur:180},{lvl:8,mag:58,cost:128,req:300,dur:210}];
export const CE_DEBUFFS=[
  {code:"weak",name:"Weakness",ico:"💢",eff:"dmg"},   // lowers the foe's attack damage
  {code:"slow",name:"Slowness",ico:"🐌",eff:"spd"},   // lowers the foe's movement speed
];
export const DEBUFF_TIERS=[{lvl:1,v:0.85,cost:18,req:0,dur:20},{lvl:2,v:0.72,cost:26,req:35,dur:25},{lvl:3,v:0.58,cost:36,req:70,dur:30},{lvl:4,v:0.45,cost:48,req:110,dur:35},
  {lvl:5,v:0.34,cost:62,req:150,dur:40},{lvl:6,v:0.25,cost:78,req:195,dur:45},{lvl:7,v:0.18,cost:96,req:250,dur:50},{lvl:8,v:0.12,cost:116,req:300,dur:55}];
export const FX_ELEM={fire:0xff3322,flame:0xff3322, acid:0x54dc3c, frost:0xeef4ff,ice:0xeef4ff,cold:0xeef4ff, shock:0x3a9bff,lightning:0x3a9bff};
export const FX_VITAL={hp:0xff3b30,health:0xff3b30, st:0xffd23b,stamina:0xffd23b, mn:0x3a9bff,mana:0x3a9bff};
export const SPELL_COL={
  fire:0xff5a1e, flame:0xff5a1e,                 // orange-red flame
  frost:0x74e0ff, ice:0x74e0ff, cold:0x74e0ff,   // icy cyan
  shock:0x8ab0ff, lightning:0x8ab0ff,            // electric blue-white
  acid:0x9bff2e,                                 // toxic green
  bludgeon:0xc98bff, force:0xc98bff,             // impact violet
  slash:0xe8f0fb, blade:0xe8f0fb,                // steel white
  pierce:0xbfe8ff,                               // pale ice-blue
  nether:0xc44dff, void:0xc44dff,                // corruption purple
  life:0x53ffa0, heal:0x53ffa0,                  // vivid life green
};
export const WAR_BOLTS=[
  {code:"flame",name:"Flame Bolt",   ico:"🔥",color:FX_ELEM.fire, r:0.28,element:"fire",    burn:true, base:18,foc:1.4,speed:55, cost:14,cd:520},
  {code:"frost",name:"Frost Bolt",   ico:"❄️",color:FX_ELEM.frost,r:0.32,element:"ice",     slow:.45,  base:24,foc:1.7,speed:46, cost:18,cd:760},
  {code:"light",name:"Lightning Bolt",ico:"⚡",color:FX_ELEM.shock,r:0.2, element:"shock",   stun:true, base:30,foc:1.8,speed:120,cost:20,cd:600},
  {code:"acid", name:"Acid Stream",  ico:"🟢",color:FX_ELEM.acid, r:0.26,element:"acid",                base:20,foc:1.5,speed:52, cost:16,cd:560},
  {code:"force",name:"Force Bolt",   ico:"⚪",color:0x4a1e8c,r:0.3, element:"bludgeon",            base:22,foc:1.5,speed:60, cost:16,cd:560},
  {code:"blade",name:"Whirling Blade",ico:"🌀",color:0xd6dbe4,r:0.28,element:"slash",              base:21,foc:1.5,speed:58, cost:16,cd:560},
  {code:"pierce",name:"Force Arrow", ico:"🏹",color:0xb0e0ff,r:0.18,element:"pierce",             base:23,foc:1.6,speed:72, cost:16,cd:540},
];
export const WAR_TIERS=[{lvl:1,mult:1,req:0},{lvl:2,mult:1.6,req:30},{lvl:3,mult:2.4,req:60},{lvl:4,mult:3.4,req:95},
  {lvl:5,mult:4.6,req:135},{lvl:6,mult:6.0,req:180},{lvl:7,mult:7.6,req:240},{lvl:8,mult:9.6,req:300}];
export const WAR_STORM=[{lvl:1,base:26,foc:1.3,cost:40,req:0},{lvl:2,base:42,foc:1.7,cost:60,req:40},{lvl:3,base:60,foc:2.1,cost:84,req:80},
  {lvl:4,base:84,foc:2.6,cost:112,req:130},{lvl:5,base:114,foc:3.0,cost:148,req:175},{lvl:6,base:150,foc:3.4,cost:188,req:215},
  {lvl:7,base:196,foc:3.8,cost:236,req:260},{lvl:8,base:256,foc:4.2,cost:300,req:310}];
export const LIFE_HEAL=[{lvl:1,base:34,foc:2.2,cost:26,cd:1500,req:0},{lvl:2,base:60,foc:2.8,cost:38,cd:1500,req:30},{lvl:3,base:95,foc:3.4,cost:54,cd:1600,req:60},{lvl:4,base:140,foc:4.0,cost:74,cd:1700,req:95},{lvl:5,base:195,foc:4.6,cost:98,cd:1800,req:135},{lvl:6,base:260,foc:5.2,cost:126,cd:1900,req:180},{lvl:7,base:340,foc:5.8,cost:158,cd:2000,req:240},{lvl:8,base:440,foc:6.4,cost:196,cd:2100,req:300}];
export const LIFE_REVIT=[{lvl:1,base:34,foc:2.0,cost:22,req:0},{lvl:2,base:58,foc:2.6,cost:32,req:30},{lvl:3,base:90,foc:3.2,cost:46,req:60},{lvl:4,base:130,foc:3.8,cost:62,req:100},
  {lvl:5,base:178,foc:4.2,cost:82,req:140},{lvl:6,base:236,foc:4.8,cost:106,req:185},{lvl:7,base:308,foc:5.4,cost:136,req:245},{lvl:8,base:396,foc:6.0,cost:172,req:305}];
export const ITEM_BUFFS=[
  {code:"blood",name:"Blood Drinker", ico:"🗡",stat:"dmg",  mags:[0.10,0.16,0.22,0.30,0.40,0.52,0.66,0.82], desc:"weapon & spell damage"},
  {code:"heart",name:"Heart Seeker",  ico:"🎯",stat:"crit", mags:[0.05,0.08,0.11,0.15,0.20,0.26,0.33,0.41], desc:"critical chance"},
  {code:"impen",name:"Impenetrability",ico:"🛡",stat:"armor",mags:[12,22,34,50,70,95,125,160],            desc:"armour"},
  {code:"swift",name:"Swift Killer",  ico:"💨",stat:"haste",mags:[0.08,0.14,0.20,0.28,0.38,0.50,0.64,0.80], desc:"attack speed"},
];
export const ITEM_TIERS=[{lvl:1,cost:20,req:0,dur:90},{lvl:2,cost:30,req:40,dur:120},{lvl:3,cost:44,req:80,dur:150},
  {lvl:4,cost:60,req:120,dur:180},{lvl:5,cost:80,req:160,dur:210},{lvl:6,cost:104,req:205,dur:240},{lvl:7,cost:134,req:255,dur:270},{lvl:8,cost:170,req:300,dur:300}];
export const BANE_TYPES=[
  {code:"flame", name:"Flame Bane",       ico:"🔥", el:"fire"},   {code:"frost",  name:"Frost Bane",       ico:"❄️", el:"frost"},
  {code:"acid",  name:"Acid Bane",        ico:"🧪", el:"acid"},   {code:"storm",  name:"Lightning Bane",   ico:"⚡", el:"shock"},
  {code:"blade", name:"Bladed Bane",      ico:"🗡", el:"slash"},  {code:"bludg",  name:"Bludgeoning Bane", ico:"🔨", el:"bludgeon"},
  {code:"pierce",name:"Piercing Bane",    ico:"🎯", el:"pierce"},
];
export const BANE_MAGS=[0.10,0.15,0.20,0.26,0.32,0.38,0.44,0.50];   // fraction of that element's damage negated, per level I–VIII
export const LIFE_DRAIN=[{lvl:1,base:22,foc:1.3,drain:0.45,cost:26,req:0},{lvl:2,base:38,foc:1.7,drain:0.5,cost:40,req:40},{lvl:3,base:58,foc:2.0,drain:0.55,cost:56,req:70},{lvl:4,base:82,foc:2.4,drain:0.6,cost:76,req:115},
  {lvl:5,base:112,foc:2.8,drain:0.62,cost:100,req:155},{lvl:6,base:150,foc:3.2,drain:0.65,cost:130,req:200},{lvl:7,base:198,foc:3.6,drain:0.68,cost:166,req:255},{lvl:8,base:258,foc:4.0,drain:0.72,cost:208,req:305}];
export const VOID_TIERS=[{lvl:1,mult:1,req:0},{lvl:2,mult:1.6,req:30},{lvl:3,mult:2.4,req:60},{lvl:4,mult:3.4,req:95},{lvl:5,mult:4.6,req:135},{lvl:6,mult:6.0,req:180},{lvl:7,mult:7.6,req:240},{lvl:8,mult:9.6,req:300}];
export const VOID_CORRUPT=[{lvl:2,dps:9,cvuln:1.12,req:40},{lvl:3,dps:13,cvuln:1.15,req:70},{lvl:4,dps:18,cvuln:1.18,req:115},
  {lvl:5,dps:24,cvuln:1.22,req:155},{lvl:6,dps:31,cvuln:1.26,req:200},{lvl:7,dps:40,cvuln:1.30,req:255},{lvl:8,dps:50,cvuln:1.35,req:305}];
export const CE_SKILLS=[
  ["war","War Magic","🔥"],["life","Life Magic","✨"],["creature","Creature Mastery","🦂"],["item","Item Mastery","💍"],
  ["void","Void Mastery","🌑"],["mana","Mana Conversion","🔷"],["heavy","Heavy Weapon","⚔️"],["light","Light Weapon","🗡"],
  ["finesse","Finesse Weapon","🤺"],["twohand","Two Handed","🪓"],["missile","Missile Weapon","🏹"],["meleed","Invulnerability","🛡"],
  ["missiled","Impregnability","🪶"],["magicd","Magic Resistance","🔯"],["healing","Healing","💉"],["arcane","Arcane Enlightenment","📜"],
  ["run","Sprint","🏃"],["summon","Summoning","🔮"]];
export const LIFE_RENEW=[
  {pool:"hp",name:"Regeneration", ico:"💗",rate:[1.6,2.6,4.0,6.0,8.5,11.5,15,20]},
  {pool:"st",name:"Rejuvenation", ico:"🌿",rate:[2.0,3.2,5.0,7.5,10.5,14,18,24]},
  {pool:"mn",name:"Mana Renewal", ico:"🔹",rate:[1.4,2.3,3.6,5.4,7.6,10.4,13.6,18]},
];
export const RENEW_TIERS=[{lvl:1,cost:22,req:0,dur:24},{lvl:2,cost:32,req:30,dur:26},{lvl:3,cost:46,req:60,dur:28},{lvl:4,cost:62,req:95,dur:30},
  {lvl:5,cost:82,req:135,dur:32},{lvl:6,cost:106,req:180,dur:34},{lvl:7,cost:136,req:240,dur:36},{lvl:8,cost:172,req:300,dur:40}];
export const LIFE_DISPEL=[{lvl:1,cost:24,req:0,immune:1.5},{lvl:2,cost:34,req:40,immune:3},{lvl:3,cost:48,req:80,immune:5},{lvl:4,cost:64,req:120,immune:8}];
export const LIFE_PROT=[{lvl:1,v:0.12,cost:24,req:0,dur:90},{lvl:2,v:0.20,cost:34,req:35,dur:120},{lvl:3,v:0.30,cost:48,req:70,dur:150},{lvl:4,v:0.40,cost:64,req:110,dur:180},
  {lvl:5,v:0.48,cost:84,req:150,dur:210},{lvl:6,v:0.55,cost:108,req:195,dur:240},{lvl:7,v:0.62,cost:138,req:250,dur:270},{lvl:8,v:0.70,cost:174,req:300,dur:300}];
export const LIFE_VULN=[{lvl:1,v:1.12,cost:20,req:0,dur:20},{lvl:2,v:1.20,cost:28,req:35,dur:25},{lvl:3,v:1.32,cost:38,req:70,dur:30},{lvl:4,v:1.45,cost:50,req:110,dur:35},
  {lvl:5,v:1.58,cost:64,req:150,dur:40},{lvl:6,v:1.72,cost:80,req:195,dur:45},{lvl:7,v:1.88,cost:100,req:250,dur:50},{lvl:8,v:2.05,cost:124,req:300,dur:55}];
export const LIFE_VULN_EL=[
  {el:"fire",   name:"Fire Vulnerability",       ico:"🔥"},{el:"ice",     name:"Cold Vulnerability",       ico:"❄️"},
  {el:"shock",  name:"Lightning Vulnerability",  ico:"⚡"},{el:"acid",    name:"Acid Vulnerability",       ico:"🟢"},
  {el:"bludgeon",name:"Bludgeoning Vulnerability",ico:"⚪"},{el:"slash",  name:"Slashing Vulnerability",    ico:"🗡"},
  {el:"pierce", name:"Piercing Vulnerability",   ico:"🏹"}];
export const LIFE_HARM=[{lvl:1,base:30,foc:1.6,cost:24,req:0},{lvl:2,base:50,foc:2.0,cost:36,req:30},{lvl:3,base:78,foc:2.4,cost:52,req:60},{lvl:4,base:112,foc:2.8,cost:72,req:95},{lvl:5,base:155,foc:3.2,cost:96,req:135},{lvl:6,base:208,foc:3.6,cost:124,req:180},{lvl:7,base:272,foc:4.0,cost:158,req:240},{lvl:8,base:352,foc:4.4,cost:196,req:300}];
export const WAR_ROOT={flame:"Flame",frost:"Frost",light:"Lightning",acid:"Acid",force:"Bludgeoning",blade:"Blade",pierce:"Force"};
export const WAR_GEOMS=[
  {geom:"blast", gname:"Blast", ico:"🔆",dmgMul:0.8,costMul:1.4,cdMul:1.0, splash:3.6},   // flies like a bolt, detonates for splash damage on impact
  {geom:"volley",gname:"Volley",ico:"🎇",dmgMul:0.6,costMul:1.3,cdMul:1.1},
  {geom:"ring",  gname:"Ring",  ico:"💥",dmgMul:0.8,costMul:1.7,cdMul:1.5},
];
export const GEOM_TIERS=[{lvl:1,m:1,req:0},{lvl:2,m:1.6,req:35},{lvl:3,m:2.4,req:70},{lvl:4,m:3.4,req:110},
  {lvl:5,m:4.6,req:150},{lvl:6,m:6.0,req:195},{lvl:7,m:7.6,req:250},{lvl:8,m:9.6,req:300}];
export const LIGHT_TIERS=[{lvl:1,dur:90, intensity:1.9,dist:17,cost:14,req:0},
                   {lvl:2,dur:180,intensity:2.5,dist:21,cost:22,req:30},
                   {lvl:3,dur:360,intensity:3.1,dist:26,cost:32,req:65},
                   {lvl:4,dur:600,intensity:3.9,dist:32,cost:46,req:110}];
export const LIGHT_SPELLS=[{school:"life",code:"enlighten",name:"Enlightenment",ico:"🔆",color:0xfff2cf},
                    {school:"creature",code:"foxfire",name:"Foxfire",ico:"🦊",color:0x9affc0}];
export const ITEM_ENCHANT_DUR=[1800,1800,1800,1800,1800,2700,3600,3600];
