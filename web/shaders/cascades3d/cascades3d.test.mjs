// node --test web/shaders/cascades3d/cascades3d.test.mjs
import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScene3d, bounds, furnaceExact, writeScene, parseScene } from "./scene3d.mjs";
import { buildBvh, prepare, closestHit, triHit } from "./bvh.mjs";
import { omegaTable, texelDir, texelOf, planCascades3d, cosineDir, jitter, dot } from "./math3d.mjs";
import { runCascades3d, noGiBaseline } from "./run3d.mjs";

// The tests written before the sixth round's defaults keep the old ones (sv, ft, cg, tf and jit off).
const plan3 = (b, o) => planCascades3d(b, { sv: 0, ft: 0, cg: 0, tf: 0, ...o });

test("cube texels: exact solid angles sum to 4 pi, directions round-trip, children nest", () => {
  for (const c of [1, 2, 4, 8]) {
    const om = omegaTable(c);
    assert.ok(Math.abs(6 * om.reduce((a, b) => a + b, 0) - 4 * Math.PI) < 1e-12);
    for (let k = 0; k < 6 * c * c; k++) {
      assert.equal(texelOf(texelDir(k, c), c), k);
      const f = Math.floor(k / (c * c)), r = k - f * c * c, j = Math.floor(r / c), i = r - j * c;
      for (const [a, b] of [[0, 0], [1, 1]]) assert.equal(texelOf(texelDir(f * 4 * c * c + (2 * j + b) * 2 * c + 2 * i + a, 2 * c), c), k);
    }
  }
});

test("cosine directions stay in the hemisphere and average to the normal's projected disk", () => {
  const n = [0.3, 0.8, -0.52].map((v, _, a) => v / Math.hypot(...a)), m = 16;
  let s = 0;
  for (let j = 0; j < m; j++) for (let i = 0; i < m; i++) { const [a, b] = jitter(5, j * m + i), d = cosineDir(n, i, j, m, a, b); assert.ok(dot(d, n) >= -1e-12); s += dot(d, n); }
  assert.ok(Math.abs(s / (m * m) - 2 / 3) < 0.01); // E[cos] under the cosine density is 2/3
});

test("the BVH finds the same closest hit as testing every triangle", () => {
  const sc = buildScene3d("leak-door"), S = prepare(sc, buildBvh(sc));
  let seed = 9;
  const rnd = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
  for (let r = 0; r < 300; r++) {
    const o = [rnd() * 10, rnd() * 3, rnd() * 5], d = [rnd() - 0.5, rnd() - 0.5, rnd() - 0.5], m = Math.hypot(...d), dd = d.map((v) => v / m);
    const h = closestHit(S, o, dd, 0, 1e30);
    let best = Infinity;
    for (let s = 0; s < S.alb.length / 3; s++) { const t = triHit(Array.from(S.tris.subarray(9 * s, 9 * s + 9)), o, dd, 0, 1e30); if (t) best = Math.min(best, t.t); }
    assert.equal(h ? h.t : Infinity, best);
  }
});

test("scene files round-trip", () => {
  const sc = buildScene3d("furnace"), back = parseScene(writeScene(sc));
  assert.deepEqual(Array.from(back.tris), Array.from(sc.tris));
  assert.deepEqual(Array.from(back.alb), Array.from(sc.alb));
  assert.ok(Math.abs(back.camera.fovy - sc.camera.fovy) < 1e-6);
});

test("planted white furnace: started at the fixed point, every pixel stays at 1 / (1 - rho)", () => {
  const sc = buildScene3d("furnace"), S = prepare(sc, buildBvh(sc)), want = furnaceExact(sc.alb[0]);
  for (const opts of [{ fix: "none", stride: 0 }, { fix: "probe", stride: 0 }, { fix: "probe", stride: 3, m: 4 }, { fix: "full", stride: 2, m: 3 },
    { fix: "probe", stride: 2, m: 3, tf: 3, sf: 2, sl: 0.5 }, { fix: "probe", stride: 3, m: 2, tf: 4, move: { d: [0.2, 0, 0.1], k: 2 } },
    { fix: "probe", stride: 2, m: 3, c1: 4 }, { fix: "full", stride: 0, c1: 4 }, { fix: "probe", stride: 2, m: 3, mw: "vis" },
    { fix: "probe", stride: 2, m: 3, sv: 1 }, { fix: "probe", stride: 3, m: 2, tf: 3, jit: 2, sv: 1 },
    { fix: "probe", stride: 2, m: 3, sv: 2, ft: 1, go: [-1 / 3, -3 / 5, -3 / 4] }, { fix: "probe", stride: 3, m: 2, tf: 3, jit: 2, sv: 2, ft: 1 },
    { fix: "probe", stride: 3, m: 2, tf: 3, jit: 2, sv: 2, ft: 1, sd: 1 }, { fix: "probe", stride: 2, m: 3, sv: 2, sd: 1, mw: "vis" }]) {
    const plan = plan3(bounds(sc), { h0: 0.5, c0: 1, r0: 2, f: 2, bounces: 3, ...opts }), out = runCascades3d(sc, S, plan, 12, 8, { cacheInit: sc.sky });
    let worst = 0;
    for (let p = 0; p < 96; p++) if (out.hit[p]) worst = Math.max(worst, Math.abs(out.radiance[3 * p] - want) / want);
    assert.ok(worst < 1e-6, `${JSON.stringify(opts)}: worst relative error ${worst}`); // the f32 albedo against the f64 fixed point: 1e-8
  }
});

