// World-level options of the 3D cascades, CPU float64 (wgsl3d-world.mjs is the GPU side). Each is a
// plan switch (math3d.mjs planCascades3d), off by default:
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
import { add, mul, sub, texelOf, neighbours } from "./math3d.mjs";

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
  const t = 2 * (q0 * 6 * cd * cd + texelOf(mul(v, 1 / dist), cd)), mu = depth[t];
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
  const out = [0, 0, 0], all = [0, 0, 0];
  let ws = 0, wa = 0;
  neighbours(ctx.plan, 1, p).forEach((q, c) => {
    if (q.w <= 0) return;
    const v = value(q.idx), w = q.w * wts[c] * (v.length > 3 ? v[3] : 1);
    for (let ch = 0; ch < 3; ch++) { all[ch] += q.w * v[ch]; out[ch] += w * v[ch]; }
    wa += q.w; ws += w;
  });
  if (!(ws > 0) && ctx.plan.ft) return fallback();
  if (!(ws > 0) && ctx.plan.fz) return [0, 0, 0];
  return ws > 0 ? mul(out, 1 / ws) : mul(all, 1 / Math.max(wa, 1e-30));
}
