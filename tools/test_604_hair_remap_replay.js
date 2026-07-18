// #604 regression harness: a head built BEFORE palettes.json lands must not keep its raw texture.
//
// #533 stopped acHeadTexture() from CACHING the raw texture when a remap was requested before
// AC_HEAD_REMAP loaded. But the material it had already been handed keeps that raw THREE.Texture
// until something rebuilds the head. The palettes handler rebuilds the player avatar and an OPEN
// character creator — nothing else. So an NPC head, or a creator preview the user never clicks,
// stays raw auburn forever.
//
// The fix records every raw serve and replays it once AC_HEAD_REMAP arrives (acHeadRemapArrived()),
// re-invoking each queued callback with the properly remapped texture so mat.map is swapped in place.
//
// ── on the REJECTED fix in the issue ────────────────────────────────────────────────────────────
// #604 proposed reordering the hair slot's maps to [hairDID, skinDID]. Measured against the real
// assets, that is wrong: the skin palette covers 47.1% of 050011FD's pixels (NOT 100%), so today's
// [skinDID, hairDID] order lets the skin pass take the 7,715 scalp pixels and the hair pass take the
// remaining 8,669 strand pixels. Hair-first matches 100%, starves the skin pass, drags the scalp into
// the palette's grey ramp, and collapses four heritages onto two greys. This harness pins the correct
// order down so nobody "fixes" it that way.
//
// Run:  node tools/test_604_hair_remap_replay.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
const HAIR_TEX="050011FD";

