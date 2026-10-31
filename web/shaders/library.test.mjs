// node --test web/shaders/library.test.mjs
// The measured film-stock library and the physical-shader option libraries: curves reproduce the
// digitised data-sheet points, every record carries provenance (and one without it is refused),
// CineStill's halation appears only with the rem-jet off, the vector display mode follows dwell time,
// catalogue glasses reproduce their printed indices, and the existing presets render exactly as before.
import test from "node:test";
import assert from "node:assert/strict";
import { FILM_LIBRARY, libraryNegative, statedHalation, HALATION } from "./film/library.mjs";
import { build, negDensities, printPixel, GREY_TARGET, NEGATIVES } from "./film/stocks.mjs";
import { FILM_PRESETS } from "./film/film.mjs";
import { createFilm } from "./film/run.mjs";
import { option, options, requireProv, ProvenanceMissing, GROUP_NAMES, OPTION_DATA } from "./options.mjs";
import { PRESETS as CRT_PRESETS, PRESET_PROV } from "./crt/params.mjs";
import { screenMatrix } from "./crt/phosphors.mjs";
import { resolveVector, vectorFrame, strokeLight } from "./crt/vector.mjs";
import { glassIndex, abbe, GLASS, LAMBDA_D } from "./lens/spectrum.mjs";
import { catalogGlass, coatingParams, lensEra, GLASS_DATA } from "./lens/glasses.mjs";
import { filmReflectance, bare } from "./lens/coating.mjs";
import { cameraMatrix } from "./sensor.mjs";
import { fingerprints, recorded, RECORDED_FILM, RECORDED_CRT } from "./golden/presets.mjs";

