// Sodium light study: Q1 to Q5 of project-docs/SODIUM-LIGHT-PROTOCOL.md (fixed in
// evidence/sodium-protocol.json before this tool existed). Writes evidence/sodium-study-runs.json.
//   node tools/shaders/sodium_study.mjs [--out evidence/sodium-study-runs.json]
import { readFileSync, writeFileSync } from "node:fs";
import { responseTable, paletteSpread, chromaSpread, PALETTE } from "../../web/shaders/sodium/spectra.mjs";
import { resolveSodium, shadePixel } from "../../web/shaders/sodium/shade.mjs";
import { sodiumLights, visOf, STRIP, quayCamera } from "../../web/shaders/fixtures/sodium-quay.mjs";
import { van, STREET } from "../../web/shaders/fixtures/quay-street.mjs";
import { rgbToXYZ, PRIMARIES, WHITE, apply3 } from "../../web/shaders/spectral.mjs";

const args = process.argv.slice(2), out = args.includes("--out") ? args[args.indexOf("--out") + 1] : "evidence/sodium-study-runs.json";
const proto = JSON.parse(readFileSync("evidence/sodium-protocol.json", "utf8")), res = { protocol: "evidence/sodium-protocol.json", date: new Date().toISOString().slice(0, 10) };
const pure = responseTable({ impurity: 0 }), def = responseTable({ impurity: 0.02 });