(async()=>{
  const browser=await puppeteer.launch({executablePath:CHROME,headless:'new',
    args:['--no-sandbox','--use-gl=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
  const page=await browser.newPage();
  const errs=[]; page.on('pageerror',e=>{ if(!BENIGN.test(e.message)) errs.push(e.message); });
  await page.goto(URL,{waitUntil:'load',timeout:60000});
  await waitForMain(page,()=>typeof startGame==="function",{timeout:30000});
  await page.evaluate(()=>{ try{ startGame(false,'aluvian'); }catch(e){} });
  await waitForMain(page,()=>typeof player!=="undefined"&&player&&typeof acHeadTexture==="function"
    &&typeof acHeadRemapArrived==="function"&&typeof AC_HEADS!=="undefined"&&AC_HEADS,{timeout:30000});
  await waitForMain(page,()=>typeof AC_HEAD_REMAP!=="undefined"&&!!AC_HEAD_REMAP,{timeout:30000});

  const data=await page.evaluate(async(HAIR_TEX)=>{
    const meanRGB=tex=>{
      const img=tex.image, cv=document.createElement('canvas');
      cv.width=img.width; cv.height=img.height;
      const cx=cv.getContext('2d'); cx.drawImage(img,0,0);
      const d=cx.getImageData(0,0,cv.width,cv.height).data;
      let r=0,g=0,b=0,n=0;
      for(let i=0;i<d.length;i+=4){ if(d[i+3]<8) continue; r+=d[i]; g+=d[i+1]; b+=d[i+2]; n++; }
      return n?[Math.round(r/n),Math.round(g/n),Math.round(b/n)]:[0,0,0];
    };
    const load=(did,rms)=>new Promise(r=>acHeadTexture(did,rms,r));
    const clear=key=>{ delete _acHeadTexCache[key]; _acHeadTexCb[key]=null; };
    const out={};
    const saved=AC_HEAD_REMAP;
    const H=AC_HEADS.heritages;

    // ── palette coverage: the fact that makes skin-first correct (and hair-first wrong)
    const px=await new Promise(res=>{ const im=new Image();
      im.onload=()=>{ const cv=document.createElement('canvas'); cv.width=im.width; cv.height=im.height;
        const cx=cv.getContext('2d'); cx.drawImage(im,0,0);
        const d=cx.getImageData(0,0,cv.width,cv.height).data;
        const cols={}; let tot=0;
        for(let i=0;i<d.length;i+=4){ const h=((d[i]<<16)|(d[i+1]<<8)|d[i+2]).toString(16).padStart(6,'0');
          cols[h]=(cols[h]||0)+1; tot++; }
        res({cols,tot}); };
      im.src='assets/acheads/tex/'+HAIR_TEX+'.png'; });
    const sho=H['sho'].female;
    const cover=did=>{ const m=saved[did]||{}; let n=0;
      for(const h in px.cols) if(m[h]!==undefined) n+=px.cols[h];
      return 100*n/px.tot; };
    out.skinCoverage=+cover(sho.skinTones[0]).toFixed(1);
    out.hairCoverage=+cover(sho.hairPals[0]).toFixed(1);

    // ── THE BUG: build a head texture while palettes are missing, then let them land.
    const skin=sho.skinTones[0], hair=sho.hairPals[0];
    const key=HAIR_TEX+"|"+[skin,hair].join(",");

    clear(key); _acHeadPending.length=0;
    AC_HEAD_REMAP=null;
    // stand in for a material: whatever texture we're handed lives here
    const mat={map:null};
    await new Promise(r=>acHeadTexture(HAIR_TEX,[skin,hair],t=>{ mat.map=t; r(); }));
    out.rawServed = !!(mat.map && !mat.map.isCanvasTexture);
    out.rawMean = mat.map?meanRGB(mat.map):null;
    // _acHeadPending is a PROCESS-GLOBAL queue, and the live render loop is still building Academy NPC
    // heads while AC_HEAD_REMAP is null — every concurrent raw serve pushes its OWN (different-key) entry,
    // so the raw global length is nondeterministic (2/3/6…). Count only the entry THIS test recorded:
    // same head texture + same skin/hair remap. Exactly one such entry exists (a shared key starts a
    // single in-flight load; extra callers just join its cbs, they don't push a second entry). (#977)
    const mine=e=>e.texDID===HAIR_TEX && e.rms.length===2 && e.rms[0]===skin && e.rms[1]===hair;
    out.pendingRecorded = _acHeadPending.filter(mine).length;

    // palettes land. Snapshot the WHOLE queue's queued-callback count right before draining (synchronously,
    // so no concurrent Image.onload can grow it in between): acHeadRemapArrived must replay exactly that
    // many — that faithful-drain invariant is the real thing under test, and it holds for any queue size.
    const totalQueuedCbs = _acHeadPending.reduce((s,e)=>s+e.cbs.length,0);
    AC_HEAD_REMAP=saved;
    out.replayed = acHeadRemapArrived();
    out.totalQueuedCbs = totalQueuedCbs;
    await new Promise(r=>setTimeout(r,400));     // the replayed load is async (Image.onload)
    out.afterMapIsCanvas = !!(mat.map && mat.map.isCanvasTexture);
    out.afterMean = mat.map?meanRGB(mat.map):null;
    out.pendingDrained = _acHeadPending.length;

    // ── replay is idempotent / safe to call with nothing pending
    out.replayEmpty = acHeadRemapArrived();

    // ── CONTROL: without the replay, the material keeps the raw texture forever.
    clear(key); _acHeadPending.length=0;
    AC_HEAD_REMAP=null;
    const mat2={map:null};
    await new Promise(r=>acHeadTexture(HAIR_TEX,[skin,hair],t=>{ mat2.map=t; r(); }));
    AC_HEAD_REMAP=saved;
    _acHeadPending.length=0;                      // simulate the pre-fix world: nothing was recorded
    acHeadRemapArrived();
    await new Promise(r=>setTimeout(r,200));
    out.control_stillRaw = !!(mat2.map && !mat2.map.isCanvasTexture);
    out.control_mean = mat2.map?meanRGB(mat2.map):null;

    // ── the remapped result differs per heritage (the user-visible symptom)
    out.perRace={};
    for(const race of ["aluvian","gharu'ndim","sho","viamontian"]){
      const f=H[race].female, sk=f.skinTones[0], hp=f.hairPals[0];
      const k=HAIR_TEX+"|"+[sk,hp].join(",");
      clear(k);
      const t=await load(HAIR_TEX,[sk,hp]);
      out.perRace[race]={hairDID:hp, mean:meanRGB(t), canvas:!!t.isCanvasTexture};
      clear(k);
    }

    clear(key); _acHeadPending.length=0;
    return out;
  },HAIR_TEX);

  const dist=(a,b)=>a&&b?Math.hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]):-1;

  // ── the measurement that refutes the issue's proposed reorder
  check("palette.skin-covers-far-less-than-the-hair-palette",
    data.skinCoverage>40 && data.skinCoverage<60 && data.hairCoverage===100,
    `skin=${data.skinCoverage}% hair=${data.hairCoverage}% — skin-first is correct; hair-first would starve the skin pass`);

  // ── the bug and the fix
  check("bug.raw-texture-served-while-palettes-pending", data.rawServed===true,
    `served a plain (non-canvas) texture = ${data.rawServed}, mean rgb(${data.rawMean})`);
  check("fix.raw-serve-is-recorded-for-replay", data.pendingRecorded===1,
    `_acHeadPending entries for this head=${data.pendingRecorded}`);
  check("fix.replay-runs-when-palettes-arrive", data.replayed===data.totalQueuedCbs && data.replayed>=1,
    `callbacks replayed=${data.replayed} (== ${data.totalQueuedCbs} queued — drains the whole global queue faithfully)`);
  check("fix.material-map-swapped-to-remapped-texture", data.afterMapIsCanvas===true,
    `mat.map.isCanvasTexture=${data.afterMapIsCanvas}`);
  check("fix.pixels-actually-changed", dist(data.rawMean,data.afterMean)>=12,
    `raw=rgb(${data.rawMean}) -> after=rgb(${data.afterMean}) dist=${dist(data.rawMean,data.afterMean).toFixed(1)}`);
  check("fix.pending-queue-drained", data.pendingDrained===0, `left=${data.pendingDrained}`);
  check("fix.replay-with-nothing-pending-is-a-no-op", data.replayEmpty===0, `replayed=${data.replayEmpty}`);

  // ── CONTROL
  check("control.without-replay-the-material-stays-raw", data.control_stillRaw===true,
    `still a raw texture=${data.control_stillRaw} mean=rgb(${data.control_mean}) — the pre-fix behaviour`);

  // ── per-heritage outcome
  const pr=data.perRace, races=Object.keys(pr);
  check("race.every-heritage-gets-a-remapped-canvas-texture", races.every(r=>pr[r].canvas),
    races.map(r=>`${r}:${pr[r].canvas}`).join(' '));
  const means=races.map(r=>pr[r].mean.join(','));
  check("race.at-least-three-distinct-hair-tones", new Set(means).size>=3,
    races.map(r=>`${r}=rgb(${pr[r].mean})`).join('  '));
  check("race.sho-is-dark-not-auburn", pr['sho'].mean[0]<90 && pr['sho'].mean[0]-pr['sho'].mean[2]<60,
    `sho=rgb(${pr['sho'].mean}) (raw auburn is ~rgb(97,52,43))`);

  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
