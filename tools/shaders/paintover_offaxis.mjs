// Off-axis views of paint-over projection and the secondary-projector search
// (project-docs/PAINT-OVER-PROTOCOL.md, part 2).
//   node tools/shaders/paintover_offaxis.mjs --out evidence/paintover-offaxis-runs.json [--save DIR]
import { writeFileSync, mkdirSync, readFileSync } from "node:fs";
import { camAt, PW, PH, VW, VH, paintProjector, renderGbufs, covered, makeProjector, renderProjected, shadePoint, analyseView, stretchStats } from "./paintover_lib.mjs";
import { groundStretch } from "./paintover_prior.mjs";
import { writePng } from "./png.mjs";

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const proto = JSON.parse(readFileSync(new URL("../../evidence/paintover-protocol.json", import.meta.url))), O = proto.offaxis, S = O.secondaries;
const IDS = { ground: 0, wall: 2 };

// ---- render everything that is independent in one pass ----
const views = [...O.yaw_offsets.map((y) => ({ key: `yaw${y}`, dyaw: y, delev: 0 })), ...O.elev_offsets_at_yaw0.map((e) => ({ key: `elev${e}`, dyaw: 0, delev: e }))];
const required = S.required_yaw.flatMap((y) => S.required_elev.map((e) => ({ dyaw: y, delev: e }))), cands = S.candidate_yaw.flatMap((y) => S.candidate_elev.map((e) => ({ dyaw: y, delev: e })));
const tasks = [{ cam: camAt(0), w: PW, h: PH }, ...views.map((v) => ({ cam: camAt(v.dyaw, v.delev), w: VW, h: VH, shadeOn: false })), ...required.map((v) => ({ cam: camAt(v.dyaw, v.delev), w: VW, h: VH, shadeOn: false })), ...cands.map((v) => ({ cam: camAt(v.dyaw, v.delev), w: PW, h: PH, shadeOn: false }))];
console.log(`rendering ${tasks.length} G-buffers`);
const gs = await renderGbufs(tasks), gPrim = gs[0], gViews = gs.slice(1, 1 + views.length), gReq = gs.slice(1 + views.length, 1 + views.length + required.length), gCand = gs.slice(1 + views.length + required.length);
const { projector: primary } = paintProjector(camAt(0), gPrim, proto.primary.seed);

// ---- greedy secondary search over the points the primary misses in the required views ----
const pts = new Map();
for (const g of gReq) for (let i = 0; i < g.w * g.h; i++) {
  if (!g.hit[i]) continue;
  const P = [g.P[i * 3], g.P[i * 3 + 1], g.P[i * 3 + 2]], N = [g.N[i * 3], g.N[i * 3 + 1], g.N[i * 3 + 2]];
  if (covered(primary, P, N)) continue;
  const key = `${Math.round(P[0] / 0.08)},${Math.round(P[1] / 0.08)},${Math.round(P[2] / 0.08)}`;
  if (!pts.has(key)) pts.set(key, { P, N });
}
let remaining = [...pts.values()];
const candProj = cands.map((c, k) => makeProjector(camAt(c.dyaw, c.delev), PW, PH, new Uint8Array(PW * PH * 4), gCand[k].depth));
const chosen = [], startCount = remaining.length;
for (let round = 0; round < S.count; round++) {
  let best = -1, bestGain = -1;
  candProj.forEach((cp, k) => { if (chosen.some((c) => c.k === k)) return; let gain = 0; for (const p of remaining) if (covered(cp, p.P, p.N)) gain++; if (gain > bestGain) { bestGain = gain; best = k; } });
  chosen.push({ k: best, ...cands[best], gain: bestGain, gain_fraction_of_start: bestGain / startCount });
  remaining = remaining.filter((p) => !covered(candProj[best], p.P, p.N));
  console.log(`secondary ${round + 1}: yaw ${cands[best].dyaw} elev ${cands[best].delev}, covers ${bestGain} of ${startCount} missed points`);
}
// paint the chosen secondaries over their own renders
const [s1, s2] = await renderGbufs(chosen.map((c) => ({ cam: camAt(c.dyaw, c.delev), w: PW, h: PH })));
const secondary = chosen.map((c, k) => paintProjector(camAt(c.dyaw, c.delev), [s1, s2][k], S.seeds[k]).projector), three = [primary, ...secondary];

