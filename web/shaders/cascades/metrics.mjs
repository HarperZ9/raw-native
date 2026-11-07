// Error measures for fluence images (Float64Array or Float32Array, w*h*3, linear RGB).
//   rmse(a, b)                      root mean square over every channel of every pixel
//   relRmse(a, ref)                 rmse divided by the reference's mean value
//   tileBootstrap(a, ref, w, h)     95% interval of relRmse, resampling 32 x 32 tiles (pixels are
//                                   correlated, so tiles are the independent unit)
//   errorMap(a, ref)                per-pixel |a - ref| averaged over channels
//   bandErrors(a, ref, w, h, distOf, edges)  relRmse per band of a per-pixel distance
import { makeRng } from "./trace.mjs";

export function rmse(a, b, mask = null) {
  let s = 0, n = 0;
  for (let i = 0; i < a.length; i++) { if (mask && !mask[(i / 3) | 0]) continue; const d = a[i] - b[i]; s += d * d; n++; }
  return Math.sqrt(s / Math.max(1, n));
}
export function mean(a, mask = null) {
  let s = 0, n = 0;
  for (let i = 0; i < a.length; i++) { if (mask && !mask[(i / 3) | 0]) continue; s += a[i]; n++; }
  return s / Math.max(1, n);
}
export function relRmse(a, ref, mask = null) { return rmse(a, ref, mask) / Math.max(1e-30, mean(ref, mask)); }

export function errorMap(a, ref) {
  const n = a.length / 3, out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = (Math.abs(a[3 * i] - ref[3 * i]) + Math.abs(a[3 * i + 1] - ref[3 * i + 1]) + Math.abs(a[3 * i + 2] - ref[3 * i + 2])) / 3;
  return out;
}

export function tileBootstrap(a, ref, w, h, { tile = 32, reps = 1000, seed = 7 } = {}) {
  const tw = Math.ceil(w / tile), th = Math.ceil(h / tile), se = new Float64Array(tw * th), sr = new Float64Array(tw * th), cnt = new Float64Array(tw * th);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const t = ((y / tile) | 0) * tw + ((x / tile) | 0), o = 3 * (y * w + x);
    for (let c = 0; c < 3; c++) { const d = a[o + c] - ref[o + c]; se[t] += d * d; sr[t] += ref[o + c]; cnt[t]++; }
  }
  const rng = makeRng(seed), T = tw * th, vals = [];
  for (let r = 0; r < reps; r++) {
    let e = 0, m = 0, n = 0;
    for (let k = 0; k < T; k++) { const t = Math.floor(rng() * T); e += se[t]; m += sr[t]; n += cnt[t]; }
    vals.push(Math.sqrt(e / n) / (m / n));
  }
  vals.sort((p, q) => p - q);
  return { lo: vals[Math.floor(0.025 * reps)], hi: vals[Math.floor(0.975 * reps)], tiles: T };
}

// relRmse inside each band [edges[k], edges[k+1]) of distOf(x, y); the image's overall relRmse is
// computed with the same normaliser (the whole image's mean) so the bands compare directly with it.
export function bandErrors(a, ref, w, h, distOf, edges) {
  const m = mean(ref), out = [];
  for (let k = 0; k + 1 < edges.length; k++) {
    const mask = new Uint8Array(w * h);
    let n = 0;
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const d = distOf(x, y); if (d >= edges[k] && d < edges[k + 1]) { mask[y * w + x] = 1; n++; } }
    out.push({ from: edges[k], to: edges[k + 1], pixels: n, rmse_over_image_mean: n ? rmse(a, ref, mask) / m : null });
  }
  return { image_rmse_over_mean: rmse(a, ref) / m, bands: out };
}
