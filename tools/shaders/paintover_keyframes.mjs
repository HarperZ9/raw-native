// Second paint-over run: keyframe windows, hole-filling secondaries, overscan
// (project-docs/PAINT-OVER-PROTOCOL.md, Addendum A; bounds in evidence/paintover-keyframes-protocol.json).
//   node tools/shaders/paintover_keyframes.mjs --out evidence/paintover-keyframes-runs.json [--save DIR] [--projectors DIR]
import { writeFileSync, mkdirSync, readFileSync } from "node:fs";
import { isoCamera } from "../../web/shaders/paintover/camera.mjs";
import { makeProjector, covered, shadePoint, renderProjected, KEYFRAME } from "../../web/shaders/paintover/projector.mjs";
import { paint } from "../../web/shaders/paintover/painter.mjs";
import { renderGbufs } from "./paintover_pool.mjs";
import { writePng } from "./png.mjs";

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const proto = JSON.parse(readFileSync(new URL("../../evidence/paintover-keyframes-protocol.json", import.meta.url))), old = JSON.parse(readFileSync(new URL("../../evidence/paintover-protocol.json", import.meta.url)));
const BW = 1152, BH = 648, SW = 960, SH = 540, OX = 96, OY = 54, VW = 480, VH = 270, TARGET = [1, 1.5, 0];
const camScreen = (dy, de = 0) => isoCamera({ yawDeg: 45 + dy, elevDeg: 35.264 + de, target: TARGET, scale: 9 });
const camPaint = (dy, de = 0) => isoCamera({ yawDeg: 45 + dy, elevDeg: 35.264 + de, target: TARGET, scale: 10.8 });
const O = old.offaxis, S = O.secondaries, B = proto.bounds, checks = [], add = (id, pass, detail) => { checks.push({ id, pass, detail }); console.log(pass ? "ok  " : "MISS", id, detail); };
const rgb8 = (v) => Math.round(Math.min(1, Math.max(0, v)) * 255);

// ---- renders ----
const views = [...O.yaw_offsets.map((y) => ({ key: `yaw${y}`, dyaw: y, delev: 0 })), ...O.elev_offsets_at_yaw0.map((e) => ({ key: `elev${e}`, dyaw: 0, delev: e }))];
const required = S.required_yaw.flatMap((y) => S.required_elev.map((e) => ({ dyaw: y, delev: e }))), cands = S.candidate_yaw.flatMap((y) => S.candidate_elev.map((e) => ({ dyaw: y, delev: e })));
const popYaws = B.K7.yaw_offsets;
const tasks = [{ cam: camPaint(0), w: BW, h: BH }, { cam: camScreen(0), w: SW, h: SH, shadeOn: false }, { cam: { ...camScreen(0), pos: camScreen(0).pos.map((v, i) => v + camScreen(0).right[i] * ((2 * 9) / SH)) }, w: SW, h: SH, shadeOn: false },
  ...views.map((v) => ({ cam: camScreen(v.dyaw, v.delev), w: VW, h: VH, shadeOn: false })), ...required.map((v) => ({ cam: camScreen(v.dyaw, v.delev), w: VW, h: VH, shadeOn: false })),
  ...cands.map((v) => ({ cam: camPaint(v.dyaw, v.delev), w: BW, h: BH, shadeOn: false })), ...popYaws.map((y) => ({ cam: camScreen(y), w: VW, h: VH, shadeOn: false }))];
console.log(`rendering ${tasks.length} G-buffers`);
const gs = await renderGbufs(tasks);
let at = 0; const take = (n) => { const r = gs.slice(at, at + n); at += n; return r; };
const [gPrim] = take(1), [gScreen0] = take(1), [gShift] = take(1), gViews = take(views.length), gReq = take(required.length), gCand = take(cands.length), gPop = take(popYaws.length);

// ---- primary painting (overscan) ----
const paintOn = (g, seed, mask = null, srcRgb = null) => paint({ w: BW, h: BH, rgb: srcRgb || g.shade }, { id: g.id, N: g.N }, { seed, mask });
const primaryPaint = paintOn(gPrim, old.primary.seed), primary = makeProjector(camPaint(0), BW, BH, primaryPaint.rgba8, gPrim.depth);

