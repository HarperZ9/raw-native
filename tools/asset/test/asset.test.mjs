// node --test tools/asset/test/   Every bound asserted here is the one DESIGN.md states; each check has a control
// that must fail (a bad file, a skewed field), so a pass cannot come from a check that accepts anything.
import test from "node:test";
import assert from "node:assert/strict";
import { Field, project } from "../src/sdf.mjs";
import { extract } from "../src/extract.mjs";
import { retopo, meshError } from "../src/retopo.mjs";
import { unwrap } from "../src/uv.mjs";
import { parseSculpt, stringifySculpt, packAsset, unpackAsset, sha256 } from "../src/format.mjs";
import { encodePng, decodePng } from "../src/png.mjs";
import { buildAsset } from "../src/build.mjs";
import { parsePaint } from "../src/paint.mjs";

const SPHERE = { format: "rnsculpt/1", name: "ball", eyes: [], strokes: [{ id: "ball", op: "add", shape: "ell", c: [0, 0, 0], r: [0.05, 0.05, 0.05], k: 0.01 }] };
const HEADISH = { format: "rnsculpt/1", name: "bump", eyes: [{ c: [0.02, 0.01, 0.04], r: 0.008, iris: 0.004, gaze: [0, 0, 1], mirror: true }], strokes: [
  { id: "skull", op: "add", shape: "ell", c: [0, 0, 0], r: [0.05, 0.06, 0.055], k: 0.01 },
  { id: "nose", op: "add", shape: "cap", a: [0, 0, 0.04], b: [0, -0.02, 0.06], r: [0.006, 0.008], k: 0.01 },
  { id: "ear", op: "add", shape: "ell", c: [0.05, 0, -0.005], r: [0.006, 0.02, 0.012], k: 0.005, mirror: true },
  { id: "pit", op: "sub", shape: "ell", c: [0.02, 0.01, 0.052], r: [0.01, 0.006, 0.01], k: 0.004, mirror: true },
  { id: "hair", op: "add", shape: "ell", c: [0, 0.01, -0.008], r: [0.053, 0.062, 0.057], k: 0.004, tag: "hair" }] };
const PAINT = { format: "rnpaint/1", layers: [{ name: "base", region: "skin", strokes: [{ kind: "fill", color: [0.8, 0.6, 0.5] }] }, { name: "hair", region: "hair", strokes: [{ kind: "fill", color: [0.3, 0.2, 0.1] }] }, { name: "ink", region: "all", strokes: [{ kind: "ink", src: "cav", lo: 0.4, hi: 0.85, color: [0.1, 0.05, 0.05] }] }] };
const BOUND = { cell3: { p99: 0.0005, max: 0.0015 }, cell6: { p99: 0.0015, max: 0.004 } };   // metres, from DESIGN.md

test("sculpt file round trips byte for byte and the hash is stable", () => {
  const text = stringifySculpt(HEADISH), again = stringifySculpt(parseSculpt(text));
  assert.equal(again, text);
  assert.equal(sha256(text), sha256(again));
  assert.equal(text.split("\n").filter((l) => l.includes('"id"')).length, HEADISH.strokes.length, "one stroke a line");
});

test("the validator refuses bad strokes (controls)", () => {
  const bad = (m) => assert.throws(() => parseSculpt(JSON.stringify({ ...SPHERE, strokes: [m] })), /rnsculpt/);
  bad({ id: "a", op: "add", shape: "ell", c: [0, 0, 0], r: [0.05, -1, 0.05] });
  bad({ id: "a", op: "melt", shape: "ell", c: [0, 0, 0], r: [1, 1, 1] });
  assert.throws(() => parseSculpt(JSON.stringify({ ...SPHERE, strokes: [SPHERE.strokes[0], SPHERE.strokes[0]] })), /repeated/);
});

test("RNA1 and PNG round trip", () => {
  const a = { "a.pos": Float32Array.of(1, 2, 3), "a.tri": Uint32Array.of(0, 1, 2), "a.curv": Uint8Array.of(1, 2, 3, 4, 5) };
  const r = unpackAsset(packAsset({ name: "x" }, a));
  assert.deepEqual(Object.keys(r.arrays).sort(), Object.keys(a).sort());
  for (const k of Object.keys(a)) assert.deepEqual([...r.arrays[k]], [...a[k]]);
  const img = { width: 5, height: 4, channels: 3, data: Uint8Array.from({ length: 60 }, (_, i) => (i * 37) & 255) }, d = decodePng(encodePng(img));
  assert.deepEqual([...d.data], [...img.data]);
});

const meshVolume = (M) => { let vol = 0; for (let q = 0; q < M.quads.length; q += 4) for (const [a, b, c] of [[0, 1, 2], [0, 2, 3]]) { const i = [M.quads[q + a], M.quads[q + b], M.quads[q + c]].map((k) => [M.pos[k * 3], M.pos[k * 3 + 1], M.pos[k * 3 + 2]]); vol += (i[0][0] * (i[1][1] * i[2][2] - i[1][2] * i[2][1]) - i[0][1] * (i[1][0] * i[2][2] - i[1][2] * i[2][0]) + i[0][2] * (i[1][0] * i[2][1] - i[1][1] * i[2][0])) / 6; } return vol; };

