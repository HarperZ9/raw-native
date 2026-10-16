// The beats of "How attention moves information": each returns display-list items for time t.
// Everything drawn is read from the data bundle (A.d) or computed from it at load.
import { span, ease, window as win, clamp01 } from "../../../web/motion/timeline.mjs";
import { rect, line } from "../../../web/motion/path.mjs";
import { dataCard, matrix, iris, beat, phase, typeOn, show, GROUNDS } from "../../../web/explainer/index.mjs";
import { CUE } from "./script.mjs";

export const ROW = { x0: 128, x1: 1792, y: 600 };
export const tokenX = (i) => ROW.x0 + (i * (ROW.x1 - ROW.x0)) / 63;

const ORB_OPEN = { iris: { blades: 9, open: 1.2, rot: 0 }, spokes: 140, spokeGain: 0.4, spokeSharp: 28, ring: 0.5, ringW: 0.01, prism: 0.03 };

export function ground(t, A, ctx) {
  const ir = iris(t, CUE.iris, CUE.close, { radius0: 40, reduced: ctx.reduced });
  const paper = ir.ground === 1;
  const items = [{ kind: "layer", layer: A.kit?.field, mode: "atmos", spec: paper ? A.PAPER : A.NIGHT, time: t, opacity: paper ? 1 : span(t, 0, 2) }];
  return { items, paper, ir };
}

// The orb: opens in the first beat, then rides above the reading position.
export function orb(t, A, ctx, ir) {
  if (!A.kit) return [];
  if (ir.active && ir.cover > 0) return [{ kind: "layer", layer: A.kit.field, mode: "orb", x: 960, y: 540, radius: ir.radius, spec: { ...ORB_OPEN, iris: { blades: 0, open: 1.2 }, gain: 0.55 }, opacity: 1 }];
  if (t >= CUE.close) return [];
  const open = ctx.reduced ? 1.2 : 0.15 + 1.05 * span(t, 0.5, 5, ease.out);
  const big = { x: 960, y: 470, r: 230 };
  const ride = A.orbAt(t);
  const u = ctx.reduced ? (t >= CUE.cloud ? 1 : 0) : span(t, CUE.cloud - 1, CUE.cloud + 1.5, ease.inOut);
  const x = big.x + (ride[0] - big.x) * u, y = big.y + (ride[1] - big.y) * u, r = big.r + (ride[2] - big.r) * u;
  return [{ kind: "layer", layer: A.kit.field, mode: "orb", x, y, radius: r, opacity: span(t, 0.2, 1.5),
    spec: { ...ORB_OPEN, iris: { blades: 9, open, rot: 0.08 * t }, gain: 1 - 0.4 * u } }];
}

export function titles(t, A, ctx, g) {
  const T = A.T, out = [], ty = (a, d = 0.9) => (ctx.reduced ? (t >= a ? 1 : 0) : typeOn(t, a, d));
  const o1 = 1 - span(t, CUE.cloud - 0.6, CUE.cloud);
  if (o1 > 0) {
    out.push(...T.meta(`A 2-LAYER ATTENTION-ONLY TRANSFORMER  ·  ${A.d.settings.heads_per_layer} HEADS PER LAYER  ·  TRAINED ON A CPU  ·  SEED ${A.d.settings.seed}`,
      { x: 960, y: 820, anchor: "center", reveal: ty(1.0, 1.4), opacity: o1, ground: g }));
    out.push(...T.head("How attention moves information", { x: 960, y: 900, size: 64, anchor: "center", reveal: ty(CUE.title, 1.2), opacity: o1, ground: g }));
  }
  return out;
}

// The lower-third line: the script row active at t, set in the body face.
export function caption(t, A, g) {
  const row = A.lines.find(([a, b]) => t >= a && t < b);
  if (!row || t < CUE.cloud) return [];   // the opening line is the title itself
  const o = win(t, row[0], row[1], 0.35, 0.35);
  return A.T.body(row[2], { x: 960, y: 1036, size: 30, anchor: "center", opacity: o, ground: g });
}

// The cloud: all 1,048,576 residual numbers, then a swap to sequence 0 alone as the camera pushes.
export function cloud(t, A, cam) {
  if (!A.kit || t < CUE.cloud - 1 || t > CUE.task + 1) return [];
  const o = win(t, CUE.cloud - 1, CUE.task + 1, 1.5, 1.2);
  const fine = span(cam.zoom, 2.5, 5, ease.linear);
  const items = [];
  if (fine < 1) items.push({ ...A.cloudItem, opacity: o * (1 - fine), sel: A.sel ?? -1 });
  if (fine > 0) items.push({ kind: "layer", layer: A.tile, keep: 1, radius: A.grid.cell * 0.42, range: A.range, ramp: A.RAMP, opacity: o * fine, sel: A.selTile ?? -1 });
  return items;
}

