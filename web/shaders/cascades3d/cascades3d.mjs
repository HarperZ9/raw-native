// World-space radiance cascades in 3D, CPU reference (float64). Method: A. Sannikov, "Radiance
// Cascades: A Novel Approach to Calculating Global Illumination" (working paper), extended to 3D world
// probes as the paper describes; written from the description, no code taken.
//
//   const S = prepare(scene, buildBvh(scene)), plan = planCascades3d(bounds(scene), opts);
//   const out = runCascades3d(scene, S, plan, W, H);    // { radiance: Float64Array W*H*3, hit }
//
// A frame: (1) per level a probe pass marks probes inside solid geometry (6 axis rays, most hits from
// behind) and, with fix "probe", which of the 8 next-level neighbours each probe sees (one segment a
// pair); (2) levels merge top-down, each entry tracing its interval and, on a miss, taking the next
// level's children texels at the neighbours; (3) level 0's merged radiance becomes an L1 irradiance
// cache per probe; (4) screen probes on the visible surface every `stride` pixels gather m^2 cosine
// rays over [0, T_1] and continue from level 1; (5) every pixel takes the screen probes' irradiance,
// upsampled, plus the shadowed sun. A ray that hits a surface returns emission + albedo / pi x
// (sun + cached irradiance from the previous frame), so each frame adds one bounce.
//
// fix "none": plain trilinear merge. "probe" (the cheap fix): the merge skips neighbours a probe cannot
// see and probes inside geometry (one segment a probe pair), and intervals overlap the next level's
// (planCascades3d T1x). "full": every entry traces one ray to each neighbour's interval start (the
// flatland path's fix, 8 rays an entry).
import { closestHit, occluded } from "./bvh.mjs";
import { sub, add, mul, dot, cross, norm, texelDir, texelOf, faceOf, cosineDir, jitter, neighbours, probePos } from "./math3d.mjs";
import { cameraBasis, pixelDir } from "./scene3d.mjs";

const AXES = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]];
const tri3 = (A, s, k) => [A[3 * s + 0 + 3 * k], A[3 * s + 1 + 3 * k], A[3 * s + 2 + 3 * k]];

