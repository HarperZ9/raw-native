// Radiance cascades on the GPU (WebGPU): the passes of cascades.mjs.
//   const rc = await createGpuCascades(host, scene, { branch: 2, n0: 4, r0: 1, fix: false });
//   rc.frame();  rc.read() -> Float32Array W*H*3 fluence;  await rc.time(50) -> GPU ms per frame
//   const R = await rc.reference({ rays: 4096, stride: 2, seed: 1, chunk: 64 }) -> Float32Array
import { planCascades, refGrid } from "./cascades.mjs";
import { packCircles } from "./scenes.mjs";
import { MIP_LEVELS } from "./trace.mjs";
import { PARAMS_WORDS, TRACE_WGSL, MERGE_WGSL, LEVEL_MAIN, FINAL_MAIN, BAND_MAIN, REF_MAIN, MIP_MAIN } from "./cascades.wgsl.mjs";

const WG = 256;

function mipLayout(W, H) {
  const w = [], off = []; let o = 0;
  for (let l = 0; l < MIP_LEVELS; l++) { const lw = Math.ceil(W / 2 ** l), lh = Math.ceil(H / 2 ** l); w.push(lw); off.push(o); o += lw * lh; }
  return { w, off, words: o };
}
// A 2D dispatch covering `total` invocations of workgroup size WG.
function grid2(total) { const groups = Math.ceil(total / WG), gx = Math.min(65535, groups); return { gx, gy: Math.ceil(groups / gx), gridX: gx * WG }; }

export function packParams(sc, plan, mips, f = {}) {
  const u = new Uint32Array(PARAMS_WORDS), fl = new Float32Array(u.buffer);
  const c = f.c || plan.cascades[0], cn = f.cn;
  u.set([sc.W, sc.H, sc.circles.length, plan.branch, c.n, c.pw, c.ph, c.s], 0);
  fl[8] = c.t0; fl[9] = c.t1; u[10] = cn ? 1 : 0; u[11] = plan.fix ? 1 : 0;
  if (cn) u.set([cn.n, cn.pw, cn.ph, cn.s], 12);
  u[16] = sc.circles.some((g) => g.kind === "glass") ? 1 : 0;
  u.set([f.rays0 || 0, f.rays1 || 0, f.raysTotal || 0, f.seed || 0, f.stride || 1, f.gw || 0, f.gh || 0, f.fresnel ? 1 : 0, f.total || 0, f.gridX || 0, f.level || 0], 17);
  if (plan.band) { u[28] = plan.band.N; fl[29] = plan.band.D; fl[30] = plan.band.S; }
  u.set(mips.w, 32); u.set(mips.off, 40);
  return u;
}

