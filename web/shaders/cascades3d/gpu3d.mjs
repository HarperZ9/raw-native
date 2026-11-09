// The 3D radiance cascades on the GPU (WebGPU): the passes of run3d.mjs.
//   const rc = await createGpuCascades3d(host, sc, S, plan, W, H, { cacheInit });
//   rc.run(plan.frames); const img = await rc.read();       // Float32Array W*H*4 (rgb, hit)
//   const t = await rc.time(50);   // per frame: GI ms (flags, levels, cache, screen probes, final) and per pass
//   await rc.baseline(16);          // the no-GI baseline into the same output
// Primary hits (gbuffer) and depth moments are set up once; every frame recomputes the probe flags
// (as dynamic geometry would need), the levels top-down, the cache, the screen probes and the final.
import { PARAMS_WORDS, FRAME_WORDS } from "./wgsl3d-core.mjs";
import { GBUF, FLAGS, DEPTH, LEVEL, CACHE, SVIS, SRAYS, SSUM, FINAL, BASELINE } from "./wgsl3d-passes.mjs";
import { START } from "./wgsl3d-world.mjs";
import { ATROUS, ACCUM } from "./wgsl3d-denoise.mjs";
import { cameraBasis } from "./scene3d.mjs";
import { probeGrid } from "./run3d.mjs";
import { frameSeed, probeOffsets, cameraAt } from "./denoise3d.mjs";
import { phasePlan } from "./world.mjs";

const WGS = { gbuf: 256, flags: 256, depth: 256, start: 256, level: 256, cache: 256, svis: 256, srays: 64, ssum: 256, atrous: 256, final: 256, accum: 256, baseline: 64 };
const CODE = { gbuf: GBUF, flags: FLAGS, depth: DEPTH, start: START, level: LEVEL, cache: CACHE, svis: SVIS, srays: SRAYS, ssum: SSUM, atrous: ATROUS, final: FINAL, accum: ACCUM, baseline: BASELINE };

function grid2(total, wg) { const groups = Math.max(1, Math.ceil(total / wg)), gx = Math.min(65535, groups); return { gx, gy: Math.ceil(groups / gx), gridX: gx * wg }; }

// The scene buffer: nodes (2 vec4 each), then 8 vec4 a triangle.
export function packScene(S) {
  const nn = S.count, nt = S.alb.length / 3, f = new Float32Array(8 * nn + 32 * nt);
  f.set(S.nodes, 0);
  for (let s = 0, o = 8 * nn; s < nt; s++, o += 32) {
    f.set(S.tris.subarray(9 * s, 9 * s + 3), o); f.set(S.tris.subarray(9 * s + 3, 9 * s + 6), o + 4); f.set(S.tris.subarray(9 * s + 6, 9 * s + 9), o + 8);
    f.set(S.nrm.subarray(9 * s, 9 * s + 3), o + 12); f.set(S.nrm.subarray(9 * s + 3, 9 * s + 6), o + 16); f.set(S.nrm.subarray(9 * s + 6, 9 * s + 9), o + 20);
    f.set(S.alb.subarray(3 * s, 3 * s + 3), o + 24); f.set(S.emit.subarray(3 * s, 3 * s + 3), o + 28);
  }
  return { data: f, triOff: 2 * nn };
}

// Words 32 to 71 of frame f: its camera, the denoise words (seed, probe offsets, flags, tmax; sl and the
// planted edge) and the previous frame's camera. planted = { x, a, b } sets every probe to a below x
// and b above, and makes the frame the irradiance (the planted check of the denoise).
export function frameWords(sc, plan, W, H, f, planted = null) {
  const buf = new ArrayBuffer(4 * (FRAME_WORDS[1] - FRAME_WORDS[0])), u = new Uint32Array(buf), fl = new Float32Array(buf);
  const cam = (c) => { const k = cameraBasis(c, W, H); return [...k.eye, k.th, ...k.f, k.aspect, ...k.s, 0, ...k.u, 0]; };
  const [ox, oy] = plan.stride ? probeOffsets(plan, f) : [0, 0], t = plan.tf > 0;
  fl.set(cam(cameraAt(sc, plan, f)), 0);
  u.set([frameSeed(plan, f), ox | (oy << 16), (t && f >= plan.warm ? 1 : 0) | (planted ? 2 : 0) | (t || planted ? 4 : 0) | (t && planted ? 8 : 0), plan.tmax || 1], 16);
  fl.set([plan.sl || 0, planted ? planted.x : 0, planted ? planted.a : 0, planted ? planted.b : 0], 20);
  fl.set(cam(cameraAt(sc, plan, Math.max(0, f - 1))), 24);
  return new Uint8Array(buf);
}

