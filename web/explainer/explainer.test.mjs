// node --test web/explainer/explainer.test.mjs
// The explainer kit's pure parts: the reference maths, LOD, data loading, layouts, cards,
// camera and transitions. The GPU half is tests/web/explainer_parity.py.
import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { hash32, hashUnit, lodKeep, lodFraction, orbAt, apertureEdge, atmosphereAt, bezier, pulseAt, rampAt, valueNoise } from "./ref.mjs";
import { orbFloats, atmosFloats, NIGHT } from "./pack.mjs";
import { parseNpy, half, show, quantile } from "./data.mjs";
import { tensorGrid, tokenRow, ellipseLattice } from "./layout.mjs";
import { typeKit } from "./style.mjs";
import { dataCard, matrix } from "./cards.mjs";
import { shots, iris, focusPull } from "./shots.mjs";

const json = async (p) => JSON.parse(await readFile(new URL(p, import.meta.url), "utf8"));

test("hash32 matches known lowbias32 values and stays in u32", () => {
  // Values computed by hand from the published lowbias32 constants.
  assert.equal(hash32(0), 0);
  assert.equal(hash32(1), 1753845952);
  for (const x of [0xffffffff, 12345, 2 ** 31]) { const h = hash32(x); assert.ok(h >= 0 && h < 2 ** 32 && Number.isInteger(h)); }
});

test("LOD keeps the expected share and is nested: a smaller keep is a subset", () => {
  const n = 200000;
  let k25 = 0, k10 = 0, notNested = 0;
  for (let i = 0; i < n; i++) {
    const a = lodKeep(i, 0.25), b = lodKeep(i, 0.1);
    k25 += a; k10 += b; if (b && !a) notNested++;
  }
  assert.ok(Math.abs(k25 / n - 0.25) < 0.005, `kept ${k25 / n}`);
  assert.ok(Math.abs(k10 / n - 0.1) < 0.005);
  assert.equal(notNested, 0);
  assert.deepEqual(lodFraction(1e6, 1e5, 1.5), { keep: 0.15, gain: 1 / 0.15 });
  assert.equal(lodFraction(10, 1e6).keep, 1);
});

test("the orb: hot at the centre, dark far away, masked by the iris, prismatic ring splits channels", () => {
  const c = orbAt(0, 0, {}), far = orbAt(6, 0, {});
  assert.ok(c[0] > 2 && c[0] > c[2]);
  assert.ok(far.every((x) => x < 0.01));
  // A 6-blade iris opened to 0.6: a point at r 0.8 is outside the blades at a corner-free angle.
  const open = orbAt(0.8, 0, { iris: { blades: 6, open: 1.2 } }), shut = orbAt(0.8, 0, { iris: { blades: 6, open: 0.6 } });
  assert.ok(shut[0] < open[0] * 0.5);
  assert.ok(Math.abs(apertureEdge(0, { blades: 6, open: 1, rot: 0 }) - 1) < 1e-12);                       // a corner
  assert.ok(Math.abs(apertureEdge(Math.PI / 6, { blades: 6, open: 1, rot: 0 }) - Math.cos(Math.PI / 6)) < 1e-12);   // a blade middle
  const ring = (r) => orbAt(r, 0.3, { ring: 1, ringW: 0.01, prism: 0.05, haloGain: 0, coreGain: 0, soft: 0.01, iris: { open: 1, blades: 0 } });
  assert.ok(ring(0.95)[0] > ring(0.95)[2], "red ring sits inside");
  assert.ok(ring(1.05)[2] > ring(1.05)[0], "blue ring sits outside");
});

test("atmosphere and noise are deterministic, bounded and continuous across lattice cells", () => {
  assert.equal(valueNoise(3.2, 4.7, 7), valueNoise(3.2, 4.7, 7));
  for (let i = 0; i < 1000; i++) { const v = valueNoise(i * 0.137, i * 0.071, 3); assert.ok(v >= 0 && v <= 1); }
  assert.ok(Math.abs(valueNoise(4 - 1e-9, 2.5, 1) - valueNoise(4, 2.5, 1)) < 1e-6);
  const top = atmosphereAt(0.5, 0.02, 0, NIGHT), band = atmosphereAt(0.5, NIGHT.band, 0, NIGHT);
  assert.ok(band[0] > top[0]);
});

test("curves and pulses", () => {
  const c = [0, 0, 10, 0, 20, 0, 30, 0];
  assert.deepEqual(bezier(c, 0), [0, 0]);
  assert.deepEqual(bezier(c, 1), [30, 0]);
  assert.ok(Math.abs(bezier(c, 0.5)[0] - 15) < 1e-12);
  assert.equal(pulseAt(0.4, 0.4), 1);
  assert.ok(pulseAt(0.9, 0.4) < 1e-6);
  assert.deepEqual(rampAt([[0, [0, 0, 0]], [1, [1, 2, 3]]], 0.5), [0.5, 1, 1.5]);
});

test("uniform packing has the layouts the WGSL structs declare", () => {
  assert.equal(orbFloats({}).length, 28);        // OrbSpec: 7 vec4f
  const a = atmosFloats({});
  assert.equal(a.floats.length, 28);              // AtmosSpec: 7 vec4f
  assert.equal(a.bandC.length, 4);
});

