// The post stack's fixtures for the C++ driver (evidence/cpp-port-style-bounds-2.json, post_stack):
// plans of web/motion/post.mjs planScenePasses with the web registry exactly as it is (importing
// web/shaders/post.mjs registers crt, film, crt-classic, dither and paint), and chains of the passes
// that have a C++ reference, run through each pass's cpu() in the planned order as the stack feeds
// them (Float32Array frames between passes).
import { join } from "node:path";
import { planScenePasses, getPass, registeredPasses } from "../../web/motion/post.mjs";
import "../../web/shaders/post.mjs";
import { labSource } from "../../web/shaders/lab/sources.mjs";
import { Bundle } from "./rnfx.mjs";

// The lists the plan link compares. A list is { id, colour, passes }.
export const PLAN_LISTS = [
  { id: "exposure", colour: false, passes: [{ pass: "exposure", ev: 1 }] },
  { id: "film", colour: false, passes: [{ pass: "film" }] },
  { id: "film-colour", colour: true, passes: [{ pass: "film" }] },
  { id: "film-then-exposure", colour: false, passes: [{ pass: "film" }, { pass: "exposure", ev: -1 }] },
  { id: "film-then-paint", colour: false, passes: [{ pass: "film" }, { pass: "paint" }] },
  { id: "film-twice", colour: false, passes: [{ pass: "film" }, { pass: "film" }] },
  { id: "mixed-stages", colour: false, passes: [{ pass: "exposure", ev: 1 }, { pass: "crt" }, { pass: "dither", mode: "bayer4" }] },
  { id: "paint-crt", colour: false, passes: [{ pass: "paint" }, { pass: "crt-classic" }] },
  { id: "decode-then-film", colour: false, passes: [{ pass: "srgb-decode" }, { pass: "film" }] },
  { id: "exposure-film-colour", colour: true, passes: [{ pass: "exposure", ev: 1 }, { pass: "film" }] },
  { id: "display-only", colour: true, passes: [{ pass: "dither", mode: "bayer8" }, { pass: "crt" }] },
  { id: "exposure-colour", colour: true, passes: [{ pass: "exposure", ev: 0.5 }, { pass: "srgb-decode" }] },
];
// Chains of passes with a C++ reference. The C++ executor runs the same plan on the host graph.
export const CHAINS = [
  { id: "one-exposure", colour: false, passes: [{ pass: "exposure", ev: 1 }] },
  { id: "decode-exposure-exposure", colour: false, passes: [{ pass: "srgb-decode" }, { pass: "exposure", ev: -1 }, { pass: "exposure", ev: 0.5 }] },
  { id: "exposure-decode", colour: false, passes: [{ pass: "exposure", ev: 2 }, { pass: "srgb-decode" }] },
];
const W = 160, H = 100, FRAMES = 3;

function plan(list) {
  try {
    const scene = planScenePasses(list.passes, list.colour).map((s) => s.pass);
    const display = list.passes.filter((s) => getPass(s.pass).stage === "display").map((s) => s.pass);
    return { refused: false, scene, display };
  } catch (e) { return { refused: true, why: String(e.message || e), scene: [], display: [] }; }
}

export function postStack(out) {
  const B = new Bundle("poststack");
  B.head.registry = registeredPasses().map((name) => { const p = getPass(name); return { name, stage: p.stage, history: p.history, input: p.input, output: p.output }; });
  B.head.lists = PLAN_LISTS.map((l) => ({ ...l, plan: plan(l) }));
  const src = labSource("street", W, H), input = Float32Array.from(src.data);
  B.add("input", "f32", input);
  B.head.width = W; B.head.height = H; B.head.frames = FRAMES;
  B.head.chains = [];
  for (const c of CHAINS) {
    const p = plan(c);
    if (p.refused || p.display.length) throw new Error(`fixtures_post: chain ${c.id} must be a plain scene chain`);
    const specs = planScenePasses(c.passes, c.colour);
    for (let f = 0; f < FRAMES; f++) {
      let cur = input;
      for (const spec of specs) cur = getPass(spec.pass).cpu(cur, W, H, spec);
      if (!(cur instanceof Float32Array)) throw new Error(`fixtures_post: ${c.id} did not end in a Float32Array`);
      B.add(`${c.id}.frame${f}`, "f32", cur);
    }
    B.head.chains.push({ id: c.id, colour: c.colour, passes: c.passes, plan: p.scene });
  }
  B.write(join(out, "poststack.rnfx"));
  return B.head.lists.length + B.head.chains.length;
}
