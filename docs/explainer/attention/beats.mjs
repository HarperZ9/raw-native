// The beats of "How attention moves information", v2 (LOOK.md): the data at full frame, in depth,
// over a spectral ground. Each function returns display-list items for time t; every mark is
// read from the data bundle (A.d) or built from it at load.
import { span, ease, window as win, clamp01, lerp } from "../../../web/motion/timeline.mjs";
import { rect, line } from "../../../web/motion/path.mjs";
import { dataCard, columns, iris, phase, typeOn, show, GROUNDS } from "../../../web/explainer/index.mjs";
import { CUE } from "./script.mjs";

export const ROW = { x0: 110, x1: 1810, y: 640 };
export const tokenX = (i) => ROW.x0 + (i * (ROW.x1 - ROW.x0)) / 63;
export const COLS = { y: 960, h: 250 };

const ORB_OPEN = { iris: { blades: 9, open: 1.2, rot: 0 }, spokes: 140, spokeGain: 0.45, spokeSharp: 28, ring: 0.6, ringW: 0.01, prism: 0.035 };

export function ground(t, A, ctx) {
  const ir = iris(t, CUE.iris, CUE.close, { radius0: 40, reduced: ctx.reduced });
  const paper = ir.ground === 1;
  return { items: [{ kind: "layer", layer: A.kit?.field, mode: "atmos", spec: paper ? A.PAPER : A.AURORA, time: t, opacity: paper ? 1 : span(t, 0, 2.5) }], paper, ir };
}

export function orb(t, A, ctx, ir) {
  if (!A.kit) return [];
  if (ir.active && ir.cover > 0) return [{ kind: "layer", layer: A.kit.field, mode: "orb", x: 960, y: 540, radius: ir.radius, spec: { ...ORB_OPEN, iris: { blades: 0, open: 1.2 }, gain: 0.55 }, opacity: 1, screen: true }];
  if (t >= CUE.close) return [{ kind: "layer", layer: A.kit.field, mode: "orb", x: 1540, y: 360, radius: 150, spec: { ...ORB_OPEN, iris: { blades: 0, open: 1.2 }, spokes: 0, ring: 0, gain: 0.32 }, opacity: span(t, CUE.close, CUE.close + 1.5), screen: true }];
  const open = ctx.reduced ? 1.2 : 0.15 + 1.05 * span(t, 0.5, 5, ease.out);
  const ride = A.orbAt(t), u = ctx.reduced ? (t >= CUE.cloud ? 1 : 0) : span(t, CUE.cloud - 1.2, CUE.cloud + 0.8, ease.inOut);
  const x = lerp(960, ride[0], u), y = lerp(450, ride[1], u), r = lerp(280, ride[2], u);
  return [{ kind: "layer", layer: A.kit.field, mode: "orb", x, y, radius: r, opacity: span(t, 0.2, 1.5), screen: true,
    spec: { ...ORB_OPEN, iris: { blades: 9, open, rot: 0.08 * t }, gain: 1 - 0.3 * u } }];
}

export function titles(t, A, ctx, g) {
  const T = A.T, out = [], ty = (a, d = 0.9) => (ctx.reduced ? (t >= a ? 1 : 0) : typeOn(t, a, d));
  const o1 = 1 - span(t, CUE.cloud - 0.6, CUE.cloud);
  if (o1 > 0) {
    out.push(...T.meta(`A two-layer attention-only transformer with ${A.d.settings.heads_per_layer} heads per layer, trained on a CPU`,
      { x: 960, y: 860, anchor: "center", reveal: ty(1.0, 1.4), opacity: o1, ground: g }));
    out.push(...T.head("How attention moves information", { x: 960, y: 950, size: 76, anchor: "center", reveal: ty(CUE.title, 1.2), opacity: o1, ground: g }));
  }
  return out;
}

export function caption(t, A, g) {
  const row = A.lines.find(([a, b]) => t >= a && t < b);
  if (!row || t < CUE.cloud) return [];
  return A.T.body(row[2], { x: 960, y: 1046, size: 30, anchor: "center", opacity: win(t, row[0], row[1], 0.35, 0.35), ground: g });
}

