// Radiance cascades (CPU, float64): the tracer, the planted analytic check, convergence, the
// bent-ray merge, and controls that show each measure can fail.
import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScene, diskFluence, OPAQUE } from "./scenes.mjs";
import { buildMips, marchGrid, traceInterval, scatterGlass, hash32, MIP_LEVELS } from "./trace.mjs";
import { planCascades, runCascades, referenceFluence, atGrid, refGrid } from "./cascades.mjs";
import { relRmse, tileBootstrap, bandErrors } from "./metrics.mjs";

function analyticDisk(sc, stride) {
  const c = sc.circles[0], g = refGrid(sc.W, sc.H, stride), A = new Float64Array(g.w * g.h * 3);
  for (let j = 0; j < g.h; j++) for (let i = 0; i < g.w; i++) {
    const f = diskFluence(1, c.r, Math.hypot(stride * i + g.off + 0.5 - c.cx, stride * j + g.off + 0.5 - c.cy));
    A.fill(f, 3 * (j * g.w + i), 3 * (j * g.w + i) + 3);
  }
  return A;
}

test("the march returns the exact chord through a medium, with and without empty-space skipping", () => {
  const sc = { W: 64, H: 32, grid: new Float32Array(64 * 32 * 4), circles: [] };
  for (let y = 10; y < 20; y++) for (let x = 30; x < 37; x++) sc.grid.set([1, 0.5, 0.25, 0.25], 4 * (y * 64 + x)); // values exact in f32
  const mips = buildMips(sc.grid, 64, 32), full = mips.map((m) => ({ ...m, occ: new Uint8Array(m.occ.length).fill(1) }));
  const d = [Math.cos(0.3), Math.sin(0.3)], P = [2.25, 7.5];
  const a = { L: [0, 0, 0], T: 1, out: false }, b = { L: [0, 0, 0], T: 1, out: false };
  marchGrid(sc, mips, P, d, 100, a); marchGrid(sc, full, P, d, 100, b);
  // Chord through the box x in [30, 37] along d, starting where y stays inside [10, 20).
  const chord = (37 - 30) / d[0];
  assert.ok(Math.abs(a.T - Math.exp(-0.25 * chord)) < 1e-12, `T ${a.T}`);
  assert.ok(Math.abs(a.L[0] - (1 - Math.exp(-0.25 * chord))) < 1e-12);
  assert.ok(Math.abs(a.T - b.T) < 1e-14 && Math.abs(a.L[1] - b.L[1]) < 1e-14);
  assert.ok(a.out, "the ray leaves the frame");
  assert.equal(mips.length, MIP_LEVELS);
});

test("planted check: the brute force matches the closed-form disk, and the metric reads a planted 1% error", () => {
  const sc = buildScene("disk", 64, 36), A = analyticDisk(sc, 2), R = referenceFluence(sc, { rays: 1024, stride: 2, seed: 3 });
  assert.ok(relRmse(R, A) < 0.03, `reference against the closed form ${relRmse(R, A)}`);
  const m = A.reduce((x, y) => x + y, 0) / A.length;
  assert.ok(Math.abs(relRmse(A.map((v) => v + 0.01 * m), A) - 0.01) < 1e-12, "an offset of 1% of the mean reads 0.01");
  const iv = tileBootstrap(R, A, 32, 18, { tile: 8, reps: 200 });
  assert.ok(iv.lo <= relRmse(R, A) && relRmse(R, A) <= iv.hi);
});

test("energy: a frame filled with one opaque emitter gives 2 pi L everywhere, cascades and brute force", () => {
  const sc = { W: 24, H: 16, grid: new Float32Array(24 * 16 * 4), circles: [] };
  for (let i = 0; i < 24 * 16; i++) sc.grid.set([0.5, 1, 2, OPAQUE], 4 * i);
  const F = runCascades(sc, planCascades(24, 16, { branch: 2, n0: 4, r0: 1 })), R = referenceFluence(sc, { rays: 64, stride: 1 });
  for (let i = 0; i < F.length; i += 3) {
    assert.ok(Math.abs(F[i + 2] - 4 * Math.PI) < 1e-9 && Math.abs(R[i + 2] - 4 * Math.PI) < 1e-9);
  }
});

