// Readers of the environment state (ADR 0017): the contracts by which lighting, materials,
// animation and gameplay take their inputs from one snapshot. Pure: runs in Node. Every
// function here is checked numerically in env.test.mjs against the specification in its comment.

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const smoothstep = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };

// ---- (a) lighting and the look -----------------------------------------------------------
// The light a zone receives: direct and ambient intensity with their colours, the balance,
// and the exposure that brings the key to a reference level (so a night is dark but readable,
// not black: exposure is clamped to +-ev range around the reference).
export function lightingFor(snap, zoneName = "outside", { reference = 1, evRange = 3 } = {}) {
  const z = snap.zones[zoneName], L = snap.light, k = z ? z.lightScale : 1, open = z ? 1 - z.shelter : 1;
  const direct = L.direct.intensity * k * (0.25 + 0.75 * open);       // a roof blocks most of the key; windows leave some
  const ambient = L.ambient.intensity * k;
  const key = direct + ambient + L.flash * 0.8 * open;
  return {
    direct: { intensity: direct, colour: L.direct.colour, dir: L.direct.dir },
    ambient: { intensity: ambient, sky: L.ambient.sky, ground: L.ambient.ground },
    key, balance: key > 0 ? direct / key : 0, shadowStrength: L.shadowStrength * open,
    exposureEV: clamp(Math.log2(reference / Math.max(1e-4, key)), -evRange, evRange), flash: L.flash * open,
  };
}

// A Motion scene's look from the snapshot: the scene brightness the template multiplies its
// base art by (the key after exposure, so the night stays dim), the ambient tint, fog as a
// veil, and post settings (vignette grows in the dark, bloom with wet light). Every number
// is a function of the snapshot, so a weather change moves the whole look together.
export function motionLook(snap, zoneName = "outside") {
  const l = lightingFor(snap, zoneName), A = snap.atmosphere, z = snap.zones[zoneName];
  const exposed = l.key * Math.pow(2, l.exposureEV * 0.6);           // exposure lifts dark scenes part of the way only
  const brightness = clamp(exposed, 0.04, 1.4);
  const amb = l.ambient.sky.map((c, i) => c * 0.7 + l.ambient.ground[i] * 0.3);
  const tint = amb.map((c, i) => clamp(c * (1 - l.balance) + l.direct.colour[i] * l.balance, 0, 1));
  return {
    brightness, tint,
    fog: { colour: A.fogColour, amount: clamp(1 - Math.exp(-A.fogDensity * 30), 0, 0.85) },
    flash: l.flash,
    post: { vignette: clamp(0.3 + 0.4 * (1 - brightness), 0.3, 0.75), bloom: clamp(0.1 + 0.3 * (z ? z.wetness : 0), 0.1, 0.45) },
  };
}

// ---- (b) a material response: wetness ----------------------------------------------------
// A water film on a surface. w = smoothstep(0.05, 0.6, wetness) is the film's coverage.
//   albedo    * (1 - 0.45 * porosity * w)          porous surfaces darken most (internal reflection in the wet layer)
//   roughness -> roughness + (0.06 - roughness) * 0.85 * w, never rougher than dry
//   peakGain  = (roughness_dry / roughness_wet)^4  the rise of a GGX lobe's peak (alpha = roughness^2)
//   sheen     = w                                   how much a 2D scene shows mirror-like reflections
export function wetMaterial({ albedo, roughness, porosity = 0.6 }, wetness) {
  const w = smoothstep(0.05, 0.6, clamp(wetness, 0, 1)), f = 1 - 0.45 * clamp(porosity, 0, 1) * w;
  const rough = roughness > 0.06 ? roughness + (0.06 - roughness) * 0.85 * w : roughness;
  return { albedo: albedo.map((c) => c * f), roughness: rough, peakGain: (roughness / rough) ** 4, sheen: w, darken: f };
}

// ---- (c) an animation response: wind sway -----------------------------------------------
// A hanging or rooted element as a damped spring driven by the wind across it:
//   angle'' = response * crossWind - stiffness * angle - damping * angle'
// crossWind is the wind component along the element's sway axis (unit [x, z]), so a still
// day settles to rest and a steady wind settles to angle = response * crossWind / stiffness.
// Stepped on the fixed step with semi-implicit Euler, which is stable for the default constants
// at any step up to 1/20 s.
export const sway = ({ stiffness = 6, damping = 2.2, response = 0.05, axis = [1, 0], maxAngle = 1.2 } = {}) => ({ angle: 0, vel: 0, stiffness, damping, response, axis, maxAngle });
export function stepSway(s, wind, dt, flutter = 0) {
  const cross = wind[0] * s.axis[0] + wind[1] * s.axis[1];
  const acc = s.response * cross * (1 + flutter) - s.stiffness * s.angle - s.damping * s.vel;
  const vel = s.vel + acc * dt, angle = clamp(s.angle + vel * dt, -s.maxAngle, s.maxAngle);
  return { ...s, vel: angle === s.angle + vel * dt ? vel : 0, angle };
}

// ---- gameplay rules -----------------------------------------------------------------------
// A carried flame. Rain reaching the zone soaks it at soakRate per (mm/h * s); shelter and
// warmth dry it at dryRate per second. At soak 1 it goes out (event "torch-out"); it relights
// only when asked, in a zone with shelter of at least 0.8, after the soak has fallen below 0.3.
// gutter (0..1) is how unsteady the flame looks, for the renderer.
export const torch = () => ({ lit: true, soak: 0 });
export function stepTorch(t, snap, zoneName, dt, { relight = false, soakRate = 0.006, dryRate = 0.05 } = {}) {
  const z = snap.zones[zoneName], events = [];
  let soak = clamp(t.soak + (z.rain * soakRate - (z.rain < 0.1 ? dryRate : 0)) * dt, 0, 1.2), lit = t.lit;
  if (lit && soak >= 1) { lit = false; events.push({ type: "torch-out", zone: zoneName }); }
  if (!lit && relight && z.shelter >= 0.8 && soak < 0.3) { lit = true; events.push({ type: "torch-lit", zone: zoneName }); }
  return { state: { lit, soak }, events, gutter: lit ? clamp(soak, 0, 1) : 0, light: lit ? 1 - 0.5 * clamp(soak, 0, 1) : 0 };
}
// How far a character can see (metres): fog thins sight by exp(-density * 25), darkness by the
// zone's light against a half-visibility level, and a carried light keeps a floor of its radius.
export function sightRange(snap, zoneName, { clearRange = 40, carried = 0 } = {}) {
  const z = snap.zones[zoneName], fog = Math.exp(-snap.atmosphere.fogDensity * 25), dark = z.light / (z.light + 0.15);
  return Math.max(carried, clearRange * fog * dark);
}
