// Fixtures of crt-classic, VHS and fibre-network watercolour (evidence/cpp-port-style-bounds-5.json).
// Each case is the web parity page's case (web/test/shaders-classic.html, shaders-vhs.html,
// shaders-fibre.html), built with the web modules unchanged.
import { join } from "node:path";
import { classicRef } from "../../web/shaders/crt-classic/classic.mjs";
import { beamTable, maskTable } from "../../web/shaders/reference/retro-crt.mjs";
import { testCard } from "../../web/shaders/fixtures/testcard.mjs";
import { gradientPlate } from "../../web/shaders/fixtures/plates.mjs";
import { createVhs, toDisplay } from "../../web/shaders/vhs/vhs.mjs";
import { YIQ_INV } from "../../web/shaders/crt/signal.mjs";
import { streetScene } from "../../web/shaders/fixtures/street.mjs";
import { linearToSrgb } from "../../web/shaders/common.mjs";
import { resolveFibre, runFibre } from "../../web/shaders/fibre/fibre.mjs";
import { displayLinear } from "../../web/shaders/lab/sources.mjs";
import { Bundle, readJson } from "./rnfx.mjs";

// As web/test/shaders-classic.html: a float frame nearest-upscaled by k into RGBA8.
function bytesUp(f, k) {
  const w = f.width * k, h = f.height * k, o = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) for (let c = 0; c < 4; c++)
    o[(y * w + x) * 4 + c] = c === 3 ? 255 : Math.round(f.data[(((y / k) | 0) * f.width + ((x / k) | 0)) * 4 + c] * 255);
  return { width: w, height: h, data: o };
}
const clamp = (x, lo, hi) => (x < lo ? lo : x > hi ? hi : x);
const fixed = (t) => Int32Array.from(t, (v) => (v * 256 + 0.5) | 0);

export function crtClassic(out) {
  const b = readJson("evidence/shaders-crt-classic-parity-bounds.json"), B = new Bundle("crtclassic");
  // retro-crt.mjs keeps its transfer tables private; these are its expressions, as classic.mjs tables() rebuilds them.
  const toLin = new Float32Array(256), toSrgb = new Uint8ClampedArray(4097);
  for (let i = 0; i < 256; i++) { const c = i / 255; toLin[i] = c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); }
  for (let i = 0; i <= 4096; i++) { const l = i / 4096; toSrgb[i] = Math.round((l <= 0.0031308 ? l * 12.92 : 1.055 * Math.pow(l, 1 / 2.4) - 0.055) * 255); }
  B.add("toLin", "f32", toLin); B.add("toSrgb", "u8", toSrgb);
  const frames = { card4: bytesUp(testCard(240, 150), 4), card3: bytesUp(testCard(320, 200), 3), gradient: gradientPlate(960, 600) };
  for (const [k, f] of Object.entries(frames)) B.add(`frame.${k}`, "u8", f.data);
  B.head.frames = Object.fromEntries(Object.entries(frames).map(([k, f]) => [k, { width: f.width, height: f.height }]));
  B.head.cases = [];
  for (const c of b.cases) {
    const f = frames[c.frame], o = c.opts, cell = Math.max(1, o.cell | 0), s = clamp(+o.scanStrength || 0, 0, 1);
    const maskOn = !!(o.mask && o.mask !== "none" && o.maskStrength > 0);
    const beam = fixed(o.scanlines && cell >= 2 ? beamTable(cell, s, o.beam) : new Float32Array(16 * cell).fill(1));
    const mt = fixed(maskOn ? maskTable(o.mask, o.maskStrength) : new Float32Array(54).fill(1));
    B.add(`${c.id}.beam`, "u32", Uint32Array.from(beam)); B.add(`${c.id}.mask`, "u32", Uint32Array.from(mt));
    B.add(`${c.id}.js`, "u8", classicRef(f.data, f.width, f.height, o));
    B.head.cases.push({ id: c.id, frame: c.frame, width: f.width, height: f.height, opts: o });
  }
  B.write(join(out, "crtclassic.rnfx"));
  return B.head.cases.length;
}

export function vhs(out) {
  const b = readJson("evidence/shaders-vhs-parity-bounds.json"), { width: W, height: H } = b.output, B = new Bundle("vhs");
  const SW = 176, SH = 120, s = streetScene(SW, SH, 0), src = { width: SW, height: SH, data: new Float32Array(SW * SH * 4) };
  for (let i = 0; i < src.data.length; i++) src.data[i] = (i & 3) === 3 ? 1 : linearToSrgb(1 - Math.exp(-Math.max(0, s.data[i]) * 2.4));
  B.add("src", "f32", src.data);
  B.head.src = { width: SW, height: SH }; B.head.cases = [];
  for (const c of b.cases) {
    const cpu = createVhs(c.preset, {}, { w: SW, h: SH }), sig = cpu.frame(src, c.frame).sig, disp = toDisplay(cpu.plan, sig, W, H), pl = cpu.plan;
    B.add(`${c.id}.sig`, "f32", sig); B.add(`${c.id}.js`, "u8", disp);
    B.add(`${c.id}.taps_luma`, "f64", pl.taps.luma); B.add(`${c.id}.taps_peak`, "f64", pl.taps.peak); B.add(`${c.id}.taps_chroma`, "f64", pl.taps.chroma);
    B.head.cases.push({ id: c.id, preset: c.preset, frame: c.frame, width: W, height: H, p: pl.p,
      plan: { fs: pl.fs, N: pl.N, lines: pl.lines, g: pl.g, lumaNoise: pl.lumaNoise, chromaNoise: pl.chromaNoise, delay: pl.delay, phaseNoise: pl.phaseNoise, yiqInv: YIQ_INV } });
  }
  B.write(join(out, "vhs.rnfx"));
  return B.head.cases.length;
}

export function fibre(out) {
  const b = readJson("evidence/shaders-fibre-parity-bounds.json"), { width: W, height: H } = b.output, B = new Bundle("fibre");
  B.head.cases = [];
  for (const c of b.cases) {
    const src = displayLinear(c.frame, W, H), plan = resolveFibre(c.preset, {}, { w: W, h: H }), r = runFibre(plan, src, { keep: true }), n = W * H, v = new Float64Array(n * 4);
    for (let i = 0; i < n; i++) { for (let k = 0; k < 3; k++) v[i * 4 + k] = plan.p.paper[k] * Math.exp(-(r.P[i * 3 + k] + r.D[i * 3 + k])); v[i * 4 + 3] = 1; }
    for (let i = 0; i < v.length; i++) if ((i & 3) !== 3 && Math.fround(v[i]) !== r.out.data[i]) throw new Error(`fixtures_signal: fibre ${c.id} value ${i} is ${v[i]}, the module wrote ${r.out.data[i]}`);
    B.add(`${c.id}.src`, "f32", src.data); B.add(`${c.id}.js`, "f64", v);
    B.head.cases.push({ id: c.id, preset: c.preset, width: W, height: H, p: plan.p, plan: { L: plan.L, C: plan.C, dt: plan.dt } });
  }
  B.write(join(out, "fibre.rnfx"));
  return B.head.cases.length;
}
