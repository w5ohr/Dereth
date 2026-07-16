/* ============================================================================
   ULTIMA-ONLINE-STYLE CRAFTING & HARVESTING  (self-contained module)
   ── Gathering skills (Mining, Lumberjacking, Fishing) feed nine metal, seven
      wood and four leather tiers. Raw ore/logs/hides refine into ingots/boards/
      leather, which the crafting skills (Blacksmithy, Tailoring, Carpentry,
      Fletching, Tinkering, Cooking, Alchemy) turn into gear at a Crafting Forge.
   ── Working level rises with USE (player.craft); training/spec is a head start.
      Higher material tiers pass their bonuses to the item; skilled crafters roll
      EXCEPTIONAL pieces signed with a maker's mark. Tools wear out with use.
   ============================================================================ */

// ---- resource ladders (mining/lumberjack/leather requirement = working level) ----
const UO_METALS=[
  {k:"iron",       n:"Iron",        req:0,  col:0x9a9a9a, dmg:1.00, arm:1.00, val:1,  res:"—"},
  {k:"dullcopper", n:"Dull Copper", req:20, col:0x9a6a4a, dmg:1.06, arm:1.12, val:2,  res:"+durability, +phys"},
  {k:"shadowiron", n:"Shadow Iron", req:30, col:0x35363f, dmg:1.10, arm:1.08, val:3,  res:"+durability"},
  {k:"copper",     n:"Copper",      req:40, col:0xb87333, dmg:1.14, arm:1.11, val:4,  res:"+phys"},
  {k:"bronze",     n:"Bronze",      req:50, col:0x8c6a3a, dmg:1.19, arm:1.15, val:6,  res:"+fire"},
  {k:"gold",       n:"Gold",        req:60, col:0xd4af37, dmg:1.24, arm:1.20, val:9,  res:"+cold/poison, +luck"},
  {k:"agapite",    n:"Agapite",     req:70, col:0xc0607e, dmg:1.29, arm:1.27, val:13, res:"+all resist"},
  {k:"verite",     n:"Verite",      req:80, col:0x2f8a5a, dmg:1.35, arm:1.35, val:18, res:"+phys/energy"},
  {k:"valorite",   n:"Valorite",    req:90, col:0x4a6ac8, dmg:1.42, arm:1.44, val:26, res:"+phys/energy, +luck"},
];
const UO_WOODS=[
  {k:"regular",  n:"",          req:0,  col:0x8a5a32, dmg:1.00, arm:1.00, val:1},
  {k:"oak",      n:"Oak",       req:25, col:0x9a6a3a, dmg:1.05, arm:1.05, val:2},
  {k:"ash",      n:"Ash",       req:40, col:0xb89a6a, dmg:1.10, arm:1.08, val:3},
  {k:"yew",      n:"Yew",       req:55, col:0x6f5f3a, dmg:1.16, arm:1.13, val:5},
  {k:"heartwood",n:"Heartwood", req:70, col:0xa84a2a, dmg:1.23, arm:1.19, val:8},
  {k:"bloodwood",n:"Bloodwood", req:82, col:0x8a1f1f, dmg:1.31, arm:1.25, val:13},
  {k:"frostwood",n:"Frostwood", req:92, col:0x8ab0d0, dmg:1.39, arm:1.33, val:20},
];
const UO_LEATHERS=[
  {k:"leather",n:"Leather", req:0,  col:0x8a5a32, arm:1.00, val:1},
  {k:"spined", n:"Spined",  req:35, col:0x5a7a4a, arm:1.14, val:3},
  {k:"horned", n:"Horned",  req:60, col:0x8a6a3a, arm:1.26, val:6},
  {k:"barbed", n:"Barbed",  req:85, col:0x6a3a5a, arm:1.38, val:11},
];
const UO_METAL_BY={},UO_WOOD_BY={},UO_LEATHER_BY={};
UO_METALS.forEach(m=>UO_METAL_BY[m.k]=m); UO_WOODS.forEach(w=>UO_WOOD_BY[w.k]=w); UO_LEATHERS.forEach(l=>UO_LEATHER_BY[l.k]=l);
const UO_TRADES=["mining","lumberjack","fishing","blacksmithy","tailoring","carpentry","fletching","tinkering","cooking","alchemy"];

// ---- working level: use-XP curve (0..100) + training(+20)/spec(+15) head start (cap 120) ----
function craftLvl(key){
  const xp=(player.craft&&player.craft[key])||0;
  let lvl=Math.min(100, Math.floor(7.6*Math.sqrt(xp)));   // xp 44→~50, xp 173→100
  if(typeof skillTier==="function"){ const t=skillTier(key); if(t>=1) lvl=Math.min(120,lvl+20); if(t>=2) lvl=Math.min(120,lvl+15); }
  return lvl;
}
function gainCraft(key,amt){
  player.craft=player.craft||{};
  const before=craftLvl(key);
  player.craft[key]=(player.craft[key]||0)+(amt||1);
  const after=craftLvl(key);
  if(after>before){
    const nm=(SKILL_BY_KEY[key]&&SKILL_BY_KEY[key].n)||key;
    floater(player.x,EYE+0.7,player.z,nm+" "+after,"#ffd86b");
    if(after%5===0) log(`Your <b>${nm}</b> skill rises to <b>${after}</b>.`,"sys");
  }
}
// weighted tier roll: mostly the base tier, rarer chance of the best unlocked (authentic "colored ore is rare")
function rollResTier(ladder,lvl){
  const avail=ladder.filter(t=>t.req<=lvl);
  for(let i=avail.length-1;i>=1;i--){ const t=avail[i]; const p=clamp(0.05+(lvl-t.req)*0.004,0.02,0.32); if(Math.random()<p) return t; }
  return avail[0];
}

// ---- resource items (stackable inventory items) ----
function resItem(kind,tierKey,count){
  count=count||1; let name,ico,col,v,kl,tl;
  if(kind==="ore"){ const t=UO_METAL_BY[tierKey]||UO_METALS[0]; name=t.n+" Ore"; ico="🪨"; col=t.col; v=Math.max(1,Math.round(2*t.val)); kl="raw ore — smelt into ingots"; tl=t.n; }
  else if(kind==="ingot"){ const t=UO_METAL_BY[tierKey]||UO_METALS[0]; name=t.n+" Ingot"; ico="🔩"; col=t.col; v=Math.max(1,Math.round(4*t.val)); kl="metal ingot"; tl=t.n; }
  else if(kind==="log"){ const t=UO_WOOD_BY[tierKey]||UO_WOODS[0]; name=(t.n?t.n+" ":"")+"Log"; ico="🪵"; col=t.col; v=Math.max(1,Math.round(2*t.val)); kl="log — mill into boards"; tl=t.n||"Common"; }
  else if(kind==="board"){ const t=UO_WOOD_BY[tierKey]||UO_WOODS[0]; name=(t.n?t.n+" ":"")+"Board"; ico="🟫"; col=t.col; v=Math.max(1,Math.round(3*t.val)); kl="board"; tl=t.n||"Common"; }
  else if(kind==="hide"){ const t=UO_LEATHER_BY[tierKey]||UO_LEATHERS[0]; name=(t.k==="leather"?"":t.n+" ")+"Hide"; ico="🟤"; col=t.col; v=Math.max(1,Math.round(2*t.val)); kl="raw hide — tan into leather"; tl=t.n; }
  else { const t=UO_LEATHER_BY[tierKey]||UO_LEATHERS[0]; name=(t.k==="leather"?"":t.n+" ")+"Leather"; ico="🟫"; col=t.col; v=Math.max(1,Math.round(4*t.val)); kl="cured leather"; tl=t.n; }
  return {name,stat:"resource",kind,mat:tierKey,count,ico,v,kindLabel:kl,tierLabel:tl};
}
function resNameFor(kind,tierKey){ return resItem(kind,tierKey,1).name; }

// ---- tools ----
function findCraftTool(kind){ return player.inv.find(it=>it&&it.stat==="tool"&&it.tool===kind&&(it.uses==null||it.uses>0)); }
function hasCraftTool(kind){ return !!findCraftTool(kind); }
function consumeCraftTool(kind){ const t=findCraftTool(kind); if(!t) return false;
  if(t.uses!=null){ t.uses--; if(t.uses<=0){ const i=player.inv.indexOf(t); if(i>=0)player.inv.splice(i,1); log(`Your <b>${t.name}</b> wears out and breaks.`,"warn"); } } return true; }
