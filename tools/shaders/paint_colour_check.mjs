// The painterly shader's colour on flat regions, held to evidence/shaders-paint-colour-bounds.json.
//   node tools/shaders/paint_colour_check.mjs [--out evidence.json] [preset ...]
// Paints one frame of flat 48 x 48 patches with each preset on the CPU and reads each patch's
// 16 x 16 interior in OKLab. Exit 1 when any bound fails.
import { writeFileSync, readFileSync } from "node:fs";
import { createPaint } from "../../web/shaders/paint/paint.mjs";

const args = process.argv.slice(2), oi = args.indexOf("--out"), out = oi >= 0 ? args[oi + 1] : null;
const presets = args.filter((a, i) => !a.startsWith("--") && args[i - 1] !== "--out");
const PRESETS = presets.length ? presets : ["oil-grotesque", "oil", "gouache", "watercolour"];
const toLin = (v) => { const c = v / 255; return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; };
export const oklab = (r, g, b) => {
  const l = Math.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b), m = Math.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b), s = Math.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return [0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s, 1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s, 0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s];
};
export const COLOURS = [[3, 3, 3], [23, 18, 14], [240, 107, 31], [224, 172, 105], [20, 60, 200], [250, 250, 250], [128, 128, 128]];
for (let r = 0; r <= 255; r += 51) for (let g = 0; g <= 255; g += 51) for (let b = 0; b <= 255; b += 51) COLOURS.push([r, g, b]);
for (let r = 0; r <= 48; r += 12) for (let g = 0; g <= 48; g += 12) for (let b = 0; b <= 48; b += 12) COLOURS.push([r, g, b]);
const P = 48, M = 16, N = Math.ceil(Math.sqrt(COLOURS.length)), w = N * P, h = N * P;

function frame() {
  const d = new Float32Array(w * h * 4);
  for (let i = 0; i < w * h; i++) d[i * 4 + 3] = 1;
  COLOURS.forEach((c, k) => {
    const ox = (k % N) * P, oy = Math.floor(k / N) * P;
    for (let y = 0; y < P; y++) for (let x = 0; x < P; x++) { const i = ((oy + y) * w + ox + x) * 4; for (let j = 0; j < 3; j++) d[i + j] = toLin(c[j]); }
  });
  return { width: w, height: h, data: d };
}
function measure(img) {
  return COLOURS.map((c, k) => {
    const ox = (k % N) * P + M, oy = Math.floor(k / N) * P + M, src = oklab(...c.map(toLin));
    let sL = 0, sa = 0, sb = 0, n = 0, maxC = 0;
    for (let y = 0; y < P - 2 * M; y++) for (let x = 0; x < P - 2 * M; x++) {
      const i = ((oy + y) * w + ox + x) * 4, o = oklab(...[0, 1, 2].map((j) => Math.min(1, Math.max(0, img[i + j]))));
      sL += o[0]; sa += o[1]; sb += o[2]; n++; maxC = Math.max(maxC, Math.hypot(o[1], o[2]));
    }
    const L = sL / n, a = sa / n, b = sb / n;
    let dh = ((Math.atan2(b, a) - Math.atan2(src[2], src[1])) * 180) / Math.PI; dh = ((dh + 540) % 360) - 180;
    return { c, L0: src[0], C0: Math.hypot(src[1], src[2]), L, C: Math.hypot(a, b), dh, maxC };
  });
}
const LIMITS = {
  darkMeanGain: 0.05, darkPixelGain: 0.08, darkL: { "oil-grotesque": 0.12, oil: 0.12, gouache: 0.12 },
  chromaKept: { "oil-grotesque": 0.7, oil: 0.7, gouache: 0.7, watercolour: 0.55 }, hue: 20, neutral: 0.05,
  midL: { oil: 0.12, "oil-grotesque": 0.22, gouache: 0.2 },
};
function judge(preset, rows) {
  const worst = (rs, f) => rs.reduce((a, r) => (f(r) > f(a) ? r : a));
  const dark = rows.filter((r) => r.L0 <= 0.3), sat = rows.filter((r) => r.C0 >= 0.1), neu = rows.filter((r) => r.C0 < 0.03 && r.L0 > 0.3), mid = rows.filter((r) => r.L0 > 0.3 && r.L0 < 0.9);
  const m = (name, rs, f, limit, dir = "max") => { const wr = worst(rs, dir === "max" ? f : (r) => -f(r)), v = f(wr); return { name, value: +v.toFixed(4), limit, worst: wr.c, pass: limit === null ? true : dir === "max" ? v <= limit : v >= limit }; };
  const checks = [
    m("dark mean chroma gain", dark, (r) => r.C - r.C0, LIMITS.darkMeanGain),
    m("dark pixel chroma gain", dark, (r) => r.maxC - r.C0, LIMITS.darkPixelGain),
    m("dark lightness gain", dark, (r) => r.L - r.L0, LIMITS.darkL[preset] ?? null),
    m("saturated chroma kept", sat, (r) => r.C / r.C0, LIMITS.chromaKept[preset], "min"),
    m("saturated hue shift", sat, (r) => Math.abs(r.dh), LIMITS.hue),
    m("neutral cast", neu, (r) => r.C, LIMITS.neutral),
    m("mid lightness", mid, (r) => Math.abs(r.L - r.L0), LIMITS.midL[preset] ?? null),
  ];
  return { counts: { dark: dark.length, saturated: sat.length, neutral: neu.length, mid: mid.length }, checks };
}

const res = { bounds: "evidence/shaders-paint-colour-bounds.json", colours: COLOURS.length, presets: {} };
const f = frame();
for (const preset of PRESETS) res.presets[preset] = judge(preset, measure(createPaint(preset, {}, { w, h }).frame(f).img));
res.pass = Object.values(res.presets).every((p) => p.checks.every((c) => c.pass));
for (const [p, r] of Object.entries(res.presets)) for (const c of r.checks) console.log(`${c.pass ? "ok  " : "FAIL"} ${p.padEnd(14)} ${c.name.padEnd(24)} ${c.value} (limit ${c.limit}) worst ${c.worst}`);
if (out) writeFileSync(out, JSON.stringify(res, null, 1) + "\n");
process.exit(res.pass ? 0 : 1);
