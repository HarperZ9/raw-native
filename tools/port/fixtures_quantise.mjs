// Fixtures of hysteresis quantisation and palette-locked lighting for the C++ core
// (evidence/cpp-port-style-bounds-3.json). The cases are the web parity pages' cases, built with the
// web modules unchanged; the JavaScript references' decisions and RGBA8 codes are exported.
import { join } from "node:path";
import { resolveHysteresis, step } from "../../web/shaders/hysteresis/hysteresis.mjs";
import { sequence } from "../../web/shaders/hysteresis/sequences.mjs";
import { chapelCamera, renderChapel } from "../../web/shaders/fixtures/chapel-render.mjs";
import { resolvePalettelight, runPalettelight } from "../../web/shaders/palettelight/palettelight.mjs";
import { Bundle, readJson } from "./rnfx.mjs";

export function hysteresis(out) {
  const b = readJson("evidence/shaders-hysteresis-parity-bounds.json"), { width: W, height: H } = b.output, B = new Bundle("hysteresis"), seqs = new Map();
  B.head.cases = [];
  for (const c of b.cases) {
    const key = `${c.sequence}/${c.frames}`;
    if (!seqs.has(key)) {
      const frames = sequence(c.sequence, W, H, c.frames);
      frames.forEach((f, k) => {
        B.add(`${key}.${k}.rgba`, "f32", f.frame.data); B.add(`${key}.${k}.dist`, "f32", f.dist);
        if (f.motion) { B.add(`${key}.${k}.mv`, "f32", f.motion.mv); B.add(`${key}.${k}.dA`, "f32", f.motion.dA); }
      });
      seqs.set(key, frames);
    }
    const frames = seqs.get(key), plan = resolveHysteresis(c.preset, {}, { w: W, h: H });
    let state = null;
    frames.forEach((f, k) => {
      const r = step(plan, f.frame, f.motion, f.dist, state); state = r.state;
      B.add(`${c.id}.${k}.js_idx`, "u32", Uint32Array.from(r.idx)); B.add(`${c.id}.${k}.js_rgba`, "u8", r.out);
    });
    B.head.cases.push({ id: c.id, preset: c.preset, sequence: key, frames: frames.length, motion: frames.map((f) => !!f.motion), width: W, height: H, p: plan.p, K: plan.K, pal: plan.pal });
  }
  B.write(join(out, "hysteresis.rnfx"));
  return B.head.cases.length;
}

const radialJson = (L) => ({ pos: L.pos, radius: L.radius, core: L.core, intensity: L.intensity, wrap: L.wrap });
export function palettelight(out) {
  const b = readJson("evidence/shaders-palettelight-parity-bounds.json"), { width: W, height: H } = b.output, B = new Bundle("palettelight");
  B.head.cases = [];
  for (const c of b.cases) {
    const cam = chapelCamera(c.camera || {}), g = renderChapel(cam, W, H, { lamp: c.lamp }), plan = resolvePalettelight({ lampPos: c.lamp, ...(c.plan || {}) });
    const r = runPalettelight(plan, g, c.time), p = `${c.id}.`;
    B.add(p + "hit", "u8", g.hit); B.add(p + "cls", "u8", g.cls); B.add(p + "a", "f32", g.a); B.add(p + "phase", "f32", g.phase);
    B.add(p + "P", "f32", g.P); B.add(p + "N", "f32", g.N); B.add(p + "vis", "f32", g.vis);
    B.add(p + "js_idx", "u32", Uint32Array.from(r.idx)); B.add(p + "js_rgba", "u8", r.rgba);
    const pal = plan.palette;
    B.head.cases.push({
      id: c.id, width: W, height: H, time: c.time, overrides: c.plan || {}, lamp: c.lamp,
      plan: { gain: plan.gain, ambient: plan.ambient, moon: plan.moon, cycleRate: plan.cycleRate, steps: plan.steps, voidSteps: plan.voidSteps, moonDir: plan.moonDir,
        lamp: radialJson(plan.lamp), glows: plan.glows.map(radialJson) },
      palette: { rgb: pal.entries.map((e) => e.rgb), lab: pal.entries.map((e) => e.lab), ramp: pal.ramp },
    });
  }
  B.write(join(out, "palettelight.rnfx"));
  return B.head.cases.length;
}
