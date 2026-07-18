// Town plazas must be RED COBBLESTONE, not a flat untextured disc.
//
// addCity() drew the central plaza as `solid(0x8f8a7e)` — a flat, untextured, warm-grey material.
// With no map to break it up, the noon sun washed it out to a featureless white/cream disc in the
// middle of every town (most visible at Holtburg, where it reads as "white ground"). Distinct from
// #554: that was the town's BUILDINGS never being built; this is the procedural plaza underneath.
//
// The fix bakes a red tint into the cobble generator (cobbleTex(tint)) rather than multiplying a
// colour over the grey texture on the material: the cobbles sit at ~45% luminance, so any multiply
// dark enough to read as red comes out near-black. The plaza UVs are world-scaled so a cobble is the
// same physical size in a capital as in a hamlet, letting ONE shared texture serve all 56 towns.
//
// Controls prove the assertions are load-bearing: the old flat material carries no map, and the
// colour-multiply alternative really is too dark.
//
// Run:  node tools/test_plaza_red_cobble.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
  await waitForMain(page,()=>typeof scene!=="undefined"&&scene&&typeof plazaCobbleTex==="function"
    &&typeof cobbleTex==="function"&&typeof scenery!=="undefined",{timeout:30000});
  // A fresh recruit spawns INSIDE the Training Academy (inDungeon): the overworld is built but hidden,
  // and once the player moves, cullWorld distance-detaches far towns — so the plaza is not a reliable
  // live scene member at this point (this is what made the old scene.traverse probe find 0 plazas).
  // Step out through the Academy portal into the starting town (Holtburg for aluvian) so the overworld
  // is active and the plaza sits right under the player, exactly as a real arrival sees it. (#974)
  await page.evaluate(()=>{ try{ player.academy=player.academy||{}; if(typeof academyExit==="function") academyExit(); }catch(e){} });
  await waitForMain(page,()=>typeof inDungeon!=="undefined"&&!inDungeon,{timeout:30000});
  await sleep(1500);   // let a few cullWorld frames re-show the overworld and stream the town in

  const data=await page.evaluate(()=>{
    const meanOf=tex=>{                                  // average colour of a CanvasTexture
      const cv=tex.image, cx=cv.getContext('2d');
      const d=cx.getImageData(0,0,cv.width,cv.height).data;
      let r=0,g=0,b=0,n=0;
      for(let i=0;i<d.length;i+=4){ r+=d[i]; g+=d[i+1]; b+=d[i+2]; n++; }
      return [Math.round(r/n),Math.round(g/n),Math.round(b/n)];
    };
    const out={};

    // ── the default generator must be untouched (roads / stone floors still grey)
    out.greyMean = meanOf(cobbleTex());

    // ── the tinted generator is red
    const red=plazaCobbleTex();
    out.redMean = meanOf(red);
    out.memoised = plazaCobbleTex()===red;
    out.acShared = red._acShared===true;
    out.wrapsRepeat = red.wrapS===THREE.RepeatWrapping && red.wrapT===THREE.RepeatWrapping;

    // ── locate the plaza meshes. addCity pushes every plaza into the persistent scenery[] array, which
    //    holds them whether or not they're currently attached to the scene / visible — more robust than
    //    scene.traverse, which only walks currently-attached nodes (a distance-culled town is detached).
    //    Fall back to a scene walk if scenery[] isn't reachable. (#974)
    const plazas=[];
    const pool=(typeof scenery!=="undefined"&&scenery)?scenery:null;
    if(pool){ for(const o of pool){ if(o&&o.isMesh&&o.material&&o.material.map===red) plazas.push(o); } }
    if(!plazas.length){ scene.traverse(o=>{ if(o.isMesh && o.material && o.material.map===red) plazas.push(o); }); }
    out.plazaCount=plazas.length;

    if(plazas.length){
      const p=plazas[0];
      out.plazaHasMap = !!p.material.map;
      out.plazaReceivesShadow = p.receiveShadow===true;
      const uv=p.geometry.getAttribute('uv');
      let mn=Infinity,mx=-Infinity;
      for(let i=0;i<uv.count;i++){ const u=uv.getX(i); if(u<mn)mn=u; if(u>mx)mx=u; }
      out.uvSpan = mx-mn;                                 // >1 => the texture tiles rather than stretching
    }

    // ── no plaza is still the old flat solid
    let flat=0;
    scene.traverse(o=>{ if(o.isMesh && o.material && !o.material.map && o.material.color
      && o.material.color.getHex()===0x8f8a7e) flat++; });
    out.flatGreyPlazas=flat;

    // ── the plaza is the surface you actually stand on at a town centre: cast straight down at
    //    Holtburg's centre and confirm the first thing hit is a plaza wearing the red cobbles.
    //    (A camera render can't be used here — the rAF loop owns `cam`, and the player spawns in
    //    the Academy — so raycast the scene graph directly.)
    const town=CITIES.find(c=>c.name==="Holtburg");
    if(town && plazas.length){
      // World-culling hides distant scenery (the player spawns in the Academy), and Raycaster skips
      // invisible objects — force visibility for the probe, then restore it.
      const wasVisible=plazas.map(p=>p.visible);
      plazas.forEach(p=>{ p.visible=true; p.updateMatrixWorld(true); });
      // Don't use groundY() for the ray origin: it resolves to 0 until the terrain heightmap is in,
      // while the plaza geometry baked the REAL height at addCity() time (Holtburg's sits at y~72-82).
      // Start above the mesh's own bounds so the cast is valid regardless of heightmap state.
      const bb=new THREE.Box3().setFromObject(plazas.find(p=>Math.hypot(p.position.x-town.x,p.position.z-town.z)<1)||plazas[0]);
      const rc=new THREE.Raycaster(new THREE.Vector3(town.x, bb.max.y+50, town.z), new THREE.Vector3(0,-1,0), 0, 500);
      const hits=rc.intersectObjects(plazas,false);
      out.holtburgPlazaWorldY = +bb.max.y.toFixed(1);
      out.holtburgPlazaHit = hits.length>0;
      out.holtburgHitIsRedCobble = hits.length>0 && hits[0].object.material.map===red;
      out.holtburgHitDepth = hits.length>0 ? +(hits[0].distance).toFixed(2) : null;
      plazas.forEach((p,i)=>{ p.visible=wasVisible[i]; });
    }

    // ── CONTROL A: the pre-fix material carries no map at all (that's what washed out)
    out.control_oldMaterialHasMap = !!solid(0x8f8a7e).map;

    // ── CONTROL B: multiplying a red colour over the GREY cobbles is too dark to use.
    //    final = tint * texel; the grey texel is ~45% luminance, so even a bright red tint
    //    lands far below the baked red's luminance.
    const lum=c=>0.299*c[0]+0.587*c[1]+0.114*c[2];
    const grey=out.greyMean, tint=[0xd9/255,0x69/255,0x5a/255];
    const multiplied=[grey[0]*tint[0],grey[1]*tint[1],grey[2]*tint[2]];
    out.control_multipliedLum = lum(multiplied);
    out.bakedLum = lum(out.redMean);
    return out;
  });

  const [gr,gg,gb]=data.greyMean, [rr,rg,rb]=data.redMean;

  check("generator.default-cobble-still-warm-grey", Math.abs(gr-gg)<12 && Math.abs(gg-gb)<12 && gr<150,
    `default mean=rgb(${data.greyMean})  (roads/stone floors unchanged)`);
  check("generator.tinted-cobble-is-red", rr>rg+40 && rr>rb+50,
    `red mean=rgb(${data.redMean})`);
  check("generator.red-is-not-washed-out", rr>120 && rr<230, `red channel=${rr}`);

  check("texture.plaza-texture-memoised", data.memoised===true, `same object=${data.memoised}`);
  check("texture.flagged-_acShared", data.acShared===true, `_acShared=${data.acShared} (survives town/scenery eviction)`);
  check("texture.repeat-wrapping", data.wrapsRepeat===true, `wrapS/wrapT=RepeatWrapping -> ${data.wrapsRepeat}`);

  check("scene.plaza-uses-the-red-cobble-texture", data.plazaCount>0, `plaza meshes with the texture=${data.plazaCount}`);
  check("scene.plaza-has-a-map", data.plazaHasMap===true, `map=${data.plazaHasMap}`);
  check("scene.plaza-still-receives-shadow", data.plazaReceivesShadow===true, `receiveShadow=${data.plazaReceivesShadow}`);
  check("scene.plaza-uv-tiles-rather-than-stretches", data.uvSpan>1,
    `uv span=${data.uvSpan!==undefined?data.uvSpan.toFixed(2):'n/a'} (need >1)`);
  check("scene.no-plaza-left-as-flat-grey-solid", data.flatGreyPlazas===0,
    `flat 0x8f8a7e meshes=${data.flatGreyPlazas}`);

  check("holtburg.plaza-is-the-ground-surface-at-town-centre", data.holtburgPlazaHit===true,
    `downward raycast hit=${data.holtburgPlazaHit} at depth=${data.holtburgHitDepth} (plaza top y=${data.holtburgPlazaWorldY})`);
  check("holtburg.that-surface-is-red-cobblestone", data.holtburgHitIsRedCobble===true,
    `hit material.map===plazaCobbleTex() -> ${data.holtburgHitIsRedCobble}`);

  // ── CONTROLS
  check("control.old-flat-material-had-no-map", data.control_oldMaterialHasMap===false,
    `solid(0x8f8a7e).map=${data.control_oldMaterialHasMap} — untextured, so it flat-washed under the sun`);
  check("control.colour-multiply-would-be-too-dark", data.control_multipliedLum < data.bakedLum*0.8,
    `multiply lum=${data.control_multipliedLum.toFixed(1)} = ${(100*data.control_multipliedLum/data.bakedLum).toFixed(0)}% of baked lum=${data.bakedLum.toFixed(1)} — why the tint is baked in`);

  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
