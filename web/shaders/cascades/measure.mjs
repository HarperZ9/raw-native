// Measurement of the GPU radiance cascades against the GPU brute force, in the browser.
//   const res = await measureScene(host, { scene: "glass", w: 3840, h: 2160, branch: 2, n0: 4, r0: 1,
//     fix: false, rays: 4096, stride: 2, frames: 50, images: true });
// Returns accuracy (relative RMSE with a tile-bootstrap interval, the reference's own noise from a
// second seed), timing (GPU timestamps per frame), the planted analytic check for "disk", the
// dark-room leak for "room", errors by distance from the glass for "glass", and PNGs (base64).
import { buildScene, diskFluence, interfaceDistance } from "./scenes.mjs";
import { createGpuCascades } from "./gpu.mjs";
import { atGrid } from "./cascades.mjs";
import { relRmse, rmse, mean, tileBootstrap, errorMap, bandErrors } from "./metrics.mjs";

export function quantiles(v) {
  const s = [...v].sort((a, b) => a - b), q = (p) => s[Math.min(s.length - 1, Math.floor(p * s.length))];
  return { median: q(0.5), p05: q(0.05), p95: q(0.95), n: s.length };
}

async function png(w, h, rgba) {
  const c = new OffscreenCanvas(w, h), g = c.getContext("2d");
  g.putImageData(new ImageData(rgba, w, h), 0, 0);
  const buf = new Uint8Array(await (await c.convertToBlob({ type: "image/png" })).arrayBuffer());
  let s = ""; for (let i = 0; i < buf.length; i += 0x8000) s += String.fromCharCode.apply(null, buf.subarray(i, i + 0x8000));
  return btoa(s);
}
// Display: average radiance (fluence / 2 pi), shoulder 1 - exp(-gain v), sRGB.
export function encodeFluence(F, n, gain = 3) {
  const o = new Uint8ClampedArray(4 * n), k = 1 / (2 * Math.PI);
  const enc = (v) => { const l = 1 - Math.exp(-Math.max(0, v * k) * gain); return 255 * (l <= 0.0031308 ? l * 12.92 : 1.055 * l ** (1 / 2.4) - 0.055); };
  for (let i = 0; i < n; i++) { o[4 * i] = enc(F[3 * i]); o[4 * i + 1] = enc(F[3 * i + 1]); o[4 * i + 2] = enc(F[3 * i + 2]); o[4 * i + 3] = 255; }
  return o;
}
// Error map: |error| over the reference's image mean, 0 to `top`, on a dark-to-light ramp.
export function encodeError(E, scale, top = 0.5) {
  const o = new Uint8ClampedArray(4 * E.length), ramp = [[0, 0, 0], [40, 40, 160], [200, 60, 120], [250, 200, 60], [255, 255, 255]];
  for (let i = 0; i < E.length; i++) {
    const x = Math.min(1, E[i] / scale / top) * (ramp.length - 1), k = Math.min(ramp.length - 2, Math.floor(x)), f = x - k;
    for (let c = 0; c < 3; c++) o[4 * i + c] = ramp[k][c] + f * (ramp[k + 1][c] - ramp[k][c]);
    o[4 * i + 3] = 255;
  }
  return o;
}