function toolItem(kind,name,uses,ico){ return {name,stat:"tool",tool:kind,uses,ico}; }

// ---- GATHERING ── mining (called from gatherNode's ore branch) ----
function mineOreYield(n){
  const lvl=craftLvl("mining");
  const metalKey=(n&&n.metal)||null;
  const t = metalKey ? (UO_METAL_BY[metalKey]||UO_METALS[0]) : rollResTier(UO_METALS,lvl);
  const under = lvl < t.req;                      // below the ideal skill: you can still work the vein, just less efficiently
  const bonus=hasCraftTool("mining")?irnd(1,3):0;
  let a=irnd(2,5)+bonus-(under?2:0); if(a<1) a=1;
  const it=resItem("ore",t.k,a);
  if(!addToInv(it)){ addDrop(player.x,player.z,{type:"item",item:it}); }
  const col=(t.k==="iron"?"#cfd0d0":"#"+t.col.toString(16).padStart(6,"0"));
  floater(player.x,EYE+0.4,player.z,"+"+a+" "+t.n+" Ore",col);
  log(`You mine <b>${a} ${t.n} Ore</b>${under?" <span style='color:#d0a06a'>(hard going at your skill)</span>":(bonus?" (pickaxe)":"")}.`,"loot");
  if(hasCraftTool("mining")) consumeCraftTool("mining");
  gainCraft("mining", under?1.7:1);
}

// ---- GATHERING ── lumberjacking & fishing (via craftInteract) ----
let craftStations=[], lumberNodes=[], fishSpots=[];
let _craftWorldBuilt=false, _treeRegrowTimer=null;   // #793: dedicated build-once guard + regrow-interval handle (don't key idempotency on array length, and don't leak a second interval on a double-build)
function chopTree(t){
  const lvl=craftLvl("lumberjack");
  t.depleted=true; t.readyAt=now()+rnd(45000,80000);
  if(t.canopy) t.canopy.visible=false; if(t.stump) t.stump.visible=true; if(t.spr) t.spr.visible=false;
  startAction("mine",1.4);
  const tier=rollResTier(UO_WOODS,lvl);
  const bonus=hasCraftTool("lumberjack")?irnd(1,3):0;
  const a=irnd(2,5)+bonus;
  const it=resItem("log",tier.k,a);
  if(!addToInv(it)){ addDrop(player.x,player.z,{type:"item",item:it}); }
  floater(player.x,EYE+0.5,player.z,"+"+a+" "+(tier.n?tier.n+" ":"")+"Log","#c8a06a");
  log(`You fell the tree for <b>${a} ${tier.n?tier.n+" ":""}Log${a>1?"s":""}</b>${bonus?" (hatchet)":""}.`,"loot");
  if(hasCraftTool("lumberjack")) consumeCraftTool("lumberjack");
  gainCraft("lumberjack",1); gainXP(22); if(SFX&&SFX.gold)SFX.gold(); if(typeof questEvent==="function")questEvent("gather","harvest"); updateHUD();
}
const UO_FISH=[
  {n:"Minnow",req:0,v:2,dur:45},{n:"Trout",req:15,v:3,dur:70},{n:"Bass",req:35,v:4,dur:110},
  {n:"Salmon",req:55,v:5,dur:160},{n:"Swordfish",req:80,v:7,dur:220},
];
function rollFish(lvl){
  const avail=UO_FISH.filter(f=>f.req<=lvl); let f=avail[0];
  for(let i=avail.length-1;i>=1;i--){ const p=clamp(0.06+(lvl-avail[i].req)*0.005,0.03,0.4); if(Math.random()<p){ f=avail[i]; break; } }
  return {name:"Raw "+f.n,stat:"food",v:f.v,attr:"Endurance",dur:f.dur,ico:"🐟",fish:true,raw:true,species:f.n};
}
function fishAtSpot(sp){
  if(!hasCraftTool("fishing")){ log("You need a <b>Fishing Pole</b> — buy or craft one at a Crafting Forge.","warn"); if(SFX&&SFX.deny)SFX.deny(); return; }
  sp.readyAt=now()+rnd(6000,12000);
  startAction("fish",1.6);
  const lvl=craftLvl("fishing");
  if(lvl>40 && Math.random()<0.03){ const g=irnd(200,900); player.gold+=g; floater(player.x,EYE+0.6,player.z,"+"+g+"p","#ffd86b"); log(`Your line snags a waterlogged coffer — <b>${g} pyreals</b>!`,"loot"); }
  else { const f=rollFish(lvl); if(!addToInv(f)){ addDrop(player.x,player.z,{type:"item",item:f}); } floater(player.x,EYE+0.6,player.z,f.name,"#8fd0e0"); log(`You reel in a <b>${f.name}</b>.`,"loot"); }
  consumeCraftTool("fishing");
  gainCraft("fishing",1); gainXP(16); if(SFX&&SFX.gold)SFX.gold(); if(typeof questEvent==="function")questEvent("gather","harvest"); updateHUD();
}
// hides drop off slain beasts. Designated game (deer/bear) always yield hides + raw meat;
// other fleshy creatures drop a hide by chance, tier scaling with the beast's strength.
function craftHideDrop(m,b){
  b=b||(typeof BESTIARY!=="undefined"&&BESTIARY[m.kind])||{};
  if(b.hide){
    const n=b.hideN?irnd(b.hideN[0],b.hideN[1]):1;
    addDrop(m.x+rnd(-1,1),m.z+rnd(-1,1),{type:"item",item:resItem("hide",b.hide,n)});
    if(b.meat) addDrop(m.x+rnd(-1,1),m.z+rnd(-1,1),{type:"item",item:{name:"Raw "+b.meat,stat:"food",v:2,attr:"Strength",dur:70,ico:"🍖",meat:true,raw:true}});
    return;
  }
  if(Math.random()>0.4) return;
  const pw=(m&&m.xp)||b.xp||10;
  let k="leather"; if(pw>1400)k="barbed"; else if(pw>550)k="horned"; else if(pw>160)k="spined";
  addDrop(m.x+rnd(-1,1),m.z+rnd(-1,1),{type:"item",item:resItem("hide",k,irnd(1,2))});
}

// ---- interact hook: stations → menu, trees → chop, water's edge → fish ----
function craftInteract(){
  for(const s of craftStations){ if(Math.hypot(s.x-player.x,s.z-player.z)<3.8){ openCraftMenu(); return true; } }
  for(const t of lumberNodes){ if(t.depleted&&now()>t.readyAt){ t.depleted=false; if(t.canopy)t.canopy.visible=true; if(t.stump)t.stump.visible=false; if(t.spr)t.spr.visible=true; }
    if(!t.depleted && Math.hypot(t.x-player.x,t.z-player.z)<3){ chopTree(t); return true; } }
  for(const sp of fishSpots){ if(Math.hypot(sp.x-player.x,sp.z-player.z)<3.4 && (!sp.readyAt||now()>sp.readyAt)){ fishAtSpot(sp); return true; } }
  return false;
}
function nearCraftStation(){ for(const s of craftStations){ if(Math.hypot(s.x-player.x,s.z-player.z)<5) return true; } return false; }

