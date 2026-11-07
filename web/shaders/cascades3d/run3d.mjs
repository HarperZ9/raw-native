// The frame loop of the 3D cascades on the CPU (float64), the no-GI baseline, and the upsampling of
// screen probes. wgsl3d.mjs and gpu3d.mjs are the same passes on the GPU.
//   const out = runCascades3d(sc, S, plan, W, H);   // { radiance, hit } after plan.frames frames
//   const base = noGiBaseline(sc, S, W, H, 16);     // sun + pi sky x cosine sky visibility (m^2 rays)
import { entry, probeFlags, cacheOf, gbuffer, screenProbe, gatherCache, offset, depthPass, startPass } from "./cascades3d.mjs";
import { cosineDir, jitter, dot, sub } from "./math3d.mjs";
import { occluded } from "./bvh.mjs";
import { cameraBasis } from "./scene3d.mjs";
import { frameSeed, probeOffsets, cameraAt, footprint, atrous, accumulate } from "./denoise3d.mjs";
import { phasePlan } from "./world.mjs";

// Screen probe (i, j) of stride k sits on pixel (k i + ox, k j + oy), clamped to the frame; the
// offsets are k / 2, or move every frame under temporal accumulation (denoise3d.mjs probeOffsets).
export function probeGrid(W, H, k, ox = k >> 1, oy = ox) {
  const pw = Math.ceil(W / k), ph = Math.ceil(H / k);
  return { pw, ph, off: ox, ox, oy, at: (i, j) => [Math.min(W - 1, k * i + ox), Math.min(H - 1, k * j + oy)] };
}
// Upsampling weight of probe surface q for pixel surface x: bilinear weight times a plane-distance
// and a normal test. tol is the pixel footprint at x times 4 k.
export function upWeight(x, q, w, tol) {
  if (!q || w <= 0) return 0;
  const n = dot(x.n, q.n);
  return n > 0.5 && Math.abs(dot(sub(q.p, x.p), x.ng)) <= tol ? w * n * n * n * n : 0;
}

export function runCascades3d(sc, S, plan, W, H, { frames = plan.frames, cacheInit = null } = {}) {
  const ctx = { S, sc, plan, cachePrev: null, depth: null }, L = plan.levels;
  const view = (f) => { const camera = cameraAt(sc, plan, f); return { G: gbuffer(S, { ...sc, camera }, W, H), kb: cameraBasis(camera, W, H) }; };
  // A phase's grid, depth moments and flags (one phase without jit), traced on its first frame.
  const phases = new Map(), phase = (p) => {
    if (!phases.has(p)) {
      const pl = phasePlan(plan, p), c = { ...ctx, plan: pl, depth: depthPass(S, pl) };
      phases.set(p, { plan: pl, depth: c.depth, flags: L.map((_, l) => probeFlags(c, l)), sv: plan.sv ? L.map((_, l) => startPass(S, pl, l)) : null });
    }
    return phases.get(p);
  };
  let { G, kb } = view(0), buf0 = null, buf1 = null, cache = null, E = null, hist = null, flags = null;
  if (cacheInit) { ctx.cachePrev = initCache(plan, cacheInit); }
  for (let f = 0; f < frames; f++) {
    if (plan.move && f > 0) ({ G, kb } = view(f));
    const ph = phase(plan.jit ? f % plan.jit : 0);
    ctx.plan = ph.plan; ctx.depth = ph.depth; ctx.sv = ph.sv; flags = ph.flags;
    let next = null;
    for (let l = L.length - 1; l >= 0; l--) {
      const cur = new Float64Array(3 * L[l].probes * L[l].texels);
      for (let idx = 0; idx < L[l].probes; idx++) for (let k = 0; k < L[l].texels; k++) {
        cur.set(entry(ctx, l, idx, k, next, flags[l], flags[l + 1]), 3 * (idx * L[l].texels + k));
      }
      if (l === 1) buf1 = cur;
      if (l === 0) buf0 = cur;
      next = cur;
    }
    cache = cacheOf(ctx, buf0, flags[0]);
    if (plan.jit) { cache.plan = ph.plan; cache.depth = ph.depth; }
    ctx.cachePrev = cache;
    if (plan.stride) {
      E = screenPass(ctx, G, W, H, buf1, flags[1], f);
      if (plan.sf) E = atrous(plan, kb, E, H);
    }
    if (plan.tf > 0) {
      const Ecur = G.map((s, p) => (s ? pixelE(ctx, s.x, p % W, Math.floor(p / W), H, E, cache, kb) : null));
      hist = accumulate(plan, G, W, H, Ecur, kb, f >= plan.warm ? hist : null);
    }
  }
  if (hist) return shadeAll(sc, G, W, H, (p) => hist.hist.subarray(3 * p, 3 * p + 3));
  return shadeAll(sc, G, W, H, (p) => pixelE(ctx, G[p].x, p % W, Math.floor(p / W), H, E, cache, kb));
}

