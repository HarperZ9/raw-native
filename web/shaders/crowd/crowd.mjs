// Peripheral crowding: the edge of the frame becomes plausible texture that cannot be read.
//
// Away from where we look, vision keeps local statistics and loses arrangement: features crowd
// together into "stuff" (Rosenholtz; Freeman and Simoncelli 2011; real-time ventral metamers,
// Walton et al. 2021, for foveated rendering). Used deliberately, it makes a thriller's frame feel
// watched and unreliable without a single jump scare: the centre is sharp and the corners hold
// shapes that will not resolve. This pass is a cheap, owned approximation, not a metamer model:
//   pooling radius rho(x) = s max(0, ecc - e0) H, capped at rhoMax H   (ecc = |x - gaze| / H)
//   B(x) = the mean over a box of radius rho(x) (two passes, fractional end taps)
//   detail is displaced in log-polar cells around the gaze: x' = x + d(cell), |d| <= rho
//   S(x) = the local standard deviation over the same box (from the mean of D^2)
//   out(x) = B(x) + (D(x') - B(x')) S(x) / max(S(x'), eps)
// so the local mean stays put, the detail takes the local contrast, and its arrangement scrambles more
// the further out it is. (A first version took B from a box pyramid; its grid-aligned, power-of-two
// boxes moved the local mean by up to 0.076 near the lit window, so B is now a centred box.)
// Inside the fovea (rho < 0.5 px) the picture is untouched. The cells reseed
// every `reseed` frames with a crossfade, so the periphery crawls slowly instead of shimmering.
import { hash3 } from "../common.mjs";

