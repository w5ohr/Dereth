// #532 regression harness: the chargen FACE SHAPE selector must actually change the retail head.
//
// Before the fix, `app.face` (oval/square/round/long/angular/heart) was read ONLY by the procedural
// `buildFaceGroup` skull builder. The retail-head path (`acHeadGroup`) never looked at it, so all six
// shapes produced a pixel-identical head — the selector was a visual no-op on every real head.
//
// The fix puts the head meshes inside an inner `morph` group whose scale is the shape's authored
// sx/sy/sz (APPEARANCE.face) normalised to the Oval baseline. That means:
//   * Oval must be EXACTLY the head as shipped (scale 1,1,1) — no regression on the default character.
//   * The other five must yield visibly distinct head geometry.
//   * The morph must be an INNER group, so it composes with acBuildHead's outer placement scale and
//     rotateY(pi) and rides the creator bust (ccBust_build adds the head group directly).
//
// The harness builds a real head group per shape off the real assets, measures the resulting world
// bounding box, and asserts distinctness. It then runs a NEGATIVE CONTROL: it flattens the morph
// scale back to identity and asserts the measurements collapse to a single geometry — proving these
// assertions actually detect the bug rather than passing vacuously.
//
// Run:  node tools/test_532_face_shape_morph.js   (needs: npm i puppeteer-core, Chrome/Chromium,
//        and a static server for the repo root: python3 -m http.server 8791)
const puppeteer=require('puppeteer-core');
const CHROME=process.env.CHROME||"/opt/pw-browsers/chromium";
const URL=process.env.DERETH_URL||'http://localhost:8791/index.html';

let pass=0,fail=0;
function check(name,ok,detail){ console.log(`${ok?'PASS':'FAIL'}  ${name}${detail?'  — '+detail:''}`); ok?pass++:fail++; }
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const BENIGN=/Pointer Lock|pointerlock/i;
// page.waitForFunction() polls in an ISOLATED world where the page's top-level let/const are invisible;
// only page.evaluate()'s MAIN world sees them. Poll by hand with bare identifiers.
async function waitForMain(page,fn,{timeout=30000,interval=100}={}){
  const t0=Date.now();
  while(Date.now()-t0<timeout){ if(await page.evaluate(fn)) return; await sleep(interval); }
  throw new Error(`waitForMain timed out after ${timeout}ms`);
}
const SHAPES=["oval","square","round","long","angular","heart"];
const near=(a,b,eps)=>Math.abs(a-b)<=eps;

