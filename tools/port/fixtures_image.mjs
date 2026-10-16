// Fixtures of the image passes of round 2 (evidence/cpp-port-style-bounds-2.json, image_passes):
// purity, lightpaint, glaze and hatch. Each case is the web parity page's case, built with the web
// modules unchanged. The web references write Float32Array frames, so each pixel is recomputed here
// in float64 with the modules' exported functions, the same operations in the same order, and every
// value is checked to round (Math.fround) to the module's own frame output before it is exported.
import { join } from "node:path";
import { resolvePurity, runPurity, landingError, landing } from "../../web/shaders/purity/purity.mjs";
import { resolveLightpaint, runLightpaint, shade } from "../../web/shaders/lightpaint/lightpaint.mjs";
import { resolveGlaze, runGlaze, glazeColour, thicknessAt } from "../../web/shaders/glaze/glaze.mjs";
import { resolveHatch, runHatch, camRay, march, curvature, coords, hatchLines } from "../../web/shaders/hatch/hatch.mjs";
import { stillSdf, STILL } from "../../web/shaders/fixtures/stilllife.mjs";
import { displayLinear, labSource } from "../../web/shaders/lab/sources.mjs";
import { Bundle, readJson } from "./rnfx.mjs";

function same(name, mine, official) {
  for (let i = 0; i < mine.length; i++) {
    if ((i & 3) === 3) continue;
    if (Math.fround(mine[i]) !== official[i]) throw new Error(`fixtures_image: ${name} value ${i} is ${mine[i]}, the module wrote ${official[i]}`);
  }
}
const casesOf = (pass) => readJson(`evidence/shaders-${pass}-parity-bounds.json`);

export function purity(out) {
  const b = casesOf("purity"), { width: W, height: H } = b.output, B = new Bundle("purity");
  B.head.cases = [];
  for (const c of b.cases) {
    const src = displayLinear(c.frame, W, H), plan = resolvePurity(c.preset), p = plan.p, v = new Float64Array(W * H * 4);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const { stat, dg } = landingError(p, W, H, x, y, c.t), e = stat + dg, f = landing(p, [e * (1 - p.spread), e, e * (1 + p.spread)]);
      const xs = x - p.wobble * (dg / Math.max(p.degauss, 1e-9)), x0 = Math.floor(xs), fx = xs - x0, I = [0, 0, 0];
      for (const [xx, wt] of [[x0, 1 - fx], [x0 + 1, fx]]) { const xc = Math.min(W - 1, Math.max(0, xx)), o = (y * W + xc) * 4; for (let k = 0; k < 3; k++) I[k] += wt * src.data[o + k]; }
      const o = (y * W + x) * 4;
      for (let j = 0; j < 3; j++) v[o + j] = f[0][j] * I[0] + f[1][j] * I[1] + f[2][j] * I[2];
      v[o + 3] = 1;
    }
    same(c.id, v, runPurity(plan, src, c.t).data);
    B.add(`${c.id}.src`, "f32", src.data); B.add(`${c.id}.js`, "f64", v);
    B.head.cases.push({ id: c.id, preset: c.preset, t: c.t, width: W, height: H, p });
  }
  B.write(join(out, "purity.rnfx"));
  return B.head.cases.length;
}

// The street fixture's lighting outputs as both lightpaint and glaze read them.
function streetArrays(B, id, src) {
  B.add(`${id}.albedo`, "f32", src.albedo); B.add(`${id}.light`, "f32", src.light); B.add(`${id}.fogT`, "f32", src.fogT);
  B.add(`${id}.kind`, "u8", src.kind); B.add(`${id}.normals`, "f32", src.normals); B.add(`${id}.depth`, "f32", src.depth);
}
const fogMix = (v, o, c, f, fog) => { for (let k = 0; k < 3; k++) v[o + k] = c[k] * (1 - f) + fog[k] * f; v[o + 3] = 1; };

export function lightpaint(out) {
  const b = casesOf("lightpaint"), { width: W, height: H } = b.output, B = new Bundle("lightpaint");
  B.head.cases = [];
  for (const c of b.cases) {
    const src = labSource(c.frame, W, H), plan = resolveLightpaint(c.preset), v = new Float64Array(W * H * 4);
    for (let i = 0; i < W * H; i++) {
      const o = i * 4, E = [src.light[o], src.light[o + 1], src.light[o + 2]];
      const col = src.kind[i] === 2 ? shade(plan, [src.albedo[o], src.albedo[o + 1], src.albedo[o + 2]], E)[0] : E;
      fogMix(v, o, col, src.fogT[i], src.fogColor);
    }
    same(c.id, v, runLightpaint(plan, src).data);
    streetArrays(B, c.id, src); B.add(`${c.id}.js`, "f64", v);
    B.head.cases.push({ id: c.id, preset: c.preset, width: W, height: H, p: plan.p, fogColor: src.fogColor });
  }
  B.write(join(out, "lightpaint.rnfx"));
  return B.head.cases.length;
}

