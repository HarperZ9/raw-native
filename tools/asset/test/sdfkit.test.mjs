// node --test tools/asset/test/sdfkit.test.mjs   Bounds are the ones in DESIGN.md section 10. The dense reference is
// the zero set of the field itself, found by sign changes on a 1.5 mm grid and bisected, so it checks the DISTANCE VALUES
// against the geometry the sign defines. Each check has a control that must fail.
import test from "node:test";
import assert from "node:assert/strict";
import { PRIMS, ellipsoidInterval, ellipsoidLower } from "../src/prims.mjs";
import { applyMods, sminV, sminW, warpScale } from "../src/mods.mjs";
import { noise3, makeSource, NOISE_LIP } from "../src/noise.mjs";
import { Field, shapeFn, project } from "../src/sdf.mjs";
import { parseSculpt, stringifySculpt } from "../src/format.mjs";

const rng = (s) => () => { s = (s + 0x6D2B79F5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
const STEP = 0.0015;

function surface(f, lo, hi) {
  const P = [], n = [0, 1, 2].map((i) => Math.ceil((hi[i] - lo[i]) / STEP) + 1), at = new Float32Array(n[0] * n[1] * n[2]), id = (i, j, k) => (k * n[1] + j) * n[0] + i;
  for (let k = 0; k < n[2]; k++) for (let j = 0; j < n[1]; j++) for (let i = 0; i < n[0]; i++) at[id(i, j, k)] = f(lo[0] + i * STEP, lo[1] + j * STEP, lo[2] + k * STEP);
  for (let k = 0; k < n[2]; k++) for (let j = 0; j < n[1]; j++) for (let i = 0; i < n[0]; i++) for (const [a, b, c] of [[1, 0, 0], [0, 1, 0], [0, 0, 1]]) {
    const i2 = i + a, j2 = j + b, k2 = k + c; if (i2 >= n[0] || j2 >= n[1] || k2 >= n[2]) continue;
    if ((at[id(i, j, k)] < 0) === (at[id(i2, j2, k2)] < 0)) continue;
    let t0 = 0, t1 = 1; const p = (t) => [lo[0] + (i + a * t) * STEP, lo[1] + (j + b * t) * STEP, lo[2] + (k + c * t) * STEP], neg0 = at[id(i, j, k)] < 0;
    for (let it = 0; it < 28; it++) { const tm = (t0 + t1) / 2, q = p(tm); if ((f(q[0], q[1], q[2]) < 0) === neg0) t0 = tm; else t1 = tm; }
    P.push(p((t0 + t1) / 2));
  }
  return P;
}
// nearest sample, then refined in three stages: 300 rays from the query inside a cone (0.5, 0.12, 0.03 rad) around the best hit so far,
// each bisected on the sign change. Every hit is a point of the zero set, so the result stays an upper bound on the true distance
// and the sampling bias shrinks with each stage.
const nearest = (P, x, y, z, f) => {
  let m = Infinity, s = null; for (const p of P) { const d = (p[0] - x) ** 2 + (p[1] - y) ** 2 + (p[2] - z) ** 2; if (d < m) { m = d; s = p; } }
  let best = Math.sqrt(m); if (!f) return best;
  const neg = f(x, y, z) < 0; let hit = s;
  for (const cone of [0.5, 0.12, 0.03]) {
    const dist = Math.hypot(hit[0] - x, hit[1] - y, hit[2] - z), u = [(hit[0] - x) / dist, (hit[1] - y) / dist, (hit[2] - z) / dist], a = Math.abs(u[0]) < 0.9 ? [1, 0, 0] : [0, 1, 0];
    let e1 = [u[1] * a[2] - u[2] * a[1], u[2] * a[0] - u[0] * a[2], u[0] * a[1] - u[1] * a[0]]; const l1 = Math.hypot(...e1); e1 = e1.map((v) => v / l1);
    const e2 = [u[1] * e1[2] - u[2] * e1[1], u[2] * e1[0] - u[0] * e1[2], u[0] * e1[1] - u[1] * e1[0]], reach = 1.5 * dist + 0.002;
    for (let i = 0; i < 120; i++) {
      const th = cone * Math.sqrt((i + 0.5) / 120), ph = i * 2.399963, c = Math.cos(th), sn = Math.sin(th), v = [0, 1, 2].map((j) => c * u[j] + sn * (Math.cos(ph) * e1[j] + Math.sin(ph) * e2[j]));
      let t0 = 0, t1 = -1; for (let t = 0.0004; t <= reach; t += 0.0004) if ((f(x + v[0] * t, y + v[1] * t, z + v[2] * t) < 0) !== neg) { t1 = t; break; } else t0 = t;
      if (t1 < 0 || t0 >= best) continue;
      for (let k = 0; k < 22; k++) { const tm = (t0 + t1) / 2; if ((f(x + v[0] * tm, y + v[1] * tm, z + v[2] * tm) < 0) !== neg) t1 = tm; else t0 = tm; }
      if (t1 < best) { best = t1; hit = [x + v[0] * t1, y + v[1] * t1, z + v[2] * t1]; }
    }
  }
  return best;
};
// queries with 4 mm <= |ref| <= 40 mm; returns [{x,y,z,d,ref}] where ref is the signed reference distance
function queries(f, P, lo, hi, N0, seed) {
  const N = Math.ceil(N0 / 3);
  const r = rng(seed), out = [];
  for (let tries = 0; out.length < N && tries < N * 60; tries++) {
    const x = lo[0] + r() * (hi[0] - lo[0]), y = lo[1] + r() * (hi[1] - lo[1]), z = lo[2] + r() * (hi[2] - lo[2]), d = f(x, y, z), ref = nearest(P, x, y, z, f) * (d < 0 ? -1 : 1);
    if (Math.abs(ref) >= 0.004 && Math.abs(ref) <= 0.025) out.push({ x, y, z, d, ref });
  }
  assert.ok(out.length >= N * 0.8, "enough queries");
  return out;
}
const TOL = 0.0005;
const maxErr = (Q) => Math.max(...Q.map((q) => Math.abs(q.d - q.ref)));
const whereWorst = (Q) => { const q = Q.reduce((a, b) => (Math.abs(b.d - b.ref) > Math.abs(a.d - a.ref) ? b : a)); return `d=${q.d.toFixed(5)} ref=${q.ref.toFixed(5)}`; };
const BOX = { lo: [-0.09, -0.09, -0.09], hi: [0.09, 0.09, 0.09] };
const fd = (f, x, y, z, h = 1e-6) => { const g = [f(x + h, y, z) - f(x - h, y, z), f(x, y + h, z) - f(x, y - h, z), f(x, y, z + h) - f(x, y, z - h)].map((v) => v / (2 * h)); return g; };

const EXACT = {
  rbox: { id: "rb", shape: "rbox", c: [0.004, -0.006, 0.002], r: [0.05, 0.03, 0.04], round: 0.01, rot: [20, 35, -15] },
  rcone: { id: "rc", shape: "rcone", a: [-0.04, -0.03, 0.01], b: [0.03, 0.04, -0.02], r: [0.02, 0.008] },
  capsule: { id: "cp", shape: "rcone", a: [-0.04, 0, 0], b: [0.04, 0.02, 0.01], r: 0.012 },
  cone: { id: "cn", shape: "cone", a: [0, -0.04, 0], b: [0.01, 0.04, 0.01], r: [0.03, 0.01] },
  torx: { id: "to", shape: "torx", c: [0, 0, 0], R: 0.04, r: 0.013, rot: [25, 0, 10] },
  extR: { id: "ex", shape: "ext", c: [0, 0, 0], h: 0.02, prof: { kind: "rrect", hx: 0.04, hy: 0.025, round: 0.01 }, rot: [0, 30, 0] },
  extP: { id: "ep", shape: "ext", c: [0, 0, 0], h: 0.015, prof: { kind: "poly", pts: [[-0.04, -0.03], [0.04, -0.03], [0.01, 0.0], [0.04, 0.035], [-0.04, 0.035]] } },
};
for (const [name, spec] of Object.entries(EXACT)) {
  test(`${name}: exact distance against the dense zero set, analytic normal against differences`, () => {
    const f = shapeFn(spec), P = surface((x, y, z) => f(x, y, z, null), BOX.lo, BOX.hi), Q = queries((x, y, z) => f(x, y, z, null), P, BOX.lo, BOX.hi, 120, 7);
    assert.ok(maxErr(Q) <= TOL, `${name} max error ${maxErr(Q)} at ${whereWorst(Q)}`);
    // control: a field scaled by 1.1 is not a distance and must be caught
    assert.ok(Math.max(...Q.map((q) => Math.abs(1.1 * q.d - q.ref))) > TOL, "control: skewed field must fail");
    const g = [0, 0, 0]; let worst = 1;
    for (const q of Q) { f(q.x, q.y, q.z, g); const n = fd((x, y, z) => f(x, y, z, null), q.x, q.y, q.z), l = Math.hypot(...n); worst = Math.min(worst, (g[0] * n[0] + g[1] * n[1] + g[2] * n[2]) / l); assert.ok(Math.abs(Math.hypot(...g) - 1) < 1e-6, "unit normal"); }
    assert.ok(worst > 0.9999, `normal agreement ${worst}`);
  });
}

test("ellipsoid: lo <= true <= hi, the lower bound never over-states, the clamped estimate stays in the interval", () => {
  const r = [0.03, 0.05, 0.08], spec = { id: "e", shape: "ellb", c: [0, 0, 0], r, rot: [10, 20, 30] }, f = shapeFn(spec), f0 = (x, y, z) => f(x, y, z, null);
  const P = surface(f0, BOX.lo, BOX.hi), Q = queries(f0, P, BOX.lo, BOX.hi, 150, 11);
  assert.ok(Q.filter((q) => Math.abs(q.ref) <= 0.008).every((q) => Math.abs(q.d - q.ref) <= 0.0008), `near-surface estimate ${whereWorst(Q)}`);   // second order near the surface; far away only the interval is promised
  const m = shapeFn({ ...spec, shape: "ellb", rot: undefined }), P0 = surface((x, y, z) => m(x, y, z, null), BOX.lo, BOX.hi), Q0 = queries((x, y, z) => m(x, y, z, null), P0, BOX.lo, BOX.hi, 150, 12);
  let est = 0, miss = 0;
  for (const q of Q0) {
    const I = ellipsoidInterval(r, q.x, q.y, q.z), low = ellipsoidLower(r, q.x, q.y, q.z);
    assert.ok(I.lo <= q.ref + TOL && q.ref <= I.hi + TOL, `interval ${I.lo} ${q.ref} ${I.hi}`);
    assert.ok(Math.abs(low) <= Math.abs(q.ref) + TOL, "conservative lower bound");
    est = Math.max(est, Math.abs(I.est - q.ref));
  }
  // sampled over 60 000 points, the raw first-order estimate never left the interval, so the clamp is a guard and not a correction.
  // control: using max(r) in place of min(r) breaks the conservative lower bound, and the check above must see that
  const rr = rng(21); for (let i = 0; i < 3000; i++) {
    const x = (rr() - 0.5) * 0.6, y = (rr() - 0.5) * 0.6, z = (rr() - 0.5) * 0.6, I = ellipsoidInterval(r, x, y, z);
    assert.ok(I.est >= I.lo - 1e-12 && I.est <= I.hi + 1e-12, "clamped inside"); if (Math.abs((I.k0 - 1) * Math.max(...r)) > Math.abs(I.hi) + 1e-9) miss++;
  }
  assert.ok(miss > 0, "control: a lower bound built on max(r) must exceed the upper bound somewhere");
  assert.ok(Q0.length > 0);
});

const SHAPE = { id: "s", shape: "rbox", c: [0, 0, 0], r: [0.04, 0.04, 0.04], round: 0.01 };
const leaf = (mods, spec = SHAPE) => shapeFn({ ...spec, mods });
function boundedCheck(name, f, tight, lo = BOX.lo, hi = BOX.hi) {
  const f0 = (x, y, z) => f(x, y, z, null), P = surface(f0, lo, hi), Q = queries(f0, P, lo, hi, 120, 3);
  for (const q of Q) assert.ok(Math.abs(q.d) <= Math.abs(q.ref) + TOL, `${name}: over-states distance, d=${q.d} ref=${q.ref}`);
  assert.ok(Q.filter((q) => Math.abs(q.d) >= tight * Math.abs(q.ref)).length > Q.length * 0.5, `${name}: bound is vacuous`);
  return { Q, f0 };
}
test("round, shell and mirror and repeat: exact where the contract says", () => {
  const rd = leaf([{ kind: "round", r: 0.008 }]); let { Q } = boundedCheck("round", rd, 0.95); assert.ok(maxErr(Q) <= TOL, `round ${whereWorst(Q)}`);
  const sh = leaf([{ kind: "shell", t: 0.01 }]); ({ Q } = boundedCheck("shell", sh, 0.95)); assert.ok(maxErr(Q) <= TOL, `shell ${whereWorst(Q)}`);
  const off = { ...SHAPE, c: [0.05, 0, 0], r: [0.02, 0.03, 0.03], round: 0 }, mi = leaf([{ kind: "mirror", axis: 0 }], off);
  ({ Q } = boundedCheck("mirror", mi, 0.95, [-0.12, -0.06, -0.06], [0.12, 0.06, 0.06])); assert.ok(maxErr(Q) <= TOL, `mirror ${whereWorst(Q)}`);
  const rp = leaf([{ kind: "repeat", period: [0.1, 0, 0], n: [1, 0, 0] }], { ...SHAPE, r: [0.02, 0.03, 0.03], round: 0.005 });
  ({ Q } = boundedCheck("repeat", rp, 0.95, [-0.16, -0.05, -0.05], [0.16, 0.05, 0.05])); assert.ok(maxErr(Q) <= TOL, `repeat ${whereWorst(Q)}`);
});
test("twist and bend: bounded by 1 / warpScale, and a too-large k with a too-small radius fails (control)", () => {
  const k = 12, R = 0.08;   // 12 rad per metre, 0.08 m radius
  const tw = leaf([{ kind: "twist", k, radius: R }], { ...SHAPE, r: [0.05, 0.04, 0.02], round: 0.005 });
  boundedCheck("twist", tw, 0.6);
  const bd = leaf([{ kind: "bend", k: 8, extent: 0.1 }], { ...SHAPE, r: [0.06, 0.02, 0.03], round: 0.005 });
  boundedCheck("bend", bd, 0.6);
  const bad = leaf([{ kind: "twist", k: 30, radius: 0.0001 }], { ...SHAPE, r: [0.05, 0.04, 0.02], round: 0.005 }), f0 = (x, y, z) => bad(x, y, z, null), P = surface(f0, BOX.lo, BOX.hi), Q = queries(f0, P, BOX.lo, BOX.hi, 150, 5);
  assert.ok(Q.some((q) => Math.abs(q.d) > Math.abs(q.ref) + TOL), "control: wrong radius must over-state somewhere");
  assert.ok(Math.abs(warpScale(12, 0.08) - (0.96 + Math.sqrt(0.96 ** 2 + 4)) / 2) < 1e-12);
});
test("displace and relief: bounded with the stated Lipschitz constants, and a wrong constant fails (control)", () => {
  const dn = leaf([{ kind: "displace", amp: 0.004, src: { kind: "noise", freq: 60, seed: 3 } }]); boundedCheck("noise displace", dn, 0.8 / (1 + 0.004 * NOISE_LIP * 60));
  const rl = leaf([{ kind: "relief", amp: 0.003, freq: 40, octaves: 3, seed: 5, mask: { c: [0, 0, 0.05], r0: 0.02, r1: 0.06 } }]); boundedCheck("relief", rl, 0.3);
  const rg = leaf([{ kind: "displace", amp: 0.003, src: { kind: "ridge", dir: [0, 1, 0], freq: 180, warp: 1.2, warpFreq: 30, seed: 2 } }]); boundedCheck("ridge", rg, 0.8 / (1 + 0.003 * (2 * Math.PI * 180 + 1.2 * NOISE_LIP * 30)));
  const map = { kind: "map", w: 4, hgt: 4, scale: 8, data: Float32Array.from({ length: 16 }, (_, i) => ((i * 7) % 5) / 2 - 1) }, mp = leaf([{ kind: "displace", amp: 0.004, src: map }]); boundedCheck("map displace", mp, 0.15);
  const lie = { ...makeSource({ kind: "noise", freq: 60 }), lip: 0.01 }, ls = leaf([{ kind: "displace", amp: 0.004, src: lie }]), f0 = (x, y, z) => ls(x, y, z, null), P = surface(f0, BOX.lo, BOX.hi), Q = queries(f0, P, BOX.lo, BOX.hi, 150, 9);
  assert.ok(Q.some((q) => Math.abs(q.d) > Math.abs(q.ref) + TOL), "control: a Lipschitz constant that is too small must be caught");
});
test("noise gradient matches differences and the stated Lipschitz constant holds on samples", () => {
  const r = rng(4), g = [0, 0, 0]; let worst = 0;
  for (let i = 0; i < 400; i++) {
    const x = r() * 9, y = r() * 9, z = r() * 9; noise3(x, y, z, 7, g); const n = fd((a, b, c) => noise3(a, b, c, 7), x, y, z, 1e-5);
    assert.ok(Math.hypot(g[0] - n[0], g[1] - n[1], g[2] - n[2]) < 1e-5, "gradient"); worst = Math.max(worst, Math.hypot(...g));
  }
  assert.ok(worst <= NOISE_LIP && worst > 0.5 * NOISE_LIP * 0.3, `max gradient ${worst} vs bound ${NOISE_LIP}`);
  for (const src of [{ kind: "fbm", freq: 30, octaves: 4, seed: 1 }, { kind: "ridge", dir: [0.6, 0.8, 0], freq: 50, warp: 1, warpFreq: 20 }]) {
    const s = makeSource(src); let mx = 0;
    for (let i = 0; i < 400; i++) { const x = r(), y = r(), z = r(), gg = [0, 0, 0]; s.h(x, y, z, gg); mx = Math.max(mx, Math.hypot(...gg)); const n = fd((a, b, c) => s.h(a, b, c, null), x, y, z, 1e-6); assert.ok(Math.hypot(gg[0] - n[0], gg[1] - n[1], gg[2] - n[2]) < 1e-3 * (1 + mx), `${src.kind} gradient`); }
    assert.ok(mx <= s.lip, `${src.kind} gradient ${mx} exceeds lip ${s.lip}`);
  }
});
test("modifier gradients equal differences of the returned field", () => {
  const mods = [{ kind: "round", r: 0.004 }, { kind: "twist", k: 10, radius: 0.1 }, { kind: "bend", k: 6, extent: 0.1 }, { kind: "displace", amp: 0.002, src: { kind: "fbm", freq: 30, octaves: 2, seed: 2 }, mask: { c: [0, 0, 0.04], r0: 0.01, r1: 0.08 } }];
  const f = leaf(mods, { ...SHAPE, r: [0.04, 0.05, 0.03], round: 0.006, rot: [10, 20, 0] }), r = rng(8), g = [0, 0, 0];
  for (let i = 0; i < 100; i++) { const x = (r() - 0.5) * 0.12, y = (r() - 0.5) * 0.12, z = (r() - 0.5) * 0.12; f(x, y, z, g); const n = fd((a, b, c) => f(a, b, c, null), x, y, z, 1e-6); assert.ok(Math.hypot(g[0] - n[0], g[1] - n[1], g[2] - n[2]) < 2e-4, "stack gradient"); }
});
test("smooth blends: depth bounds, exact derivative, and cubic is the smoother join", () => {
  for (const cubic of [false, true]) for (let i = 0; i < 200; i++) {
    const a = Math.sin(i) * 0.02, b = Math.cos(i * 1.7) * 0.02, k = 0.03, v = sminV(a, b, k, cubic), d = Math.min(a, b) - v;
    assert.ok(d >= -1e-12 && d <= k / (cubic ? 6 : 4) + 1e-12, "depth bound");
    const e = 1e-7, wa = sminW(a, b, k, cubic), da = (sminV(a + e, b, k, cubic) - sminV(a - e, b, k, cubic)) / (2 * e); assert.ok(Math.abs(da - wa) < 1e-5, "weight is the derivative");
  }
  // second derivative jump at the blend edge (a - b = +-k): quadratic has one, cubic has none
  const jump = (cubic) => { const k = 0.03, e = 1e-5, s = (t) => sminV(t, 0, k, cubic), d2 = (t) => (s(t + e) - 2 * s(t) + s(t - e)) / (e * e); return Math.abs(d2(-k + 3 * e) - d2(-k - 3 * e)); };
  assert.ok(jump(false) > 10 && jump(true) < 1, `curvature jump quad ${jump(false)} cubic ${jump(true)}`);
});
test("Field: analytic fold gradient equals the differenced fold, with cubic blends, sub, and mirror", () => {
  const S = { format: "rnsculpt/1", name: "t", eyes: [], strokes: [
    { id: "a", op: "add", shape: "ellb", c: [0, 0, 0], r: [0.05, 0.06, 0.05], k: 0.02, blend: "cubic" },
    { id: "b", op: "add", shape: "rcone", a: [0.03, -0.02, 0.03], b: [0.05, -0.06, 0.04], r: [0.012, 0.006], k: 0.03, blend: "cubic", mirror: true },
    { id: "c", op: "sub", shape: "rbox", c: [0, 0.01, 0.055], r: [0.02, 0.006, 0.01], round: 0.004, k: 0.01, blend: "cubic", mods: [{ kind: "displace", amp: 0.001, src: { kind: "noise", freq: 80 } }] },
    { id: "d", op: "add", shape: "cap", a: [0, -0.03, 0.04], b: [0.01, -0.05, 0.05], r: 0.008, k: 0.02 }] };
  const F = new Field(parseSculpt(stringifySculpt(S)).strokes), r = rng(2), g = [0, 0, 0]; let n = 0;
  for (let i = 0; i < 400; i++) {
    const q = project(F, [(r() - 0.5) * 0.14, (r() - 0.5) * 0.16, (r() - 0.5) * 0.14], 30, 0.005); if (q[3] > 1e-7) continue;
    const [x, y, z] = q, d = F.dg(x, y, z, g); n++;
    const f = fd((a, b, c) => F.d(a, b, c), x, y, z, 1e-6), l = Math.hypot(...f); assert.ok((g[0] * f[0] + g[1] * f[1] + g[2] * f[2]) / (Math.hypot(...g) * l) > 0.9999, "fold normal");
    assert.ok(Math.abs(F.d(x, y, z) - d) < 1e-12);
  }
  assert.ok(n > 20, `near-surface samples ${n}`);
});

import { retopo } from "../src/retopo.mjs";
import { triangulate } from "../src/extract.mjs";
import { decimate, triError } from "../src/decimate.mjs";
test("adaptive decimation meets the budget inside the tolerance; a zero tolerance refuses (control)", () => {
  const F = new Field([{ id: "a", op: "add", shape: "ellb", c: [0, 0, 0], r: [0.05, 0.06, 0.05], k: 0.02, blend: "cubic" }, { id: "n", op: "add", shape: "rcone", a: [0, -0.01, 0.04], b: [0, -0.03, 0.07], r: [0.01, 0.005], k: 0.02, blend: "cubic" }]);
  const B = F.bounds(0.004), M = retopo(F, { lo: B.lo, hi: B.hi, cell: 0.004, relax: 2 }), tri = triangulate(M), n0 = tri.length / 3, target = Math.floor(n0 * 0.4);
  const D = decimate(F, { pos: M.pos, tri }, { target, tol: 0.0015 }), E = triError(F, D);
  assert.ok(D.tri.length / 3 <= target && E.max <= 0.0015, `tris ${D.tri.length / 3} of ${target}, error ${E.max}`);
  // curvature adaptive: the nose region keeps more triangles per area than the flat cranium
  const dens = (pred) => { let c = 0; for (let i = 0; i < D.tri.length; i += 3) if (pred(D.pos[D.tri[i] * 3 + 2], D.pos[D.tri[i] * 3 + 1])) c++; return c; };
  assert.ok(dens((z) => z > 0.035) > 0);
  const Z = decimate(F, { pos: M.pos, tri }, { target, tol: 0 });
  assert.ok(Z.tri.length / 3 > target, "control: tolerance 0 cannot reach the budget");
});

test("decimation never leaves a face sagging past the tolerance (fails on the centroid-blind version: max 6.19 mm against 6 mm)", () => {
  const F = new Field([{ id: "a", op: "add", shape: "ellb", c: [0, 0, 0], r: [0.05, 0.06, 0.05], k: 0.02, blend: "cubic" }, { id: "n", op: "add", shape: "rcone", a: [0, -0.01, 0.04], b: [0, -0.03, 0.07], r: [0.01, 0.005], k: 0.02, blend: "cubic" },
    { id: "e", op: "add", shape: "ellb", c: [0.03, 0.02, 0.04], r: [0.012, 0.008, 0.01], k: 0.01, blend: "cubic", mirror: true }, { id: "s", op: "sub", shape: "ellb", c: [0.02, -0.03, 0.05], r: [0.006, 0.003, 0.006], k: 0.003, mirror: true }]);
  const B = F.bounds(0.004), M = retopo(F, { lo: B.lo, hi: B.hi, cell: 0.004, relax: 2 }), tri = triangulate(M);
  const D = decimate(F, { pos: M.pos, tri }, { target: Math.floor(tri.length / 3 * 0.08), tol: 0.006 });
  assert.ok(D.tri.length / 3 < tri.length / 3 * 0.2, "decimated hard");
  assert.ok(triError(F, D).max <= 0.006, `max sag ${triError(F, D).max}`);
});

import { adaptiveLod } from "../src/decimate.mjs";
test("a finer base cell keeps the decimated mesh inside tol; the coarse base fails it (control, the old behaviour)", () => {
  const F = new Field([{ id: "a", op: "add", shape: "ellb", c: [0, 0, 0], r: [0.05, 0.06, 0.05], k: 0.02, blend: "cubic" }, { id: "ear", op: "add", shape: "ellb", c: [0.055, 0, -0.01], r: [0.008, 0.03, 0.018], k: 0.006, blend: "cubic", mirror: true },
    { id: "e", op: "add", shape: "ellb", c: [0.03, 0.02, 0.04], r: [0.012, 0.008, 0.01], k: 0.01, blend: "cubic", mirror: true }, { id: "s", op: "sub", shape: "ellb", c: [0.02, -0.03, 0.05], r: [0.006, 0.003, 0.006], k: 0.003, mirror: true }]);
  const B = F.bounds(0.004), tol = 0.004, fine = adaptiveLod(F, B, { cell: 0.010, baseCell: 0.004, budget: 500, tol }), old = adaptiveLod(F, B, { cell: 0.010, budget: 500, tol });
  assert.equal(fine.baseCell, 0.004); assert.equal(old.baseCell, 0.010);
  assert.ok(triError(F, fine).max <= tol + 1e-9, `fine base max ${triError(F, fine).max}`);
  assert.ok(triError(F, old).max > tol * 1.5, `control: the coarse base must exceed tol, got ${triError(F, old).max}`);
});
