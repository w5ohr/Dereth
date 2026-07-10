// #534 regression harness: the Training Academy's indoor fog color must be clearly distinguishable
// from the hall's own wall/floor/courtyard albedo. Before this fix, fog was 0x9a9078 while the stone
// wall material was 0x8d8578 and the floor was 0x9a927e — near-identical colors — so any surface more
// than a few metres away visually "melted" into the fog instead of fading into a readable shadow,
// leaving only floating quest markers visible past ~10m (the reported "uniform beige field").
//
// This boots the game headless, walks straight into the academy (a fresh character always starts
// there), and asserts the actual live fog color used by updateInstanceLight() is far enough (in RGB
// distance) from each of the hall's real material colors that they stay visually distinct, and that
// the fog is a *darker* recede-into-shadow tone rather than a same-brightness wash.
//
// Run:  node tools/test_534_academy_fog_contrast.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
function luminance(hex){ const r=(hex>>16)&255,g=(hex>>8)&255,b=hex&255; return 0.2126*r+0.7152*g+0.0722*b; }

(async()=>{
  const browser=await puppeteer.launch({executablePath:CHROME,headless:'new',
    args:['--no-sandbox','--use-gl=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
  const page=await browser.newPage();
  const errs=[]; page.on('pageerror',e=>{ if(!BENIGN.test(e.message)) errs.push(e.message); });
  await page.goto(URL,{waitUntil:'load',timeout:60000});
  await waitForMain(page,()=>typeof startGame==="function",{timeout:30000});
  await page.evaluate(()=>{ try{ startGame(false,'aluvian'); }catch(e){} });   // a fresh character always spawns in the Training Academy
  await waitForMain(page,()=>typeof inDungeon!=="undefined"&&inDungeon===true&&typeof curDungeon!=="undefined"&&curDungeon&&curDungeon.academy===true,{timeout:30000});
  await waitForMain(page,()=>typeof updateInstanceLight==="function"&&typeof dungeonObjs!=="undefined"&&dungeonObjs.length>0,{timeout:15000});
  await page.evaluate(()=>{ updateInstanceLight(); });   // force one deterministic lighting/fog pass

  const data=await page.evaluate(()=>{
    // find real wall meshes (BoxGeometry walls are placed at y=2.2 per buildAcademyHall's `wall()` helper)
    // and floor meshes (thin BoxGeometry(w,0.1,d) at y=0) among the academy's own dungeonObjs — no
    // hardcoded colors here, this reads whatever the live materials actually are.
    const walls=dungeonObjs.filter(o=>o.isMesh&&o.geometry&&o.geometry.type==="BoxGeometry"&&Math.abs(o.position.y-2.2)<0.01);
    const floors=dungeonObjs.filter(o=>o.isMesh&&o.geometry&&o.geometry.type==="BoxGeometry"&&Math.abs(o.position.y)<0.01);
    return {
      fog: scene.fog?scene.fog.color.getHex():null,
      fogNear: scene.fog?scene.fog.near:null,
      fogFar: scene.fog?scene.fog.far:null,
      wallCount: walls.length, floorCount: floors.length,
      wallColors:[...new Set(walls.map(w=>w.material.color.getHex()))],
      floorColors:[...new Set(floors.map(f=>f.material.color.getHex()))],
    };
  });
  check('academy.fog-active',data.fog!=null,`fog=${data.fog!=null?'0x'+data.fog.toString(16):'null'}`);
  check('academy.walls-found',data.wallCount>0,`walls=${data.wallCount}`);
  check('academy.floors-found',data.floorCount>0,`floors=${data.floorCount}`);

  const MIN_DIST=60;   // ~60 in 0-441 RGB-distance space is a comfortably visible difference; the old bug measured ~14
  for(const wc of data.wallColors){
    const d=rgbDist(data.fog,wc);
    check(`academy.fog-vs-wall-0x${wc.toString(16)}`,d>=MIN_DIST,`distance=${d.toFixed(1)} (need >=${MIN_DIST})`);
  }
  for(const fc of data.floorColors){
    const d=rgbDist(data.fog,fc);
    check(`academy.fog-vs-floor-0x${fc.toString(16)}`,d>=MIN_DIST,`distance=${d.toFixed(1)} (need >=${MIN_DIST})`);
  }
  // the fog should recede into shadow (darker), not wash out at the same brightness as lit surfaces
  for(const wc of data.wallColors){
    check(`academy.fog-darker-than-wall-0x${wc.toString(16)}`,luminance(data.fog)<luminance(wc),
      `fog lum=${luminance(data.fog).toFixed(1)} vs wall lum=${luminance(wc).toFixed(1)}`);
  }
  check('academy.fog-far-covers-complex',data.fogFar>=150,`far=${data.fogFar} (hall+courtyard+wings+gatehouse span ~60m from spawn)`);

  check('no-unexpected-page-errors',errs.length===0,errs.slice(0,5).join(' | ').slice(0,400));
  await browser.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{console.error('FATAL',e);process.exit(1)});
