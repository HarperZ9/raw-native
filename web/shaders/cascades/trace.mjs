// The flatland ray tracer shared by the radiance cascades and the brute-force reference (CPU, float64).
// cascades.wgsl.mjs is the same algorithm in WGSL, step for step.
//
// A ray walks the pixel grid exactly (each crossed pixel contributes for its true chord length) and
// skips empty space with an occupancy pyramid: level l marks every 2^l x 2^l block holding any
// pixel with sigma > 0, and a ray in an empty block jumps to the block's exit. Analytic circles are
// intersected exactly; a glass circle bends the ray (Snell) and, in "fresnel" mode, picks reflection
// with probability F (unpolarised Fresnel), so the reference is unbiased for the dielectric.

export const MIP_LEVELS = 8;
export const MAX_EVENTS = 32;

export function buildMips(grid, W, H) {
  const levels = [];
  let w = W, h = H, prev = new Uint8Array(W * H);
  for (let i = 0; i < W * H; i++) prev[i] = grid[4 * i + 3] > 0 ? 1 : 0;
  levels.push({ w, h, occ: prev });
  for (let l = 1; l < MIP_LEVELS; l++) {
    const nw = Math.ceil(w / 2), nh = Math.ceil(h / 2), o = new Uint8Array(nw * nh);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (prev[y * w + x]) o[(y >> 1) * nw + (x >> 1)] = 1;
    levels.push({ w: nw, h: nh, occ: o }); w = nw; h = nh; prev = o;
  }
  return levels;
}

