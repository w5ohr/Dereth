// #232 regression harness: GPU memory must NOT ratchet as you travel, delve, and hub-hop.
//
// Drives the real game headless and asserts that every streamed-out / torn-down region actually
// RELEASES its GPU geometries and textures:
//   1. unit: disposeObject3D / releaseObjectGPU skip acShared (model-cache) subtrees, dispose the
//      rest; releaseObjectGPU leaves the object attach-able and the LABELS registry intact.
//   2. cullWorld eviction tier — far, long-detached world objects get their GPU copies released
//      (userData._evD set + renderer counts drop) and re-attach cleanly on return.
//   3. dungeon enter/exit cycles — live resource counts return to baseline (no ratchet).
//   4. town-network hub enter/exit cycles — same.
//   5. buildWorld rebuild — rebuilding the world does not stack the old one underneath.
//
// Methodology — two measures, each immune to a different confounder:
//   · LIVE COUNTS (primary for cycles): created-minus-disposed THREE.BufferGeometry / Texture,
//     tracked from before startGame. renderer.info.memory keeps climbing benignly for a long time
//     as cached content (monster breeds, NPC outfits) renders for the FIRST time, which swamps
//     small deltas — but a detach-without-dispose leak leaves objects undisposed forever, so live
//     counts catch exactly the #232 bug class without that noise.
//   · renderer.info.memory (for eviction): live counts can't see GPU residency, so the eviction
//     test asserts the uploaded-resource counters actually DROP when far regions are released.
//   Background asset streaming is additionally frozen (request interception) after a warm-up tour,
//   and the 5-minute eviction timer is suspended during the cycle tests so it can't deflate them.
//
// Run:  node tools/test_gpu_release.js            (needs: npm i puppeteer-core, Chrome installed,
//        and a static server for the repo root:  python -m http.server 8791)
// SwiftShader notes (headless): verify by state/counters, never by screenshot; frames can take
// hundreds of ms, so every wait polls for quiescence instead of trusting a fixed sleep.
const puppeteer=require('puppeteer-core');
const CHROME=process.env.CHROME||"C:/Program Files/Google/Chrome/Application/chrome.exe";
const URL=process.env.DERETH_URL||'http://localhost:8791/index.html';

let pass=0,fail=0;
function check(name,ok,detail){ console.log(`${ok?'PASS':'FAIL'}  ${name}${detail?'  — '+detail:''}`); ok?pass++:fail++; }
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
// headless Chrome cannot grant pointer lock — a boot-time NotAllowedError is environmental, not a bug
const BENIGN=/Pointer Lock|pointerlock/i;
// poll a main-world predicate to quiescence — every wait must gate on real state, never a fixed sleep,
// because SwiftShader frames run seconds apart and the async boot (initThree) resolves on its own clock.
async function waitForMain(page,fn,{timeout=30000,interval=100}={}){
  const t0=Date.now();
  while(Date.now()-t0<timeout){ if(await page.evaluate(fn)) return; await sleep(interval); }
  throw new Error(`waitForMain timed out after ${timeout}ms`);
}

