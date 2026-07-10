// #642 regression harness: damage-over-time must actually damage SERVER-AUTHORITATIVE (shared) mobs.
//
// The burn tick lived inside the monster-AI loop, which opens with `if(m.shared) continue;` — correct
// for AI (the server owns shared mobs) but it took the DoT with it. Runtime proof in the issue: a
// shared mob planted with burnT=3, burnDps=50 (worth ~150 damage offline) took 0 damage, and burnT
// never even decremented. Fire-imbued weapons and the entire nether (DoT) school were dead online.
//
// The shared-mob protocol vocabulary is only {t:"attack", id, dmg}, so sharedStatusTick() converts
// accumulated burn into throttled damage claims. The server (resolve_attack) applies them.
//
// Three properties this pins down, each of which is a way to reintroduce silent damage loss:
//   * the client must NEVER touch m.hp for a shared mob — the server owns it
//   * resolve_attack DROPS any claim from beyond ATTACK_RANGE (16.0), so a tick fired while kiting
//     would vanish. We hold instead, and cap the backlog so returning can't dump a stored lump.
//   * while disconnected (NET.open false) we must not fake feedback (#636) — accumulate, send nothing.
//
// Run:  node tools/test_642_shared_mob_dot.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
  await waitForMain(page,()=>typeof player!=="undefined"&&player&&typeof sharedStatusTick==="function"&&typeof NET!=="undefined",{timeout:30000});

  // ── setup: capture netSend, fake a shared mob next to the player
  await page.evaluate(()=>{
    window.__sent=[];
    window.__realNetSend=netSend;
    netSend=msg=>{ window.__sent.push(msg); };
    window.__mkMob=(dist)=>({ id:"testmob", shared:true, kind:"skeleton",
      x:player.x+(dist||2), z:player.z, headY:1.6, hp:500, mhp:500,
      burnT:0, burnDps:0, burnAcc:0, slow:0, flash:0 });
    NET.open=true;
  });

  // Drive the ticks from NODE, not from an in-page loop: headless Chrome clamps setTimeout in a
  // hidden page to ~1/sec, so an in-page `while` with `await setTimeout(16)` runs a couple of
  // iterations, not hundreds. The throttle inside sharedStatusTick keys off the real clock (now()),
  // so the wall-clock spacing between ticks has to be real.
  const tick = (dt)=>page.evaluate(d=>sharedStatusTick(window.__m,d), dt);

  // ── burn ticks, claims are sent, hp is untouched
  await page.evaluate(()=>{ window.__m=window.__mkMob(2); window.__sent.length=0;
    window.__m.burnT=3; window.__m.burnDps=50; window.__hp0=window.__m.hp; });
  for(let i=0;i<6;i++){ await tick(0.5); await sleep(1050); }   // 6 x 0.5s sim, spaced past the 1s throttle
  const burn = await page.evaluate(()=>{
    const m=window.__m, sent=window.__sent.filter(s=>s.t==="attack"&&s.id==="testmob");
    return {burnTLeft:+Math.max(0,m.burnT).toFixed(2), hpUntouched:m.hp===window.__hp0, hp:m.hp,
            claims:sent.length, total:sent.reduce((a,s)=>a+s.dmg,0), dmgs:sent.map(s=>s.dmg),
            allAtLeast1:sent.every(s=>s.dmg>=1), allIntegers:sent.every(s=>Number.isInteger(s.dmg))};
  });

  check("burn.duration-actually-decrements", burn.burnTLeft<=0.05,
    `burnT left=${burn.burnTLeft} (was 3; pre-fix it stayed 3.00 forever)`);
  check("burn.damage-claims-are-sent", burn.claims>=2, `attack claims=${burn.claims} dmgs=[${burn.dmgs}]`);
  check("burn.total-damage-is-material", burn.total>=120 && burn.total<=160,
    `total claimed=${burn.total} (3s x 50dps = 150)`);
  check("burn.each-claim-is-a-positive-integer", burn.allAtLeast1&&burn.allIntegers, `dmgs=[${burn.dmgs}]`);
  check("burn.client-never-touches-shared-hp", burn.hpUntouched===true,
    `hp stayed ${burn.hp} — the server owns it (resolve_attack)`);

  // ── throttle: rapid ticks inside one second collapse to a single claim
  const thr = await page.evaluate(()=>{
    window.__m=window.__mkMob(2); window.__sent.length=0;
    window.__m.burnT=5; window.__m.burnDps=50;
    for(let i=0;i<5;i++) sharedStatusTick(window.__m,0.2);   // 1s of sim, no wall-clock gap
    return {claims:window.__sent.filter(s=>s.t==="attack").length};
  });
  check("burn.claims-are-throttled", thr.claims===1, `5 rapid ticks -> ${thr.claims} claim (cap 1/s)`);

  // ── out of ATTACK_RANGE: nothing is sent, and the backlog is capped (no lump on return)
  await page.evaluate(()=>{ window.__m=window.__mkMob(40); window.__sent.length=0;   // 40u away; ATTACK_RANGE is 16
    window.__m.burnT=3; window.__m.burnDps=50; });
  for(let i=0;i<3;i++){ await tick(0.5); await sleep(1050); }
  const far = await page.evaluate(()=>{
    const m=window.__m;
    const farClaims=window.__sent.filter(s=>s.t==="attack").length, held=m.burnAcc;
    m.x=player.x+2; m._burnSendT=0;          // walk back into range, clear the throttle
    sharedStatusTick(m,0.02);
    const sent=window.__sent.filter(s=>s.t==="attack");
    return {farClaims, held:+held.toFixed(1), burnDps:m.burnDps,
            firstClaimOnReturn: sent.length?sent[0].dmg:null};
  });

  check("range.no-claims-fired-beyond-attack-range", far.farClaims===0,
    `claims while 40u away = ${far.farClaims} (the server would DROP them -> silent damage loss)`);
  check("range.backlog-capped-at-one-second-of-burn", far.held<=far.burnDps+0.01,
    `held backlog=${far.held} <= 1s worth (${far.burnDps}) — kiting 3s can't bank 150 damage`);
  check("range.return-lands-at-most-one-second-of-burn",
    far.firstClaimOnReturn!=null && far.firstClaimOnReturn>=1 && far.firstClaimOnReturn<=far.burnDps+2,
    `first claim on return = ${far.firstClaimOnReturn} (<= 1s of dps, not the whole kited duration)`);

  // ── disconnected: accumulate, send nothing, fake nothing
  await page.evaluate(()=>{ window.__m=window.__mkMob(2); window.__sent.length=0;
    NET.open=false; window.__m.burnT=3; window.__m.burnDps=50; });
  for(let i=0;i<2;i++){ await tick(0.5); await sleep(1050); }
  const off = await page.evaluate(()=>{
    const m=window.__m;
    const r={claims:window.__sent.filter(s=>s.t==="attack").length, acc:+m.burnAcc.toFixed(1), burnT:+m.burnT.toFixed(2)};
    NET.open=true; return r;
  });
  check("offline.no-claims-while-disconnected", off.claims===0, `claims=${off.claims}`);
  check("offline.burn-still-accumulates", off.acc>0 && off.burnT<3, `acc=${off.acc} burnT=${off.burnT}`);

  // ── slow decays instead of sticking forever
  const slow = await page.evaluate(()=>{
    const m=window.__mkMob(2); m.slow=1;
    for(let i=0;i<20;i++) sharedStatusTick(m,0.1);   // 2s of sim
    return {slow:+m.slow.toFixed(3)};
  });
  check("status.transient-slow-decays", slow.slow<1 && slow.slow>=0, `slow after 2s = ${slow.slow}`);

  // ── CONTROL: the AI loop still skips shared mobs, so without sharedStatusTick nothing ticks.
  //    Real rAF frames run updateMonsters(); a shared mob in monsters[] must come out untouched.
  await page.evaluate(()=>{ window.__c=window.__mkMob(2); window.__sent.length=0;
    window.__c.burnT=3; window.__c.burnDps=50; monsters.push(window.__c); });
  await sleep(1500);                        // rAF keeps running even when timers are throttled
  const control = await page.evaluate(()=>{
    const m=window.__c, i=monsters.indexOf(m); if(i>=0) monsters.splice(i,1);
    return {burnT:+m.burnT.toFixed(2), hp:m.hp, claims:window.__sent.filter(s=>s.t==="attack").length};
  });
  check("control.ai-loop-alone-never-ticks-a-shared-mob", control.burnT===3 && control.hp===500 && control.claims===0,
    `burnT=${control.burnT} hp=${control.hp} claims=${control.claims} — the pre-fix behaviour (AI loop skips m.shared)`);

  await page.evaluate(()=>{ netSend=window.__realNetSend; });
  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