// ---- area difficulty → ore tier (low ore near towns, valorite in the Direlands) ----
function oreAreaLevel(x,z){
  const dd=(typeof nearestCapitalDist==="function"?nearestCapitalDist(x,z):300)/(typeof WSCALE!=="undefined"?WSCALE:1);
  let lvl=clamp((dd-120)/8,0,90);                                   // farther from town = deadlier = richer ore
  const reg=(typeof regionOf==="function")?regionOf(x,z):0;
  if(reg===-1) lvl=Math.max(lvl, 74+clamp((dd-300)/40,0,16));       // Direlands: the deadliest frontier → top-tier ore
  if(typeof coldZone==="function" && coldZone(x,z)) lvl=Math.max(lvl,52);
  const h=(Math.imul((((x*0.11)|0)+7919)>>>0,2654435761) ^ (((z*0.11)|0)+104729))>>>0;
  lvl+=((h%17)-8);                                                  // gentle variation so tiers aren't perfect radial bands
  return clamp(lvl,0,98);
}
const ORE_DEPOSIT_SCALE=30;   // deposit body is drawn this many × the base outcrop size (base outcrop ~1.2 wide, ~1.5 tall)
// Researched per-metal ore look. Each deposit = a craggy HOST outcrop (stone) + an ORE expression:
//   • "crust"  — oxidized mineral crusts hugging the rock (copper malachite/azurite, dull-copper patina)
//   • "vein"   — metallic veins + flecks threading the stone (iron hematite specks, brassy bronze, gold-in-quartz)
//   • "crystal"— faceted crystals emerging from the rock; the arcane metals GLOW (shadow-iron→valorite)
// rock=host stone, ore=metallic tone, cr1/cr2=crust hues, glow=emissive intensity, gcol=glow/crystal colour.
const ORE_PROFILES={
  iron:      {rock:0x4a453d, band:0x6e4636, ore:0x9a9a9a, style:"vein",    metalness:0.30, roughness:0.88, glow:0.00, banded:true},   // hematite/limonite: dark rock, rusty bands, dull grey specks
  copper:    {rock:0x584636, ore:0xb87333, cr1:0x2fae7a, cr2:0x2f7fc0,     style:"crust", metalness:0.30, roughness:0.80, glow:0.00}, // green malachite + blue azurite on brown rock, native-copper flecks
  bronze:    {rock:0x554633, ore:0x9a7a44, style:"vein",                   metalness:0.62, roughness:0.50, glow:0.00},                // brassy metallic veins (tin/cassiterite dark host)
  gold:      {rock:0x8f8a7c, ore:0xd4af37, style:"vein",                   metalness:0.92, roughness:0.32, glow:0.06, quartz:true},   // pale quartz host laced with bright gold
  dullcopper:{rock:0x554438, ore:0x9a6a4a, cr1:0x9a6a4a, cr2:0x6f4a34,     style:"crust", metalness:0.22, roughness:0.86, glow:0.00}, // muted oxidised copper patina
  shadowiron:{rock:0x21222a, ore:0x35363f, style:"crystal", gcol:0x5566aa, metalness:0.72, roughness:0.42, glow:0.16},               // near-black stone, cold dark crystals w/ faint blue sheen
  agapite:   {rock:0x463840, ore:0xc0607e, style:"crystal", gcol:0xff9ec0, metalness:0.55, roughness:0.34, glow:0.55},               // rose crystals, glow
  verite:    {rock:0x293a30, ore:0x2f8a5a, style:"crystal", gcol:0x62e6a4, metalness:0.55, roughness:0.30, glow:0.68},               // green crystals, glow
  valorite:  {rock:0x2c3340, ore:0x4a6ac8, style:"crystal", gcol:0x93b6ff, metalness:0.66, roughness:0.26, glow:0.85},               // blue crystals, strongest glow (top tier)
};
// materials are identical for every deposit of a metal → build once and share (fewer GPU state changes)
const _oreMatCache={};
function oreMats(k){
  if(_oreMatCache[k]) return _oreMatCache[k];
  const p=ORE_PROFILES[k]||ORE_PROFILES.iron;
  const m={
    stone:new THREE.MeshStandardMaterial({color:p.rock,flatShading:true,roughness:p.quartz?0.55:0.95,metalness:0.04}),
    band :p.banded?new THREE.MeshStandardMaterial({color:p.band,flatShading:true,roughness:0.9,metalness:0.15}):null,
    ore  :new THREE.MeshStandardMaterial({color:p.ore,metalness:p.metalness,roughness:p.roughness,emissive:p.ore,emissiveIntensity:p.glow,flatShading:true}),
    cr1  :p.cr1!=null?new THREE.MeshStandardMaterial({color:p.cr1,roughness:0.72,metalness:0.10,flatShading:true}):null,
    cr2  :p.cr2!=null?new THREE.MeshStandardMaterial({color:p.cr2,roughness:0.68,metalness:0.12,flatShading:true}):null,
    xtal :new THREE.MeshStandardMaterial({color:p.ore,emissive:p.gcol||p.ore,emissiveIntensity:p.glow||0.2,metalness:p.metalness,roughness:p.roughness,flatShading:true}),
  };
  return (_oreMatCache[k]=m);
}
// craggy boulder: jitter every vertex of a base solid radially so flat-shading reads as fractured rock
function _craggy(geo,amt){ const pa=geo.attributes.position; for(let i=0;i<pa.count;i++){ const f=1+rnd(-amt,amt); pa.setXYZ(i,pa.getX(i)*f,pa.getY(i)*f,pa.getZ(i)*f); } geo.computeVertexNormals(); return geo; }
function buildOreDeposit(metalKey){
  const p=ORE_PROFILES[metalKey]?metalKey:"iron", prof=ORE_PROFILES[p], M=oreMats(p);
  const g=new THREE.Group();
  const body=new THREE.Group();   // the physical deposit — scaled as one unit so the ⛏ marker (added later) stays independent
  // ── host outcrop: a main craggy boulder + two shoulders, with a low rubble skirt (footprint kept ~the approved 30× size) ──
  const main=part(body,_craggy(new THREE.IcosahedronGeometry(0.82,1),0.22),M.stone,0,0.58,0,rnd(0,6.28),rnd(0,6.28),rnd(0,6.28)); main.castShadow=main.receiveShadow=true; main.scale.set(1,rnd(0.82,1.05),1);
  for(let i=0;i<2;i++){ const a=rnd(0,6.28), r=rnd(0.4,0.55); const s=part(body,_craggy(new THREE.IcosahedronGeometry(rnd(0.34,0.48),1),0.26),M.stone,Math.cos(a)*r,rnd(0.26,0.46),Math.sin(a)*r,rnd(0,6.28),rnd(0,6.28),rnd(0,6.28)); s.castShadow=true; }
  for(let i=0;i<4;i++){ const a=rnd(0,6.28), r=rnd(0.55,0.82); part(body,_craggy(new THREE.IcosahedronGeometry(rnd(0.1,0.2),0),0.3),M.stone,Math.cos(a)*r,rnd(0.05,0.14),Math.sin(a)*r,rnd(0,6.28),rnd(0,6.28),rnd(0,6.28)); }
  if(prof.banded&&M.band){ // iron: rusty banded-iron-formation strata tilted across the boulder
    for(let i=0;i<3;i++){ const b=part(body,new THREE.CylinderGeometry(0.9,0.9,0.1,10),M.band,0,0.4+i*0.42,0,rnd(-0.25,0.25),0,rnd(-0.25,0.25)); b.scale.set(1,1,0.5); }
  }
  // ── ore expression ──
  if(prof.style==="crust"){
    for(let i=0;i<10;i++){ const a=rnd(0,6.28), el=rnd(0.15,1.0), r=rnd(0.45,0.8); // patches clinging to the surface
      const patch=part(body,new THREE.SphereGeometry(rnd(0.11,0.22),7,5),(i%2?M.cr2:M.cr1)||M.ore,
        Math.cos(a)*Math.cos(el)*r,0.5+Math.sin(el)*0.8,Math.sin(a)*Math.cos(el)*r,rnd(0,3),rnd(0,3),rnd(0,3));
      patch.scale.set(1,0.4,1); }
    for(let i=0;i<5;i++){ const a=rnd(0,6.28); part(body,new THREE.OctahedronGeometry(rnd(0.05,0.09),0),M.ore,Math.cos(a)*rnd(0.35,0.75),rnd(0.4,1.1),Math.sin(a)*rnd(0.35,0.75),rnd(0,3),rnd(0,3),rnd(0,3)); } // native-metal flecks
  } else if(prof.style==="vein"){
    for(let i=0;i<6;i++){ const a=rnd(0,6.28), y=rnd(0.3,1.05); // metallic veins threading the stone
      const v=part(body,new THREE.BoxGeometry(0.05,rnd(0.35,0.65),0.05),M.ore,Math.cos(a)*rnd(0.45,0.72),y,Math.sin(a)*rnd(0.45,0.72),rnd(-0.9,0.9),rnd(0,3),rnd(-0.9,0.9)); }
    for(let i=0;i<8;i++){ const a=rnd(0,6.28); part(body,new THREE.IcosahedronGeometry(rnd(0.04,0.08),0),M.ore,Math.cos(a)*rnd(0.4,0.8),rnd(0.35,1.15),Math.sin(a)*rnd(0.4,0.8),rnd(0,3),rnd(0,3),rnd(0,3)); } // flecks
  } else { // crystal — faceted prisms emerging in a cluster; magical metals glow via emissive
    const n=6+irnd(0,2);
    for(let i=0;i<n;i++){ const a=(i/n)*6.28+rnd(-0.4,0.4), r=rnd(0.15,0.6), h=rnd(0.5,1.05);
      const cr=part(body,new THREE.CylinderGeometry(0.015,rnd(0.11,0.17),h,6),M.xtal,Math.cos(a)*r,0.6+rnd(-0.1,0.35),Math.sin(a)*r,rnd(-0.5,0.5),rnd(0,6.28),rnd(-0.5,0.5));
      cr.castShadow=true; }
    for(let i=0;i<3;i++){ const a=rnd(0,6.28); part(body,new THREE.OctahedronGeometry(rnd(0.06,0.1),0),M.xtal,Math.cos(a)*rnd(0.4,0.8),rnd(0.4,1.1),Math.sin(a)*rnd(0.4,0.8),rnd(0,3),rnd(0,3),rnd(0,3)); } // shards
  }
  body.scale.setScalar(ORE_DEPOSIT_SCALE);   // 30× bigger ore deposits
  g.add(body);
  return g;
}
function addOreDeposit(x,z,metalKey){
  const mesh=buildOreDeposit(metalKey); mesh.position.set(x,(typeof terrainH==="function"?terrainH(x,z):0),z);
  const t=UO_METAL_BY[metalKey]||UO_METALS[0];
  const icon=textSprite("⛏","#"+(t.k==="iron"?"cdbb88":t.col.toString(16).padStart(6,"0")));
  // sit the marker just above the enlarged deposit (base rock top ≈1.35) and size it up so it still reads over a 30× boulder
  icon.scale.set(4,4,1); icon.position.y=ORE_DEPOSIT_SCALE*1.35+3; mesh.add(icon);
  scene.add(mesh);
  mesh.updateMatrixWorld(true); mesh.traverse(o=>{ o.matrixAutoUpdate=false; });   // #perf: static deposit (deplete only toggles .visible) → skip per-frame matrix recompute of its ~20 meshes
  nodes.push({x,z,type:"ore",metal:metalKey,mesh,depleted:false,readyAt:0});   // rides the existing node interact + respawn tick; regrows as the SAME metal
}
let _orePlaced={};

