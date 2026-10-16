// The assessor's instruments: the environment state the rules read, shown to the player
// (GAME-DESIGN: nothing in play depends on a value the player cannot inspect). Screen-space
// Motion items in the corner: a clock, a wind vane, a wetness gauge for the ground underfoot,
// a light meter, and the lamp's state. Text uses the canon face's atlas when it is loaded
// (Hanken Grotesk); without it the dials still read.
import { circle, rect, polyline, rotateAbout } from "../../../web/motion/path.mjs";
import { text } from "../../../web/motion/text.mjs";

const INK = [0.93, 0.9, 0.84, 0.92], DIM = [0.93, 0.9, 0.84, 0.35], HOT = [0.98, 0.72, 0.38, 0.95];
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const hhmm = (h) => { const m = Math.floor(((h % 24) + 24) % 24 * 60); return `${String(Math.floor(m / 60)).padStart(2, "0")}:${String(m % 60).padStart(2, "0")}`; };

export function hud(snap, zoneName, { lampLit, soak }, atlas, { x = 40, y = 900 } = {}) {
  const z = snap.zones[zoneName], items = [], scr = (it) => items.push({ ...it, screen: true });
  scr({ shape: rect(x - 18, y - 22, 600, 150, 6), fill: [0.02, 0.02, 0.03, 0.62] });
  // Clock: one hand, a full turn every 12 hours.
  const cx = x + 40, cy = y + 52, a = ((snap.time.hour % 12) / 12) * 2 * Math.PI - Math.PI / 2;
  scr({ shape: circle(cx, cy, 30, 40), fill: null, stroke: DIM, width: 2 });
  scr({ shape: polyline([cx, cy, cx + Math.cos(a) * 24, cy + Math.sin(a) * 24]), fill: null, stroke: INK, width: 3 });
  // Wind vane: the arrow points where the wind blows to, its length the speed (10 m/s fills it).
  const vx = x + 140, w = z.wind, sp = Math.hypot(w[0], w[1]), len = 6 + 24 * clamp(sp / 10, 0, 1), d = sp > 0 ? [w[0] / sp, w[1] / sp] : [1, 0];
  scr({ shape: circle(vx, cy, 30, 40), fill: null, stroke: DIM, width: 2 });
  const tip = [vx + d[0] * len, cy + d[1] * len];
  scr({ shape: polyline([vx - d[0] * len, cy - d[1] * len, ...tip]), fill: null, stroke: INK, width: 3 });
  scr({ shape: rotateAbout(polyline([tip[0] - 8, tip[1] - 5, tip[0], tip[1], tip[0] - 8, tip[1] + 5]), Math.atan2(d[1], d[0]), tip[0], tip[1]), fill: null, stroke: INK, width: 3 });
  // Gauges: wetness of the ground here, and the light level (key, log scale from 0.01 to 2).
  const bar = (bx, v, hot) => { scr({ shape: rect(bx, y + 22, 18, 60), fill: null, stroke: DIM, width: 2 }); scr({ shape: rect(bx + 3, y + 79 - 54 * clamp(v, 0, 1), 12, 54 * clamp(v, 0, 1)), fill: hot ? HOT : INK }); };
  bar(x + 220, z.wetness, z.wet);
  bar(x + 270, (Math.log10(Math.max(0.01, z.light)) + 2) / 2.3, false);
  // The lamp: a flame when lit, shrinking with the soak; a cross when out.
  const lx = x + 340, ly = cy;
  if (lampLit) scr({ shape: polyline([lx, ly + 16, lx - 8, ly, lx, ly - 18 * (1 - 0.6 * clamp(soak, 0, 1)), lx + 8, ly], true), fill: HOT });
  else scr({ shape: [...polyline([lx - 10, ly - 10, lx + 10, ly + 10]), ...polyline([lx + 10, ly - 10, lx - 10, ly + 10])], fill: null, stroke: DIM, width: 3 });
  if (atlas) {
    const label = (s, tx, ty, size = 20, c = DIM) => scr({ shape: text(s, { atlas, size, x: tx, y: ty, anchor: "center" }).shape, fill: c });
    label(hhmm(snap.time.hour), cx, y + 112, 22, INK);
    label(`${sp.toFixed(1)} m/s`, vx, y + 112, 20);
    label("wet", x + 229, y + 112); label("light", x + 279, y + 112); label(lampLit ? "lamp" : "out", lx, y + 112);
    const w8 = snap.weather.weights, top = Object.entries(w8).sort((p, q) => q[1] - p[1])[0][0];
    label(top, x + 470, y + 50, 30, INK); label(`${snap.weather.rainRate.toFixed(0)} mm/h`, x + 470, y + 88, 20);
  }
  return items;
}