test("parseNpy reads float16, float32 and int32 arrays, and half() decodes binary16", () => {
  const npy = (descr, shape, bytes) => {
    let h = `{'descr': '${descr}', 'fortran_order': False, 'shape': (${shape.join(", ")}${shape.length === 1 ? "," : ""}), }`;
    while ((10 + h.length + 1) % 64) h += " ";
    h += "\n";
    const out = new Uint8Array(10 + h.length + bytes.length);
    out.set([0x93, 78, 85, 77, 80, 89, 1, 0, h.length & 255, h.length >> 8]);
    out.set(new TextEncoder().encode(h), 10); out.set(bytes, 10 + h.length);
    return out.buffer;
  };
  const f4 = new Float32Array([1.5, -2, 3.25]);
  assert.deepEqual([...parseNpy(npy("<f4", [3], new Uint8Array(f4.buffer))).data], [1.5, -2, 3.25]);
  const f2 = new Uint16Array([0x3c00, 0xc000, 0x3555]);   // 1, -2, 0.333...
  const r = parseNpy(npy("<f2", [1, 3], new Uint8Array(f2.buffer)));
  assert.deepEqual(r.shape, [1, 3]);
  assert.equal(r.data[0], 1); assert.equal(r.data[1], -2); assert.ok(Math.abs(r.data[2] - 1 / 3) < 1e-3);
  assert.equal(half(0x7c00), Infinity);
  assert.equal(half(0x0001), 2 ** -24);
  assert.throws(() => parseNpy(new ArrayBuffer(16)), /not an .npy/);
  assert.equal(show(0.95779, "pct"), "95.8%");
  assert.equal(show(370325, "int"), "370,325");
  assert.equal(quantile([5, 1, 3, 2, 4], 0.5), 3);
});

test("the attention demo's data bundle: residual.npy matches its recorded hash and shape", async () => {
  const d = await json("../../docs/explainer/attention/data.json");
  const buf = await readFile(new URL("../../docs/explainer/attention/residual.npy", import.meta.url));
  assert.equal(createHash("sha256").update(buf).digest("hex"), d.files["residual.npy"]);
  const a = parseNpy(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
  assert.deepEqual(a.shape, d.residual.shape);
  assert.equal(a.data.length, 256 * 64 * 64);
  assert.ok(d.accuracy.repeat > 0.9 && d.accuracy.elsewhere < 0.05, "the model learned the repeat");
  assert.ok(d.ablations.both_layer_1_heads_zeroed.repeat < 0.05, "and needs layer 1 to do it");
});

test("tensorGrid places every element and its inverse names the element under a point", () => {
  const g = tensorGrid({ shape: [8, 4, 6], x: 100, y: 50, w: 800, h: 400 });
  const p = g.positions(null);
  for (let i = 0; i < 8 * 4 * 6; i += 7) {
    const hit = g.at(p[4 * i], p[4 * i + 1]);
    assert.equal(hit.index, i);
  }
  assert.equal(g.at(0, 0), null);
  assert.equal(tokenRow(5, { x0: 0, x1: 100, y: 3 })[4][0], 100);
  const e = ellipseLattice(500, 0, 0, 100, 50);
  assert.equal(e.length, 500);
  assert.ok(e.every(([x, y]) => (x / 100) ** 2 + (y / 50) ** 2 <= 1 + 1e-9));
});

test("type: labels type on glyph by glyph; cards show the bound value, not a typed one", async () => {
  const atlas = await json("../../docs/motion/hanken-grotesk-500.atlas.json");
  const mono = await json("./fonts/cascadia-mono-400.atlas.json");
  const T = typeKit({ head: atlas, body: atlas, mono });
  const full = T.meta("SEED 7", { x: 0, y: 0 })[0].shape.length, half1 = T.meta("SEED 7", { x: 0, y: 0, reveal: 0.5 })[0].shape.length;
  assert.ok(half1 < full && half1 > 0);
  assert.deepEqual(T.meta("SEED 7", { reveal: 0 }), []);
  const card = dataCard(T, { x: 0, y: 0, title: "Accuracy", rows: [{ label: "repeat", value: 0.95779, format: "pct", bar: 1 }] });
  assert.ok(card.length > 6);
  const m = matrix({ x: 0, y: 0, size: 100, rows: [[1], [0.5, 0.5]] });
  assert.equal(m.length, 4);
});

test("camera shots ease between keys, zoom in log space, and cut under reduced motion", () => {
  const keys = [{ t: 0, x: 0, y: 0, zoom: 1 }, { t: 10, x: 100, y: 0, zoom: 4 }];
  const cam = shots(keys), cut = shots(keys, { reduced: true });
  assert.equal(cam(5).zoom, 2);                    // geometric midpoint, inOut eases to 0.5 at 5
  assert.equal(cam(5).x, 50);
  assert.equal(cut(4.9).x, 0);
  assert.equal(cut(5.1).x, 100);
  const ir = iris(10, 9, 11), irR = iris(10, 9, 11, { reduced: true });
  assert.ok(ir.cover > 0.99 && ir.ground === 1);
  assert.equal(irR.cover, 0);
  assert.deepEqual(focusPull(0, 0, 2, 20, { reduced: true }).in, { blur: 0, opacity: 0 });
});