// ---- WORLD: Crafting Forges in towns + ore veins, choppable trees, fishing spots ----
function buildStationMesh(x,y,z){
  const g=new THREE.Group(); g.position.set(x,y,z);
  const stone=new THREE.MeshStandardMaterial({color:0x5a554c,roughness:0.95,metalness:0.05});
  const iron=new THREE.MeshStandardMaterial({color:0x2b2b30,roughness:0.55,metalness:0.7});
  const wood=new THREE.MeshStandardMaterial({color:0x6a4a2a,roughness:0.9});
  // forge base + hood
  const base=new THREE.Mesh(new THREE.BoxGeometry(2.0,1.1,1.4),stone); base.position.set(-1.0,0.55,0); base.castShadow=base.receiveShadow=true; g.add(base);
  const hood=new THREE.Mesh(new THREE.CylinderGeometry(0.15,0.6,1.3,8),stone); hood.position.set(-1.0,1.85,0); g.add(hood);
  const coals=new THREE.Mesh(new THREE.CircleGeometry(0.55,12),new THREE.MeshBasicMaterial({color:0xff7a2a})); coals.rotation.x=-Math.PI/2; coals.position.set(-1.0,1.12,0); g.add(coals);
  const glow=new THREE.PointLight(0xff8a3a,1.2,7); glow.position.set(-1.0,1.4,0); g.add(glow);
  // anvil
  const anv=new THREE.Mesh(new THREE.BoxGeometry(0.9,0.32,0.4),iron); anv.position.set(0.7,0.95,0.2); anv.castShadow=true; g.add(anv);
  const anvNeck=new THREE.Mesh(new THREE.BoxGeometry(0.35,0.55,0.3),iron); anvNeck.position.set(0.7,0.5,0.2); g.add(anvNeck);
  // workbench
  const bench=new THREE.Mesh(new THREE.BoxGeometry(1.6,0.16,0.7),wood); bench.position.set(0.9,0.95,-0.9); bench.castShadow=true; g.add(bench);
  for(const bx of[0.2,1.6]) for(const bz of[-0.6,-1.2]){ const leg=new THREE.Mesh(new THREE.BoxGeometry(0.12,0.9,0.12),wood); leg.position.set(bx,0.45,bz); g.add(leg); }
  const spr=textSprite("⚒️","#ffd27a"); spr.scale.set(1.3,1.3,1); spr.position.set(0,2.7,0); g.add(spr);
  scene.add(g); return g;
}
function buildTreeMesh(x,y,z,wood){
  const g=new THREE.Group(); g.position.set(x,y,z);
  const bark=new THREE.MeshStandardMaterial({color:0x6a4a2c,roughness:0.95});
  const trunk=new THREE.Mesh(new THREE.CylinderGeometry(0.28,0.42,3.0,7),bark); trunk.position.y=1.5; trunk.castShadow=true; g.add(trunk);
  const canopy=new THREE.Group();
  const leaf=new THREE.MeshStandardMaterial({color:0x2f6a34,roughness:0.9});
  for(let i=0;i<3;i++){ const b=new THREE.Mesh(new THREE.IcosahedronGeometry(1.1-i*0.18,0),leaf); b.position.set(rnd(-0.4,0.4),2.8+i*0.7,rnd(-0.4,0.4)); b.castShadow=true; canopy.add(b); }
  g.add(canopy);
  const stump=new THREE.Mesh(new THREE.CylinderGeometry(0.34,0.44,0.5,7),bark); stump.position.y=0.25; stump.visible=false; g.add(stump);
  const spr=textSprite("🪓","#d8c088"); spr.scale.set(0.8,0.8,1); spr.position.y=4.2; g.add(spr);
  scene.add(g);
  return {x,z,mesh:g,canopy,stump,spr,depleted:false,readyAt:0};
}
function waterNear(x,z){ for(let a=0;a<6.28;a+=0.8){ if(terrainH(x+Math.cos(a)*4.5,z+Math.sin(a)*4.5)<-0.2) return true; } return false; }
function buildCraftWorld(){
  if(_craftWorldBuilt) return;   // #793: build once — a dedicated flag, not craftStations.length (which a partial/failed build or an external reset could leave falsy, letting the whole build — and its setInterval — run twice)
  _craftWorldBuilt=true;
  // 1) a Crafting Forge in / beside every town
  for(const c of CITIES){
    let px=null,py=0,pz=0;
    for(const ang of [0.4,1.3,2.2,3.1,4.0,4.9,5.8,0.9]){
      const x=c.x+Math.cos(ang)*15, z=c.z+Math.sin(ang)*15, h=terrainH(x,z);
      if(h>1.1 && Math.abs(terrainH(x+2,z)-h)<2.2 && Math.abs(terrainH(x,z+2)-h)<2.2){ px=x;py=h;pz=z; break; }
    }
    if(px==null){ px=c.x+15; pz=c.z; py=Math.max(1,terrainH(px,pz)); }
    const mesh=buildStationMesh(px,py,pz);
    craftStations.push({x:px,z:pz,y:py,mesh,town:c.name});
  }
  // 2) TYPED ore deposits — every one of the nine metals, placed where its area level fits: iron & dull
  //    copper ring the towns, valorite & verite lie deep in the Direlands. Quotas guarantee each metal is
  //    accessible and spread across Dereth (deposits regrow as the same metal via the node respawn tick).
  const _oreQuota={iron:32,dullcopper:26,shadowiron:24,copper:22,bronze:20,gold:18,agapite:16,verite:14,valorite:14};
  const WSC=(typeof WSCALE!=="undefined"?WSCALE:1);
  _orePlaced={};
  for(const M of UO_METALS){
    const want=_oreQuota[M.k]||16; let got=0,tr=0;
    const baseDist=120+M.req*8;                 // distance-from-town (base units) where oreAreaLevel ≈ M.req
    const loA=M.req-15, hiA=(M.req>=80?400:M.req+24);
    while(got<want && tr<want*160){ tr++;
      const c=CITIES[irnd(0,CITIES.length-1)];
      const ring=(baseDist+rnd(-45,70))*WSC, a=rnd(0,6.28);
      const x=clamp(c.x+Math.cos(a)*ring,-HALF+14,HALF-14), z=clamp(c.z+Math.sin(a)*ring,-HALF+14,HALF-14), h=terrainH(x,z);
      if(h<1.2||h>240) continue;                                  // land only, not lakebed/peak
      if(typeof nearestCapitalDist==="function" && nearestCapitalDist(x,z)<24*WSC) continue;   // not inside a town
      const al=oreAreaLevel(x,z); if(al<loA||al>hiA) continue;    // spot's difficulty must fit this metal's tier
      addOreDeposit(x,z,M.k); got++;
    }
    _orePlaced[M.k]=got;
  }
  // 3) choppable trees in wooded land
  let placed=0,tries=0;
  while(placed<130 && tries<3000){ tries++;
    const x=rnd(-HALF+14,HALF-14), z=rnd(-HALF+14,HALF-14), h=terrainH(x,z);
    if(h>1.6 && h<30 && (typeof nearestCapitalDist!=="function"||nearestCapitalDist(x,z)>30)){ lumberNodes.push(buildTreeMesh(x,h,z)); placed++; }
  }
  // 4) fishing spots along shorelines — the shore is a 1-D curve, so march OUTWARD from a
  //    random water cell until we cross onto land, and drop the spot right at the water's edge
  placed=0; tries=0;
  while(placed<50 && tries<8000){ tries++;
    const x=rnd(-HALF+25,HALF-25), z=rnd(-HALF+25,HALF-25);
    if(terrainH(x,z)>-1) continue;                       // start in water
    const a=rnd(0,6.28), dx=Math.cos(a), dz=Math.sin(a);
    let fx=x,fz=z,found=false;
    for(let s=0;s<40;s++){ fx+=dx*2.5; fz+=dz*2.5; if(Math.abs(fx)>HALF-12||Math.abs(fz)>HALF-12) break;
      const h=terrainH(fx,fz); if(h>0.4 && h<8){ found=true; break; } if(h>20) break; }
    if(!found) continue;
    const h=terrainH(fx,fz);
    const g=new THREE.Group(); g.position.set(fx,Math.max(0.05,h),fz);
    const ring=new THREE.Mesh(new THREE.TorusGeometry(0.5,0.06,6,16),new THREE.MeshBasicMaterial({color:0x6fd0e0,transparent:true,opacity:0.7})); ring.rotation.x=-Math.PI/2; ring.position.y=0.05; g.add(ring);
    const spr=textSprite("🎣","#8fe0f0"); spr.scale.set(0.8,0.8,1); spr.position.y=1.4; g.add(spr);
    scene.add(g); fishSpots.push({x:fx,z:fz,mesh:g,readyAt:0}); placed++;
  }
  // lazy tree-regrow tick (cheap; restores canopies you're not standing next to)
  if(_treeRegrowTimer) clearInterval(_treeRegrowTimer);   // #793: never leak a second regrow interval
  _treeRegrowTimer=setInterval(()=>{ const t0=now(); for(const t of lumberNodes){ if(t.depleted&&t0>t.readyAt){ t.depleted=false; if(t.canopy)t.canopy.visible=true; if(t.stump)t.stump.visible=false; if(t.spr)t.spr.visible=true; } } },4000);
}

