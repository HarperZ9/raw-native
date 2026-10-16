// The environment state's terms and the default weather library (ADR 0017, first slice E0).
// Pure: runs in Node.
//
// Every term has a unit and a valid range; a term without both does not enter the state.
// A weather state authors any term as one value per time-of-day slot (8 slots by default, 6
// accepted, fewer refused: web/look/keys.mjs). A term a weather state leaves out inherits the
// "clear" curve. The default library is authored on the 8 slots below; a 6-slot environment
// samples each 8-slot curve at its own slot hours, so the two agree at those hours.
//
//   8 slots: midnight 0, predawn 4, dawn 6, morning 9, noon 12.5, golden 17, dusk 19.5, night 22

export const TERMS = {
  // Light composition: the author sets the key level and the direct-to-ambient balance; the
  // engine derives direct = key * balance and ambient = key * (1 - balance).
  keyLevel:       { unit: "relative scene luminance (1 = overcast noon)", min: 0, max: 4 },
  balance:        { unit: "direct / (direct + ambient)", min: 0, max: 1 },
  directColour:   { unit: "linear RGB, unit peak", dim: 3, min: 0, max: 1 },
  ambientSky:     { unit: "linear RGB, unit peak", dim: 3, min: 0, max: 1 },
  ambientGround:  { unit: "linear RGB, unit peak", dim: 3, min: 0, max: 1 },
  // Inputs only when the environment is made with composition "explicit": the author gives
  // direct and ambient, and the engine reports the balance instead of deriving them.
  direct:         { unit: "relative scene luminance", min: 0, max: 4 },
  ambient:        { unit: "relative scene luminance", min: 0, max: 4 },
  shadowStrength: { unit: "0 none to 1 full", min: 0, max: 1 },
  colourTemp:     { unit: "kelvin", min: 1500, max: 12000 },
  // Atmosphere.
  fogDensity:     { unit: "per metre", min: 0, max: 0.5 },
  fogFalloff:     { unit: "per metre of height", min: 0, max: 2 },
  fogColour:      { unit: "linear RGB, unit peak", dim: 3, min: 0, max: 1 },
  anisotropy:     { unit: "scattering g", min: -0.9, max: 0.9 },
  cloudCover:     { unit: "fraction of sky", min: 0, max: 1 },
  haze:           { unit: "0 clear to 1 thick", min: 0, max: 1 },
  // Wind, moisture, temperature.
  windSpeed:      { unit: "metres per second", min: 0, max: 40 },
  gustiness:      { unit: "0 steady to 1 gusty", min: 0, max: 1 },
  rainRate:       { unit: "millimetres per hour", min: 0, max: 100 },
  snowRate:       { unit: "millimetres per hour, water equivalent", min: 0, max: 50 },
  humidity:       { unit: "relative, 0 to 1", min: 0, max: 1 },
  temperature:    { unit: "degrees Celsius", min: -40, max: 50 },
};

const rep = (v) => Array(8).fill(v);
const night = [0.10, 0.12, 0.18];            // a cold moonlit blue, kept dim

