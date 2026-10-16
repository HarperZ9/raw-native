// Round trip of paint-over projection (project-docs/PAINT-OVER-PROTOCOL.md, part 1).
//   node tools/shaders/paintover_roundtrip.mjs --out evidence/paintover-roundtrip-runs.json [--save DIR]
import { writeFileSync, mkdirSync, readFileSync } from "node:fs";
import { camAt, PW, PH, paintProjector, renderGbufs, renderProjected, makeProjector, shadePoint } from "./paintover_lib.mjs";
import { writePng } from "./png.mjs";

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const proto = JSON.parse(readFileSync(new URL("../../evidence/paintover-protocol.json", import.meta.url))), B = proto.roundtrip;
const cam = camAt(0), shifted = { ...cam, pos: cam.pos.map((v, i) => v + cam.right[i] * ((2 * cam.scale) / PH)) };

const [gShaded, gPlain, gShift] = await renderGbufs([{ cam, w: PW, h: PH }, { cam, w: PW, h: PH, shadeOn: false }, { cam: shifted, w: PW, h: PH, shadeOn: false }]);
const { projector, painting } = paintProjector(cam, gShaded, proto.primary.seed);

function compare(projectors, g, viewCam, reference) {
  const r = renderProjected(projectors, g, viewCam);
  let n = 0, cov = 0, hit = 0, sum = 0, max = 0;
  for (let i = 0; i < g.w * g.h; i++) {
    if (!g.hit[i]) continue;
    hit++;
    if (!r.covered[i]) continue;
    cov++; n++;
    for (let c = 0; c < 3; c++) { const d = Math.abs(Math.round(Math.min(1, Math.max(0, r.rgb[i * 3 + c])) * 255) - reference[i * 4 + c]); sum += d; if (d > max) max = d; }
  }
  return { covered_fraction: cov / hit, max_abs_code: max, mean_abs_code: sum / (n * 3), covered: cov, hit_pixels: hit, uncovered: hit - cov, image: r };
}
const checks = [], add = (id, pass, detail) => { checks.push({ id, pass, detail }); console.log(pass ? "ok  " : "MISS", id, detail); };
const rt = compare([projector], gPlain, cam, painting.rgba8);
add("R1 max", rt.max_abs_code <= B.bounds.max_abs_code, `max ${rt.max_abs_code} (bound ${B.bounds.max_abs_code})`);
add("R1 mean", rt.mean_abs_code <= B.bounds.mean_abs_code, `mean ${rt.mean_abs_code.toFixed(5)} (bound ${B.bounds.mean_abs_code})`);
add("R2 covered", rt.covered_fraction >= B.bounds.covered_fraction_min, `covered ${rt.covered_fraction.toFixed(5)}, ${rt.uncovered} uncovered of ${rt.hit_pixels} (bound ${B.bounds.covered_fraction_min})`);
const c3 = compare([projector], gShift, shifted, painting.rgba8);
add("R3 shift control", c3.mean_abs_code >= B.controls.shift_one_pixel_mean_min, `mean ${c3.mean_abs_code.toFixed(3)} (needs >= ${B.controls.shift_one_pixel_mean_min})`);
const unpainted = new Uint8Array(PW * PH * 4); for (let i = 0; i < PW * PH; i++) { for (let c = 0; c < 3; c++) unpainted[i * 4 + c] = Math.round(Math.min(1, gShaded.shade[i * 3 + c]) * 255); unpainted[i * 4 + 3] = 255; }
const c4 = compare([makeProjector(cam, PW, PH, unpainted, gShaded.depth)], gPlain, cam, painting.rgba8);
add("R4 unpainted control", c4.mean_abs_code >= B.controls.unpainted_mean_min, `mean ${c4.mean_abs_code.toFixed(3)} (needs >= ${B.controls.unpainted_mean_min})`);
const c5 = compare([makeProjector(cam, PW, PH, painting.rgba8, gShaded.depth.map((d) => d - 1))], gPlain, cam, painting.rgba8);
add("R5 wrong-depth control", c5.covered_fraction <= B.controls.wrong_depth_covered_max, `covered ${c5.covered_fraction.toFixed(5)} (needs <= ${B.controls.wrong_depth_covered_max})`);
const o = opt("--out", null), sv = opt("--save", null);
if (sv) {
  mkdirSync(sv, { recursive: true });
  const toRgba = (rgb) => { const a = new Uint8ClampedArray(PW * PH * 4); for (let i = 0; i < PW * PH; i++) { for (let c = 0; c < 3; c++) a[i * 4 + c] = Math.round(Math.min(1, Math.max(0, rgb[i * 3 + c])) * 255); a[i * 4 + 3] = 255; } return a; };
  writePng(`${sv}/painting.png`, { width: PW, height: PH, data: new Uint8ClampedArray(painting.rgba8) }); writePng(`${sv}/reprojected.png`, { width: PW, height: PH, data: toRgba(rt.image.rgb) }); writePng(`${sv}/export.png`, { width: PW, height: PH, data: new Uint8ClampedArray(unpainted) });
}
if (o) { const r = (x) => ({ ...x, image: undefined }); writeFileSync(o, JSON.stringify({ protocol: "evidence/paintover-protocol.json", size: [PW, PH], roundtrip: r(rt), shift: r(c3), unpainted: r(c4), wrong_depth: r(c5), checks }, null, 1) + "\n"); }
void shadePoint;
