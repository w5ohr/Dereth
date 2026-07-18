// #641 regression harness: the Incursion HUD must show the SERVER's alive count, not the count of
// event mobs the client happens to be able to see.
//
// netEventAlive() counts NET.mobs entries tagged with the event id — but mobs are AOI-filtered out of
// snapshots at 300 units, while event mobs ring 90-440 units from the anchor. So standing at the
// beacon with all 8 alive, the HUD read "2/8 cleared": the two mobs beyond 300u simply weren't in
// NET.mobs, and `total - visible` treated them as dead.
//
// The server now sends `alive` in event_snap() — the same count step_events() uses to decide success,
// so the HUD and the win condition can never disagree. eventCleared() prefers it and falls back to
// netEventAlive() for servers that don't send it.
//
// Run:  node tools/test_641_incursion_alive_count.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
  // initThree() is async: eventCleared/reconcileEvent/NET are hoisted and exist immediately, but `scene`
  // and `renderer` do not. reconcileEvent -> buildEventBeacon does scene.add(beam), so gate on the
  // renderer/scene being live (the `renderer!==null` readiness the verify SKILL uses) before probing —
  // otherwise the first evaluate races initThree and throws "Cannot read properties of undefined". (#975)
  await waitForMain(page,()=>typeof eventCleared==="function"&&typeof reconcileEvent==="function"&&typeof NET!=="undefined"
    &&typeof renderer!=="undefined"&&renderer!==null&&typeof scene!=="undefined"&&!!scene,{timeout:30000});

  const data = await page.evaluate(()=>{
    const out={};
    const saved={isOnline:typeof isOnline!=="undefined"?isOnline:false, event:NET.event, mobs:NET.mobs};
    isOnline=true;

    // Stand up an event with 8 mobs, only 6 of which are inside the AOI (2 ring out past 300u).
    NET.event=null;
    NET.mobs={};
    reconcileEvent({id:"evt1", name:"Shadow Incursion", x:0, z:0, col:0x8a4aff, total:8, alive:8});
    for(let i=0;i<6;i++) NET.mobs["m"+i]={id:"m"+i, eventId:"evt1"};   // the 6 the client can see

    out.aoiVisible = netEventAlive();                 // 6 — what the old code used
    out.total = NET.event.total;
    out.serverAlive = NET.event.alive;
    out.clearedWithServerCount = eventCleared();      // must be 0: nobody has killed anything

    // the old formula, for the record
    out.clearedOldFormula = NET.event.total - netEventAlive();

    // server reports kills; the client's AOI view is unchanged
    reconcileEvent({id:"evt1", name:"Shadow Incursion", x:0, z:0, col:0x8a4aff, total:8, alive:5});
    out.clearedAfter3Kills = eventCleared();

    // alive refreshes on every snapshot, not just on a new event id
    reconcileEvent({id:"evt1", name:"Shadow Incursion", x:0, z:0, col:0x8a4aff, total:8, alive:0});
    out.clearedWhenAllDead = eventCleared();

    // never negative, never past total (a stale/oversized alive can't make the bar go backwards)
    reconcileEvent({id:"evt1", name:"Shadow Incursion", x:0, z:0, col:0x8a4aff, total:8, alive:99});
    out.clampedLow = eventCleared();
    reconcileEvent({id:"evt1", name:"Shadow Incursion", x:0, z:0, col:0x8a4aff, total:8, alive:-5});
    out.clampedHigh = eventCleared();

    // ── FALLBACK: an old server sends no `alive` at all. We must not crash, and we fall back to
    //    the AOI count (which is what the pre-fix code did).
    NET.event=null; NET.mobs={};
    reconcileEvent({id:"evt2", name:"Old Server", x:0, z:0, col:0x8a4aff, total:8});
    for(let i=0;i<6;i++) NET.mobs["m"+i]={id:"m"+i, eventId:"evt2"};
    out.fallbackAliveUndefined = (typeof NET.event.alive==="undefined");
    out.fallbackCleared = eventCleared();             // 8-6 = 2 (the old, wrong-but-safe behaviour)

    // ── CONTROL: the AOI-derived count really does overstate progress with all 8 alive
    out.control_overstates = (out.clearedOldFormula > 0);

    NET.event=saved.event; NET.mobs=saved.mobs; isOnline=saved.isOnline;
    return out;
  });

  check("hud.no-phantom-kills-when-all-mobs-alive", data.clearedWithServerCount===0,
    `cleared=${data.clearedWithServerCount}/${data.total} with ${data.serverAlive} alive and only ${data.aoiVisible} in AOI`);
  check("hud.reflects-real-kills", data.clearedAfter3Kills===3,
    `server alive 5/8 -> cleared=${data.clearedAfter3Kills}`);
  check("hud.alive-refreshes-on-every-snapshot", data.clearedWhenAllDead===8,
    `alive 0 -> cleared=${data.clearedWhenAllDead}/8 (reconcileEvent must update an existing event, not only a new id)`);
  check("hud.count-is-clamped", data.clampedLow===0 && data.clampedHigh===8,
    `alive=99 -> ${data.clampedLow}, alive=-5 -> ${data.clampedHigh}`);

  check("compat.old-server-without-alive-falls-back", data.fallbackAliveUndefined===true && data.fallbackCleared===2,
    `no alive field -> cleared=${data.fallbackCleared} via netEventAlive() (8 total, 6 visible)`);

  check("control.aoi-derived-count-overstates-progress", data.control_overstates===true,
    `old formula said ${data.clearedOldFormula}/${data.total} cleared while all 8 were alive — the bug`);

  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