// ---- K2: round trip of the overscan primary alone ----
function compareAt(projectors, g, viewCam, projIndex, opts = KEYFRAME) {
  const r = renderProjected(projectors, g, viewCam, { opt: opts }), pr = projectors[projIndex], P = [0, 0, 0], N = [0, 0, 0];
  let n = 0, hit = 0, cov = 0, sum = 0, max = 0;
  for (let i = 0; i < g.w * g.h; i++) {
    if (!g.hit[i]) continue;
    hit++; if (r.covered[i]) cov++;
    for (let c = 0; c < 3; c++) { P[c] = g.P[i * 3 + c]; N[c] = g.N[i * 3 + c]; }
    if (!r.covered[i] || !covered(pr, P, N, opts)) continue;
    n++;
    const x = i % g.w, y = (i / g.w) | 0, j = (y + OY) * pr.w + (x + OX);
    for (let c = 0; c < 3; c++) { const d = Math.abs(rgb8(r.rgb[i * 3 + c]) - pr.rgba[j * 4 + c]); sum += d; if (d > max) max = d; }
  }
  return { covered_fraction: cov / hit, max_abs_code: max, mean_abs_code: sum / (n * 3 || 1), compared: n, image: r };
}
const rt = compareAt([primary], gScreen0, camScreen(0), 0);
add("K2 max", rt.max_abs_code <= B.K2.max_abs_code, `max ${rt.max_abs_code}`); add("K2 mean", rt.mean_abs_code <= B.K2.mean_abs_code, `mean ${rt.mean_abs_code.toFixed(5)}`);
add("K2 covered", rt.covered_fraction >= B.K2.covered_fraction_min, `covered ${rt.covered_fraction.toFixed(5)}`);
const sh = compareAt([primary], gShift, { ...camScreen(0), pos: gShift ? camScreen(0).pos.map((v, i) => v + camScreen(0).right[i] * ((2 * 9) / SH)) : null }, 0);
add("K2 shift control", sh.mean_abs_code >= B.K2.shift_one_pixel_mean_min, `mean ${sh.mean_abs_code.toFixed(3)}`);
const wd = compareAt([makeProjector(camPaint(0), BW, BH, primaryPaint.rgba8, gPrim.depth.map((d) => d - 1))], gScreen0, camScreen(0), 0);
add("K2 wrong-depth control", wd.covered_fraction <= B.K2.wrong_depth_covered_max, `covered ${wd.covered_fraction.toFixed(5)}`);

// ---- secondaries: greedy choice, then hole-filling paintings ----
const pts = new Map();
for (const g of gReq) for (let i = 0; i < g.w * g.h; i++) {
  if (!g.hit[i]) continue;
  const P = [g.P[i * 3], g.P[i * 3 + 1], g.P[i * 3 + 2]], N = [g.N[i * 3], g.N[i * 3 + 1], g.N[i * 3 + 2]];
  if (covered(primary, P, N)) continue;
  const key = `${Math.round(P[0] / 0.08)},${Math.round(P[1] / 0.08)},${Math.round(P[2] / 0.08)}`; if (!pts.has(key)) pts.set(key, { P, N });
}
let remaining = [...pts.values()]; const start = remaining.length, chosen = [], candProj = cands.map((c, k) => makeProjector(camPaint(c.dyaw, c.delev), BW, BH, new Uint8Array(4), gCand[k].depth));
for (let round = 0; round < S.count; round++) {
  let best = -1, bg = -1;
  candProj.forEach((cp, k) => { if (chosen.some((c) => c.k === k)) return; let gain = 0; for (const p of remaining) if (covered(cp, p.P, p.N)) gain++; if (gain > bg) { bg = gain; best = k; } });
  chosen.push({ k: best, ...cands[best], gain: bg }); remaining = remaining.filter((p) => !covered(candProj[best], p.P, p.N));
  console.log(`secondary ${round + 1}: yaw ${cands[best].dyaw} elev ${cands[best].delev}, covers ${bg} of ${start}`);
}
const projectors = [primary], secondaryInfo = [];
for (let s = 0; s < chosen.length; s++) {
  const c = chosen[s], [g] = await renderGbufs([{ cam: camPaint(c.dyaw, c.delev), w: BW, h: BH }]), cam = camPaint(c.dyaw, c.delev), n = BW * BH, comp = new Float32Array(n * 3), holes = new Uint8Array(n), P = [0, 0, 0], N = [0, 0, 0];
  for (let i = 0; i < n; i++) {
    for (let k = 0; k < 3; k++) comp[i * 3 + k] = g.shade[i * 3 + k];
    if (!g.hit[i]) continue;
    for (let k = 0; k < 3; k++) { P[k] = g.P[i * 3 + k]; N[k] = g.N[i * 3 + k]; }
    const sp = shadePoint(projectors, P, N, cam, KEYFRAME);
    if (sp.rgb) { for (let k = 0; k < 3; k++) comp[i * 3 + k] = sp.rgb[k]; } else holes[i] = 1;
  }
  const dil = (m, r) => { const o = new Uint8Array(n); for (let y = 0; y < BH; y++) for (let x = 0; x < BW; x++) { if (!m[y * BW + x]) continue; for (let dy = -r; dy <= r; dy++) for (let dx = -r; dx <= r; dx++) { const X = x + dx, Y = y + dy; if (X >= 0 && Y >= 0 && X < BW && Y < BH) o[Y * BW + X] = 1; } } return o; };
  const mask = dil(holes, 2), painting = paintOn(g, S.seeds[s], mask, comp);
  // K5: copy exactness away from the holes
  const far = dil(holes, B.K5.ring_px); let max5 = 0, n5 = 0;
  for (let i = 0; i < n; i++) { if (!g.hit[i] || far[i]) continue; n5++; for (let k = 0; k < 3; k++) max5 = Math.max(max5, Math.abs(painting.rgba8[i * 4 + k] - rgb8(comp[i * 3 + k]))); }
  add(`K5 copy exactness secondary ${s + 1}`, max5 <= B.K5.max_abs_code, `max ${max5} over ${n5} pixels, hole fraction ${(holes.reduce((a, b) => a + b, 0) / n).toFixed(4)}`);
  projectors.push(makeProjector(cam, BW, BH, painting.rgba8, g.depth)); secondaryInfo.push({ dyaw: c.dyaw, delev: c.delev, painting });
}

