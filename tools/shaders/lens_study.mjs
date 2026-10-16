// Lens suite study: runs the predictions of project-docs/LENS-SUITE-PROTOCOL.md (fixed in
// evidence/lens-protocol.json before this tool existed) and writes evidence/lens-study-runs.json.
//   node tools/shaders/lens_study.mjs [--out evidence/lens-study-runs.json]
import { readFileSync, writeFileSync } from "node:fs";
import { resolveLens, extractSources, prepareSources, runLens } from "../../web/shaders/lens/lens.mjs";
import { psfMono, countSpikes, angularHarmonics } from "../../web/shaders/lens/aperture.mjs";
import { filmReflectance, quarterWaveMinimum } from "../../web/shaders/lens/coating.mjs";
import { ghostTable, firstOrder } from "../../web/shaders/lens/prescription.mjs";
import { traceExact, chiefRay } from "../../web/shaders/lens/exact.mjs";
import { BINS, BIN_NM, BIN_RGB, LAMBDA_D } from "../../web/shaders/lens/spectrum.mjs";

const args = process.argv.slice(2), outPath = args.includes("--out") ? args[args.indexOf("--out") + 1] : "evidence/lens-study-runs.json";
const proto = JSON.parse(readFileSync("evidence/lens-protocol.json", "utf8")), res = { protocol: "evidence/lens-protocol.json", date: new Date().toISOString().slice(0, 10) };
const blank = (W, H) => ({ width: W, height: H, data: new Float64Array(W * H * 4) });
const total = (f) => { const s = [0, 0, 0]; for (let i = 0; i < f.data.length; i += 4) for (let k = 0; k < 3; k++) s[k] += f.data[i + k]; return s; };

