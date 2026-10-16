// Temporal shimmer metrics for dither-quantised lighting (protocol:
// project-docs/DITHER-LIGHTING-PROTOCOL.md, evidence/ditherlight-motion-protocol.json).
//
// L, surface lock: over the valid pixels of frame t, the fraction whose output level differs
//    from the level of the pixel that showed the same surface point in frame t-1 (exact motion
//    vector, nearest pixel). It measures a pattern that slides over a surface.
// F, boil: 100 x the mean over all pixels of |e_t - e_(t-1)| in screen coordinates, with
//    e = cbrt(box4(out)) - cbrt(box4(tone)) in linear light (4 x 4 box, edge clamped). It
//    measures how much the dither error moves on the screen from one frame to the next.
// A, accuracy: 100 x the mean of |e_t|.
// S, seam: for world-octave anchoring, the fraction of valid pixels whose octave differs from
//    that of their reprojected pixel.
// A pair is valid at pixel p when p shows a lit surface, the reprojected pixel q = round(prev)
// is on screen and shows the same object, and the depth at q is within 3% of the reprojected
// depth (+ 2 cm). Disocclusions and silhouettes are dropped.
import { anchorCell, octaveOf } from "../../web/shaders/ditherlight/thresholds.mjs";

export function reproject(g0, g1) {
  const { width: w, height: h } = g1, q = new Int32Array(w * h).fill(-1), dx = new Float32Array(w * h), dy = new Float32Array(w * h);
  for (let i = 0; i < w * h; i++) {
    if (g1.kind[i] !== 2) continue;
    const px = g1.prev[i * 3], py = g1.prev[i * 3 + 1], pz = g1.prev[i * 3 + 2];
    if (!(px === px)) continue;
    const qx = Math.round(px), qy = Math.round(py);
    if (qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
    const j = qy * w + qx;
    if (g0.kind[j] !== 2 || g0.id[j] !== g1.id[i] || Math.abs(g0.depth[j] - pz) > 0.03 * pz + 0.02) continue;
    q[i] = j; dx[i] = px - qx; dy[i] = py - qy;
  }
  return { q, dx, dy };
}

export function surfaceLock(r, level0, level1, levels = 2) {
  let n = 0, s = 0;
  for (let i = 0; i < r.q.length; i++) if (r.q[i] >= 0) { n++; s += Math.abs(level1[i] - level0[r.q[i]]) / (levels - 1); }
  return n ? s / n : NaN;
}
export function validCount(r) { let n = 0; for (let i = 0; i < r.q.length; i++) if (r.q[i] >= 0) n++; return n; }

// cbrt of the 4 x 4 box (offsets -2 .. +1, edge clamped) of a linear single-channel image.
export function boxCbrt(a, w, h) {
  const o = new Float64Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    let s = 0;
    for (let dy = -2; dy < 2; dy++) { const yy = Math.min(h - 1, Math.max(0, y + dy)) * w; for (let dx = -2; dx < 2; dx++) s += a[yy + Math.min(w - 1, Math.max(0, x + dx))]; }
    o[y * w + x] = Math.cbrt(s / 16);
  }
  return o;
}
// The error image e of one frame. outLin: the output's linear value per pixel; tone: the tone.
export function errorImage(outLin, tone, w, h) {
  const a = boxCbrt(outLin, w, h), b = boxCbrt(tone, w, h), e = new Float64Array(w * h);
  for (let i = 0; i < e.length; i++) e[i] = a[i] - b[i];
  return e;
}
export function boil(e0, e1) { let s = 0; for (let i = 0; i < e0.length; i++) s += Math.abs(e1[i] - e0[i]); return (100 * s) / e0.length; }
export function accuracy(e) { let s = 0; for (let i = 0; i < e.length; i++) s += Math.abs(e[i]); return (100 * s) / e.length; }

export function seam(plan, g0, g1, r) {
  let n = 0, c = 0;
  for (let i = 0; i < r.q.length; i++) if (r.q[i] >= 0) { n++; if (octaveOf(plan.cell, plan.px, plan.pa, g1.depth[i]) !== octaveOf(plan.cell, plan.px, plan.pa, g0.depth[r.q[i]])) c++; }
  return n ? c / n : NaN;
}

// The model behind the prediction: expected L from the motion field and the tone alone, without
// running a threshold. For a pixel with ideal 1-bit fraction f the two thresholds read are
// independent, so the levels differ with probability 2 f (1 - f) when the cells differ. Screen
// anchoring reads different cells exactly when round(prev) != the pixel itself. World anchoring
// reads different cells when the sub-pixel residual of the reprojection moves the anchor
// across a cell edge, with probability min(1, (|dx| + |dy|) / cellPixels), cellPixels being the
// cell's size in pixels at this depth.
export function predictL(plan, g1, r, anchor) {
  let n = 0, s = 0;
  for (let i = 0; i < r.q.length; i++) {
    if (r.q[i] < 0) continue;
    const f = Math.min(1, Math.max(0, g1.tone[i])), indep = 2 * f * (1 - f);
    let pDiff;
    if (anchor === "screen") pDiff = r.q[i] === i ? 0 : 1;
    else {
      const size = plan.cell * (anchor === "world-octave" ? Math.pow(2, octaveOf(plan.cell, plan.px, plan.pa, g1.depth[i])) : 1), cellPx = size / (plan.pa * g1.depth[i]);
      pDiff = Math.min(1, (Math.abs(r.dx[i]) + Math.abs(r.dy[i])) / cellPx);
    }
    n++; s += indep * pDiff;
  }
  return n ? s / n : NaN;
}
export { anchorCell };
