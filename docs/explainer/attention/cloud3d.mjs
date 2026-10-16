// The residual cloud in depth: 1,048,576 numbers as 16 slabs of 16 sequences, spaced in z. The
// camera starts deep in the stack and pulls back through it until slab 0 faces it flat, then
// pushes into sequence 0. Pure layout here; the GPU layers are made in the scene's load().
import { span, ease, window as win } from "../../../web/motion/timeline.mjs";
import { tensorGrid, show } from "../../../web/explainer/index.mjs";
import { CUE } from "./script.mjs";

export const SLABS = 16, PER = 16, GAP_Z = 260;
export const slabGrid = () => tensorGrid({ shape: [PER, 64, 64], x: 300, y: 70, w: 1320, h: 820 });

// How far the stack is pushed toward the camera at t (scene units): deep inside at the start of
// the beat, zero when slab 0 sits flat at the focal plane.
export function travel(t, reduced) {
  if (reduced) return t < CUE.cloud + 3 ? (SLABS - 1) * GAP_Z * 0.6 : 0;
  return (SLABS - 1) * GAP_Z * 1.15 * (1 - span(t, CUE.cloud - 0.5, CUE.cloudPush - 0.5, ease.inOut));
}

// One item per slab, back to front, faded as it nears or passes the camera (depth 1000 + z).
export function slabs(t, A, ctx, cam) {
  if (!A.slabs || t < CUE.cloud - 1.5 || t > CUE.task + 1) return [];
  const o = win(t, CUE.cloud - 1.5, CUE.task + 1, 1.5, 1.2), tr = travel(t, ctx.reduced);
  const fine = span(cam.zoom, 1.6, 2.8, ease.linear), items = [];
  for (let k = SLABS - 1; k >= 0; k--) {
    const z = k * GAP_Z - tr, depth = 1000 + z;
    if (depth < 60) continue;
    const near = span(depth, 120, 520, ease.linear);
    const behind = k === 0 ? 1 - fine : 1 - span(t, CUE.cloudPush - 0.5, CUE.cloudPush + 1, ease.inOut);   // the stack clears for the push
    const op = o * near * behind * (1 - 0.5 * span(z, 600, 3800, ease.linear));
    if (op <= 0.003) continue;
    items.push({ ...A.slabItem, layer: A.slabs[k], z, opacity: op, sel: k === 0 ? A.sel ?? -1 : -1 });
  }
  if (fine > 0) items.push({ kind: "layer", layer: A.tile, keep: 1, radius: A.grid.cell * 0.36, range: A.range, ramp: A.RAMP, gainScale: 0.22, opacity: o * fine, sel: A.selTile ?? -1 });
  return items;
}

export function slabLabels(t, A, ctx, cam) {
  const T = A.T, g = "night", o = win(t, CUE.cloud, CUE.task, 0.8, 0.6), out = [];
  if (o <= 0) return out;
  const [S, P, D] = A.d.residual.shape;
  out.push(...T.meta(`The final residual stream: ${S} sequences, ${P} positions, ${D} numbers each, in ${SLABS} layers of ${PER} sequences`,
    { x: 110, y: 44, reveal: typeOnLinear(t, CUE.cloud + 0.3, 1.5), opacity: o, ground: g, screen: true }));
  if (A.slabs && ctx.motion && cam.zoom < 1.6) {
    let drawn = 0;
    const tr = travel(t, ctx.reduced);
    A.slabs.forEach((s, k) => { const l = s.lodFor({ ...A.slabItem, z: k * GAP_Z - tr }, cam, ctx.motion.w, ctx.motion.h); if (l && 1000 + k * GAP_Z - tr >= 60) drawn += l.drawn; });
    out.push(...T.meta(`Drawn this frame: ${show(drawn, "int")} of ${show(S * P * D, "int")}, by screen density`, { x: 110, y: 70, opacity: o * 0.8, ground: g, screen: true }));
  }
  if (cam.zoom > 2.4) out.push(...T.meta("Sequence 0: rows are positions, columns are the 64 numbers at each", { x: 110, y: 990, opacity: o * span(cam.zoom, 2.4, 3.2), ground: g, screen: true }));
  return out;
}

const typeOnLinear = (t, a, d) => span(t, a, a + d, ease.linear);
