// #554 regression harness (part 1 of 2 — see also test_554_shared_texture_dispose.js).
//
// Symptom: Holtburg rendered as featureless white/cream masses with live collision on invisible
// geometry. The town's real AC buildings were NEVER BUILT — what the player saw was only the pale,
// untextured procedural plaza that addCity() always draws.
//
// Root cause: tbStream gates the town build on EVERY TB_KINDS object's model resolving first.
// tbModelReq caches a DID absent from assets/actownmodels/index.json as `_tbModels[did]=false` and
// returns null forever. The old gate could not distinguish "still loading" from "permanently missing",
// so one un-baked scenery prop kept ready=false forever and tbBuildTown was never called.
//
// Holtburg references 0X01000075 and 0X01000C00 (both `scenery`), neither of which is in the 846-entry
// bake. 21 of 56 towns hit this. The fix treats a DID cached as `false` as skippable while still
// blocking on models that are genuinely in flight.
//
// The harness drives the REAL tbStream against the REAL assets and asserts Holtburg builds. It then
// runs a NEGATIVE CONTROL that re-evaluates the OLD gate expression over the same fully-loaded state
// and asserts it still says "not ready" — proving the fix is load-bearing and the assertion is not
// passing vacuously.
//
// Run:  node tools/test_554_town_build_gate.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
const TOWN="Holtburg";
const UNBAKED=["0X01000075","0X01000C00"];   // the two scenery DIDs that used to block the whole town

(async()=>{
  const browser=await puppeteer.launch({executablePath:CHROME,headless:'new',
    args:['--no-sandbox','--use-gl=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
  const page=await browser.newPage();
  const errs=[]; page.on('pageerror',e=>{ if(!BENIGN.test(e.message)) errs.push(e.message); });
  await page.goto(URL,{waitUntil:'load',timeout:60000});
  await waitForMain(page,()=>typeof startGame==="function",{timeout:30000});
  await page.evaluate(()=>{ try{ startGame(false,'aluvian'); }catch(e){} });
  await waitForMain(page,()=>typeof player!=="undefined"&&player&&typeof tbStream==="function"&&typeof AC_TOWNS!=="undefined"&&AC_TOWNS,{timeout:30000});
  // the model index must land before the gate means anything. tbIndexReq() is the one-shot kick that
  // starts the fetch (it returns null until the JSON lands); poll _tbIndex separately afterwards.
  await page.evaluate(()=>{ tbIndexReq(); });
  await waitForMain(page,()=>_tbIndex!==null,{timeout:30000});

  // Park the player in Holtburg and pump the streamer until the town commits.
  await page.evaluate((TOWN)=>{
    const c=CITIES.find(c=>c.name===TOWN);
    player.x=c.x; player.z=c.z;
    if(typeof inDungeon!=="undefined") inDungeon=false;
    if(typeof inNetwork!=="undefined") inNetwork=false;
  },TOWN);
  const t0=Date.now();
  let built=false;
  while(Date.now()-t0<60000){
    built=await page.evaluate((TOWN)=>{ _tbAcc=0; tbStream(1); return !!(_tbBuilt[TOWN]&&_tbBuilt[TOWN].g); },TOWN);
    if(built) break;
    await sleep(250);
  }

  const data=await page.evaluate((TOWN,UNBAKED)=>{
    const rec=_tbBuilt[TOWN];
    let meshes=0, textured=0;
    if(rec&&rec.g) rec.g.traverse(o=>{ if(o.isMesh){ meshes++; if(o.material&&o.material.map) textured++; } });

    // NEGATIVE CONTROL: evaluate the OLD gate over the same (fully settled) state.
    const objs=AC_TOWNS[TOWN];
    let oldReady=true, newReady=true;
    for(const o of objs){ if(!TB_KINDS[o.kind]) continue;
      if(!tbModelReq(o.did)) oldReady=false;                                                   // pre-fix rule
      if(!tbModelReq(o.did) && _tbModels[(''+o.did).toUpperCase()]!==false) newReady=false;    // post-fix rule
    }

    // How many towns would the OLD gate block, repo-wide? (PR #566 claimed 21 of 56.)
    let blocked=0, total=0;
    for(const name in AC_TOWNS){ total++;
      const os=AC_TOWNS[name];
      for(const o of os){ if(!TB_KINDS[o.kind]) continue;
        const up=(''+o.did).toUpperCase();
        if(_tbIndex[up]===undefined){ blocked++; break; }
      }
    }

    return {
      hasRecord: !!rec, hasGroup: !!(rec&&rec.g), meshes, textured,
      obstacles: rec&&rec.obst?rec.obst.length:0,
      unbakedCachedFalse: UNBAKED.map(d=>({did:d, cached:_tbModels[d]})),
      unbakedAbsentFromIndex: UNBAKED.map(d=>({did:d, inIndex:_tbIndex[d]!==undefined})),
      oldReady, newReady, blocked, total
    };
  },TOWN,UNBAKED);

  check("town.built-despite-unbaked-prop", data.hasRecord && data.hasGroup,
    `record=${data.hasRecord} group=${data.hasGroup}`);
  check("town.building-meshes-exist", data.meshes>0, `meshes=${data.meshes}`);
  check("town.meshes-are-textured", data.textured>0, `textured=${data.textured}/${data.meshes}`);
  check("town.colliders-present", data.obstacles>0, `obstacles=${data.obstacles}`);

  check("gate.unbaked-dids-absent-from-index",
    data.unbakedAbsentFromIndex.every(x=>x.inIndex===false),
    data.unbakedAbsentFromIndex.map(x=>`${x.did}:${x.inIndex?'present':'absent'}`).join(' '));
  check("gate.unbaked-dids-cached-as-false",
    data.unbakedCachedFalse.every(x=>x.cached===false),
    data.unbakedCachedFalse.map(x=>`${x.did}=${x.cached}`).join(' '));
  check("gate.new-rule-reports-ready", data.newReady===true, `newReady=${data.newReady}`);

  // ── NEGATIVE CONTROL: the pre-fix rule must STILL be false even now that everything is loaded.
  check("control.old-rule-would-still-block-this-town", data.oldReady===false,
    `oldReady=${data.oldReady} (missing DIDs return null forever — this is the pre-fix behaviour)`);
  check("control.old-rule-blocks-21-of-56-towns", data.blocked===21 && data.total===56,
    `blocked=${data.blocked}/${data.total}`);

  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
