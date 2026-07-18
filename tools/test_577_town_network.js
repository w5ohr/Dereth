// #577 regression harness: the Town Network hub's fog must stay clearly distinguishable from its own
// wall/floor materials (same class of bug as the Academy's #534 — before this fix the hall's walls
// sat within ~18 RGB units of the inNetwork fog color and "melted" away a few metres out), and the
// portal ring must not place a high-level frontier destination (e.g. Redspire, level 35) among the
// nearest portals to the entrance while low-level destinations sit far away.
//
// Run:  node tools/test_577_town_network.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
function rgbDist(a,b){
  const ar=(a>>16)&255,ag=(a>>8)&255,ab=a&255, br=(b>>16)&255,bg=(b>>8)&255,bb=b&255;
  return Math.hypot(ar-br,ag-bg,ab-bb);
}

(async()=>{
  const browser=await puppeteer.launch({executablePath:CHROME,headless:'new',
    args:['--no-sandbox','--use-gl=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
  const page=await browser.newPage();
  const errs=[]; page.on('pageerror',e=>{ if(!BENIGN.test(e.message)) errs.push(e.message); });
  await page.goto(URL,{waitUntil:'load',timeout:60000});
  // initThree() creates scene/renderer ASYNC (after the asset loaders resolve) — but startGame() is
  // defined at script load and runs buildWorld()/enterAcademyHall() synchronously, so calling it before
  // the renderer/scene exist throws mid-build into our catch and leaves the world half-built. Wait for
  // them to actually exist FIRST. (#979/#983)
  await waitForMain(page,()=>typeof startGame==="function"&&typeof renderer!=="undefined"&&renderer!==null
    &&typeof scene!=="undefined"&&!!scene,{timeout:60000});   // end on a boolean: a bare `&&scene` returns the (unserializable, circular) THREE.Scene, which puppeteer marshals to undefined → the gate reads falsy forever
  await page.evaluate(()=>{ try{ startGame(false,'aluvian'); }catch(e){} });
  // Gate on the scene being live before driving enterNetwork — enterNetwork/buildNetwork call scene.add(),
  // which throws on an undefined scene (the #983 TypeError). #986: do NOT also gate on the arrival
  // portalTransit clearing — enterNetwork's guard is `if(inNetwork||inDungeon) return;` (it never checks
  // portalTransit), and the test forces inDungeon=false below, so the tube is irrelevant here. Under
  // swiftshader that tube's game-time maxHold stretches past any fixed real-time ceiling (clamped-dt
  // frames), so waiting on it turned the #983 fix into a deterministic timeout on software GL.
  await waitForMain(page,()=>typeof player!=="undefined"&&player&&typeof enterNetwork==="function"
    &&typeof scene!=="undefined"&&!!scene,{timeout:30000});

  // force out of the academy (a fresh character always starts there) so enterNetwork's guard passes,
  // then enter the towns hub and force one deterministic lighting/fog pass.
  const data=await page.evaluate(()=>{
    inDungeon=false; curDungeon=null;
    enterNetwork(0,0);
    updateInstanceLight();
    const walls=netObjs.filter(o=>o.isMesh&&o.geometry&&o.geometry.type==="CylinderGeometry"&&o.material&&o.material.side===THREE.BackSide);
    const floors=netObjs.filter(o=>o.isMesh&&o.geometry&&o.geometry.type==="CircleGeometry"&&o.geometry.parameters.radius>10);
    const townPortals=netPortals.filter(np=>np.kind==="town");
    const ENTRANCE_A=Math.PI/2;
    const angDist=(x,z)=>{ const a=Math.atan2(z-DCEN.z,x-DCEN.x); let d=Math.abs(a-ENTRANCE_A); if(d>Math.PI) d=2*Math.PI-d; return d; };
    const byAngle=townPortals.map(np=>({name:np.name,req:np.town.req||0,d:angDist(np.x,np.z)})).sort((a,b)=>a.d-b.d);
    const nearest5=byAngle.slice(0,5);
    const redspire=byAngle.find(p=>p.name==="Redspire");
    return {
      inNetwork,
      fog: scene.fog?scene.fog.color.getHex():null,
      wallColors:[...new Set(walls.map(w=>w.material.color.getHex()))],
      floorColors:[...new Set(floors.map(f=>f.material.color.getHex()))],
      nearest5, redspireDist: redspire?redspire.d:null,
      maxReqNearest5: Math.max(...nearest5.map(p=>p.req)),
    };
  });

  check('network.entered',data.inNetwork===true);
  check('network.fog-active',data.fog!=null,`fog=${data.fog!=null?'0x'+data.fog.toString(16):'null'}`);
  check('network.walls-found',data.wallColors.length>0,`walls=${JSON.stringify(data.wallColors)}`);
  check('network.floors-found',data.floorColors.length>0,`floors=${JSON.stringify(data.floorColors)}`);

  const MIN_DIST=40;   // enclosed hall is intrinsically dark (portal-lit only); still needs a clear gap
  for(const wc of data.wallColors){
    const d=rgbDist(data.fog,wc);
    check(`network.fog-vs-wall-0x${wc.toString(16)}`,d>=MIN_DIST,`distance=${d.toFixed(1)} (need >=${MIN_DIST})`);
  }
  for(const fc of data.floorColors){
    const d=rgbDist(data.fog,fc);
    check(`network.fog-vs-floor-0x${fc.toString(16)}`,d>=MIN_DIST,`distance=${d.toFixed(1)} (need >=${MIN_DIST})`);
  }

  check('network.redspire-not-nearest',data.maxReqNearest5<35,
    `nearest 5 to entrance: ${JSON.stringify(data.nearest5)}`);
  check('network.redspire-pushed-back',data.redspireDist>Math.PI/2,
    `redspire angular distance from entrance=${data.redspireDist!=null?data.redspireDist.toFixed(2):'?'} rad (need >${(Math.PI/2).toFixed(2)})`);

  check('no-unexpected-page-errors',errs.length===0,errs.slice(0,5).join(' | ').slice(0,400));
  await browser.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{console.error('FATAL',e);process.exit(1)});
