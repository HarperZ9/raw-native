// "How attention moves information": a 72-second explainer made with the explainer kit
// (web/explainer/), driven by a model trained on a CPU (tools/explainer/attention_model.py,
// seed 7). Every number on screen is read from attention/data.json and attention/residual.npy;
// the bundle's SHA-256 values are checked when the scene loads. No narration: the words are the
// on-screen lines, and attention/captions.vtt carries the same rows.
import { span, ease, window as win } from "../../web/motion/timeline.mjs";
import { createLayers, loadBundle, tensorGrid, typeKit, shots, arc, quantile, show, NIGHT, PAPER, GROUNDS } from "../../web/explainer/index.mjs";
import { CUE, script } from "./attention/script.mjs";
import * as B from "./attention/beats.mjs";

const RAMP = [[0, [0.16, 0.15, 0.20]], [0.5, [0.020, 0.018, 0.022]], [0.8, [0.95, 0.52, 0.18]], [1, [2.2, 1.6, 1.0]]];
export const FOCUS_Q = 51;

function curves(rows, min, from = 0) {
  const out = [];
  rows.forEach((row, q) => row.forEach((w, k) => {
    if (k >= q || q < from || w < min) return;
    const x0 = B.tokenX(k), x1 = B.tokenX(q), h = Math.min(380, 30 + 0.45 * (x1 - x0));
    out.push({ c: arc(x0, B.ROW.y - 34, x1, B.ROW.y - 34, h), weight: w, phase: -0.02 * q });
  }));
  return out;
}

function camera(ctx, A) {
  const [x, y, w] = A.grid.tileRect(0);
  return shots([
    { t: 0, x: 960, y: 540, zoom: 1 },
    { t: CUE.cloudPush, x: 960, y: 540, zoom: 1, hold: 0.5 },
    { t: CUE.cloudPush + 4.5, x: x + w / 2, y: y + w / 2, zoom: 720 / w, ease: "inOut", hold: 2 },
    { t: CUE.task, x: 960, y: 540, zoom: 1, ease: "inOut" },
  ], { reduced: ctx.reduced });
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
    const d = bundle.json, res = bundle.arrays["residual.npy"];
    const [head, body, mono] = await Promise.all(["../../web/explainer/fonts/hanken-grotesk-700.atlas.json",
      "../motion/hanken-grotesk-500.atlas.json", "../../web/explainer/fonts/cascadia-mono-400.atlas.json"].map((p) => ctx.json(p)));
    const A = { d, bundle, res, T: typeKit({ head, body, mono }), NIGHT, PAPER, RAMP, lines: script(d) };
    A.grid = tensorGrid({ shape: d.residual.shape, x: 160, y: 160, w: 1600, h: 760 });
    A.range = [quantile(res.data, 0.01, 37), quantile(res.data, 0.99, 37)];
    A.cloudItem = { kind: "layer", area: A.grid.area, density: 0.8, radius: A.grid.cell * 0.5, range: A.range, ramp: RAMP };
    const e = d.example, L = e.repeat_length, s = e.repeat_start;
    A.focus = { q: FOCUS_Q, copy: FOCUS_Q - L, k: FOCUS_Q - L + 1 };
    if (FOCUS_Q < s + L || FOCUS_Q >= s + 2 * L) throw new Error("focus query is not inside the repeat");
    A.cam = camera(ctx, A);
    A.orbAt = (t) => {   // the orb rides above the position being read
      if (t < CUE.task) return [1836, 96, 34];
      const q = t < CUE.step2 ? Math.min(63, Math.floor(span(t, CUE.step1, CUE.step2 - 1) * 63)) : t < CUE.step2Focus ? 42 + Math.floor(span(t, CUE.step2, CUE.step2Focus) * 18) : FOCUS_Q;
      return [B.tokenX(t < CUE.step1 ? 0 : q), B.ROW.y - 80, 30];
    };
    if (ctx.motion) {
      const kit = await createLayers(ctx.motion);
      A.kit = kit;
      A.cloud = kit.points(A.grid.positions(res.data));
      A.cloudItem.layer = A.cloud;
      const tile = A.grid.positions(res.data.subarray(0, 64 * 64)).subarray(0, 4 * 64 * 64);
      A.tile = kit.points(tile);
      A.flow0 = kit.flow(curves(e.previous_token_rows, 0.08));
      A.flow1 = kit.flow(curves(e.induction_rows, 0.08, s + L));
      A.focusFlow = kit.flow(curves(e.induction_rows.map((r, q) => (q === FOCUS_Q ? r : r.map(() => 0))), 0.3));
    }
    return A;
  },
  frame(t, ctx) {
    const A = ctx.assets, cam = A.cam(t), items = [];
    const { items: gi, paper, ir } = B.ground(t, A, ctx);
    const g = paper ? "paper" : "night";
    items.push(...gi, ...B.cloud(t, A, cam), ...B.flows(t, A, ctx), ...B.orb(t, A, ctx, ir));
    const ui = [...B.titles(t, A, ctx, g), ...B.cloudLabels(t, A, ctx, cam), ...B.tokens(t, A, ctx, t >= CUE.step2Focus ? A.focus : null),
      ...B.focusLabels(t, A, A.focus), ...B.cards(t, A, ctx), ...closing(t, A, ctx), ...B.caption(t, A, g)];
    // Type stays fixed on screen while the camera moves over the cloud.
    for (const it of ui) if (cam.zoom !== 1 && it.screen === undefined) it.screen = true;
    items.push(...ui);
    return { background: paper ? [0.83, 0.79, 0.73] : [0.006, 0.006, 0.01], camera: cam, items,
      post: { bloom: paper ? 0.15 : 0.55, threshold: 0.75, vignette: paper ? 0.12 : 0.3, grain: 0.018 } };
  },
};

