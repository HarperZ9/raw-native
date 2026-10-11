// Beats: where each guide stands in each template world, its pose, and the film 1 props
// around it. The home-style beat of each guide is a moment from film 1, "Claiming got cheap.
// Checking did not."; the other two show the same guide carried into another template.
import { P, ellipse, seeded, lin, INK } from "./rig.mjs";
import { coneParts } from "./pylon.mjs";
import { rect } from "../../web/motion/path.mjs";
import { label } from "./look.mjs";
import { floorCards } from "./sets.mjs";

const PAPER = "#d9cdb0";

// Claim cards scattered on a floor: n cards in the box, seeded. lit(i) dims or lights each.
export function cards(n, x0, y0, x1, y1, seed, { w = 26, h = 17, lit = () => 1 } = {}) {
  const r = seeded(seed), out = [];
  for (let i = 0; i < n; i++) {
    const cx = x0 + r() * (x1 - x0), cy = y0 + r() * (y1 - y0), a = (r() - 0.5) * 1.2, c = Math.cos(a), s = Math.sin(a);
    const pt = (u, v) => `${(cx + u * c - v * s).toFixed(1)} ${(cy + u * s + v * c).toFixed(1)}`;
    const k = lit(i);
    out.push({ shape: P(`M ${pt(-w / 2, -h / 2)} L ${pt(w / 2, -h / 2)} L ${pt(w / 2, h / 2)} L ${pt(-w / 2, h / 2)} Z`),
      fill: [0.85 * k, 0.8 * k, 0.69 * k, 1], stroke: INK, width: 1.2 });
    out.push({ shape: P(`M ${pt(-w / 3, -h / 6)} L ${pt(w / 3, -h / 6)} M ${pt(-w / 3, h / 6)} L ${pt(w / 6, h / 6)}`), fill: null, stroke: [0.3 * k, 0.26 * k, 0.2 * k, 1], width: 1.2 });
  }
  return out;
}

// Paper boats on the harbour plaza: claims that washed up.
export function boats(n, x0, y0, x1, y1, seed, coned = () => false) {
  const r = seeded(seed), out = [];
  for (let i = 0; i < n; i++) {
    const x = x0 + r() * (x1 - x0), y = y0 + r() * (y1 - y0), w = 24 + r() * 8;
    out.push({ shape: P(`M ${x - w} ${y - 6} L ${x + w} ${y - 6} L ${x + w * 0.6} ${y + 6} L ${x - w * 0.6} ${y + 6} Z`), fill: "#e9dfc6", stroke: INK, width: 2 });
    out.push({ shape: P(`M ${x - w * 0.5} ${y - 6} L ${x + 2} ${y - 34} L ${x + w * 0.5} ${y - 6} Z`), fill: "#f6efdd", stroke: INK, width: 2 });
    if (coned(i)) for (const p of coneParts(x, y + 4, { scale: 0.3 })) out.push({ shape: P(p.d), fill: p.fill, stroke: INK, width: 1.5 });
  }
  return out;
}

// Shells on the beach: a grid of claims. state(i): 0 unknocked, 1 holds, 2 hollow.
export function shells(cols, rows, x0, y0, dx, dy, state) {
  const out = [];
  for (let j = 0; j < rows; j++) for (let i = 0; i < cols; i++) {
    const k = j * cols + i, x = x0 + i * dx + (j % 2) * dx * 0.5, y = y0 + j * dy, st = state(k);
    const fill = st === 2 ? "#1d2b53" : st === 1 ? "#fff1e8" : "#c2c3c7";
    out.push({ shape: P(`M ${x - 22} ${y} C ${x - 24} ${y - 26} ${x - 6} ${y - 36} ${x + 8} ${y - 32} C ${x + 24} ${y - 26} ${x + 26} ${y - 8} ${x + 22} ${y} Z`), fill, stroke: INK, width: 3 });
    out.push({ shape: P(`M ${x - 10} ${y - 4} C ${x - 8} ${y - 18} ${x + 6} ${y - 22} ${x + 12} ${y - 12}`), fill: null, stroke: st === 2 ? "#5f574f" : "#83769c", width: 3 });
  }
  return out;
}

// A key term on a calm ground, bottom centre (the bible's 1 to 6 words).
const keyTerm = (A, s, L, size = 44, y = 968, x = 960) => [
  { shape: rect(x - s.length * size * 0.27 - 40, y - size - 18, s.length * size * 0.54 + 80, size + 44), screen: true, fill: [0.03, 0.028, 0.03, 0.82] },
  label(A.atlas, s, size, x, y, L.ink),
];

