// Dereth PWA service worker (#673).
//
// Two rules, nothing clever:
//  · NAVIGATIONS (index.html) are network-first — a deploy lands the moment the player reloads,
//    exactly like the browser tab; the cached copy only serves when the network is unreachable,
//    so the title screen still opens offline (offline solo play works, the MMO needs the net).
//  · STATIC GAME DATA (assets/*, three.min.js — content-addressed textures, meshes, sounds, JSON
//    packs that change only when re-exported) is stale-while-revalidate: served instantly from
//    cache, refreshed in the background. This is where the real win lives — the asset packs are
//    the slow part of every load, and on a phone they now warm once.
//
// Everything else (websocket upgrade, server/*, cross-origin) is untouched.
// Bump V on any asset change (new/updated models, textures, three.min.js): the activate handler below
// deletes every cache whose key != V, so a version bump purges stale asset caches on every client's
// next visit. v2: ship the rigged Horse/Deer/Stag GLBs + the UO crafting build (was serving old cached
// Horse.glb / index.html to returning players).
// v3: acdialogue.json v2 (ambient chatter). v4: acspellfx.json (spell effect+projectile data).
// v5: acspellsets.json (equipment-set spell bonuses). v6: acitems.json v2 (scrolls) + 2 scroll icons.
// v7: acportalfx.json + acportal/ sprites (the authentic retail portal particle effect).
// v8: #webgpu Phase A — the game code moved out of inline index.html into js/game.js + js/craft.js
// (ES-module three via vendor/three.module.js). js/*.js are treated like the app (network-first, so a
// deploy lands on reload); vendor/* is stale-while-revalidate but SELF-HEALING (#806): its background
// revalidation forces a conditional server check, so a new Three.js build is picked up on the next load
// WITHOUT a manual `V` bump — bumping V stays a belt-and-suspenders, no longer the only safety net.
const V = "dereth-v8";

// #804: network-first must not hang on "lie-fi" (a connected-but-dead link where fetch never settles).
// Race every network-first fetch against a timeout so a stalled request falls back to cache instead of
// leaving the player staring at a blank tab. AbortController cancels the in-flight request on timeout.
const NET_TIMEOUT_MS = 6000;
function fetchWithTimeout(req, ms) {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), ms);
  return fetch(req, { signal: ctl.signal }).finally(() => clearTimeout(timer));
}

self.addEventListener("install", () => { self.skipWaiting(); });

self.addEventListener("activate", e => {
  e.waitUntil((async () => {
    for (const k of await caches.keys()) if (k !== V) await caches.delete(k);
    await self.clients.claim();
  })());
});

self.addEventListener("fetch", e => {
  const req = e.request;
  if (req.method !== "GET") return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;

  // app code (js/game.js, js/craft.js) changes on every deploy just like index.html did when it was
  // inline — serve it network-first so a reload always gets the latest, falling back to cache offline.
  const isCode = url.pathname.endsWith("/js/game.js") || url.pathname.endsWith("/js/craft.js");
  const isNav = req.mode === "navigate" || url.pathname === "/" || url.pathname.endsWith("/index.html");
  if (isNav || isCode) {
    e.respondWith((async () => {
      const c = await caches.open(V);
      try {
        const r = await fetchWithTimeout(req, NET_TIMEOUT_MS);   // #804: don't hang forever on lie-fi
        if (r && r.ok) c.put(req, r.clone());
        return r;
      } catch (_) {
        // #803: offline fallback. A navigation can carry a query string (/?ref=x, deep links, share
        // params) that matches no cache key exactly, so retry ignoring the search params and finally
        // fall back to the cached shell — otherwise the app fails to open offline on any non-bare URL.
        let hit = await c.match(req);
        if (!hit && isNav) {
          hit = await c.match(req, { ignoreSearch: true })
             || await c.match("./index.html", { ignoreSearch: true })
             || await c.match("./");
        }
        return hit || Response.error();
      }
    })());
    return;
  }

  // assets/music/ is EXCLUDED (#669): <audio> streams the score with Range requests, and the Cache
  // API rejects 206 partials — and ~100 MB of MP3s doesn't belong in a phone's cache storage anyway.
  const isStatic = (url.pathname.includes("/assets/") && !url.pathname.includes("/assets/music/"))
    || url.pathname.includes("/vendor/")   // pinned Three.js ESM build + jsm addons (change only on a version bump)
    || url.pathname.endsWith("manifest.webmanifest");
  if (!isStatic) return;

  e.respondWith((async () => {
    const c = await caches.open(V);
    const hit = await c.match(req);
    // #806: the pinned Three.js ESM build (vendor/) must self-heal without a manual `V` bump — otherwise a
    // new build shipped without one serves the stale module indefinitely (vendor has no Cache-Control, so
    // the browser's heuristic freshness can pin it for days, and the plain background fetch is satisfied
    // from that stale HTTP cache without ever reaching the server). Force its revalidation to check the
    // server CONDITIONALLY: a cheap 304 when unchanged, a full 200 + cache update when it changed. The
    // cached copy is still returned instantly below, so there's no added latency; the next load is fresh.
    // (assets/ are immutable + content-addressed — they change only with a network-first index.html — so
    // they keep the plain revalidation.)
    const isVendor = url.pathname.includes("/vendor/");
    const net = fetch(req, isVendor ? { cache: "no-cache" } : undefined)
      .then(r => { if (r && r.ok) c.put(req, r.clone()); return r; }).catch(() => null);
    return hit || net.then(r => r || Response.error());
  })());
});
