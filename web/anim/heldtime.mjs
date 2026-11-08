// The held-time clock, the JavaScript mirror of raw/scene/held_time.hpp (PROPOSALS P1;
// evidence/heldtime-loop-bounds.json). Pure: runs in Node and the browser.
//
// A layer samples at an authored rate independent of the render rate: on ones, twos or threes of a
// 24 fps film base, a named rate such as 12 Hz, or continuous (every render frame, the camera's
// default). Render frame k at fps R shows tick floor(k * R.den * rate.num / (R.num * rate.den)), in
// integers, so 30, 60 and 144 fps show the same ticks; the held time is tick / rate. A layer's boil
// seed hashes its seed with its pose buffer's FNV-1a, so it changes only when the pose changes.
//   const L = layout(clip, lengths);                                    // clip.mjs, frames at clipFps
//   const f = heldClipFrame({ rate: onN(2) }, k, { num: 60, den: 1 }, 24);
//   sampleClip(clip, L, f)                                              // the pose, held on twos
import { sampleClip } from "./clip.mjs";

export const continuous = () => ({ kind: "continuous", num: 0, den: 1 });
export const onN = (n, base = { num: 24, den: 1 }) => {
  if (!(n >= 1) || !Number.isInteger(n)) throw new Error("heldtime: on N needs a whole N >= 1");
  return { kind: "held", num: base.num, den: base.den * n };
};
export const named = (num, den = 1) => {
  if (!(num > 0 && den > 0)) throw new Error("heldtime: rate must be positive");
  return { kind: "held", num, den };
};

const fpsOf = (fps) => (typeof fps === "number" ? { num: fps, den: 1 } : fps);

// { tick, time } of a layer at render frame k.
export function sampleHeld(layer, k, fps) {
  const R = fpsOf(fps), r = layer.rate;
  if (!(R.num > 0 && R.den > 0)) throw new Error("heldtime: fps must be positive");
  if (r.kind === "continuous") return { tick: k, time: (k * R.den) / R.num };
  const tick = Math.floor((k * R.den * r.num) / (R.num * r.den));
  return { tick, time: (tick * r.den) / r.num };
}

// The first render frame showing tick n: ceil(n * fps / rate).
export function onsetFrame(rate, n, fps) {
  const R = fpsOf(fps);
  return rate.kind === "continuous" ? n : Math.ceil((n * R.num * rate.den) / (R.den * rate.num));
}

// The clip frame (clip.mjs units at clipFps) a layer shows at render frame k.
export const heldClipFrame = (layer, k, fps, clipFps) => sampleHeld(layer, k, fps).time * clipFps;
export const sampleHeldClip = (clip, L, layer, k, fps, clipFps, base) => sampleClip(clip, L, heldClipFrame(layer, k, fps, clipFps), base);

// FNV-1a 64 over bytes, as a BigInt.
export function fnv64(bytes) {
  let h = 0xcbf29ce484222325n;
  for (const b of bytes) { h ^= BigInt(b); h = (h * 0x100000001b3n) & 0xffffffffffffffffn; }
  return h;
}
const mix = (x) => {
  x >>>= 0; x ^= x >>> 16; x = Math.imul(x, 0x7feb352d) >>> 0; x ^= x >>> 15; x = Math.imul(x, 0x846ca68b) >>> 0; x ^= x >>> 16;
  return x >>> 0;
};
export function boilSeed(layerSeed, poseHash) {
  const lo = Number(poseHash & 0xffffffffn), hi = Number(poseHash >> 32n);
  return mix((lo ^ mix(hi ^ mix(layerSeed))) >>> 0);
}

// Held ratio over per-frame pose keys (strings or numbers); frame 0 counts as a change.
export function heldRatio(keys) {
  let changes = 0;
  keys.forEach((k, i) => { if (i === 0 || k !== keys[i - 1]) changes++; });
  return keys.length ? (keys.length - changes) / keys.length : 0;
}
