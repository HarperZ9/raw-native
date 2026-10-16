// Low Water's gameplay rules on the environment state (ADR 0017). Pure: runs in Node, and each
// rule is checked in slice.test.mjs without rendering, by feeding commands and reading snapshots.
//
//   footprints  a walker on a wet open floor leaves a print every 0.45 s; rain erases prints at
//               0.02 per (mm/h * s) of rain reaching them, and they fade slowly as the ground dries
//   street lamps switch on when the quay turns dark (the snapshot's hysteresis band), one after
//               another 0.4 s apart, and off when it turns light
//   door        open: the room's shelter falls to 0.95, so wind and a little rain come in (wetness
//               that is still rising indoors says the door was opened); closed: shelter 1
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
// Timers add the fixed step each step; 27 steps of 1/60 s sum to a hair under 0.45 s in floating
// point, so every timer compares with this tolerance (found by the first test run).
const EPS = 1e-9;

export const PRINT_EVERY = 0.45, MAX_PRINTS = 90, PRINT_WET = 0.25, ERASE_PER_MMH = 0.02, FADE = 0.002;

export function stepPrints(prints, { pos, facing, moving, wetness, open }, rainRate, dt, since) {
  const out = [];
  for (const p of prints) {
    const s = p.s - (ERASE_PER_MMH * rainRate * p.open + FADE) * dt;
    if (s > 0.02) out.push({ ...p, s });
  }
  let next = since + dt, left = 0;
  if (moving && wetness > PRINT_WET && next + EPS >= PRINT_EVERY) {
    left = out.length % 2;                                   // alternate feet
    const side = left ? 1 : -1, nx = -Math.sin(facing) * 5 * side, ny = Math.cos(facing) * 5 * side;
    out.push({ x: pos[0] + nx, y: pos[1] + ny, a: facing, s: clamp(0.4 + wetness, 0, 1), open });
    next = 0;
  } else if (!moving) next = Math.min(next, PRINT_EVERY);
  return { prints: out.slice(-MAX_PRINTS), since: next };
}

export function stepLamps(lamps, dark, dt) {
  // lamps: [{ on: bool, level: 0..1, wait: seconds }]; dark: the quay's dark flag.
  return lamps.map((l, i) => {
    let { on, level, wait } = l;
    if (dark !== on) { wait += dt; if (wait + EPS >= 0.4 * (i + 1)) { on = dark; wait = 0; } } else wait = 0;
    level = clamp(level + (on ? 1.5 : -2) * dt, 0, 1);
    return { on, level, wait };
  });
}
// A street lamp warming up flickers once or twice before it holds: 1 when settled.
export const lampFlicker = (level, t, i) => (level >= 1 ? 1 : level * (0.6 + 0.4 * Math.abs(Math.sin(t * 31 + i * 7))));

export const doorCommand = (open) => ({ type: "setZone", zone: "shelter", shelter: open ? 0.95 : 1 });