test("extraction error is inside the bound, against an analytic sphere", () => {
  const F = new Field(SPHERE.strokes), box = { lo: [-0.06, -0.06, -0.06], hi: [0.06, 0.06, 0.06], cell: 0.003 }, M = extract(F, box);
  let worst = 0; for (let v = 0; v < M.pos.length; v += 3) worst = Math.max(worst, Math.abs(Math.hypot(M.pos[v], M.pos[v + 1], M.pos[v + 2]) - 0.05));
  assert.ok(worst < 2e-5, "vertex radius error " + worst);
  const e = meshError(F, M); assert.ok(e.p99 < BOUND.cell3.p99 && e.max < BOUND.cell3.max, JSON.stringify(e));
  const ratio = meshVolume(M) / (4 / 3 * Math.PI * 0.05 ** 3); assert.ok(Math.abs(ratio - 1) < 0.02, "outward winding, volume ratio " + ratio);
  const skew = extract(new Field([{ ...SPHERE.strokes[0], r: [0.05, 0.05, 0.056] }]), box);   // control: a different sculpt must read as a large error
  assert.ok(meshError(F, skew).max > 0.004, "the error measure must see a 6 mm change");
});

test("retopo of the head-like sculpt stays inside the LOD bounds and is all quads", () => {
  const F = new Field(HEADISH.strokes), B = F.bounds(0.004);
  for (const [cell, key] of [[0.003, "cell3"], [0.006, "cell6"]]) { const M = retopo(F, { lo: B.lo, hi: B.hi, cell }), e = meshError(F, M); assert.equal(M.quads.length % 4, 0); assert.ok(e.p99 < BOUND[key].p99 && e.max < BOUND[key].max, key + " " + JSON.stringify(e)); }
});

test("sparse bricks and the stroke-box cull lose nothing (check the checks)", () => {
  const F = new Field(HEADISH.strokes), Fall = new Field(HEADISH.strokes, { cull: false }), B = F.bounds(0.004), o = { lo: B.lo, hi: B.hi, cell: 0.004 };
  const a = extract(F, o), b = extract(Fall, { ...o, sparse: false });
  assert.equal(a.quads.length, b.quads.length); assert.equal(a.pos.length, b.pos.length);
  assert.ok(a.stats.skipped > 0, "the sparse path must actually skip bricks");
  let worst = 0; for (let i = 0; i < 4000; i++) { const p = project(F, [Math.sin(i) * 0.05, Math.cos(i * 1.3) * 0.06, Math.sin(i * 0.7) * 0.055], 4, 0.05); worst = Math.max(worst, Math.abs(F.d(p[0], p[1], p[2]) - Fall.d(p[0], p[1], p[2]))); }
  assert.ok(worst < 1e-6, "culled and full field differ by " + worst + " near the surface");
});

test("mirror makes the field symmetric", () => {
  const F = new Field(HEADISH.strokes);
  for (let i = 0; i < 200; i++) { const x = Math.sin(i) * 0.07, y = Math.cos(i * 1.7) * 0.07, z = Math.sin(i * 2.3) * 0.07; assert.ok(Math.abs(F.d(x, y, z) - F.d(-x, y, z)) < 1e-9); }
});

test("UV atlas fits, with bounded stretch", () => {
  const F = new Field(HEADISH.strokes), B = F.bounds(0.004), M = retopo(F, { lo: B.lo, hi: B.hi, cell: 0.004 }), U = unwrap(M, { res: 256 });
  assert.ok(U.uv.every((v) => v >= 0 && v <= 1)); assert.ok(U.stretch.p95 < 2, JSON.stringify(U.stretch));
});

test("a build is deterministic: same inputs, same bytes", () => {
  const run = () => buildAsset({ sculpt: HEADISH, paint: parsePaint(JSON.stringify(PAINT)), res: 128, refCell: 0, lods: [{ name: "portrait", cell: 0.005, relax: 2 }, { name: "play", cell: 0.01, relax: 2 }] });
  const a = run(), b = run();
  assert.equal(sha256(Buffer.from(a.rna)), sha256(Buffer.from(b.rna)));
  assert.equal(sha256(a.tex.albedo), sha256(b.tex.albedo)); assert.equal(sha256(a.tex.normal), sha256(b.tex.normal));
  assert.ok(a.bounds.bake.normalErrDegMax < 1, "normal map round trip " + a.bounds.bake.normalErrDegMax + " deg");
  assert.equal(a.bounds.bake.cageViolations, 0);
  assert.ok(a.arrays["portrait.hair"].some((v) => v === 1) && a.arrays["portrait.hair"].some((v) => v === 0), "hair and skin both present");
});