// ---- per-view numbers ----
const result = {}, checks = [], add = (id, pass, detail) => { checks.push({ id, pass, detail }); console.log(pass ? "ok  " : "MISS", id, detail); };
views.forEach((v, k) => {
  const g = gViews[k], a1 = analyseView([primary], g, camAt(v.dyaw, v.delev)), a3 = analyseView(three, g, camAt(v.dyaw, v.delev)), st = stretchStats(primary, g, IDS);
  result[v.key] = { holes_primary: a1.holes, holes_three: a3.holes, blend_three: a3.blend, stretch: st };
  console.log(v.key, `holes ${a1.holes.toFixed(4)} -> ${a3.holes.toFixed(4)}; ground M ${st.ground.M_mean.toFixed(3)} A ${st.ground.A_mean.toFixed(3)}; wall M ${st.wall.M_mean.toFixed(3)} A ${st.wall.A_mean.toFixed(3)}`);
});
// ---- checks against the predictions ----
for (const th of [0, 5, 10, 20, 30, 45, 60]) {
  const m = result[`yaw${th}`].stretch.ground, p = groundStretch(th), tol = O.predictions.P1_ground_stretch_rel;
  add(`P1 ground M yaw${th}`, Math.abs(m.M_mean / p.magnification - 1) <= tol, `measured ${m.M_mean.toFixed(3)} model ${p.magnification.toFixed(3)}`);
  add(`P1 ground A yaw${th}`, Math.abs(m.A_mean / p.anisotropy - 1) <= tol, `measured ${m.A_mean.toFixed(3)} model ${p.anisotropy.toFixed(3)}`);
}
for (const de of [-10, 10]) { const m = result[`elev${de}`].stretch.ground, p = groundStretch(0, de); add(`P1 ground M elev${de}`, Math.abs(m.M_mean / p.magnification - 1) <= 0.03, `measured ${m.M_mean.toFixed(3)} model ${p.magnification.toFixed(3)}`); add(`P1 ground A elev${de}`, Math.abs(m.A_mean / p.anisotropy - 1) <= 0.03, `measured ${m.A_mean.toFixed(3)} model ${p.anisotropy.toFixed(3)}`); }
const P2 = O.predictions.P2_holes_primary_only;
for (const th of ["5", "10", "20", "30"]) add(`P2 holes yaw${th}`, result[`yaw${th}`].holes_primary <= P2[th], `${result[`yaw${th}`].holes_primary.toFixed(4)} <= ${P2[th]}`);
add("P2 holes yaw90", result.yaw90.holes_primary >= P2["90_min"], `${result.yaw90.holes_primary.toFixed(4)} >= ${P2["90_min"]}`);
const hp = O.yaw_offsets.map((y) => result[`yaw${y}`].holes_primary); add("P2 monotone", hp.every((v, i) => i === 0 || v >= hp[i - 1] - P2.monotone_slack), hp.map((v) => v.toFixed(3)).join(" "));
add("P3 secondaries at yaw30", result.yaw30.holes_three <= O.predictions.P3_secondary_hole_ratio_max["30"] * result.yaw30.holes_primary, `${result.yaw30.holes_three.toFixed(4)} vs ${(O.predictions.P3_secondary_hole_ratio_max["30"] * result.yaw30.holes_primary).toFixed(4)}`);
add("P3 secondaries at yaw90", result.yaw90.holes_three <= O.predictions.P3_secondary_hole_ratio_max["90"] * result.yaw90.holes_primary, `${result.yaw90.holes_three.toFixed(4)} vs ${(O.predictions.P3_secondary_hole_ratio_max["90"] * result.yaw90.holes_primary).toFixed(4)}`);
const yawFromPrimary = chosen.map((c) => Math.abs(c.dyaw)), sep = Math.abs(chosen[0].dyaw - chosen[1].dyaw);
add("P4 secondary yaw", yawFromPrimary.every((y) => y >= O.predictions.P4_secondary_yaw_from_primary_min), `yaw offsets ${chosen.map((c) => c.dyaw).join(", ")}`);
add("P4 separation", sep >= O.predictions.P4_secondary_separation_min, `separation ${sep}`);
const w30 = result.yaw30.stretch.wall; add("P5 walls yaw30", w30.M_mean <= O.predictions.P5_walls_yaw30.M_max && w30.A_mean <= O.predictions.P5_walls_yaw30.A_max, `M ${w30.M_mean.toFixed(3)} A ${w30.A_mean.toFixed(3)}`);

