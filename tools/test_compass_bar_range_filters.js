// The top compass BAR must cull blips by distance, not just bearing, and must honour per-category
// filters — matching the round minimap's reach by default.
//
// Before this, drawCompass()'s marker() culled on ANGLE only (|bearing| <= 70 deg). A world boss on
// the far side of the continent still painted a dot on the bar, and there was no way to turn any
// category off. The round minimap already had per-category filters (miniShow) and a radius
// (miniZoom); the bar had neither.
//
// Two subtleties this pins down:
//   * The minimap's real visible radius is miniZoom*72/73, NOT miniZoom: drawMinimap uses S=150 ->
//     R=73, and its inR() admits pixels within (R-1). barBlipRange() must equal that exactly, or
//     "same distance as the compass in the top right" is a lie by ~1.4%.
//   * The dungeon EXIT marker and the quest arrow are navigation aids and must NEVER be range-culled;
//     in a big delve the exit is routinely farther away than the bar's range.
//
// Blips are the only ctx.arc() calls in drawCompass (cardinals are strokes, the quest arrow is a
// lineTo path), so instrumenting arc() gives an exact blip count.
//
// Run:  node tools/test_compass_bar_range_filters.js   (needs: npm i puppeteer-core, Chrome/Chromium,
//        and a static server for the repo root: python3 -m http.server 8791)
const puppeteer=require('puppeteer-core');
const CHROME=process.env.CHROME||"/opt/pw-browsers/chromium";
const URL=process.env.DERETH_URL||'http://localhost:8791/index.html';

let pass=0,fail=0;
function check(name,ok,detail){ console.log(`${ok?'PASS':'FAIL'}  ${name}${detail?'  — '+detail:''}`); ok?pass++:fail++; }
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const BENIGN=/Pointer Lock|pointerlock/i;
async function waitForMain(page,fn,{timeout=30000,interval=100}={}){
  const t0=Date.now();
  while(Date.now()-t0<timeout){ if(await page.evaluate(fn)) return; await sleep(interval); }
  throw new Error(`waitForMain timed out after ${timeout}ms`);
}

