// Sun and moon directions and the seeded noise the environment state uses. Pure: runs in Node.
//
// Directions are unit vectors with x east, y up and z south. The sun follows the standard
// solar-position approximation: declination -23.44 deg * cos(2 pi (day + 10) / 365), hour
// angle 15 deg per hour from solar noon, no equation of time. The moon is a stand-in for the
// first slice: the sun's path half a day later with the declination reversed (no phases, no
// orbit), so night has a direction to light from. Both are labelled as such in ADR 0017's terms.
const RAD = Math.PI / 180;

function solar(hour, day, latitude, flip = false) {
  const decl = (flip ? 1 : -1) * 23.44 * RAD * Math.cos((2 * Math.PI * (day + 10)) / 365);
  const H = (hour - 12) * 15 * RAD, phi = latitude * RAD;
  const x = -Math.cos(decl) * Math.sin(H);
  const y = Math.sin(phi) * Math.sin(decl) + Math.cos(phi) * Math.cos(decl) * Math.cos(H);
  const north = Math.cos(phi) * Math.sin(decl) - Math.sin(phi) * Math.cos(decl) * Math.cos(H);
  const n = Math.hypot(x, y, north);
  return [x / n, y / n, -north / n];
}
export const sunDirection = (hour, day = 172, latitude = 47.6) => solar(hour, day, latitude);
export const moonDirection = (hour, day = 172, latitude = 47.6) => solar(hour + 12, day, latitude, true);

// A stateless hash to [0, 1): the same (seed, stream, index) always gives the same number, so
// nothing random needs saving.
export function hash01(seed, stream, index) {
  let h = Math.imul(seed | 0, 0x9e3779b1) ^ Math.imul(stream | 0, 0x85ebca77) ^ Math.imul(index | 0, 0xc2b2ae3d);
  h ^= h >>> 16; h = Math.imul(h, 0x7feb352d); h ^= h >>> 15; h = Math.imul(h, 0x846ca68b); h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}
// Smooth value noise in [-1, 1] over a real coordinate x (one lattice value per whole x).
export function noise1(seed, stream, x) {
  const i = Math.floor(x), f = x - i, s = f * f * (3 - 2 * f);
  const a = hash01(seed, stream, i) * 2 - 1, b = hash01(seed, stream, i + 1) * 2 - 1;
  return a + (b - a) * s;
}
