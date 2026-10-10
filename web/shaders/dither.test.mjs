// node --test web/shaders/dither.test.mjs
// The blue-noise mask and the dither modes on the CPU. GPU parity: tests/web/shaders_parity.py
// --shader dither (bit-equal palette indices) and --shader classic.
import test from "node:test";
import assert from "node:assert/strict";
import { voidAndCluster, blueNoise64 } from "./dither/bluenoise.mjs";
import { ditherRef } from "./dither/dither.mjs";
import { dsSplit } from "./dither/ds.wgsl.mjs";
import { labPalette } from "./reference/retro-palettes.mjs";

test("blue noise is a permutation of 0 .. N^2 - 1 and deterministic", () => {
  const a = blueNoise64(), b = voidAndCluster(64);
  assert.equal(new Set(a).size, 4096); assert.equal(Math.min(...a), 0); assert.equal(Math.max(...a), 4095);
  assert.deepEqual(Array.from(a), Array.from(b));
});

test("blue noise has little low-frequency power (its 50% pattern)", () => {
  const N = 16, r = voidAndCluster(N), f = Array.from(r, (v) => (v < (N * N) / 2 ? 1 : -1));
  let low = 0, nl = 0, high = 0, nh = 0;
  for (let v = 0; v < N; v++) for (let u = 0; u < N; u++) {
    let re = 0, im = 0;
    for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) { const a = (-2 * Math.PI * (u * x + v * y)) / N; re += f[y * N + x] * Math.cos(a); im += f[y * N + x] * Math.sin(a); }
    const k = Math.hypot(Math.min(u, N - u), Math.min(v, N - v)), p = re * re + im * im;
    if (k > 0 && k <= 2) { low += p; nl++; } else if (k >= 5) { high += p; nh++; }
  }
  assert.ok(low / nl < 0.2 * (high / nh), `low ${low / nl}, high ${high / nh}`);
});

test("on a two-colour palette, every patterned mode reproduces a flat grey's mix ratio", () => {
  const pal = labPalette("mono1"), w = 128, h = 128;
  for (const v of [64, 128, 200]) {
    const plate = new Uint8Array(w * h * 4).fill(v); for (let i = 3; i < plate.length; i += 4) plate[i] = 255;
    const fractions = ["bayer8", "noise", "blue"].map((mode) => ditherRef(plate, w, h, { palette: pal, mode }).reduce((a, b) => a + b, 0) / (w * h));
    for (let i = 1; i < fractions.length; i++) assert.ok(Math.abs(fractions[i] - fractions[0]) < 0.02, `${v}: ${fractions}`);
    assert.ok(fractions[0] > 0.02 && fractions[0] < 0.98, `${v}: a mix, not a flat pick`);
  }
});

test("double-single split keeps an f64 to about 2^-47 (two 24-bit mantissas)", () => {
  for (const v of [0.4122214708, 1 / 3, 52.9829189, 1e-9, -2.428592205]) { const [hi, lo] = dsSplit(v); assert.ok(Math.abs(hi + lo - v) <= Math.abs(v) * 2 ** -47, `${v}: ${Math.abs(hi + lo - v) / Math.abs(v)}`); }
});
