// Value noise with an analytic gradient, plus the Lipschitz constants the displacement modifiers need.
// Own derivation: trilinear interpolation of hashed lattice values with the quintic fade 6t^5-15t^4+10t^3.
// The fade's largest slope is 15/8; two lattice values differ by at most 2, so one axis derivative is at most
// 3.75 f and the gradient norm at most sqrt(3) * 3.75 f = 6.5 f for lattice frequency f. The test samples it.
export const NOISE_LIP = Math.sqrt(3) * 3.75;
function hash(ix, iy, iz, seed) {
  let h = (Math.imul(ix, 374761393) ^ Math.imul(iy, 668265263) ^ Math.imul(iz, 1274126177) ^ Math.imul(seed, 2246822519)) | 0;
  h = Math.imul(h ^ (h >>> 15), 2246822519); h = Math.imul(h ^ (h >>> 13), 3266489917); h ^= h >>> 16;
  return (h >>> 0) / 2147483648 - 1;   // [-1, 1)
}
const fade = (t) => t * t * t * (t * (t * 6 - 15) + 10), dfade = (t) => 30 * t * t * (t - 1) * (t - 1);
// returns the value; if g is given, writes the gradient (per unit of input) into g[0..2]
export function noise3(x, y, z, seed = 0, g = null) {
  const ix = Math.floor(x), iy = Math.floor(y), iz = Math.floor(z), fx = x - ix, fy = y - iy, fz = z - iz, ux = fade(fx), uy = fade(fy), uz = fade(fz);
  const a = hash(ix, iy, iz, seed), b = hash(ix + 1, iy, iz, seed), c = hash(ix, iy + 1, iz, seed), d = hash(ix + 1, iy + 1, iz, seed);
  const e = hash(ix, iy, iz + 1, seed), f = hash(ix + 1, iy, iz + 1, seed), gg = hash(ix, iy + 1, iz + 1, seed), h = hash(ix + 1, iy + 1, iz + 1, seed);
  const k1 = b - a, k2 = c - a, k3 = a - b - c + d, k4 = e - a, k5 = a - c - e + gg, k6 = a - b - e + f, k7 = -a + b + c - d + e - f - gg + h;
  if (g) {
    g[0] = dfade(fx) * (k1 + k3 * uy + k6 * uz + k7 * uy * uz); g[1] = dfade(fy) * (k2 + k3 * ux + k5 * uz + k7 * ux * uz); g[2] = dfade(fz) * (k4 + k5 * uy + k6 * ux + k7 * ux * uy);
  }
  return a + k1 * ux + k2 * uy + k4 * uz + k3 * ux * uy + k5 * uy * uz + k6 * uz * ux + k7 * ux * uy * uz;
}

// Displacement sources: { h(x, y, z, g) -> value in [-1, 1], gradient into g per metre; lip } in metres.
export function makeSource(src) {
  const g3 = [0, 0, 0], seed = src.seed ?? 0;
  if (src.kind === "noise") {
    const f = src.freq;
    return { lip: NOISE_LIP * f, h: (x, y, z, g) => { const v = noise3(x * f, y * f, z * f, seed, g ? g3 : null); if (g) { g[0] = g3[0] * f; g[1] = g3[1] * f; g[2] = g3[2] * f; } return v; } };
  }
  if (src.kind === "fbm") {   // relief: octaves of value noise, amplitude gain^i, frequency lacunarity^i, normalised to [-1, 1]
    const oct = src.octaves ?? 4, gain = src.gain ?? 0.5, lac = src.lacunarity ?? 2, f0 = src.freq;
    let norm = 0, lip = 0; for (let i = 0, a = 1, f = f0; i < oct; i++, a *= gain, f *= lac) { norm += a; lip += a * NOISE_LIP * f; }
    return { lip: lip / norm, h: (x, y, z, g) => {
      let v = 0; if (g) g[0] = g[1] = g[2] = 0;
      for (let i = 0, a = 1, f = f0; i < oct; i++, a *= gain, f *= lac) { v += a * noise3(x * f, y * f, z * f, seed + i * 101, g ? g3 : null); if (g) { g[0] += a * f * g3[0]; g[1] += a * f * g3[1]; g[2] += a * f * g3[2]; } }
      if (g) { g[0] /= norm; g[1] /= norm; g[2] /= norm; }
      return v / norm;
    } };
  }
  if (src.kind === "ridge") {   // wrinkles: cos(2 pi f (p . dir) + warp * noise(p * warpFreq)), dir is a unit vector
    const [dx, dy, dz] = src.dir, w = src.warp ?? 0, wf = src.warpFreq ?? 20, T = 2 * Math.PI * src.freq;
    return { lip: T + w * NOISE_LIP * wf, h: (x, y, z, g) => {
      const n = w ? noise3(x * wf, y * wf, z * wf, seed, g ? g3 : null) : 0, ph = T * (x * dx + y * dy + z * dz) + w * n;
      if (g) { const s = -Math.sin(ph), q = w ? w * wf : 0; g[0] = s * (T * dx + q * g3[0]); g[1] = s * (T * dy + q * g3[1]); g[2] = s * (T * dz + q * g3[2]); }
      return Math.cos(ph);
    } };
  }
  if (src.kind === "map") {   // heightmap {w, hgt, data in [-1, 1]}, planar along local z: u = x * scale + 0.5, bilinear
    const { w, hgt, data, scale: s } = src; let L = 0;
    for (let j = 0; j < hgt; j++) for (let i = 0; i < w; i++) { const v = data[j * w + i]; if (i + 1 < w) L = Math.max(L, Math.abs(data[j * w + i + 1] - v)); if (j + 1 < hgt) L = Math.max(L, Math.abs(data[(j + 1) * w + i] - v)); }
    const at = (i, j) => data[Math.min(hgt - 1, Math.max(0, j)) * w + Math.min(w - 1, Math.max(0, i))];
    return { lip: L * s * Math.max(w, hgt) * Math.SQRT2, h: (x, y, z, g) => {
      const u = (x * s + 0.5) * w - 0.5, v = (y * s + 0.5) * hgt - 0.5, i = Math.floor(u), j = Math.floor(v), fu = u - i, fv = v - j;
      const a = at(i, j), b = at(i + 1, j), c = at(i, j + 1), d = at(i + 1, j + 1);
      if (g) { g[0] = ((b - a) * (1 - fv) + (d - c) * fv) * s * w; g[1] = ((c - a) * (1 - fu) + (d - b) * fu) * s * hgt; g[2] = 0; }
      return a * (1 - fu) * (1 - fv) + b * fu * (1 - fv) + c * (1 - fu) * fv + d * fu * fv;
    } };
  }
  throw new Error(`sdf: unknown displacement source "${src.kind}"`);
}