test("cascades converge toward the closed-form disk as angular and interval resolution grow", () => {
  const sc = buildScene("disk", 96, 54), A = analyticDisk(sc, 1);
  const e = [[4, 1], [16, 4], [32, 8]].map(([n0, r0]) => relRmse(runCascades(sc, planCascades(96, 54, { branch: 2, n0, r0 })), A));
  assert.ok(e[0] > e[1] && e[1] > e[2], e.join(" "));
  assert.ok(e[2] < 0.12, `finest ${e[2]}`);
  // Control: a cascade set too coarse to meet the penumbra condition is clearly worse.
  assert.ok(e[0] > 2 * e[2]);
});

test("bent-ray merge: a glass circle of index 1 changes nothing", () => {
  const W = 64, H = 36, a = buildScene("room", W, H), b = buildScene("room", W, H);
  b.circles.push({ kind: "glass", cx: 30, cy: 18, r: 9, L: [0, 0, 0], n: 1 });
  for (const plan of [{ branch: 2, n0: 4, r0: 2 }, { branch: 4, n0: 4, r0: 1 }]) {
    const p = planCascades(W, H, plan), Fa = runCascades(a, p), Fb = runCascades(b, p);
    assert.ok(relRmse(Fb, Fa) < 1e-9, `${JSON.stringify(plan)} ${relRmse(Fb, Fa)}`);
  }
  // Control: index 1.5 does change the image.
  b.circles[b.circles.length - 1].n = 1.5;
  const p = planCascades(W, H, { branch: 2, n0: 4, r0: 2 });
  assert.ok(relRmse(runCascades(b, p), runCascades(a, p)) > 1e-3);
});

test("glass: Snell's law at the circle, total internal reflection, and the reference's Fresnel split", () => {
  const c = { cx: 0, cy: 0, r: 1, n: 1.5 }, th = 0.5, d = [Math.sin(th), -Math.cos(th)], p = [0, 1];
  const t = scatterGlass(c, p, d, "refract", 0);
  assert.ok(Math.abs(Math.sin(th) - 1.5 * t[0]) < 1e-12, "sin i = n sin t");
  const out = scatterGlass(c, [0, 1], [Math.sin(0.9), Math.cos(0.9)], "refract", 0); // inside, past the critical angle
  assert.ok(Math.abs(out[1] + Math.cos(0.9)) < 1e-12, "totally reflected");
  assert.deepEqual(scatterGlass(c, p, [0, -1], "fresnel", 0.039), [0, 1], "u below F = 0.04 reflects");
  assert.ok(scatterGlass(c, p, [0, -1], "fresnel", 0.041)[1] < 0, "u above F refracts");
});

test("an interval skips its prefix: [t0, t1] sees only what lies in it", () => {
  const sc = { W: 40, H: 8, grid: new Float32Array(40 * 8 * 4), circles: [] };
  for (let y = 0; y < 8; y++) sc.grid.set([1, 1, 1, OPAQUE], 4 * (y * 40 + 10));
  for (let y = 0; y < 8; y++) sc.grid.set([3, 3, 3, OPAQUE], 4 * (y * 40 + 30));
  const m = buildMips(sc.grid, 40, 8), o = [0.5, 4.5], d = [1, 0];
  assert.ok(Math.abs(traceInterval(sc, m, o, d, 0, 20).L[0] - 1) < 1e-9);
  assert.ok(Math.abs(traceInterval(sc, m, o, d, 15, 40).L[0] - 3) < 1e-9);
  assert.equal(traceInterval(sc, m, o, d, 15, 25).L[0], 0);
});

test("the error bands and the GPU's hash are fixed", () => {
  const a = new Float64Array([1, 1, 1, 2, 2, 2]), r = new Float64Array([1, 1, 1, 1, 1, 1]);
  const b = bandErrors(a, r, 2, 1, (x) => x, [0, 1, 2]);
  assert.equal(b.bands[0].rmse_over_image_mean, 0); assert.equal(b.bands[1].rmse_over_image_mean, 1);
  assert.deepEqual([hash32(0), hash32(1), hash32(123456789)], [129708002, 2831084092, 4272394698]);
  assert.equal(atGrid(new Float64Array(12), 2, 2, 2).length, 3);
});