(async()=>{
  const browser=await puppeteer.launch({executablePath:CHROME,headless:'new',protocolTimeout:600000,   // #986: SwiftShader frames run seconds apart, so a single settle/eviction evaluate can exceed puppeteer's 180s default protocolTimeout — raise the ceiling so software GL can finish the whole cycle
    args:['--no-sandbox','--use-gl=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
  const page=await browser.newPage();
  const errs=[]; page.on('pageerror',e=>{ if(!BENIGN.test(e.message)) errs.push(e.message); });
  await page.goto(URL,{waitUntil:'load',timeout:60000});
  // initThree() creates scene/renderer ASYNC (after the asset loaders resolve) — but startGame() is
  // defined at script load and runs buildWorld() synchronously, so calling it before the renderer/scene
  // exist throws mid-build into our catch and leaves the world half-built. Wait for them FIRST. (#979/#981)
  await waitForMain(page,()=>typeof startGame==="function"&&typeof renderer!=="undefined"&&renderer!==null
    &&typeof scene!=="undefined"&&scene&&typeof scenery!=="undefined",{timeout:60000});
  // live-count tracker: every geometry/texture the GAME creates, minus every one it disposes
  await page.evaluate(()=>{
    window.__live={geo:0,tex:0};
    for(const name of ['Texture','CanvasTexture','DataTexture','CompressedTexture']){
      const C=THREE[name]; if(!C) continue;
      THREE[name]=function(...a){ const t=new C(...a); t.__tracked=1; window.__live.tex++; return t; };
      THREE[name].prototype=C.prototype;
    }
    const TP=THREE.Texture.prototype, otd=TP.dispose;
    TP.dispose=function(){ if(this.__tracked){ this.__tracked=0; window.__live.tex--; } return otd.call(this); };
    const GP=THREE.BufferGeometry.prototype, ogd=GP.dispose, osa=GP.setAttribute;
    GP.setAttribute=function(n,attr){ if(n==='position'&&!this.__tracked){ this.__tracked=1; window.__live.geo++; } return osa.call(this,n,attr); };
    GP.dispose=function(){ if(this.__tracked){ this.__tracked=0; window.__live.geo--; } return ogd.call(this); };
  });
  await page.evaluate(()=>{ try{ startGame(false,'aluvian'); }catch(e){} });
  // A fresh 'aluvian' recruit spawns INSIDE the Training Academy (inDungeon===true, curDungeon.academy),
  // and startGame ends by holding an arrival portalTransit until the overworld streams in. enterDungeon()
  // no-ops while inDungeon||portalTransit is truthy (js/game.js: "already below, or a transit is already
  // carrying you"), so the warm-up delve below would silently no-op and inDungeon would never flip — the
  // old fixed sleep(5000) raced that transit and hit the line-100 timeout under SwiftShader. Step out
  // through the Academy portal and wait for BOTH inDungeon and portalTransit to clear before driving the
  // delve/hub/capital cycle. Same async-boot race class as #974/#979. (#981)
  await waitForMain(page,()=>typeof scenery!=="undefined"&&scenery.length>0
    &&typeof inDungeon!=="undefined"&&inDungeon===true
    &&typeof curDungeon!=="undefined"&&curDungeon&&curDungeon.academy,{timeout:30000});
  await page.evaluate(()=>{ try{ player.academy=player.academy||{}; if(typeof academyExit==="function") academyExit(); }catch(e){} });
  await waitForMain(page,()=>typeof inDungeon!=="undefined"&&!inDungeon,{timeout:30000});
  // #986: the warm-up enterDungeon() below no-ops while portalTransit is set, but under swiftshader the
  // arrival tube's game-time maxHold stretches past any fixed real-time ceiling (clamped-dt frames), so
  // waiting on it deterministically timed out on software GL. Give it a chance to clear naturally, then
  // force it (mirroring the game's own maxHold hard cap) so a slow renderer can't deadlock the gate.
  await waitForMain(page,()=>typeof portalTransit==="undefined"||!portalTransit,{timeout:15000}).catch(()=>{});
  await page.evaluate(()=>{ if(typeof portalTransit!=="undefined"&&portalTransit) portalTransit=null; });
  await sleep(2000);   // let a few cullWorld frames re-show the overworld and stream the starting town in
  const mem=()=>page.evaluate(()=>({g:renderer.info.memory.geometries,t:renderer.info.memory.textures,
    lgeo:window.__live.geo,ltex:window.__live.tex,labels:LABELS.length}));
  const settle=async(maxMs=25000)=>{ let prev=await mem(), stable=0;
    for(let t=0;t<maxMs&&stable<2;t+=700){ await sleep(700); const cur=await mem();
      stable=(cur.g===prev.g&&cur.t===prev.t&&cur.lgeo===prev.lgeo&&cur.ltex===prev.ltex)?stable+1:0; prev=cur; }
    return prev; };
  // #991: enterDungeon() no-ops while a portalTransit tube is winding down (js/game.js:
  // `if(inDungeon||portalTransit) return`), and the cycle transitions (exitDungeon / arriveAt on return
  // from the evict section) can leave one active for a few seconds. Before each transition, wait briefly
  // for it to clear then FORCE it null (mirroring the game's own maxHold and the #986 boot gate), so
  // enterDungeon isn't silently dropped and inDungeon reliably flips. Never READ portalTransit raw out of
  // an evaluate — it's a circular object and marshals to undefined (same hazard as `scene`, line 38).
  const clearTransit=async()=>{
    await page.waitForFunction('typeof portalTransit==="undefined"||!portalTransit',{timeout:8000}).catch(()=>{});
    await page.evaluate(()=>{ if(typeof portalTransit!=="undefined"&&portalTransit) portalTransit=null; });
  };

  // ── 1. unit: acShared skip + LABELS behaviour ─────────────────────────────────────────────
  const unit=await page.evaluate(()=>{
    const out={};
    const g=new THREE.Group();
    const mk=shared=>{ const m=new THREE.Mesh(new THREE.BoxGeometry(1,1,1),new THREE.MeshBasicMaterial());
      m.userData.acShared=shared; let n=0; const gd=m.geometry.dispose.bind(m.geometry);
      m.geometry.dispose=()=>{n++;gd();}; m._count=()=>n; g.add(m); return m; };
    const plain=mk(false), shared=mk(true);
    const lbl=labelSprite("harness-probe","npcs"); g.add(lbl);
    const lblRegistered=LABELS.indexOf(lbl)>=0;
    scene.add(g);
    releaseObjectGPU(g);
    out.releasePlain=plain._count()===1;           // plain geometry released
    out.releaseShared=shared._count()===0;         // cache-shared geometry untouched
    out.releaseKeepsParent=g.parent===scene;       // still attached — eviction is not retirement
    out.releaseKeepsLabel=LABELS.indexOf(lbl)>=0===lblRegistered;  // registry untouched
    disposeObject3D(g);
    out.disposePlain=plain._count()===2;           // full dispose frees it again
    out.disposeShared=shared._count()===0;         // ...but never the shared subtree
    out.disposeDetaches=g.parent===null;
    out.disposePrunesLabel=LABELS.indexOf(lbl)<0;  // #22: nametag left the registry
    return out;
  });
  for(const k of Object.keys(unit)) check('unit.'+k,unit[k]);

  // ── warm-up: dungeon + hub + two capitals once (caches fill, regions upload), then freeze ──
  const dgnName=await page.evaluate(()=>{
    const real=AC_DGN?Object.keys(AC_DGN):[];
    return real.find(n=>DUNGEONS.some(d=>d.name===n))||(DUNGEONS[0]&&DUNGEONS[0].name)||null;
  });
  const capitals=await page.evaluate(()=>CITIES.filter(c=>c.capital).slice(0,2).map(c=>({x:c.x,z:c.z})));
  await page.evaluate(name=>{ const def=DUNGEONS.find(d=>d.name===name); enterDungeon(def,player.x,player.z); },dgnName);
  await page.waitForFunction('inDungeon===true',{timeout:20000});
  await sleep(2000);
  await page.evaluate(()=>exitDungeon(true)); await sleep(1000);
  await page.evaluate(()=>enterNetwork(player.x,player.z)); await sleep(1000);
  await page.evaluate(()=>exitNetwork(netReturn)); await sleep(1000);
  await page.evaluate(p=>arriveAt(p.x,p.z),capitals[0]); await settle(15000);   // capital A uploads
  await page.evaluate(p=>arriveAt(p.x,p.z),capitals[1]); await settle(15000);   // …now detached at B
  await page.setRequestInterception(true);
  page.on('request',r=>{ r.url().includes('/assets/')?r.abort():r.continue(); });
  await settle(30000);

  // ── 2. cullWorld eviction tier (run FIRST: capital A is still uploaded + freshly detached) ──
  {
    const ev=await page.evaluate(async()=>{
      const px=player.x,pz=player.z, farDetached=[];
      const scan=o=>{ if(o&&!o.parent&&o.userData._evT&&!o.userData._evD
        &&Math.hypot(o.position.x-px,o.position.z-pz)>CULL_EVICT_R+50) farDetached.push(o); };
      for(const n of npcs) scan(n.mesh); for(const s of shops) scan(s.mesh);
      for(const o of scenery) scan(o); for(const nd of nodes) scan(nd.mesh);
      for(const o of farDetached) o.userData._evT=-1e9;          // fast-forward the 5-minute dwell
      const before={g:renderer.info.memory.geometries,t:renderer.info.memory.textures};
      let evicted=0;                                              // slow SwiftShader frames: poll the sweep
      for(let t=0;t<40000;t+=800){ await new Promise(r=>setTimeout(r,800));
        evicted=farDetached.filter(o=>o.userData._evD===true).length;
        if(evicted===farDetached.length) break; }
      const after={g:renderer.info.memory.geometries,t:renderer.info.memory.textures};
      return {far:farDetached.length,evicted,before,after};
    });
    check('evict.far-detached-exist',ev.far>20,`far detached objects=${ev.far}`);
    check('evict.marks-and-releases',ev.far>0&&ev.evicted===ev.far,`evicted ${ev.evicted}/${ev.far}`);
    check('evict.gpu-counts-drop',ev.after.g<ev.before.g,
      `g ${ev.before.g} → ${ev.after.g}, t ${ev.before.t} → ${ev.after.t}`);
    // returning must re-attach and re-upload cleanly (no errors, flags reset)
    await page.evaluate(p=>arriveAt(p.x,p.z),capitals[0]); await settle();
    const back=await page.evaluate(()=>{
      let reattached=0; const scan=o=>{ if(o&&o.parent&&o.userData._evT===0&&o.userData._evD===false) reattached++; };
      for(const n of npcs) scan(n.mesh); for(const s of shops) scan(s.mesh);
      return {reattached,g:renderer.info.memory.geometries};
    });
    check('evict.reattaches-on-return',back.reattached>0,`re-attached=${back.reattached}, g=${back.g} (re-uploaded)`);
  }

  // suspend natural eviction so it can't deflate the cycle measurements
  await page.evaluate(()=>{ CULL_EVICT_S=1e9; });

  // ── 3. dungeon enter/exit cycles ──────────────────────────────────────────────────────────
  if(dgnName){
    const cyc=[];
    for(let i=0;i<3;i++){
      await clearTransit();   // #991: the return-from-evict arriveAt (and each cycle's exitDungeon) can leave a transit winding down
      await page.evaluate(name=>{ const def=DUNGEONS.find(d=>d.name===name);
        enterDungeon(def,player.x,player.z); },dgnName);
      // #993: enterDungeon builds ASYNC — startPortalTransit's callback _dungeonEnter sets inDungeon AND
      // populates dungeonObjs/curDungeon after minHold (~0.7s). Waiting on inDungeon alone could catch a
      // transient flip before the entry settled, so `inside` was sometimes measured on an incomplete/reset
      // entry (dungeonObjs=0) — a pure-timing flake. Wait for the FULLY-built state before measuring.
      await page.waitForFunction('typeof inDungeon!=="undefined"&&inDungeon===true'
        +'&&typeof curDungeon!=="undefined"&&!!curDungeon'
        +'&&typeof dungeonObjs!=="undefined"&&dungeonObjs.length>0',{timeout:15000});
      const inside=await settle();
      await page.evaluate(()=>exitDungeon(true));
      cyc.push({inside,after:await settle()});
    }
    const base=cyc[0].after, last=cyc[2].after;
    check('dungeon.builds-inside',cyc[1].inside.lgeo>base.lgeo+50,
      `live geo inside=${cyc[1].inside.lgeo} vs after-exit=${base.lgeo} (dungeon: ${dgnName})`);
    check('dungeon.no-ratchet-across-cycles',last.lgeo<=base.lgeo+60&&last.ltex<=base.ltex+30,
      `after cycle1 liveGeo=${base.lgeo}/liveTex=${base.ltex}, cycle3 liveGeo=${last.lgeo}/liveTex=${last.ltex}`);
    check('dungeon.labels-registry-stable',Math.abs(last.labels-base.labels)<=5,
      `labels ${base.labels} → ${last.labels}`);
  } else check('dungeon.no-ratchet-across-cycles',false,'no dungeon defs loaded');

  // ── 4. town-network hub cycles ────────────────────────────────────────────────────────────
  {
    const cyc=[];
    for(let i=0;i<3;i++){
      await clearTransit();   // #991: defensive — the prior exitDungeon/exitNetwork can leave a transit active (enterNetwork ignores it today, but keep the cycle transitions uniform)
      await page.evaluate(()=>enterNetwork(player.x,player.z));
      const inside=await settle();
      await page.evaluate(()=>exitNetwork(netReturn));
      cyc.push({inside,after:await settle()});
    }
    check('network.no-ratchet-across-cycles',cyc[2].after.lgeo<=cyc[0].after.lgeo+40&&cyc[2].after.ltex<=cyc[0].after.ltex+20,
      `after cycle1 liveGeo=${cyc[0].after.lgeo}/liveTex=${cyc[0].after.ltex}, cycle3 liveGeo=${cyc[2].after.lgeo}/liveTex=${cyc[2].after.ltex}`);
  }

  // ── 5. buildWorld rebuild does not stack worlds (rebuild #1 vs #2 — a stacked world ratchets) ──
  {
    await page.evaluate(()=>buildWorld()); const r1=await settle(30000);
    await page.evaluate(()=>buildWorld()); const r2=await settle(30000);
    // #993: gate on GPU RESIDENCY (renderer.info.memory), not the live create-minus-dispose tracker.
    // buildWorld() spins up a large pool of transient/streamed geometries, so window.__live balloons to
    // ~160k while only ~5-12k are ever GPU-resident — a +216 tracker delta there is ~0.1% noise and does
    // NOT mean the world stacked. #232 is about GPU residency, which DROPS across the rebuild (no
    // stacking), so measure that, exactly as evict.gpu-counts-drop does.
    check('rebuild.no-stacked-world',r2.g<=r1.g+80&&r2.t<=r1.t+40,
      `rebuild1 gpuGeo=${r1.g}/gpuTex=${r1.t}, rebuild2 gpuGeo=${r2.g}/gpuTex=${r2.t}`);
  }

  check('no-page-errors',errs.length===0,errs.slice(0,5).join(' | ').slice(0,400));
  await browser.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{console.error('FATAL',e);process.exit(1)});
