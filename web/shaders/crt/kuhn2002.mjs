// The measured P22 decay of Kuhn 2002 (Optical time-domain eavesdropping risks of CRT displays,
// IEEE S&P, eqs. 8 to 10): the impulse response of one P22 monitor read with a photomultiplier.
// Red is a sum of exponentials, a exp(-2 pi f t). Green and blue are a power law A (t + t0)^-n plus
// exponentials. The power law is cut at 10 ms, the end of the measured span: with n = 1.1 an uncut
// tail would hold over a third of the green energy beyond 100 ms, which the measurement does not show.
//
// The persistence pass carries two exponentials per component, so phosphors.mjs uses a two-exponential
// least-squares fit to cumulativeF below (1 us to 10 ms, log-spaced). crt.test.mjs holds the fit to it.
export const KUHN2002 = {
  red: { exp: [[4, 360], [1.75, 1600], [2, 8000], [2.25, 25000], [15, 7e5], [29, 7e6]] },
  green: { power: [0.00021, 5.5e-6, 1.10], exp: [[37, 1.5e5], [100, 7e5], [90, 5e6]] },
  blue: { power: [0.00019, 5e-6, 1.11], exp: [[75, 1e5], [1000, 1.1e6], [1100, 4e6]] },
};
export const KUHN_CUT_S = 1e-2;

// Light emitted by time t after the excitation, unnormalised.
function energy(m, t) {
  const u = Math.min(Math.max(t, 0), KUHN_CUT_S);
  let e = 0;
  for (const [a, f] of m.exp) { const k = 2 * Math.PI * f; e += (a / k) * (1 - Math.exp(-k * u)); }
  if (m.power) { const [A, t0, n] = m.power; e += (A / (n - 1)) * (Math.pow(t0, 1 - n) - Math.pow(u + t0, 1 - n)); }
  return e;
}
// Fraction of the light (inside the measured span) emitted by time t.
export function cumulativeF(colour, t) {
  const m = KUHN2002[colour];
  return t > 0 ? energy(m, t) / energy(m, KUHN_CUT_S) : 0;
}
// The power-law term alone falls to `level` of its value at t = 0 after t0 (level^(-1/n) - 1).
export function powerTimeTo(colour, level) {
  const [, t0, n] = KUHN2002[colour].power;
  return t0 * (Math.pow(level, -1 / n) - 1);
}
