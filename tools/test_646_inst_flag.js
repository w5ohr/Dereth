// #646 regression harness: a player inside an INSTANCE must not exist in the overworld.
//
// #307 made the client broadcast an instanced player's overworld RETURN POINT instead of
// instance-local coords, and it has sent `inst:1` alongside ever since. Nothing ever read the flag —
// the server didn't ingest it, `player_pub` didn't relay it, and no client code checked it. So the
// entrance showed a motionless, fully targetable phantom of anyone in a dungeon, the Town Network, or
// the Academy (where every new character starts).
//
// The consequences were not cosmetic. `nearest_player()` had no instanced filter, so ambient mobs
// chased and meleed the phantom, and each landed hit sent the victim {t:"dmg"} which onMobDmg applies
// unconditionally: real, invisible, unavoidable damage to someone inside a dungeon, with no attacker
// to see, fight or flee. Death was possible. A PK could camp the phantom the same way.
//
// #635 (per-player ambient top-up) made it constant rather than rare: 14 live creatures are now kept
// within 260u of every in-world player's reported position — which, for a dungeon-dweller, IS the
// entrance phantom.
//
// This harness covers the CLIENT half. The server half is covered end-to-end by
// server/check_646_inst.py, which reads two clients' snapshots off the wire.
//
// Run:  node tools/test_646_inst_flag.js   (needs: npm i puppeteer-core, Chrome/Chromium,
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
  await waitForMain(page,()=>typeof player!=="undefined"&&player&&typeof reconcileRemotes==="function"
    &&typeof netPeerPresent==="function"&&typeof updateRemotes==="function",{timeout:30000});

  const data = await page.evaluate(()=>{
    const out={};
    const saved={isOnline:typeof isOnline!=="undefined"?isOnline:false, inDungeon, inNetwork,
                 players:NET.players, meshes:NET.meshes, me:NET.me};
    isOnline=true; inDungeon=false; inNetwork=false;
    NET.players={}; NET.meshes={}; NET.me="ME";
    player.x=0; player.z=0; player.yaw=0;

    const mk=(id,x,z,inst)=>({id,name:id,x,z,yaw:0,hp:27,mhp:27,level:1,heritage:"aluvian",
                              title:"",pk:true,pkState:"pk",wt:"sword",wmode:"sword",shield:null,
                              ...(inst?{inst:1}:{})});

    // OVERWORLD peer at (5,0); INSTANCED peer reporting the same entrance coords (6,0)
    reconcileRemotes([ mk("outside",5,0,false), mk("dweller",6,0,true) ]);

    out.bothTracked = !!(NET.players["outside"] && NET.players["dweller"]);
    out.instFlagIngested = NET.players["dweller"].inst===true;
    out.outsideNotInst = NET.players["outside"].inst===false;

    out.presentOutside = netPeerPresent(NET.players["outside"]);
    out.presentDweller = netPeerPresent(NET.players["dweller"]);

    // updateRemotes owns visibility every frame — it must keep the phantom hidden
    updateRemotes(1/60);
    out.meshOutsideVisible = NET.meshes["outside"].visible;
    out.meshDwellerVisible = NET.meshes["dweller"].visible;

    // ── ally targeting (pickAllyTarget): aim straight at the dweller's entrance coords
    // Put ONLY the dweller in range so nothing else can win the crosshair contest.
    NET.players={}; NET.meshes={};
    reconcileRemotes([ mk("dweller",6,0,true) ]);
    cam.position.set(0,1.6,0); cam.lookAt(6,1.6,0); cam.updateMatrixWorld(true);
    const tgtAll = pickAllyTarget(50);
    out.targetAcquired = tgtAll ? tgtAll.id : null;

    // ── pvpStrike must not relay a hit at the phantom
    player.pk=true; player.pkState="pk";
    const sent=[]; const realSend=netSend; netSend=m=>sent.push(m);
    pvpStrike(10,"");
    out.pvpSentWhileInstanced = sent.filter(m=>m.t==="pvp").length;

    // ...and both work once they leave the instance (proves the geometry, so the blocks above are the flag)
    reconcileRemotes([ mk("dweller",6,0,false) ]);
    const tgtAfter = pickAllyTarget(50);
    out.targetAfterExit = tgtAfter ? tgtAfter.id : null;
    // pvpStrike needs range <= 6; move the dweller adjacent
    reconcileRemotes([ mk("dweller",3,0,false) ]);
    NET.players["dweller"].x=3; NET.players["dweller"].z=0;
    sent.length=0; pvpStrike(10,"");
    out.pvpSentAfterExit = sent.filter(m=>m.t==="pvp").length;
    netSend=realSend;

    out.dwellerVisibleAfterExit = (updateRemotes(1/60), NET.meshes["dweller"].visible);

    // re-entering the instance hides them again (the flag is per-snapshot, not sticky-once)
    reconcileRemotes([ mk("dweller",6,0,true) ]);
    updateRemotes(1/60);
    out.reHidden = !NET.meshes["dweller"].visible;

    // CONTROL: the pre-fix predicate (presence == "is in NET.players") treats the phantom as present
    out.control_prefixWouldTarget = !!NET.players["dweller"];

    NET.players=saved.players; NET.meshes=saved.meshes; NET.me=saved.me;
    isOnline=saved.isOnline; inDungeon=saved.inDungeon; inNetwork=saved.inNetwork;
    return out;
  });

  check("wire.inst-flag-is-ingested", data.bothTracked && data.instFlagIngested && data.outsideNotInst,
    `dweller.inst=${data.instFlagIngested} outside.inst=${data.outsideNotInst}`);
  check("presence.instanced-peer-is-not-present", data.presentDweller===false && data.presentOutside===true,
    `netPeerPresent: outside=${data.presentOutside} dweller=${data.presentDweller}`);
  check("render.phantom-avatar-is-hidden", data.meshDwellerVisible===false && data.meshOutsideVisible===true,
    `mesh.visible: outside=${data.meshOutsideVisible} dweller=${data.meshDwellerVisible}`);
  check("target.phantom-cannot-be-aimed-at", data.targetAcquired===null,
    `pickAllyTarget() -> ${data.targetAcquired===null?"nothing":data.targetAcquired} while aiming straight at the entrance`);
  check("pvp.no-strike-relayed-at-the-phantom", data.pvpSentWhileInstanced===0,
    `pvp messages sent = ${data.pvpSentWhileInstanced} (free hits on someone who cannot see or answer)`);
  check("target.acquirable-again-once-out-of-the-instance", data.targetAfterExit==="dweller",
    `pickAllyTarget() -> ${data.targetAfterExit} (same aim geometry, so the block above is the flag, not the maths)`);
  check("pvp.strike-lands-again-once-out", data.pvpSentAfterExit===1,
    `pvp messages sent = ${data.pvpSentAfterExit}`);
  check("render.visible-again-once-out", data.dwellerVisibleAfterExit===true, `visible=${data.dwellerVisibleAfterExit}`);
  check("wire.flag-is-per-snapshot-not-sticky", data.reHidden===true,
    `re-entering the instance hides them again = ${data.reHidden}`);

  check("control.pre-fix-presence-test-accepts-the-phantom", data.control_prefixWouldTarget===true,
    `"is in NET.players" == ${data.control_prefixWouldTarget} — the old implicit presence test, which is why the phantom was targetable`);

  check("no-unexpected-page-errors", errs.length===0, errs.slice(0,3).join(' | '));
  await browser.close();
  console.log(`${pass} passed, ${fail} failed`);
  process.exit(fail?1:0);
})().catch(e=>{ console.error(e); process.exit(1); });
