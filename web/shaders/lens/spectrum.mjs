// Wavelength bins for the lens suite, and the glass the lens is made of.
//
// Light from a source is carried as RGB energy. Every lens term that depends on wavelength
// (diffraction size, refraction, coating reflectance, halation depth) is evaluated at BINS
// wavelengths and recombined with per-bin RGB weights. The weights are the linear Rec. 709
// response of each 40 nm bin of the CIE 1931 observer, negative lobes clipped, then scaled so
// the weights of each channel sum to 1 over the bins: a term that does not depend on
// wavelength gives back exactly the source's RGB, and a term that does spreads it.
import { cmf, XYZ_TO, apply3 } from "../spectral.mjs";

export const BINS = 8;
export const BIN_NM = Array.from({ length: BINS }, (_, l) => 420 + 40 * l); // 420 .. 700 nm
export const LAMBDA_D = 587.6; // the helium d line, where a glass's nominal index is quoted

// RGB weight per bin: weights[l] = [r, g, b]; each channel sums to 1 over l.
export const BIN_RGB = (() => {
  const raw = BIN_NM.map((c) => {
    const xyz = [0, 0, 0];
    for (let wl = c - 20; wl < c + 20; wl++) { const v = cmf(wl + 0.5); xyz[0] += v[0]; xyz[1] += v[1]; xyz[2] += v[2]; }
    return apply3(XYZ_TO.rec709, xyz).map((v) => Math.max(0, v));
  });
  const sum = [0, 1, 2].map((k) => raw.reduce((s, w) => s + w[k], 0));
  return raw.map((w) => w.map((v, k) => v / sum[k]));
})();

// Glass by the Sellmeier formula n^2 - 1 = sum B_i l^2 / (l^2 - C_i) (l in micrometres), with
// SCHOTT's catalogue coefficients (optical glass overview, 2025): the crown is N-BK7 (nd 1.5168,
// vd 64.17) and the flint N-SF2 (nd 1.6477, vd 33.82), the catalogue glasses nearest the indices
// the suite's prescriptions were drawn for.
export const GLASS = {
  crown: { name: "N-BK7", B: [1.03961212, 0.231792344, 1.01046945], C: [0.00600069867, 0.0200179144, 103.560653] },
  flint: { name: "N-SF2", B: [1.47343127, 0.163681849, 1.36920899], C: [0.0109019098, 0.0585683687, 127.404933] },
  air: { name: "air", B: [0, 0, 0], C: [0, 0, 0] },
};
export function glassIndex(g, nm) {
  const l2 = (nm / 1000) * (nm / 1000);
  let s = 1;
  for (let i = 0; i < 3; i++) s += (g.B[i] * l2) / (l2 - g.C[i]);
  return Math.sqrt(s);
}
// Abbe number at the helium d line and the hydrogen F and C lines.
export function abbe(g) { return (glassIndex(g, LAMBDA_D) - 1) / (glassIndex(g, 486.1327) - glassIndex(g, 656.2725)); }

// Per-channel sum of w(l) * f(l): how a wavelength-dependent factor f weights each channel.
export function channelMean(f) { const o = [0, 0, 0]; for (let l = 0; l < BINS; l++) for (let k = 0; k < 3; k++) o[k] += BIN_RGB[l][k] * f(l); return o; }