// ---- P6: a view at the primary with a secondary 60 degrees away ----
{
  const sec60 = candProj.findIndex((_, k) => cands[k].dyaw === 60 && cands[k].delev === 0), [g60] = await renderGbufs([{ cam: camAt(60, 0), w: PW, h: PH }]);
  const p60 = paintProjector(camAt(60, 0), g60, S.seeds[0]).projector, [gv] = await renderGbufs([{ cam: camAt(0), w: PW, h: PH, shadeOn: false }]), r = renderProjected([primary, p60], gv, camAt(0));
  let max = 0, sum = 0, n = 0;
  for (let i = 0; i < gv.w * gv.h; i++) { if (!gv.hit[i] || !r.covered[i]) continue; n++; for (let c = 0; c < 3; c++) { const d = Math.abs(Math.round(Math.min(1, Math.max(0, r.rgb[i * 3 + c])) * 255) - primary.rgba[i * 4 + c]); max = Math.max(max, d); sum += d; } }
  add("P6 blend contamination at the primary", max <= proto.roundtrip.bounds.max_abs_code && sum / (n * 3) <= proto.roundtrip.bounds.mean_abs_code, `max ${max} mean ${(sum / (n * 3)).toFixed(5)} (candidate index ${sec60} unused: cameras built directly)`);
}
// ---- pop test: colour change at one world point per degree of view yaw (the default rule) ----
let popMax = 0, popP999 = [];
for (const base of [0, 10, 20, 30, 40, 50]) {
  const [g] = await renderGbufs([{ cam: camAt(base, 0), w: VW, h: VH, shadeOn: false }]), cA = camAt(base, 0), cB = camAt(base + 1, 0), d = [];
  for (let i = 0; i < g.w * g.h; i += 3) {
    if (!g.hit[i]) continue;
    const P = [g.P[i * 3], g.P[i * 3 + 1], g.P[i * 3 + 2]], N = [g.N[i * 3], g.N[i * 3 + 1], g.N[i * 3 + 2]], a = shadePoint(three, P, N, cA), b = shadePoint(three, P, N, cB);
    if (!a.rgb || !b.rgb) continue;
    d.push(Math.max(...[0, 1, 2].map((c) => Math.abs(a.rgb[c] - b.rgb[c]) * 255)));
  }
  d.sort((x, y) => x - y); popMax = Math.max(popMax, d[d.length - 1]); popP999.push(d[Math.floor(0.999 * d.length)]);
}
add("default rule: no pop above 8 codes per degree (max over views)", popMax <= 8, `max ${popMax.toFixed(2)}, p99.9 per base view ${popP999.map((v) => v.toFixed(2)).join(" ")}`);

const out = opt("--out", null), sv = opt("--save", null);
if (sv) {
  mkdirSync(sv, { recursive: true });
  for (const th of [0, 20, 45, 90]) {
    const k = views.findIndex((v) => v.key === `yaw${th}`), g = gViews[k];
    for (const [name, projs] of [["primary", [primary]], ["three", three]]) {
      const r = renderProjected(projs, g, camAt(th, 0)), a = new Uint8ClampedArray(g.w * g.h * 4);
      for (let i = 0; i < g.w * g.h; i++) { const hole = g.hit[i] && !r.covered[i]; for (let c = 0; c < 3; c++) a[i * 4 + c] = hole ? [255, 0, 255][c] : Math.round(Math.min(1, Math.max(0, r.rgb[i * 3 + c])) * 255); a[i * 4 + 3] = 255; }
      writePng(`${sv}/yaw${th}-${name}.png`, { width: g.w, height: g.h, data: a });
    }
  }
}
if (out) writeFileSync(out, JSON.stringify({ protocol: "evidence/paintover-protocol.json", views: result, secondaries: chosen.map((c) => ({ dyaw: c.dyaw, delev: c.delev, gain: c.gain, gain_fraction: c.gain_fraction_of_start })), missed_points_start: startCount, missed_points_left: remaining.length, pop: { max: popMax, p999: popP999 }, checks }, null, 1) + "\n");
