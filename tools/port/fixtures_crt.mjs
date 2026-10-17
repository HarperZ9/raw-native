// Fixtures of the physical CRT (evidence/cpp-port-style-bounds-12.json with Addendum A): the
// eleven cases of evidence/shaders-crt-parity-bounds.json as web/test/shaders-crt.html runs them.
import { join } from "node:path";
import { createCrt, encode8 } from "../../web/shaders/crt/crt.mjs";
import { packParams } from "../../web/shaders/crt/layout.mjs";
import { packTaps } from "../../web/shaders/crt/signal.mjs";
import { testCard } from "../../web/shaders/fixtures/testcard.mjs";
import { Bundle, readJson } from "./rnfx.mjs";

// packParams and packTaps build Float32Array; with the global swapped they return the same values
// before the f32 store (Addendum A).
function inFloat64(fn) {
  const F = globalThis.Float32Array;
  globalThis.Float32Array = Float64Array;
  try { return fn(); } finally { globalThis.Float32Array = F; }
}

export function crt(out) {
  const B = new Bundle("crt"), bounds = readJson("evidence/shaders-crt-parity-bounds.json"), src = testCard();
  const { width: W, height: H } = bounds.output, s = { w: src.width, h: src.height }, o = { w: W, h: H };
  B.head.cases = [];
  B.add("src", "f32", src.data);
  for (const c of bounds.cases) {
    const cpu = createCrt(c.preset, c.overrides || {}, s, o);
    let ref;
    for (let f = 0; f < c.frames; f++) ref = cpu.frame(src, f);
    const p = c.id + ".";
    B.add(p + "params", "f64", inFloat64(() => packParams(cpu.plan, packTaps(cpu.plan.taps), 0)));
    B.add(p + "taps", "f64", inFloat64(() => packTaps(cpu.plan.taps).data));
    B.add(p + "halo", "f32", cpu.plan.halo.weights);
    B.add(p + "sig", "f32", ref.sig);
    B.add(p + "img", "f32", ref.img);
    B.add(p + "js", "u8", encode8(ref.img, W, H, 1));
    const persistence = c.id === "radar-p7-persistence";
    if (persistence) { B.add(p + "em", "f32", ref.em); B.add(p + "hist", "f32", cpu.hist); }
    B.head.cases.push({ id: c.id, preset: c.preset, overrides: c.overrides || {}, frames: c.frames, width: W, height: H, emission: persistence });
  }
  B.write(join(out, "crt.rnfx"));
  return B.head.cases.length;
}
