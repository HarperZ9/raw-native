// The painterly shader on the GPU: the passes of paint.mjs through the host's frame graph.
//   const p = await createGpuPaint(host, "oil-grotesque", { canvasOffset: [panX, panY] }, { w, h });
//   p.frame(sceneLinearRGBA /* Float32Array */);   p.packed: sRGB RGBA8;  await p.read()
import { Access } from "../../frame-graph.mjs";
import { COMMON_WGSL } from "../common.mjs";
import { resolvePaint } from "./paint.mjs";
import { K_TAB, S_TAB, W_TAB, latentLut } from "./pigments.mjs";
import { PAINT_COMMON_WGSL, PAINT_INDEX, PAINT_FIELDS } from "./common.wgsl.mjs";
import { PREP_WGSL, TENSOR_WGSL, BLUR4_WGSL, AKF_WGSL, STROKE_WGSL } from "./passes.wgsl.mjs";
import { COMPOSE_WGSL, RELIEF_WGSL, PAINT_ENCODE_WGSL } from "./compose.wgsl.mjs";

const head = COMMON_WGSL + "\n" + PAINT_COMMON_WGSL + "\n";
export const PAINT_PASSES = { prep: PREP_WGSL, tensor: TENSOR_WGSL, blur: BLUR4_WGSL, akf: AKF_WGSL, stroke: STROKE_WGSL, relief: RELIEF_WGSL, compose: COMPOSE_WGSL, encode8: PAINT_ENCODE_WGSL };
const MEDIA = { oil: 0, matte: 1, water: 2 };

export function packPaintParams(plan, dir = 0) {
  const p = plan.p, a = new Float32Array(PAINT_FIELDS.length), N = 8;
  const v = { w: plan.out.w, h: plan.out.h, exposure: p.exposure, radius: p.radius, q: p.q, zeta: p.zeta, eta: (p.zeta + Math.cos(Math.PI / N)) / Math.sin(Math.PI / N) ** 2,
    strokeLen: p.strokeLen, ox: p.canvasOffset[0], oy: p.canvasOffset[1], medium: MEDIA[p.medium], impasto: p.impasto, gloss: p.gloss, push: p.push, broken: p.broken,
    valueBands: p.valueBands, lines: p.lines, lineSigma: p.lineSigma, lineSharp: p.lineSharp, warp: p.warp, granulation: p.granulation, edge: p.edge, dilution: p.dilution, dir };
  for (const [k, i] of Object.entries(PAINT_INDEX)) a[i] = v[k];
  return a;
}
export function packPigments() {
  return Float32Array.from([...K_TAB.flat(), ...S_TAB.flat(), ...W_TAB.flat(), ...latentLut()]);
}

export async function createGpuPaint(host, preset, overrides, size) {
  const plan = resolvePaint(preset, overrides, size), { w, h } = size, n = w * h, pipes = {};
  await Promise.all(Object.entries(PAINT_PASSES).map(async ([k, code]) => { pipes[k] = await host.compute("paint." + k, head + code); }));
  const st = (bytes, label, data) => host.buffer({ size: bytes, usage: ["storage", "copy-dst", "copy-src"], label, data });
  const pig = packPigments();
  const B = {
    P: st(4 * PAINT_FIELDS.length, "paint params"), Px: st(4 * PAINT_FIELDS.length, "paint params x"), Py: st(4 * PAINT_FIELDS.length, "paint params y"),
    pig: st(pig.byteLength, "paint pigments", pig), tw: st(plan.tw.byteLength, "paint tensor weights", plan.tw),
    scene: st(16 * n, "paint scene"), s: st(16 * n, "paint s"), lat: st(32 * n, "paint latent"), T0: st(16 * n, "paint tensor"), T1: st(16 * n, "paint tensor tmp"),
    olat: st(32 * n, "paint akf latent"), os: st(16 * n, "paint akf s"), H: st(8 * n, "paint stroke"), Rl: st(16 * n, "paint relief"), img: st(16 * n, "paint image"), out8: st(4 * n, "paint packed"),
  };
  const g = host.graph(), r = {};
  for (const k of Object.keys(B)) r[k] = g.importBuffer(k, B[k]);
  const R = Access.StorageRead, Wr = Access.StorageWrite, wg = (x) => Math.ceil(x / 8);
  const pass = (name, pipe, uses, list) => g.addPass(name, uses, (c) => c.dispatch(pipe, list, wg(w), wg(h)));
  pass("prep", pipes.prep, [[r.P, R], [r.pig, R], [r.scene, R], [r.s, Wr], [r.lat, Wr]], [B.P, B.pig, B.scene, B.s, B.lat]);
  pass("tensor", pipes.tensor, [[r.P, R], [r.pig, R], [r.s, R], [r.T0, Wr]], [B.P, B.pig, B.s, B.T0]);
  pass("tensor.x", pipes.blur, [[r.Px, R], [r.pig, R], [r.tw, R], [r.T0, R], [r.T1, Wr]], [B.Px, B.pig, B.tw, B.T0, B.T1]);
  pass("tensor.y", pipes.blur, [[r.Py, R], [r.pig, R], [r.tw, R], [r.T1, R], [r.T0, Wr]], [B.Py, B.pig, B.tw, B.T1, B.T0]);
  pass("akf", pipes.akf, [[r.P, R], [r.pig, R], [r.s, R], [r.lat, R], [r.T0, R], [r.olat, Wr], [r.os, Wr]], [B.P, B.pig, B.s, B.lat, B.T0, B.olat, B.os]);
  pass("stroke", pipes.stroke, [[r.P, R], [r.pig, R], [r.T0, R], [r.H, Wr]], [B.P, B.pig, B.T0, B.H]);
  pass("relief", pipes.relief, [[r.P, R], [r.pig, R], [r.os, R], [r.H, R], [r.T0, R], [r.Rl, Wr]], [B.P, B.pig, B.os, B.H, B.T0, B.Rl]);
  pass("compose", pipes.compose, [[r.P, R], [r.pig, R], [r.olat, R], [r.os, R], [r.H, R], [r.Rl, R], [r.img, Wr]], [B.P, B.pig, B.olat, B.os, B.H, B.Rl, B.img]);
  pass("encode8", pipes.encode8, [[r.P, R], [r.pig, R], [r.img, R], [r.out8, Wr]], [B.P, B.pig, B.img, B.out8]);
  g.markOutput(r.out8); g.markOutput(r.img); g.compile();
  return {
    plan, packed: B.out8, image: B.img,
    frame(sceneRGBA, offset = null) {
      if (offset) plan.p.canvasOffset = offset;
      host.write(B.P, packPaintParams(plan, 0)); host.write(B.Px, packPaintParams(plan, 0)); host.write(B.Py, packPaintParams(plan, 1));
      if (sceneRGBA) host.write(B.scene, sceneRGBA);
      host.frame(g);
    },
    async read() {
      const rd = async (buf, bytes) => {
        const s = host.buffer({ size: bytes, usage: ["map-read", "copy-dst"] }), enc = host.device.createCommandEncoder();
        enc.copyBufferToBuffer(buf, 0, s, 0, bytes); host.device.queue.submit([enc.finish()]);
        await s.mapAsync(1); const o = s.getMappedRange().slice(0); s.unmap(); s.destroy(); return o;
      };
      return { image: new Float32Array(await rd(B.img, 16 * n)), packed: new Uint8Array(await rd(B.out8, 4 * n)) };
    },
    destroy() { for (const b of Object.values(B)) b.destroy(); },
  };
}
