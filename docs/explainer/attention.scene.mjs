// "How attention moves information" (v2, LOOK.md): a 73-second explainer made with the explainer
// kit (web/explainer/), driven by a model trained on a CPU (tools/explainer/attention_model.py,
// seed 7). Every number on screen is read from attention/data.json and attention/residual.npy;
// the bundle's SHA-256 values are checked when the scene loads. No narration in this cut: the
// words are the on-screen lines, and attention/captions.vtt carries the same rows.
import { span, ease } from "../../web/motion/timeline.mjs";
import { createLayers, loadBundle, typeKit, shots, arc, quantile, show, NIGHT, PAPER, AURORA, GROUNDS } from "../../web/explainer/index.mjs";
import { CUE, script } from "./attention/script.mjs";
import * as B from "./attention/beats.mjs";
import { SLABS, PER, slabGrid, slabs, slabLabels, travel } from "./attention/cloud3d.mjs";

const RAMP = [[0, [0.20, 0.10, 0.45]], [0.5, [0.025, 0.02, 0.04]], [0.8, [1.0, 0.45, 0.14]], [1, [2.4, 1.7, 1.1]]];
const MATRAMP = [[0, [0, 0, 0]], [0.15, [0.25, 0.08, 0.45]], [0.6, [1.1, 0.40, 0.15]], [1, [2.2, 1.8, 1.4]]];
export const FOCUS_Q = 51;
const MAT = { x: 1320, y: 64, size: 480 };

function curves(rows, min, from = 0, lift = 0.6) {
  const out = [];
  rows.forEach((row, q) => row.forEach((w, k) => {
    if (k >= q || q < from || w < min) return;
    const x0 = B.tokenX(k), x1 = B.tokenX(q), h = Math.min(440, 40 + lift * (x1 - x0));
    out.push({ c: arc(x0, B.ROW.y - 46, x1, B.ROW.y - 46, h), weight: w, phase: -0.02 * q });
  }));
  return out;
}

function matrixPoints(rows) {
  const n = rows.length, c = MAT.size / n, out = new Float32Array(4 * n * n);
  rows.forEach((row, q) => row.forEach((w, k) => out.set([MAT.x + (k + 0.5) * c, MAT.y + (q + 0.5) * c, k <= q ? w : 0, 0], 4 * (q * n + k))));
  return out;
}

function camera(ctx, A) {
  const [x, y, w] = A.grid.tileRect(0);
  return shots([
    { t: 0, x: 960, y: 540, zoom: 1 },
    { t: CUE.cloudPush, x: 960, y: 540, zoom: 1, hold: 0.3 },
    { t: CUE.cloudPush + 4, x: x + w / 2, y: y + w / 2, zoom: 760 / w, ease: "inOut", hold: 1.2 },
    { t: CUE.task, x: 960, y: 540, zoom: 1, ease: "inOut" },
  ], { reduced: ctx.reduced });
}

async function gpu(ctx, A, res, e) {
  const kit = await createLayers(ctx.motion), per = PER * 64 * 64;
  A.kit = kit;
  A.slabs = Array.from({ length: SLABS }, (_, k) => kit.points(A.grid.positions(res.data.subarray(k * per, (k + 1) * per))));
  A.tile = kit.points(A.grid.positions(res.data.subarray(0, 64 * 64)).subarray(0, 4 * 64 * 64));
  const L = e.repeat_length, s = e.repeat_start;
  A.flow0 = kit.flow(curves(e.previous_token_rows, 0.08, 0, 1.4));
  A.flow1 = kit.flow(curves(e.induction_rows, 0.08, s + L));
  A.focusFlow = kit.flow(curves(e.induction_rows.map((r, q) => (q === FOCUS_Q ? r : r.map(() => 0))), 0.3));
  A.mat0 = kit.points(matrixPoints(e.previous_token_rows));
  A.mat1 = kit.points(matrixPoints(e.induction_rows));
}