// ---- RECIPE CATALOG (built with loops) ----
const _AS_SLOTS=[{slot:"head",suf:"Helm",vMul:0.55,n:2},{slot:"chest",suf:"Hauberk",vMul:1.0,n:6},{slot:"upperarm",suf:"Pauldrons",vMul:0.55,n:2},{slot:"lowerarm",suf:"Vambraces",vMul:0.45,n:2},{slot:"hands",suf:"Gauntlets",vMul:0.4,n:2},{slot:"legs",suf:"Leggings",vMul:0.8,n:4},{slot:"feet",suf:"Sabatons",vMul:0.45,n:2}];
const _LE_SLOTS=[{slot:"head",suf:"Cap",vMul:0.5,n:2},{slot:"chest",suf:"Tunic",vMul:1.0,n:5},{slot:"upperarm",suf:"Sleeves",vMul:0.5,n:2},{slot:"hands",suf:"Gloves",vMul:0.4,n:2},{slot:"legs",suf:"Leggings",vMul:0.8,n:4},{slot:"feet",suf:"Boots",vMul:0.45,n:2}];
function _mprefix(tier){ if(!tier) return ""; if(tier.k==="iron"||tier.k==="regular"||tier.k==="leather") return ""; return tier.n+" "; }
function _workRoll(skill,exc){ const l=craftLvl(skill); return Math.min(10, Math.max(1, Math.round(l/12)+(exc?2:0)+irnd(0,1))); }
const UO_RECIPES=[];
function _addWeapon(skill,matKind,ico,list){ for(const w of list) UO_RECIPES.push({id:skill+"_"+(w.id||w.wt),skill,tab:skill,name:w.name,ico:ico||"⚔️",matKind,n:w.n,extraBoard:w.board||0,min:w.min||0,
  make:(tier,exc)=>{ const wm=rnd(0.95,1.08); const it={name:_mprefix(tier)+w.name,stat:"weapon",wt:w.wt,v:Math.max(1,Math.round(w.base*(tier?tier.dmg:1)*(exc?1.18:1)*wm)),mat:(tier?tier.n:"Iron"),work:_workRoll(skill,exc)}; if(w.shield){it.stat="worn";it.aslot="offhand";it.shield=true;it.at=w.at;delete it.wt;} return it; } }); }
function _addArmor(skill,matKind,setL,slots,ico){ for(const s of setL) for(const sl of slots) UO_RECIPES.push({id:skill+"_"+s.set.toLowerCase().replace(/ /g,"")+"_"+sl.slot,skill,tab:skill,name:s.set+" "+sl.suf,ico:ico||"🛡",matKind,n:sl.n,min:s.min||0,
  make:(tier,exc)=>{ const wm=rnd(0.95,1.08); return {name:_mprefix(tier)+s.set+" "+sl.suf,stat:"worn",at:s.at,aslot:sl.slot,v:Math.max(1,Math.round(s.base*sl.vMul*(tier?tier.arm:1)*(exc?1.15:1)*wm)),mat:(tier?tier.n:"Iron"),work:_workRoll(skill,exc)}; } }); }
// Blacksmithy — weapons, metal armour, metal shields (matKind: ingot)
_addWeapon("blacksmithy","ingot","⚔️",[
  {id:"dagger",name:"Dagger",wt:"dagger",base:12,n:2,min:0},{id:"shortsword",name:"Short Sword",wt:"sword",base:18,n:4,min:8},
  {id:"longsword",name:"Long Sword",wt:"sword",base:22,n:6,min:22},{id:"spear",name:"War Spear",wt:"spear",base:20,n:5,min:18},
  {id:"battleaxe",name:"Battle Axe",wt:"axe",base:24,n:6,min:30},{id:"warmace",name:"War Mace",wt:"mace",base:26,n:6,min:34},
  {id:"greatsword",name:"Greatsword",wt:"twohand",base:34,n:9,min:52},
  {id:"heater",name:"Heater Shield",wt:"_sh",shield:true,at:"heavy",base:14,n:5,min:24},{id:"kite",name:"Kite Shield",wt:"_sh",shield:true,at:"heavy",base:16,n:6,min:40},
]);
_addArmor("blacksmithy","ingot",[{set:"Ringmail",at:"medium",base:11,min:12},{set:"Chainmail",at:"medium",base:13,min:28},{set:"Platemail",at:"heavy",base:16,min:54}],_AS_SLOTS,"🛡");
// Tailoring — leather armour (matKind: leather)
_addArmor("tailoring","leather",[{set:"Leather",at:"light",base:9,min:0},{set:"Studded Leather",at:"light",base:12,min:34}],_LE_SLOTS,"🧥");
// Carpentry — wooden weapons, wooden shield, furniture (matKind: board)
_addWeapon("carpentry","board","🪵",[
  {id:"club",name:"Club",wt:"mace",base:15,n:3,min:0},{id:"qstaff",name:"Quarterstaff",wt:"staff",base:19,n:4,min:12},
  {id:"wshield",name:"Wooden Shield",wt:"_sh",shield:true,at:"medium",base:11,n:5,min:6},
]);
for(const f of [{id:"chair",name:"Wooden Chair",n:6,min:8},{id:"table",name:"Wooden Table",n:10,min:18},{id:"stool",name:"Wooden Stool",n:4,min:0}])
  UO_RECIPES.push({id:"carpentry_"+f.id,skill:"carpentry",tab:"carpentry",name:f.name,ico:"🪑",matKind:"board",n:f.n,min:f.min,
    make:(tier,exc)=>({name:_mprefix(tier)+f.name,stat:"furniture",kind:f.id,v:Math.max(4,Math.round(f.n*3*(tier?tier.val:1))),work:_workRoll("carpentry",exc)})});
