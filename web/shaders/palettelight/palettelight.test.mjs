import test from "node:test";
import assert from "node:assert/strict";
import { buildPalette } from "./palette.mjs";
import { resolvePalettelight, indexOf, runPalettelight, irradiance, tonality, idealLab } from "./palettelight.mjs";
import { renderChapel, chapelCamera } from "../fixtures/chapel-render.mjs";
import { CLS } from "../fixtures/chapel.mjs";

const plan = resolvePalettelight();
const px = (o) => ({ hit: 1, cls: CLS.stone, a: 1, P: [4, 0.5, 4], N: [0, 1, 0], vis: [1, 1, 1], phase: 0.3, ix: 5, iy: 9, ...o });

test("the palette is a table of exact 8-bit entries, ramps rise in lightness, shadows are cooler and the lamp variant is warmer", () => {
  const pal = buildPalette(), E = pal.entries;
  assert.ok(pal.size >= 40 && pal.size <= 48, `${pal.size} colours`);
  for (const e of E) assert.ok(e.rgb.every((v) => Number.isInteger(v) && v >= 0 && v <= 255));
  for (const name of ["ground", "stone", "wood", "foliage"]) for (const v of [0, 1]) { const L = pal.ramp[name][v].map((k) => E[k].lab[0]); for (let s = 1; s < L.length; s++) assert.ok(L[s] > L[s - 1], `${name} ${v} lightness`); }
  for (const name of ["ground", "stone", "wood", "foliage"]) for (let s = 0; s < pal.steps; s++) { const moon = E[pal.ramp[name][0][s]].lab, lamp = E[pal.ramp[name][1][s]].lab; assert.ok(lamp[2] > moon[2] - 1e-9 && lamp[1] > moon[1] - 0.2, `${name} step ${s}: warm variant has higher b`); }
  const stone = pal.ramp.stone[0].map((k) => E[k].lab); assert.ok(stone[0][2] < stone[3][2], "shadows are bluer than lights (b falls toward blue)");
});
test("every pixel is a palette entry, a miss is the night colour, and the result is deterministic", () => {
  const g = renderChapel(chapelCamera(), 64, 36), a = runPalettelight(plan, g, 0.4), b = runPalettelight(plan, g, 0.4);
  assert.deepEqual(Array.from(a.idx), Array.from(b.idx));
  const ok = new Set(plan.palette.entries.map((e) => e.rgb.join(",")));
  for (let i = 0; i < a.idx.length; i++) assert.ok(a.idx[i] < plan.palette.size && ok.has(`${a.rgba[i * 4]},${a.rgba[i * 4 + 1]},${a.rgba[i * 4 + 2]}`));
  assert.equal(indexOf(plan, px({ hit: 0 }), 0), plan.palette.ramp.night);
});
test("monotone: more light never lowers the ramp step at a fixed pixel and threshold", () => {
  for (const [ix, iy] of [[0, 0], [3, 5], [7, 2], [12, 9]]) for (const cls of [CLS.ground, CLS.stone, CLS.wood, CLS.foliage]) {
    let prev = -1;
    for (const vis1 of [0, 0.1, 0.2, 0.4, 0.7, 1]) {
      const p = px({ cls, ix, iy, vis: [0, vis1, 1], P: [5.2, 0, 5.6 + 0.001], N: [0, 1, 0], a: 0.9 }), k = plan.palette.entries[indexOf(plan, p, 0)];
      assert.ok(k.step >= prev || k.variant !== plan.palette.entries[indexOf(plan, px({ cls, ix, iy, vis: [0, 0, 1], a: 0.9 }), 0)].variant, "step");
      prev = Math.max(prev, k.step);
    }
  }
});
test("mean preservation in index space: over a tile the mean step equals the ramp coordinate within one threshold rank", () => {
  for (const E of [0.15, 0.3, 0.6]) {
    const p0 = px({ vis: [1, 0, 1], N: [0, 1, 0], P: [20, 0, 20], a: 1 }); // out of every light's reach but the moon
    const pl = resolvePalettelight({ moon: E / 0.7, ambient: 0 }); let sum = 0, n = 0;
    for (let y = 0; y < 8; y++) for (let x = 0; x < 8; x++) { const q = { ...p0, ix: x, iy: y }; sum += pl.palette.entries[indexOf(pl, q, 0)].step; n++; }
    const t = tonality(pl, p0), want = Math.min(pl.steps - 1, t.s);
    assert.ok(Math.abs(sum / n - want) <= 1 / 64 + 1e-9 || want >= pl.steps - 1.0001 || want <= 1e-6, `E ${E}: mean ${sum / n} want ${want}`);
  }
});
test("palette cycling: a time shift of one ramp step rotates every void index by exactly one", () => {
  const dt = 1 / plan.cycleRate / plan.voidSteps; let same = 0, n = 0;
  for (let k = 0; k < 200; k++) {
    const p = px({ cls: CLS.void, phase: (k * 0.0377) % 1, ix: k % 29, iy: (k * 7) % 31 }), a = plan.palette.entries[indexOf(plan, p, 0.3)], b = plan.palette.entries[indexOf(plan, p, 0.3 + dt)];
    n++; if (b.step === (a.step + 1) % plan.voidSteps) same++;
  }
  assert.equal(same, n);
});
test("a warm light shifts the hue of the stone it lights; the continuous ideal agrees in direction", () => {
  const hue = (lab) => Math.atan2(lab[2], lab[1]);
  const moonOnly = idealLab(plan, px({ vis: [1, 0, 0], P: [20, 0, 20] })), lit = idealLab(plan, px({ vis: [0, 1, 0], P: [5.2, 0, 5.8] }));
  assert.ok(hue(lit) !== hue(moonOnly)); const dh = ((hue(lit) - hue(moonOnly)) * 180) / Math.PI; assert.ok(Math.abs(dh) > 10, `hue shift ${dh}`);
  const w = tonality(plan, px({ vis: [0, 1, 0], P: [5.2, 0, 5.8] })); assert.ok(w.w > 0.7, `warm fraction ${w.w}`); void irradiance;
});
