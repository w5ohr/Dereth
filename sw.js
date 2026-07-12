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
// v3: acdialogue.json v2 (adds the ambient-chatter bucket).
const V = "dereth-v3";

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

  const isNav = req.mode === "navigate" || url.pathname === "/" || url.pathname.endsWith("/index.html");
  if (isNav) {
    e.respondWith((async () => {
      const c = await caches.open(V);
      try {
        const r = await fetch(req);
        if (r && r.ok) c.put(req, r.clone());
        return r;
      } catch (_) {
        return (await c.match(req)) || Response.error();
      }
    })());
    return;
  }

  // assets/music/ is EXCLUDED (#669): <audio> streams the score with Range requests, and the Cache
  // API rejects 206 partials — and ~100 MB of MP3s doesn't belong in a phone's cache storage anyway.
  const isStatic = (url.pathname.includes("/assets/") && !url.pathname.includes("/assets/music/"))
    || url.pathname.endsWith("three.min.js") || url.pathname.endsWith("manifest.webmanifest");
  if (!isStatic) return;

  e.respondWith((async () => {
    const c = await caches.open(V);
    const hit = await c.match(req);
    const net = fetch(req).then(r => { if (r && r.ok) c.put(req, r.clone()); return r; }).catch(() => null);
    return hit || net.then(r => r || Response.error());
  })());
});