test("closed room: with the probe fix, no light reaches room B in the CPU reference", () => {
  const sc = buildScene3d("leak-closed-B"), S = prepare(sc, buildBvh(sc));
  const plan = plan3(bounds(sc), { h0: 0.5, c0: 1, r0: 2, f: 2, bounces: 2, fix: "probe", stride: 4, m: 3 });
  const out = runCascades3d(sc, S, plan, 16, 10), base = noGiBaseline(sc, S, 16, 10, 4);
  let lum = 0, b = 0;
  for (let i = 0; i < out.radiance.length; i++) { lum += out.radiance[i]; b += base.radiance[i]; }
  assert.equal(b, 0);
  assert.ok(lum / 480 < 1e-3, `mean ${lum / 480}`);
});

test("denoise: the probes move through every cell position, and history accumulates on a still camera", async () => {
  const { probeOffsets, frameSeed } = await import("./denoise3d.mjs");
  const plan = { stride: 4, tf: 16 }, seen = new Set();
  for (let f = 0; f < 16; f++) { const [x, y] = probeOffsets(plan, f); assert.ok(x >= 0 && x < 4 && y >= 0 && y < 4); seen.add(x + 4 * y); }
  assert.ok(seen.size >= 12, `cell positions ${seen.size}`);
  assert.deepEqual(probeOffsets({ stride: 8, tf: 0 }, 5), [4, 4]);
  assert.equal(frameSeed({ tf: 0 }, 3), 0);
  // Variance of a pixel's irradiance across seeds falls with the history length.
  const sc = buildScene3d("leak-door-A"), S = prepare(sc, buildBvh(sc)), W = 12, H = 8, spread = (tf) => {
    const vals = [];
    for (const d of [0.0, 1e-9]) { // two plans that differ only in their frame count reach different seeds
      const plan = plan3(bounds(sc), { h0: 0.75, c0: 2, r0: 1.5, f: 4, fix: "probe", stride: 2, m: 2, bounces: 2, cd: 2, tf: tf + (d ? 1 : 0), tmax: 64 });
      vals.push(runCascades3d(sc, S, plan, W, H).radiance);
    }
    let s = 0, n = 0;
    for (let i = 0; i < vals[0].length; i++) { s += (vals[0][i] - vals[1][i]) ** 2; n += vals[0][i]; }
    return Math.sqrt(s) / n;
  };
  assert.ok(spread(12) < spread(1), `${spread(12)} against ${spread(1)}`);
});

test("world options: c1 children nest, jit phases stay inside one span, levels map onto level 0", async () => {
  const { phaseOffset, levelZeroIndex } = await import("./world.mjs");
  for (let k = 0; k < 96; k++) assert.equal(texelOf(texelDir(k, 4), 1), Math.floor(k / 16)); // a c-4 texel lies inside its c-1 parent
  const sc = buildScene3d("leak-closed-B");
  const plan = plan3(bounds(sc), { h0: 0.5, c0: 1, stride: 4, m: 3, tf: 2, jit: 4 }), seen = new Set();
  for (let p = 0; p < 4; p++) { const o = phaseOffset(plan, p); assert.ok(o.every((v) => v >= 0 && v < plan.hj)); seen.add(o.join()); }
  assert.equal(seen.size, 4);
  const L1 = plan.levels[1], n0 = plan.levels[0].n;
  for (let i = 0; i < L1.probes; i++) {
    const q0 = levelZeroIndex(plan, 1, i), x = i % L1.n[0];
    if (q0 >= 0) assert.equal(q0 % n0[0], 2 * x); else assert.ok(2 * x >= n0[0] || 2 * (Math.floor(i / L1.n[0]) % L1.n[1]) >= n0[1] || 2 * Math.floor(i / (L1.n[0] * L1.n[1])) >= n0[2]);
  }
  assert.throws(() => planCascades3d(bounds(sc), { c1: 3 }));
  assert.throws(() => planCascades3d(bounds(sc), { jit: 4, tf: 0 }));
});

