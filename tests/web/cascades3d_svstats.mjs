// D4 of round 8 (CPU float64): what sv 2 drops, by level, on raw-hall, and how far each neighbour rule of a merge
// strays from the exact ray. node tests/web/cascades3d_svstats.mjs SCENE.bin FRAMES OUT.json (measurement only, not CI)
// Plan: J2 without jit (sv 2, ft 1, cg 2, pi 0), grid g0. Runs FRAMES frames from a uniform cache, then on
// the last frame's level buffers: per level l >= 1, the entries sv 2 drops (blocker class, what the entry
// holds); per merge at level l - 1 (and the screen probes for l = 1), the trusted weight dropped, and the
// mean luminance of the merge without sv (M0), with sv (K), of the dropped part (D) and of the exact own
// ray traced on from T1x (X).
import { readFileSync, writeFileSync } from "node:fs";
import { parseScene, bounds } from "../../web/shaders/cascades3d/scene3d.mjs";
import { buildBvh, prepare, closestHit } from "../../web/shaders/cascades3d/bvh.mjs";
import { planCascades3d, neighbours, probePos, texelDir, dot, cosineDir, jitter } from "../../web/shaders/cascades3d/math3d.mjs";
import { entry, probeFlags, cacheOf, depthPass, startPass, children, lookup, onward, surfaceAt, gbuffer, offset } from "../../web/shaders/cascades3d/cascades3d.mjs";
import { initCache } from "../../web/shaders/cascades3d/run3d.mjs";