// The token row, set large; the two copies sit on glowing bands (art), the type stays ink.
export function tokens(t, A, ctx, focus = null) {
  const T = A.T, g = "night", e = A.d.example, s = e.repeat_start, L = e.repeat_length, gc = GROUNDS[g];
  const o = win(t, CUE.task - 0.5, CUE.iris + 0.4, 0.8, 0.4), out = [];
  if (o <= 0) return out;
  const band = (a, b, at, c) => {
    const u = ctx.reduced ? 1 : span(t, at, at + 1, ease.out);
    if (u <= 0) return;
    const x = tokenX(a) - 14, w = (tokenX(b) - tokenX(a) + 28) * u;
    out.push({ shape: rect(x, ROW.y - 44, w, 64, 3), fill: c, opacity: o * 0.35, blur: 18, blend: "add" });
    out.push({ shape: line(x, ROW.y + 26, x + w, ROW.y + 26), stroke: c, width: 3, opacity: o, blend: "add" });
  };
  band(s, s + L - 1, CUE.task + 2.2, [0.55, 0.30, 0.95]);
  band(s + L, s + 2 * L - 1, CUE.task + 2.9, [1.0, 0.55, 0.25]);
  [...e.text].forEach((ch, i) => {
    const hot = focus && i === focus.k, dim = focus && !hot && i !== focus.q && i !== focus.copy;
    out.push(...T.mono(ch, { x: tokenX(i), y: ROW.y, size: 36, anchor: "center", ground: g, opacity: o * (dim ? 0.4 : 1),
      color: hot ? gc.verified : undefined, reveal: ctx.reduced ? 1 : clamp01((t - CUE.task + 0.5) * 24 - i * 0.35) }));
  });
  const lab = (label, x, at) => { const u = span(t, at, at + 0.6); if (u > 0) out.push(...T.meta(label, { x, y: ROW.y + 58, opacity: o * u, ground: g })); };
  lab("First copy", tokenX(s) - 14, CUE.task + 2.6);
  lab("Repeat", tokenX(s + L) - 14, CUE.task + 3.3);
  return out;
}

// p(next character) for this sequence, one luminous column per position. The values morph to an
// ablated run while a head is switched off (A.d.example.p_next_ablated).
export function probColumns(t, A, ctx) {
  const e = A.d.example, o = win(t, CUE.taskCard - 0.3, CUE.iris + 0.3, 0.6, 0.4);
  if (o <= 0) return [];
  const off1 = win(t, CUE.step1Card + 1.5, CUE.step2 - 0.5, 1.2, 1.2), off2 = span(t, CUE.step2Card + 0.8, CUE.step2Card + 2.2);
  const k1 = ctx.reduced ? (off1 > 0.5 ? 1 : 0) : off1, k2 = ctx.reduced ? (off2 > 0.5 ? 1 : 0) : off2;
  const a1 = e.p_next_ablated.previous_token_head_zeroed, a2 = e.p_next_ablated.both_layer_1_heads_zeroed;
  const v = e.p_next.map((p, i) => lerp(lerp(p, a1[i], k1), a2[i], k2));
  const out = columns({ x0: tokenX(0) - 13, x1: tokenX(63) + 13, y: COLS.y, h: COLS.h, values: [...v, 0], reveal: ctx.reduced ? 1 : span(t, CUE.taskCard, CUE.taskCard + 2.5), opacity: o });
  out.push(...A.T.meta("Probability of the right next character at each position, this sequence", { x: tokenX(0) - 13, y: COLS.y + 26, opacity: o * 0.85, ground: "night" }));
  const state = k2 > 0.5 ? "Layer 1 switched off" : k1 > 0.5 ? `Layer ${A.d.heads.previous_token.layer}, head ${A.d.heads.previous_token.head} switched off` : "The model as trained";
  out.push(...A.T.meta(state, { x: tokenX(63) + 13, y: COLS.y + 26, anchor: "right", opacity: o, ground: "night", color: k1 > 0.5 || k2 > 0.5 ? GROUNDS.night.drift : undefined }));
  return out;
}

export function flows(t, A, ctx) {
  if (!A.kit) return [];
  const out = [], ph = ctx.reduced ? 0.6 : phase(t, CUE.step1, 1.6);
  const o1 = win(t, CUE.step1, CUE.step2 + 0.5, 0.8, 0.8), o2 = win(t, CUE.step2, CUE.iris + 0.3, 0.8, 0.5);
  if (o1 > 0) out.push({ kind: "layer", layer: A.flow0, reveal: ctx.reduced ? 1 : span(t, CUE.step1, CUE.step1 + 3, ease.out), phase: ph, width: 6, opacity: o1, base: 0.5 });
  if (o2 > 0) {
    const f = span(t, CUE.step2Focus, CUE.step2Focus + 0.8);
    out.push({ kind: "layer", layer: A.flow1, reveal: ctx.reduced ? 1 : span(t, CUE.step2, CUE.step2 + 3, ease.out), phase: ph, width: 6, opacity: o2 * (1 - 0.7 * f), base: 0.5 });
    if (f > 0) out.push({ kind: "layer", layer: A.focusFlow, reveal: 1, phase: ph, width: 11, opacity: o2 * f, hot: [0.9, 1.8, 1.1], cold: [0.25, 0.7, 0.45] });
  }
  return out;
}