export function surfaceAt(S, o, d, h) {
  const s = h.slot, a = tri3(S.tris, 3 * s, 0), b = tri3(S.tris, 3 * s, 1), c = tri3(S.tris, 3 * s, 2);
  let ng = norm(cross(sub(b, a), sub(c, a)));
  const back = dot(ng, d) > 0;
  if (back) ng = mul(ng, -1);
  const w = 1 - h.u - h.v, n0 = tri3(S.nrm, 3 * s, 0), n1 = tri3(S.nrm, 3 * s, 1), n2 = tri3(S.nrm, 3 * s, 2);
  let n = norm(add(add(mul(n0, w), mul(n1, h.u)), mul(n2, h.v)));
  if (dot(n, ng) < 0) n = mul(n, -1);
  return { p: add(o, mul(d, h.t)), n, ng, back, slot: s, alb: tri3(S.alb, s, 0), emit: tri3(S.emit, s, 0) };
}
export function offset(x, dir) {
  const e = 2e-4 * (1 + Math.max(Math.abs(x.p[0]), Math.abs(x.p[1]), Math.abs(x.p[2])));
  return add(x.p, mul(x.ng, dot(x.ng, dir) > 0 ? e : -e));
}
// Shadowed sun irradiance scale at x (multiply by scene.sunE).
export function sunScale(S, sc, x) {
  if (Math.max(...sc.sunE) <= 0) return 0;
  const c = dot(x.n, sc.sunDir);
  return c > 0 && !occluded(S, offset(x, sc.sunDir), sc.sunDir, 1e30) ? c : 0;
}
// Irradiance at a surface from level 0's L1 cache (16 floats a probe: a.rgb, inside, bx, by, bz).
// A probe counts when it is not inside geometry, sits on the surface's lit side, and its depth
// moments (depthPass) say it sees the point: Chebyshev's bound on the share of its texel that reaches
// the point, cubed, and dropped under 0.05. The test point is lifted off the surface by a quarter
// spacing (a normal bias, as in irradiance-probe practice). If no probe counts, the lit-side probes
// are used without the depth test, then the plain trilinear mix of probes outside geometry, then of
// all eight (a surface hidden inside geometry, seen only by probes inside it).
export function gatherCache(ctx, cache, x) {
  const { plan, depth } = ctx, nb = neighbours(plan, 0, x.p), E = [0, 0, 0], Ef = [0, 0, 0], El = [0, 0, 0], cd = plan.cd, nt = 6 * cd * cd;
  let ws = 0, wf = 0, wl = 0, wa = 0;
  const Ea = [0, 0, 0];
  const at = add(x.p, mul(x.ng, 0.25 * plan.h0));
  for (const q of nb) {
    if (q.w <= 0) continue;
    const o = 16 * q.idx, e = [0, 1, 2].map((c) => Math.max(0, cache[o + c] + cache[o + 4 + c] * x.n[0] + cache[o + 8 + c] * x.n[1] + cache[o + 12 + c] * x.n[2]));
    for (let c = 0; c < 3; c++) Ea[c] += q.w * e[c];
    wa += q.w;
    if (cache[o + 3] > 0) continue;
    for (let c = 0; c < 3; c++) Ef[c] += q.w * e[c];
    wf += q.w;
    const v = sub(at, q.pos), dist = Math.hypot(...v);
    if (dot(sub(q.pos, x.p), x.ng) <= 0) continue; // the probe is behind the surface's plane
    for (let c = 0; c < 3; c++) El[c] += q.w * e[c];
    wl += q.w;
    let vis = 1;
    if (dist > 0) {
      const t = 2 * (q.idx * nt + texelOf(mul(v, 1 / dist), cd)), mu = depth[t], var_ = Math.max(depth[t + 1] - mu * mu, 1e-4 * plan.h0 * plan.h0);
      if (dist > mu) { vis = var_ / (var_ + (dist - mu) ** 2); vis = vis * vis * vis; if (vis < 0.05) vis = 0; }
    }
    const w = q.w * vis;
    if (w > 0) { for (let c = 0; c < 3; c++) E[c] += w * e[c]; ws += w; }
  }
  if (ws > 0) return mul(E, 1 / ws);
  if (wl > 0) return mul(El, 1 / wl);
  return wf > 0 ? mul(Ef, 1 / wf) : mul(Ea, 1 / wa);
}
// Depth moments of level 0's probes: cd x cd texels a face, 2 x 2 rays a texel, distances capped at
// plan.dmax. Static geometry: traced once, like the BVH.
export function depthPass(S, plan) {
  const L = plan.levels[0], cd = plan.cd, nt = 6 * cd * cd, out = new Float64Array(2 * L.probes * nt);
  for (let q = 0; q < L.probes; q++) {
    const p = probePos(plan, 0, q);
    for (let k = 0; k < nt; k++) {
      let m1 = 0, m2 = 0;
      for (let r = 0; r < 4; r++) {
        const d = subTexelDir(k, cd, r & 1, r >> 1), h = closestHit(S, p, d, 0, plan.dmax), t = h ? h.t : plan.dmax;
        m1 += t / 4; m2 += t * t / 4;
      }
      out[2 * (q * nt + k)] = m1; out[2 * (q * nt + k) + 1] = m2;
    }
  }
  return out;
}
// Direction through sub-texel (a, b) of 2 x 2 in texel k of face side c.
export function subTexelDir(k, c, a, b) {
  const f = Math.floor(k / (c * c)), r = k - f * c * c, j = Math.floor(r / c), i = r - j * c, m = f >> 1, d = [0, 0, 0];
  d[m] = f & 1 ? -1 : 1; d[(m + 1) % 3] = (i + 0.25 + 0.5 * a) / c * 2 - 1; d[(m + 2) % 3] = (j + 0.25 + 0.5 * b) / c * 2 - 1;
  return norm(d);
}
export function surfaceRadiance(ctx, cache, x, d) {
  const { S, sc } = ctx;
  if (dot(x.n, d) >= 0) return x.emit; // seen from below its shading normal: as the path tracer, emission only
  const sun = sunScale(S, sc, x), E = cache ? gatherCache(ctx, cache, x) : [0, 0, 0];
  return [0, 1, 2].map((c) => x.emit[c] + x.alb[c] / Math.PI * (sc.sunE[c] * sun + E[c]));
}
const traced = (ctx, o, d, t0, t1) => {
  const h = closestHit(ctx.S, o, d, t0, t1);
  return h ? surfaceRadiance(ctx, ctx.cachePrev, surfaceAt(ctx.S, o, d, h), d) : null;
};

