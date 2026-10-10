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