// The attention pattern itself, full frame behind the row: 64 x 64 points, brightness by weight.
export function bigMatrix(t, A) {
  if (!A.kit) return [];
  const o1 = win(t, CUE.step1 + 0.5, CUE.step2 + 0.3, 1.2, 0.8), o2 = win(t, CUE.step2 + 0.5, CUE.iris + 0.2, 1.2, 0.5), out = [];
  if (o1 > 0) out.push({ kind: "layer", layer: A.mat0, keep: 1, radius: 4, range: [0, 1], ramp: A.MATRAMP, opacity: o1 * 0.8, screen: true });
  if (o2 > 0) out.push({ kind: "layer", layer: A.mat1, keep: 1, radius: 4, range: [0, 1], ramp: A.MATRAMP, opacity: o2 * 0.8, screen: true });
  return out;
}

export function matrixLabel(t, A) {
  const h = A.d.heads, o1 = win(t, CUE.step1 + 1, CUE.step2, 0.8, 0.5), o2 = win(t, CUE.step2 + 1, CUE.iris, 0.8, 0.3), out = [];
  const m = (s, o) => out.push(...A.T.meta(s, { x: A.MAT.x + A.MAT.size, y: A.MAT.y - 16, anchor: "right", opacity: o, ground: "night" }));
  if (o1 > 0) m(`Attention of layer ${h.previous_token.layer}, head ${h.previous_token.head}: each row reads the columns`, o1);
  if (o2 > 0) m(`Attention of layer ${h.induction.layer}, head ${h.induction.head}: each row reads the columns`, o2);
  return out;
}

export function cards(t, A) {
  const T = A.T, d = A.d, g = "night", out = [], ab = d.ablations, h = d.heads;
  const card = (a, b, title, rows, source) => {
    const o = win(t, a, b, 0.6, 0.5);
    if (o > 0) out.push(...dataCard(T, { x: 110, y: 90, w: 600, title, ground: g, opacity: o, reveal: span(t, a, a + 1.5), rows, source }));
  };
  card(CUE.taskCard, CUE.step1, "Next character right, 256 sequences",
    [{ label: "inside the repeat", value: d.accuracy.repeat, format: "pct", bar: 1, mark: "verified" },
     { label: `elsewhere  ·  chance ${show(d.chance, "pct")}`, value: d.accuracy.elsewhere, format: "pct", bar: 1 }], "Held-out sequences, data.json");
  card(CUE.step1Card, CUE.step2, `Layer ${h.previous_token.layer}, head ${h.previous_token.head}`,
    [{ label: "attention on the previous token", value: h.previous_token.mean_weight, format: "f2", bar: 1 },
     { label: "repeat accuracy with this head off", value: ab.previous_token_head_zeroed.repeat, format: "pct", bar: 1, mark: "drift" }], "data.json, heads and ablations");
  card(CUE.step2Card, CUE.iris, "Layer 1, both heads",
    [{ label: `attention on the token after the copy  ·  head ${h.induction.head}`, value: h.induction.mean_weight, format: "f2", bar: 1 },
     { label: "repeat accuracy with both heads off", value: ab.both_layer_1_heads_zeroed.repeat, format: "pct", bar: 1, mark: "drift" }], "data.json, heads and ablations");
  return out;
}

export function focusLabels(t, A, focus) {
  const T = A.T, g = "night", o = win(t, CUE.step2Focus, CUE.iris, 0.6, 0.3), out = [], gc = GROUNDS[g];
  if (o <= 0) return out;
  const e = A.d.example, w = e.induction_rows[focus.q][focus.k];
  out.push({ shape: rect(tokenX(focus.q) - 18, ROW.y - 40, 36, 52, 3), stroke: gc.ink, width: 2, opacity: o });
  out.push(...T.mono(`reading ${e.text[focus.q]} at ${focus.q}`, { x: tokenX(focus.q), y: ROW.y - 330, anchor: "center", size: 22, opacity: o, ground: g }));
  out.push(...T.mono(`after the earlier ${e.text[focus.copy]} came ${e.text[focus.k]}  ·  weight ${show(w, "f2")}`, { x: tokenX(focus.k), y: ROW.y - 330, anchor: "center", size: 22, opacity: o, ground: g, color: gc.verified }));
  return out;
}
