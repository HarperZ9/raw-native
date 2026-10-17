// Fixtures of scan-causal EHT sag and the salience caricature warp (evidence/cpp-port-style-bounds-8.json),
// the passes that need workgroup memory. Each case is the web parity page's case
// (web/test/shaders-sag.html, shaders-caricature.html), built with the web modules unchanged.
import { join } from "node:path";
import { resolveSag, runSag, signal8 } from "../../web/shaders/sag/sag.mjs";
import { resolveCaricature, runCaricature, salienceFrom } from "../../web/shaders/caricature/caricature.mjs";
import { labSource } from "../../web/shaders/lab/sources.mjs";
import { labEncode8 } from "../../web/shaders/lab/gpu-kit.mjs";
import { Bundle, readJson } from "./rnfx.mjs";

export function sag(out) {
  const b = readJson("evidence/shaders-sag-parity-bounds.json"), { width: W, height: H } = b.output, B = new Bundle("sag");
  B.head.cases = [];
  for (const c of b.cases) {
    const src = c.frame === "game" ? labSource("game", 256, 224, 1) : labSource(c.frame === "street" ? "street-signal" : c.frame, W, H), size = { w: src.width, h: src.height };
    const plan = resolveSag(c.preset, {}, size), r = runSag(plan, src), id = c.id;
    B.add(`${id}.src`, "f32", src.data); B.add(`${id}.out`, "f32", r.out.data); B.add(`${id}.js`, "u8", signal8(r.out));
    B.add(`${id}.I`, "f64", r.I); B.add(`${id}.v`, "f64", r.sc.v); B.add(`${id}.k`, "f64", r.sc.k); B.add(`${id}.Y`, "f64", r.sc.Y); B.add(`${id}.sig`, "f64", r.sc.sig);
    B.head.cases.push({ id, preset: c.preset, frame: c.frame, width: size.w, height: size.h, p: plan.p,
      plan: { blank: plan.blank, tauLines: plan.tauLines, alpha: plan.alpha, R: plan.R } });
  }
  B.write(join(out, "sag.rnfx"));
  return B.head.cases.length;
}

export function caricature(out) {
  const b = readJson("evidence/shaders-caricature-parity-bounds.json"), { width: W, height: H } = b.output, B = new Bundle("caricature"), gain = 2.4;
  B.head.cases = [];
  for (const c of b.cases) {
    const src = labSource(c.frame, W, H), plan = resolveCaricature(c.preset, {}, { w: W, h: H }), r = runCaricature(plan, src, salienceFrom(plan, src.mat)), id = c.id;
    B.add(`${id}.src`, "f32", src.data); B.add(`${id}.mat`, "u32", Uint32Array.from(src.mat, (m) => m >>> 0));
    B.add(`${id}.out`, "f32", r.out.data); B.add(`${id}.js`, "u8", labEncode8(r.out, gain));
    B.add(`${id}.taps`, "f64", plan.taps); B.add(`${id}.S`, "f64", r.S); B.add(`${id}.g`, "f64", r.g);
    B.head.cases.push({ id, preset: c.preset, frame: c.frame, width: W, height: H, gain, R: plan.R, lambda: r.lambda, p: plan.p });
  }
  B.write(join(out, "caricature.rnfx"));
  return B.head.cases.length;
}