// Fletching — bows & ammunition (matKind: board)
_addWeapon("fletching","board","🏹",[
  {id:"shortbow",name:"Short Bow",wt:"bow",base:18,n:4,min:0},{id:"longbow",name:"Long Bow",wt:"bow",base:24,n:6,min:30},{id:"crossbow",name:"Crossbow",wt:"crossbow",base:28,n:7,min:46},
]);
for(const am of [{id:"arrows",name:"Arrows",for:"bow",min:0},{id:"bolts",name:"Crossbow Bolts",for:"crossbow",min:20}])
  UO_RECIPES.push({id:"fletching_"+am.id,skill:"fletching",tab:"fletching",name:am.name,ico:"🏹",matKind:"board",n:1,min:am.min,
    make:(tier,exc)=>({name:_mprefix(tier)+am.name,stat:"ammo",for:am.for,dmg:Math.round(3*(tier?tier.dmg:1)),element:null,count:(exc?30:20),ico:"🏹"})});
// Tinkering — tools, jewellery, lockpicks (matKind: ingot, some boards)
const _TOOLS=[
  {id:"pickaxe",name:"Pickaxe",tool:"mining",uses:220,ico:"⛏️",n:3,min:0},
  {id:"hatchet",name:"Hatchet",tool:"lumberjack",uses:220,ico:"🪓",n:3,min:0},
  {id:"pole",name:"Fishing Pole",tool:"fishing",uses:180,ico:"🎣",matKind:"board",n:2,min:0},
  {id:"hammer",name:"Smith's Hammer",tool:"blacksmithy",uses:120,ico:"🔨",n:4,min:15},
  {id:"kit",name:"Sewing Kit",tool:"tailoring",uses:120,ico:"🧵",n:2,min:15},
  {id:"saw",name:"Saw",tool:"carpentry",uses:120,ico:"🪚",n:2,min:15},
  {id:"ttools",name:"Tinker's Tools",tool:"tinkering",uses:120,ico:"🛠️",n:3,min:20},
  {id:"ftools",name:"Fletcher's Tools",tool:"fletching",uses:120,ico:"🗡️",n:2,min:15},
];
for(const t of _TOOLS) UO_RECIPES.push({id:"tinkering_"+t.id,skill:"tinkering",tab:"tinker",name:t.name,ico:t.ico,matKind:t.matKind||"ingot",n:t.n,min:t.min,exc:false,
  make:(tier,exc)=>toolItem(t.tool,t.name,t.uses,t.ico)});
UO_RECIPES.push({id:"tinkering_ring",skill:"tinkering",tab:"tinker",name:"Jewelled Ring",ico:"💍",matKind:"ingot",n:2,min:38,
  make:(tier,exc)=>({name:_mprefix(tier)+"Ring",stat:"jewelry",jtype:"finger",bonus:{kind:"vital",vs:"mhp",v:Math.round((exc?18:12)*(tier?tier.val*0.4+0.8:1))}})});
UO_RECIPES.push({id:"tinkering_brace",skill:"tinkering",tab:"tinker",name:"Jewelled Bracelet",ico:"⌚",matKind:"ingot",n:2,min:44,
  make:(tier,exc)=>({name:_mprefix(tier)+"Bracelet",stat:"jewelry",jtype:"wrist",bonus:{kind:"vital",vs:"mst",v:Math.round((exc?18:12)*(tier?tier.val*0.4+0.8:1))}})});
UO_RECIPES.push({id:"tinkering_lockpick",skill:"tinkering",tab:"tinker",name:"Lockpicks (×3)",ico:"🔓",matKind:"ingot",n:1,min:10,exc:false,
  make:(tier,exc)=>({name:"Lockpick",stat:"pick",v:1,count:3})});
// Cooking (raw fish → cooked; bread/rations from materials)
UO_RECIPES.push({id:"cooking_fish",skill:"cooking",tab:"cook",name:"Cooked Fish",ico:"🍳",matKind:"fish",n:1,min:0,exc:false,
  make:(tier,exc,src)=>({name:"Cooked "+(src&&src.species?src.species:"Fish"),stat:"food",v:(src?src.v+3:5),attr:"Endurance",dur:(src?src.dur*2:180),ico:"🍖"})});
UO_RECIPES.push({id:"cooking_meat",skill:"cooking",tab:"cook",name:"Roast Meat",ico:"🍖",matKind:"meat",n:1,min:0,exc:false,
  make:(tier,exc,src)=>({name:"Roast "+((src&&src.name)?src.name.replace("Raw ",""):"Meat"),stat:"food",v:(src?src.v+4:6),attr:"Strength",dur:(src?src.dur*2:200),ico:"🍖"})});
UO_RECIPES.push({id:"cooking_bread",skill:"cooking",tab:"cook",name:"Loaf of Bread",ico:"🍞",matKind:"materials",n:4,min:4,exc:false,
  make:()=>({name:"Loaf of Bread",stat:"food",v:3,attr:"Strength",dur:150,ico:"🍞"})});
UO_RECIPES.push({id:"cooking_rations",skill:"cooking",tab:"cook",name:"Trail Rations",ico:"🥖",matKind:"materials",n:6,min:14,exc:false,
  make:()=>({name:"Trail Rations",stat:"food",v:4,attr:"Endurance",dur:240,ico:"🥖"})});
// Alchemy (materials → potions)
for(const p of [{id:"health",name:"Health Potion",mats:8,st:"hp",v:60,min:0},{id:"mana",name:"Mana Potion",mats:8,st:"mn",v:60,min:0},{id:"stam",name:"Stamina Potion",mats:6,st:"st",v:60,min:0},{id:"great",name:"Greater Healing Potion",mats:16,st:"hp",v:150,min:28}])
  UO_RECIPES.push({id:"alchemy_"+p.id,skill:"alchemy",tab:"alch",name:p.name,ico:"🧪",matKind:"materials",n:p.mats,min:p.min,exc:false,
    make:()=>({name:p.name,stat:p.st,v:p.v})});