// Probe pass of level l: bit 8 = inside, bit c = neighbour c of level l+1 visible.
export function probeFlags(ctx, l) {
  const { plan, S } = ctx, L = plan.levels[l], out = new Uint32Array(L.probes), top = l === plan.levels.length - 1;
  for (let idx = 0; idx < L.probes; idx++) {
    const p = probePos(plan, l, idx);
    let back = 0, front = 0;
    for (const a of AXES) { const h = closestHit(S, p, a, 0, L.h); if (h) { if (dot(norm(cross(...triEdges(S, h.slot))), a) > 0) back++; else front++; } }
    let f = back >= 3 && back > front ? 256 : 0;
    if (!top) neighbours(plan, l + 1, p).forEach((q, c) => { const e = sub(q.pos, p), len = Math.hypot(...e); if (len === 0 || !occluded(S, p, mul(e, 1 / len), len)) f |= 1 << c; });
    out[idx] = f;
  }
  return out;
}
function triEdges(S, s) { const a = tri3(S.tris, 3 * s, 0); return [sub(tri3(S.tris, 3 * s, 1), a), sub(tri3(S.tris, 3 * s, 2), a)]; }

// The next level's merged radiance at probe q for parent texel k (face side c): its 4 children,
// weighted by solid angle.
export function children(ctx, l1, buf, q, k, c) {
  const L = ctx.plan.levels[l1], cc = 2 * c, f = Math.floor(k / (c * c)), r = k - f * c * c, j = Math.floor(r / c), i = r - j * c;
  const om = ctx.plan.omega[l1], out = [0, 0, 0];
  let ws = 0;
  for (let b = 0; b < 2; b++) for (let a = 0; a < 2; a++) {
    const ti = 2 * i + a, tj = 2 * j + b, w = om[tj * cc + ti], o = 3 * (q * L.texels + f * cc * cc + tj * cc + ti);
    out[0] += w * buf[o]; out[1] += w * buf[o + 1]; out[2] += w * buf[o + 2]; ws += w;
  }
  return mul(out, 1 / ws);
}
// Level l1's merged radiance at probe q in direction d: bilinear over the face's texel centres.
export function lookup(ctx, l1, buf, q, d) {
  const L = ctx.plan.levels[l1], c = L.c, { f, s, t } = faceOf(d);
  const u = Math.min(c - 1, Math.max(0, (s + 1) / 2 * c - 0.5)), v = Math.min(c - 1, Math.max(0, (t + 1) / 2 * c - 0.5));
  const i0 = Math.min(c - 2, Math.floor(u)), j0 = Math.min(c - 2, Math.floor(v)), out = [0, 0, 0];
  if (c === 1) { const o = 3 * (q * L.texels + f); return [buf[o], buf[o + 1], buf[o + 2]]; }
  const fu = u - i0, fv = v - j0;
  for (const [i, j, w] of [[i0, j0, (1 - fu) * (1 - fv)], [i0 + 1, j0, fu * (1 - fv)], [i0, j0 + 1, (1 - fu) * fv], [i0 + 1, j0 + 1, fu * fv]]) {
    const o = 3 * (q * L.texels + f * c * c + j * c + i);
    out[0] += w * buf[o]; out[1] += w * buf[o + 1]; out[2] += w * buf[o + 2];
  }
  return out;
}

