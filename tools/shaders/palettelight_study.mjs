// Palette-locked lighting against post-quantised dither (project-docs/PALETTE-LIGHTING-PROTOCOL.md).
//   node tools/shaders/palettelight_study.mjs --out evidence/palettelight-study-runs.json [--save DIR]
import { writeFileSync, readFileSync, mkdirSync } from "node:fs";
import { chapelCamera } from "../../web/shaders/fixtures/chapel-render.mjs";
import { CLS } from "../../web/shaders/fixtures/chapel.mjs";
import { resolvePalettelight, runPalettelight, indexOf, tonality, idealLab, naiveRgb, irradiance } from "../../web/shaders/palettelight/palettelight.mjs";
import { ditherRef } from "../../web/shaders/dither/dither.mjs";
import { SRGB8_TO_LINEAR, linearToOklab, srgbToOklab } from "../../web/shaders/reference/retro-palettes.mjs";
import { oklabToLinearSrgb } from "../../web/shaders/reference/contracts.mjs";
import { renderChapels } from "./palettelight_pool.mjs";
import { writePng } from "./png.mjs";

const args = process.argv.slice(2), opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const proto = JSON.parse(readFileSync(new URL("../../evidence/palettelight-protocol.json", import.meta.url))), SW = proto.sweep, W = proto.size[0], H = proto.size[1], NAMES = ["ground", "stone", "wood", "foliage"];
const lampAt = (k) => [SW.x0 + ((SW.x1 - SW.x0) * k) / (SW.frames - 1), SW.y, SW.z], cam = chapelCamera();
const checks = [], add = (id, pass, detail) => { checks.push({ id, pass, detail }); console.log(pass ? "ok  " : "MISS", id, detail); };
const mean = (a) => a.reduce((s, v) => s + v, 0) / (a.length || 1);

const lamps = [...Array.from({ length: SW.frames }, (_, k) => lampAt(k)), [SW.hue_frame_x, SW.y, SW.z]];
const gs = await renderChapels(lamps.map((lamp) => ({ cam, w: W, h: H, lamp })));
const px0 = () => ({ P: [0, 0, 0], N: [0, 0, 1], vis: [0, 0, 0] });
const pixelOf = (g, i, x, y, px) => { px.hit = g.hit[i]; px.cls = g.cls[i]; px.a = g.a[i]; px.phase = g.phase[i]; px.ix = x; px.iy = y; for (let c = 0; c < 3; c++) { px.P[c] = g.P[i * 3 + c]; px.N[c] = g.N[i * 3 + c]; px.vis[c] = g.vis[i * 3 + c]; } return px; };
const isLit = (g, i) => g.hit[i] && g.cls[i] <= CLS.foliage;
// 4 x 4 box in linear light, then OKLab distance x 100 against a reference given as linear RGB.
function boxLin(lin, w, h) { const o = new Float64Array(w * h * 3); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) for (let c = 0; c < 3; c++) { let s = 0; for (let dy = -2; dy < 2; dy++) for (let dx = -2; dx < 2; dx++) s += lin[(Math.min(h - 1, Math.max(0, y + dy)) * w + Math.min(w - 1, Math.max(0, x + dx))) * 3 + c]; o[(y * w + x) * 3 + c] = s / 16; } return o; }
const labOf = (lin, i) => linearToOklab(lin[i * 3], lin[i * 3 + 1], lin[i * 3 + 2]);
function errVs(outLin, refLin, g, w, h) { const a = boxLin(outLin, w, h), b = boxLin(refLin, w, h); let s = 0, n = 0; for (let i = 0; i < w * h; i++) { if (!isLit(g, i)) continue; const p = labOf(a, i), q = labOf(b, i); s += Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]); n++; } return (100 * s) / n; }
const linFromBytes = (rgba, n) => { const o = new Float64Array(n * 3); for (let i = 0; i < n; i++) for (let c = 0; c < 3; c++) o[i * 3 + c] = SRGB8_TO_LINEAR[rgba[i * 4 + c]]; return o; };

