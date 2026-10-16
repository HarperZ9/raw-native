// Spec objects to the uniform layouts in wgsl.mjs. Pure: the GPU layers and the parity test
// both pack through here, so the kernel sees exactly what a frame sees.

import { orbSpec } from "./ref.mjs";

const v4 = (c, w = 0) => [c[0], c[1], c[2], w];

// The default orb and atmosphere looks. Linear light. The spectrum lives here, in the art layer
// only: UI type and data marks use the ink and verdict colours (palette.mjs).
export { ORB } from "./ref.mjs";
export const NIGHT = Object.freeze({
  stops: [[0, [0.012, 0.010, 0.040]], [0.55, [0.060, 0.025, 0.150]], [0.85, [0.240, 0.045, 0.200]], [1, [0.080, 0.012, 0.060]]],
  band: 0.82, bandW: 0.07, bandC: [0.30, 0.05, 0.16], aspect: 16 / 9, drift: 0.012, grain: 0.14, seed: 7,
});
export const PAPER = Object.freeze({
  stops: [[0, [0.88, 0.85, 0.80]], [0.6, [0.86, 0.83, 0.77]], [0.9, [0.85, 0.80, 0.74]], [1, [0.83, 0.78, 0.72]]],
  band: 0.95, bandW: 0.12, bandC: [0.05, 0.02, 0.0], aspect: 16 / 9, drift: 0.006, grain: 0.025, seed: 11,
});

export function orbFloats(spec = {}) {
  const s = orbSpec(spec);
  return [
    ...v4(s.core), ...v4(s.mid), ...v4(s.rim),
    s.soft, s.coreGain, s.coreR, s.haloGain,
    s.haloR, s.ring, s.ringW, s.iris.blades,
    s.iris.open, s.iris.rot, s.gain, 0,
  ];
}

// Four ramp stops (pads a shorter list by repeating its last stop).
export function rampFloats(stops) {
  const st = stops.slice(0, 4);
  while (st.length < 4) st.push([st[st.length - 1][0] + 1e-3 * st.length, st[st.length - 1][1]]);
  return { xs: st.map((s) => s[0]), cs: st.flatMap((s) => v4(s[1])) };
}

export function atmosFloats(spec = {}) {
  const s = { ...NIGHT, ...spec };
  const r = rampFloats(s.stops);
  return {
    floats: [...r.xs, ...r.cs, s.band, s.bandW, 0, 0, s.aspect, s.drift, s.grain, s.seed],
    bandC: v4(s.bandC),
  };
}