test("start visibility: the closed room stays dark at every grid placement, far darker than without it", () => {
  const sc = buildScene3d("leak-closed-B"), S = prepare(sc, buildBvh(sc)), room = (o) => {
    const plan = plan3(bounds(sc), { h0: 0.5, c0: 1, r0: 2, f: 2, bounces: 1, fix: "probe", stride: 4, m: 3, ...o }), out = runCascades3d(sc, S, plan, 16, 10);
    let lum = 0;
    for (let i = 0; i < out.radiance.length; i++) lum += out.radiance[i];
    return lum / 480;
  };
  let on = 0, off = 0;
  for (const go of [[-2 / 3, -4 / 5, -1 / 2], [-1 / 3, -3 / 5, -3 / 4], [-8 / 9, -2 / 5, -1 / 4], [-5 / 9, -1 / 5, -7 / 8]]) { on += room({ go, sv: 1 }); off += room({ go }); }
  assert.ok(on < 0.2 * off, `with ${on} without ${off}`);
});

test("own-ray start visibility with the traced fallback: the closed room stays dark at every placement; sub-texel sv does not", () => {
  const sc = buildScene3d("leak-closed-B"), S = prepare(sc, buildBvh(sc)), room = (o) => {
    const plan = plan3(bounds(sc), { h0: 0.5, c0: 1, r0: 2, f: 2, bounces: 2, fix: "probe", stride: 4, m: 3, ...o }), out = runCascades3d(sc, S, plan, 16, 10);
    let lum = 0;
    for (let i = 0; i < out.radiance.length; i++) lum += out.radiance[i];
    return lum / 480;
  };
  const lit = 0.055; // room A's view under the same plan (build/diag, 16 x 10)
  for (const go of [null, [-1 / 3, -3 / 5, -3 / 4], [-2 / 9, -0.96, -3 / 8], [-1 / 9, -0.36, -15 / 16]]) {
    const v = room({ go, sv: 2, ft: 1 });
    assert.ok(v / lit < 1e-4, `go ${go}: ${v / lit}`);
  }
  // The control: the same fallback with round 4's sub-texel start visibility leaks at the second placement.
  assert.ok(room({ go: [-1 / 3, -3 / 5, -3 / 4], sv: 1, ft: 1 }) / lit > 5e-3);
});

test("sd keeps the white furnace exact at a moved grid; cg's no-light fallback does not, and cg 2 is no worse than cg 1", () => {
  const sc = buildScene3d("furnace"), S = prepare(sc, buildBvh(sc)), want = furnaceExact(sc.alb[0]);
  const run = (o) => {
    const plan = plan3(bounds(sc), { h0: 0.5, c0: 1, r0: 2, f: 2, bounces: 3, fix: "probe", stride: 3, m: 2, sv: 2, ft: 1, go: [-1 / 3, -3 / 5, -3 / 4], ...o });
    const out = runCascades3d(sc, S, plan, 12, 8, { cacheInit: sc.sky });
    let worst = 0;
    for (let p = 0; p < 96; p++) if (out.hit[p]) worst = Math.max(worst, Math.abs(out.radiance[3 * p] - want) / want);
    return worst;
  };
  assert.ok(run({ sd: 1 }) < 1e-6, `sd ${run({ sd: 1 })}`);
  // cg returns no light where no probe is shown to see the surface: 10 of 96 pixels, up to 1% here (2026-11-07).
  const c1 = run({ sd: 1, cg: 1 }), c2 = run({ sd: 1, cg: 2 });
  assert.ok(c2 <= c1 + 1e-12 && c2 < 0.02, `cg 1 ${c1}, cg 2 ${c2}`);
});

test("cg 2 and sd with sv 2 and ft: the closed room stays dark at every placement", () => {
  const sc = buildScene3d("leak-closed-B"), S = prepare(sc, buildBvh(sc)), lit = 0.055;
  for (const go of [null, [-1 / 3, -3 / 5, -3 / 4], [-2 / 9, -0.96, -3 / 8]]) {
    const plan = plan3(bounds(sc), { h0: 0.5, c0: 1, r0: 2, f: 2, bounces: 2, fix: "probe", stride: 4, m: 3, sv: 2, ft: 1, cg: 2, sd: 1, go });
    const out = runCascades3d(sc, S, plan, 16, 10);
    let lum = 0;
    for (let i = 0; i < out.radiance.length; i++) lum += out.radiance[i];
    assert.ok(lum / 480 / lit < 1e-4, `go ${go}: ${lum / 480 / lit}`);
  }
  assert.throws(() => planCascades3d(bounds(sc), { sd: 1, sv: 0 }));
  assert.throws(() => planCascades3d(bounds(sc), { cg: 4 }));
});

test("defaults: sv 2, ft 1, cg 2 and, with screen probes, tf 32 and jit 8; tf 0 or stride 0 runs no jitter", () => {
  const b = bounds(buildScene3d("leak-closed-B")), d = planCascades3d(b, { stride: 8, m: 6 });
  assert.deepEqual([d.sv, d.ft, d.cg, d.tf, d.jit, d.sd], [2, 1, 2, 32, 8, 0]);
  assert.deepEqual([planCascades3d(b, { tf: 0 }).jit, planCascades3d(b, { stride: 0 }).jit, planCascades3d(b, { stride: 0 }).tf], [0, 0, 0]);
});
