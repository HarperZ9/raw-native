// Comparison images for the lab 2 techniques, from the CPU references (the GPU passes match them;
// see evidence/shaders-<name>-parity-*.json).
//   node tools/shaders/lab2-sheets.mjs <outdir> [adjacency] [sag] [lightpaint] [glaze] [hysteresis]
// Each technique writes an "off" panel (the technique disabled or the conventional method) and
// one or more "on" panels from the same source at the same size.
import { mkdirSync } from "node:fs";
import { writePng, crop, nearest } from "./png.mjs";
import { labEncode8 } from "../../web/shaders/lab/gpu-kit.mjs";
import { labSource } from "../../web/shaders/lab/sources.mjs";

const args = process.argv.slice(2), out = args[0] || "build/lab2-sheets", want = new Set(args.slice(1));
mkdirSync(out, { recursive: true });
const save = (name, img) => { writePng(`${out}/${name}.png`, img); console.log(`${out}/${name}.png`); };
const img8 = (f) => ({ width: f.width, height: f.height, data: labEncode8(f) });
const on = (k) => want.size === 0 || want.has(k);
const W = 640, H = 400;

if (on("adjacency")) {
  const { resolveAdjacency, develop } = await import("../../web/shaders/adjacency/adjacency.mjs");
  for (const frame of ["street", "edges"]) {
    const src = labSource(frame, W, H), size = { w: W, h: H };
    save(`adjacency-${frame}-0-source`, img8(src));
    for (const preset of ["eberhard", "mackie", "exhausted"]) {
      const r = develop(resolveAdjacency(preset, {}, size), src).out, im = img8(r);
      save(`adjacency-${frame}-${preset}`, im);
      if (frame === "street") save(`adjacency-${frame}-${preset}-crop`, nearest(crop(im, 120, 90, 160, 120), 4));
    }
    if (frame === "street") save(`adjacency-${frame}-0-source-crop`, nearest(crop(img8(src), 120, 90, 160, 120), 4));
  }
}

if (on("sag")) {
  const { resolveSag, runSag, signal8 } = await import("../../web/shaders/sag/sag.mjs");
  const sig8 = (f) => ({ width: f.width, height: f.height, data: signal8(f) });
  for (const frame of ["street-signal", "box"]) {
    const src = labSource(frame, W, 480);
    save(`sag-${frame}-0-source`, sig8(src));
    for (const preset of ["consumer", "thriller"]) {
      const r = runSag(resolveSag(preset, {}, { w: W, h: 480 }), src).out;
      save(`sag-${frame}-${preset}`, sig8(r));
      // Where the picture moved: |sagged - source| x 8, as grey.
      const d = new Uint8ClampedArray(W * 480 * 4);
      for (let i = 0; i < d.length; i += 4) { const v = Math.min(255, 8 * 255 * Math.abs(r.data[i + 1] - src.data[i + 1])); d[i] = d[i + 1] = d[i + 2] = v; d[i + 3] = 255; }
      save(`sag-${frame}-${preset}-diff8x`, { width: W, height: 480, data: d });
    }
  }
}

if (on("lightpaint")) {
  const { resolveLightpaint, runLightpaint } = await import("../../web/shaders/lightpaint/lightpaint.mjs");
  for (const frame of ["street", "day"]) {
    const src = labSource(frame, W, H);
    save(`lightpaint-${frame}-0-multiply`, img8(src));
    for (const preset of ["disco", "grotesque", "complement"]) save(`lightpaint-${frame}-${preset}`, img8(runLightpaint(resolveLightpaint(preset), src)));
  }
}

if (on("glaze")) {
  const { resolveGlaze, runGlaze } = await import("../../web/shaders/glaze/glaze.mjs");
  for (const frame of ["street", "day"]) {
    const src = labSource(frame, W, H), g = frame === "day" ? 1 : 2.4, enc = (f) => ({ width: f.width, height: f.height, data: labEncode8(f, g) });
    save(`glaze-${frame}-0-plain`, enc(src));
    for (const preset of ["beetle", "oil-slick", "bruise"]) save(`glaze-${frame}-${preset}`, enc(runGlaze(resolveGlaze(preset), src)));
  }
}

if (on("hysteresis")) {
  const { resolveHysteresis, step } = await import("../../web/shaders/hysteresis/hysteresis.mjs");
  const { sequence } = await import("../../web/shaders/hysteresis/sequences.mjs");
  const w = 320, h = 200, frames = sequence("flicker", w, h, 48);
  for (const preset of ["bands6", "pico8"]) {
    const plan = resolveHysteresis(preset, {}, { w, h });
    for (const hyst of [false, true]) {
      let state = null, prev = null; const count = new Uint16Array(w * h); let last;
      for (const f of frames) { const r = step(plan, f.frame, null, f.dist, state, { hysteresis: hyst }); state = r.state; if (prev) for (let i = 0; i < count.length; i++) if (r.idx[i] !== prev[i]) count[i]++; prev = r.idx; last = r; }
      const tag = hyst ? "held" : "plain";
      save(`hysteresis-${preset}-${tag}-frame47`, { width: w, height: h, data: last.out });
      // Toggles per pixel over 47 frame changes: black none, white 12 or more.
      const d = new Uint8ClampedArray(w * h * 4); for (let i = 0; i < count.length; i++) { const v = Math.min(255, count[i] * 21); d[i * 4] = v; d[i * 4 + 1] = v; d[i * 4 + 2] = v; d[i * 4 + 3] = 255; }
      save(`hysteresis-${preset}-${tag}-toggles`, { width: w, height: h, data: d });
    }
  }
}

if (on("purity")) {
  const { resolvePurity, runPurity, displayEncode8 } = await import("../../web/shaders/purity/purity.mjs");
  const { displayLinear } = await import("../../web/shaders/lab/sources.mjs");
  const enc = (f) => ({ width: f.width, height: f.height, data: displayEncode8(f) });
  for (const frame of ["white", "street"]) {
    const src = displayLinear(frame, W, 480);
    save(`purity-${frame}-0-source`, enc(src));
    save(`purity-${frame}-magnetised`, enc(runPurity(resolvePurity("magnetised"), src, 0)));
    save(`purity-${frame}-earth-field`, enc(runPurity(resolvePurity("earth-field"), src, 0)));
    for (const t of [0.02, 0.15, 0.4]) save(`purity-${frame}-degauss-${t}s`, enc(runPurity(resolvePurity("degauss"), src, t)));
  }
}