export async function createGpuCascades(host, sc, opts = {}) {
  const plan = planCascades(sc.W, sc.H, opts), mips = mipLayout(sc.W, sc.H), dev = host.device;
  const mk = (bytes, label, data = null, extra = []) => host.buffer({ size: bytes, usage: ["storage", "copy-dst", "copy-src", ...extra], label, data });
  const pipes = {
    level: await host.compute("rc.level", TRACE_WGSL + MERGE_WGSL + LEVEL_MAIN),
    final: await host.compute("rc.final", TRACE_WGSL + MERGE_WGSL + FINAL_MAIN),
    band: plan.band ? await host.compute("rc.band", TRACE_WGSL + MERGE_WGSL + BAND_MAIN) : null,
    ref: await host.compute("rc.reference", TRACE_WGSL + REF_MAIN),
    mip: await host.compute("rc.mip", MIP_MAIN),
  };
  const G = mk(sc.grid.byteLength, "rc grid", sc.grid), M = mk(4 * mips.words, "rc occupancy"), C = mk(32 * Math.max(1, sc.circles.length), "rc circles", packCircles(sc));
  let maxEntries = 1;
  for (const c of plan.cascades.slice(1)) maxEntries = Math.max(maxEntries, c.pw * c.ph * c.n);
  const A = mk(12 * maxEntries, "rc ping"), B = mk(12 * maxEntries, "rc pong"), F = mk(12 * sc.W * sc.H, "rc fluence");
  const owned = [G, M, C, A, B, F];
  // One list of dispatches per frame: the occupancy pyramid, then cascades top-down, then cascade 0.
  const steps = [];
  for (let l = 0; l < MIP_LEVELS; l++) {
    const P = mk(4 * PARAMS_WORDS, `rc mip ${l}`, packParams(sc, plan, mips, { level: l })); owned.push(P);
    steps.push([pipes.mip, [P, G, M], Math.ceil(mips.w[l] / 16), Math.ceil(Math.ceil(sc.H / 2 ** l) / 16)]);
  }
  let src = B, dst = A;
  const top = plan.cascades.length - 1;
  for (let i = top; i >= 1; i--) {
    const c = plan.cascades[i], cn = i < top ? plan.cascades[i + 1] : null, total = c.pw * c.ph * c.n, g = grid2(total);
    const P = mk(4 * PARAMS_WORDS, `rc cascade ${i}`, packParams(sc, plan, mips, { c, cn, total, gridX: g.gridX })); owned.push(P);
    steps.push([pipes.level, [P, G, M, C, src, dst], g.gx, g.gy]);
    [src, dst] = [dst, src];
  }
  {
    const c = plan.cascades[0], cn = top >= 1 ? plan.cascades[1] : null, total = sc.W * sc.H, g = grid2(total);
    const P = mk(4 * PARAMS_WORDS, "rc cascade 0", packParams(sc, plan, mips, { c, cn, total, gridX: g.gridX })); owned.push(P);
    steps.push([pipes.final, [P, G, M, C, src, F], g.gx, g.gy]);
    if (plan.band && cn) steps.push([pipes.band, [P, G, M, C, src, F], g.gx, g.gy]);
  }
  const canTime = dev.features.has("timestamp-query");
  const qs = canTime ? dev.createQuerySet({ type: "timestamp", count: 2 }) : null;
  const qb = canTime ? dev.createBuffer({ size: 16, usage: 0x200 | 0x04, label: "rc timestamps" }) : null; // QUERY_RESOLVE | COPY_SRC
  // One compute pass per step: every step reads what the step before wrote. Dispatches inside one
  // pass are ordered by the spec, but SwiftShader in CI raced them (2026-11-06), so pass boundaries
  // carry the dependency. The timestamps bracket the first and the last pass.
  function record(enc, timed = false) {
    steps.forEach(([pipe, bufs, x, y], i) => {
      const tw = timed && qs ? { querySet: qs, ...(i === 0 ? { beginningOfPassWriteIndex: 0 } : {}), ...(i === steps.length - 1 ? { endOfPassWriteIndex: 1 } : {}) } : null;
      const cp = enc.beginComputePass(tw && (i === 0 || i === steps.length - 1) ? { timestampWrites: tw } : {});
      cp.setPipeline(pipe); cp.setBindGroup(0, host.bind(pipe, bufs)); cp.dispatchWorkgroups(x, y); cp.end();
    });
    if (timed && qs) enc.resolveQuerySet(qs, 0, 2, qb, 0);
  }
  async function readBuf(buf, bytes) {
    const s = host.buffer({ size: bytes, usage: ["map-read", "copy-dst"] }), enc = dev.createCommandEncoder();
    enc.copyBufferToBuffer(buf, 0, s, 0, bytes); dev.queue.submit([enc.finish()]);
    await s.mapAsync(1); const o = s.getMappedRange().slice(0); s.unmap(); s.destroy(); return o;
  }
  return {
    plan, steps: steps.length, bytes: owned.reduce((a, b) => a + b.size, 0),
    frame() { const enc = dev.createCommandEncoder(); record(enc); dev.queue.submit([enc.finish()]); },
    async read() { return new Float32Array(await readBuf(F, 12 * sc.W * sc.H)); },
    // GPU time per frame from timestamps (null without the feature) and wall time submit to done.
    async time(frames = 50) {
      const gpu = [], wall = [];
      for (let f = 0; f < frames; f++) {
        const enc = dev.createCommandEncoder(); record(enc, true);
        const t = performance.now(); dev.queue.submit([enc.finish()]); await dev.queue.onSubmittedWorkDone(); wall.push(performance.now() - t);
        if (qb) { const v = new BigUint64Array(await readBuf(qb, 16)); gpu.push(Number(v[1] - v[0]) / 1e6); }
      }
      return { gpu_ms: qb ? gpu : null, wall_ms: wall };
    },
    async reference({ rays = 4096, stride = 1, seed = 1, chunk = 64, fresnel = true } = {}) {
      const g = refGrid(sc.W, sc.H, stride), total = g.w * g.h, d = grid2(total), R = mk(12 * total, "rc reference");
      dev.queue.writeBuffer(R, 0, new Float32Array(3 * total));
      { const enc = dev.createCommandEncoder(); // the occupancy pyramid, one pass per level
        for (const [pipe, bufs, x, y] of steps.slice(0, MIP_LEVELS)) { const cp = enc.beginComputePass(); cp.setPipeline(pipe); cp.setBindGroup(0, host.bind(pipe, bufs)); cp.dispatchWorkgroups(x, y); cp.end(); }
        dev.queue.submit([enc.finish()]); }
      const t = performance.now();
      for (let r0 = 0; r0 < rays; r0 += chunk) {
        const P = mk(4 * PARAMS_WORDS, "rc reference params", packParams(sc, plan, mips, { rays0: r0, rays1: Math.min(rays, r0 + chunk), raysTotal: rays, seed, stride, gw: g.w, gh: g.h, fresnel, total, gridX: d.gridX }));
        const enc = dev.createCommandEncoder(), cp = enc.beginComputePass();
        cp.setPipeline(pipes.ref); cp.setBindGroup(0, host.bind(pipes.ref, [P, G, M, C, R])); cp.dispatchWorkgroups(d.gx, d.gy); cp.end();
        dev.queue.submit([enc.finish()]); await dev.queue.onSubmittedWorkDone(); P.destroy();
      }
      const ms = performance.now() - t, out = new Float32Array(await readBuf(R, 12 * total)); R.destroy();
      const w = 2 * Math.PI / rays; for (let i = 0; i < out.length; i++) out[i] *= w;
      return { fluence: out, grid: g, ms };
    },
    destroy() { for (const b of owned) b.destroy(); if (qs) qs.destroy(); if (qb) qb.destroy(); },
  };
}