export function cloudLabels(t, A, ctx, cam) {
  const T = A.T, g = "night", o = win(t, CUE.cloud, CUE.task, 0.8, 0.6), out = [];
  if (o <= 0) return out;
  const [S, P, D] = A.d.residual.shape;
  out.push(...T.meta(`FINAL RESIDUAL STREAM  ·  ${S} SEQUENCES x ${P} POSITIONS x ${D} DIMENSIONS  ·  residual.npy  ·  sha256 ${A.d.files["residual.npy"].slice(0, 12)}`,
    { x: 160, y: 120, reveal: typeOn(t, CUE.cloud + 0.3, 1.5), opacity: o, ground: g, screen: true }));
  const lod = A.cloud && ctx.motion ? A.cloud.lodFor(A.cloudItem, cam, ctx.motion.w, ctx.motion.h) : null;
  if (lod && cam.zoom < 2.5) out.push(...T.meta(`DRAWN ${show(lod.drawn, "int")} OF ${show(A.cloud.count, "int")}  ·  LEVEL OF DETAIL ${show(lod.keep, "pct0")}`,
    { x: 160, y: 146, opacity: o * 0.8, ground: g, screen: true }));
  if (cam.zoom > 4) out.push(...T.meta(`SEQUENCE 0  ·  ROWS ARE POSITIONS, COLUMNS ARE DIMENSIONS`, { x: 160, y: 960, opacity: o * span(cam.zoom, 4, 6), ground: g, screen: true }));
  return out;
}

// The token row: the example's 64 characters, the two copies marked.
export function tokens(t, A, ctx, focus = null) {
  const T = A.T, g = "night", e = A.d.example, s = e.repeat_start, L = e.repeat_length;
  const o = win(t, CUE.task, CUE.iris + 0.4, 0.8, 0.4), out = [];
  if (o <= 0) return out;
  const gc = GROUNDS[g];
  [...e.text].forEach((ch, i) => {
    const hot = focus && i === focus.k, pred = focus && i === focus.q;
    const dim = focus && !hot && !pred && i !== focus.copy;
    out.push(...T.mono(ch, { x: tokenX(i), y: ROW.y, size: 30, anchor: "center", ground: g, opacity: o * (dim ? 0.45 : 1),
      color: hot ? gc.verified : undefined, reveal: ctx.reduced ? 1 : clamp01((t - CUE.task) * 24 - i * 0.35) }));
  });
  const mark = (a, b, label, at) => {
    const u = span(t, at, at + 0.8);
    if (u <= 0) return;
    out.push({ shape: line(tokenX(a) - 10, ROW.y + 18, tokenX(a) - 10 + (tokenX(b) - tokenX(a) + 20) * u, ROW.y + 18), stroke: gc.ink2, width: 2, opacity: o });
    out.push(...T.meta(label, { x: tokenX(a) - 10, y: ROW.y + 46, opacity: o * u, ground: g }));
  };
  mark(s, s + L - 1, "FIRST COPY", CUE.task + 2.5);
  mark(s + L, s + 2 * L - 1, "REPEAT", CUE.task + 3.2);
  out.push(...T.meta("POSITION 0", { x: tokenX(0), y: ROW.y - 52, opacity: o * 0.7, ground: g }));
  out.push(...T.meta("63", { x: tokenX(63), y: ROW.y - 52, anchor: "right", opacity: o * 0.7, ground: g }));
  return out;
}

export function flows(t, A, ctx) {
  if (!A.kit) return [];
  const out = [], ph = ctx.reduced ? 0.6 : phase(t, CUE.step1, 1.8);
  const o1 = win(t, CUE.step1, CUE.step2 + 0.5, 0.8, 0.8), o2 = win(t, CUE.step2, CUE.iris + 0.3, 0.8, 0.5);
  if (o1 > 0) out.push({ kind: "layer", layer: A.flow0, reveal: ctx.reduced ? 1 : span(t, CUE.step1, CUE.step1 + 2.5, ease.out), phase: ph, width: 2.2, opacity: o1 });
  if (o2 > 0) {
    const f = span(t, CUE.step2Focus, CUE.step2Focus + 0.8);
    out.push({ kind: "layer", layer: A.flow1, reveal: ctx.reduced ? 1 : span(t, CUE.step2, CUE.step2 + 2.5, ease.out), phase: ph, width: 2.4, opacity: o2 * (1 - 0.75 * f) });
    if (f > 0) out.push({ kind: "layer", layer: A.focusFlow, reveal: 1, phase: ph, width: 4, opacity: o2 * f, hot: [0.9, 1.6, 1.0], cold: [0.25, 0.6, 0.4] });
  }
  return out;
}

