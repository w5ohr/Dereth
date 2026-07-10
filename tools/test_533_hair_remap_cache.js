// #533 regression harness: every female must NOT default to the same ginger hair.
//
// Root cause (the real one — see the closed PR #552 for the misdiagnosis this guards against):
// AC head hair colour is a texture RGB-remap through palettes.json (AC_HEAD_REMAP). index.json and
// palettes.json are fetched in parallel, but a head only needs index.json to build. When a head was
// built BEFORE palettes.json arrived, acHeadTexture() had no remap to apply, served the RAW texture
// (a warm ginger), and then permanently cached that raw result under the texture+remap key. Every
// later rebuild — and every race — reused the cached raw texture. Hence the shared ginger.
//
// The fix: when a remap was requested but AC_HEAD_REMAP has not loaded, do NOT cache the raw texture
// (`remapPending` -> delete instead of store), so the next rebuild re-remaps correctly.
//
// NOTE ON A REJECTED THEORY: it was claimed the hair textures are greyscale and the hair palettes only
// map non-grey colours, so the remap could never work. That is false and this harness pins it down:
// 050011FD.png has ZERO grey pixels, and palettes 04001FB1 / 04001FBE each remap 100% of its colours.
// If someone "fixes" this by tinting the mesh instead of remapping the texture, these assertions fail.
//
// Includes a NEGATIVE CONTROL that reproduces the pre-fix caching and asserts the ginger actually
// sticks — proving the assertions detect the bug rather than passing vacuously.
//
// Run:  node tools/test_533_hair_remap_cache.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
const HAIR_TEX="050011FD";   // the female hair texture named in the issue