// ---- difficulty / success / craft attempt ----
function recipeDiff(rec,tier){ return (rec.min||0) + (tier?tier.req*0.45:0); }
function successChance(rec,tier){ return clamp(0.35 + (craftLvl(rec.skill)-recipeDiff(rec,tier))*0.02, 0.05, 0.98); }
function excChance(rec,tier){ if(rec.exc===false) return 0; return clamp((craftLvl(rec.skill)-recipeDiff(rec,tier)-12)*0.012,0,0.35) + (skillTier(rec.skill)>=2?0.05:0); }
function availTiers(matKind){ // {key,name,count} the player currently carries
  const ladder=matKind==="ingot"?UO_METALS:matKind==="board"?UO_WOODS:matKind==="leather"?UO_LEATHERS:null;
  if(!ladder) return [];
  const out=[]; for(const t of ladder){ const nm=resNameFor(matKind,t.k); const c=invCountOf(nm); if(c>0) out.push({key:t.k,name:t.n||"Common",count:c}); } return out;
}
function firstRawFish(){ for(let i=0;i<player.inv.length;i++){ const it=player.inv[i]; if(it&&it.fish&&it.raw) return {it,i}; } return null; }
function firstRawMeat(){ for(let i=0;i<player.inv.length;i++){ const it=player.inv[i]; if(it&&it.meat&&it.raw) return {it,i}; } return null; }
function attemptCraft(rec,tierKey){
  if(!nearCraftStation()){ log("Stand at a <b>Crafting Forge</b> to craft.","warn"); return; }
  const matKind=rec.matKind;
  const tier=matKind==="ingot"?UO_METAL_BY[tierKey]:matKind==="board"?UO_WOOD_BY[tierKey]:matKind==="leather"?UO_LEATHER_BY[tierKey]:null;
  // ingredient checks
  let srcFish=null, srcMeat=null;
  if(matKind==="ingot"||matKind==="board"||matKind==="leather"){ if(!tier){ log("Choose a material.","warn"); return; }
    const nm=resNameFor(matKind,tierKey); if(invCountOf(nm)<rec.n){ log(`Not enough <b>${nm}</b> (need ${rec.n}).`,"warn"); if(SFX&&SFX.deny)SFX.deny(); return; } }
  else if(matKind==="materials"){ if(player.materials<rec.n){ log(`Not enough materials (need ${rec.n}).`,"warn"); if(SFX&&SFX.deny)SFX.deny(); return; } }
  else if(matKind==="fish"){ srcFish=firstRawFish(); if(!srcFish){ log("You have no raw fish to cook.","warn"); if(SFX&&SFX.deny)SFX.deny(); return; } }
  else if(matKind==="meat"){ srcMeat=firstRawMeat(); if(!srcMeat){ log("You have no raw meat to cook.","warn"); if(SFX&&SFX.deny)SFX.deny(); return; } }
  if(rec.extraBoard){ const bn=resNameFor("board","regular"); if(invCountOf(bn)<rec.extraBoard){ log(`Also needs ${rec.extraBoard} ${bn}.`,"warn"); return; } }
  // tool
  if(rec.skill!=="cooking" && rec.skill!=="alchemy" && !hasCraftTool(rec.skill)){ log(`You need the right tool for <b>${SKILL_BY_KEY[rec.skill].n}</b> (buy one in the Tools tab).`,"warn"); if(SFX&&SFX.deny)SFX.deny(); return; }
  // consume ingredients up-front
  if(matKind==="ingot"||matKind==="board"||matKind==="leather") removeItemsByName(resNameFor(matKind,tierKey),rec.n);
  else if(matKind==="materials") player.materials-=rec.n;
  else if(matKind==="fish") takeFromInv(srcFish.i);
  else if(matKind==="meat") takeFromInv(srcMeat.i);
  if(rec.extraBoard) removeItemsByName(resNameFor("board","regular"),rec.extraBoard);
  if(rec.skill!=="cooking" && rec.skill!=="alchemy") consumeCraftTool(rec.skill);
  startAction("craft",1.1);
  gainCraft(rec.skill, 0.6);
  if(Math.random()>successChance(rec,tier)){ log(`You fail and ruin the materials.`,"warn"); if(SFX&&SFX.deny)SFX.deny(); updateHUD(); if(_cmOpen) renderCraftMenu(); return; }
  const exc=Math.random()<excChance(rec,tier);
  const item=rec.make(tier,exc,(srcFish&&srcFish.it)||(srcMeat&&srcMeat.it)); item.crafted=true;
  if(exc){ item.exceptional=true; item.maker=player.name||"Adventurer"; if(item.v) item.v=Math.round(item.v); }
  if(!addToInv(item)){ addDrop(player.x,player.z,{type:"item",item}); log("Pack full — set at your feet.","warn"); }
  gainCraft(rec.skill, exc?1.6:1.0);
  log(`You craft ${exc?"an <b>exceptional</b> ":"a "}<b>${item.name}</b>${exc?` <span style="color:#ffd86b">✦ signed by ${esc(item.maker)}</span>`:""}.`,"loot");
  if(SFX&&SFX.gold)SFX.gold(); updateHUD(); saveGame(); if(_cmOpen) renderCraftMenu();
}
function refineAll(kind){ // ore→ingot / log→board / hide→leather (1:1)
  if(!nearCraftStation()){ log("Stand at a <b>Crafting Forge</b> to refine.","warn"); return; }
  const src=kind==="ingot"?"ore":kind==="board"?"log":"hide";
  const ladder=kind==="ingot"?UO_METALS:kind==="board"?UO_WOODS:UO_LEATHERS;
  let total=0;
  for(const t of ladder){ const nm=resNameFor(src,t.k), c=invCountOf(nm); if(c>0){ removeItemsByName(nm,c); const out=resItem(kind,t.k,c); if(!addToInv(out)) addDrop(player.x,player.z,{type:"item",item:out}); total+=c; } }
  if(!total){ log(`You have no ${src}s to refine.`,"warn"); return; }
  const verb=kind==="ingot"?"smelt":kind==="board"?"mill":"tan";
  log(`You ${verb} <b>${total}</b> ${src}${total>1?"s":""}.`,"loot"); if(SFX&&SFX.gold)SFX.gold(); startAction("craft",0.9); updateHUD(); saveGame(); if(_cmOpen) renderCraftMenu();
}
// tool shop (buy for pyreals)
const UO_SHOP=[
  {tool:"mining",name:"Pickaxe",uses:220,ico:"⛏️",cost:150},{tool:"lumberjack",name:"Hatchet",uses:220,ico:"🪓",cost:150},
  {tool:"fishing",name:"Fishing Pole",uses:180,ico:"🎣",cost:120},{tool:"blacksmithy",name:"Smith's Hammer",uses:120,ico:"🔨",cost:220},
  {tool:"tailoring",name:"Sewing Kit",uses:120,ico:"🧵",cost:200},{tool:"carpentry",name:"Saw",uses:120,ico:"🪚",cost:180},
  {tool:"tinkering",name:"Tinker's Tools",uses:120,ico:"🛠️",cost:240},{tool:"fletching",name:"Fletcher's Tools",uses:120,ico:"🗡️",cost:200},
];
function buyTool(i){ const t=UO_SHOP[i]; if(!t) return; if(player.gold<t.cost){ log("Not enough pyreals.","warn"); if(SFX&&SFX.deny)SFX.deny(); return; }
  player.gold-=t.cost; addToInv(toolItem(t.tool,t.name,t.uses,t.ico)); log(`You buy a <b>${t.name}</b> (${t.uses} uses).`,"loot"); if(SFX&&SFX.gold)SFX.gold(); updateHUD(); saveGame(); if(_cmOpen) renderCraftMenu(); }