export function glaze(out) {
  const b = casesOf("glaze"), { width: W, height: H } = b.output, B = new Bundle("glaze");
  B.head.cases = [];
  for (const c of b.cases) {
    const src = labSource(c.frame, W, H), cam = src.camera, plan = resolveGlaze(c.preset), p = plan.p, v = new Float64Array(W * H * 4);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const i = y * W + x, o = i * 4, E = [src.light[o], src.light[o + 1], src.light[o + 2]];
      let col = E;
      if (src.kind[i] === 2) {
        const sx = ((x + 0.5) / W - 0.5) * 2 * cam.aspect * cam.tan, sy = (0.5 - (y + 0.5) / H) * 2 * cam.tan;
        const r = [cam.fwd[0] + cam.right[0] * sx + cam.up[0] * sy, cam.fwd[1] + cam.right[1] * sx + cam.up[1] * sy, cam.fwd[2] + cam.right[2] * sx + cam.up[2] * sy];
        const rl = Math.hypot(r[0], r[1], r[2]), cosAir = Math.min(1, Math.abs(src.normals[o] * r[0] + src.normals[o + 1] * r[1] + src.normals[o + 2] * r[2]) / rl);
        const t = src.depth[i] / rl, q = [cam.eye[0] + r[0] * t, cam.eye[1] + r[1] * t, cam.eye[2] + r[2] * t];
        const g = glazeColour(p, [src.albedo[o], src.albedo[o + 1], src.albedo[o + 2]], cosAir, thicknessAt(p, q));
        col = [g[0] * E[0], g[1] * E[1], g[2] * E[2]];
      }
      fogMix(v, o, col, src.fogT[i], src.fogColor);
    }
    same(c.id, v, runGlaze(plan, src).data);
    streetArrays(B, c.id, src); B.add(`${c.id}.js`, "f64", v);
    B.head.cases.push({ id: c.id, preset: c.preset, width: W, height: H, p, fogColor: src.fogColor, camera: cam });
  }
  B.write(join(out, "glaze.rnfx"));
  return B.head.cases.length;
}

export function hatch(out) {
  const b = casesOf("hatch"), { width: W, height: H } = b.output, B = new Bundle("hatch");
  B.head.cases = []; B.head.still = STILL;
  for (const c of b.cases) {
    const plan = resolveHatch(c.preset, {}, { w: W, h: H }), p = plan.p, v = new Float64Array(W * H * 4), hit = new Uint8Array(W * H), fb = new Uint8Array(W * H);
    const eye = STILL.camera.eye, Ln = Math.sqrt(STILL.light[0] * STILL.light[0] + STILL.light[1] * STILL.light[1] + STILL.light[2] * STILL.light[2]), L = STILL.light.map((x) => x * (1 / Ln));
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const i = y * W + x, rd = camRay(plan, x, y), t = march(eye, rd);
      let col = p.paper;
      if (t > 0) {
        const q = [eye[0] + rd[0] * t, eye[1] + rd[1] * t, eye[2] + rd[2] * t], cv = curvature(stillSdf, q, p.umb);
        const tone = 0.12 + 0.88 * Math.max(0, cv.n[0] * L[0] + cv.n[1] * L[1] + cv.n[2] * L[2]);
        hit[i] = 1; fb[i] = cv.umbilic ? 1 : 0;
        const px = (t * 2 * STILL.camera.tan) / H, halfW = 0.5 * p.width * px, dark = 1 - tone, [s1, s2] = coords(q, cv);
        let ink = hatchLines(s1, p.freq, px, halfW * (0.4 + 1.6 * dark));
        if (p.layers > 1 && tone < 0.5) ink = 1 - (1 - ink) * (1 - hatchLines(s2, p.freq, px, halfW * (0.4 + 2 * (0.5 - tone))));
        if (p.contour > 0) { const nv = Math.abs(cv.n[0] * rd[0] + cv.n[1] * rd[1] + cv.n[2] * rd[2]); if (nv < p.contour) ink = Math.max(ink, Math.min(1, (p.contour - nv) / (0.5 * p.contour))); }
        col = [0, 1, 2].map((k) => p.paper[k] * (1 - ink) + p.ink[k] * ink);
      }
      v.set([col[0], col[1], col[2], 1], i * 4);
    }
    const official = runHatch(plan);
    same(c.id, v, official.data);
    for (let i = 0; i < W * H; i++) if (official.fallback[i] !== fb[i]) throw new Error(`fixtures_image: ${c.id} fallback flag differs at ${i}`);
    B.add(`${c.id}.js`, "f64", v); B.add(`${c.id}.js_hit`, "u8", hit); B.add(`${c.id}.js_fallback`, "u8", fb);
    B.head.cases.push({ id: c.id, preset: c.preset, width: W, height: H, p });
  }
  B.write(join(out, "hatch.rnfx"));
  return B.head.cases.length;
}

// Development adjacency (evidence/cpp-port-style-bounds-4.json): the float64 output from develop()'s
// D and A, checked against its own Float32 frame.
import { resolveAdjacency, develop, EPS } from "../../web/shaders/adjacency/adjacency.mjs";
export function adjacency(out) {
  const b = casesOf("adjacency"), { width: W, height: H } = b.output, B = new Bundle("adjacency");
  B.head.cases = [];
  for (const c of b.cases) {
    const src = labSource(c.frame, W, H), plan = resolveAdjacency(c.preset, {}, { w: W, h: H }), r = develop(plan, src), n = W * H, v = new Float64Array(n * 4), free = 1 - plan.decay;
    for (let i = 0; i < n; i++) { for (let k = 0; k < 3; k++) v[i * 4 + k] = src.data[i * 4 + k] * ((r.D[i * 3 + k] + EPS) / (r.A[i * 3 + k] * free + EPS)); v[i * 4 + 3] = 1; }
    same(c.id, v, r.out.data);
    B.add(`${c.id}.src`, "f32", src.data); B.add(`${c.id}.js`, "f64", v);
    const { w, h, s, cw, ch, Dc, Db, steps, dt, decay } = plan;
    B.head.cases.push({ id: c.id, preset: c.preset, width: W, height: H, p: plan.p, plan: { w, h, s, cw, ch, Dc, Db, steps, dt, decay } });
  }
  B.write(join(out, "adjacency.rnfx"));
  return B.head.cases.length;
}