export async function measureScene(host, q) {
  const sc = buildScene(q.scene, q.w, q.h), opts = { branch: q.branch, n0: q.n0, r0: q.r0, fix: !!q.fix, band: q.band || null };
  const rc = await createGpuCascades(host, sc, opts);
  rc.frame(); const F = await rc.read();
  const timing = await rc.time(q.frames);
  const ref1 = await rc.reference({ rays: q.rays, stride: q.stride, seed: 1, chunk: q.chunk || 32 });
  const ref2 = await rc.reference({ rays: q.rays, stride: q.stride, seed: 2, chunk: q.chunk || 32 });
  const g = ref1.grid, Fg = atGrid(F, sc.W, sc.H, q.stride), R = ref1.fluence;
  const res = {
    scene: q.scene, size: [sc.W, sc.H], options: opts, cascades: rc.plan.cascades.length, gpu_bytes: rc.bytes,
    reference: { rays: q.rays, stride: q.stride, samples: [g.w, g.h], ms: [ref1.ms, ref2.ms] },
    accuracy: { rel_rmse: relRmse(Fg, R), interval95: tileBootstrap(Fg, R, g.w, g.h), ref_mean: mean(R) },
    reference_noise: { rel_rmse_between_seeds: relRmse(ref2.fluence, R), per_reference: relRmse(ref2.fluence, R) / Math.SQRT2 },
    timing: { gpu_ms: timing.gpu_ms ? quantiles(timing.gpu_ms) : null, wall_ms: quantiles(timing.wall_ms) },
  };
  if (q.scene === "disk") {
    const c = sc.circles[0], A = new Float64Array(R.length);
    for (let j = 0; j < g.h; j++) for (let i = 0; i < g.w; i++) {
      const f = diskFluence(1, c.r, Math.hypot(q.stride * i + g.off + 0.5 - c.cx, q.stride * j + g.off + 0.5 - c.cy)); A.fill(f, 3 * (j * g.w + i), 3 * (j * g.w + i) + 3);
    }
    // The planted check: the reference must match the closed form, an offset of 1% of the mean added
    // to the closed form must read 0.01 (the metric sees a known error), and the cascades are scored
    // on the same closed form.
    const m = mean(A), planted = A.map((v) => v + 0.01 * m);
    res.planted = { reference_vs_analytic: relRmse(R, A), cascades_vs_analytic: relRmse(Fg, A), metric_on_planted_1pct: relRmse(planted, A),
      cascades_vs_analytic_interval95: tileBootstrap(Fg, A, g.w, g.h) };
  }
  if (q.scene === "room") {
    const d = darkRoom(sc, q.stride, g, Fg, R);
    res.dark_room = { cascades_mean: d.cascades, reference_mean: d.reference, ratio: d.ratio, rel_rmse_in_room: d.rel };
  }
  if (q.scene === "glass") {
    const dist = (i, j) => interfaceDistance(sc, q.stride * i + g.off, q.stride * j + g.off);
    res.interface = bandErrors(Fg, R, g.w, g.h, dist, [0, 2, 8, 32, 128, 1e9]);
    const ref0 = await rc.reference({ rays: q.rays, stride: q.stride, seed: 3, chunk: q.chunk || 32, fresnel: false });
    res.interface_transmit_only = bandErrors(Fg, ref0.fluence, g.w, g.h, dist, [0, 2, 8, 32, 128, 1e9]);
  }
  if (q.images) {
    const E = errorMap(Fg, R), m = res.accuracy.ref_mean;
    res.images = { render: await png(sc.W, sc.H, encodeFluence(F, sc.W * sc.H)), reference: await png(g.w, g.h, encodeFluence(R, g.w * g.h)),
      error: await png(g.w, g.h, encodeError(E, m)) };
  }
  rc.destroy();
  return res;
}

// One reference, many cascade settings: relative RMSE (with its tile interval) and GPU time for each.
export async function sweepScene(host, q, configs) {
  const sc = buildScene(q.scene, q.w, q.h), out = [];
  let ref = null;
  for (const cfg of configs) {
    const rc = await createGpuCascades(host, sc, cfg);
    if (!ref) ref = await rc.reference({ rays: q.rays, stride: q.stride, seed: 1, chunk: q.chunk || 32 });
    rc.frame(); const F = await rc.read(), t = await rc.time(q.frames), g = ref.grid, Fg = atGrid(F, sc.W, sc.H, q.stride);
    const row = { ...cfg, cascades: rc.plan.cascades.length, gpu_bytes: rc.bytes, rel_rmse: relRmse(Fg, ref.fluence),
      interval95: tileBootstrap(Fg, ref.fluence, g.w, g.h, { reps: 300 }), gpu_ms: t.gpu_ms ? quantiles(t.gpu_ms) : null };
    if (q.scene === "room") row.dark_room_ratio = darkRoom(sc, q.stride, g, Fg, ref.fluence).ratio;
    out.push(row); rc.destroy();
  }
  return { scene: q.scene, size: [q.w, q.h], reference: { rays: q.rays, stride: q.stride, ms: ref ? ref.ms : null }, rows: out };
}

// The room behind the thin wall (scenes.mjs "room"), two pixels in from its walls.
function darkRoom(sc, stride, g, Fg, R) {
  const H = sc.H, a = sc.W / H, w = Math.max(1, Math.round(H / 1080)), mask = new Uint8Array(g.w * g.h);
  for (let j = 0; j < g.h; j++) for (let i = 0; i < g.w; i++) {
    const x = stride * i + g.off + 0.5, y = stride * j + g.off + 0.5;
    mask[j * g.w + i] = x > 0.62 * a * H + w + 2 && x < 0.95 * a * H - w - 2 && y > 0.05 * H + w + 2 && y < 0.95 * H - w - 2 ? 1 : 0;
  }
  const c = mean(Fg, mask), r = mean(R, mask);
  return { cascades: c, reference: r, ratio: c / r, rel: rmse(Fg, R, mask) / r };
}