(async()=>{
  const browser=await puppeteer.launch({executablePath:CHROME,headless:'new',
    args:['--no-sandbox','--use-gl=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
  const page=await browser.newPage();
  const errs=[]; page.on('pageerror',e=>{ if(!BENIGN.test(e.message)) errs.push(e.message); });
  await page.goto(URL,{waitUntil:'load',timeout:60000});
  await waitForMain(page,()=>typeof startGame==="function",{timeout:30000});
  await page.evaluate(()=>{ try{ startGame(false,'aluvian'); }catch(e){} });
  await waitForMain(page,()=>typeof player!=="undefined"&&player&&typeof acHeadTexture==="function"&&typeof AC_HEADS!=="undefined"&&AC_HEADS,{timeout:30000});
  // palettes.json load is what the whole bug is about — wait for it explicitly
  await waitForMain(page,()=>typeof AC_HEAD_REMAP!=="undefined"&&!!AC_HEAD_REMAP,{timeout:30000});

  const data=await page.evaluate(async(HAIR_TEX)=>{
    const meanRGB=tex=>{                       // average colour of a texture's pixels
      const img=tex.image, cv=document.createElement('canvas');
      cv.width=img.width; cv.height=img.height;
      const cx=cv.getContext('2d'); cx.drawImage(img,0,0);
      const d=cx.getImageData(0,0,cv.width,cv.height).data;
      let r=0,g=0,b=0,n=0;
      for(let i=0;i<d.length;i+=4){ if(d[i+3]<8) continue; r+=d[i]; g+=d[i+1]; b+=d[i+2]; n++; }
      return n?[Math.round(r/n),Math.round(g/n),Math.round(b/n)]:[0,0,0];
    };
    const load=(did,rms)=>new Promise(r=>acHeadTexture(did,rms,r));
    const out={};

    // ── the texture / palette facts the rejected "greyscale" theory got wrong
    const px=await new Promise(res=>{ const im=new Image();
      im.onload=()=>{ const cv=document.createElement('canvas'); cv.width=im.width; cv.height=im.height;
        const cx=cv.getContext('2d'); cx.drawImage(im,0,0);
        const d=cx.getImageData(0,0,cv.width,cv.height).data;
        let grey=0,tot=0,maxChroma=0; const cols=new Set();
        for(let i=0;i<d.length;i+=4){ const r=d[i],g=d[i+1],b=d[i+2];
          const ch=Math.max(r,g,b)-Math.min(r,g,b); if(ch>maxChroma) maxChroma=ch;
          if(r===g&&g===b) grey++; tot++;
          cols.add(((r<<16)|(g<<8)|b).toString(16).padStart(6,'0')); }
        res({grey,tot,maxChroma,cols:[...cols]}); };
      im.src='assets/acheads/tex/'+HAIR_TEX+'.png'; });
    out.tex={greyPixels:px.grey,totalPixels:px.tot,maxChroma:px.maxChroma,uniqueColors:px.cols.length};

    // per-race female hair palette DIDs, and how much of the texture each palette covers
    const races=["aluvian","gharundim","sho","viamontian"];
    out.hairPals={}; out.paletteCoverage={};
    for(const race of races){
      const app=JSON.parse(JSON.stringify(player.appearance));
      app.race=race; app.sex="female"; app.gender="female";
      const H=acHeadOpts(app);
      const did=H&&H.hairPals&&H.hairPals.length?H.hairPals[0]:null;
      out.hairPals[race]=did;
      if(did&&AC_HEAD_REMAP&&AC_HEAD_REMAP[did]){
        const rm=AC_HEAD_REMAP[did];
        const hit=px.cols.filter(c=>rm[c]!==undefined).length;
        out.paletteCoverage[race]={did,keys:Object.keys(rm).length,matched:hit,ofColors:px.cols.length};
      }
    }

    // ── the actual bug: build the texture with palettes MISSING, then again with them present
    const skin=acHeadOpts(player.appearance).skinTones[0];
    const hair=out.hairPals.aluvian;
    const key=HAIR_TEX+"|"+[skin,hair].filter(Boolean).join(",");
    const saved=AC_HEAD_REMAP;

    delete _acHeadTexCache[key]; _acHeadTexCb[key]=null;
    AC_HEAD_REMAP=null;                                     // simulate palettes.json not yet arrived
    const raw=await load(HAIR_TEX,[skin,hair]);
    out.cachedWhileRemapPending = (key in _acHeadTexCache) ? "CACHED" : "not-cached";
    out.rawIsCanvas = !!(raw && raw.isCanvasTexture);
    out.rawMean = raw?meanRGB(raw):null;

    AC_HEAD_REMAP=saved;                                    // palettes land
    const remapped=await load(HAIR_TEX,[skin,hair]);
    out.cachedAfterRemap = (key in _acHeadTexCache) ? "CACHED" : "not-cached";
    out.remappedIsCanvas = !!(remapped && remapped.isCanvasTexture);
    out.remappedMean = remapped?meanRGB(remapped):null;
    out.sameObject = raw===remapped;

    // ── NEGATIVE CONTROL: reproduce the pre-fix caching (store the raw texture) and confirm the
    //    ginger sticks forever — i.e. these assertions would catch the regression.
    delete _acHeadTexCache[key]; _acHeadTexCb[key]=null;
    AC_HEAD_REMAP=null;
    const raw2=await load(HAIR_TEX,[skin,hair]);
    _acHeadTexCache[key]=raw2;                              // <- what the buggy code did
    AC_HEAD_REMAP=saved;
    const afterBug=await load(HAIR_TEX,[skin,hair]);
    out.control={ servedStaleRaw: afterBug===raw2, mean: afterBug?meanRGB(afterBug):null };

    delete _acHeadTexCache[key]; _acHeadTexCb[key]=null;    // leave the cache clean
    return out;
  },HAIR_TEX);

  const dist=(a,b)=>a&&b?Math.hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]):-1;

  // ── pin the asset facts (guards against the "hair textures are greyscale" misdiagnosis)
  check("asset.hair-texture-is-not-greyscale", data.tex.greyPixels===0 && data.tex.maxChroma>20,
    `grey=${data.tex.greyPixels}/${data.tex.totalPixels} px, maxChroma=${data.tex.maxChroma}`);
  const cov=Object.values(data.paletteCoverage);
  check("asset.hair-palettes-remap-every-texture-colour",
    cov.length>0 && cov.every(c=>c.matched===c.ofColors),
    cov.map(c=>`${c.did}:${c.matched}/${c.ofColors}`).join(' '));

  // ── the fix itself
  check("fix.raw-texture-not-cached-while-palettes-pending", data.cachedWhileRemapPending==="not-cached",
    `cache state=${data.cachedWhileRemapPending}`);
  check("fix.raw-serve-is-unremapped", data.rawIsCanvas===false, `rawIsCanvasTexture=${data.rawIsCanvas}`);
  check("fix.rebuild-after-palettes-produces-remapped-texture", data.remappedIsCanvas===true,
    `remappedIsCanvasTexture=${data.remappedIsCanvas}`);
  check("fix.rebuild-is-a-fresh-object-not-the-cached-raw", data.sameObject===false, `same=${data.sameObject}`);
  check("fix.remapped-texture-is-cached", data.cachedAfterRemap==="CACHED", `cache state=${data.cachedAfterRemap}`);
  check("fix.remap-actually-changes-the-pixels", dist(data.rawMean,data.remappedMean)>=12,
    `raw=rgb(${data.rawMean}) remapped=rgb(${data.remappedMean}) dist=${dist(data.rawMean,data.remappedMean).toFixed(1)}`);

  // ── per-race defaults really differ (the reported symptom)
  const pals=data.hairPals;
  check("race.sho-and-gharundim-do-not-use-aluvian-hair-palette",
    pals.sho!==pals.aluvian && pals.gharundim!==pals.aluvian,
    `aluvian=${pals.aluvian} gharundim=${pals.gharundim} sho=${pals.sho} viamontian=${pals.viamontian}`);
  check("race.at-least-two-distinct-hair-palettes", new Set(Object.values(pals).filter(Boolean)).size>=2,
    `distinct=${new Set(Object.values(pals).filter(Boolean)).size}`);

  // ── NEGATIVE CONTROL
  check("control.pre-fix-caching-serves-stale-raw-forever", data.control.servedStaleRaw===true,
    `stale raw re-served=${data.control.servedStaleRaw} mean=rgb(${data.control.mean}) — this is the pre-fix behaviour`);

  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
