// #546 regression harness: two small text defects.
//   1. The shop panel header duplicated the vendor name when the vendor's own `name` equals its
//      VENDOR_TYPES `title` (e.g. the Wandering Caravan) -> "Wandering Caravan — Wandering Caravan".
//   2. The interact-hint for a "townsfolk" NPC always prepended "Greet the " -> "Greet the Rolf Holt
//      (E)", a stray definite article in front of a proper name.
//
// This boots the game headless and exercises the real functions directly:
//   - buildShop() with activeShop set to a caravan-shaped object (name===title) and to a normal
//     provisioner-shaped object (name!==title), reading the actual #shop h2 text each time.
//   - currentPrompt() with the player standing next to a real townsfolk NPC.
//
// Run:  node tools/test_546_vendor_npc_text.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
  await waitForMain(page,()=>typeof buildShop==="function"&&typeof VENDOR_TYPES!=="undefined"&&typeof currentPrompt==="function",{timeout:30000});

  // ── 1a. caravan-shaped vendor: name === VENDOR_TYPES title -> header shows it ONCE ────────────
  const caravanHeader=await page.evaluate(()=>{
    activeShop={name:"Wandering Caravan",vtype:"caravan",title:"Wandering Caravan",pyreals:1000,pmax:1000};
    buildShop();
    return document.querySelector('#shop h2').textContent;
  });
  check('caravan.header-not-duplicated',caravanHeader==="Wandering Caravan",JSON.stringify(caravanHeader));

  // ── 1b. a normal named vendor working a role: name !== title -> still "name — title" ──────────
  const namedHeader=await page.evaluate(()=>{
    activeShop={name:"Rowena Blackwood",vtype:"tavern",pyreals:1000,pmax:1000};
    VENDOR_TYPES.tavern=VENDOR_TYPES.tavern||{title:"Barkeep",marker:"🍺",mcol:"#fff",robe:0,stock:()=>[]};
    buildShop();
    return document.querySelector('#shop h2').textContent;
  });
  check('named-vendor.header-still-joins-name-and-title',namedHeader==="Rowena Blackwood — Barkeep",JSON.stringify(namedHeader));

  // ── 2. townsfolk greet prompt: no stray "the" before a proper name ───────────────────────────
  // a fresh character spawns inside the Training Academy (inDungeon===true), where currentPrompt()
  // takes a different (academy-only) branch entirely — leave the dungeon so the open-world townsfolk
  // branch (the one this fix touches) is actually the one being exercised.
  const greetPrompt=await page.evaluate(()=>{
    inDungeon=false; inNetwork=false;
    const rec=addNPC(player.x+1,player.z,{role:"townsfolk",town:"Holtburg",name:"Rolf Holt"});
    return currentPrompt();
  });
  check('townsfolk.greet-prompt-no-stray-the',greetPrompt==="Greet Rolf Holt (E)",JSON.stringify(greetPrompt));
  check('townsfolk.greet-prompt-has-no-the-substring',!/Greet the /.test(greetPrompt),JSON.stringify(greetPrompt));

  check('no-unexpected-page-errors',errs.length===0,errs.slice(0,5).join(' | ').slice(0,400));
  await browser.close();
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{console.error('FATAL',e);process.exit(1)});
