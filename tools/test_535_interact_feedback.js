// #535 regression harness: pressing E (interact()) must never be silent. Before this fix, interact()
// was a long chain of range-gated `if(...) { ...; return; }` branches with no fallback — if a target
// existed but was just out of range (e.g. the Society Greeter, nameplate visible via tap-target, but
// standing a few metres past the 3.5-unit talk radius), or if nothing was nearby at all, E did
// absolutely nothing: no log line, no toast, no sound.
//
// This boots the game headless, spawns into the Training Academy (every fresh character does), and
// exercises interact() at three real distances from the real Society Greeter NPC object:
//   1. far away (no interactable within the "too far" hint radius either) -> generic fallback message
//   2. within the "too far" hint radius but outside the actual talk radius -> named "get closer" message
//   3. within the actual talk radius -> the dialog really opens (player.academy.greet gets set)
//
// Run:  node tools/test_535_interact_feedback.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
  await waitForMain(page,()=>typeof inDungeon!=="undefined"&&inDungeon===true&&typeof curDungeon!=="undefined"&&curDungeon&&curDungeon.academy===true,{timeout:30000});
  await waitForMain(page,()=>typeof npcs!=="undefined"&&npcs.some(n=>n.role==="academy"&&n.greet==="greet"),{timeout:15000});

  const greeter=await page.evaluate(()=>{ const n=npcs.find(n=>n.role==="academy"&&n.greet==="greet"); return {x:n.x,z:n.z}; });
  check('setup.greeter-found',!!greeter,JSON.stringify(greeter));

  // ── 1. far away: nothing nearby at all -> the generic fallback, never silence ────────────────
  const far=await page.evaluate(()=>{
    player.x=1000; player.z=1000;   // nowhere near any academy object
    const before=logLines.length;
    interact();
    return {gained:logLines.length-before, last:logLines[logLines.length-1]};
  });
  check('far.logged-something',far.gained>=1,`gained=${far.gained}`);
  check('far.message-is-fallback',!!far.last&&/nothing here to interact/i.test(far.last.msg),JSON.stringify(far.last));

  // ── 2. within the "too far to talk" hint radius (>3.5, <9) but outside the talk radius ───────
  const near=await page.evaluate((gx,gz)=>{
    player.x=gx-5; player.z=gz;   // 5 units away (toward open ground, not another NPC): outside the 3.5 talk radius, inside the 9 hint radius
    const A0=JSON.stringify(player.academy);
    const before=logLines.length;
    interact();
    return {gained:logLines.length-before, last:logLines[logLines.length-1], academyBefore:A0, academyAfter:JSON.stringify(player.academy)};
  },greeter.x,greeter.z);
  check('near.logged-something',near.gained>=1,`gained=${near.gained}`);
  check('near.message-names-npc',!!near.last&&/get closer.*Society Greeter/i.test(near.last.msg),JSON.stringify(near.last));
  check('near.dialog-did-not-open',near.academyBefore===near.academyAfter,`before=${near.academyBefore} after=${near.academyAfter}`);

  // ── 3. actually in talk range: the dialog opens for real ──────────────────────────────────────
  const inRange=await page.evaluate((gx,gz)=>{
    player.x=gx-1; player.z=gz;   // 1 unit away (toward open ground, not another NPC): inside the 3.5 talk radius
    const before=logLines.length;
    interact();
    return {gained:logLines.length-before, greetSet:!!(player.academy&&player.academy.greet), lines:logLines.slice(-3).map(l=>l.msg)};
  },greeter.x,greeter.z);
  check('inRange.logged-something',inRange.gained>=1,`gained=${inRange.gained}`);
  check('inRange.dialog-opened',inRange.greetSet,JSON.stringify(inRange.lines));

  check('no-unexpected-page-errors',errs.length===0,errs.slice(0,5).join(' | ').slice(0,400));
  await browser.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{console.error('FATAL',e);process.exit(1)});
