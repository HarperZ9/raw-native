// Modifier stack and smooth blends. A modifier wraps a leaf f(x, y, z, g) and returns a leaf; the first
// modifier in a stroke's `mods` list wraps the shape, the next wraps that, and so on. Each one keeps the contract
// of DESIGN.md section 10: the returned field never over-states the distance (|d| <= |d_true|) and g is the
// gradient of the returned field. Smooth-min family: Inigo Quilez, iquilezles.org/articles/smin (polynomial forms).
import { makeSource } from "./noise.mjs";

// polynomial smooth min. quad: C1, at most k/4 below min. cubic: C2, at most k/6 below min.
export function sminV(a, b, k, cubic) {
  if (k <= 0) return Math.min(a, b);
  const h = Math.max(k - Math.abs(a - b), 0) / k;
  return Math.min(a, b) - (cubic ? h * h * h * k / 6 : h * h * k / 4);
}
// weight of a's gradient in the blended gradient (b gets 1 - wa); exact derivative of sminV
export function sminW(a, b, k, cubic) {
  if (k <= 0) return a < b ? 1 : 0;
  const h = Math.max(k - Math.abs(a - b), 0) / k, t = cubic ? h * h / 2 : h / 2;
  return a < b ? 1 - t : t;
}

// largest singular value of a rotation-by-(k * coordinate) warp for points within radius R of the axis
export const warpScale = (k, R) => { const a = Math.abs(k) * R; return (a + Math.sqrt(a * a + 4)) / 2; };

const need = (c, msg) => { if (!c) throw new Error(`sdf: ${msg}`); };
export const MODS = {
  round: (f, m) => (x, y, z, g) => f(x, y, z, g) - m.r,   // IQ opRound
  shell: (f, m) => (x, y, z, g) => { const d = f(x, y, z, g); if (g && d < 0) { g[0] = -g[0]; g[1] = -g[1]; g[2] = -g[2]; } return Math.abs(d) - m.t / 2; },   // IQ opOnion
  mirror: (f, m) => {
    const ax = m.axis ?? 0, off = m.offset ?? 0;
    return (x, y, z, g) => {
      const p = [x, y, z], flip = p[ax] < off; if (flip) p[ax] = 2 * off - p[ax];
      const d = f(p[0], p[1], p[2], g); if (g && flip) g[ax] = -g[ax]; return d;
    };
  },
  repeat: (f, m) => {   // IQ opLimitedRepetition: exact while the shape fits inside half a period
    const T = m.period, N = m.n ?? [1, 1, 1];
    return (x, y, z, g) => {
      const p = [x, y, z]; for (let i = 0; i < 3; i++) if (T[i] > 0) p[i] -= T[i] * Math.max(-N[i], Math.min(N[i], Math.round(p[i] / T[i])));
      return f(p[0], p[1], p[2], g);
    };
  },
  twist: (f, m) => {   // rotate (x, z) about the y axis through pivot c by k * (y - cy); bounded by 1 / warpScale(k, R)
    need(m.radius > 0, "twist needs radius"); const [cx, cy, cz] = m.c ?? [0, 0, 0], k = m.k, S = warpScale(k, m.radius), t = [0, 0, 0];
    return (x, y, z, g) => {
      const th = k * (y - cy), c = Math.cos(th), s = Math.sin(th), X = x - cx, Z = z - cz, qx = X * c - Z * s, qz = X * s + Z * c;
      const d = f(qx + cx, y, qz + cz, g ? t : null);
      if (g) { g[0] = (c * t[0] + s * t[2]) / S; g[1] = (t[1] + k * (-qz * t[0] + qx * t[2])) / S; g[2] = (-s * t[0] + c * t[2]) / S; }
      return d / S;
    };
  },
  bend: (f, m) => {   // rotate (x, y) about the z axis through pivot c by k * (x - cx); bounded by 1 / warpScale(k, E)
    need(m.extent > 0, "bend needs extent"); const [cx, cy, cz] = m.c ?? [0, 0, 0], k = m.k, S = warpScale(k, m.extent), t = [0, 0, 0];
    return (x, y, z, g) => {
      const th = k * (x - cx), c = Math.cos(th), s = Math.sin(th), X = x - cx, Y = y - cy, qx = X * c - Y * s, qy = X * s + Y * c;
      const d = f(qx + cx, qy + cy, z, g ? t : null);
      if (g) { g[0] = (c * t[0] + s * t[1] + k * (-qy * t[0] + qx * t[1])) / S; g[1] = (-s * t[0] + c * t[1]) / S; g[2] = t[2] / S; }
      return d / S;
    };
  },
  displace: (f, m) => {   // f + A * mask * h, divided by 1 + A (L_h + L_mask); h in [-1, 1] with Lipschitz L_h
    const src = m.src.h ? m.src : makeSource(m.src), A = m.amp, mk = m.mask, Lm = mk ? 1.5 / (mk.r1 - mk.r0) : 0, S = 1 + A * (src.lip + Lm), hg = [0, 0, 0];
    return (x, y, z, g) => {
      const d = f(x, y, z, g), h = src.h(x, y, z, g ? hg : null);
      let w = 1, wx = 0, wy = 0, wz = 0;
      if (mk) {
        const dx = x - mk.c[0], dy = y - mk.c[1], dz = z - mk.c[2], r = Math.hypot(dx, dy, dz), t = Math.min(1, Math.max(0, (r - mk.r0) / (mk.r1 - mk.r0)));
        w = 1 - t * t * (3 - 2 * t);
        if (g && r > 1e-12 && t > 0 && t < 1) { const e = -6 * t * (1 - t) / (mk.r1 - mk.r0) / r; wx = e * dx; wy = e * dy; wz = e * dz; }
      }
      if (g) { g[0] = (g[0] + A * (w * hg[0] + h * wx)) / S; g[1] = (g[1] + A * (w * hg[1] + h * wy)) / S; g[2] = (g[2] + A * (w * hg[2] + h * wz)) / S; }
      return (d + A * w * h) / S;
    };
  },
};
MODS.relief = (f, m) => MODS.displace(f, { ...m, src: { kind: "fbm", freq: m.freq, octaves: m.octaves, gain: m.gain, lacunarity: m.lacunarity, seed: m.seed } });