// A cache holding uniform radiance L (rgb) in every direction: E(n) = pi L.
export function initCache(plan, L) {
  const c = new Float64Array(16 * plan.levels[0].probes);
  for (let q = 0; q < plan.levels[0].probes; q++) c.set([Math.PI * L[0], Math.PI * L[1], Math.PI * L[2]], 16 * q);
  return c;
}

export function screenPass(ctx, G, W, H, buf1, flags1, f = 0) {
  const [ox, oy] = probeOffsets(ctx.plan, f), seed = frameSeed(ctx.plan, f);
  const g = probeGrid(W, H, ctx.plan.stride, ox, oy), E = new Array(g.pw * g.ph).fill(null);
  for (let j = 0; j < g.ph; j++) for (let i = 0; i < g.pw; i++) {
    const [px, py] = g.at(i, j), s = G[py * W + px], pid = j * g.pw + i;
    if (s) E[pid] = { e: screenProbe(ctx, s.x, (pid ^ seed) >>> 0, buf1, flags1), p: s.x.p, n: s.x.n, ng: s.x.ng };
  }
  return { g, E };
}

// Irradiance at pixel (x, y), surface X: screen probes upsampled, else the cache gather.
export function pixelE(ctx, X, x, y, H, SP, cache, kb) {
  if (!SP) return gatherCache(ctx, cache, X);
  const k = ctx.plan.stride, u = Math.max(0, (x - SP.g.ox) / k), v = Math.max(0, (y - SP.g.oy) / k);
  const i0 = Math.min(SP.g.pw - 1, Math.floor(u)), j0 = Math.min(SP.g.ph - 1, Math.floor(v)), fu = u - i0, fv = v - j0;
  const tol = 4 * k * footprint(kb, X.p, H) + 1e-3, E = [0, 0, 0];
  let ws = 0;
  for (const [i, j, w] of [[i0, j0, (1 - fu) * (1 - fv)], [i0 + 1, j0, fu * (1 - fv)], [i0, j0 + 1, (1 - fu) * fv], [i0 + 1, j0 + 1, fu * fv]]) {
    if (i >= SP.g.pw || j >= SP.g.ph) continue;
    const q = SP.E[j * SP.g.pw + i], ww = upWeight(X, q, w, tol);
    if (ww > 0) { for (let c = 0; c < 3; c++) E[c] += ww * q.e[c]; ws += ww; }
  }
  return ws > 0 ? E.map((e) => e / ws) : gatherCache(ctx, cache, X);
}

export function shadeAll(sc, G, W, H, Eof) {
  const rad = new Float64Array(3 * W * H), hit = new Uint8Array(W * H);
  for (let p = 0; p < W * H; p++) { if (!G[p]) continue; hit[p] = 1; shade(rad, 3 * p, sc, G[p], Eof(p)); }
  return { radiance: rad, hit };
}
function shade(rad, o, sc, s, E) {
  const X = s.x;
  for (let c = 0; c < 3; c++) rad[o + c] = dot(X.n, s.d) >= 0 ? X.emit[c] : X.emit[c] + X.alb[c] / Math.PI * (sc.sunE[c] * s.sun + E[c]);
}

// Lighting model 2 with more sky rays: E_gi = pi x sky x the cosine-weighted share of m^2 rays that
// escape. The no-GI baseline of ROADMAP P6.
export function noGiBaseline(sc, S, W, H, m = 16) {
  const G = gbuffer(S, sc, W, H), rad = new Float64Array(3 * W * H), hit = new Uint8Array(W * H);
  G.forEach((s, p) => {
    if (!s) return;
    hit[p] = 1;
    let open = 0;
    for (let j = 0; j < m; j++) for (let i = 0; i < m; i++) {
      const [h1, h2] = jitter(p, j * m + i), d = cosineDir(s.x.n, i, j, m, h1, h2);
      if (!occluded(S, offset(s.x, d), d, 1e30)) open++;
    }
    shade(rad, 3 * p, sc, s, sc.sky.map((v) => Math.PI * v * open / (m * m)));
  });
  return { radiance: rad, hit };
}
