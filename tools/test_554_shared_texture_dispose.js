// #554 regression harness (part 2 of 2 — see also test_554_town_build_gate.js).
//
// tbTex() memoises ONE texture object per PNG in _tbTexCache and hands that same object to every
// building material in every town, and to the open-world structures via tbBuildMesh. When a town
// streams beyond TB_DROP_R, tbDropTown -> disposeObject3D -> _dispMat also calls _dispTex(mat.map).
// Those maps ARE the shared cache entries. Without the _acShared flag, dropping one town disposed a
// GPU texture still referenced by other visible towns, by the open-world structs, and by that same
// town when it streams back in (the cache returns the now-disposed object).
//
// On a compliant renderer THREE silently re-uploads from texture.image, so the glitch is invisible;
// under ANGLE-Metal a disposed-then-reused texture is left GPU-incomplete and samples as WHITE —
// the featureless white building masses reported at Holtburg.
//
// The fix flags the cached textures `_acShared=true` so _dispTex skips them — the same convention
// (#326) already protecting shared canvas textures, luminance normal maps and merged archetype meshes.
// Per-build materials and geometry are STILL disposed, so nothing leaks.
//
// Includes a NEGATIVE CONTROL: the same disposal path against an unflagged texture must dispose it.
//
// Run:  node tools/test_554_shared_texture_dispose.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
const TEX="06003789.png";   // a real baked town-building texture

(async()=>{
  const browser=await puppeteer.launch({executablePath:CHROME,headless:'new',
    args:['--no-sandbox','--use-gl=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
  const page=await browser.newPage();
  const errs=[]; page.on('pageerror',e=>{ if(!BENIGN.test(e.message)) errs.push(e.message); });
  await page.goto(URL,{waitUntil:'load',timeout:60000});
  await waitForMain(page,()=>typeof startGame==="function",{timeout:30000});
  await page.evaluate(()=>{ try{ startGame(false,'aluvian'); }catch(e){} });
  await waitForMain(page,()=>typeof player!=="undefined"&&player&&typeof tbTex==="function"&&typeof _dispMat==="function",{timeout:30000});

  const data=await page.evaluate((TEX)=>{
    const spy=obj=>{ let n=0; const orig=obj.dispose&&obj.dispose.bind(obj);
      obj.dispose=function(){ n++; if(orig) return orig(); };
      return ()=>n; };

    const out={};

    // ── the cache is genuinely shared and flagged
    const t1=tbTex(TEX), t2=tbTex(TEX);
    out.memoised = t1===t2;
    out.inCache  = _tbTexCache[TEX]===t1;
    out.flagged  = t1._acShared===true;

    // ── dropping a material that uses the shared texture must NOT dispose the texture
    const texDisposed=spy(t1);
    const mat=new THREE.MeshStandardMaterial({map:t1});
    const matDisposed=spy(mat);
    _dispMat(mat);
    out.sharedTexDisposeCalls = texDisposed();
    out.materialDisposeCalls  = matDisposed();
    out.texStillInCache       = _tbTexCache[TEX]===t1;

    // ── _dispTex must skip it directly too
    const t1b=spy(t1);
    _dispTex(t1);
    out.dispTexDirectCalls = t1b();

    // ── per-build geometry is still reclaimed (no leak): an unflagged geometry disposes
    const geo=new THREE.BufferGeometry();
    const geoDisposed=spy(geo);
    _dispGeo(geo);
    out.unflaggedGeoDisposeCalls = geoDisposed();

    // ── NEGATIVE CONTROL: identical disposal path, texture NOT flagged -> must dispose.
    const raw=new THREE.Texture(); raw._acShared=false;
    const rawDisposed=spy(raw);
    const mat2=new THREE.MeshStandardMaterial({map:raw});
    _dispMat(mat2);
    out.control_unflaggedTexDisposeCalls = rawDisposed();

    // ── and a control on the real cache object with the flag stripped (the exact pre-fix state)
    const t3=new THREE.Texture(); t3._acShared=undefined;
    const t3Disposed=spy(t3);
    const mat3=new THREE.MeshStandardMaterial({map:t3});
    _dispMat(mat3);
    out.control_preFixFlagMissingDisposeCalls = t3Disposed();

    return out;
  },TEX);

  check("cache.tbTex-memoises-one-object-per-png", data.memoised===true, `t1===t2 -> ${data.memoised}`);
  check("cache.texture-registered-in-_tbTexCache", data.inCache===true, `_tbTexCache["${TEX}"]===t1 -> ${data.inCache}`);
  check("fix.shared-texture-is-flagged-_acShared", data.flagged===true, `_acShared=${data.flagged}`);

  check("fix.material-drop-does-not-dispose-shared-texture", data.sharedTexDisposeCalls===0,
    `dispose calls on shared texture=${data.sharedTexDisposeCalls} (expect 0)`);
  check("fix.per-build-material-is-still-disposed", data.materialDisposeCalls===1,
    `material dispose calls=${data.materialDisposeCalls} (expect 1 — no leak)`);
  check("fix.shared-texture-survives-in-cache", data.texStillInCache===true, `still cached=${data.texStillInCache}`);
  check("fix._dispTex-skips-shared-texture-directly", data.dispTexDirectCalls===0,
    `dispose calls=${data.dispTexDirectCalls} (expect 0)`);
  check("fix.unflagged-geometry-still-reclaimed", data.unflaggedGeoDisposeCalls===1,
    `geometry dispose calls=${data.unflaggedGeoDisposeCalls} (expect 1 — no leak)`);

  // ── NEGATIVE CONTROLS
  check("control.unflagged-texture-IS-disposed", data.control_unflaggedTexDisposeCalls===1,
    `dispose calls=${data.control_unflaggedTexDisposeCalls} (expect 1)`);
  check("control.pre-fix-missing-flag-disposes-shared-texture", data.control_preFixFlagMissingDisposeCalls===1,
    `dispose calls=${data.control_preFixFlagMissingDisposeCalls} (expect 1 — this is the pre-fix behaviour)`);

  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