// ---- K1: keyframe exactness from each projector's own camera ----
const keyTasks = chosen.map((c) => ({ cam: camScreen(c.dyaw, c.delev), w: SW, h: SH, shadeOn: false })), gKey = await renderGbufs(keyTasks);
[[camScreen(0), gScreen0, 0], ...chosen.map((c, k) => [camScreen(c.dyaw, c.delev), gKey[k], k + 1])].forEach(([cam, g, idx]) => {
  const r = compareAt(projectors, g, cam, idx);
  add(`K1 keyframe ${idx} (yaw ${idx ? chosen[idx - 1].dyaw : 0}, elev ${idx ? chosen[idx - 1].delev : 0})`, r.max_abs_code <= B.K1.max_abs_code && r.mean_abs_code <= B.K1.mean_abs_code, `max ${r.max_abs_code} mean ${r.mean_abs_code.toFixed(5)} over ${r.compared} pixels`);
});

// ---- K3/K4: holes ----
function holesOf(set, g) { let hit = 0, h = 0, blend = 0; const P = [0, 0, 0], N = [0, 0, 0]; for (let i = 0; i < g.w * g.h; i++) { if (!g.hit[i]) continue; hit++; for (let c = 0; c < 3; c++) { P[c] = g.P[i * 3 + c]; N[c] = g.N[i * 3 + c]; } let k = 0; for (const pr of set) if (covered(pr, P, N)) k++; if (!k) h++; else if (k > 1) blend++; } return { holes: h / hit, blend: blend / hit }; }
const result = {};
views.forEach((v, k) => { const a = holesOf([primary], gViews[k]), b = holesOf(projectors, gViews[k]); result[v.key] = { holes_primary: a.holes, holes_three: b.holes, blend_three: b.blend }; console.log(v.key, `holes ${a.holes.toFixed(4)} -> ${b.holes.toFixed(4)}`); });
for (const th of ["5", "10", "20", "30"]) add(`K3 holes yaw${th}`, result[`yaw${th}`].holes_primary <= B.K3.holes_primary_max[th], `${result[`yaw${th}`].holes_primary.toFixed(4)} <= ${B.K3.holes_primary_max[th]}`);
add("K3 holes yaw90", result.yaw90.holes_primary >= B.K3.holes_primary_90_min, `${result.yaw90.holes_primary.toFixed(4)} >= ${B.K3.holes_primary_90_min}`);
for (const th of ["30", "90"]) add(`K4 ratio yaw${th}`, result[`yaw${th}`].holes_three <= B.K4.ratio_max[th] * result[`yaw${th}`].holes_primary, `${result[`yaw${th}`].holes_three.toFixed(4)} vs ${(B.K4.ratio_max[th] * result[`yaw${th}`].holes_primary).toFixed(4)}`);
for (const th of ["20", "30"]) add(`K4 absolute yaw${th}`, result[`yaw${th}`].holes_three <= B.K4.absolute_max[th], `${result[`yaw${th}`].holes_three.toFixed(4)} <= ${B.K4.absolute_max[th]}`);

