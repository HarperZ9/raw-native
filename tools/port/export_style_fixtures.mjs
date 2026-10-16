// Exports the fixtures and the JavaScript float64 reference outputs of the ported style passes, for
// the C++ core's CPU link (ADR 0019, evidence/cpp-port-style-bounds.json).
//   node tools/port/export_style_fixtures.mjs <out dir>
// Writes <out>/ditherlight.rnfx, <out>/paintover.rnfx, <out>/lookkeys.json and, from round 2
// (evidence/cpp-port-style-bounds-2.json), <out>/poststack.rnfx and <out>/<pass>.rnfx for purity,
// lightpaint, glaze and hatch, and (evidence/cpp-port-style-bounds-3.json) hysteresis and palettelight. The cases are the
// ones of the web parity bounds files, built with the web modules exactly as the web parity pages
// build them (web/test/shaders-ditherlight.html, shaders-paintover.html). Nothing under web/ is
// changed. The reference functions are called per pixel (toneOf, shadePoint) so their float64
// results are exported before any Float32Array rounds them.
//
// An .rnfx file: the 8 bytes "RNFX0001", a little-endian u32 header length, the header as UTF-8
// JSON, zero padding to a multiple of 8, then the arrays. The header names each array with its
// type (u8, u32, f32, f64), element count and byte offset from the start of the array block.
import { mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { resolveDitherlight, toneOf, quantise, levelRgb8, runDitherlight } from "../../web/shaders/ditherlight/ditherlight.mjs";
import { anchorCell, thresholdAt } from "../../web/shaders/ditherlight/thresholds.mjs";
import { renderFrame, pathSpecs, lightsOf, FOV } from "../../web/shaders/fixtures/dream.mjs";
import { renderFrame2, lightsOf2 } from "../../web/shaders/fixtures/dream2.mjs";
import { isoCamera } from "../../web/shaders/paintover/camera.mjs";
import { makeProjector, shadePoint, reaches, KEYFRAME } from "../../web/shaders/paintover/projector.mjs";
import { paint } from "../../web/shaders/paintover/painter.mjs";
import { renderGbuf } from "../../web/shaders/fixtures/harbour-render.mjs";
import { createLookKeys, resolveSlots } from "../../web/look/keys.mjs";
import { Bundle } from "./rnfx.mjs";
import { postStack } from "./fixtures_post.mjs";
import { purity, lightpaint, glaze, hatch, adjacency } from "./fixtures_image.mjs";
import { hysteresis, palettelight, dither } from "./fixtures_quantise.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const readJson = (p) => JSON.parse(readFileSync(join(ROOT, p), "utf8"));


// Dither lighting: the cases of the web bounds file; tone in float64 from toneOf.
function ditherlight(out) {
  const bounds = readJson("evidence/shaders-ditherlight-parity-bounds.json"), { width: W, height: H } = bounds.output, B = new Bundle("ditherlight");
  B.head.cases = [];
  for (const c of bounds.cases) {
    // as web/test/shaders-ditherlight.html builds each case
    let spec, g, lights;
    if (c.scene === "v2") { spec = { cam: c.cam, beamAngle: c.beamAngle, flicker: 0.4, time: 10, spin: c.spin }; g = renderFrame2(spec, W, H); lights = lightsOf2(spec); }
    else { spec = pathSpecs(c.path, c.frame + 1, { fixed: !c.sweep, lanternAttached: !!c.attached })[c.frame]; g = renderFrame(spec, W, H); lights = lightsOf(spec); }
    const plan = resolveDitherlight(c.preset, c.overrides, { height: H, fov: FOV }), official = runDitherlight(plan, lights, g);
    const n = W * H, tone = new Float64Array(n), level = new Uint8Array(n), rgba = new Uint8Array(n * 4), px = { vis: [0, 0, 0, 0], wpos: [0, 0, 0], nrm: [0, 0, 1], apos: [0, 0, 0], anrm: [0, 0, 1] };
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const i = y * W + x;
      px.kind = g.kind[i]; px.albedo = g.albedo[i]; px.depth = g.depth[i]; px.haze = g.haze ? g.haze[i] : 0; px.ix = x; px.iy = y;
      for (let k = 0; k < 3; k++) { px.wpos[k] = g.wpos[i * 3 + k]; px.nrm[k] = g.nrm[i * 3 + k]; px.apos[k] = g.apos[i * 3 + k]; px.anrm[k] = g.anrm[i * 3 + k]; }
      for (let k = 0; k < 4; k++) px.vis[k] = g.vis[i * 4 + k];
      const Y = toneOf(plan, lights, px), [cx, cy] = anchorCell(plan, px), k = quantise(plan, Y, thresholdAt(plan.source, cx, cy));
      tone[i] = Y; level[i] = k; rgba.set([...levelRgb8(plan, k), 255], i * 4);
      if (k !== official.level[i]) throw new Error(`exporter: per-pixel level differs from runDitherlight at ${c.id} ${i}`);
    }
    const p = `${c.id}.`;
    B.add(p + "albedo", "f32", g.albedo); B.add(p + "kind", "u8", g.kind); B.add(p + "depth", "f32", g.depth);
    B.add(p + "wpos", "f32", g.wpos); B.add(p + "nrm", "f32", g.nrm); B.add(p + "apos", "f32", g.apos); B.add(p + "anrm", "f32", g.anrm); B.add(p + "vis", "f32", g.vis); B.add(p + "haze", "f32", g.haze ?? new Float32Array(W * H));
    B.add(p + "js_tone", "f64", tone); B.add(p + "js_level", "u8", level); B.add(p + "js_rgba", "u8", rgba);
    B.head.cases.push({ id: c.id, preset: c.preset, overrides: c.overrides, width: W, height: H, fov: FOV, plan, lights });
  }
  B.write(join(out, "ditherlight.rnfx"));
  return B.head.cases.length;
}

