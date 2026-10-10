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
