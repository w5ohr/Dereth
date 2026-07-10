// #501 regression harness: an exception thrown by updateLightPool() or renderComposite() must NOT
// kill the requestAnimationFrame(loop) chain. Before this fix, loop() called those two functions
// un-guarded AFTER the try/catch around update() — a throw there skipped the trailing
// requestAnimationFrame(loop) call and froze the camera/render forever while game-logic paths that
// don't depend on the rAF chain (chat commands, UI clicks, websocket handlers) kept working. That
// exact asymmetry is what let the portal-tube arrival ("WELCOME BACK") get stuck on-screen forever:
// updatePortalTransit's maxHold safety cap lives inside update(), which is only ever advanced by the
// rAF chain, so once the chain died the tube could never time out.
//
// This harness boots the real game headless, injects a one-shot throw into updateLightPool() and
// then into renderComposite(), and asserts in each case that the rAF chain keeps advancing (a frame
// counter keeps climbing) and that a portal transit with a never-satisfied ready() still clears via
// maxHold instead of hanging.
//
// Run:  node tools/test_501_raf_chain.js   (needs: npm i puppeteer-core, Chrome/Chromium installed,
//        and a static server for the repo root: python3 -m http.server 8791)
const puppeteer=require('puppeteer-core');
const CHROME=process.env.CHROME||"/opt/pw-browsers/chromium";
const URL=process.env.DERETH_URL||'http://localhost:8791/index.html';

let pass=0,fail=0;
function check(name,ok,detail){ console.log(`${ok?'PASS':'FAIL'}  ${name}${detail?'  — '+detail:''}`); ok?pass++:fail++; }
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const BENIGN=/Pointer Lock|pointerlock/i;   // headless Chrome cannot grant pointer lock — environmental, not a bug
// page.waitForFunction() polls in an ISOLATED world with its own global object, where the page's
// top-level `let`/`const` (e.g. `portalTransit`) and even plain function declarations (e.g. `loop`)
// are not visible at all — only page.evaluate()'s MAIN-world execution can see them. So poll by hand.
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
  await waitForMain(page,()=>typeof loop==="function"&&typeof portalTransit!=="undefined",{timeout:30000});
  await sleep(500);   // let the natural startup portal transit (if any) clear so it doesn't confound the injected-transit test

  // ── 1. a frame-counter proves the rAF chain is alive at baseline ─────────────────────────────
  const base=await page.evaluate(async()=>{
    window.__frames=0;
    const orig=window.requestAnimationFrame.bind(window);
    window.requestAnimationFrame=fn=>orig(ts=>{ window.__frames++; fn(ts); });
    await new Promise(r=>setTimeout(r,300));
    return window.__frames;
  });
  check('baseline.rAF-chain-alive',base>0,`frames advanced=${base}`);

  // ── 2. inject a ONE-SHOT throw into updateLightPool — chain must survive ─────────────────────
  const afterLP=await page.evaluate(async()=>{
    const real=window.updateLightPool;
    let threw=false;
    window.updateLightPool=function(...a){ if(!threw){ threw=true; throw new Error('injected updateLightPool failure'); } return real.apply(this,a); };
    const f0=window.__frames;
    await new Promise(r=>setTimeout(r,400));
    window.updateLightPool=real;
    return {threw,gained:window.__frames-f0};
  });
  check('updateLightPool-throw.injected',afterLP.threw);
  check('updateLightPool-throw.rAF-chain-survives',afterLP.gained>=2,`frames gained after throw=${afterLP.gained}`);

  // ── 3. inject a ONE-SHOT throw into renderComposite — chain must survive ─────────────────────
  const afterRC=await page.evaluate(async()=>{
    const real=window.renderComposite;
    let threw=false;
    window.renderComposite=function(...a){ if(!threw){ threw=true; throw new Error('injected renderComposite failure'); } return real.apply(this,a); };
    const f0=window.__frames;
    await new Promise(r=>setTimeout(r,400));
    window.renderComposite=real;
    return {threw,gained:window.__frames-f0};
  });
  check('renderComposite-throw.injected',afterRC.threw);
  check('renderComposite-throw.rAF-chain-survives',afterRC.gained>=2,`frames gained after throw=${afterRC.gained}`);

  // ── 4. the actual #501 symptom: a portal transit whose ready() never fires must still clear via
  //    maxHold even when renderComposite throws on every single frame while it's held ────────────
  // startPortalTransit() is a no-op (just calls arrive()) whenever a transit is already active — so
  // first wait out the game's own startup arrival tube (maxHold up to 10s) or our injected transit
  // would silently be dropped and we'd be observing the unrelated startup transit's timing instead.
  await waitForMain(page,()=>!portalTransit,{timeout:15000});
  const isOurs=await page.evaluate(()=>{
    const real=window.renderComposite;
    window.__realRenderComposite=real;
    window.renderComposite=function(...a){ try{ return real.apply(this,a); }finally{ throw new Error('persistent injected renderComposite failure'); } };
    window.startPortalTransit(null,{ready:()=>false,minHold:0.2,maxHold:0.6});
    return !!portalTransit&&portalTransit.maxHold===0.6;   // confirm it's OUR transit (distinct maxHold), not some other transit racing in
  });
  check('portal-tube.transit-started',isOurs);
  // poll from Node with short discrete evaluate() calls — an in-page async loop with internal
  // setTimeout()s over several seconds proved unreliable under CDP's awaitPromise (frames appeared to
  // stop advancing partway through one giant evaluate), where separate short evaluate() calls do not.
  // Generous cap: maxHold(600ms)+fade(500ms)=1.1s of simulated time is the contract, but under a
  // loaded CI/dev box a single swiftshader frame can itself take several hundred ms wall-clock — this
  // asserts the tube clears at all (the #501 bug), not that it clears within a tight wall-clock bound.
  const CAP_MS=30000;
  const t0=Date.now();
  let cleared=false;
  while(Date.now()-t0<CAP_MS){ if(await page.evaluate(()=>!portalTransit)){ cleared=true; break; } await sleep(100); }
  const elapsedMs=Date.now()-t0;
  await page.evaluate(()=>{ window.renderComposite=window.__realRenderComposite; });
  check('portal-tube.clears-via-maxHold-despite-render-throw',cleared,
    `cleared=${cleared} after ${elapsedMs}ms (contract: maxHold=600ms + fade 500ms; cap=${CAP_MS}ms guards against a genuine hang)`);

  check('no-unexpected-page-errors',errs.length===0,errs.slice(0,5).join(' | ').slice(0,400));
  await browser.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{console.error('FATAL',e);process.exit(1)});
