// World-level options of the 3D cascades, CPU float64 (wgsl3d-world.mjs is the GPU side). Each is a
// plan switch (math3d.mjs planCascades3d); sv 2, ft 1 and jit 8 are on by default since the sixth round, pi 1,
// cg 6 and sk 1 since the seventh (cascades3d.mjs: probeFlags, gatherCache, entry):
// - mw: how a screen probe weighs level 1's eight neighbours. "seg" (the default): on or off by one
//   segment from the surface to the neighbour. "vis": instead, the depth-moment visibility of the
//   surface point from the level-0 probe at the neighbour's place (level 1's probes sit on every other
//   level-0 probe), as the cache gather does.
// - sv: start visibility. Level i's entry for direction d traces from q + d T_i, so it skips the
//   first T_i of the ray; when that part crosses a wall, the entry holds the far side's light. With sv
//   each entry carries the share of 4 sub-texel rays that cross [0, T_i] unblocked (traced once for
//   static geometry, cascades3d.mjs startPass), and every merge (world levels and screen probes)
//   weighs a neighbour's texels by it.
// - fz: a merge that rejects all eight neighbours returns no light, instead of the plain mean of all
//   eight (which takes back the neighbours behind a wall that the tests rejected).
// - jit K: the whole world grid moves by one of K offsets a frame (Halton 3, 5, 2 times h0 2^jl), so
//   the trilinear pattern of the world levels changes from frame to frame and the temporal
//   accumulation averages it. Depth moments and probe flags belong to a phase; with static geometry
//   each phase's are traced once, on the phase's first frame, and reused afterwards.
// - cg 2 (3): the cache gather's fallback, when no probe passes the depth test, keeps the lit-side
//   probes outside geometry whose farthest (nearest) depth ray of the texel toward the point reaches
//   past it; none gives no light. The depth rays are traced once a phase (static geometry), so the
//   fallback traces nothing, where cg 1 traces a segment to each probe.
// - sd (with sv 2): an entry whose own ray is blocked over [0, T_i] traces that ray from 0 instead
//   and holds the radiance of the surface that blocks it, the light that reaches the probe from that
//   direction. sv 2 dropped such entries, and with them the dark values they held (the far side of
//   the occluder, often its back face), which brightened raw-hall by 2%. The surface lies on the
//   probe's side of anything the ray crosses, so no entry holds light from past a wall, and merges
//   weigh every entry again, as without sv.
// - pi: a probe also counts as inside geometry when at least half of 96 rays meet a surface from behind
//   (thin slabs, which the 6 axis rays miss). cg 4 / 5 / 6: after cg 2, traced segments from the surface
//   to all eligible probes / the one of largest weight / each in decreasing weight until one is reached.
//   sk: skip the entries of probes inside geometry (nothing reads them with fix probe, ft and cg).
//   fd 1 (screen probes) / 2 (every merge): a merge's dropped share takes its own ray traced on.
// - sx (cascades3d.mjs screenProbe; the default since the eighth round): screen rays run to their hit, no level-1
//   merge. svl: a mask of the levels whose start visibility applies (startPass).
import { add, mul, sub, dot, texelOf, neighbours } from "./math3d.mjs";

export const halton = (i, b) => { let r = 0; for (let s = 1 / b; i > 0; i = Math.floor(i / b), s /= b) r += s * (i % b); return r; };

// The world-grid offset of phase p (zero without jit).
export function phaseOffset(plan, p) {
  if (!plan.jit) return [0, 0, 0];
  return [3, 5, 2].map((b) => plan.hj * halton(p + 1, b));
}
// The plan of phase p: the same levels on a moved grid.
export function phasePlan(plan, p) {
  if (!plan.jit) return plan;
  const o = phaseOffset(plan, p);
  return { ...plan, lo: plan.lo.map((v, k) => v + o[k]), phase: p };
}

// Chebyshev visibility of point `at` from level-0 probe q0 at position qp (depth moments, cubed, 0
// under 0.05), the cache gather's test. A texel whose four rays all missed (mean at dmax) sees it.
export function chebVis(plan, depth, q0, qp, at) {
  const v = sub(at, qp), dist = Math.hypot(...v), cd = plan.cd;
  if (!(dist > 0)) return 1;
  const t = 4 * (q0 * 6 * cd * cd + texelOf(mul(v, 1 / dist), cd)), mu = depth[t];
  if (dist <= mu || mu >= 0.999 * plan.dmax) return 1;
  const va = Math.max(depth[t + 1] - mu * mu, 1e-4 * plan.h0 * plan.h0);
  let vis = va / (va + (dist - mu) ** 2);
  vis = vis * vis * vis;
  return vis < 0.05 ? 0 : vis;
}