(async()=>{
  const browser=await puppeteer.launch({executablePath:CHROME,headless:'new',
    args:['--no-sandbox','--use-gl=swiftshader','--enable-unsafe-swiftshader','--ignore-gpu-blocklist']});
  const page=await browser.newPage();
  const errs=[]; page.on('pageerror',e=>{ if(!BENIGN.test(e.message)) errs.push(e.message); });
  await page.goto(URL,{waitUntil:'load',timeout:60000});
  await waitForMain(page,()=>typeof startGame==="function",{timeout:30000});
  await page.evaluate(()=>{ try{ startGame(false,'aluvian'); }catch(e){} });
  await waitForMain(page,()=>typeof player!=="undefined"&&player&&typeof acHeadGroup==="function"&&typeof AC_HEADS!=="undefined"&&AC_HEADS,{timeout:30000});

  const data=await page.evaluate(async(SHAPES)=>{
    const build=app=>new Promise(r=>acHeadGroup(app,r));
    const measure=grp=>{
      grp.updateMatrixWorld(true);
      const bb=new THREE.Box3().setFromObject(grp), s=new THREE.Vector3(); bb.getSize(s);
      return {w:s.x,h:s.y,d:s.z};
    };
    const out={shapes:{},structural:null,control:{}};
    for(const f of SHAPES){
      const app=JSON.parse(JSON.stringify(player.appearance)); app.face=f;
      const grp=await build(app);
      if(!grp){ out.shapes[f]=null; continue; }
      const morph=grp.children[0];
      if(out.structural===null){
        out.structural={
          innerIsGroup: !!morph && morph.isGroup===true,
          headMeshesUnderMorph: morph?morph.children.filter(o=>o.isMesh).length:0,
          meshesDirectlyUnderRoot: grp.children.filter(o=>o.isMesh).length
        };
      }
      const m=measure(grp);
      out.shapes[f]={scale:[morph.scale.x,morph.scale.y,morph.scale.z],w:m.w,h:m.h,d:m.d,ratio:m.h/m.w};

      // NEGATIVE CONTROL: flatten the morph to identity (the pre-fix behaviour) and re-measure.
      morph.scale.set(1,1,1);
      const c=measure(grp);
      out.control[f]={w:c.w,h:c.h,d:c.d,ratio:c.h/c.w};
    }
    return out;
  },SHAPES);

  // ── structural: the morph must be an inner group holding the meshes
  const st=data.structural||{};
  check("structure.morph-is-inner-group", st.innerIsGroup===true, `grp.children[0].isGroup=${st.innerIsGroup}`);
  check("structure.head-meshes-live-under-morph", st.headMeshesUnderMorph>0, `meshes under morph=${st.headMeshesUnderMorph}`);
  check("structure.no-meshes-bypass-the-morph", st.meshesDirectlyUnderRoot===0, `meshes directly under root=${st.meshesDirectlyUnderRoot}`);

  const built=SHAPES.filter(s=>data.shapes[s]);
  check("build.all-six-shapes-built", built.length===6, `built=${built.length}/6`);

  // ── Oval is the baseline: exactly the head as shipped
  const oval=data.shapes.oval;
  check("oval.scale-is-identity", oval && near(oval.scale[0],1,1e-9)&&near(oval.scale[1],1,1e-9)&&near(oval.scale[2],1,1e-9),
    oval?`scale=[${oval.scale.map(v=>v.toFixed(6)).join(', ')}]`:"no oval");

  // ── all six scale triples distinct
  const keys=built.map(s=>data.shapes[s].scale.map(v=>v.toFixed(6)).join(','));
  check("scales.six-distinct", new Set(keys).size===6, `distinct=${new Set(keys).size}/6`);

  // ── all six measured geometries distinct, with a real separation margin
  const ratios=built.map(s=>data.shapes[s].ratio);
  let minDiff=Infinity, closest="";
  for(let i=0;i<ratios.length;i++) for(let j=i+1;j<ratios.length;j++){
    const rel=Math.abs(ratios[i]-ratios[j])/Math.max(ratios[i],ratios[j]);
    if(rel<minDiff){ minDiff=rel; closest=`${built[i]}/${built[j]}`; }
  }
  check("geometry.six-distinct-height-width-ratios", new Set(ratios.map(r=>r.toFixed(5))).size===6,
    `distinct=${new Set(ratios.map(r=>r.toFixed(5))).size}/6`);
  check("geometry.closest-pair-separated-by->=1%", minDiff>=0.01, `closest ${closest} differ by ${(minDiff*100).toFixed(2)}%`);

  // ── directional sanity from the authored table: Long is tallest/narrowest, Square widest/shortest
  const r=f=>data.shapes[f].ratio;
  check("geometry.long-taller-than-oval", r("long")>r("oval"), `long=${r("long").toFixed(4)} oval=${r("oval").toFixed(4)}`);
  check("geometry.square-shorter-than-oval", r("square")<r("oval"), `square=${r("square").toFixed(4)} oval=${r("oval").toFixed(4)}`);
  check("geometry.long-is-max-ratio", Math.max(...ratios)===r("long"), `max=${Math.max(...ratios).toFixed(4)}`);

  // ── every non-oval shape actually moves the mesh off the oval baseline
  const moved=built.filter(s=>s!=="oval").filter(s=>Math.abs(data.shapes[s].ratio-r("oval"))/r("oval")>=0.01);
  check("geometry.all-five-non-oval-shapes-differ-from-oval", moved.length===5, `moved=${moved.length}/5`);

  // ── NEGATIVE CONTROL: with the morph flattened, every shape must collapse to ONE geometry.
  //    If this fails, the assertions above would pass even with the bug present.
  const ctl=built.map(s=>data.control[s].ratio.toFixed(5));
  check("control.identity-morph-collapses-to-one-geometry", new Set(ctl).size===1,
    `distinct under identity scale=${new Set(ctl).size} (expect 1) — this is the pre-fix behaviour`);

  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