const tallowHome = {
  x: 700, y: 985, s: 0.88, o: { armR: 160, prop: "card", look: [6, -7], lid: 0.15, lean: 10 },
  props: () => floorCards(40, "night-floor", { lit: (i) => 0.55 + 0.45 * ((i * 7) % 5 === 0) }),
  hud: (style, A, L) => keyTerm(A, "Does this work exist?", L, 44, 1000, 1380),
};
const pylonHome = {
  x: 930, y: 700, s: 0.55, camera: { x: 960, y: 560, zoom: 1.15 }, o: { tilt: 8, lid: 0.15, flipR: 55, look: [2, 2] },
  props: () => [
    ...boats(40, 520, 420, 1420, 860, "harbour", (i) => i % 3 === 0),
    ...[0].flatMap((k) => coneParts(1150, 760 - k * 18, { scale: 0.42 }).map((p) => ({ shape: P(p.d), fill: p.fill, stroke: INK, width: 2 }))),
  ],
  hud: (style, A, L) => keyTerm(A, "1 boat = 1 reference", L, 48),
};
const rerunHome = {
  x: 640, y: 560, s: 0.5, camera: { x: 800, y: 470, zoom: 2 }, o: { clawR: 70, look: [4, -3] },
  props: () => shells(6, 3, 800, 470, 56, 50, (k) => (k < 10 ? ((k * 5) % 9 < 5 ? 2 : 1) : 0)),
  hud: (style, A, L) => [label(A.atlas, "1 shell = 1 reference", 64, 960, 1010, "#fff1e8")],
};

export const BEATS = {
  tallow: {
    thriller: tallowHome,
    painted: { x: 960, y: 660, s: 0.5, camera: { x: 960, y: 520, zoom: 1.15 }, o: { armR: -40, prop: "loupe", lid: 0.5 }, yaw: 20,
      props: () => cards(26, 640, 520, 1280, 780, "plaza-cards") },
    pixel: { x: 640, y: 560, s: 0.3, camera: { x: 760, y: 450, zoom: 2 }, o: { armR: 160, prop: "card", look: [6, -7], lid: 0.15 },
      props: () => cards(10, 740, 440, 1060, 640, "beach-cards", { w: 40, h: 26 }) },
  },
  pylon: {
    painted: pylonHome,
    thriller: { x: 760, y: 1000, s: 0.95, o: { tilt: 15, lid: 0.5 },
      props: () => floorCards(30, "dock-floor", { lit: () => 0.6 }) },
    pixel: { x: 640, y: 580, s: 0.36, camera: { x: 760, y: 470, zoom: 2 }, o: { bark: 1, lid: 0.05, tilt: -4 },
      props: () => boats(8, 740, 440, 1060, 640, "pixel-boats", (i) => i % 2 === 0) },
  },
  rerun: {
    pixel: rerunHome,
    thriller: { x: 760, y: 1010, s: 1.05, o: { clawR: 70, look: [6, 2] },
      props: () => [...floorCards(24, "pool-floor", { lit: () => 0.5 }), ...shells(1, 1, 1190, 1000, 0, 0, () => 2)] },
    painted: { x: 900, y: 690, s: 0.75, camera: { x: 960, y: 520, zoom: 1.15 }, o: { stalk: 1.6, look: [0, -6] }, yaw: 20,
      props: () => shells(4, 2, 1120, 640, 70, 60, (k) => (k % 3 === 0 ? 2 : 1)) },
  },
};

// The playable lesson's level, drawn as a still: Night Shift in the thriller corridor.
export const LESSON = {
  x: 760, y: 900, s: 0.62, o: { armR: 160, prop: "card", look: [8, -4], lid: 0.3, flame: 0.8, lean: 12 },
  props: () => [
    ...floorCards(34, "spread", { lit: (i) => (i % 3 ? 0.45 : 0.85), x0: 1150, y0: 700 }),
    ...cards(16, 40, 690, 520, 760, "queue", { w: 46, h: 30, lit: () => 0.75 }),
  ],
  front: () => [
    { shape: rect(860, 760, 520, 40), fill: "#4a3428", stroke: INK, width: 3 },
    { shape: rect(880, 800, 480, 230), fill: lin(0, 800, 0, 1030, [0, "#2e2019"], [1, "#150e0a"]), stroke: INK, width: 3 },
    ...cards(5, 900, 740, 1340, 770, "desk", { w: 46, h: 28 }),
  ],
  hud: (style, A, L) => {
    const box = (x, y, w, h) => ({ shape: rect(x, y, w, h), screen: true, fill: [0.03, 0.028, 0.03, 0.85], stroke: [0.85, 0.8, 0.7, 0.5], width: 1.5 });
    const keys = [["C", "check, 6 h"], ["R", "rerun, 2 to 4 h"], ["L", "let through"], ["H", "hold, 3 left"]];
    return [
      box(40, 40, 470, 150), label(A.atlas, "Night Shift", 40, 64, 92, L.ink, "left"),
      label(A.atlas, "hours left: 22 of 40", 30, 64, 140, "#e8b860", "left"), label(A.atlas, "claims waiting: 18", 26, 64, 176, L.dim, "left"),
      box(40, 940, 1840, 100),
      ...keys.flatMap(([k, s], i) => [label(A.atlas, `[${k}]`, 30, 80 + i * 450, 1002, "#e8b860", "left"), label(A.atlas, s, 30, 140 + i * 450, 1002, L.ink, "left")]),
      box(1380, 40, 500, 110), label(A.atlas, "Tallow: Your guess first.", 28, 1404, 88, L.ink, "left"),
      label(A.atlas, "How many can we check tonight?", 26, 1404, 126, L.dim, "left"),
    ];
  },
};