// One entry of level l: probe idx, texel k. `next` is level l+1's merged buffer, flags this level's.
export function entry(ctx, l, idx, k, next, flags, nextFlags) {
  const { plan, sc } = ctx, L = plan.levels[l], p = probePos(plan, l, idx), d = texelDir(k, L.c), top = l === plan.levels.length - 1;
  if (plan.fix === "full" && !top) {
    const s = add(p, mul(d, L.T0)), out = [0, 0, 0];
    for (const q of neighbours(plan, l + 1, p)) {
      if (q.w <= 0) continue;
      const e = sub(add(q.pos, mul(d, L.T1)), s), len = Math.hypot(...e);
      const v = traced(ctx, s, mul(e, 1 / len), 0, len) || children(ctx, l + 1, next, q.idx, k, L.c);
      for (let c = 0; c < 3; c++) out[c] += q.w * v[c];
    }
    return out;
  }
  const v = traced(ctx, p, d, L.T0, L.T1x);
  if (v) return v;
  if (top) return sc.sky;
  return mergeNext(ctx, l + 1, p, flags ? flags[idx] : 511, nextFlags, (q) => children(ctx, l + 1, next, q, k, L.c));
}
function mergeNext(ctx, l1, p, bits, nextFlags, value) {
  const out = [0, 0, 0], all = [0, 0, 0];
  let ws = 0, wa = 0;
  neighbours(ctx.plan, l1, p).forEach((q, c) => {
    if (q.w <= 0) return;
    const v = value(q.idx);
    for (let ch = 0; ch < 3; ch++) all[ch] += q.w * v[ch];
    wa += q.w;
    if (ctx.plan.fix === "probe" && (!(bits & (1 << c)) || (nextFlags[q.idx] & 256))) return;
    for (let ch = 0; ch < 3; ch++) out[ch] += q.w * v[ch];
    ws += q.w;
  });
  return ws > 0 ? mul(out, 1 / ws) : mul(all, 1 / Math.max(wa, 1e-30));
}
// Level 0's merged radiance as an L1 irradiance cache: E(n) = a + bx n.x + by n.y + bz n.z.
export function cacheOf(ctx, buf0, flags0) {
  const L = ctx.plan.levels[0], om = ctx.plan.omega[0], out = new Float64Array(16 * L.probes), cc = L.c * L.c;
  const Y0 = 0.28209479177387814, Y1 = 0.4886025119029199, A0 = Math.PI, A1 = 2 * Math.PI / 3;
  for (let q = 0; q < L.probes; q++) {
    const o = 16 * q;
    for (let k = 0; k < L.texels; k++) {
      const d = texelDir(k, L.c), w = om[k % cc], b = 3 * (q * L.texels + k);
      for (let c = 0; c < 3; c++) {
        const v = w * buf0[b + c];
        out[o + c] += A0 * Y0 * Y0 * v;
        out[o + 4 + c] += A1 * Y1 * Y1 * v * d[0]; out[o + 8 + c] += A1 * Y1 * Y1 * v * d[1]; out[o + 12 + c] += A1 * Y1 * Y1 * v * d[2];
      }
    }
    out[o + 3] = flags0[q] & 256 ? 1 : 0;
  }
  return out;
}

// Primary hits: { hit, x (surface), sun (scale) } a pixel.
export function gbuffer(S, sc, W, H) {
  const k = cameraBasis(sc.camera, W, H), out = [];
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const d = pixelDir(k, x, y, W, H), h = closestHit(S, k.eye, d, 0, 1e30);
    if (!h) { out.push(null); continue; }
    const s = surfaceAt(S, k.eye, d, h);
    out.push({ x: s, d, sun: sunScale(S, sc, s) });
  }
  return out;
}
// Screen probe at surface x: irradiance from m^2 cosine rays over [0, T_1], then level 1.
export function screenProbe(ctx, x, pid, buf1, flags1) {
  const { plan } = ctx, m = plan.m, T1 = plan.levels[0].T1, T1x = plan.levels[0].T1x, E = [0, 0, 0], one = plan.levels.length > 1;
  const up = offset(x, x.ng);
  let bits = 0;
  if (one && plan.fix === "probe") neighbours(plan, 1, x.p).forEach((q, c) => { const e = sub(q.pos, up), len = Math.hypot(...e); if (len === 0 || !occluded(ctx.S, up, mul(e, 1 / len), len)) bits |= 1 << c; });
  for (let j = 0; j < m; j++) for (let i = 0; i < m; i++) {
    const [h1, h2] = jitter(pid, j * m + i), d = cosineDir(x.n, i, j, m, h1, h2), o = offset(x, d);
    let v;
    if (one && plan.fix === "full") {
      v = [0, 0, 0];
      for (const q of neighbours(plan, 1, x.p)) {
        if (q.w <= 0) continue;
        const e = sub(add(q.pos, mul(d, T1)), o), len = Math.hypot(...e);
        const r = traced(ctx, o, mul(e, 1 / len), 0, len) || lookup(ctx, 1, buf1, q.idx, d);
        for (let c = 0; c < 3; c++) v[c] += q.w * r[c];
      }
    } else {
      v = traced(ctx, o, d, 0, one ? T1x : Infinity);
      if (!v) v = one ? mergeNext(ctx, 1, x.p, bits, flags1, (q) => lookup(ctx, 1, buf1, q, d)) : ctx.sc.sky;
    }
    for (let c = 0; c < 3; c++) E[c] += v[c];
  }
  return mul(E, Math.PI / (m * m));
}
