// A short Motion demo: text drawn on and morphed into an equation, a circle
// becoming a square by point correspondence, a live plot whose parameter you
// can drag, a particle field settling into a grid, a camera flight through
// scale with depth of field, over the Threads light-thread layer.
import { circle, rect, plot, line, trim } from "../../web/motion/path.mjs";
import { morph } from "../../web/motion/morph.mjs";
import { text, tex } from "../../web/motion/text.mjs";
import { ease, span, window, track, lerpLog, rng } from "../../web/motion/timeline.mjs";

const INK = "#ece5d6", DIM = "#958e80", HOT = "#e29472", OK = "#63d4ce";

export default {
  title: "Motion demo",
  duration: 32,
  fps: 30,
  params: [{ id: "k", label: "frequency", min: 0.5, max: 4, step: 0.01, value: 1.5 }],
  layers: { threads: { world: 5, particles: 1 << 17, exposure: 0.8, warm: 120 }, worlds: { world: "eye" } },
  chapters: [{ t: 0, title: "Text" }, { t: 6, title: "Morph" }, { t: 11, title: "Plot" }, { t: 16, title: "Scale" }, { t: 23, title: "Into a world" }],
  async load(ctx) {
    const atlas = await ctx.json("hanken-grotesk-500.atlas.json");
    const title = text("Claims and checks", { atlas, size: 120, x: 960, y: 520, anchor: "center" });
    const eq = tex("\\frac{a}{b} \\times 6\\,\\text{h} \\approx 10^{8}", { atlas, size: 110, x: 960, y: 520, anchor: "center" });
    const m1 = morph(title.shape, eq.shape);
    const m2 = morph(circle(960, 540, 220), rect(740, 320, 440, 440, 12));
    const assets = { atlas, title, eq, m1, m2 };
    if (ctx.motion) {
      const n = 4096, r = rng(7), cloud = new Float32Array(2 * n), grid = new Float32Array(2 * n), delay = new Float32Array(n);
      for (let i = 0; i < n; i++) {
        const a = r() * Math.PI * 2, d = Math.sqrt(r()) * 900;
        cloud[2 * i] = 960 + Math.cos(a) * d; cloud[2 * i + 1] = 540 + Math.sin(a) * d * 0.6;
        grid[2 * i] = 960 + ((i % 64) - 31.5) * 14; grid[2 * i + 1] = 540 + (Math.floor(i / 64) - 31.5) * 14;
        delay[i] = (i % 64) / 64;
      }
      const colors = Array.from({ length: n }, (_, i) => (i % 9 === 0 ? HOT : INK));
      assets.field = ctx.motion.particles({ count: n, formations: [cloud, grid], colors: [colors, colors], delay });
    }
    return assets;
  },
  frame(t, ctx) {
    const A = ctx.assets, items = [];
    items.push({ kind: "threads", opacity: 0.45 * window(t, 0, 24, 2, 2) });
    // 0-6: draw on, fill, then morph into the equation.
    const draw = span(t, 0.3, 2.6, ease.inOut);
    const mm = span(t, 3.4, 5.4, ease.inOut);
    if (t < 6.2) {
      const sh = mm > 0 ? A.m1(mm) : trim(A.title.shape, 0, draw);
      items.push({ shape: sh, stroke: INK, width: 1.6, fill: mm > 0 || draw >= 1 ? INK : null, opacity: 1 - span(t, 5.6, 6.2) });
    }
    // 6-11: circle to square.
    const w2 = window(t, 6, 11, 0.5, 0.6);
    if (w2 > 0) items.push({ shape: A.m2(span(t, 7, 9.5, ease.inOut)), stroke: OK, width: 3, fill: [0.39, 0.83, 0.81, 0.08], opacity: w2 });
    // 11-16: a live plot; drag the parameter to change it.
    const w3 = window(t, 11, 16, 0.5, 0.6);
    if (w3 > 0) {
      const k = ctx.params.k, grow = span(t, 11.2, 13.2, ease.inOut);
      const map = (x, y) => [360 + x * 120, 540 - y * 160];
      items.push({ shape: line(360, 540, 1560, 540), stroke: DIM, width: 1.5, opacity: w3 });
      items.push({ shape: line(360, 320, 360, 760), stroke: DIM, width: 1.5, opacity: w3 });
      items.push({ shape: plot((x) => Math.sin(k * x + t) * Math.exp(-0.15 * x), 0, 10 * grow + 1e-3, 400, map), stroke: HOT, width: 4, opacity: w3 });
      items.push({ shape: text(`k = ${k.toFixed(2)}`, { atlas: A.atlas, size: 40, x: 1560, y: 300, anchor: "right" }).shape, fill: INK, opacity: w3 });
    }
    // 23-32: the grid falls away and the camera flies into a Worlds diorama.
    if (t > 23) {
      const u = span(t, 23, 31, ease.inOut);
      items.unshift({ kind: "world", id: "eye", time: t, quality: 0.7, opacity: span(t, 23, 24.5),
        camera: { target: [0, 1.15, 0], distance: 14 - 9.6 * u, yaw: 0.35 + 0.9 * u, pitch: 0.4 - 0.25 * u }, blur: 6 * (1 - span(t, 24, 27)) });
    }
    // 16-24: particles settle into a grid; the camera pulls back through scale.
    if (t > 15.5 && t < 24.5 && A.field) {
      const zoom = lerpLog(6, 0.6, span(t, 16, 23, ease.inOut));
      items.push({ kind: "particles", system: A.field, from: 0, to: 1, t: span(t, 16, 20, ease.linear), spread: 0.5, drift: 8 * (1 - span(t, 18, 21)), time: t, size: [3, 4], blend: "add" });
      return { camera: { x: 960, y: 540, zoom }, items, post: { bloom: 0.8 } };
    }
    const cam = track([[0, [960, 540, 1]], [16, [960, 540, 1]]])(t);
    return { camera: { x: cam[0], y: cam[1], zoom: cam[2], aperture: 30, focus: 0 }, items };
  },
};
