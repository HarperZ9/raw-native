// node --test web/shaders/hatch.test.mjs
// Curvature from the SDF Hessian, on the CPU reference. Thresholds from
// evidence/shaders-hatch-parity-bounds.json ("tests"), committed before the first run.
// GPU parity: tests/web/shaders_parity.py --shader hatch.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { curvature } from "./hatch/hatch.mjs";
import { STILL, sdSphere, sdCapsule, sdTorus, sdPlane } from "./fixtures/stilllife.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-hatch-parity-bounds.json", import.meta.url), "utf8")).tests;
// Points on a sphere of radius R around c, from a fixed spiral.
const spiral = (c, R, n) => Array.from({ length: n }, (_, i) => { const z = 1 - (2 * (i + 0.5)) / n, r = Math.sqrt(1 - z * z), a = i * 2.399963; return [c[0] + R * r * Math.cos(a), c[1] + R * z, c[2] + R * r * Math.sin(a)]; });

test("sphere: both curvatures are 1/R, and every point is an umbilic", () => {
  const s = STILL.sphere, pts = spiral(s.c, s.r, 400); let worst = 0, umb = 0;
  for (const p of pts) { const c = curvature(sdSphere, p); worst = Math.max(worst, Math.abs(c.kmax * s.r - 1), Math.abs(c.kmin * s.r - 1)); umb += c.umbilic ? 1 : 0; }
  assert.ok(worst <= B.sphere_curvature_rel_error_max, `worst relative error ${worst}`);
  assert.ok(umb / pts.length >= B.sphere_umbilic_fraction_min, `umbilic fraction ${umb / pts.length}`);
});

test("cylinder (the capsule's side): 1/r around, 0 along, and the strong direction runs around", () => {
  const c = STILL.capsule, r = c.r; let worstK = 0, worstZ = 0, worstDir = 0;
  for (let i = 0; i < 64; i++) {
    const a = (i / 64) * 2 * Math.PI, y = c.a[1] + 0.3 + (i % 8) * 0.1, p = [c.a[0] + r * Math.cos(a), y, c.a[2] + r * Math.sin(a)], k = curvature(sdCapsule, p);
    worstK = Math.max(worstK, Math.abs(k.kmax * r - 1)); worstZ = Math.max(worstZ, Math.abs(k.kmin) * r); worstDir = Math.max(worstDir, Math.abs(k.dir[1]));
    assert.ok(!k.umbilic, "the cylinder must not fall back");
  }
  assert.ok(worstK <= 0.02, `max curvature error ${worstK}`);
  assert.ok(worstZ <= 0.02, `min curvature ${worstZ} / r`);
  assert.ok(worstDir <= B.cylinder_direction_axis_dot_max, `axis component of the strong direction ${worstDir}`);
});

test("plane: no curvature, so the fallback direction is used", () => {
  let fb = 0, n = 0; for (let i = 0; i < 200; i++) { const p = [-2 + (i % 20) * 0.2, 0, -2 + Math.floor(i / 20) * 0.2]; fb += curvature(sdPlane, p).umbilic ? 1 : 0; n++; }
  assert.ok(fb / n >= B.plane_fallback_fraction_min, `fallback fraction ${fb / n}`);
});

test("torus: the inner equator is a saddle (curvatures of opposite sign)", () => {
  const t = STILL.torus;
  for (let i = 0; i < 32; i++) {
    const a = (i / 32) * 2 * Math.PI, rr = t.R - t.r, p = [t.c[0] + rr * Math.cos(a), t.c[1], t.c[2] + rr * Math.sin(a)], k = curvature(sdTorus, p);
    assert.ok(k.kmax * k.kmin < 0, `at ${a}: ${k.kmax}, ${k.kmin}`);
    assert.ok(Math.abs(k.kmax * t.r - 1) < 0.05 && Math.abs(k.kmin * (t.R - t.r) + 1) < 0.05, `values ${k.kmax}, ${k.kmin}`);
  }
});