(async()=>{
  const browser=await puppeteer.launch({executablePath:CHROME,headless:'new',
    args:['--no-sandbox','--use-gl=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
  const page=await browser.newPage();
  const errs=[]; page.on('pageerror',e=>{ if(!BENIGN.test(e.message)) errs.push(e.message); });
  await page.goto(URL,{waitUntil:'load',timeout:60000});
  await waitForMain(page,()=>typeof startGame==="function",{timeout:30000});
  await page.evaluate(()=>{ try{ startGame(false,'aluvian'); }catch(e){} });
  await waitForMain(page,()=>typeof player!=="undefined"&&player&&typeof drawCompass==="function"
    &&typeof barBlipRange==="function"&&typeof miniWorldRadius==="function",{timeout:30000});

  const data=await page.evaluate(()=>{
    const out={};

    // ── instrument the compass canvas: blips are the only arc() calls
    const ctx=document.getElementById('compass').getContext('2d');
    const realArc=ctx.arc.bind(ctx);
    let arcs=0;
    ctx.arc=function(...a){ arcs++; return realArc(...a); };
    const blips=fn=>{ arcs=0; fn&&fn(); drawCompass(); return arcs; };   // synchronous: rAF cannot interleave

    // ── snapshot every global drawCompass reads, so we can restore afterwards
    const S={lifestones,boss,worldEvent,monsters,nodes,portals,shops,npcs,activeQuests,inDungeon,
             barRange,barShow:JSON.parse(JSON.stringify(barShow)),miniZoom,
             px:player.x,pz:player.z,yaw:player.yaw};

    // ── the default range must EQUAL the minimap's true visible radius
    out.defaultRangeIsNull = barRange===null;
    out.barRange = barBlipRange();
    out.miniRadius = miniWorldRadius();
    out.miniZoom = miniZoom;
    out.rangeMatchesMinimap = Math.abs(barBlipRange()-miniWorldRadius())<1e-9;
    out.radiusIsNotJustMiniZoom = Math.abs(miniWorldRadius()-miniZoom)>1;   // 72/73 factor is real

    // ── isolate the scene: face north (yaw 0 -> bearing 0); both probes sit due north, in FOV
    player.yaw=0; player.x=0; player.z=0;
    boss=null; worldEvent=null; activeQuests=[]; inDungeon=false;
    monsters=[]; nodes=[]; portals=[]; shops=[]; npcs=[];
    for(const k in barShow) barShow[k]=false;

    const NEAR={x:0,z:-10,bound:true}, FAR={x:0,z:-1e6,bound:true};

    // ── range culling
    barShow.town=true;
    lifestones=[NEAR,FAR];
    out.nearAndFar_defaultRange = blips();          // expect 1: FAR is beyond the minimap radius

    lifestones=[FAR];
    out.farOnly_defaultRange = blips();             // expect 0

    lifestones=[NEAR];
    out.nearOnly_defaultRange = blips();            // expect 1

    // ── CONTROL: the pre-fix bar had no range check. Widen the range past FAR and it reappears,
    //    proving the cull (not some other filter) is what removed it.
    lifestones=[NEAR,FAR];
    barRange=2e6;
    out.control_wideRange_showsBoth = blips();      // expect 2

    // ── an explicit custom range is honoured, and is exclusive at the boundary+
    barRange=5;  lifestones=[NEAR]; out.customRange5  = blips();   // expect 0 (NEAR is 10 away)
    barRange=50; lifestones=[NEAR]; out.customRange50 = blips();   // expect 1
    barRange=null;

    // ── category filters gate each kind independently
    lifestones=[];
    barShow.town=false;
    monsters=[{x:0,z:-10,isBoss:false,isDungeon:false}];
    barShow.monster=false; out.monsterOff = blips();   // expect 0
    barShow.monster=true;  out.monsterOn  = blips();   // expect 1
    // ...and creatures obey the same range
    barRange=5; out.monsterOn_outOfRange = blips();    // expect 0
    barRange=null; monsters=[]; barShow.monster=false;

    shops=[{x:0,z:-10}];
    barShow.vendor=false; out.vendorOff=blips();
    barShow.vendor=true;  out.vendorOn =blips();
    shops=[]; barShow.vendor=false;

    boss={x:0,z:-10};
    barShow.boss=false; out.bossOff=blips();
    barShow.boss=true;  out.bossOn =blips();
    boss=null; barShow.boss=false;

    // ── defaults: the bar's out-of-the-box categories match what it drew before this change
    const defaults={}; for(const [k,,onBar] of BLIP_CATS) defaults[k]=onBar;
    out.defaultOnBar = Object.keys(defaults).filter(k=>defaults[k]).sort().join(",");

    // ── the dungeon EXIT is a navigation aid: never range-culled
    inDungeon=true;
    const savedExit={x:dungeonExit.x,z:dungeonExit.z};
    dungeonExit.x=0; dungeonExit.z=-1e6;             // absurdly far
    barRange=50;
    out.dungeonExit_farButShown = blips();           // expect 1
    dungeonExit.x=savedExit.x; dungeonExit.z=savedExit.z;
    inDungeon=false; barRange=null;

    // ── persistence round-trip
    barRange=1234; barShow.monster=true; saveMiniPrefs();
    barRange=null; barShow.monster=false;
    loadMiniPrefs();
    out.persistedRange = barRange;
    out.persistedMonster = barShow.monster;

    // ── restore everything we touched
    ctx.arc=realArc;
    lifestones=S.lifestones; boss=S.boss; worldEvent=S.worldEvent; monsters=S.monsters; nodes=S.nodes;
    portals=S.portals; shops=S.shops; npcs=S.npcs; activeQuests=S.activeQuests; inDungeon=S.inDungeon;
    barRange=S.barRange; Object.assign(barShow,S.barShow); miniZoom=S.miniZoom;
    player.x=S.px; player.z=S.pz; player.yaw=S.yaw;
    saveMiniPrefs();
    return out;
  });

  check("range.default-tracks-the-minimap", data.defaultRangeIsNull===true && data.rangeMatchesMinimap===true,
    `barRange=${data.defaultRangeIsNull?'null (match)':'custom'}  barBlipRange=${data.barRange.toFixed(3)}  miniWorldRadius=${data.miniRadius.toFixed(3)}`);
  check("range.minimap-radius-is-not-simply-miniZoom", data.radiusIsNotJustMiniZoom===true,
    `miniWorldRadius=${data.miniRadius.toFixed(1)} vs miniZoom=${data.miniZoom} (the (R-1)/R factor)`);

  check("cull.far-blip-is-hidden-at-default-range", data.nearAndFar_defaultRange===1,
    `blips drawn=${data.nearAndFar_defaultRange} (expect 1 of 2)`);
  check("cull.far-only-draws-nothing", data.farOnly_defaultRange===0, `blips=${data.farOnly_defaultRange}`);
  check("cull.near-only-draws-one", data.nearOnly_defaultRange===1, `blips=${data.nearOnly_defaultRange}`);

  check("range.custom-range-too-small-hides-blip", data.customRange5===0, `blips=${data.customRange5} at range 5, target 10 away`);
  check("range.custom-range-large-enough-shows-blip", data.customRange50===1, `blips=${data.customRange50} at range 50`);

  check("filter.creatures-off-hides-them", data.monsterOff===0, `blips=${data.monsterOff}`);
  check("filter.creatures-on-shows-them", data.monsterOn===1, `blips=${data.monsterOn}`);
  check("filter.creatures-still-obey-range", data.monsterOn_outOfRange===0, `blips=${data.monsterOn_outOfRange} at range 5`);
  check("filter.vendors-toggle", data.vendorOff===0 && data.vendorOn===1, `off=${data.vendorOff} on=${data.vendorOn}`);
  check("filter.boss-toggle", data.bossOff===0 && data.bossOn===1, `off=${data.bossOff} on=${data.bossOn}`);

  check("defaults.bar-shows-its-historical-categories", data.defaultOnBar==="boss,dungeon,event,town",
    `default-on = ${data.defaultOnBar}`);

  check("navaid.dungeon-exit-never-range-culled", data.dungeonExit_farButShown===1,
    `blips=${data.dungeonExit_farButShown} with exit 1e6 away and range 50`);

  check("prefs.range-and-filters-persist", data.persistedRange===1234 && data.persistedMonster===true,
    `range=${data.persistedRange} monster=${data.persistedMonster}`);

  // ── CONTROL
  check("control.pre-fix-behaviour-draws-the-far-blip", data.control_wideRange_showsBoth===2,
    `blips=${data.control_wideRange_showsBoth} with range 2e6 — an unranged bar shows both (the old bug)`);

  // ── the Settings controls must actually render and drive the prefs
  const ui=await page.evaluate(()=>{
    const saved={barRange,barShow:JSON.parse(JSON.stringify(barShow))};
    openSettings();
    const match=document.getElementById('setBarMatch'), rng=document.getElementById('setBarRange'),
          rv=document.getElementById('setBarRangeV');
    const barBtns=[...document.querySelectorAll('#setBarBlips [data-blip]')].map(b=>b.dataset.blip);
    const miniBtns=[...document.querySelectorAll('#setMiniBlips [data-blip]')].map(b=>b.dataset.blip);
    const o={ barCats:barBtns.length, miniCats:miniBtns.length,
      miniOffersBoss: miniBtns.includes("boss"), barOffersBoss: barBtns.includes("boss"),
      matchCheckedByDefault: match.checked, sliderDisabledWhenMatching: rng.disabled,
      rangeLabel: rv.textContent, sliderMax:+rng.max };

    // unchecking "match minimap" must free the slider and pin barRange to a number
    match.checked=false; match.onchange();
    o.afterUncheck_disabled = rng.disabled;
    o.afterUncheck_barRangeIsNumber = typeof barRange==="number";

    // dragging the slider updates barRange live
    rng.value=String(Math.min(900,+rng.max)); rng.oninput();
    o.afterSlider_barRange = barRange;

    // re-checking returns to minimap tracking
    match.checked=true; match.onchange();
    o.afterRecheck_barRangeNull = barRange===null;

    // clicking a category chip flips the store
    const before=barShow.monster;
    document.querySelector('#setBarBlips [data-blip="monster"]').click();
    o.chipTogglesStore = barShow.monster!==before;

    document.getElementById('settings').style.display="none"; paused=false;
    barRange=saved.barRange; Object.assign(barShow,saved.barShow); saveMiniPrefs();
    return o;
  });

  check("ui.bar-offers-every-category", ui.barCats===11, `bar chips=${ui.barCats} (expect 11)`);
  check("ui.minimap-omits-boss-and-event", ui.miniCats===9 && ui.miniOffersBoss===false && ui.barOffersBoss===true,
    `minimap chips=${ui.miniCats} (expect 9, no boss/event — drawMinimap draws neither)`);
  check("ui.match-minimap-checked-by-default", ui.matchCheckedByDefault===true && ui.sliderDisabledWhenMatching===true,
    `checked=${ui.matchCheckedByDefault} sliderDisabled=${ui.sliderDisabledWhenMatching} label="${ui.rangeLabel}"`);
  check("ui.unchecking-match-frees-the-slider", ui.afterUncheck_disabled===false && ui.afterUncheck_barRangeIsNumber===true,
    `disabled=${ui.afterUncheck_disabled} barRange is number=${ui.afterUncheck_barRangeIsNumber}`);
  check("ui.slider-sets-the-range-live", ui.afterSlider_barRange===Math.min(900,ui.sliderMax),
    `barRange=${ui.afterSlider_barRange}`);
  check("ui.re-checking-match-restores-minimap-tracking", ui.afterRecheck_barRangeNull===true,
    `barRange null=${ui.afterRecheck_barRangeNull}`);
  check("ui.category-chip-toggles-the-filter", ui.chipTogglesStore===true, `toggled=${ui.chipTogglesStore}`);

  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