// The clear day is the base every other state inherits from.
const clear = {
  keyLevel:       [0.05, 0.06, 0.35, 1.6, 2.2, 1.5, 0.45, 0.08],
  balance:        [0.35, 0.30, 0.55, 0.72, 0.78, 0.74, 0.50, 0.38],
  directColour:   [[0.55, 0.65, 1.0], [0.6, 0.65, 0.95], [1.0, 0.62, 0.38], [1.0, 0.9, 0.78], [1.0, 0.97, 0.92], [1.0, 0.72, 0.42], [0.95, 0.45, 0.30], [0.55, 0.62, 0.95]],
  ambientSky:     [[0.10, 0.13, 0.25], [0.14, 0.15, 0.28], [0.55, 0.45, 0.55], [0.55, 0.68, 0.95], [0.50, 0.66, 1.0], [0.60, 0.55, 0.70], [0.35, 0.28, 0.45], [0.12, 0.14, 0.28]],
  ambientGround:  [[0.03, 0.03, 0.04], [0.04, 0.04, 0.05], [0.20, 0.15, 0.12], [0.30, 0.27, 0.22], [0.32, 0.30, 0.26], [0.30, 0.22, 0.15], [0.14, 0.10, 0.09], [0.04, 0.04, 0.05]],
  shadowStrength: [0.5, 0.5, 0.75, 0.9, 0.95, 0.9, 0.7, 0.5],
  colourTemp:     [9000, 8500, 3200, 5200, 5800, 3600, 2800, 8800],
  fogDensity:     [0.012, 0.02, 0.025, 0.006, 0.003, 0.005, 0.01, 0.012],
  fogFalloff:     rep(0.4),
  fogColour:      [night, night, [0.6, 0.5, 0.5], [0.7, 0.75, 0.8], [0.75, 0.8, 0.9], [0.8, 0.65, 0.5], [0.45, 0.32, 0.35], night],
  anisotropy:     rep(0.6),
  cloudCover:     rep(0.1),
  haze:           [0.1, 0.15, 0.3, 0.1, 0.05, 0.15, 0.25, 0.1],
  windSpeed:      [2, 1.5, 1.5, 3, 4, 3.5, 2.5, 2],
  gustiness:      rep(0.2),
  rainRate:       rep(0),
  snowRate:       rep(0),
  humidity:       [0.75, 0.82, 0.85, 0.65, 0.5, 0.55, 0.65, 0.72],
  temperature:    [8, 6, 7, 12, 16, 15, 12, 9],
};
const scale = (vals, k) => vals.map((v) => (Array.isArray(v) ? v.map((x) => x * k) : v * k));
const grey = (vals, g = 0.6) => vals.map((c) => { const m = (c[0] + c[1] + c[2]) / 3; return c.map((x) => x + (m - x) * g); });

export const DEFAULT_LIBRARY = {
  clear,
  overcast: {
    keyLevel: scale(clear.keyLevel, 0.55), balance: scale(clear.balance, 0.45), shadowStrength: rep(0.25), cloudCover: rep(0.9),
    directColour: grey(clear.directColour), ambientSky: grey(clear.ambientSky, 0.7), haze: rep(0.35), humidity: rep(0.8),
    windSpeed: scale(clear.windSpeed, 1.6), gustiness: rep(0.35), temperature: scale(clear.temperature, 0.9),
  },
  rain: {
    keyLevel: scale(clear.keyLevel, 0.35), balance: rep(0.15), shadowStrength: rep(0.1), cloudCover: rep(1), haze: rep(0.55),
    directColour: grey(clear.directColour, 0.85), ambientSky: grey(clear.ambientSky, 0.8), fogDensity: rep(0.03),
    windSpeed: scale(clear.windSpeed, 2.2), gustiness: rep(0.5), rainRate: rep(8), humidity: rep(0.97), temperature: scale(clear.temperature, 0.8),
  },
  storm: {
    keyLevel: scale(clear.keyLevel, 0.2), balance: rep(0.1), shadowStrength: rep(0.05), cloudCover: rep(1), haze: rep(0.7),
    directColour: grey(clear.directColour, 0.9), ambientSky: grey(scale(clear.ambientSky, 0.8), 0.85), fogDensity: rep(0.045),
    windSpeed: scale(clear.windSpeed, 4.5), gustiness: rep(0.85), rainRate: rep(30), humidity: rep(1), temperature: scale(clear.temperature, 0.7),
  },
  fog: {
    keyLevel: scale(clear.keyLevel, 0.6), balance: rep(0.12), shadowStrength: rep(0.08), haze: rep(0.9), cloudCover: rep(0.6),
    fogDensity: rep(0.12), fogFalloff: rep(0.15), anisotropy: rep(0.3), windSpeed: rep(0.6), gustiness: rep(0.05), humidity: rep(0.99),
  },
};
