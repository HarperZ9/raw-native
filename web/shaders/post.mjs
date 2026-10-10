// The shader library in the Motion post stack (web/motion/post.mjs): importing this
// module registers its looks as passes a display list can name in post.passes.
//
//   "crt"   display stage: the finished frame is the tube's R'G'B' signal at the source
//           resolution spec.source ([320, 240] by default, the frame box-filtered down);
//           the tube is drawn at the frame's size and encoded to display values.
//           spec: { preset: "pvm-20" | ..., source: [w, h], exposure, ...overrides }
//   "film"  scene stage: scene-linear light in, the print out as display values (the film
//           is its own tone mapper, so use it without post.colour).
//           spec: { preset: "500t-print" | ..., ev, ...overrides }
// Each pass carries the library's CPU reference, run the way the stack feeds the GPU, so
// tests/web/post_passes.py holds the GPU to it.
import { registerPass } from "../motion/post.mjs";
import { Access } from "../frame-graph.mjs";
import { createGpuCrt } from "./crt/gpu.mjs";
import { createCrt } from "./crt/crt.mjs";
import { shoulder } from "./crt/glass.mjs";
import { createGpuFilm } from "./film/gpu.mjs";
import { createFilm } from "./film/run.mjs";

const srgb = (l) => (l <= 0.0031308 ? l * 12.92 : 1.055 * Math.pow(l, 1 / 2.4) - 0.055);
const SPLIT = (spec, keys) => Object.fromEntries(Object.entries(spec).filter(([k]) => k !== "pass" && !keys.includes(k)));

// Encode a linear vec4 image buffer to display values in another buffer, after the effect.
const ENCODE_WGSL = (tube) => /* wgsl */ `
@group(0) @binding(0) var<storage, read> img: array<vec4f>;
@group(0) @binding(1) var<storage, read_write> out: array<vec4f>;
@group(0) @binding(2) var<uniform> k: vec4f;   // exposure
fn shoulder(x: f32) -> f32 { let s = 0.8; if (x <= s) { return x; } return s + (1.0 - s) * tanh((x - s) / (1.0 - s)); }
fn enc(v: f32) -> f32 {
  let l = ${tube ? "shoulder(max(0.0, v * k.x))" : "clamp(v * k.x, 0.0, 1.0)"};
  if (l <= 0.0031308) { return l * 12.92; }
  return 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) g: vec3u) {
  if (g.x >= arrayLength(&out)) { return; }
  let c = img[g.x];
  out[g.x] = vec4f(enc(c.x), enc(c.y), enc(c.z), 1.0);
}`;

async function encoder(host, tube, n, img, exposure) {
  const out = host.buffer({ size: 16 * n, usage: ["storage", "copy-src", "copy-dst"], label: "shader post out" });
  const k = host.buffer({ size: 16, usage: ["uniform", "copy-dst"], label: "shader post exposure" });
  host.write(k, new Float32Array([exposure, 0, 0, 0]));
  const pipe = await host.compute(tube ? "crt encode" : "film encode", ENCODE_WGSL(tube));
  const g = host.graph(), ri = g.importBuffer("img", img), ro = g.importBuffer("out", out), rk = g.importBuffer("k", k);
  g.addPass("encode", [[ri, Access.StorageRead], [rk, Access.StorageRead], [ro, Access.StorageWrite]], (c) => c.dispatch(pipe, [img, out, k], Math.ceil(n / 64)));
  g.markOutput(ro); g.compile();
  return { out, record: (enc) => host.record(g, enc, false), destroy() { out.destroy(); k.destroy(); } };
}

// The stack's box filter (post_run.mjs TO_BUF), on the CPU: input pixel (x, y) is the
// mean of the frame pixels from floor(x * W / w) to max(that + 1, floor((x + 1) * W / w)).
function boxDown(input, W, H, w, h) {
  const out = new Float32Array(w * h * 4);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const ax = Math.floor((x * W) / w), ay = Math.floor((y * H) / h);
    const bx = Math.max(ax + 1, Math.floor(((x + 1) * W) / w)), by = Math.max(ay + 1, Math.floor(((y + 1) * H) / h));
    for (let k = 0; k < 4; k++) {
      let s = 0;
      for (let yy = ay; yy < by; yy++) for (let xx = ax; xx < bx; xx++) s += input[4 * (yy * W + xx) + k];
      out[4 * (y * w + x) + k] = s / ((bx - ax) * (by - ay));
    }
  }
  return out;
}

// CPU references keep their state (persistence, frame number) per test spec.
const cpuState = new WeakMap();

registerPass("crt", {
  stage: "display",
  tests: [{ preset: "pvm-20", source: [320, 200] }],
  effect: async (host, { width, height, spec }) => {
    const [sw, sh] = spec.source || [320, 240], exposure = spec.exposure ?? 1;
    const crt = await createGpuCrt(host, spec.preset || "pvm-20", SPLIT(spec, ["preset", "source", "exposure"]), { w: sw, h: sh }, { w: width, h: height }, { exposure });
    const e = await encoder(host, true, width * height, crt.image, exposure);
    return { input: crt.buffers.src, inputSize: [sw, sh], output: e.out,
      record(enc, f) { crt.record(enc, f); e.record(enc); }, destroy() { crt.destroy(); e.destroy(); } };
  },
  cpu: (input, W, H, spec) => {
    const [sw, sh] = spec.source || [320, 240], exposure = spec.exposure ?? 1;
    let st = cpuState.get(spec);
    if (!st) cpuState.set(spec, (st = { crt: createCrt(spec.preset || "pvm-20", SPLIT(spec, ["preset", "source", "exposure"]), { w: sw, h: sh }, { w: W, h: H }), f: 0 }));
    const { img } = st.crt.frame({ width: sw, height: sh, data: boxDown(input, W, H, sw, sh) }, st.f++);
    const out = new Float32Array(W * H * 4);
    for (let i = 0; i < out.length; i += 4) {
      for (let c = 0; c < 3; c++) out[i + c] = srgb(shoulder(Math.max(0, img[i + c] * exposure)));
      out[i + 3] = 1;
    }
    return out;
  },
});

registerPass("film", {
  stage: "scene",
  tests: [{ preset: "500t-print" }],
  effect: async (host, { width, height, spec }) => {
    const film = await createGpuFilm(host, spec.preset || "500t-print", SPLIT(spec, ["preset"]), { w: width, h: height }, { w: width, h: height });
    const e = await encoder(host, false, width * height, film.image, 1);
    return { input: film.buffers.scene, output: e.out,
      record(enc, f) { film.record(enc, f); e.record(enc); }, destroy() { film.destroy(); e.destroy(); } };
  },
  cpu: (input, W, H, spec) => {
    let st = cpuState.get(spec);
    if (!st) cpuState.set(spec, (st = { film: createFilm(spec.preset || "500t-print", SPLIT(spec, ["preset"]), { w: W, h: H }, { w: W, h: H }), f: 0 }));
    const { img } = st.film.frame({ width: W, height: H, data: input }, st.f++);
    const out = new Float32Array(W * H * 4);
    for (let i = 0; i < out.length; i += 4) {
      for (let c = 0; c < 3; c++) out[i + c] = srgb(Math.min(1, Math.max(0, img[i + c])));
      out[i + 3] = 1;
    }
    return out;
  },
});
