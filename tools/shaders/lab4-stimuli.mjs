// Stimuli for the shader lab 4 blind panel (evidence/shaders-lab4-panel-protocol.json), from the CPU references.
//   node tools/shaders/lab4-stimuli.mjs <outdir> crt <preset> <in.rgba> <W> <H>
//   node tools/shaders/lab4-stimuli.mjs <outdir> pigment
//   node tools/shaders/lab4-stimuli.mjs <outdir> sagclip flash|sweep on|off
//   node tools/shaders/lab4-stimuli.mjs <outdir> ditherclip <mode>
// in.rgba: u32 width, u32 height, then RGBA8 R'G'B' signal (the panel's input frames).
import { readFileSync, mkdirSync } from "node:fs";
import os from "node:os";
import { writePng, nearest } from "./png.mjs";
import { createCrt, encode8 } from "../../web/shaders/crt/crt.mjs";

try { os.setPriority(os.constants.priority.PRIORITY_BELOW_NORMAL); } catch (e) { console.error("priority not lowered:", e.message); }
const [out, kind, ...a] = process.argv.slice(2), t0 = Date.now();
mkdirSync(out, { recursive: true });
const pad = (i) => String(i).padStart(2, "0");

if (kind === "crt") {
  const [preset, input, W, H] = [a[0], a[1], +a[2], +a[3]], b = readFileSync(input), w = b.readUInt32LE(0), h = b.readUInt32LE(4);
  const d = new Float32Array(w * h * 4); for (let i = 0; i < d.length; i++) d[i] = b[8 + i] / 255;
  const crt = createCrt(preset, {}, { w, h }, { w: W, h: H }); let r;
  for (let f = 0; f < 2; f++) r = crt.frame({ width: w, height: h, data: d }, f);
  const name = input.split(/[\\/]/).pop().replace(".rgba", "");
  writePng(`${out}/rn-${preset}-${name}-${W}.png`, { width: W, height: H, data: encode8(r.img, W, H, 1) });
} else if (kind === "pigment") {
  const { labSource } = await import("../../web/shaders/lab/sources.mjs");
  const { labEncode8 } = await import("../../web/shaders/lab/gpu-kit.mjs");
  const { resolveLightpaint, runLightpaint } = await import("../../web/shaders/lightpaint/lightpaint.mjs");
  const src = labSource("stilllife-sat", 640, 400), img8 = (f) => ({ width: f.width, height: f.height, data: labEncode8(f) });
  writePng(`${out}/lp-stilllife-multiply.png`, img8(src));
  for (const p of ["disco", "grotesque"]) writePng(`${out}/lp-stilllife-${p}.png`, img8(runLightpaint(resolveLightpaint(p), src)));
} else if (kind === "sagclip") {
  // 24 frames at 24 fps through the sag pass (thriller; "off" is the same pass with S = 0, so the
  // spot softening of the resampler is in both) and then the trinitron-tv CRT, persistence carried.
  const [clip, onoff] = a, { labSource } = await import("../../web/shaders/lab/sources.mjs");
  const { resolveSag, runSag } = await import("../../web/shaders/sag/sag.mjs");
  const w = 320, h = 240, base = labSource("street-signal", w, h), N = 24;
  const plan = resolveSag("thriller", { settle: false, ...(onoff === "off" ? { S: 0 } : {}) }, { w, h });
  const crt = createCrt("trinitron-tv", {}, { w, h }, { w: 640, h: 480 }), dir = `${out}/sag-${clip}-${onoff}`;
  mkdirSync(dir, { recursive: true });
  let carry = 0;
  for (let f = 0; f < N; f++) {
    const d = new Float32Array(w * h * 4), g = f === 6 || f === 7 ? 3 : f === 8 ? 1.6 : 1, wy = Math.round((f / (N - 1)) * (h * 0.8));
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4, win = clip === "sweep" && x > w * 0.35 && x < w * 0.65 && y >= wy && y < wy + h * 0.12;
      for (let c = 0; c < 3; c++) d[i + c] = win ? 1 : Math.min(1, base.data[i + c] * (clip === "flash" ? g : 0.7));
      d[i + 3] = 1;
    }
    plan.p.initial = carry; const r = runSag(plan, { width: w, height: h, data: d }); carry = r.sc.end * Math.pow(1 - plan.alpha, plan.blank);
    const img = crt.frame(r.out, f).img;
    writePng(`${dir}/f${pad(f)}.png`, { width: 640, height: 480, data: encode8(img, 640, 480, 1) });
  }
} else if (kind === "ditherclip") {
  // gameFrame(320, 240, t) to PICO-8, 24 frames at 30 fps, shown nearest x 2.
  const { sequences } = await import("./dither_temporal.mjs"), { ditherRef } = await import("../../web/shaders/dither/dither.mjs");
  const { labPalette } = await import("../../web/shaders/reference/retro-palettes.mjs");
  const mode = a[0], seq = sequences().game, pal = labPalette("pico8"), dir = `${out}/dither-game-${mode}`;
  mkdirSync(dir, { recursive: true });
  for (let i = 0; i < 24; i++) {
    const s = seq.frame(i), idx = ditherRef(s, seq.w, seq.h, { palette: "pico8", mode }), o = new Uint8ClampedArray(seq.w * seq.h * 4);
    for (let p = 0; p < idx.length; p++) { o.set(pal[idx[p]].rgb, p * 4); o[p * 4 + 3] = 255; }
    writePng(`${dir}/f${pad(i)}.png`, nearest({ width: seq.w, height: seq.h, data: o }, 2));
  }
} else throw new Error("unknown kind " + kind);
console.log(kind, a.join(" "), ((Date.now() - t0) / 1000).toFixed(1) + " s");