res.Q1 = { S: paletteSpread(pure, { sodium: 1 }) }; res.Q1.pass = res.Q1.S < proto.Q1_pure_sodium_S_max;
res.Q2 = { S: paletteSpread(def, { sodium: 1 }) }; res.Q2.pass = res.Q2.S < proto.Q2_default_sodium_S_max;
const S1 = paletteSpread(def, { window: 1 });
res.Q3 = { S: S1, led: paletteSpread(def, { led: 1 }), sky: paletteSpread(def, { sky: 1 }) }; res.Q3.pass = S1 > proto.Q3_window_S_min;
{
  const n = proto.Q4_mixing.steps, curve = Array.from({ length: n }, (_, k) => { const f = k / (n - 1); return { f, S: paletteSpread(def, { sodium: 1 - f, window: f }) }; });
  const mono = curve.every((c, k) => k === 0 || c.S >= curve[k - 1].S);
  let f50 = null; for (let k = 1; k < n; k++) if (curve[k].S >= 0.5 * S1) { const a = curve[k - 1], b = curve[k]; f50 = a.f + ((0.5 * S1 - a.S) / (b.S - a.S)) * (b.f - a.f); break; }
  const half = curve[(n - 1) / 2].S / S1;
  res.Q4 = { curve, monotone: mono, S_half_over_S_one: half, f50, pass: mono && half >= proto.Q4_mixing.S_half_over_S_one[0] && half <= proto.Q4_mixing.S_half_over_S_one[1] && f50 >= proto.Q4_mixing.f50[0] && f50 <= proto.Q4_mixing.f50[1] };
}
// Q5: probe points along the strip, all eight palette materials at each point.
// --ambient X is exploratory (not the protocol run): a different sky fill.
const ambOverride = args.includes("--ambient") ? { ambient: +args[args.indexOf("--ambient") + 1] } : {};
{
  const t = 3, lights = sodiumLights(t), vs = van(t), cam = quayCamera(t), M = rgbToXYZ(PRIMARIES.rec709, WHITE.D65);
  const len = Math.hypot(STRIP.x1 - STRIP.x0, STRIP.z1 - STRIP.z0), steps = Math.round(len / 0.05);
  const probe = (P, shadows, plan) => {
    const g = { w: PALETTE, h: 1, data: new Float64Array(PALETTE * 40), lights, cam }, N = [0, 1, 0], vis = shadows ? visOf(P, N, lights, vs) : lights.map(() => 1);
    for (let m = 0; m < PALETTE; m++) { const b = m * 40; g.data.set([...P, m, ...N, 1, -1, 0, Math.hypot(P[0] - cam.pos[0], P[1] - cam.pos[1], P[2] - cam.pos[2]), 1], b); const dv = [P[0] - cam.pos[0], P[1] - cam.pos[1], P[2] - cam.pos[2]], dl = Math.hypot(...dv); g.data.set(dv.map((v) => v / dl), b + 12); vis.forEach((v, k) => { g.data[b + 28 + k] = v; }); }
    return chromaSpread(Array.from({ length: PALETTE }, (_, m) => apply3(M, shadePixel(plan, g, m, 0, t))));
  };
  // window share of a white surface's illuminance (luminance), the inverse-square and cosine model
  const amb = resolveSodium(ambOverride).ambient; let bbShare = 0;
  const share = (P) => { let win = 0, all = 0, bb = 0; for (const L of lights) { const v = [L.pos[0] - P[0], L.pos[1] - P[1], L.pos[2] - P[2]], d2 = v[0] ** 2 + v[1] ** 2 + v[2] ** 2, dl = Math.sqrt(d2), l = v.map((c) => c / dl); let k = Math.max(0, l[1]) / d2; if (L.mode === 1) { const a = -(l[0] * L.cone[0] + l[1] * L.cone[1] + l[2] * L.cone[2]); k *= a > 0.8 ? Math.pow((a - 0.8) / 0.2, 1.5) * 4 : 0; } else if (L.mode === 2) k *= Math.max(0, -(l[0] * L.cone[0] + l[1] * L.cone[1] + l[2] * L.cone[2])); const e = L.I * k / Math.PI; all += e; if (L.type === 1) win += e; else if (L.type !== 0) bb += e; } bbShare = (win + bb + amb) / (all + amb); return win / (all + amb); };
  const direct = resolveSodium({ mist: 0, rain: 0, extinct: 0, ...ambOverride }), full = resolveSodium({ rain: 0, ...ambOverride }), rows = [];
  for (let k = 0; k <= steps; k++) { const u = k / steps, P = [STRIP.x0 + u * (STRIP.x1 - STRIP.x0), STREET.kerb, STRIP.z0 + u * (STRIP.z1 - STRIP.z0)]; const f = share(P); rows.push({ d: u * len, f, f_broadband: bbShare, S_direct: probe(P, true, direct), S_model: probe(P, false, direct), S_full: probe(P, true, full) }); }
  const firstAt = (key, thr) => { for (let k = 1; k < rows.length; k++) if (rows[k][key] >= thr) { const a = rows[k - 1], b = rows[k]; return a.d + ((thr - a[key]) / (b[key] - a[key])) * (b.d - a.d); } return null; };
  const d50_bb_explore = firstAt("f_broadband", res.Q4.f50), d50_pred = firstAt("f", res.Q4.f50), d50_direct = firstAt("S_direct", 0.5 * S1), d50_full = firstAt("S_full", 0.5 * S1), d50_model = firstAt("S_model", 0.5 * S1);
  res.Q5 = { t, strip_length_m: len, d50_predicted: d50_pred, d50_model_shading: d50_model, d50_direct, d50_full_report_only: d50_full, d50_from_all_broadband_explore: d50_bb_explore, S_at_lamp: rows[0].S_direct, S_at_shop: rows[rows.length - 1].S_direct, rows: rows.filter((_, k) => k % 4 === 0),
    pass: d50_pred !== null && d50_direct !== null && Math.abs(d50_direct - d50_pred) <= proto.Q5_distance.max_abs_error_m };
}
res.verdicts = { Q1: res.Q1.pass, Q2: res.Q2.pass, Q3: res.Q3.pass, Q4: res.Q4.pass, Q5: res.Q5.pass };
writeFileSync(out, JSON.stringify(res, null, 1) + "\n");
console.log(JSON.stringify({ Q1: res.Q1, Q2: res.Q2, Q3: res.Q3, Q4: { monotone: res.Q4.monotone, half: res.Q4.S_half_over_S_one, f50: res.Q4.f50 }, Q5: { ...res.Q5, rows: undefined }, verdicts: res.verdicts }, null, 1));
