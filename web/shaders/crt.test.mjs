// node --test web/shaders/crt.test.mjs
// Physical properties of the CRT model, checked on its CPU reference. GPU parity is
// tests/web/shaders_crt.py; these tests are about whether the model is right.
import test from "node:test";
import assert from "node:assert/strict";
import { erf, gaussMass, lowpassTaps, notchTaps } from "./common.mjs";
import { spdToXYZ, xyOf, apply3, rgbToXYZ, PRIMARIES, WHITE } from "./spectral.mjs";
import { screenMatrix, SCREENS } from "./crt/phosphors.mjs";
import { resolve } from "./crt/params.mjs";
import { packTaps, signalChain } from "./crt/signal.mjs";
import { beamPass } from "./crt/beam.mjs";
import { maskCover, maskMean } from "./crt/geometry.mjs";
import { haloKernel, radialReturn } from "./crt/halo.mjs";

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b} (tol ${tol})`);
const flat = (w, h, rgb) => { const d = new Float32Array(w * h * 4); for (let i = 0; i < w * h; i++) d.set([...rgb, 1], i * 4); return { width: w, height: h, data: d }; };
const flatPlan = (o = {}, out = { w: 96, h: 72 }) => resolve("pvm-20", { curvature: { rx: 1e9, ry: 1e9 }, mask: { type: "none" }, view: { border: 0, zoom: 2 }, ...o }, { w: 64, h: 48 }, out);

test("erf and gaussian mass", () => {
  near(erf(0.5), 0.5204998778, 2e-7, "erf(0.5)"); near(erf(-1.3), -0.9340079449, 2e-7, "erf(-1.3)");
  near(gaussMass(-50, 50, 0.3, 1.7), 1, 1e-6, "total mass");
});

test("FIR filters: unit DC gain; the notch removes the subcarrier", () => {
  const lp = lowpassTaps(0.1, 16); near(lp.reduce((a, b) => a + b, 0), 1, 1e-12, "low-pass DC");
  const n = notchTaps(0.25, 0.07, 16);
  let re = 0, im = 0; n.forEach((h, i) => { re += h * Math.cos(Math.PI / 2 * i); im += h * Math.sin(Math.PI / 2 * i); });
  assert.ok(Math.hypot(re, im) < 0.01, `notch gain at fsc ${Math.hypot(re, im)}`);
  near(n.reduce((a, b) => a + b, 0), 1, 1e-9, "notch DC");
});

test("spectral: equal energy is at the centre of the chromaticity diagram", () => {
  const [x, y] = xyOf(spdToXYZ(() => 1)); near(x, 1 / 3, 2e-3, "x"); near(y, 1 / 3, 2e-3, "y");
});

test("P22 spectral model lands within 0.025 of the standard primaries (measured: red 0.021, green and blue under 0.01)", () => {
  const sc = screenMatrix("p22", { primaries: null });
  const ref = [PRIMARIES.ebu[0], PRIMARIES.smpteC[1], PRIMARIES.ebu[2]];
  sc.chromaticities.forEach(([x, y], k) => assert.ok(Math.hypot(x - ref[k][0], y - ref[k][1]) < 0.025, `component ${k}: (${x}, ${y})`));
});

test("a colour screen at full drive is the white point at Y = 1", () => {
  for (const white of ["D65", "J9300"]) {
    const m = screenMatrix("p22", { white, primaries: PRIMARIES.smpteC }).matrix;
    const xyz = apply3(rgbToXYZ(PRIMARIES.rec709, WHITE.D65), apply3(m, [1, 1, 1]));
    near(xyz[1], 1, 1e-9, `${white} Y`); const [x, y] = xyOf(xyz);
    near(x, WHITE[white][0], 1e-6, `${white} x`); near(y, WHITE[white][1], 1e-6, `${white} y`);
  }
});

test("a monochrome screen at full drive has unit luminance", () => {
  for (const s of Object.keys(SCREENS).filter((k) => !SCREENS[k].colour)) {
    const m = screenMatrix(s, { output: "rec2020" }).matrix;
    const Y = apply3(rgbToXYZ(PRIMARIES.rec2020, WHITE.D65), apply3(m, [1, 1, 1]))[1];
    near(Y, 1, 1e-9, s);
  }
});

test("the beam conserves light: a flat field emits its gun current, at any spot size", () => {
  for (const v of [0.2, 0.5, 0.9]) {
    const plan = flatPlan({ phosphor: { screen: "p22" } }), T = packTaps(plan.taps);
    const em = beamPass(plan, signalChain(plan, flat(64, 48, [v, v, v]), 0, T), new Float32Array(96 * 72 * 8), 0);
    let s = 0, n = 0;
    for (let y = 24; y < 48; y++) for (let x = 32; x < 64; x++) { s += em[(y * 96 + x) * 4 + 1]; n++; }
    near(s / n, Math.pow(v, 2.4), 0.01 * Math.pow(v, 2.4) + 1e-4, `field ${v}`);
  }
});

test("dark lines show gaps, bright lines close them (spot growth)", () => {
  const depth = (v) => {
    const plan = flatPlan({ view: { border: 0, zoom: 6 }, spot: { lo: 0.2, hi: 0.42 } }), T = packTaps(plan.taps);
    const em = beamPass(plan, signalChain(plan, flat(64, 48, [v, v, v]), 0, T), new Float32Array(96 * 72 * 8), 0);
    let lo = Infinity, hi = 0;
    for (let y = 20; y < 52; y++) { const e = em[(y * 96 + 48) * 4 + 1]; lo = Math.min(lo, e); hi = Math.max(hi, e); }
    return (hi - lo) / hi;
  };
  const dark = depth(0.25), bright = depth(1);
  assert.ok(dark > 0.5 && bright < 0.15, `consumer spot: modulation depth dark ${dark}, bright ${bright}`);
});

test("mask transmission averages to 1 over the screen, for each mask", () => {
  for (const type of ["grille", "slot", "delta"]) {
    const plan = resolve("pvm-20", { mask: { type, pitch: 0.6 } }, { w: 64, h: 48 }, { w: 96, h: 72 });
    const acc = [0, 0, 0]; let n = 0;
    for (let y = -40; y < 40; y += 0.37) for (let x = -60; x < 60; x += 0.29) { maskCover(plan, x, y, 0.05, 0.05).forEach((v, c) => (acc[c] += v)); n++; }
    acc.forEach((a, c) => near(a / n, 1, 0.03, `${type} channel ${c}`));
    assert.ok(maskMean(plan.p.mask) > 0 && maskMean(plan.p.mask) < 1);
  }
});

test("RGB signal with the bandwidth off is the source, sample for sample", () => {
  const plan = resolve("pvm-20", { signal: { rgbMHz: 0 } }, { w: 32, h: 8 }, { w: 64, h: 32 });
  const src = flat(32, 8, [0, 0, 0]); for (let x = 0; x < 32; x++) src.data[(3 * 32 + x) * 4] = (x % 5) / 4;
  const sig = signalChain(plan, src, 0);
  for (let k = 0; k < plan.N; k++) near(sig[(3 * plan.N + k) * 4], src.data[(3 * 32 + Math.floor(((2 * k + 1) * 32) / (2 * plan.N))) * 4], 1e-7, `k ${k}`);
});

test("composite decodes a flat colour back to itself, and grey carries no chroma", () => {
  for (const decoder of ["comb", "notch"]) for (const rgb of [[0.5, 0.5, 0.5], [0.7, 0.3, 0.2]]) {
    const plan = resolve("trinitron-tv", { signal: { decoder } }, { w: 64, h: 8 }, { w: 64, h: 32 });
    const sig = signalChain(plan, flat(64, 8, rgb), 0);
    const i = (4 * plan.N + (plan.N >> 1)) * 4;
    rgb.forEach((v, c) => near(sig[i + c], v, 0.01, `${decoder} ${rgb} channel ${c}`));
  }
});

test("dot crawl: cross-luma at a colour edge flips sign from one frame to the next", () => {
  const plan = resolve("slot-tv", {}, { w: 64, h: 8 }, { w: 64, h: 32 });
  const src = flat(64, 8, [0.2, 0.2, 0.2]); for (let y = 0; y < 8; y++) for (let x = 32; x < 64; x++) src.data.set([0.8, 0.1, 0.6, 1], (y * 64 + x) * 4);
  const lum = (sig, k) => 0.299 * sig[k * 4] + 0.587 * sig[k * 4 + 1] + 0.114 * sig[k * 4 + 2];
  const a = signalChain(plan, src, 0), b = signalChain(plan, src, 1), row = 4 * plan.N, k = Math.round(plan.N * 0.53);
  const da = lum(a, row + k) - lum(a, row + k + 1), db = lum(b, row + k) - lum(b, row + k + 1);
  assert.ok(Math.sign(da) !== Math.sign(db) && Math.abs(da) > 1e-3, `luma ripple ${da} then ${db}`);
});

test("halo: energy is conserved and the ring starts at 2 t tan(theta_c)", () => {
  const glass = { thickness: 12, n: 1.52, transmission: 0.56, albedo: 0.5, cell: 1 };
  const k = haloKernel(glass, { mmPerPx: 0.5, maxCells: 25 });
  near(k.direct + k.weights.reduce((a, b) => a + b, 0), 1, 1e-6, "direct + halo");
  near(k.ringMm, 2 * 12 * Math.tan(Math.asin(1 / 1.52)), 1e-9, "ring radius");
  // Areal density of returned light (energy per bin over radius) peaks at the ring.
  const dr = 0.25, { bins } = radialReturn(glass, dr, 80), dens = Array.from(bins, (b, i) => b / ((i + 0.5) * dr));
  let peak = 8; dens.forEach((v, i) => { if (i > 8 && v > dens[peak]) peak = i; });
  near((peak + 0.5) * dr, k.ringMm, 2 * dr, "halo density peak");
  assert.ok(dens[peak] > 5 * dens[Math.floor(15 / dr)], "the ring is brighter than the disc inside it");
});

test("persistence keeps every joule: a long-glow phosphor gives back all it took", () => {
  const W = 16, H = 12, plan = flatPlan({ phosphor: { screen: "p7" } }, { w: W, h: H }), T = packTaps(plan.taps);
  const lit = signalChain(plan, flat(64, 48, [0.8, 0.8, 0.8]), 0, T), dark = signalChain(plan, flat(64, 48, [0, 0, 0]), 0, T);
  // Component 1 is P7's yellow-green afterglow (component 0 is its fast blue flash).
  const pix = 6 * W + 8, sum3 = (a, o) => a[o + 1];
  const hist = new Float32Array(W * H * 8), first = beamPass(plan, lit, hist, 0);
  const deposited = sum3(first, pix * 4) + sum3(hist, pix * 8) + sum3(hist, pix * 8 + 4);
  let total = sum3(first, pix * 4), prev = Infinity;
  for (let f = 1; f <= 400; f++) {
    const s = sum3(beamPass(plan, dark, hist, f), pix * 4);
    assert.ok(s <= prev + 1e-9, `afterglow brightened at frame ${f}`); prev = s; total += s;
  }
  assert.ok(sum3(first, pix * 4) < 0.7 * deposited, "a P7 screen holds most of a frame's light for later frames");
  near(total, deposited, deposited * 0.005, "energy out vs energy in");
});
