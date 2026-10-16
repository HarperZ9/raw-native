// GPU layers for Motion's display list: the atmosphere and the orb (one full-frame pipeline),
// point clouds with level of detail, and flow along curves. Each draws into Motion's linear HDR
// target inside its main pass, through the renderer's "layer" run kind:
//
//   const kit = await createLayers(motion);
//   items.push({ kind: "layer", layer: kit.field, mode: "atmos", spec: NIGHT, opacity: 1, time: t });
//   items.push({ kind: "layer", layer: kit.field, mode: "orb", x: 960, y: 540, radius: 220, spec: { ... } });
//   const cloud = kit.points(Float32Array /* x, y, value, aux per point */);
//   items.push({ kind: "layer", layer: cloud, range: [-3, 3], ramp: [...], radius: 1.2, opacity: 1, sel: -1 });
//   const flow = kit.flow([{ c: [x0, y0, x1, y1, x2, y2, x3, y3], weight: 0.8, phase: 0 }, ...]);
//   items.push({ kind: "layer", layer: flow, reveal: 1, phase: 0.3, width: 3, opacity: 1 });
// A layer object may appear several times in one frame (uniform slots ring per frame).
import { project } from "../motion/vector.mjs";
import { FIELD_WGSL, POINTS_WGSL, FLOW_WGSL } from "./wgsl.mjs";
import { orbFloats, atmosFloats, rampFloats } from "./pack.mjs";
import { lodFraction } from "./ref.mjs";

const HDR = "rgba16float";
const OVER = { color: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" }, alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" } };
export const INK_RAMP = [[0, [0.05, 0.05, 0.06]], [0.5, [0.35, 0.33, 0.30]], [0.85, [0.85, 0.80, 0.70]], [1, [1.4, 1.3, 1.1]]];

class Slots {
  constructor(host, bytes, label) { this.host = host; this.bytes = bytes; this.label = label; this.list = []; this.key = -1; this.n = 0; }
  next(frameKey) {
    if (frameKey !== this.key) { this.key = frameKey; this.n = 0; }
    if (this.n === this.list.length) this.list.push(this.host.buffer({ size: this.bytes, usage: ["uniform", "copy-dst"], label: this.label }));
    return this.list[this.n++];
  }
  destroy() { for (const b of this.list) b.destroy(); }
}

async function pipeline(device, code, label, fs = "fs") {
  const module = device.createShaderModule({ code, label });
  return device.createRenderPipelineAsync({ label, layout: "auto", primitive: { topology: "triangle-strip" },
    vertex: { module, entryPoint: "vs" }, fragment: { module, entryPoint: fs, targets: [{ format: HDR, blend: OVER }] } });
}

class Field {
  constructor(host, pipes) { this.host = host; this.pipes = pipes; this.slots = new Slots(host, 256, "explainer field"); }
  draw(rp, item, f) {
    const u = this.slots.next(f.frameKey), a = atmosFloats(item.spec || {});
    let place = [0, 0, 1, item.opacity ?? 1];
    if (item.mode === "orb") {
      const P = project({ z: item.z || 0, screen: item.screen }, f.cam, f.W, f.H, f.design);
      if (!P) return;
      place = [item.x * P.k + P.ox, item.y * P.k + P.oy, Math.max(1, item.radius * P.k), item.opacity ?? 1];
    }
    const data = new Float32Array(64);
    data.set(orbFloats(item.spec || {}), 0);
    data.set(a.floats, 24);
    data.set(a.bandC, 52);
    data.set(place, 56);
    data.set([item.time ?? f.time, 0, 0, 0], 60);
    this.host.write(u, data);
    const pipe = item.mode === "orb" ? this.pipes.orb : this.pipes.atmos;
    rp.setPipeline(pipe);
    rp.setBindGroup(0, this.host.bind(pipe, [f.view, u]));
    rp.draw(3);
  }
  destroy() { this.slots.destroy(); }
}

