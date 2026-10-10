// The post stack: full-screen passes that a display list names in post.passes,
// run in order around the finish. The shader library (web/shaders/) registers
// the looks (CRT, film, dither and the rest); this file holds the contract, the
// registry and one small built-in pass.
//
//   registerPass("exposure", { stage: "scene", wgsl, params: (s) => [s.ev ?? 0] , cpu });
//   list.post = { passes: [{ pass: "exposure", ev: 1 }] };
//
// Stages:
//   "scene"    linear light, rgba16float, before bloom and the finish
//   "display"  after the finish, display-referred values in [0, 1]
// A pass's WGSL is appended to PASS_HEADER and defines
//   @fragment fn main(@builtin(position) q: vec4f) -> @location(0) vec4f
// reading src (this frame's input), prev (its own output last frame when it asks for
// history, else src again) and P (size: width, height, frame, time; p: 16 floats
// from params()). cpu(input, width, height, spec, prev) is the pass's CPU reference
// on Float32Array RGBA rows; tests/web/post_passes.py holds every pass with one to
// its GPU output within 1/255 at 8 bits.
//
// An effect is a pass with its own multi-pass graph (the shader library's CRT and film):
//   registerPass("crt", { stage: "display", effect: async (host, { width, height, spec }) => ({
//     input, output,        // GPUBuffers, one vec4f per pixel, rows from the top; output is width x height
//     inputSize,            // optional [w, h] of input (a CRT's source resolution); default width x height
//     record(enc, frame),   // record the effect into the frame's encoder (host.record(g, enc, false))
//     destroy() }), cpu, tests });
// The stack copies its texture into input, records the effect, and copies output back.
// Effects are created ahead of the frame (Motion.capture awaits them; a live draw skips
// an effect until it is ready).

export const PASS_HEADER = /* wgsl */ `
struct PostPass { size: vec4f, p: array<vec4f, 4>, }
@group(0) @binding(0) var<uniform> P: PostPass;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(0) @binding(2) var smp: sampler;
@group(0) @binding(3) var prev: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
  let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
  return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}
fn texel(q: vec4f) -> vec4f { return textureLoad(src, vec2i(q.xy), 0); }
`;

const registry = new Map();

// tests: the parameter specs the CPU comparison runs (default: one, with no parameters).
export function registerPass(name, { wgsl = null, effect = null, stage = "scene", history = false, params = () => [], cpu = null, tests = [{}] }) {
  if (!["scene", "display"].includes(stage)) throw new Error(`post pass ${name}: stage must be "scene" or "display"`);
  if (!wgsl === !effect) throw new Error(`post pass ${name}: give wgsl or effect`);
  registry.set(name, { name, wgsl, effect, stage, history, params, cpu, tests });
}
export const getPass = (name) => {
  const p = registry.get(name);
  if (!p) throw new Error(`post pass ${name} is not registered`);
  return p;
};
export const registeredPasses = () => [...registry.keys()];

// The 16 parameter floats of a pass spec.
export function passParams(pass, spec) {
  const v = Float32Array.from(pass.params(spec) || []);
  if (v.length > 16) throw new Error(`post pass ${pass.name}: at most 16 parameters`);
  const out = new Float32Array(16);
  out.set(v);
  return out;
}

// The built-in exposure pass: multiply linear light by 2^ev. It exists to exercise
// the stack and its tests; the looks live in the shader library.
registerPass("exposure", {
  stage: "scene",
  params: (s) => [s.ev ?? 0],
  tests: [{ ev: -1 }, { ev: 0.5 }, { ev: 2 }],
  wgsl: /* wgsl */ `
@fragment fn main(@builtin(position) q: vec4f) -> @location(0) vec4f {
  let c = texel(q);
  return vec4f(c.rgb * exp2(P.p[0].x), c.a);
}`,
  cpu: (input, w, h, spec) => {
    const k = 2 ** (spec.ev ?? 0), out = new Float32Array(input.length);
    for (let i = 0; i < input.length; i += 4) { out[i] = input[i] * k; out[i + 1] = input[i + 1] * k; out[i + 2] = input[i + 2] * k; out[i + 3] = input[i + 3]; }
    return out;
  },
});
