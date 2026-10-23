// Stroke program: the painter's brush strokes kept as data instead of being thrown away after they are
// rasterised. Prototype for the vector-rendering experiment (mission-control/2026-10-23-vector-rendering).
// Original code. The strokes are the ones web/shaders/paintover/painter.mjs lays down (Hertzmann 1998 style
// coarse-to-fine painting); this file is only the container, a reference rasteriser that evaluates the same
// formulas at any output scale, a tile binner, and the packers the GPU pass reads.
//
// Stroke (plate pixels): segment a->e, half width, display colour, object id, an id0 for the stripe noise,
// and a layer (0 = coarsest). Order is paint order. A stroke only covers pixels whose object id equals its id.
import { pcg } from "../common.mjs";

const sat = (x) => (x < 0 ? 0 : x > 1 ? 1 : x);
const hash2 = (a, b, s) => pcg((a | 0) ^ pcg((b | 0) ^ pcg(s | 0))) / 4294967296;
const smooth = (t) => t * t * (3 - 2 * t);
export function vnoise(x, y, s) {
  const x0 = Math.floor(x), y0 = Math.floor(y), fx = smooth(x - x0), fy = smooth(y - y0);
  const a = hash2(x0, y0, s), b = hash2(x0 + 1, y0, s), c = hash2(x0, y0 + 1, s), d = hash2(x0 + 1, y0 + 1, s);
  return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

// ---- compact file format, 13 bytes a stroke: ax ay (i16, 1/8 px), ex-ax ey-ay (i16, 1/8 px), half width
// (u8, 1/16 px), r g b (u8), object id (u8). Layer comes from the counts; id0 is derived from the end points.
export const Q_POS = 8, Q_HW = 16, STROKE_BYTES = 13;
const derivedId0 = (qx, qy) => (hash2(qx, qy, 1234) * 1000) | 0;
export function toCompact(strokes) {
  return strokes.map((s) => {
    const qx = Math.round(s.ax * Q_POS), qy = Math.round(s.ay * Q_POS);
    return { layer: s.layer, qx, qy, qdx: Math.round((s.ex - s.ax) * Q_POS), qdy: Math.round((s.ey - s.ay) * Q_POS), qhw: Math.min(255, Math.round(s.halfW * Q_HW)), rgb: s.col.map((v) => Math.round(sat(v) * 255)), sid: s.sid };
  });
}
export function packProgram(strokes, meta) {
  const c = toCompact(strokes), counts = [0, 0, 0, 0]; for (const s of c) counts[s.layer]++;
  const head = Buffer.alloc(16 + 4 * counts.length); head.write("SPG1", 0); head.writeUInt16LE(meta.W, 4); head.writeUInt16LE(meta.H, 6); head.writeUInt16LE(meta.seed, 8); head.writeUInt8(counts.length, 10); head.writeUInt8(Math.round(meta.relief * 100), 11);
  counts.forEach((n, i) => head.writeUInt32LE(n, 16 + 4 * i));
  const body = Buffer.alloc(c.length * STROKE_BYTES); let o = 0;
  for (const s of c) { body.writeInt16LE(s.qx, o); body.writeInt16LE(s.qy, o + 2); body.writeInt16LE(s.qdx, o + 4); body.writeInt16LE(s.qdy, o + 6); body[o + 8] = s.qhw; body[o + 9] = s.rgb[0]; body[o + 10] = s.rgb[1]; body[o + 11] = s.rgb[2]; body[o + 12] = s.sid; o += STROKE_BYTES; }
  return Buffer.concat([head, body]);
}
// Decode to the float form the rasteriser takes. dx, dy: unit direction of the segment ((1,0) when it has no length).
export function unpackProgram(buf) {
  if (buf.toString("latin1", 0, 4) !== "SPG1") throw new Error("not a stroke program");
  const nl = buf.readUInt8(10), counts = Array.from({ length: nl }, (_, i) => buf.readUInt32LE(16 + 4 * i)), strokes = [];
  let o = 16 + 4 * nl;
  for (let l = 0; l < nl; l++) for (let k = 0; k < counts[l]; k++, o += STROKE_BYTES) {
    const qx = buf.readInt16LE(o), qy = buf.readInt16LE(o + 2), qdx = buf.readInt16LE(o + 4), qdy = buf.readInt16LE(o + 6), len = Math.hypot(qdx, qdy);
    strokes.push({ layer: l, ax: qx / Q_POS, ay: qy / Q_POS, ex: (qx + qdx) / Q_POS, ey: (qy + qdy) / Q_POS, dx: len > 0 ? qdx / len : 1, dy: len > 0 ? qdy / len : 0, halfW: buf[o + 8] / Q_HW, col: [buf[o + 9] / 255, buf[o + 10] / 255, buf[o + 11] / 255], sid: buf[o + 12], id0: derivedId0(qx, qy) });
  }
  return { strokes, meta: { W: buf.readUInt16LE(4), H: buf.readUInt16LE(6), seed: buf.readUInt16LE(8), relief: buf.readUInt8(11) / 100, counts } };
}

// ---- underpaint: the painter starts from the reference blurred by sigma 9. Kept at 1/8 size (bilinear back up).
export function blur(plane, w, h, sigma) {
  const r = Math.ceil(3 * sigma), k = new Float64Array(2 * r + 1); let sum = 0;
  for (let i = -r; i <= r; i++) { k[i + r] = Math.exp(-0.5 * (i / sigma) ** 2); sum += k[i + r]; }
  for (let i = 0; i < k.length; i++) k[i] /= sum;
  const tmp = new Float32Array(w * h), out = new Float32Array(w * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { let a = 0; for (let i = -r; i <= r; i++) a += k[i + r] * plane[y * w + Math.min(w - 1, Math.max(0, x + i))]; tmp[y * w + x] = a; }
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { let a = 0; for (let i = -r; i <= r; i++) a += k[i + r] * tmp[Math.min(h - 1, Math.max(0, y + i)) * w + x]; out[y * w + x] = a; }
  return out;
}
export const UNDER_F = 8;
// rgb: Float32 0..1, 3 per pixel. Returns { w, h, data: Float32 3 per texel } sampled at the blurred image's block centres.
export function makeUnderpaint(rgb, W, H) {
  const uw = Math.ceil(W / UNDER_F), uh = Math.ceil(H / UNDER_F), data = new Float32Array(uw * uh * 3);
  for (let c = 0; c < 3; c++) {
    const p = new Float32Array(W * H); for (let i = 0; i < W * H; i++) p[i] = rgb[i * 3 + c];
    const b = blur(p, W, H, 9);
    for (let y = 0; y < uh; y++) for (let x = 0; x < uw; x++) data[(y * uw + x) * 3 + c] = b[Math.min(H - 1, y * UNDER_F + UNDER_F / 2) * W + Math.min(W - 1, x * UNDER_F + UNDER_F / 2)];
  }
  return { w: uw, h: uh, data };
}
const sampleUnder = (u, x, y) => {   // x, y in plate pixels (index space); texel centres sit at k*8+4
  const fx = Math.max(0, Math.min(u.w - 1, (x - UNDER_F / 2) / UNDER_F)), fy = Math.max(0, Math.min(u.h - 1, (y - UNDER_F / 2) / UNDER_F));
  const x0 = Math.floor(fx), y0 = Math.floor(fy), x1 = Math.min(u.w - 1, x0 + 1), y1 = Math.min(u.h - 1, y0 + 1), tx = fx - x0, ty = fy - y0, o = [0, 0, 0];
  for (let c = 0; c < 3; c++) o[c] = (u.data[(y0 * u.w + x0) * 3 + c] * (1 - tx) + u.data[(y0 * u.w + x1) * 3 + c] * tx) * (1 - ty) + (u.data[(y1 * u.w + x0) * 3 + c] * (1 - tx) + u.data[(y1 * u.w + x1) * 3 + c] * tx) * ty;
  return o;
};

// ---- reference rasteriser (CPU). Output pixel (X, Y) at scale s sits at plate coordinates (X + .5)/s - .5.
// opt: scale, ids { data Uint8, w, h } (any resolution; nearest lookup by normalised position), under (makeUnderpaint) or
// underExact { rgb, W, H } to use the painter's own blurred canvas, edge (device px of antialias ramp, painter: 1.2 at 1x).
// Returns { w, h, canvas Float32 4 per px (rgb, height), rgba8 } with the painter's relief and tooth applied.
export function renderProgram(strokes, meta, { scale = 1, ids, under, canvas0 = null, edge = 1.2, relief = meta.relief, post = true }) {
  const { W, H, seed, radii } = meta, w = Math.round(W * scale), h = Math.round(H * scale), s = scale;
  const cv = new Float32Array(w * h * 4);
  for (let Y = 0; Y < h; Y++) for (let X = 0; X < w; X++) {
    const i = (Y * w + X) * 4;
    if (canvas0) { for (let c = 0; c < 3; c++) cv[i + c] = canvas0[c][Y * w + X]; } else { const u = sampleUnder(under, (X + 0.5) / s - 0.5, (Y + 0.5) / s - 0.5); cv[i] = u[0]; cv[i + 1] = u[1]; cv[i + 2] = u[2]; }
  }
  const idAt = (X, Y) => ids.data[Math.min(ids.h - 1, Math.floor((Y + 0.5) / h * ids.h)) * ids.w + Math.min(ids.w - 1, Math.floor((X + 0.5) / w * ids.w))];
  for (const k of strokes) {
    const r = radii[k.layer], sx = k.ex - k.ax, sy = k.ey - k.ay, sl = Math.max(1e-6, sx * sx + sy * sy), pad = Math.ceil(k.halfW + 2), freq = 2.2 / Math.max(1, r * 0.35);
    const X0 = Math.max(0, Math.floor((Math.min(k.ax, k.ex) - pad + 0.5) * s - 0.5)), X1 = Math.min(w - 1, Math.ceil((Math.max(k.ax, k.ex) + pad + 0.5) * s - 0.5));
    const Y0 = Math.max(0, Math.floor((Math.min(k.ay, k.ey) - pad + 0.5) * s - 0.5)), Y1 = Math.min(h - 1, Math.ceil((Math.max(k.ay, k.ey) + pad + 0.5) * s - 0.5));
    for (let Y = Y0; Y <= Y1; Y++) for (let X = X0; X <= X1; X++) {
      if (idAt(X, Y) !== k.sid) continue;
      const x = (X + 0.5) / s - 0.5, y = (Y + 0.5) / s - 0.5;
      const tt = Math.min(1, Math.max(0, ((x - k.ax) * sx + (y - k.ay) * sy) / sl)), dist = Math.hypot(k.ax + sx * tt - x, k.ay + sy * tt - y);
      let a = sat((k.halfW - dist) * s / edge + 0.5); if (a <= 0) continue;
      const perp = (x - k.ax) * -k.dy + (y - k.ay) * k.dx, stripe = 0.8 + 0.2 * vnoise(perp * freq, tt * sl * 0.0004 + k.id0, seed + 17);
      a *= stripe + 0.12;
      const shade = 0.95 + 0.1 * stripe, i = (Y * w + X) * 4;
      for (let c = 0; c < 3; c++) cv[i + c] = cv[i + c] * (1 - a) + sat(k.col[c] * shade) * a;
      cv[i + 3] = cv[i + 3] * (1 - a) + a * (stripe * (0.55 + 0.45 * (1 - dist / k.halfW)));
    }
  }
  const rgba8 = new Uint8ClampedArray(w * h * 4);
  for (let Y = 0; Y < h; Y++) for (let X = 0; X < w; X++) {
    const i = (Y * w + X) * 4, xm = Math.max(0, X - 1), xp = Math.min(w - 1, X + 1), ym = Math.max(0, Y - 1), yp = Math.min(h - 1, Y + 1);
    const hx = (cv[(Y * w + xp) * 4 + 3] - cv[(Y * w + xm) * 4 + 3]), hy = (cv[(yp * w + X) * 4 + 3] - cv[(ym * w + X) * 4 + 3]);
    const lit = 1 + relief * (-0.8 * hx - 0.6 * hy), tooth = 0.965 + 0.035 * vnoise(X * 1.1, Y * 1.1, seed + 41);
    for (let c = 0; c < 3; c++) rgba8[i + c] = Math.round(sat(cv[i + c] * lit * tooth) * 255); rgba8[i + 3] = 255;
  }
  return { w, h, canvas: cv, rgba8 };
}

// ---- tile bins for the GPU pass: stable (paint order) lists of stroke indices per tile of `tile` plate pixels.
export function binStrokes(strokes, W, H, tile = 16) {
  const tx = Math.ceil(W / tile), ty = Math.ceil(H / tile), counts = new Uint32Array(tx * ty + 1), span = (k) => {
    const pad = Math.ceil(k.halfW + 2);
    return [Math.max(0, Math.floor((Math.min(k.ax, k.ex) - pad) / tile)), Math.min(tx - 1, Math.floor((Math.max(k.ax, k.ex) + pad) / tile)), Math.max(0, Math.floor((Math.min(k.ay, k.ey) - pad) / tile)), Math.min(ty - 1, Math.floor((Math.max(k.ay, k.ey) + pad) / tile))];
  };
  strokes.forEach((k) => { const [a, b, c, d] = span(k); for (let y = c; y <= d; y++) for (let x = a; x <= b; x++) counts[y * tx + x + 1]++; });
  for (let i = 0; i < tx * ty; i++) counts[i + 1] += counts[i];
  const list = new Uint32Array(counts[tx * ty]), cur = counts.slice(0, tx * ty);
  strokes.forEach((k, idx) => { const [a, b, c, d] = span(k); for (let y = c; y <= d; y++) for (let x = a; x <= b; x++) list[cur[y * tx + x]++] = idx; });
  return { tx, ty, tile, offsets: counts, list };
}
// 16-byte GPU record per stroke (from the compact form): w0 = qx | qy << 16, w1 = qdx | qdy << 16, w2 = r | g << 8 | b << 16 | qhw << 24,
// w3 = sid | layer << 8 | id0 << 16.
export function gpuRecords(strokes) {
  const c = toCompact(strokes), u = new Uint32Array(c.length * 4);
  c.forEach((s, i) => {
    u[i * 4] = ((s.qx & 0xffff) | ((s.qy & 0xffff) << 16)) >>> 0; u[i * 4 + 1] = ((s.qdx & 0xffff) | ((s.qdy & 0xffff) << 16)) >>> 0;
    u[i * 4 + 2] = (s.rgb[0] | (s.rgb[1] << 8) | (s.rgb[2] << 16) | (s.qhw << 24)) >>> 0; u[i * 4 + 3] = (s.sid | (s.layer << 8) | (derivedId0(s.qx, s.qy) << 16)) >>> 0;
  });
  return u;
}