// ---- CRAFT MENU UI ----
let _cmOpen=false, _cmTab="refine", _cmSel={ingot:null,board:null,leather:null};
const _CM_TABS=[["refine","Refine","⚒️"],["blacksmithy","Blacksmithy","⚔️"],["tailoring","Tailoring","🧥"],["carpentry","Carpentry","🪑"],["fletching","Fletching","🏹"],["tinker","Tinkering","🛠️"],["cook","Cooking","🍳"],["alch","Alchemy","🧪"],["tools","Tools","🛒"]];
function openCraftMenu(){
  if(typeof closeOtherModals==="function") closeOtherModals("craftmenu");
  let ov=document.getElementById('craftmenu');
  if(!ov){ ov=document.createElement('div'); ov.id='craftmenu';
    ov.style.cssText="display:none;position:fixed;inset:0;z-index:260;align-items:center;justify-content:center;background:rgba(0,0,0,.6)";
    document.body.appendChild(ov);
    ov.addEventListener('click',e=>{ if(e.target===ov) closeCraftMenu(); });
  }
  _cmOpen=true; ov.style.display="flex"; paused=true; if(locked)document.exitPointerLock();
  renderCraftMenu();
}
function closeCraftMenu(){ const ov=document.getElementById('craftmenu'); if(ov) ov.style.display="none"; _cmOpen=false; paused=false; }
function _tierPicker(matKind){
  const tiers=availTiers(matKind); if(!tiers.length) return `<span style="color:#a06a6a">no ${matKind} — gather &amp; refine first</span>`;
  if(!_cmSel[matKind]||!tiers.find(t=>t.key===_cmSel[matKind])) _cmSel[matKind]=tiers[tiers.length-1].key;
  return tiers.map(t=>`<button data-tier="${matKind}:${t.key}" class="cmTier${_cmSel[matKind]===t.key?' on':''}">${esc(t.name)} <span style="opacity:.6">×${t.count}</span></button>`).join("");
}
function renderCraftMenu(){
  const ov=document.getElementById('craftmenu'); if(!ov) return;
  const at=nearCraftStation();
  const trades=UO_TRADES.map(k=>`<span class="cmLv"><b>${esc(SKILL_BY_KEY[k].n)}</b> ${craftLvl(k)}</span>`).join("");
  let body="";
  if(_cmTab==="refine"){
    body=`<div class="cmHint">Smelt ore into ingots, mill logs into boards, tan hides into leather. (Gather ore at ⛏ veins, chop 🪓 trees, skin slain beasts.)</div>`;
    const rows=[["ingot","Smelt ore → ingots","ore","🔩"],["board","Mill logs → boards","log","🟫"],["leather","Tan hides → leather","hide","🟫"]];
    for(const [kind,label,src,ico] of rows){ const ladder=kind==="ingot"?UO_METALS:kind==="board"?UO_WOODS:UO_LEATHERS;
      let have=0; for(const t of ladder) have+=invCountOf(resNameFor(src,t.k));
      body+=`<div class="cmRow"><span class="cmI">${ico}</span><span class="cmN">${label}</span><span class="cmMid">carrying ${have} ${src}${have===1?"":"s"}</span><button data-refine="${kind}" class="cmBtn"${have?"":" disabled"}>Refine all</button></div>`;
    }
  } else if(_cmTab==="tools"){
    body=`<div class="cmHint">Buy the tool a craft needs (each has limited uses, then breaks). Tinkers can also <i>craft</i> tools.</div>`;
    UO_SHOP.forEach((t,i)=>{ const have=findCraftTool(t.tool); body+=`<div class="cmRow"><span class="cmI">${t.ico}</span><span class="cmN">${esc(t.name)}${have?` <span style="color:#7fd07f">(have ${have.uses})</span>`:""}</span><span class="cmMid">${t.uses} uses</span><button data-buy="${i}" class="cmBtn"${player.gold>=t.cost?"":" disabled"}>${t.cost}p</button></div>`; });
  } else {
    const list=UO_RECIPES.filter(r=>r.tab===_cmTab);
    const kinds=[...new Set(list.map(r=>r.matKind).filter(k=>k==="ingot"||k==="board"||k==="leather"))];
    if(kinds.length) body+=`<div class="cmPick">${kinds.map(k=>`<span class="cmPickL">${k==="ingot"?"Metal":k==="board"?"Wood":"Leather"}:</span> ${_tierPicker(k)}`).join('<span style="margin:0 8px"></span>')}</div>`;
    for(const r of list){
      const tk=r.matKind==="ingot"||r.matKind==="board"||r.matKind==="leather"?_cmSel[r.matKind]:null;
      const tier=r.matKind==="ingot"?UO_METAL_BY[tk]:r.matKind==="board"?UO_WOOD_BY[tk]:r.matKind==="leather"?UO_LEATHER_BY[tk]:null;
      const sc=Math.round(successChance(r,tier)*100), xc=Math.round(excChance(r,tier)*100);
      let cost = r.matKind==="materials"?`${r.n} mat`: r.matKind==="fish"?"1 raw fish": r.matKind==="meat"?"1 raw meat": tier?`${r.n} ${esc(tier.n||"Common")}`:(tk?`${r.n}`:`${r.n} — pick a material`);
      const canSkill=craftLvl(r.skill)>=Math.floor(recipeDiff(r,tier));
      const tool=(r.skill==="cooking"||r.skill==="alchemy")||hasCraftTool(r.skill);
      body+=`<div class="cmRow"><span class="cmI">${r.ico}</span><span class="cmN">${esc(r.name)}<span class="cmSub">${cost}${r.extraBoard?` + ${r.extraBoard} Board`:""} · needs ${SKILL_BY_KEY[r.skill].n} ${Math.floor(recipeDiff(r,tier))}${r.exc!==false?` · <span style="color:#ffd86b">${xc}% exc</span>`:""}</span></span><span class="cmMid ${canSkill?'':'cmBad'}">${sc}%${tool?"":' · <span class="cmBad">no tool</span>'}</span><button data-craft="${r.id}" class="cmBtn"${at?"":" disabled"}>Craft</button></div>`;
    }
  }
  ov.innerHTML=`<div class="cmWrap">
    <style>
      .cmWrap{max-width:760px;width:94%;max-height:88vh;display:flex;flex-direction:column;background:#14161d;border:1px solid var(--gold,#c8a24a);border-radius:12px;overflow:hidden;font-size:13px;color:#e8dcc0}
      .cmHead{padding:12px 16px;border-bottom:1px solid rgba(255,255,255,.08);display:flex;align-items:center;gap:10px;flex-wrap:wrap}
      .cmHead h2{margin:0;color:var(--gold,#e8c060);font-size:15px;letter-spacing:.5px;text-transform:uppercase}
      .cmLv{font-size:11px;color:#b8ad90;background:rgba(255,255,255,.04);padding:2px 7px;border-radius:20px}
      .cmLv b{color:#e8dcc0;font-weight:600}
      .cmX{margin-left:auto;cursor:pointer;color:#c8b890;font-size:20px;line-height:1;background:none;border:none}
      .cmTabs{display:flex;flex-wrap:wrap;gap:4px;padding:8px 12px;border-bottom:1px solid rgba(255,255,255,.08)}
      .cmTab{cursor:pointer;background:rgba(255,255,255,.04);border:1px solid transparent;border-radius:8px;color:#cdbf9a;padding:6px 10px;font-size:12px}
      .cmTab.on{background:rgba(200,162,74,.18);border-color:var(--gold,#c8a24a);color:#ffe8b0}
      .cmScroll{overflow-y:auto;padding:10px 14px}
      .cmHint{color:#9a9078;font-size:12px;margin:2px 2px 10px;line-height:1.4}
      .cmPick{margin:2px 2px 12px;line-height:2}
      .cmPickL{color:#b8ad90;font-size:11px;text-transform:uppercase;letter-spacing:.4px}
      .cmTier{cursor:pointer;background:rgba(255,255,255,.05);border:1px solid var(--border,#3a3830);border-radius:7px;color:#d8cba0;padding:3px 9px;margin:0 3px 3px 0;font-size:12px}
      .cmTier.on{background:rgba(200,162,74,.22);border-color:var(--gold,#c8a24a);color:#ffe8b0}
      .cmRow{display:flex;align-items:center;gap:10px;padding:8px 6px;border-bottom:1px solid rgba(255,255,255,.05)}
      .cmI{font-size:20px;width:26px;text-align:center;flex:none}
      .cmN{flex:1;min-width:0}
      .cmSub{display:block;color:#8f866c;font-size:11px;margin-top:1px}
      .cmMid{flex:none;width:120px;text-align:right;color:#a8cfa0;font-size:12px}
      .cmBad{color:#d08a6a}
      .cmBtn{flex:none;background:linear-gradient(180deg,#c8a24a,#9a7a2a);border:none;border-radius:7px;color:#241c08;font-weight:700;padding:6px 12px;cursor:pointer;font-size:12px}
      .cmBtn[disabled]{opacity:.35;cursor:not-allowed;filter:grayscale(.5)}
    </style>
    <div class="cmHead"><h2>⚒️ Crafting Forge</h2>${trades}<button class="cmX" id="cmClose">✕</button></div>
    <div class="cmTabs">${_CM_TABS.map(t=>`<button class="cmTab${_cmTab===t[0]?' on':''}" data-tab="${t[0]}">${t[2]} ${t[1]}</button>`).join("")}</div>
    ${at?"":'<div style="padding:8px 16px;color:#d08a6a;background:rgba(208,138,106,.08);font-size:12px">⚠ Walk up to a Crafting Forge (in any town) to craft — you can browse from anywhere.</div>'}
    <div class="cmScroll">${body}</div></div>`;
  ov.querySelector('#cmClose').onclick=closeCraftMenu;
  ov.querySelectorAll('[data-tab]').forEach(b=>b.onclick=()=>{ _cmTab=b.dataset.tab; renderCraftMenu(); });
  ov.querySelectorAll('[data-tier]').forEach(b=>b.onclick=()=>{ const [mk,tk]=b.dataset.tier.split(":"); _cmSel[mk]=tk; renderCraftMenu(); });
  ov.querySelectorAll('[data-refine]').forEach(b=>b.onclick=()=>refineAll(b.dataset.refine));
  ov.querySelectorAll('[data-buy]').forEach(b=>b.onclick=()=>buyTool(+b.dataset.buy));
  ov.querySelectorAll('[data-craft]').forEach(b=>b.onclick=()=>{ const r=UO_RECIPES.find(x=>x.id===b.dataset.craft); if(r) attemptCraft(r, r.matKind==="ingot"||r.matKind==="board"||r.matKind==="leather"?_cmSel[r.matKind]:null); });
}
if(typeof BIG_MODALS!=="undefined" && BIG_MODALS.indexOf('craftmenu')<0) BIG_MODALS.push('craftmenu');