const camJson = (c) => ({ ortho: c.ortho, pos: c.pos, fwd: c.fwd, right: c.right, up: c.up, scale: c.scale, fovY: c.fovY });
// Paint-over: the cases of the web bounds file; colour in float64 from shadePoint.
function paintover(out) {
  const bounds = readJson("evidence/shaders-paintover-parity-bounds.json"), { width: W, height: H } = bounds.output, PR = bounds.projectors, TARGET = [1, 1.5, 0], B = new Bundle("paintover");
  const projectors = PR.set.map(([dy, de], k) => {
    const cam = isoCamera({ yawDeg: 45 + dy, elevDeg: 35.264 + de, target: TARGET, scale: PR.scale }), g = renderGbuf(cam, PR.w, PR.h, { shadeOn: true });
    const p = paint({ w: PR.w, h: PR.h, rgb: g.shade }, { id: g.id, N: g.N }, { seed: PR.seeds[k] });
    B.add(`proj${k}.rgba`, "u8", p.rgba8); B.add(`proj${k}.depth`, "f32", g.depth);
    return makeProjector(cam, PR.w, PR.h, p.rgba8, g.depth);
  });
  B.head.projectors = projectors.map((p) => ({ cam: camJson(p.cam), w: p.w, h: p.h }));
  B.head.opt = KEYFRAME; B.head.cases = [];
  for (const c of bounds.cases) {
    const set = c.use.map((k) => projectors[k]), vc = c.view.ortho === false ? isoCamera({ yawDeg: 45 + c.view.dyaw, elevDeg: 35.264 + c.view.delev, target: TARGET, dist: 60, ortho: false, fovY: 0.5 }) : isoCamera({ yawDeg: 45 + c.view.dyaw, elevDeg: 35.264 + c.view.delev, target: TARGET, scale: 9 });
    const g = renderGbuf(vc, W, H, { shadeOn: false }), n = W * H, rgb = new Float64Array(n * 3), cov = new Uint8Array(n), hits = new Uint8Array(n), P = [0, 0, 0], N = [0, 0, 0];
    for (let i = 0; i < n; i++) {
      if (!g.hit[i]) continue;
      for (let k = 0; k < 3; k++) { P[k] = g.P[i * 3 + k]; N[k] = g.N[i * 3 + k]; }
      const s = shadePoint(set, P, N, vc, KEYFRAME);
      if (s.rgb) { cov[i] = 1; hits[i] = s.hit; for (let k = 0; k < 3; k++) rgb[i * 3 + k] = s.rgb[k]; }
    }
    const p = `${c.id}.`;
    B.add(p + "P", "f32", g.P); B.add(p + "N", "f32", g.N); B.add(p + "hit", "u8", g.hit);
    B.add(p + "js_rgb", "f64", rgb); B.add(p + "js_covered", "u8", cov); B.add(p + "js_hits", "u8", hits);
    B.head.cases.push({ id: c.id, use: c.use, width: W, height: H, view: camJson(vc), js_reach: reaches(set, KEYFRAME) });
  }
  B.write(join(out, "paintover.rnfx"));
  return B.head.cases.length;
}

