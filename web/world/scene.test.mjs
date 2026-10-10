// node --test web/world/scene.test.mjs
// G0 (ROADMAP M2 criterion 13): the scene hierarchy round-trips through superstack.scene/1.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { Scene, toSuperstack, fromSuperstack, KINDS, apply } from "./scene.mjs";
import { canonical, canonicalSha256, validateScene, rng } from "../../third_party/superstack/superstack.mjs";

const near = (a, b, e = 1e-9) => assert.ok(Math.abs(a - b) <= e, `${a} != ${b}`);

// A random tree of n nodes: every kind, nested, with non-trivial transforms and props.
function randomScene(seed, n) {
  const r = rng(seed), s = new Scene(), ids = [];
  const pick = (a) => a[Math.floor(r.nextFloat() * a.length)];
  for (let i = 0; i < n; i++) {
    const parent = ids.length && r.nextFloat() < 0.8 ? pick(ids) : null;
    const q = [r.nextFloat() - 0.5, r.nextFloat() - 0.5, r.nextFloat() - 0.5, r.nextFloat() - 0.5], l = Math.hypot(...q);
    const kind = KINDS[i % KINDS.length];
    const props = kind === "mesh" ? { shape: r.nextFloat() < 0.5 ? "box" : "quad", half: 0.5 + r.nextFloat(), albedo: [r.nextFloat(), r.nextFloat(), r.nextFloat()] }
      : kind === "tilemap" ? { tiles: [[1, 2], [3, 0]], size: 16 } : kind === "sprite" ? { frame: Math.floor(r.nextFloat() * 8) } : kind === "light" ? { intensity: r.nextFloat() * 3 } : {};
    ids.push(s.add({ name: `node ${i}`, kind, parent, t: [r.nextFloat() * 10 - 5, r.nextFloat() * 10 - 5, r.nextFloat() * 10 - 5], r: q.map((x) => x / l), s: [0.5 + r.nextFloat(), 0.5 + r.nextFloat(), 0.5 + r.nextFloat()], props }));
  }
  return s;
}

test("the hierarchy round-trips through superstack.scene/1 byte for byte", () => {
  for (let k = 0; k < 50; k++) {
    const s = randomScene(`g0-${k}`, 5 + k * 3);
    const doc = toSuperstack(s, { width: 640, height: 360 });
    assert.deepEqual(validateScene(doc), [], "a valid superstack.scene/1 document");
    const back = fromSuperstack(JSON.parse(canonical(doc)));
    const again = toSuperstack(back, { width: 640, height: 360 });
    assert.equal(canonicalSha256(again), canonicalSha256(doc));
    // World transforms survive too.
    for (const n of s.walk()) { const a = s.world(n.id), b = back.world(n.id); for (let i = 0; i < 16; i++) near(a[i], b[i], 1e-12); }
  }
});

test("world transforms compose parent then child, and reparenting keeps the local transform", () => {
  const s = new Scene();
  const a = s.add({ t: [10, 0, 0], r: [0, Math.SQRT1_2, 0, Math.SQRT1_2] });   // 90 degrees about y
  const b = s.add({ parent: a, t: [1, 0, 0], s: [2, 2, 2] });
  const p = apply(s.world(b), [1, 0, 0]);                       // child x axis, scaled 2, rotated to -z
  near(p[0], 10); near(p[1], 0); near(p[2], -3);
  s.reparent(b, null);
  const q = apply(s.world(b), [1, 0, 0]);
  near(q[0], 3); near(q[2], 0);
  assert.throws(() => s.reparent(a, b) || s.reparent(b, a), /ancestor/);
});

test("a plain superstack scene imports as a flat tree and exports the same geometry", () => {
  const v = JSON.parse(readFileSync(new URL("../../third_party/superstack/vectors/scene.json", import.meta.url), "utf8"));
  const doc = v.hashes[0].scene;
  const s = fromSuperstack(doc);
  const out = toSuperstack(s, { width: doc.frame.width, height: doc.frame.height, seed: doc.seed });
  assert.deepEqual(out.meshes.map((m) => m.id), doc.meshes.map((m) => m.id));
  for (const [i, m] of out.meshes.entries()) {
    if (m.shape === "box") { m.center.forEach((x, j) => near(x, doc.meshes[i].center[j])); near(m.half, doc.meshes[i].half); }
    else m.corners.flat().forEach((x, j) => near(x, doc.meshes[i].corners.flat()[j]));
  }
  // The camera comes back pointing the same way.
  const f = (c) => { const d = c.target.map((x, j) => x - c.eye[j]), l = Math.hypot(...d); return d.map((x) => x / l); };
  out.camera.eye.forEach((x, j) => near(x, doc.camera.eye[j], 1e-9));
  f(out.camera).forEach((x, j) => near(x, f(doc.camera)[j], 1e-9));
});