export function cards(t, A, ctx) {
  const T = A.T, d = A.d, g = "night", out = [], ab = d.ablations;
  const c1 = win(t, CUE.taskCard, CUE.step1, 0.6, 0.5);
  if (c1 > 0) out.push(...dataCard(T, { x: 128, y: 700, w: 620, title: "Next character right", ground: g, opacity: c1, reveal: span(t, CUE.taskCard, CUE.taskCard + 1.5),
    rows: [{ label: "inside the repeat", value: d.accuracy.repeat, format: "pct", bar: 1, mark: "verified" },
           { label: `elsewhere  ·  chance ${show(d.chance, "pct")}`, value: d.accuracy.elsewhere, format: "pct", bar: 1 }],
    source: `${d.settings.test_sequences} HELD-OUT SEQUENCES  ·  data.json accuracy` }));
  const c2 = win(t, CUE.step1Card, CUE.step2, 0.6, 0.5), h = d.heads;
  if (c2 > 0) out.push(...dataCard(T, { x: 128, y: 700, w: 620, title: `Layer ${h.previous_token.layer}, head ${h.previous_token.head}`, ground: g, opacity: c2,
    reveal: span(t, CUE.step1Card, CUE.step1Card + 1.5),
    rows: [{ label: "attention on the previous token", value: h.previous_token.mean_weight, format: "f2", bar: 1 },
           { label: "repeat accuracy with this head off", value: ab.previous_token_head_zeroed.repeat, format: "pct", bar: 1, mark: "drift" }],
    source: "MEAN OVER ALL POSITIONS  ·  data.json heads, ablations" }));
  const c3 = win(t, CUE.step2Card, CUE.iris, 0.6, 0.3);
  if (c3 > 0) out.push(...dataCard(T, { x: 128, y: 700, w: 620, title: "Layer 1, both heads", ground: g, opacity: c3, reveal: span(t, CUE.step2Card, CUE.step2Card + 1.5),
    rows: [{ label: `attention on the token after the copy  ·  head ${h.induction.head}`, value: h.induction.mean_weight, format: "f2", bar: 1 },
           { label: "repeat accuracy with both heads off", value: ab.both_layer_1_heads_zeroed.repeat, format: "pct", bar: 1, mark: "drift" }],
    source: "QUERIES INSIDE THE REPEAT  ·  data.json heads, ablations" }));
  const m1 = win(t, CUE.step1 + 1, CUE.step2 + 0.4, 0.8, 0.5), m2 = win(t, CUE.step2 + 1, CUE.iris, 0.8, 0.3);
  const e = d.example;
  if (m1 > 0) out.push(...matrix({ x: 1500, y: 700, size: 260, rows: e.previous_token_rows, opacity: m1, reveal: span(t, CUE.step1 + 1, CUE.step1 + 3) }),
    ...T.meta(`ATTENTION, LAYER ${h.previous_token.layer} HEAD ${h.previous_token.head}  ·  QUERY DOWN, KEY ACROSS`, { x: 1760, y: 982, anchor: "right", size: 14, opacity: m1, ground: g }));
  if (m2 > 0) out.push(...matrix({ x: 1500, y: 700, size: 260, rows: e.induction_rows, opacity: m2, reveal: span(t, CUE.step2 + 1, CUE.step2 + 3) }),
    ...T.meta(`ATTENTION, LAYER ${h.induction.layer} HEAD ${h.induction.head}  ·  QUERY DOWN, KEY ACROSS`, { x: 1760, y: 982, anchor: "right", size: 14, opacity: m2, ground: g }));
  return out;
}

export function focusLabels(t, A, focus) {
  const T = A.T, g = "night", o = win(t, CUE.step2Focus, CUE.iris, 0.6, 0.3), out = [], gc = GROUNDS[g];
  if (o <= 0) return out;
  const e = A.d.example, w = e.induction_rows[focus.q][focus.k];
  out.push({ shape: rect(tokenX(focus.q) - 14, ROW.y - 30, 28, 40, 2), stroke: gc.ink, width: 1.5, opacity: o });
  out.push(...T.meta(`READING ${e.text[focus.q]} AT ${focus.q}`, { x: tokenX(focus.q), y: ROW.y + 82, anchor: "center", opacity: o, ground: g }));
  out.push(...T.meta(`AFTER THE EARLIER ${e.text[focus.copy]}: ${e.text[focus.k]}  ·  WEIGHT ${show(w, "f2")}`, { x: tokenX(focus.k), y: ROW.y + 82, anchor: "center", opacity: o, ground: g, color: gc.verified }));
  return out;
}