// ---- K6: seam between projectors at yaw +-15 ----
{
  const diffs = [];
  const [gA, gB] = await renderGbufs([{ cam: camScreen(15), w: VW, h: VH, shadeOn: false }, { cam: camScreen(-15), w: VW, h: VH, shadeOn: false }]), P = [0, 0, 0], N = [0, 0, 0];
  for (const g of [gA, gB]) for (let i = 0; i < g.w * g.h; i += 2) {
    if (!g.hit[i]) continue; for (let c = 0; c < 3; c++) { P[c] = g.P[i * 3 + c]; N[c] = g.N[i * 3 + c]; }
    const cs = projectors.map((pr) => covered(pr, P, N)); const idx = cs.map((c, k) => (c ? k : -1)).filter((k) => k >= 0);
    if (idx.length < 2) continue;
    const col = idx.map((k) => { const pr = projectors[k]; const c = cs[k]; const x = Math.round(c.px), y = Math.round(c.py), j = (Math.min(pr.h - 1, Math.max(0, y)) * pr.w + Math.min(pr.w - 1, Math.max(0, x))) * 4; return [pr.rgba[j], pr.rgba[j + 1], pr.rgba[j + 2]]; });
    diffs.push(Math.max(...[0, 1, 2].map((c) => Math.abs(col[0][c] - col[1][c]))));
  }
  diffs.sort((a, b) => a - b); const med = diffs[Math.floor(diffs.length / 2)], p95 = diffs[Math.floor(0.95 * diffs.length)];
  add("K6 seam median", med <= B.K6.median_max, `median ${med} over ${diffs.length} overlap pixels`); add("K6 seam p95", p95 <= B.K6.p95_max, `p95 ${p95}`);
}
// ---- K7: pop per degree ----
{
  let popMax = 0; const p999 = [], P = [0, 0, 0], N = [0, 0, 0];
  popYaws.forEach((y, k) => {
    const g = gPop[k], cA = camScreen(y), cB = camScreen(y + 1), d = [];
    for (let i = 0; i < g.w * g.h; i += 2) { if (!g.hit[i]) continue; for (let c = 0; c < 3; c++) { P[c] = g.P[i * 3 + c]; N[c] = g.N[i * 3 + c]; } const a = shadePoint(projectors, P, N, cA, KEYFRAME), b = shadePoint(projectors, P, N, cB, KEYFRAME); if (!a.rgb || !b.rgb) continue; d.push(Math.max(...[0, 1, 2].map((c) => Math.abs(a.rgb[c] - b.rgb[c]) * 255))); }
    d.sort((x, y2) => x - y2); popMax = Math.max(popMax, d[d.length - 1]); p999.push(d[Math.floor(0.999 * d.length)]);
  });
  add("K7 pop p99.9", Math.max(...p999) <= B.K7.p999_max, `per base view ${p999.map((v) => v.toFixed(2)).join(" ")}`); add("K7 pop max", popMax <= B.K7.max_max, `max ${popMax.toFixed(2)}`);
  result.pop = { max: popMax, p999 };
}

const out = opt("--out", null), sv = opt("--save", null), pdir = opt("--projectors", null);
if (sv) {
  mkdirSync(sv, { recursive: true });
  const toA = (rgb, w, h, holeOf = null) => { const a = new Uint8ClampedArray(w * h * 4); for (let i = 0; i < w * h; i++) { const hole = holeOf && holeOf(i); for (let c = 0; c < 3; c++) a[i * 4 + c] = hole ? [255, 0, 255][c] : rgb8(rgb[i * 3 + c]); a[i * 4 + 3] = 255; } return a; };
  for (const th of [0, 20, 45, 90]) { const k = views.findIndex((v) => v.key === `yaw${th}`), g = gViews[k]; for (const [name, set] of [["primary", [primary]], ["three", projectors]]) { const r = renderProjected(set, g, camScreen(th), { opt: KEYFRAME }); writePng(`${sv}/yaw${th}-${name}.png`, { width: g.w, height: g.h, data: toA(r.rgb, g.w, g.h, (i) => g.hit[i] && !r.covered[i]) }); } }
  writePng(`${sv}/primary-painting.png`, { width: BW, height: BH, data: new Uint8ClampedArray(primaryPaint.rgba8) }); secondaryInfo.forEach((s, k) => writePng(`${sv}/secondary${k + 1}-painting.png`, { width: BW, height: BH, data: new Uint8ClampedArray(s.painting.rgba8) }));
}
if (pdir) { mkdirSync(pdir, { recursive: true }); projectors.forEach((p, k) => writeFileSync(`${pdir}/projector${k}.bin`, Buffer.concat([Buffer.from(p.rgba.buffer, p.rgba.byteOffset, p.rgba.byteLength), Buffer.from(p.depth.buffer, p.depth.byteOffset, p.depth.byteLength)]))); writeFileSync(`${pdir}/cams.json`, JSON.stringify(projectors.map((p, k) => ({ cam: p.cam, w: p.w, h: p.h, dyaw: k ? chosen[k - 1].dyaw : 0, delev: k ? chosen[k - 1].delev : 0 })))); }
if (out) writeFileSync(out, JSON.stringify({ protocol: "evidence/paintover-keyframes-protocol.json", roundtrip: { ...rt, image: undefined }, views: result, secondaries: chosen.map((c) => ({ dyaw: c.dyaw, delev: c.delev, gain: c.gain })), missed_points_start: start, missed_points_left: remaining.length, checks }, null, 1) + "\n");
