// node --test web/shaders/film.test.mjs
// Properties of the spectral film model, on its CPU reference. GPU parity is
// tests/web/shaders_parity.py --shader film.
import test from "node:test";
import assert from "node:assert/strict";
import { build, negDensities, printPixel, GREY_TARGET } from "./film/stocks.mjs";
import { varianceTable, grainAt, tableAt, poissonCount, MU_MAX } from "./film/grain.mjs";
import { resolveFilm, weave, mtfWeights } from "./film/film.mjs";
import { radialReturn } from "./crt/halo.mjs";
import { createFilm } from "./film/run.mjs";
import { pcg } from "./common.mjs";

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b} (tol ${tol})`);
const lum = (c) => 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
const out = (t, rgb, ev = 0) => printPixel(t, negDensities(t, rgb, ev));

test("colour timing prints an 18% grey as a neutral 18%, for every process", () => {
  for (const process of ["normal", "bleach-bypass"]) for (const negative of ["neg-500t", "neg-250d"]) {
    const o = out(build({ process, negative }), [0.18, 0.18, 0.18]);
    o.forEach((v, c) => near(v, GREY_TARGET, 1e-5, `${process} ${negative} channel ${c}`));
  }
});

test("the grey scale tracks: RMS chroma deviation under 3% from -5 to +5 stops", () => {
  const t = build({}); let e = 0, n = 0;
  for (let ev = -5; ev <= 5; ev += 0.5) { const o = out(t, [0.18, 0.18, 0.18], ev), m = (o[0] + o[1] + o[2]) / 3; for (const v of o) { e += ((v - m) / m) ** 2; n++; } }
  assert.ok(Math.sqrt(e / n) < 0.03, `RMS ${Math.sqrt(e / n)}`);
});

test("tone: brighter scenes print brighter, from -8 to +10 stops, with a shoulder below white", () => {
  const t = build({}); let prev = -1;
  for (let ev = -8; ev <= 10; ev += 0.5) { const y = lum(out(t, [0.18, 0.18, 0.18], ev)); assert.ok(y > prev, `ev ${ev}: ${y} after ${prev}`); prev = y; }
  assert.ok(prev < 1 && prev > 0.8, `highlight at +10 stops ${prev}`);
});

test("bleach bypass desaturates and deepens the blacks", () => {
  const n = build({}), b = build({ process: "bleach-bypass" }), red = [0.5, 0.1, 0.1];
  const sat = (c) => (Math.max(...c) - Math.min(...c)) / Math.max(...c);
  assert.ok(sat(out(b, red)) < 0.8 * sat(out(n, red)), "saturation");
  assert.ok(lum(out(b, [0.18, 0.18, 0.18], -5)) < 0.7 * lum(out(n, [0.18, 0.18, 0.18], -5)), "blacks");
});

test("printer points move the print as a colour timer would (12 points = 1 stop, 0.30 log E)", () => {
  const t0 = build({}), t1 = build({ printerPoints: [12, 12, 12] });
  t0.logGain.forEach((g, j) => near(t1.logGain[j] - g, 0.3, 1e-9, `channel ${j}`));
  assert.ok(lum(out(t1, [0.18, 0.18, 0.18])) < lum(out(t0, [0.18, 0.18, 0.18])), "more printer light, a darker print");
});

test("grain keeps the mean, in both regimes", () => {
  const T = varianceTable();
  for (const um of [26, 3]) for (const u of [0.2, 0.5, 0.85]) {
    let s = 0, n = 0;
    for (let y = 4; y < 84; y++) for (let x = 4; x < 84; x++) { s += grainAt(x, y, 0, 3.2, um, 0, () => u, 88, 88, T, 1); n++; }
    near(s / n, u, 0.01, `mean at ${um} um/px, u ${u}`);
  }
});

test("Selwyn's law: in the small-grain regime sigma * sqrt(area) is constant", () => {
  const T = varianceTable(), sd = (um) => {
    let s = 0, s2 = 0, n = 0;
    for (let y = 0; y < 120; y++) for (let x = 0; x < 120; x++) { const v = grainAt(x, y, 1, 3.2, um, 0, () => 0.5, 120, 120, T, 1); s += v; s2 += v * v; n++; }
    return Math.sqrt(s2 / n - (s / n) ** 2);
  };
  const a = sd(40) * 40, b = sd(20) * 20;
  near(a / b, 1, 0.06, "sigma sqrt(A) at 40 and 20 um per pixel");
  near(sd(40), Math.sqrt(tableAt(T, 0.5) * 3.2 * 3.2 / 1600), 0.004, "against the model's variance");
});

test("the integer Poisson draw has the right mean", () => {
  for (const mu of [0.1, 0.7, 1.6]) {
    let s = 0; const n = 200000;
    for (let i = 0; i < n; i++) s += poissonCount(mu, pcg(i * 2654435761));
    near(s / n, mu, 0.01 + MU_MAX / 512, `mu ${mu}`);
  }
});

test("halation without the remjet is red-first and forms a ring; the remjet suppresses it", () => {
  const W = 192, H = 108, scene = { width: W, height: H, data: new Float32Array(W * H * 4) };
  scene.data.set([400, 400, 400, 1], ((H >> 1) * W + (W >> 1)) * 4);
  const ring = (preset) => {
    const f = createFilm(preset, { grainAmount: 0, weaveUm: 0, jitterUm: 0, gateWidthMm: 3 }, { w: W, h: H }, { w: W, h: H });
    const r = f.frame(scene, 0), px = Math.round((f.plan.halo.ringMm * 1000) / f.plan.umPerPx), i = ((H >> 1) * W + (W >> 1) + px) * 4;
    return [r.H[i], r.H[i + 1], r.img[i], r.img[i + 1], f];
  };
  const [, , rOff, gOff] = ring("500t-no-remjet"), [, , rOn] = ring("500t-print");
  assert.ok(rOff > 2 * gOff, `red-first: red ${rOff}, green ${gOff}`);
  assert.ok(rOn < rOff * 0.5, `remjet: ${rOn} against ${rOff}`);
  const p = resolveFilm("500t-no-remjet", {}, { w: W, h: H }, { w: W, h: H });
  near(p.halo.ringMm, 2 * 0.125 * Math.tan(Math.asin(1 / 1.49)), 1e-12, "ring radius from the base geometry");
});

test("gate weave is deterministic and bounded by its amplitude", () => {
  const p = resolveFilm("500t-print", {}, { w: 1920, h: 1080 }, { w: 1920, h: 1080 });
  for (let f = 0; f < 200; f++) {
    const [x, y] = weave(p, f), [x2, y2] = weave(p, f);
    assert.equal(x, x2); assert.equal(y, y2);
    assert.ok(Math.hypot(x, y) * p.umPerPx <= Math.SQRT2 * (p.p.weaveUm + p.p.jitterUm / 2) + 1e-9, `frame ${f}`);
  }
});

test("the pressure plate adds to the Fresnel fill inside the halation ring, in proportion to its reflectance", () => {
  const base = { thickness: 0.125, n: 1.49, transmission: 1 }, dr = 0.002, ring = 2 * 0.125 * Math.tan(Math.asin(1 / 1.49));
  const dens = (R) => { const { bins, kappa } = radialReturn({ ...base, backReflectance: R }, dr, 0.6); const i = Math.floor((0.5 * ring) / dr); return { kappa, inside: bins[i] / ((i + 0.5) * dr) }; };
  const off = dens(0), p5 = dens(0.05), p20 = dens(0.2);
  assert.ok(p5.inside > 1.5 * off.inside, `inside the ring: ${p5.inside} with a 5% plate, ${off.inside} from Fresnel alone`);
  near((p20.inside - off.inside) / (p5.inside - off.inside), 4, 0.05, "fill scales with plate reflectance");
  assert.ok(p5.kappa > off.kappa && p5.kappa - off.kappa < 0.05, `returned energy ${off.kappa} -> ${p5.kappa}`);
});

test("emulsion scatter: sigma 4.2 um puts the 50% MTF near 45 cycles/mm, and the blur keeps the mean", () => {
  const sigma = 4.2e-3, f50 = Math.sqrt(Math.LN2 / (2 * Math.PI * Math.PI)) / sigma;
  near(f50, 45, 1, "f50 in cycles/mm");
  const w = mtfWeights(1.3); near(w.reduce((a, b) => a + b, 0), 1, 1e-6, "weights");
  assert.equal(mtfWeights(0.1).length, 1, "no blur under 0.15 pixel");
});