// Look keys: the series of web/look/keys.test.mjs on an hour grid, and the refusals.
function lookkeys(out) {
  const exposure8 = [0.2, 0.15, 0.6, 1.0, 1.2, 0.9, 0.5, 0.25];
  const series = [
    { id: "exposure8", slots: 8, params: { exposure: exposure8, tint: exposure8.map((v) => [v, 1 - v / 2, 0.5]) } },
    { id: "six", slots: 6, params: { exposure: [0.2, 0.6, 1.0, 1.2, 0.5, 0.25] } },
    { id: "steps", slots: 8, params: { exposure: [0, 0, 1, 1, 0, 0, 1, 0] } },
    { id: "ramp", slots: 8, params: { rise: [0, 1, 2, 3, 4, 5, 6, 7] } },
    { id: "clamped", slots: 8, params: { e: { values: exposure8, min: 0.18, max: 1.1 } } },
    { id: "seven", slots: Array.from({ length: 7 }, (_, i) => ({ name: `s${i}`, hour: i * 3 })), params: { x: [3, 1, 4, 1, 5, 9, 2] } },
  ];
  const hours = []; for (let k = -200; k <= 2600; k++) hours.push(k / 100);
  const result = series.map((s) => {
    const keys = createLookKeys(s), slotHours = keys.slots.map((x) => x.hour), values = {};
    for (const name of keys.names) values[name] = hours.map((h) => keys.term(name, h));
    const atSlots = {}; for (const name of keys.names) atSlots[name] = slotHours.map((h) => keys.term(name, h));
    return { ...s, slotHours, values, atSlots };
  });
  const refusalInputs = [
    ...[0, 1, 3, 5].map((n) => ({ id: `too-few-${n}`, slots: Array.from({ length: n }, (_, i) => ({ name: `s${i}`, hour: i * 4 })), params: {} })),
    { id: "three", slots: 3, params: {} },
    { id: "non-increasing", slots: [0, 4, 6, 1, 12.5, 17, 19.5, 22].map((hour, i) => ({ name: `s${i}`, hour })), params: {} },
    { id: "wrong-count", slots: 8, params: { x: [1, 2, 3, 4, 5, 6] } },
    { id: "hour-24", slots: [0, 4, 6, 9, 12.5, 17, 19.5, 24].map((hour, i) => ({ name: `s${i}`, hour })), params: {} },
    { id: "ok-seven", slots: Array.from({ length: 7 }, (_, i) => ({ name: `s${i}`, hour: i * 3 })), params: { x: [1, 2, 3, 4, 5, 6, 7] } },
  ];
  const refusals = refusalInputs.map((r) => { try { createLookKeys(r); return { ...r, refused: false }; } catch { return { ...r, refused: true }; } });
  let unknownRefused = false; try { createLookKeys(series[0]).term("nope", 3); } catch { unknownRefused = true; }
  resolveSlots(8);
  writeFileSync(join(out, "lookkeys.json"), JSON.stringify({ pass: "lookkeys", hours, series: result, refusals, unknownTermRefused: unknownRefused }));
  return result.length;
}

const out = process.argv[2];
if (!out) { console.error("usage: node tools/port/export_style_fixtures.mjs <out dir>"); process.exit(2); }
mkdirSync(out, { recursive: true });
const t0 = Date.now(), counts = { ditherlight: ditherlight(out), paintover: paintover(out), lookkeys: lookkeys(out), poststack: postStack(out),
  purity: purity(out), lightpaint: lightpaint(out), glaze: glaze(out), hatch: hatch(out), hysteresis: hysteresis(out), palettelight: palettelight(out),
  dither: dither(out), adjacency: adjacency(out) };
// The file list, for hosts that copy the fixtures by name (web/test/texture-identity.html).
writeFileSync(join(out, "files.json"), JSON.stringify(readdirSync(out).filter((f) => f.endsWith(".rnfx") || f === "lookkeys.json").sort()));
console.log(JSON.stringify({ out, cases: counts, seconds: (Date.now() - t0) / 1000 }));