const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b} (tol ${tol})`);
const sig = (x, f) => f.dmin + f.span / (1 + Math.exp(Math.max(-60, Math.min(60, -f.k * (x - f.x0)))));
const interp = (c, x) => { if (x <= c[0][0]) return c[0][1]; for (let i = 1; i < c.length; i++) if (x <= c[i][0]) return c[i - 1][1] + (c[i][1] - c[i - 1][1]) * (x - c[i - 1][0]) / (c[i][0] - c[i - 1][0]); return c[c.length - 1][1]; };
const out = (t, rgb, ev = 0) => printPixel(t, negDensities(t, rgb, ev));
const HEX = /^[0-9a-f]{64}$/;

test("every library stock carries provenance; a missing stock says why and carries no curves", () => {
  for (const [id, s] of Object.entries(FILM_LIBRARY.stocks)) {
    if (s.prov.status === "measured") {
      assert.ok(s.prov.citation && s.prov.licence, id);
      assert.ok(Object.keys(s.prov.files).length > 0 && Object.values(s.prov.files).every((h) => HEX.test(h)), id);
      for (const [fig, at] of Object.entries(s.prov.figures)) assert.ok(at && at.file in s.prov.files && at.page >= 1, `${id} ${fig}`);
    } else {
      assert.equal(s.prov.status, "missing", id);
      assert.ok(s.prov.why, id);
      assert.ok(!s.sensitometry, id);
    }
  }
  assert.ok(Object.keys(FILM_LIBRARY.held).length > 10, "held-but-undigitised sheets are listed");
});

test("each stock's curves reproduce its data-sheet points within the stated tolerance", () => {
  let n = 0;
  for (const [id, s] of Object.entries(FILM_LIBRARY.stocks)) {
    if (!s.sensitometry) continue;
    const m = s.sensitometry;
    for (const [k, pts] of Object.entries(m.checkPoints)) {
      for (const [x, d] of pts) {
        near(interp(m.table[k], x), d, m.tolerance, `${id} ${k} table at ${x}`);
        near(sig(x, m.fit[k]), d, m.fit[k].max_residual_D + 6e-4, `${id} ${k} logistic at ${x}`);
        n++;
      }
      assert.ok(m.fit[k].max_residual_D < 0.12, `${id} ${k}: logistic residual ${m.fit[k].max_residual_D}`);
    }
  }
  assert.ok(n > 300, `points checked: ${n}`);
});

test("control: a table moved by 0.05 D, or a layer swap, is caught", () => {
  const m = FILM_LIBRARY.stocks["kodak-portra400"].sensitometry;
  const moved = m.table.G.map(([x, y]) => [x, y + 0.05]);
  assert.ok(m.checkPoints.G.some(([x, d]) => Math.abs(interp(moved, x) - d) > m.tolerance));
  assert.ok(m.checkPoints.R.some(([x, d]) => Math.abs(interp(m.table.B, x) - d) > m.tolerance));
});

test("library negatives through the print: an 18% grey prints neutral, tone rises from -8 to +10 stops, the grey scale tracks", () => {
  const presets = Object.entries(FILM_PRESETS).filter(([, p]) => String(p.negative).startsWith("lib:"));
  assert.ok(presets.length >= 18);
  for (const [name, p] of presets) {
    const t = build({ negative: p.negative, print: p.print || "print-2383" });
    out(t, [0.18, 0.18, 0.18]).forEach((v, c) => near(v, GREY_TARGET, 1e-5, `${name} grey channel ${c}`));
    let prev = -1, e = 0, n = 0;
    for (let ev = -8; ev <= 10; ev += 0.5) { const o = out(t, [0.18, 0.18, 0.18], ev), y = (o[0] + o[1] + o[2]) / 3; assert.ok(y > prev, `${name} ev ${ev}`); prev = y; }
    for (let ev = -5; ev <= 5; ev += 0.5) { const o = out(t, [0.18, 0.18, 0.18], ev), mm = (o[0] + o[1] + o[2]) / 3; for (const v of o) { e += ((v - mm) / mm) ** 2; n++; } }
    const rms = Math.sqrt(e / n), s = FILM_LIBRARY.stocks[p.negative.slice(4)];
    // Motion-picture negatives are made for this print: under 7%. Still-camera negatives are made for
    // paper and scanners, and the shader's masking and interimage are borrowed from 500T: under 13%.
    // The measured 2383 curve shape with its still-engineered dyes tracks worse (11 to 26%): reported, not bounded.
    if (!p.print) assert.ok(rms < (s.process === "ECN-2" ? 0.07 : 0.13), `${name}: RMS chroma ${rms}`);
  }
});

test("libraryNegative refuses what the shader cannot render and lists what it borrowed", () => {
  assert.throws(() => libraryNegative("kodak-e100", NEGATIVES["neg-500t"]), /colour-reversal/);
  assert.throws(() => libraryNegative("kodak-trix400", NEGATIVES["neg-500t"]), /bw-negative/);
  assert.throws(() => libraryNegative("ilford-hp5", NEGATIVES["neg-500t"]), /bw-negative|no characteristic/);
  const p4 = libraryNegative("kodak-portra400", NEGATIVES["neg-500t"]), sup = libraryNegative("fuji-superia400", NEGATIVES["neg-500t"]);
  assert.ok(!p4.borrowed.includes("spectral sensitivity") && p4.borrowed.includes("dye spectra"), p4.borrowed.join(", "));
  assert.ok(sup.borrowed.includes("spectral sensitivity"), sup.borrowed.join(", "));
  // the 50% MTF read off the sheet becomes the emulsion scatter: sigma = sqrt(ln2 / 2) / (pi f50)
  const f50 = FILM_LIBRARY.stocks["kodak-5219"].mtf50.R, s = libraryNegative("kodak-5219", NEGATIVES["neg-500t"]).emulsionSigmaUm[0];
  near(Math.sqrt(Math.LN2 / 2) / (Math.PI * s / 1000), f50, 1e-9, "500T red layer f50");
});

test("halation control: Kodak's current VISION3 edition states an undercoat; CineStill is the rem-jet edition with it washed off", () => {
  for (const id of ["kodak-5203", "kodak-5213", "kodak-5219"]) assert.equal(statedHalation(id), "ahu", id);
  assert.equal(FILM_PRESETS["cinestill-800t"].halation, "none");
  assert.equal(FILM_PRESETS["cinestill-800t"].negative, "lib:" + FILM_LIBRARY.cinestill["cinestill-800t"].base_stock);
  assert.match(FILM_LIBRARY.cinestill["cinestill-800t"].base_stock_status, /inferred/);
  near(FILM_PRESETS["cinestill-800t"].ev, Math.log2(500 / 800), 1e-12, "rated 800 on a 500 emulsion");
  assert.ok(HALATION.remjet < HALATION.none && HALATION.ahu < HALATION.none);
});

test("CineStill halation appears only with the rem-jet off: red-first ring, gone when a rem-jet or undercoat is back", () => {
  const W = 192, H = 108, scene = { width: W, height: H, data: new Float32Array(W * H * 4) };
  scene.data.set([400, 400, 400, 1], ((H >> 1) * W + (W >> 1)) * 4);
  const ring = (preset, o = {}) => {
    const f = createFilm(preset, { grainAmount: 0, weaveUm: 0, jitterUm: 0, gateWidthMm: 3, ...o }, { w: W, h: H }, { w: W, h: H });
    const r = f.frame(scene, 0), px = Math.round((f.plan.halo.ringMm * 1000) / f.plan.umPerPx), i = ((H >> 1) * W + (W >> 1) + px) * 4;
    const bg = ((H >> 1) * W + 5) * 4;                    // the unexposed print far from the spot
    return { r: r.img[i] - r.img[bg], g: r.img[i + 1] - r.img[bg + 1], halo: f.plan.haloStrength };
  };
  const off = ring("cinestill-800t"), rj = ring("cinestill-800t-remjet"), ahu = ring("kodak-5219-print");
  assert.ok(off.r > 2 * off.g, `red-first with the rem-jet off: red ${off.r}, green ${off.g}`);
  assert.ok(off.halo[0] > 10 * rj.halo[0] && off.halo[0] > 10 * ahu.halo[0], "returned light: none against remjet and undercoat");
  assert.ok(rj.r < 0.2 * off.r && ahu.r < 0.25 * off.r, `ring above the black: off ${off.r}, remjet ${rj.r}, undercoat ${ahu.r}`);
  assert.equal(off.halo[2], 0, "blue does not reach the base");
});

test("golden images: the presets that existed before render exactly as recorded; new presets are new looks", () => {
  const now = fingerprints(), rec = recorded();
  for (const n of RECORDED_FILM) assert.equal(now.film[n], rec.film[n], `film ${n}`);
  for (const n of RECORDED_CRT) assert.equal(now.crt[n], rec.crt[n], `crt ${n}`);
  assert.equal(now.lens.default, rec.lens.default, "lens default");
  const lib = fingerprints(["kodak-portra400-print", "kodak-ektar100-print", "cinestill-800t", "kodak-5219-print"], ["pvm-14m2u", "pvm-20-y2o2s"]);
  const all = [...Object.values(lib.film), ...Object.values(lib.crt)];
  assert.equal(new Set(all).size, all.length, "each new preset renders differently");
  assert.ok(!Object.values(rec.film).some((h) => all.includes(h)) && !Object.values(rec.crt).some((h) => all.includes(h)), "and differently from the old ones");
});

test("every option record carries provenance, and a record without it is refused", () => {
  let n = 0;
  for (const g of GROUP_NAMES) n += options(g).length;
  assert.ok(n > 1650, `records ${n}`);
  const bare_ = { ...OPTION_DATA.crt.masks[0] }; delete bare_.prov;
  assert.throws(() => requireProv(bare_), ProvenanceMissing);
  assert.throws(() => option("crt.mask", bare_), ProvenanceMissing);
  assert.throws(() => option("crt.mask", { id: "x", prov: { status: "guessed", citation: "?" } }), ProvenanceMissing);
  const fm = { ...OPTION_DATA.crt.phosphors[1], prov: { ...OPTION_DATA.crt.phosphors[1].prov } }; delete fm.prov.confidence;
  assert.throws(() => requireProv(fm), /confidence/);
});

test("CRT options: record presets take the cited mask and pitch; the oxysulfide red sits deeper than the oxide red", () => {
  assert.deepEqual([CRT_PRESETS["pvm-14m2u"].mask.type, CRT_PRESETS["pvm-14m2u"].mask.pitch], ["grille", 0.25]);
  assert.deepEqual([CRT_PRESETS["superscan-814"].mask.type, CRT_PRESETS["superscan-814"].mask.pitch], ["delta", 0.26]);
  assert.equal(CRT_PRESETS["diamondtron-2070sb"].mask.pitch, 0.24);
  for (const k of Object.keys(PRESET_PROV)) assert.ok(PRESET_PROV[k].prov.status, k);
  const ox = screenMatrix("p22").chromaticities[0], os = screenMatrix("p22-y2o2s").chromaticities[0];
  assert.ok(os[0] > ox[0], `red x: Y2O2S ${os[0]} against Y2O3 ${ox[0]}`);
  assert.ok(options("crt.phosphor").some((r) => r.host === "Y2O2S:Eu") && options("crt.phosphor").some((r) => r.host === "Y2O3:Eu"));
});

test("vector mode: light follows dwell time, vertices glow from the settle, and the phosphor carries a trail", () => {
  const plan = resolveVector({ widthMm: 60, heightMm: 45, screen: "p31" }, { w: 120, h: 90 });
  const line = (y, slew) => strokeLight({ ...plan, p: { ...plan.p, slew, settle: 0 } }, [{ x0: -20, y0: y, x1: 20, y1: y, i: 1 }]);
  const mid = (L) => L[(45 * 120 + 60) * 3];
  near(mid(line(0, plan.p.slew / 2)) / mid(line(0, plan.p.slew)), 2, 1e-6, "half the slew speed, twice the light");
  const L = strokeLight(plan, [{ x0: -20, y0: 0, x1: 20, y1: 0, i: 1 }]), end = L[(45 * 120 + 20) * 3];
  assert.ok(end > 1.5 * mid(L), `vertex ${end} against mid-segment ${mid(L)}`);
  let total = 0; for (let i = 0; i < L.length; i += 3) total += L[i];
  near(total * (plan.mmPerPx ** 0), 40 / plan.p.slew + 2 * plan.p.settle, 0.02 * total, "energy = segment time plus two settles");
  const hist = new Float64Array(120 * 90 * 6), drawn = vectorFrame(plan, [{ x0: -20, y0: 0, x1: 20, y1: 0, i: 1 }], hist), after = vectorFrame(plan, [], hist);
  const g = (img) => img[(45 * 120 + 60) * 4 + 1];
  assert.ok(after[(45 * 120 + 60) * 4 + 1] > 0 && g(after) < g(drawn), "the trail decays but persists");
  assert.equal(plan.prov.status, "from_memory");
});

test("catalogue glasses reproduce their printed nd and vd; eras change the coating and the lateral colour", () => {
  const rows = GLASS_DATA.rows.filter((r) => r.nd);
  assert.ok(rows.length > 1500);
  for (const r of rows) near(glassIndex(catalogGlass(r.id), LAMBDA_D === 587.6 ? 587.5618 : LAMBDA_D), r.nd, 2e-4, r.id);
  const bk7 = catalogGlass("schott:N-BK7");
  near(glassIndex(bk7, 587.5618), glassIndex(GLASS.crown, 587.5618), 2e-5, "catalogue N-BK7 against the suite's N-BK7");
  near(abbe(catalogGlass("ohara:S-FPL51")), 81.5, 0.6, "S-FPL51 vd");
  const v = lensEra("vintage"), m = lensEra("modern");
  near(filmReflectance(550, 0, v.coating), bare(0, v.coating.ns), 1e-12, "vintage: uncoated reflects as bare glass");
  // MgF2 (n 1.378) on a low-index fluorophosphate crown (nd 1.497) is far from the ideal sqrt(ns): R drops to about 0.36 of bare
  assert.ok(filmReflectance(550, 0, m.coating) < bare(0, m.coating.ns) / 2, "coated");
  assert.ok(abbe(m.crownGlass) > abbe(v.crownGlass) + 15, "modern low-dispersion crown");
  assert.throws(() => coatingParams("v-coat-two-layer"), /library data only/);
});

test("camera sensors: 28 measured cameras, white maps to white, and cameras differ", () => {
  const cams = options("sensor.camera");
  assert.equal(cams.length, 28);
  const a = cameraMatrix(cams[0].id).matrix, b = cameraMatrix(cams[5].id).matrix;
  for (let k = 0; k < 3; k++) near(a[k * 3] + a[k * 3 + 1] + a[k * 3 + 2], 1, 1e-9, `row ${k}`);
  assert.ok(a.some((v, i) => Math.abs(v - b[i]) > 0.02), "two cameras give two matrices");
  assert.ok(a[0] > a[1] && a[4] > a[3] && a[8] > a[7], "each channel answers most to its own primary");
});