// The level-0 index of level l's probe idx, or -1 when level 0's grid ends before it.
export function levelZeroIndex(plan, l, idx) {
  const L = plan.levels[l], n0 = plan.levels[0].n, s = 2 ** l;
  const x = (idx % L.n[0]) * s, y = (Math.floor(idx / L.n[0]) % L.n[1]) * s, z = Math.floor(idx / (L.n[0] * L.n[1])) * s;
  return x < n0[0] && y < n0[1] && z < n0[2] ? (z * n0[1] + y) * n0[0] + x : -1;
}

export const usesSegment = (mw) => mw !== "vis";
// A screen probe's per-probe weights of level 1's eight neighbours on surface x, given the segment
// test's bits (bit c) and level 1's flags (256 = inside geometry): 0 inside geometry; the bit unless
// mw uses point visibility; times the point visibility when it does.
export function levelOneWeights(ctx, x, bits, flags1) {
  const { plan, depth } = ctx, at = add(x.p, mul(x.ng, 0.25 * plan.h0));
  return neighbours(plan, 1, x.p).map((q, c) => {
    if (flags1[q.idx] & 256) return 0;
    if (usesSegment(plan.mw)) return bits & (1 << c) ? 1 : 0;
    const q0 = levelZeroIndex(plan, 1, q.idx);
    return q0 < 0 ? 1 : chebVis(plan, depth, q0, q.pos, at);
  });
}
// Level 1's values at p merged with trilinear weights times wts (and, with sv, the value's start
// visibility, its 4th element); all eight when every weight is 0.
export function mergeWeighted(ctx, p, wts, value, fallback) {
  return mergeOf(ctx, neighbours(ctx.plan, 1, p).map((q, c) => (q.w > 0 ? { w: q.w, v: value(q.idx), t: wts[c] } : null)), fallback, true);
}
// A merge over rows { w: trilinear weight, v: value (with sv: rgb, share), t: trust }: the mean
// weighed by w t share. No trusted weight: ft traces on (fallback), fz gives no light, else the plain
// mean of all rows.
export function mergeOf(ctx, rows, fallback, screen = false) {
  const out = [0, 0, 0], all = [0, 0, 0];
  let ws = 0, wa = 0, wt = 0;
  for (const r of rows) {
    if (!r) continue;
    const w = r.w * r.t * (r.v.length > 3 ? r.v[3] : 1);
    for (let c = 0; c < 3; c++) { all[c] += r.w * r.v[c]; out[c] += w * r.v[c]; }
    wa += r.w; ws += w; wt += r.w * r.t;
  }
  if (ws > 0 && ctx.plan.fd && (screen || ctx.plan.fd === 2) && wt - ws > 1e-6 * wt) { // fd: the dropped share takes the own ray
    const v = fallback();
    return [0, 1, 2].map((c) => (out[c] + (wt - ws) * v[c]) / wt);
  }
  if (!(ws > 0) && ctx.plan.ft) return fallback();
  if (!(ws > 0) && ctx.plan.fz) return [0, 0, 0];
  return ws > 0 ? mul(out, 1 / ws) : mul(all, 1 / Math.max(wa, 1e-30));
}
// cg 2, 3 (cascades3d.mjs gatherCache): lit-side probes outside geometry whose farthest (cg 2) or
// nearest (cg 3) depth ray of the texel toward the lifted point reaches it; none gives no light.
export function gatherDepthSeen(ctx, cache, x, nb, plan, depth, cg = ctx.plan.cg) {
  const at = add(x.p, mul(x.ng, 0.25 * plan.h0)), nt = 6 * plan.cd * plan.cd, k = cg === 3 ? 2 : 3, E = [0, 0, 0];
  let ws = 0;
  for (const q of nb) {
    const o = 16 * q.idx;
    if (q.w <= 0 || cache[o + 3] > 0 || dot(sub(q.pos, x.p), x.ng) <= 0) continue;
    const v = sub(at, q.pos), dist = Math.hypot(...v);
    if (dist > 0 && depth[4 * (q.idx * nt + texelOf(mul(v, 1 / dist), plan.cd)) + k] < dist) continue;
    for (let c = 0; c < 3; c++) E[c] += q.w * Math.max(0, cache[o + c] + cache[o + 4 + c] * x.n[0] + cache[o + 8 + c] * x.n[1] + cache[o + 12 + c] * x.n[2]);
    ws += q.w;
  }
  return ws > 0 ? mul(E, 1 / ws) : null;
}