// A point cloud: Float32Array of [x, y, value, aux] per point, in scene units.
class Points {
  constructor(host, pipe, data) {
    this.host = host; this.pipe = pipe; this.count = data.length / 4;
    this.buf = host.buffer({ size: Math.max(16, data.byteLength), usage: ["storage", "copy-dst"], label: "explainer points" });
    host.write(this.buf, data);
    this.slots = new Slots(host, 128, "explainer points u");
    this.last = null;
  }
  // The LOD a frame used: { keep, gain, drawn } (read by tests and the inspector).
  draw(rp, item, f) {
    const P = project({ z: item.z || 0, screen: item.screen }, f.cam, f.W, f.H, f.design);
    if (!P) return;
    const covered = (item.area ?? 1) * P.k * P.k;            // item.area: the cloud's area in scene units^2
    const lod = item.keep !== undefined ? { keep: item.keep, gain: 1 / item.keep } : lodFraction(this.count, covered, item.density ?? 1.5);
    const gain = Math.min(item.maxGain ?? 8, lod.gain) * (item.gainScale ?? 1);
    const r = rampFloats(item.ramp || INK_RAMP), range = item.range || [0, 1];
    const u = this.slots.next(f.frameKey);
    this.host.write(u, new Float32Array([
      P.k, P.ox, P.oy, (item.radius ?? 1) * P.k,
      lod.keep, gain, this.count, item.sel ?? -1,
      range[0], range[1], item.opacity ?? 1, item.selGain ?? 4,
      ...r.xs, ...r.cs]));
    this.last = { keep: lod.keep, gain, drawn: Math.round(lod.keep * this.count) };
    rp.setPipeline(this.pipe);
    rp.setBindGroup(0, this.host.bind(this.pipe, [f.view, u, this.buf]));
    rp.draw(4, this.count);
  }
  destroy() { this.buf.destroy(); this.slots.destroy(); }
}

// Curves: [{ c: [8 numbers], weight, phase }]. weight 0 hides a curve.
class Flow {
  constructor(host, pipe, curves, samples = 96) {
    this.host = host; this.pipe = pipe; this.samples = samples; this.n = curves.length;
    const d = new Float32Array(Math.max(1, curves.length) * 12);
    curves.forEach((cv, i) => { d.set(cv.c, i * 12); d.set([cv.weight ?? 1, cv.phase ?? 0, 0, 0], i * 12 + 8); });
    this.buf = host.buffer({ size: d.byteLength, usage: ["storage", "copy-dst"], label: "explainer flow" });
    host.write(this.buf, d);
    this.slots = new Slots(host, 64, "explainer flow u");
  }
  draw(rp, item, f) {
    if (!this.n) return;
    const P = project({ z: item.z || 0, screen: item.screen }, f.cam, f.W, f.H, f.design);
    if (!P) return;
    const cold = item.cold || [0.55, 0.12, 0.35], hot = item.hot || [1.6, 0.75, 0.30];
    const u = this.slots.next(f.frameKey);
    this.host.write(u, new Float32Array([
      P.k, P.ox, P.oy, (item.width ?? 2) * P.k,
      this.samples, item.reveal ?? 1, item.phase ?? 0, item.pulse ?? 0.07,
      cold[0], cold[1], cold[2], item.base ?? 0.35,
      hot[0], hot[1], hot[2], item.opacity ?? 1]));
    rp.setPipeline(this.pipe);
    rp.setBindGroup(0, this.host.bind(this.pipe, [f.view, u, this.buf]));
    rp.draw(4, this.n * this.samples);
  }
  destroy() { this.buf.destroy(); this.slots.destroy(); }
}

export async function createLayers(motion) {
  const host = motion.host, d = host.device;
  const [orb, atmos, pts, flow] = await Promise.all([
    pipeline(d, FIELD_WGSL, "explainer orb", "fs_orb"), pipeline(d, FIELD_WGSL, "explainer atmosphere", "fs_atmos"),
    pipeline(d, POINTS_WGSL, "explainer points"), pipeline(d, FLOW_WGSL, "explainer flow")]);
  return {
    field: new Field(host, { orb, atmos }),
    points: (data) => new Points(host, pts, data),
    flow: (curves, samples) => new Flow(host, flow, curves, samples),
  };
}