// x.clo: the grid origin of the cache the pass gathers from (the previous phase's for the levels
// under jit); plan.lo otherwise.
export function packParams(sc, plan, W, H, triOff, x = {}) {
  const L = plan.levels, omOff = [], flagOff = [], svOff = [];
  let om = 0, fl = 0, sv = 0;
  for (const l of L) { omOff.push(om); om += l.c * l.c; flagOff.push(fl); fl += l.probes; svOff.push(sv); sv += l.probes * l.texels; }
  const words = PARAMS_WORDS + om, buf = new ArrayBuffer(4 * words), u = new Uint32Array(buf), f = new Float32Array(buf);
  const g = plan.stride ? probeGrid(W, H, plan.stride) : { pw: 0, ph: 0, off: 0 }, k = cameraBasis(sc.camera, W, H);
  const fix = { none: 0, probe: 1, full: 2 }[plan.fix];
  u.set([W, H, L.length, x.level || 0, fix, plan.stride, x.m || plan.m, plan.cd, x.total || 0, x.gridX || 0, triOff, 6 * plan.cd * plan.cd, g.pw, g.ph, 1, g.off], 0);
  const fin = (v) => (v === Infinity ? 1e30 : v);
  f.set([...plan.lo, plan.h0, ...sc.sunDir, Math.max(...sc.sunE) > 0 ? 1 : 0, ...sc.sunE, plan.dmax, ...sc.sky, 0], 16);
  f.set([...k.eye, k.th, ...k.f, k.aspect, ...k.s, 0, ...k.u, 0], 32);
  new Uint8Array(buf).set(frameWords(sc, plan, W, H, 0, x.planted), 4 * FRAME_WORDS[0]);
  f.set([...(x.clo || plan.lo), 0], 72);
  u.set([{ seg: 0, vis: 1 }[plan.mw || "seg"], plan.sv || 0, (plan.fz ? 1 : 0) | (plan.sd ? 2 : 0) | (plan.pi ? 4 : 0) | ((plan.fd || 0) << 3) | (plan.sk ? 32 : 0) | (plan.sx ? 64 : 0), (plan.ft ? 1 : 0) | ((plan.cg || 0) << 1)], 76);
  L.forEach((l, i) => {
    u.set([...l.n, l.probes], 80 + 4 * i);
    f.set([l.h, l.c, fin(l.T0), fin(l.T1x)], 112 + 4 * i);
    u.set([omOff[i], flagOff[i], l.texels, svOff[i]], 144 + 4 * i);
  });
  plan.omega.forEach((t, i) => f.set(t, PARAMS_WORDS + omOff[i]));
  return { data: new Uint8Array(buf), flagWords: fl, svWords: sv };
}