// P1 energy and control C1
{
  const c = proto.P1_energy, [W, H] = c.frame, f = blank(W, H), e = c.source.E.map((v) => v / 9);
  for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) { const i = ((c.source.y + dy) * W + c.source.x + dx) * 4; f.data[i] = e[0]; f.data[i + 1] = e[1]; f.data[i + 2] = e[2]; }
  const tin = total(f), run = (extra, keepOn) => {
    const plan = resolveLens({ ...c.params, ...extra }, { width: W, height: H }), src = extractSources(plan, f);
    const out = runLens(plan, f, prepareSources(plan, src.list), keepOn ? src.keep : null), t = total(out);
    return { rel: t.map((v, k) => v / tin[k] - 1), phi: plan.phi, sources: src.list.length };
  };
  const base = run({ caAmount: 0 }, true), disp = run(c.with_dispersion, true), ctl = run({ caAmount: 0 }, false);
  res.P1 = { input: tin, base, with_dispersion: disp, control_C1: ctl,
    pass: base.rel.every((v) => Math.abs(v) <= c.max_rel_error) && disp.rel.every((v) => Math.abs(v) <= c.max_rel_error),
    control_failed_as_required: ctl.rel.some((v) => Math.abs(v) > c.max_rel_error),
    control_in_predicted_range: ctl.rel.every((v) => v >= c.control_C1.predicted_excess[0] && v <= c.control_C1.predicted_excess[1]) };
  console.log("P1", JSON.stringify(res.P1));
}
// P2 spikes
{
  const c = proto.P2_spikes, got = {};
  for (const [k, want] of Object.entries(c.expect)) {
    const iris = k === "round" ? { blades: 6, roundness: 1, rotation: 0.3 } : { blades: +k, roundness: 0.15, rotation: 0.3 };
    got[k] = { want, got: countSpikes(psfMono(iris, c.n, c.rad), c.n, c.ring, { prominence: c.prominence }) };
  }
  res.P2 = { got, pass: Object.values(got).every((g) => g.want === g.got) };
  console.log("P2", JSON.stringify(res.P2));
}
// P2b (addendum A): dominant angular harmonic over a radial band
{
  const c = proto.P2b_addendum_A, got = {};
  for (const k of [...Object.keys(c.expect_dominant), "round"]) {
    const iris = k === "round" ? { blades: 6, roundness: 1, rotation: 0.3 } : { blades: +k, roundness: 0.15, rotation: 0.3 };
    const amp = angularHarmonics(psfMono(iris, proto.P2_spikes.n, proto.P2_spikes.rad), proto.P2_spikes.n, c.band_cells, { samples: c.samples, kMax: c.harmonics[1] });
    let kd = 1; for (let h = 1; h < amp.length; h++) if (amp[h] > amp[kd]) kd = h;
    const ok = k === "round" ? Math.max(...amp.slice(1)) < c.round_max_over_mean : kd === c.expect_dominant[k] && amp[kd] >= c.min_dominant_over_mean;
    got[k] = { want: c.expect_dominant[k] ?? "none", dominant: kd, amp: +amp[kd].toFixed(4), max_other: +Math.max(...amp.slice(1).filter((_, h) => h + 1 !== kd)).toFixed(4), ok };
  }
  res.P2b = { got, pass: Object.values(got).every((g) => g.ok) };
  console.log("P2b", JSON.stringify(res.P2b));
}
// P3 ghost positions, paraxial against exact
{
  const c = proto.P3_ghosts, gt = ghostTable(undefined, [LAMBDA_D]), { f } = firstOrder(), [W, H] = c.frame, D = Math.hypot(W, H) / 2, pxPerMm = D / 21.63;
  const at = (deg) => {
    const th = (deg * Math.PI) / 180, chief = chiefRay(th), yMain = traceExact({ ...chief }), yPar = f * Math.tan(th);
    return gt.ghosts.map((g) => { const ex = traceExact({ ...chief }, { i: g.i, j: g.j }), par = g.m[0] * yPar; return { i: g.i, j: g.j, exact_mm: ex, paraxial_mm: par, err_px: Math.abs(ex - par) * pxPerMm }; }).concat([{ main: true, exact_mm: yMain, paraxial_mm: yPar, err_px: Math.abs(yMain - yPar) * pxPerMm }]);
  };
  const small = at(c.field_deg), big = at(c.exploratory_field_deg);
  res.P3 = { note: "both at the d line, 587.6 nm", small, max_px_small: Math.max(...small.map((g) => g.err_px)), big, max_px_big: Math.max(...big.map((g) => g.err_px)) };
  res.P3.pass = res.P3.max_px_small <= c.max_px;
  console.log("P3 small max px", res.P3.max_px_small.toFixed(4), "big max px", res.P3.max_px_big.toFixed(2));
}
// P4 coating
{
  const c = proto.P4_coating, r550 = filmReflectance(550, 0), closed = quarterWaveMinimum(), r420 = filmReflectance(420, 0), r700 = filmReflectance(700, 0);
  let best = { nm: 0, R: 1 }; for (let nm = 400; nm <= 700; nm += 0.25) { const R = filmReflectance(nm, Math.PI / 6); if (R < best.R) best = { nm, R }; }
  let best0 = { nm: 0, R: 1 }; for (let nm = 400; nm <= 700; nm += 0.25) { const R = filmReflectance(nm, 0); if (R < best0.R) best0 = { nm, R }; }
  res.P4 = { r550, closed, r420, r700, min_0deg_nm: best0.nm, min_30deg_nm: best.nm,
    pass: Math.abs(r550 - closed) <= c.tol && r420 > c.ends_over_centre * r550 && r700 > c.ends_over_centre * r550 && best.nm >= c.min_at_30deg_nm[0] && best.nm <= c.min_at_30deg_nm[1] };
  console.log("P4", JSON.stringify(res.P4));
}
// P5 dispersion
{
  const c = proto.P5_dispersion, W = 384, H = 216, f = blank(W, H), D = Math.hypot(W, H) / 2, ang = Math.atan2(H, W);
  const px = Math.floor(W / 2 + c.radius_of_half_diag * D * Math.cos(ang)), py = Math.floor(H / 2 + c.radius_of_half_diag * D * Math.sin(ang)), i0 = (py * W + px) * 4;
  f.data[i0] = f.data[i0 + 1] = f.data[i0 + 2] = 1;
  const plan = resolveLens({ caAmount: c.caAmount, threshold: 1e9 }, { width: W, height: H }), out = runLens(plan, f, [], null);
  const cen = [0, 1, 2].map((k) => { let s = 0, sx = 0, sy = 0; for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const v = out.data[(y * W + x) * 4 + k]; s += v; sx += v * (x + 0.5); sy += v * (y + 0.5); } return [sx / s, sy / s]; });
  const rad = (p) => Math.hypot(p[0] - W / 2, p[1] - H / 2), r0 = Math.hypot(px + 0.5 - W / 2, py + 0.5 - H / 2);
  const pred = (k) => r0 * BIN_RGB.reduce((s, w, l) => s + w[k] * plan.caScale[l], 0);
  const measured = rad(cen[0]) - rad(cen[2]), predicted = pred(0) - pred(2);
  res.P5 = { r0, measured, predicted, err: Math.abs(measured - predicted), pass: Math.abs(measured - predicted) <= c.max_px };
  console.log("P5", JSON.stringify(res.P5));
}
// P6 halation
{
  const c = proto.P6_halation, W = 257, H = 257, f = blank(W, H), i0 = (128 * W + 128) * 4;
  f.data[i0] = f.data[i0 + 1] = f.data[i0 + 2] = 100;
  const plan = resolveLens({ refHeight: H, ghostGain: 0, streakGain: 0, starGain: 0, caAmount: 0 }, { width: W, height: H }), src = extractSources(plan, f);
  const out = runLens(plan, f, prepareSources(plan, src.list), null), tin = total(f), t = total(out), added = t.map((v, k) => v - tin[k]);
  const pred = [0, 1, 2].map((k) => { let s = 0; for (let l = 0; l < BINS; l++) s += BIN_RGB[l][k] * plan.halo[l][0]; return s; });
  const ratio = added[0] / added[2], predRatio = pred[0] / pred[2];
  res.P6 = { added, pred_fraction: pred, ratio, predRatio, pass: added[0] > added[1] && added[1] > added[2] && Math.abs(ratio / predRatio - 1) <= c.ratio_RB_tol };
  console.log("P6", JSON.stringify(res.P6));
}
res.bins_nm = BIN_NM;
writeFileSync(outPath, JSON.stringify(res, null, 1) + "\n");
console.log("written", outPath);