const [file, frames = "3", out = "svstats.json"] = process.argv.slice(2);
const sc = parseScene(new Uint8Array(readFileSync(file))), S = prepare(sc, buildBvh(sc));
const plan = planCascades3d(bounds(sc), { h0: 0.5, stride: 8, m: 6, tf: 0, pi: 0, cg: 2, sk: 0 }), L = plan.levels;
const lum = (v) => 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2], t0 = Date.now(), log = (...a) => console.log(((Date.now() - t0) / 1000).toFixed(0) + "s", ...a);
const ctx = { S, sc, plan, cachePrev: initCache(plan, sc.sky), depth: depthPass(S, plan) };
log("depth");
const flags = L.map((_, l) => probeFlags(ctx, l)); log("flags");
const SV = L.map((_, l) => startPass(S, plan, l)); log("sv");
let bufs = [];
for (let f = 0; f < +frames; f++) {
  ctx.sv = SV; let next = null; bufs = [];
  for (let l = L.length - 1; l >= 0; l--) {
    const cur = new Float64Array(3 * L[l].probes * L[l].texels);
    for (let idx = 0; idx < L[l].probes; idx++) for (let k = 0; k < L[l].texels; k++) cur.set(entry(ctx, l, idx, k, next, flags[l], flags[l + 1]), 3 * (idx * L[l].texels + k));
    bufs[l] = cur; next = cur;
  }
  if (f + 1 < +frames) ctx.cachePrev = cacheOf(ctx, bufs[0], flags[0]); log("frame", f); // the last frame's cache stays the one its buffers used
}
const Z = () => ({ n: 0, wt: 0, dw: 0, M0: 0, K: 0, D: 0, X: 0, KmX: 0, M0mX: 0, nd: 0, partial: 0, all: 0, est: {} });
const clear = (a, b) => { const v = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], len = Math.hypot(...v); return len === 0 || !closestHit(S, a, v.map((x) => x / len), 0, len); };
// One merge: neighbours of level l1 at p in direction d. ests: { name: [trust(c, q), sv?] }; each estimator's value
// is its trusted mean (with sv: weighed by the share), X (ft) when it trusts none.
function mergeStats(st, l1, p, ests, valSv, valAll, X) {
  const nb = neighbours(plan, l1, p), x = lum(X());
  let wt = 0, ws = 0, K = [0, 0, 0], A = [0, 0, 0], D = [0, 0, 0];
  const base = ests.bits[0];
  nb.forEach((q, c) => {
    if (!(q.w > 0) || !base(c, q)) return;
    const v = valSv(q.idx), a = valAll(q.idx), s = v[3];
    wt += q.w; ws += q.w * s;
    for (let j = 0; j < 3; j++) { K[j] += q.w * s * v[j]; A[j] += q.w * a[j]; D[j] += q.w * (a[j] - s * v[j]); }
  });
  for (const [name, [trust, useSv]] of Object.entries(ests)) {
    let w = 0, o = 0;
    nb.forEach((q, c) => { if (!(q.w > 0) || !trust(c, q)) return; const v = useSv ? valSv(q.idx) : valAll(q.idx), s = useSv ? v[3] : 1; w += q.w * s; o += q.w * s * lum(v); });
    const e = st.est[name] || (st.est[name] = { n: 0, dev: 0, none: 0, absdev: 0 }), val = w > 0 ? o / w : x;
    e.n++; e.dev += val - x; e.absdev += Math.abs(val - x); if (!(w > 0)) e.none++;
  }
  if (!(wt > 0)) return;
  const m0 = lum(A) / wt, k = ws > 0 ? lum(K) / ws : x;
  st.n++; st.wt += wt; st.dw += wt - ws; st.M0 += m0; st.K += k; st.X += x; st.KmX += k - x; st.M0mX += m0 - x;
  if (wt - ws > 1e-9 * wt) { st.partial += ws > 0 ? 1 : 0; st.all += ws > 0 ? 0 : 1; st.nd += wt - ws; st.D += lum(D); }
}
const res = { plan: { h0: 0.5, sv: 2, ft: 1, cg: 2, pi: 0, frames: +frames }, levels: [] };
for (let l = 1; l < L.length; l++) {
  const e = { level: l, entries: 0, dropped: 0, blockerFront: 0, blockerBack: 0, holdsBack: 0, holdsFront: 0, holdsMerge: 0, lumDropped: 0, lumKept: 0, merges: Z(), screen: null };
  for (let idx = 0; idx < L[l].probes; idx++) {
    if (flags[l][idx] & 256) continue;
    const p = probePos(plan, l, idx);
    for (let k = 0; k < L[l].texels; k++) {
      const i = idx * L[l].texels + k, v = lum(bufs[l].subarray(3 * i, 3 * i + 3));
      e.entries++;
      if (SV[l][i] > 0) { e.lumKept += v; continue; }
      e.dropped++; e.lumDropped += v;
      const d = texelDir(k, L[l].c), b = closestHit(S, p, d, 0, L[l].T0);
      if (b) (surfaceAt(S, p, d, b).back ? e.blockerBack++ : e.blockerFront++);
      const h = closestHit(S, p, d, L[l].T0, L[l].T1x);
      if (!h) e.holdsMerge++; else (dot(surfaceAt(S, p, d, h).n, d) >= 0 ? e.holdsBack++ : e.holdsFront++);
    }
  }
  e.lumKept /= Math.max(1, e.entries - e.dropped); e.lumDropped /= Math.max(1, e.dropped);
  const lb = l - 1, Lb = L[lb], ctxAll = { ...ctx, sv: null };
  for (let idx = 0; idx < Lb.probes; idx++) {
    if (flags[lb][idx] & 256) continue;
    const p = probePos(plan, lb, idx);
    for (let k = 0; k < Lb.texels; k++) {
      const d = texelDir(k, Lb.c);
      if (closestHit(S, p, d, Lb.T0, Lb.T1x)) continue;
      const T = L[l].T0, at = [p[0] + d[0] * T, p[1] + d[1] * T, p[2] + d[2] * T], inside = (q) => flags[l][q.idx] & 256;
      const bit = (c, q) => (flags[lb][idx] & (1 << c)) && !inside(q), ss = (c, q) => !inside(q) && clear(at, [q.pos[0] + d[0] * T, q.pos[1] + d[1] * T, q.pos[2] + d[2] * T]);
      mergeStats(e.merges, l, p, { bits: [bit, false], sv: [bit, true], ss: [ss, false], ssbits: [(c, q) => bit(c, q) && ss(c, q), false], sssv: [(c, q) => ss(c, q), true] },
        (q) => children(ctx, l, bufs[l], q, k, Lb.c), (q) => children(ctxAll, l, bufs[l], q, k, Lb.c), () => onward(ctx, p, d, Lb.T1x));
    }
  }
  res.levels.push(e); log("level", l, JSON.stringify({ dropped: e.dropped / e.entries }));
}
// Screen probes (level 1 lookups) on a 240 x 135 g-buffer, every 8th pixel, 6 x 6 rays.
const G = gbuffer(S, sc, 240, 135), st = Z(), ctxAll = { ...ctx, sv: null }, T1x = L[0].T1x;
for (let y = 4; y < 135; y += 8) for (let x = 4; x < 240; x += 8) {
  const g = G[y * 240 + x]; if (!g) continue;
  const s = g.x, up = offset(s, s.ng), bits = neighbours(plan, 1, s.p).map((q) => { const v = [q.pos[0] - up[0], q.pos[1] - up[1], q.pos[2] - up[2]], len = Math.hypot(...v); return len === 0 || !closestHit(S, up, v.map((a) => a / len), 0, len); });
  for (let j = 0; j < 6; j++) for (let i = 0; i < 6; i++) {
    const [h1, h2] = jitter(y * 240 + x, j * 6 + i), d = cosineDir(s.n, i, j, 6, h1, h2), o = offset(s, d);
    if (closestHit(S, o, d, 0, T1x)) continue;
    const T = L[1].T0, at = [o[0] + d[0] * T, o[1] + d[1] * T, o[2] + d[2] * T], inside = (q) => flags[1][q.idx] & 256;
    const bit = (c, q) => bits[c] && !inside(q), ss = (c, q) => !inside(q) && clear(at, [q.pos[0] + d[0] * T, q.pos[1] + d[1] * T, q.pos[2] + d[2] * T]);
    mergeStats(st, 1, s.p, { bits: [bit, false], sv: [bit, true], ss: [ss, false], ssbits: [(c, q) => bit(c, q) && ss(c, q), false], sssv: [(c, q) => ss(c, q), true] }, (q) => lookup(ctx, 1, bufs[1], q, d), (q) => lookup(ctxAll, 1, bufs[1], q, d), () => onward(ctx, o, d, T1x));
  }
}
res.levels[0].screen = st;
for (const e of res.levels) for (const m of [e.merges, e.screen]) if (m) {
  m.share_dropped = m.dw / Math.max(m.wt, 1e-30);
  for (const k of ["M0", "K", "X", "KmX", "M0mX"]) m[k] /= Math.max(1, m.n);
  m.D /= Math.max(m.nd, 1e-30);
  for (const e of Object.values(m.est)) { e.dev /= e.n; e.absdev /= e.n; e.none /= e.n; }
}
writeFileSync(out, JSON.stringify(res, null, 1)); log("done");