const N = W * H, per = [], px = px0();
const palArr = (plan) => plan.palette.entries.map((e) => e.rgb);
let prevA = null, prevB = null;
const acc = { purityA: [], purityB: [], fidA_ideal: [], fidB_ideal: [], fidA_naive: [], fidB_naive: [], chA: [], chB: [], flipA: [], flipB: [], exactA: [], exactB: [] };
for (let f = 0; f < SW.frames; f++) {
  const g = gs[f], plan = resolvePalettelight({ lampPos: lamps[f] }), A = runPalettelight(plan, g, 0);
  const plate = new Uint8Array(N * 4), idealLin = new Float64Array(N * 3), naiveLin = new Float64Array(N * 3);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = y * W + x, p = pixelOf(g, i, x, y, px), rgb = naiveRgb(plan, p, 0);
    for (let c = 0; c < 3; c++) plate[i * 4 + c] = Math.round(Math.min(1, Math.max(0, rgb[c])) * 255); plate[i * 4 + 3] = 255;
    for (let c = 0; c < 3; c++) naiveLin[i * 3 + c] = SRGB8_TO_LINEAR[plate[i * 4 + c]];
    let lab; if (isLit(g, i)) lab = idealLab(plan, p); else lab = srgbToOklab(...[0, 1, 2].map((c) => plate[i * 4 + c] / 255));
    const lin = oklabToLinearSrgb(lab[0], lab[1], lab[2]); for (let c = 0; c < 3; c++) idealLin[i * 3 + c] = Math.min(1, Math.max(0, lin[c]));
  }
  const bIdx = ditherRef(plate, W, H, { palette: palArr(plan), mode: "mix-bayer8" }), bRgba = new Uint8Array(N * 4);
  for (let i = 0; i < N; i++) { bRgba.set(plan.palette.entries[bIdx[i]].rgb, i * 4); bRgba[i * 4 + 3] = 255; }
  const aLin = linFromBytes(A.rgba, N), bLin = linFromBytes(bRgba, N), palSet = new Set(plan.palette.entries.map((e) => e.rgb.join(",")));
  let exA = 0, exB = 0, pA = 0, pB = 0, nl = 0;
  for (let i = 0; i < N; i++) {
    if (palSet.has(`${A.rgba[i * 4]},${A.rgba[i * 4 + 1]},${A.rgba[i * 4 + 2]}`)) exA++; if (palSet.has(`${bRgba[i * 4]},${bRgba[i * 4 + 1]},${bRgba[i * 4 + 2]}`)) exB++;
    if (isLit(g, i)) { nl++; const want = NAMES[g.cls[i]]; if (plan.palette.entries[A.idx[i]].cls === want) pA++; if (plan.palette.entries[bIdx[i]].cls === want) pB++; }
  }
  acc.exactA.push(exA / N); acc.exactB.push(exB / N); acc.purityA.push(pA / nl); acc.purityB.push(pB / nl);
  acc.fidA_ideal.push(errVs(aLin, idealLin, g, W, H)); acc.fidB_ideal.push(errVs(bLin, idealLin, g, W, H)); acc.fidA_naive.push(errVs(aLin, naiveLin, g, W, H)); acc.fidB_naive.push(errVs(bLin, naiveLin, g, W, H));
  if (prevA) {
    let nB = 0, cA = 0, cB = 0, fA = 0, fB = 0;
    for (let i = 0; i < N; i++) { if (!isLit(g, i) || !isLit(gs[f - 1], i)) continue; nB++; if (A.idx[i] !== prevA.idx[i]) cA++; if (bIdx[i] !== prevB[i]) cB++; if (plan.palette.entries[A.idx[i]].cls !== plan.palette.entries[prevA.idx[i]].cls) fA++; if (plan.palette.entries[bIdx[i]].cls !== plan.palette.entries[prevB[i]].cls) fB++; }
    acc.chA.push(cA / nB); acc.chB.push(cB / nB); acc.flipA.push(fA / nB); acc.flipB.push(fB / nB);
  }
  prevA = A; prevB = bIdx;
  if (f === SW.frames - 1 && opt("--save", null)) { const d = opt("--save"); mkdirSync(d, { recursive: true }); const S = 3, up = (src) => { const o = new Uint8ClampedArray(W * S * H * S * 4); for (let y = 0; y < H * S; y++) for (let x = 0; x < W * S; x++) { const s = (Math.floor(y / S) * W + Math.floor(x / S)) * 4, q = (y * W * S + x) * 4; for (let c = 0; c < 4; c++) o[q + c] = src[s + c]; } return o; }; writePng(`${d}/locked.png`, { width: W * S, height: H * S, data: up(A.rgba) }); writePng(`${d}/post.png`, { width: W * S, height: H * S, data: up(bRgba) }); }
}
const m = {}; for (const [k, v] of Object.entries(acc)) m[k] = mean(v);
// hue shift and index mean at the hue frame
const gh = gs[gs.length - 1], ph = resolvePalettelight({ lampPos: lamps[lamps.length - 1] }), Ah = runPalettelight(ph, gh, 0), near = [], far = [], classStep = {}, classS = {};
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  const i = y * W + x; if (!isLit(gh, i)) continue; const p = pixelOf(gh, i, x, y, px), e = ph.palette.entries[Ah.idx[i]], t = tonality(ph, p);
  (classStep[gh.cls[i]] ??= []).push(e.step); (classS[gh.cls[i]] ??= []).push(t.s);
  if (gh.cls[i] === CLS.stone) { const d = Math.hypot(p.P[0] - lamps[lamps.length - 1][0], p.P[2] - lamps[lamps.length - 1][2]); if (d < 1.5 && p.P[1] < 2) near.push(e.lab); else if (t.warm < 1e-3) far.push(e.lab); }
}
const hueOf = (labs) => { const a = mean(labs.map((l) => l[1])), b = mean(labs.map((l) => l[2])); return (Math.atan2(b, a) * 180) / Math.PI; };
const hueShift = near.length && far.length ? Math.abs(hueOf(near) - hueOf(far)) : NaN;
const idxMean = Object.fromEntries(Object.keys(classStep).map((c) => [NAMES[c], Math.abs(mean(classStep[c]) - mean(classS[c]))]));
let rot = 0, nv = 0; { const dt = 1 / ph.cycleRate / ph.voidSteps; for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { const i = y * W + x; if (!gh.hit[i] || gh.cls[i] !== CLS.void) continue; const p = pixelOf(gh, i, x, y, px), a = ph.palette.entries[indexOf(ph, p, 0.3)], b = ph.palette.entries[indexOf(ph, p, 0.3 + dt)]; nv++; if (b.step === (a.step + 1) % ph.voidSteps) rot++; } }
const P = proto.predictions;
add("S1 exactness", m.exactA === 1 && m.exactB === 1, `locked ${m.exactA} post ${m.exactB}`);
add("S2 purity", m.purityA === 1 && m.purityB <= P.S2.purity_post_max, `locked ${m.purityA.toFixed(4)} post ${m.purityB.toFixed(4)} (post bound <= ${P.S2.purity_post_max})`);
add("S3 look fidelity", m.fidA_ideal <= P.S3.look_fidelity_locked_over_post_max * m.fidB_ideal, `locked ${m.fidA_ideal.toFixed(3)} post ${m.fidB_ideal.toFixed(3)} ratio ${(m.fidA_ideal / m.fidB_ideal).toFixed(3)}`);
add("S4 physical fidelity", m.fidB_naive <= P.S4.physical_fidelity_post_over_locked_max * m.fidA_naive, `locked ${m.fidA_naive.toFixed(3)} post ${m.fidB_naive.toFixed(3)} ratio ${(m.fidB_naive / m.fidA_naive).toFixed(3)}`);
add("S5 change rate", m.chA / m.chB >= P.S5.change_rate_ratio_range[0] && m.chA / m.chB <= P.S5.change_rate_ratio_range[1], `locked ${m.chA.toFixed(4)} post ${m.chB.toFixed(4)} ratio ${(m.chA / m.chB).toFixed(3)}`);
add("S5 class flips", m.flipA === P.S5.class_flips_locked && m.flipB >= P.S5.class_flips_post_min, `locked ${m.flipA} post ${m.flipB.toFixed(4)}`);
add("S6 hue shift", hueShift >= P.S6.hue_shift_deg_min, `${hueShift.toFixed(2)} deg over ${near.length} near and ${far.length} far stone pixels`);
add("S7 index mean", Object.values(idxMean).every((v) => v <= P.S7.index_mean_abs_max), JSON.stringify(Object.fromEntries(Object.entries(idxMean).map(([k, v]) => [k, +v.toFixed(4)]))));
add("S8 cycle rotation", nv > 100 && rot / nv === P.S8.cycle_rotation, `${rot} of ${nv}`);
const out = opt("--out", null); if (out) writeFileSync(out, JSON.stringify({ protocol: "evidence/palettelight-protocol.json", size: [W, H], means: m, hueShift, idxMean, cycle: { rot, nv }, checks }, null, 1) + "\n");