export const CROWD_PRESETS = {
  thriller: { similar: 0.04, s: 0.5, e0: 0.12, rhoMax: 0.12, cellsPerRing: 24, ringStep: 0.18, reseed: 48, fade: 24, seed: 7 },
  dread: { similar: 0.06, s: 0.85, e0: 0.08, rhoMax: 0.18, cellsPerRing: 18, ringStep: 0.22, reseed: 72, fade: 36, seed: 11 },
};
export function resolveCrowd(preset, overrides = {}, size) {
  const p = { ...CROWD_PRESETS[preset], gaze: [0.5, 0.5], ...overrides };
  return { p, w: size.w, h: size.h };
}
export function rhoAt(plan, x, y) {
  const p = plan.p, H = plan.h, ecc = Math.hypot(x + 0.5 - p.gaze[0] * plan.w, y + 0.5 - p.gaze[1] * H) / H;
  return Math.min(p.rhoMax * H, p.s * Math.max(0, ecc - p.e0) * H);
}
// Box weights for a fractional radius: taps |j| <= floor(r) weigh 1, taps at floor(r) + 1 weigh the fraction.
const tapW = (j, r) => { const a = Math.abs(j), ri = Math.floor(r); return a <= ri ? 1 : a === ri + 1 ? r - ri : 0; };
// Local mean and mean of squares per pixel, per channel, over the box of radius rho(x, y).
// Returns Float64Array(w * h * 6): [mean r, g, b, mean of squares r, g, b].
export function localStats(plan, D) {
  const { w, h } = plan, n = w * h, Hs = new Float64Array(n * 6), S = new Float64Array(n * 6);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const r = rhoAt(plan, x, y), R = Math.floor(r) + 1; let ws = 0; const acc = [0, 0, 0, 0, 0, 0];
    for (let j = -R; j <= R; j++) { const xx = x + j, wt = tapW(j, r); if (xx < 0 || xx >= w || wt === 0) continue; ws += wt; for (let c = 0; c < 3; c++) { const v = D[(y * w + xx) * 4 + c]; acc[c] += wt * v; acc[c + 3] += wt * v * v; } }
    for (let k = 0; k < 6; k++) Hs[(y * w + x) * 6 + k] = acc[k] / ws;
  }
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const r = rhoAt(plan, x, y), R = Math.floor(r) + 1; let ws = 0; const acc = [0, 0, 0, 0, 0, 0];
    for (let j = -R; j <= R; j++) { const yy = y + j, wt = tapW(j, r); if (yy < 0 || yy >= h || wt === 0) continue; ws += wt; for (let k = 0; k < 6; k++) acc[k] += wt * Hs[(yy * w + x) * 6 + k]; }
    for (let k = 0; k < 6; k++) S[(y * w + x) * 6 + k] = acc[k] / ws;
  }
  return S;
}
// Integer displacement for pixel (x, y) with seed k: log-polar cell around the gaze.
export function displacement(plan, x, y, rho, k) {
  const p = plan.p, dx = x + 0.5 - p.gaze[0] * plan.w, dy = y + 0.5 - p.gaze[1] * plan.h, ecc = Math.hypot(dx, dy) / plan.h;
  const ring = Math.floor(Math.log(Math.max(ecc, 1e-6) / p.e0) / p.ringStep), sector = Math.floor(((Math.atan2(dy, dx) / (2 * Math.PI)) + 0.5) * p.cellsPerRing);
  const a = (ring + 1024) >>> 0, b = (sector + 1024) >>> 0, s = (p.seed * 7919 + k) >>> 0;
  return [Math.round(rho * (2 * hash3(a, b, s) - 1)), Math.round(rho * (2 * hash3(a, b, s + 104729) - 1))];
}
export const EPS_S = 0.01;
function crowdWith(plan, D, S, k) {
  const { w, h } = plan, out = new Float64Array(w * h * 3);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const i = y * w + x, rho = rhoAt(plan, x, y);
    if (rho < 0.5) { for (let c = 0; c < 3; c++) out[i * 3 + c] = D[i * 4 + c]; continue; }
    // Detail only moves between places of similar local mean: halve the displacement until the
    // source's local mean (luminance) is within `similar` of this pixel's, so a lit window's edges
    // are not carried into the dark around it.
    let [ddx, ddy] = displacement(plan, x, y, rho, k), xs = x, ys = y;
    const Lm = (q) => 0.2126 * S[q * 6] + 0.7152 * S[q * 6 + 1] + 0.0722 * S[q * 6 + 2], L0 = Lm(i);
    for (let tries = 0; tries < 4; tries++) {
      const cx = Math.max(0, Math.min(w - 1, x + ddx)), cy = Math.max(0, Math.min(h - 1, y + ddy));
      if (Math.abs(Lm(cy * w + cx) - L0) <= plan.p.similar) { xs = cx; ys = cy; break; }
      ddx = Math.trunc(ddx / 2); ddy = Math.trunc(ddy / 2);
    }
    const j = ys * w + xs;
    for (let c = 0; c < 3; c++) {
      const m = S[i * 6 + c], mj = S[j * 6 + c], sx = Math.sqrt(Math.max(0, S[i * 6 + 3 + c] - m * m)), sj = Math.sqrt(Math.max(0, S[j * 6 + 3 + c] - mj * mj));
      out[i * 3 + c] = m + (D[j * 4 + c] - mj) * (sx / Math.max(sj, EPS_S));
    }
  }
  return out;
}
// Mean matching, as texture synthesis imposes statistics: the scrambled detail is not zero-mean over
// a box that straddles an edge, so measure the result's local mean and put the input's back:
// out += B_in - B_out (once; outside the fovea only).
function matchMean(plan, S, O) {
  const { w, h } = plan, o4 = new Float64Array(w * h * 4);
  for (let i = 0; i < w * h; i++) for (let c = 0; c < 3; c++) o4[i * 4 + c] = O[i * 3 + c];
  const So = localStats(plan, o4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const i = y * w + x; if (rhoAt(plan, x, y) < 0.5) continue; for (let c = 0; c < 3; c++) O[i * 3 + c] += S[i * 6 + c] - So[i * 6 + c]; }
  return O;
}
// A frame: display-linear RGBA in and out. frameNo drives the slow reseed and its crossfade.
export function runCrowd(plan, src, frameNo = 0) {
  const p = plan.p, S = localStats(plan, src.data), k = Math.floor(frameNo / p.reseed), into = frameNo - k * p.reseed;
  const A = (plan.p.noMatch ? (x) => x : (O) => matchMean(plan, S, O))(crowdWith(plan, src.data, S, k)), t = into < p.fade && k > 0 ? into / p.fade : 1, Bk = t < 1 ? matchMean(plan, S, crowdWith(plan, src.data, S, k - 1)) : null;
  const o = new Float32Array(plan.w * plan.h * 4);
  for (let i = 0; i < plan.w * plan.h; i++) { for (let c = 0; c < 3; c++) o[i * 4 + c] = Bk ? Bk[i * 3 + c] * (1 - t) + A[i * 3 + c] * t : A[i * 3 + c]; o[i * 4 + 3] = 1; }
  return { width: plan.w, height: plan.h, data: o };
}
