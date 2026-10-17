// Fixtures of the painterly shader (evidence/cpp-port-style-bounds-11.json): the still cases, the
// advected-canvas sequences and the post frames, as web/test/shaders-paint.html runs them.
import { join } from "node:path";
import { createPaint, encodePaint8 } from "../../web/shaders/paint/paint.mjs";
import { streetScene, motionVectors } from "../../web/shaders/fixtures/street.mjs";
import { frameSet } from "../../tests/web/post_frames.mjs";
import { Bundle, readJson } from "./rnfx.mjs";

export function paint(out) {
  const B = new Bundle("paint");
  B.head.cases = [];
  const add = (id, set, preset, overrides, w, h, img, extra = {}, keepImage = true) => {
    if (keepImage) B.add(`${id}.img`, "f32", img);
    B.add(`${id}.js`, "u8", encodePaint8(img, w, h));
    B.head.cases.push({ id, set, preset, overrides, width: w, height: h, ...extra });
  };
  // Stills: offset canvas.
  const sb = readJson("evidence/shaders-paint-parity-bounds.json"), W = 240, H = 150, dusk = streetScene(W, H, 0), day = streetScene(W, H, 0, { daylight: true });
  B.add("still.dusk", "f32", dusk.data); B.add("still.day", "f32", day.data);
  for (const c of sb.cases) {
    const cpu = createPaint(c.preset, c.overrides || {}, { w: W, h: H }), ref = cpu.frame(c.daylight ? day : dusk);
    if (!B.head.tw) { B.head.tw = Array.from(cpu.plan.tw); }
    add(c.id, "stills", c.preset, c.overrides || {}, W, H, ref.img, { scene: c.daylight ? "still.day" : "still.dusk" });
  }
  // Advected canvas: camera sequences with motion vectors.
  const mb = readJson("evidence/shaders-paint-motion-parity-bounds.json"), MW = 160, MH = 100, R = Math.hypot(5.2, 6.4);
  const cams = { orbit: (f) => { const a = 0.69 + f * 0.01; return { eye: [R * Math.sin(a), 4.6, R * Math.cos(a)] }; },
    dolly: (f) => ({ eye: [5.2 * (1 - 0.012 * f), 4.6 * (1 - 0.012 * f), 6.4 * (1 - 0.012 * f)] }),
    pan: (f) => ({ eye: [5.2 + 0.02 * f, 4.6, 6.4], target: [0.2 + 0.02 * f, 0.8, 0] }) };
  for (const c of mb.cases) {
    const cpu = createPaint(c.preset, {}, { w: MW, h: MH });
    let prev = null, ref;
    for (let f = 0; f < c.frames; f++) {
      const sc = streetScene(MW, MH, 0, cams[c.camera](f)), m = prev ? motionVectors(prev, sc) : { mv: new Float32Array(MW * MH * 2), dA: sc.depth };
      ref = cpu.frame(sc, { mv: m.mv, dist: sc.depth, distPrev: m.dA }); prev = sc;
      const p = `motion-${c.id}.f${f}.`;
      B.add(p + "scene", "f32", sc.data); B.add(p + "mv", "f32", m.mv); B.add(p + "dist", "f32", sc.depth); B.add(p + "distPrev", "f32", m.dA);
    }
    const L = ref.canvas.layer;
    add("motion-" + c.id, "motion", c.preset, {}, MW, MH, ref.img, { frames: c.frames, layers: L.map((l) => ({ age: l.age, seed: l.seed, meanD: l.meanD })) });
  }
  // The committed post frames.
  const pb = readJson("evidence/shaders-paint-postframes-parity-bounds.json"), fs = frameSet(), used = new Set();
  for (const c of pb.cases) {
    const data = fs.data[fs.names.indexOf(c.frame)];
    if (!used.has(c.frame)) { B.add(`post.${c.frame}`, "f32", data); used.add(c.frame); }
    const ref = createPaint(c.preset, {}, { w: fs.width, h: fs.height }).frame({ width: fs.width, height: fs.height, data });
    add("post-" + c.id, "postframes", c.preset, {}, fs.width, fs.height, ref.img, { scene: `post.${c.frame}` }, false);   // codes only (bounds-11 Addendum A)
  }
  B.write(join(out, "paint.rnfx"));
  return B.head.cases.length;
}