// The close, on paper: the two numbers that carry the result, and the source line.
function closing(t, A, ctx) {
  if (t < CUE.close) return [];
  const T = A.T, d = A.d, g = "paper", gc = GROUNDS[g], o = span(t, CUE.close, CUE.close + 0.8), out = [];
  const rev = (a) => (ctx.reduced ? 1 : span(t, a, a + 1.2, ease.out));
  out.push(...T.head("Two steps, two layers.", { x: 160, y: 250, size: 72, ground: g, opacity: o }));
  out.push(...T.figure(show(d.accuracy.repeat * rev(CUE.close + 0.6), "pct"), { x: 160, y: 470, size: 150, ground: g, opacity: o }));
  out.push(...T.meta("REPEAT ACCURACY, THE MODEL AS TRAINED", { x: 168, y: 520, ground: g, opacity: o }));
  out.push(...T.figure(show(d.ablations.both_layer_1_heads_zeroed.repeat, "pct"), { x: 1000, y: 470, size: 150, ground: g, opacity: o * rev(CUE.close + 1.6), color: gc.drift }));
  out.push(...T.meta(`WITH LAYER 1 SWITCHED OFF  ·  CHANCE IS ${show(d.chance, "pct")}`, { x: 1008, y: 520, ground: g, opacity: o * rev(CUE.close + 1.6) }));
  const so = span(t, CUE.source, CUE.source + 0.8);
  if (so > 0) {
    out.push(...T.meta(`SOURCE  ·  python ${d.script} --seed ${d.settings.seed} --steps ${d.settings.steps}  ·  torch ${d.torch}  ·  CPU`, { x: 160, y: 760, ground: g, opacity: so }));
    out.push(...T.meta(`data.json sha256 ${A.bundle.jsonSha256.slice(0, 16)}  ·  residual.npy sha256 ${d.files["residual.npy"].slice(0, 16)}  ·  checked at load`, { x: 160, y: 792, ground: g, opacity: so }));
    out.push(...T.meta("NO NARRATION  ·  THE LINES ON SCREEN ARE THE CAPTIONS", { x: 160, y: 824, ground: g, opacity: so }));
  }
  return out;
}

// For the interactive page: what is under scene point (x, y) at time t.
export function inspect(t, x, y, A) {
  if (t >= CUE.cloud - 1 && t < CUE.task) {
    const hit = A.grid.at(x, y);
    if (!hit) return null;
    const [s, p, dim] = hit.coords;
    return { sel: hit.index, text: `sequence ${s}, position ${p}, dimension ${dim}: ${A.res.data[hit.index].toFixed(4)} (residual.npy, float16)` };
  }
  if (t >= CUE.task && t < CUE.iris) {
    const i = Math.round(((x - B.ROW.x0) / (B.ROW.x1 - B.ROW.x0)) * 63);
    if (i < 0 || i > 63 || Math.abs(y - B.ROW.y) > 60) return null;
    const e = A.d.example, layer1 = t >= CUE.step2, rows = layer1 ? e.induction_rows : e.previous_token_rows;
    const top = rows[i].map((w, k) => [w, k]).filter(([, k]) => k <= i).sort((a, b) => b[0] - a[0]).slice(0, 3);
    const h = layer1 ? A.d.heads.induction : A.d.heads.previous_token;
    return { token: i, text: `position ${i} '${e.text[i]}', layer ${h.layer} head ${h.head} attends to: ` +
      top.map(([w, k]) => `${k} '${e.text[k]}' ${w.toFixed(2)}`).join(", ") + `; p(next) ${show(e.p_next[i] ?? 0, "f2")}` };
  }
  return null;
}