// Integrate the grid along a straight segment from P in unit direction d for length len.
// acc = { L: [r, g, b], T, out } is updated in place; out becomes true when the ray leaves the frame.
export function marchGrid(sc, mips, P, d, len, acc) {
  const { W, H, grid } = sc;
  if (len <= 0) return;
  if (P[0] < 0 || P[1] < 0 || P[0] > W || P[1] > H) { acc.out = true; return; }
  const sx = d[0] > 0 ? 1 : -1, sy = d[1] > 0 ? 1 : -1;
  const ivx = d[0] !== 0 ? 1 / d[0] : Infinity, ivy = d[1] !== 0 ? 1 / d[1] : Infinity;
  let ix = Math.min(W - 1, Math.floor(P[0])), iy = Math.min(H - 1, Math.floor(P[1])), t = 0;
  for (let guard = 0; guard < 1 << 20; guard++) {
    if (ix < 0 || iy < 0 || ix >= W || iy >= H) { acc.out = true; return; }
    if (t >= len || acc.T < 1e-6) return;
    if (!mips[0].occ[iy * W + ix]) {
      let l = 0;
      while (l + 1 < MIP_LEVELS && !mips[l + 1].occ[(iy >> (l + 1)) * mips[l + 1].w + (ix >> (l + 1))]) l++;
      const bx = (ix >> l) << l, by = (iy >> l) << l, size = 1 << l;
      const tx = d[0] !== 0 ? ((sx > 0 ? bx + size : bx) - P[0]) * ivx : Infinity;
      const ty = d[1] !== 0 ? ((sy > 0 ? by + size : by) - P[1]) * ivy : Infinity;
      if (Math.min(tx, ty) >= len) return;
      if (tx < ty) { t = Math.max(t, tx); ix = sx > 0 ? bx + size : bx - 1; iy = clampI(Math.floor(P[1] + d[1] * t), by, by + size - 1); }
      else { t = Math.max(t, ty); iy = sy > 0 ? by + size : by - 1; ix = clampI(Math.floor(P[0] + d[0] * t), bx, bx + size - 1); }
      continue;
    }
    const tx = d[0] !== 0 ? ((sx > 0 ? ix + 1 : ix) - P[0]) * ivx : Infinity;
    const ty = d[1] !== 0 ? ((sy > 0 ? iy + 1 : iy) - P[1]) * ivy : Infinity;
    const tn = Math.min(tx, ty, len), seg = tn - t;
    if (seg > 0) {
      const k = 4 * (iy * W + ix), a = Math.exp(-grid[k + 3] * seg), e = acc.T * (1 - a);
      acc.L[0] += e * grid[k]; acc.L[1] += e * grid[k + 1]; acc.L[2] += e * grid[k + 2]; acc.T *= a;
    }
    t = Math.max(t, tn);
    if (tn >= len) return;
    if (tx < ty) ix += sx; else iy += sy;
  }
}
function clampI(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

// Nearest circle hit beyond eps along (p, d): { t, i } or null. The circle the ray last met is
// handled by state, not by an epsilon (f32 cannot resolve a root at 0 on a large circle): inside it,
// only the far root counts; outside it (a reflection), it cannot be met again.
export function hitCircle(circles, p, d, last = -1, inside = false) {
  let best = Infinity, bi = -1;
  for (let i = 0; i < circles.length; i++) {
    const c = circles[i], ox = p[0] - c.cx, oy = p[1] - c.cy, b = ox * d[0] + oy * d[1];
    const disc = b * b - (ox * ox + oy * oy - c.r * c.r);
    let t;
    if (i === last) { if (!inside) continue; t = -b + Math.sqrt(Math.max(0, disc)); }
    else {
      if (disc < 0) continue;
      const q = Math.sqrt(disc), t1 = -b - q, t2 = -b + q; t = t1 > 1e-4 ? t1 : t2 > 1e-4 ? t2 : Infinity;
    }
    if (t < best) { best = t; bi = i; }
  }
  return bi < 0 ? null : { t: best, i: bi };
}
function insideEmitter(circles, p) {
  for (const c of circles) if (c.kind === "emit" && (p[0] - c.cx) ** 2 + (p[1] - c.cy) ** 2 < c.r * c.r) return c;
  return null;
}

// Bend d at a glass circle hit point p: returns the new direction. u in [0, 1) picks reflection when
// u < F in "fresnel" mode; mode "refract" always refracts unless the ray is totally reflected.
export function scatterGlass(c, p, d, mode, u) {
  let nx = (p[0] - c.cx) / c.r, ny = (p[1] - c.cy) / c.r, cosi = -(nx * d[0] + ny * d[1]), n1 = 1, n2 = c.n;
  if (cosi < 0) { nx = -nx; ny = -ny; cosi = -cosi; n1 = c.n; n2 = 1; }
  const eta = n1 / n2, k = 1 - eta * eta * (1 - cosi * cosi);
  const reflect = [d[0] + 2 * cosi * nx, d[1] + 2 * cosi * ny];
  if (k < 0) return reflect;
  const cost = Math.sqrt(k);
  if (mode === "fresnel") {
    const rs = (n1 * cosi - n2 * cost) / (n1 * cosi + n2 * cost), rp = (n2 * cosi - n1 * cost) / (n2 * cosi + n1 * cost);
    if (u < 0.5 * (rs * rs + rp * rp)) return reflect;
  }
  const f = eta * cosi - cost, r = [eta * d[0] + f * nx, eta * d[1] + f * ny], m = Math.hypot(r[0], r[1]);
  return [r[0] / m, r[1] / m];
}

// Radiance interval along a (possibly bent) path from o in direction d: what lies between path
// lengths t0 and t1. Returns { L, T, p, d, out, bent } where p and d are the path's end point and
// direction, and bent is true when a glass circle turned the ray.
// rng is null for the deterministic mode, else a function returning uniforms (fresnel mode).
export function traceInterval(sc, mips, o, d, t0, t1, rng = null) {
  const acc = { L: [0, 0, 0], T: 1, out: false };
  let p = [o[0], o[1]], dir = [d[0], d[1]], s = 0, started = false, bent = false, last = -1, inside = false;
  for (let ev = 0; ev < MAX_EVENTS; ev++) {
    const h = sc.circles.length ? hitCircle(sc.circles, p, dir, last, inside) : null, th = h ? h.t : Infinity;
    const end = Math.min(th, t1 - s), a0 = Math.max(0, t0 - s);
    if (!started && end >= a0) {
      started = true;
      const ps = [p[0] + dir[0] * a0, p[1] + dir[1] * a0], c = insideEmitter(sc.circles, ps);
      if (c) { for (let k = 0; k < 3; k++) acc.L[k] += acc.T * c.L[k]; acc.T = 0; return { ...acc, p: ps, d: dir, bent }; }
    }
    if (end > a0) marchGrid(sc, mips, [p[0] + dir[0] * a0, p[1] + dir[1] * a0], dir, end - a0, acc);
    if (acc.out || acc.T < 1e-6 || th >= t1 - s) {
      const step = Math.min(th, t1 - s);
      return { ...acc, p: [p[0] + dir[0] * step, p[1] + dir[1] * step], d: dir, bent };
    }
    p = [p[0] + dir[0] * th, p[1] + dir[1] * th]; s += th;
    const c = sc.circles[h.i];
    if (c.kind === "emit" && s >= t0) { for (let k = 0; k < 3; k++) acc.L[k] += acc.T * c.L[k]; acc.T = 0; return { ...acc, p, d: dir, bent }; }
    // A solid emitter met inside the skipped prefix lets the ray go on (the interval starts later).
    if (c.kind === "glass") { dir = scatterGlass(c, p, dir, rng ? "fresnel" : "refract", rng ? rng() : 0); bent = true; }
    last = h.i; inside = (p[0] - c.cx) * dir[0] + (p[1] - c.cy) * dir[1] < 0;
  }
  return { ...acc, p, d: dir, bent };
}

// A small counter-based generator (PCG-style hash), identical in WGSL: hash(seed) -> u32.
export function hash32(x) {
  x = (Math.imul(x >>> 0, 747796405) + 2891336453) >>> 0;
  const w = Math.imul(((x >>> ((x >>> 28) + 4)) ^ x) >>> 0, 277803737) >>> 0;
  return ((w >>> 22) ^ w) >>> 0;
}
export function makeRng(seed) { let s = seed >>> 0; return () => { s = hash32(s); return s / 4294967296; }; }