export default {
  title: "How attention moves information",
  duration: CUE.end,
  fps: 30,
  design: [1920, 1080],
  burnsCaptions: true,
  chapters: [{ t: 0, title: "Open" }, { t: CUE.cloud, title: "Every number" }, { t: CUE.task, title: "The task" },
    { t: CUE.step1, title: "Step 1" }, { t: CUE.step2, title: "Step 2" }, { t: CUE.close, title: "Two steps" }],
  async load(ctx) {
    const bundle = await loadBundle(ctx.url("attention/data.json"));
    if (!bundle.verified) throw new Error("attention data: a file does not match its recorded SHA-256");
    const d = bundle.json, res = bundle.arrays["residual.npy"], e = d.example;
    const [head, body, mono] = await Promise.all(["../../web/explainer/fonts/hanken-grotesk-700.atlas.json",
      "../motion/hanken-grotesk-500.atlas.json", "../../web/explainer/fonts/conso-400.atlas.json"].map((p) => ctx.json(p)));
    const A = { d, bundle, res, T: typeKit({ head, body, mono }), NIGHT, PAPER, AURORA, RAMP, MATRAMP, MAT, lines: script(d) };
    A.grid = slabGrid();
    A.range = [quantile(res.data, 0.01, 37), quantile(res.data, 0.99, 37)];
    A.slabItem = { kind: "layer", area: A.grid.area, density: 0.5, radius: A.grid.cell * 0.55, range: A.range, ramp: RAMP, maxGain: 2, gainScale: 0.45 };
    A.focus = { q: FOCUS_Q, copy: FOCUS_Q - e.repeat_length, k: FOCUS_Q - e.repeat_length + 1 };
    if (FOCUS_Q < e.repeat_start + e.repeat_length || FOCUS_Q >= e.repeat_start + 2 * e.repeat_length) throw new Error("focus query is not inside the repeat");
    A.cam = camera(ctx, A);
    A.orbAt = (t) => {   // the orb rides above the position being read
      if (t < CUE.task) return [1830, 52, 30];
      const q = t < CUE.step2 ? Math.min(63, Math.floor(span(t, CUE.step1, CUE.step2 - 1) * 63)) : t < CUE.step2Focus ? 42 + Math.floor(span(t, CUE.step2, CUE.step2Focus) * 18) : FOCUS_Q;
      return [B.tokenX(t < CUE.step1 ? 0 : q), B.ROW.y - 96, 34];
    };
    if (ctx.motion) await gpu(ctx, A, res, e);
    return A;
  },
  frame(t, ctx) {
    const A = ctx.assets, cam = A.cam(t), items = [];
    const { items: gi, paper, ir } = B.ground(t, A, ctx);
    const g = paper ? "paper" : "night";
    items.push(...gi, ...B.bigMatrix(t, A), ...slabs(t, A, ctx, cam), ...B.flows(t, A, ctx), ...B.probColumns(t, A, ctx), ...B.orb(t, A, ctx, ir));
    const ui = [...B.titles(t, A, ctx, g), ...slabLabels(t, A, ctx, cam), ...B.matrixLabel(t, A), ...B.tokens(t, A, ctx, t >= CUE.step2Focus ? A.focus : null),
      ...B.focusLabels(t, A, A.focus), ...B.cards(t, A), ...closing(t, A, ctx), ...B.caption(t, A, g)];
    for (const it of ui) if (cam.zoom !== 1 && it.screen === undefined) it.screen = true;
    items.push(...ui);
    return { background: paper ? [0.83, 0.79, 0.73] : [0.006, 0.008, 0.03], camera: cam, items,
      post: { bloom: paper ? 0.2 : t < CUE.task ? 0.4 : 0.75, threshold: 0.7, vignette: paper ? 0.12 : 0.35, grain: 0.022 } };
  },
};

function closing(t, A, ctx) {
  if (t < CUE.close) return [];
  const T = A.T, d = A.d, g = "paper", gc = GROUNDS[g], o = span(t, CUE.close, CUE.close + 0.8), out = [];
  const rev = (a) => (ctx.reduced ? 1 : span(t, a, a + 1.2, ease.out));
  out.push(...T.head("Two steps, two layers.", { x: 140, y: 250, size: 84, ground: g, opacity: o }));
  out.push(...T.figure(show(d.accuracy.repeat * rev(CUE.close + 0.6), "pct"), { x: 140, y: 500, size: 190, ground: g, opacity: o }));
  out.push(...T.meta("Repeat accuracy, the model as trained", { x: 150, y: 552, ground: g, opacity: o }));
  out.push(...T.figure(show(d.ablations.both_layer_1_heads_zeroed.repeat, "pct"), { x: 140, y: 760, size: 190, ground: g, opacity: o * rev(CUE.close + 1.6), color: gc.drift }));
  out.push(...T.meta(`With layer 1 switched off (chance is ${show(d.chance, "pct")})`, { x: 150, y: 812, ground: g, opacity: o * rev(CUE.close + 1.6) }));
  const so = span(t, CUE.source, CUE.source + 0.8);
  if (so > 0) {
    out.push(...T.meta(`Source: a model trained on a CPU with this command, torch ${d.torch}`, { x: 1000, y: 800, ground: g, opacity: so }));
    out.push(...T.mono(`python ${d.script} --seed ${d.settings.seed} --steps ${d.settings.steps}`, { x: 1000, y: 836, size: 20, ground: g, opacity: so }));
    out.push(...T.meta("The data files are checked against their recorded hashes when the film loads.", { x: 1000, y: 872, ground: g, opacity: so }));
  }
  return out;
}

// For the interactive page: what is under scene point (x, y) at time t.
export function inspect(t, x, y, A) {
  if (t >= CUE.cloudPush - 0.5 && t < CUE.task) {
    if (travel(t, false) > 1) return null;
    const hit = A.grid.at(x, y, { snap: true });
    if (!hit) return null;
    const [s, p, dim] = hit.coords;
    return { sel: hit.index, text: `sequence ${s}, position ${p}, dimension ${dim}: ${A.res.data[hit.index].toFixed(4)} (residual.npy, float16)` };
  }
  if (t >= CUE.task && t < CUE.iris) {
    const i = Math.round(((x - B.ROW.x0) / (B.ROW.x1 - B.ROW.x0)) * 63);
    if (i < 0 || i > 63) return null;
    const e = A.d.example, layer1 = t >= CUE.step2, rows = layer1 ? e.induction_rows : e.previous_token_rows;
    const top = rows[i].map((w, k) => [w, k]).filter(([, k]) => k <= i).sort((a, b) => b[0] - a[0]).slice(0, 3);
    const h = layer1 ? A.d.heads.induction : A.d.heads.previous_token;
    return { token: i, text: `position ${i} '${e.text[i]}', layer ${h.layer} head ${h.head} attends to: ` +
      top.map(([w, k]) => `${k} '${e.text[k]}' ${w.toFixed(2)}`).join(", ") + `; p(next) ${show(e.p_next[i] ?? 0, "f2")}` };
  }
  return null;
}