export function applyMods(f, mods) {
  for (const m of mods || []) { const fn = MODS[m.kind]; need(fn, `unknown modifier "${m.kind}"`); f = fn(f, m); }
  return f;
}

// the box a stroke can reach after its modifiers (conservative), for culling and the sparse grid
export function modBox(b, mods) {
  let lo = [...b.lo], hi = [...b.hi];
  const grow = (e) => { lo = lo.map((v) => v - e); hi = hi.map((v) => v + e); };
  for (const m of mods || []) {
    if (m.kind === "round") grow(Math.max(0, m.r));
    else if (m.kind === "shell") grow(m.t / 2);
    else if (m.kind === "displace" || m.kind === "relief") grow(m.amp);
    else if (m.kind === "mirror") { const a = m.axis ?? 0, o = m.offset ?? 0, l = Math.min(lo[a], 2 * o - hi[a]), h = Math.max(hi[a], 2 * o - lo[a]); lo[a] = l; hi[a] = h; }
    else if (m.kind === "repeat") for (let i = 0; i < 3; i++) if (m.period[i] > 0) { const e = m.period[i] * (m.n?.[i] ?? 1); lo[i] -= e; hi[i] += e; }
    else if (m.kind === "twist" || m.kind === "bend") {
      const [i, j] = m.kind === "twist" ? [0, 2] : [0, 1], c = m.c ?? [0, 0, 0];
      let R = 0; for (const u of [lo[i], hi[i]]) for (const v of [lo[j], hi[j]]) R = Math.max(R, Math.hypot(u - c[i], v - c[j]));
      lo[i] = Math.min(lo[i], c[i] - R); hi[i] = Math.max(hi[i], c[i] + R); lo[j] = Math.min(lo[j], c[j] - R); hi[j] = Math.max(hi[j], c[j] + R);
    }
  }
  return { lo, hi };
}
