// node --test web/shaders/crowd.test.mjs
// Peripheral crowding on the CPU reference. Thresholds from evidence/shaders-crowd-parity-bounds.json
// ("tests"), committed before the first run. GPU parity: tests/web/shaders_parity.py --shader crowd.
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolveCrowd, runCrowd, rhoAt } from "./crowd/crowd.mjs";
import { displayLinear } from "./lab/sources.mjs";

const B = JSON.parse(readFileSync(new URL("../../evidence/shaders-crowd-parity-bounds.json", import.meta.url), "utf8")).tests;
const W = 200, H = 125, src = displayLinear("street", W, H), size = { w: W, h: H };
const lum = (d, i) => 0.2126 * d[i * 4] + 0.7152 * d[i * 4 + 1] + 0.0722 * d[i * 4 + 2];
function boxMean(d, x, y, r) { let s = 0, n = 0; for (let yy = Math.max(0, y - r); yy <= Math.min(H - 1, y + r); yy++) for (let xx = Math.max(0, x - r); xx <= Math.min(W - 1, x + r); xx++) { s += lum(d, yy * W + xx); n++; } return s / n; }
function boxStd(d, x, y, r) { const m = boxMean(d, x, y, r); let s = 0, n = 0; for (let yy = Math.max(0, y - r); yy <= Math.min(H - 1, y + r); yy++) for (let xx = Math.max(0, x - r); xx <= Math.min(W - 1, x + r); xx++) { s += (lum(d, yy * W + xx) - m) ** 2; n++; } return Math.sqrt(s / n); }
const plan = resolveCrowd("thriller", {}, size), out = runCrowd(plan, src, 0).data;
const periphery = []; for (let y = 3; y < H; y += 6) for (let x = 3; x < W; x += 6) { const r = rhoAt(plan, x, y); if (r >= 3) periphery.push([x, y, Math.round(r)]); }

test("inside the fovea the picture is untouched", () => {
  let dev = 0, n = 0;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (rhoAt(plan, x, y) < 0.5) { n++; for (let c = 0; c < 3; c++) dev = Math.max(dev, Math.abs(out[(y * W + x) * 4 + c] - src.data[(y * W + x) * 4 + c])); }
  assert.ok(n > 1000, `fovea has ${n} pixels`);
  assert.ok(dev <= B.inside_fovea_max_dev, `max deviation ${dev}`);
});

test("the local mean stays, at the pooling scale", () => {
  let worst = 0; for (const [x, y, r] of periphery) worst = Math.max(worst, Math.abs(boxMean(out, x, y, r) - boxMean(src.data, x, y, r)));
  assert.ok(periphery.length > 100, `${periphery.length} periphery samples`);
  assert.ok(worst <= B.local_mean_max_abs_dev, `worst local mean change ${worst}`);
});

test("the local contrast stays, on average over the periphery", () => {
  let so = 0, si = 0; for (const [x, y, r] of periphery) { so += boxStd(out, x, y, r); si += boxStd(src.data, x, y, r); }
  const ratio = so / si, [lo, hi] = B.local_std_ratio_range;
  assert.ok(ratio >= lo && ratio <= hi, `std ratio ${ratio}`);
});

test("peripheral detail is rearranged; with no pooling it is not (control)", () => {
  const corr = (o, pl) => {
    let sxy = 0, sxx = 0, syy = 0;
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { if (rhoAt(plan, x, y) < 3) continue; const i = y * W + x, a = lum(src.data, i) - boxMean(src.data, x, y, 3), b = lum(o, i) - boxMean(o, x, y, 3); sxy += a * b; sxx += a * a; syy += b * b; }
    return sxy / Math.sqrt(sxx * syy);
  };
  const c = corr(out), c0 = corr(runCrowd(resolveCrowd("thriller", { s: 0 }, size), src, 0).data);
  assert.ok(c <= B.periphery_detail_correlation_max, `detail correlation ${c}`);
  assert.ok(c0 >= B.control_zero_pooling_correlation_min, `control correlation ${c0}`);
});

test("a static input gives identical frames between reseeds", () => {
  const a = runCrowd(plan, src, 5).data, b = runCrowd(plan, src, 30).data;
  assert.deepEqual(Array.from(a), Array.from(b));
});
