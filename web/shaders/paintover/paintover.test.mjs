import test from "node:test";
import assert from "node:assert/strict";
import { isoCamera, ray, project, toCamera } from "./camera.mjs";
import { makeProjector, covered, shadePoint, renderProjected, reaches, KEYFRAME, DEFAULTS } from "./projector.mjs";
import { paint } from "./painter.mjs";
import { renderGbuf } from "../fixtures/harbour-render.mjs";

const T = [1, 1.5, 0], W = 128, H = 72;
const cam = (dy = 0, de = 0, scale = 10.8) => isoCamera({ yawDeg: 45 + dy, elevDeg: 35.264 + de, target: T, scale });

test("project inverts ray for orthographic and perspective cameras", () => {
  for (const c of [cam(), isoCamera({ target: T, dist: 60, ortho: false, fovY: 0.5 })]) {
    for (const [x, y] of [[0, 0], [63, 35], [127, 71], [10.5, 20.25]]) {
      const r = ray(c, W, H, x, y), t = 17.5, p = project(c, W, H, [r.o[0] + r.d[0] * t, r.o[1] + r.d[1] * t, r.o[2] + r.d[2] * t]);
      assert.ok(Math.abs(p[0] - x) < 1e-6 && Math.abs(p[1] - y) < 1e-6, `${x},${y} -> ${p[0]},${p[1]}`);
    }
  }
  assert.deepEqual(toCamera(cam(), [0, 0, 0]).map((v) => Math.round(v * 1e6) / 1e6), cam().fwd.map((v) => Math.round(-v * 1e6) / 1e6));
});

function setup(seed = 3) {
  const c = cam(), g = renderGbuf(c, W, H, { shadeOn: true }), p = paint({ w: W, h: H, rgb: g.shade }, { id: g.id, N: g.N }, { seed });
  return { c, g, p, projector: makeProjector(c, W, H, p.rgba8, g.depth) };
}
test("the painter is deterministic, keeps object boundaries, and a mask leaves every other pixel exactly as it was", () => {
  const { g, p } = setup(), again = paint({ w: W, h: H, rgb: g.shade }, { id: g.id, N: g.N }, { seed: 3 });
  assert.deepEqual(Array.from(p.rgba8.slice(0, 4000)), Array.from(again.rgba8.slice(0, 4000)));
  const mask = new Uint8Array(W * H); for (let y = 20; y < 40; y++) for (let x = 30; x < 70; x++) mask[y * W + x] = 1;
  const m = paint({ w: W, h: H, rgb: g.shade }, { id: g.id, N: g.N }, { seed: 3, mask });
  let outside = 0, inside = 0;
  for (let i = 0; i < W * H; i++) for (let c = 0; c < 3; c++) { const want = Math.round(Math.min(1, Math.max(0, g.shade[i * 3 + c])) * 255), got = m.rgba8[i * 4 + c]; if (!mask[i]) { if (want !== got) outside++; } else if (want !== got) inside++; }
  assert.equal(outside, 0); assert.ok(inside > 100, "the masked region is painted");
});
test("round trip: the projected painting equals the painting on covered pixels, a one-pixel shift does not", () => {
  const { c, projector, p } = setup(), g = renderGbuf(c, W, H, { shadeOn: false }), r = renderProjected([projector], g, c, { opt: KEYFRAME });
  let n = 0, hit = 0, max = 0;
  for (let i = 0; i < W * H; i++) { if (!g.hit[i]) continue; hit++; if (!r.covered[i]) continue; n++; for (let k = 0; k < 3; k++) max = Math.max(max, Math.abs(Math.round(Math.min(1, Math.max(0, r.rgb[i * 3 + k])) * 255) - p.rgba8[i * 4 + k])); }
  assert.ok(n / hit > 0.98, `covered ${n / hit}`); assert.ok(max <= 1, `max ${max}`);
  const sh = { ...c, pos: c.pos.map((v, i) => v + c.right[i] * ((2 * c.scale) / H)) }, gs = renderGbuf(sh, W, H, { shadeOn: false }), rs = renderProjected([projector], gs, sh, { opt: KEYFRAME });
  let sum = 0, m2 = 0; for (let i = 0; i < W * H; i++) { if (!gs.hit[i] || !rs.covered[i]) continue; m2++; for (let k = 0; k < 3; k++) sum += Math.abs(Math.round(Math.min(1, Math.max(0, rs.rgb[i * 3 + k])) * 255) - p.rgba8[i * 4 + k]); }
  assert.ok(sum / (m2 * 3) > 1.0, `shift control mean ${sum / (m2 * 3)}`);
  const none = makeProjector(c, W, H, p.rgba8, g.depth.map((d) => d - 10)), z = renderProjected([none], g, c, { opt: KEYFRAME });
  assert.equal(z.covered.reduce((a, b) => a + b, 0), 0, "a depth map that hides every surface covers nothing");
});
test("keyframe windows: at a projector's own camera the others weigh only the floor; legacy windows leak", () => {
  const a = setup(1), b = { c: cam(30, -10) }; b.g = renderGbuf(b.c, W, H, { shadeOn: true }); b.p = paint({ w: W, h: H, rgb: b.g.shade }, { id: b.g.id, N: b.g.N }, { seed: 2 }); b.projector = makeProjector(b.c, W, H, b.p.rgba8, b.g.depth);
  const set = [a.projector, b.projector];
  assert.ok(Math.abs(reaches(set, KEYFRAME)[0] - Math.acos(Math.min(1, a.c.fwd.reduce((s, v, i) => s + v * b.c.fwd[i], 0)))) < 1e-9, "reach is the angle to the nearest projector");
  let worstKey = 0, worstLegacy = 0, n = 0;
  for (let i = 0; i < W * H; i += 5) {
    if (!a.g.hit[i]) continue;
    const P = [a.g.P[i * 3], a.g.P[i * 3 + 1], a.g.P[i * 3 + 2]], N = [a.g.N[i * 3], a.g.N[i * 3 + 1], a.g.N[i * 3 + 2]];
    if (!covered(a.projector, P, N) || !covered(b.projector, P, N)) continue;
    n++;
    const k = shadePoint(set, P, N, a.c, KEYFRAME), l = shadePoint(set, P, N, a.c, DEFAULTS), own = shadePoint([a.projector], P, N, a.c, KEYFRAME);
    for (let q = 0; q < 3; q++) { worstKey = Math.max(worstKey, Math.abs(k.rgb[q] - own.rgb[q]) * 255); worstLegacy = Math.max(worstLegacy, Math.abs(l.rgb[q] - own.rgb[q]) * 255); }
  }
  assert.ok(n > 50, `overlap pixels ${n}`); assert.ok(worstKey < 0.05, `keyframe contamination ${worstKey}`); assert.ok(worstLegacy > 2, `legacy windows leak ${worstLegacy}`);
});
