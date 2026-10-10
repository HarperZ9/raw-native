// node --test web/shaders/adjacency.test.mjs
// Development adjacency on its CPU reference. Thresholds come from
// evidence/shaders-adjacency-parity-bounds.json ("tests"), committed before the first run.
// GPU parity: tests/web/shaders_parity.py --shader adjacency.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolveAdjacency, develop } from "./adjacency/adjacency.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-adjacency-parity-bounds.json", import.meta.url), "utf8")).tests;
const plate = (w, h, f) => { const d = new Float32Array(w * h * 4); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const v = f(x, y); d.set([v, v, v, 1], (y * w + x) * 4); } return { width: w, height: h, data: d }; };
// Plate values chosen before the first run: an edge from 0.12 to 1.5 (developable 0.34 and 0.97).
const DARK = 0.12, BRIGHT = 1.5, PITCH = 37.5;      // 150 um developer length = 4 pixels
const Drow = (r, w, y) => Array.from({ length: w }, (_, x) => r.D[(y * w + x) * 3 + 1]);

function edgeProfile(over = {}, W = 256) {
  const plan = resolveAdjacency("eberhard", { pitchUm: PITCH, ...over }, { w: W, h: 4 });
  return Drow(develop(plan, plate(W, 4, (x) => (x < W / 2 ? DARK : BRIGHT))), W, 2);
}

test("a flat field stays flat", () => {
  const plan = resolveAdjacency("exhausted", { pitchUm: PITCH }, { w: 40, h: 30 }), r = develop(plan, plate(40, 30, () => 0.7));
  let lo = Infinity, hi = -Infinity; for (const v of r.D) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
  assert.ok(hi - lo <= B.flat_field_max_dev, `spread ${hi - lo}`);
});

test("an edge overshoots on the dense side and undershoots on the thin side", () => {
  const W = 256, d = edgeProfile({}, W), farDark = d[8], farBright = d[W - 9], step = farBright - farDark;
  const peak = Math.max(...d.slice(W / 2, W / 2 + 40)), dip = Math.min(...d.slice(W / 2 - 40, W / 2));
  const over = (peak - farBright) / step, under = (farDark - dip) / step;
  assert.ok(over >= B.edge_overshoot_min_fraction_of_step, `overshoot ${over} of the step`);
  assert.ok(under >= B.edge_undershoot_min_fraction_of_step, `undershoot ${under} of the step`);
});

test("with diffusion off there is no overshoot (control)", () => {
  const W = 256, d = edgeProfile({ devUm: 0, bromUm: 0 }, W), farBright = d[W - 9];
  const peak = Math.max(...d.slice(W / 2, W / 2 + 40));
  assert.ok(peak - farBright <= B.no_diffusion_max_overshoot, `overshoot ${peak - farBright}`);
});

test("the overshoot width scales as the square root of the diffusivity", () => {
  const width = (k) => {
    const W = 512, d = edgeProfile({ devUm: 150 * k, bromUm: 100 * k }, W), far = d[W - 9];
    let ip = W / 2; for (let x = W / 2; x < W / 2 + 120; x++) if (d[x] > d[ip]) ip = x;
    const target = far + (d[ip] - far) / Math.E;
    for (let x = ip; x < W - 1; x++) if (d[x + 1] <= target) return x + (d[x] - target) / (d[x] - d[x + 1]) - (W / 2 - 0.5);
    return NaN;
  };
  const r = width(2) / width(1), [lo, hi] = B.width_ratio_for_4x_diffusivity;
  assert.ok(r >= lo && r <= hi, `width ratio ${r} for 4x diffusivity`);
});

test("developer and bromide are conserved when the bath is cut off", () => {
  for (const scale of [1, 2]) {
    const W = 48, H = 32, plan = resolveAdjacency("eberhard", { pitchUm: PITCH, r: 0, rB: 0, scale }, { w: W, h: H });
    const r = develop(plan, plate(W, H, (x, y) => ((x >> 3) + (y >> 3)) & 1 ? BRIGHT : DARK));
    let sc = 0, sb = 0, sd = 0; for (let i = 0; i < plan.cw * plan.ch; i++) { sc += r.F[i * 2]; sb += r.F[i * 2 + 1]; } for (const v of r.D) sd += v;
    const cells = plan.cw * plan.ch, c0 = cells * scale * scale;
    const errC = Math.abs(sc * scale * scale + plan.p.eta * sd - c0) / c0, errB = Math.abs(sb * scale * scale - plan.p.etaB * sd) / c0;
    assert.ok(errC <= B.conservation_max_rel_error && errB <= B.conservation_max_rel_error, `scale ${scale}: developer ${errC}, bromide ${errB}`);
  }
});

test("a small dense spot develops more than a large one at the same exposure", () => {
  const W = 128, H = 128, plan = resolveAdjacency("eberhard", { pitchUm: PITCH }, { w: W, h: H });
  const centre = (rad, cx, cy) => { const r = develop(plan, plate(W, H, (x, y) => (Math.hypot(x - cx, y - cy) <= rad ? BRIGHT : DARK))); return r.D[(cy * W + cx) * 3 + 1]; };
  const ratio = centre(2, 64, 64) / centre(40, 64, 64);
  assert.ok(ratio >= B.small_area_min_ratio, `small over large ${ratio}`);
});