export async function createGpuCascades3d(host, sc, S, plan, W, H, { cacheInit = null, staticFlags = false, planted = null } = {}) {
  const dev = host.device, L = plan.levels, owned = [];
  const mk = (bytes, label, data = null) => { const b = host.buffer({ size: Math.max(16, bytes), usage: ["storage", "copy-dst", "copy-src"], label, data }); owned.push(b); return b; };
  const pipes = {};
  for (const k of Object.keys(CODE)) pipes[k] = await host.compute(`rc3d.${k}`, CODE[k]);
  const sp = packScene(S), SC = mk(4 * sp.data.length, "rc3d scene", sp.data);
  // K grid phases (world.mjs jit; one without), each with its own flags and depth moments.
  const K = plan.jit || 1, PH = Array.from({ length: K }, (_, p) => phasePlan(plan, p));
  const params = (x, pl) => { const p = packParams(sc, pl, W, H, sp.triOff, { ...x, planted }); return mk(p.data.byteLength, "rc3d params", p.data); };
  const { flagWords, svWords } = packParams(sc, plan, W, H, sp.triOff), nt = 6 * plan.cd * plan.cd, npx = W * H;
  const T = plan.tf > 0, mv = !!plan.move, GA = mk(48 * npx, "rc3d gbuffer"), GB = mv ? mk(48 * npx, "rc3d gbuffer b") : GA, G = GA;
  const FLs = PH.map((_, p) => mk(4 * flagWords, `rc3d flags ${p}`)), Ds = PH.map((_, p) => mk(16 * L[0].probes * nt, `rc3d depth ${p}`));
  const SVs = PH.map((_, p) => (plan.sv ? mk(4 * svWords, `rc3d start visibility ${p}`) : null));
  const LV = L.map((l, i) => mk(16 * l.probes * l.texels, `rc3d level ${i}`)), dummy = mk(16, "rc3d dummy");
  const cinit = new Float32Array(16 * L[0].probes);
  if (cacheInit) for (let q = 0; q < L[0].probes; q++) cinit.set([Math.PI * cacheInit[0], Math.PI * cacheInit[1], Math.PI * cacheInit[2]], 16 * q);
  const CA = mk(64 * L[0].probes, "rc3d cache a", cinit), CB = mk(64 * L[0].probes, "rc3d cache b", cinit);
  const grid = plan.stride ? probeGrid(W, H, plan.stride) : { pw: 1, ph: 1 }, np = grid.pw * grid.ph, E = mk(16 * np, "rc3d screen probes");
  const BI = mk(4 * np, "rc3d screen bits"), R = mk(plan.stride ? 16 * np * plan.m * plan.m : 16, "rc3d screen rays");
  const OUT = mk(16 * npx, "rc3d out"), E2 = plan.sf ? mk(16 * np, "rc3d screen probes b") : null;
  const I = T ? mk(16 * npx, "rc3d irradiance") : null, HA = T ? mk(16 * npx, "rc3d history a") : null, HB = T ? mk(16 * npx, "rc3d history b") : null;
  const step = (name, total, bufs, x = {}, pl = PH[0]) => { const g = grid2(total, WGS[name]); return { name, pipe: pipes[name], bufs: [params({ ...x, total, gridX: g.gridX }, pl), SC, ...bufs], gx: g.gx, gy: g.gy }; };
  const flagSteps = (p) => [{ clear: FLs[p] }, ...L.map((l, i) => step("flags", (plan.pi ? 110 : 14) * l.probes, [FLs[p]], { level: i }, PH[p]))];
  // One frame's steps in grid phase p, reading cache `cp` (made in phase p - 1) and writing cache
  // `cn`, on g-buffer Gc (Gp the previous frame's, the same buffer for a still camera) and history
  // Hp into Hn.
  function frameSteps(cp, cn, Gc, Gp, Hp, Hn, p) {
    const out = [], pl = PH[p], q = (p + K - 1) % K, FL = FLs[p], D = Ds[p];
    if (mv) out.push(step("gbuf", npx, [Gc]));
    if (!staticFlags) out.push(...flagSteps(p));
    for (let i = L.length - 1; i >= 0; i--) out.push(step("level", L[i].probes * L[i].texels, [cp, Ds[q], FL, i + 1 < L.length ? LV[i + 1] : dummy, LV[i], SVs[p] || dummy], { level: i, clo: PH[q].lo }, pl));
    out.push(step("cache", L[0].probes, [LV[0], FL, cn], {}, pl));
    if (plan.stride) {
      out.push(step("svis", np, [FL, Gc, BI], {}, pl));
      out.push(step("srays", np * plan.m * plan.m, [cn, D, L.length > 1 ? LV[1] : dummy, Gc, BI, R], {}, pl));
      out.push(step("ssum", np, [BI, R, E, Gc], {}, pl));
    }
    let Ef = E;
    for (let a = 0; a < plan.sf; a++) { const Eo = Ef === E ? E2 : E; out.push(step("atrous", np, [Gc, Ef, Eo], { level: 1 << a }, pl)); Ef = Eo; }
    out.push(step("final", npx, [cn, D, Gc, Ef, T ? I : OUT], {}, pl));
    if (T) out.push(step("accum", npx, [Gc, Gp, I, Hp, Hn, OUT], {}, pl));
    return out;
  }
  // Frame f runs step set f % sets: cache parity f % 2 and phase f % K (K is even under jit).
  const sets = Math.max(2, K), frames = Array.from({ length: sets }, (_, s) => (s % 2 ? frameSteps(CB, CA, GB, GA, HB, HA, s % K) : frameSteps(CA, CB, GA, GB, HA, HB, s % K)));
  // A phase's depth moments (and, for static geometry, its flags) are traced on its first frame.
  const startSteps = (p) => (plan.sv ? L.map((l, i) => step("start", l.probes * l.texels, [SVs[p]], { level: i | ((plan.svl ?? 255) >> i & 1 ? 0 : 65536) }, PH[p])) : []);
  const phaseSetup = (p) => [step("depth", L[0].probes * nt, [Ds[p]], {}, PH[p]), ...startSteps(p), ...(staticFlags ? flagSteps(p) : [])], built = new Set([0]);
  const setup = [step("gbuf", npx, [G]), ...phaseSetup(0)];
  const canTime = dev.features.has("timestamp-query"), maxQ = 2 * Math.max(frames[0].length, setup.length, 2);
  const qs = canTime ? dev.createQuerySet({ type: "timestamp", count: maxQ }) : null;
  const qb = canTime ? dev.createBuffer({ size: 8 * maxQ, usage: 0x200 | 0x04, label: "rc3d timestamps" }) : null;
  let frameNo = 0;
  // Before a frame: its camera and denoise words into every pass's params.
  function setFrame(steps) {
    const w = frameWords(sc, plan, W, H, frameNo++, planted);
    for (const s of steps) if (!s.clear) dev.queue.writeBuffer(s.bufs[0], 4 * FRAME_WORDS[0], w);
  }
  // One compute pass a step (each step reads what the one before wrote); timed: a timestamp pair a pass.
  function record(enc, steps, timed) {
    steps.forEach((s, i) => {
      if (s.clear) { enc.clearBuffer(s.clear); return; }
      const cp = enc.beginComputePass(timed && qs ? { timestampWrites: { querySet: qs, beginningOfPassWriteIndex: 2 * i, endOfPassWriteIndex: 2 * i + 1 } } : {});
      cp.setPipeline(s.pipe); cp.setBindGroup(0, host.bind(s.pipe, s.bufs)); cp.dispatchWorkgroups(s.gx, s.gy); cp.end();
    });
    if (timed && qs) enc.resolveQuerySet(qs, 0, 2 * steps.length, qb, 0);
  }
  async function readBuf(buf, bytes) {
    const s = host.buffer({ size: bytes, usage: ["map-read", "copy-dst"] }), enc = dev.createCommandEncoder();
    enc.copyBufferToBuffer(buf, 0, s, 0, bytes); dev.queue.submit([enc.finish()]);
    await s.mapAsync(1); const o = s.getMappedRange().slice(0); s.unmap(); s.destroy(); return o;
  }
  async function submit(steps, timed = false) {
    const enc = dev.createCommandEncoder(); record(enc, steps, timed); dev.queue.submit([enc.finish()]); await dev.queue.onSubmittedWorkDone();
    if (!timed || !qb) return null;
    const v = new BigUint64Array(await readBuf(qb, 16 * steps.length));
    return steps.map((s, i) => ({ name: s.name || "clear", ms: s.clear ? 0 : Number(v[2 * i + 1] - v[2 * i]) / 1e6 }));
  }
  const setupTimes = await submit(setup, true);
  // The step set of the next frame, after tracing its phase's moments if this is the phase's first frame.
  async function nextSet() {
    const s = frameNo % sets, p = s % K;
    if (!built.has(p)) { await submit(phaseSetup(p)); built.add(p); }
    const steps = frames[s]; setFrame(steps); return steps;
  }
  return {
    plan, bytes: owned.reduce((a, b) => a + b.size, 0), setupTimes,
    async run(n = plan.frames) { for (let f = 0; f < n; f++) await submit(await nextSet()); },
    async read() { return new Float32Array(await readBuf(OUT, 16 * npx)); },
    async readParts() { // the frame's intermediate buffers, for debugging against the CPU
      const p = ((frameNo + sets - 1) % sets) % K, odd = frameNo % 2; // the last frame's phase, and the cache and g-buffer it wrote
      return { flags: new Uint32Array(await readBuf(FLs[p], 4 * flagWords)), levels: await Promise.all(LV.map(async (b) => new Float32Array(await readBuf(b, b.size)))),
        cache: new Float32Array(await readBuf(odd ? CB : CA, 64 * L[0].probes)), depth: new Float32Array(await readBuf(Ds[p], 16 * L[0].probes * nt)), G: new Float32Array(await readBuf(odd ? GA : GB, 48 * npx)) };
    },
    // n timed frames: GI ms a frame (first pass start to last pass end) and the per-pass split.
    async time(n = 50) {
      const gi = [], passes = [];
      for (let f = 0; f < n; f++) {
        const steps = await nextSet(); const enc = dev.createCommandEncoder(); record(enc, steps, true);
        dev.queue.submit([enc.finish()]); await dev.queue.onSubmittedWorkDone();
        if (!qb) continue;
        const v = new BigUint64Array(await readBuf(qb, 16 * steps.length)), on = steps.map((s, i) => i).filter((i) => !steps[i].clear);
        gi.push(Number(v[2 * on[on.length - 1] + 1] - v[2 * on[0]]) / 1e6);
        passes.push(on.map((i) => Number(v[2 * i + 1] - v[2 * i]) / 1e6));
      }
      const names = frames[0].filter((s) => !s.clear).map((s) => s.name);
      return { gi_ms: qb ? gi : null, passes: qb ? passes : null, names };
    },
    async gbufferMs(n = 10) { const t = []; for (let i = 0; i < n; i++) { const r = await submit([setup[0]], true); if (r) t.push(r[0].ms); } return t; },
    async baseline(m = 16) { await submit([step("baseline", npx, [G, OUT], { m })]); },
    destroy() { for (const b of owned) b.destroy(); if (qs) qs.destroy(); if (qb) qb.destroy(); },
  };
}
