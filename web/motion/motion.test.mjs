// node --test web/motion/motion.test.mjs
// The pure half of Motion: path parsing and flattening, trims, morph
// correspondence, text and TeX layout, timeline maths and the display list
// compiler. The GPU half is exercised by scripts/motion_render.py.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { parsePath, circle, rect, bounds, area, length, trim, resample, lengths } from "./path.mjs";
import { morph } from "./morph.mjs";
import { text, tex, fmt } from "./text.mjs";
import { ease, span, track, timeline, narrationCues, countUp, lerpLog, rng } from "./timeline.mjs";
import { compile, rgba, BAND } from "./vector.mjs";
import { frameTimes } from "./scene.mjs";
import { sayTimeline } from "./narration.mjs";
import { plan, wrap } from "./walkthrough.mjs";

const atlas = JSON.parse(readFileSync(new URL("../../docs/motion/hanken-grotesk-500.atlas.json", import.meta.url), "utf8"));
const near = (a, b, e = 1e-3) => assert.ok(Math.abs(a - b) <= e, `${a} != ${b} (+/- ${e})`);

test("parsePath reads absolute, relative, implicit and closed subpaths", () => {
  const s = parsePath("M0 0 L10 0 10 10 Z m 20 0 h 5 v 5 z M 0 50 l 10 0");
  assert.equal(s.length, 3);
  assert.deepEqual([...s[0].pts], [0, 0, 10, 0, 10, 10]);
  assert.equal(s[0].closed, true);
  assert.deepEqual([...s[1].pts], [20, 0, 25, 0, 25, 5]);
  assert.equal(s[2].closed, false);
});

test("a cubic flattens within tolerance and ends on its end point", () => {
  const s = parsePath("M0 0 C 0 100 100 100 100 0", { tolerance: 0.1 })[0];
  const p = s.pts, n = p.length / 2;
  near(p[2 * n - 2], 100); near(p[2 * n - 1], 0);
  // The curve's peak is at t = 0.5: y = 75.
  let ymax = 0;
  for (let i = 1; i < p.length; i += 2) ymax = Math.max(ymax, p[i]);
  near(ymax, 75, 0.15);
});

test("arcs: a full circle from two arcs has the circle's length", () => {
  const s = parsePath("M 100 50 A 50 50 0 0 1 0 50 A 50 50 0 0 1 100 50 Z", { tolerance: 0.01 });
  near(length(s), 2 * Math.PI * 50, 0.5);
});

test("area sign separates outlines from holes; bounds are exact for a rect", () => {
  const r = rect(10, 20, 30, 40)[0];
  near(area(r), 1200);
  const b = bounds([r]);
  assert.deepEqual([b.x0, b.y0, b.x1, b.y1], [10, 20, 40, 60]);
  const g = text("0", { atlas, size: 100 }).shape;
  assert.equal(g.length, 2);
  assert.equal(g.filter((c) => area(c) < 0).length, 1, "the zero has one hole");
});

test("trim draws on by arc length", () => {
  const s = [parsePath("M0 0 L100 0")[0]];
  near(length(trim(s, 0, 0.25)), 25);
  near(length(trim(s, 0.5, 1)), 50);
  assert.equal(trim(s, 0.6, 0.6).length, 0);
});

test("resample spaces points evenly along a closed contour", () => {
  const r = resample(rect(0, 0, 100, 100)[0], 40);
  assert.equal(r.pts.length, 80);
  const L = lengths(r);
  for (let i = 1; i < L.length; i++) near(L[i] - L[i - 1], 10, 1e-3);
});

test("morph: endpoints are the shapes, the middle lies between, holes stay holes", () => {
  const m = morph(circle(0, 0, 100), rect(-50, -50, 100, 100));
  const a = bounds(m(0)), b = bounds(m(1)), c = bounds(m(0.5));
  near(a.w, 200, 0.5); near(b.w, 100, 0.5);
  assert.ok(c.w < a.w && c.w > b.w);
  const t1 = text("o", { atlas, size: 100 }).shape, t2 = text("8", { atlas, size: 100 }).shape;
  const mm = morph(t1, t2);
  for (const u of [0, 0.3, 0.7, 1]) {
    const holes = mm(u).filter((cc) => area(cc) < -1).length;
    assert.ok(holes >= 1, `holes at ${u}`);
  }
  near(bounds(mm(1)).w, bounds(t2).w, 0.5);
});

test("morph pairs differing contour counts by growing from points", () => {
  const two = [...circle(0, 0, 10), ...circle(100, 0, 10)];
  const m = morph(circle(50, 0, 20), two);
  assert.equal(m.pairs, 2);
  near(bounds(m(1)).w, 120, 0.5);
});

test("text: width scales with size and kerning is applied", () => {
  const a = text("Checking", { atlas, size: 50 }), b = text("Checking", { atlas, size: 100 });
  near(b.width, 2 * a.width, 1e-6);
  const right = text("AV", { atlas, size: 100, x: 500, anchor: "right" });
  near(bounds(right.shape).x1, 500, 15);
  assert.equal(fmt(130800757), "130,800,757");
  assert.equal(fmt(14931.6, 1), "14,931.6");
});

test("tex lays out fractions, scripts and operators", () => {
  const e = tex("\\frac{1}{2} \\times 10^{8} \\approx 5", { atlas, size: 60 });
  const b = bounds(e.shape);
  assert.ok(b.h > 60, "a fraction is taller than a line");
  assert.ok(e.width > 200);
  assert.throws(() => tex("\\unknowncommand", { atlas }), /unsupported/);
});

test("easing, spans, tracks and cues", () => {
  near(ease.inOut(0.5), 0.5); assert.equal(ease.out(2), 1); assert.equal(ease.in(-1), 0);
  near(span(5, 4, 6, ease.linear), 0.5);
  const tr = track([[0, 0], [1, 10, ease.linear], [3, [0, 0]]].slice(0, 2));
  near(tr(0.5), 5); assert.equal(tr(5), 10);
  const tl = timeline({ a: 1, b: 3 });
  near(tl.span(2, "a", "b", ease.linear), 0.5);
  assert.throws(() => tl.at("nope"), /no cue/);
  const cues = narrationCues([{ segment: 2, line: 1, sentence: 0, start: 36.02, end: 40.92 }]);
  assert.equal(cues["s2.l1.0"], 36.02); assert.equal(cues["s2.l1.0.end"], 40.92);
  near(lerpLog(1, 100, 0.5), 10, 1e-9);
  assert.equal(countUp(10, 0, 5, 1, 130800757), 130800757);
  const r1 = rng(5), r2 = rng(5);
  assert.equal(r1(), r2());
});

test("frame times are exact multiples of 1 / fps", () => {
  const f = frameTimes({ duration: 1, fps: 30 });
  assert.equal(f.length, 30);
  assert.equal(f[29].t, 29 / 30);
});

test("the compiler bins a shape into bands and culls what is off screen", () => {
  const items = [
    { shape: rect(100, 100, 200, 64), fill: "#ffffff" },
    { shape: rect(5000, 100, 10, 10), fill: "#ffffff" },
    { shape: circle(960, 540, 50), stroke: "#e29472", width: 2 },
  ];
  const c = compile(items, {}, 1920, 1080);
  assert.equal(c.stats.items, 2);
  assert.equal(c.stats.culled, 1);
  const bands = Math.ceil((64 + 2 * 4) / BAND) + 1;
  assert.ok(c.stats.instances >= bands);
  assert.deepEqual(rgba("#ff000080").map((v) => +v.toFixed(3)), [0.502, 0, 0, 0.502]);
  // Every index points at a real segment.
  for (const i of c.idx) assert.ok(i < c.segs.length / 4);
});

test("the camera scales scene units to pixels and depth adds parallax and defocus", () => {
  const it = { shape: rect(950, 530, 20, 20), fill: "#fff" };
  const a = compile([it], { zoom: 2 }, 1920, 1080);
  const f = new Float32Array(a.inst.buffer, a.inst.byteOffset, 20);
  assert.ok(f[2] - f[0] > 40, "zoom 2 doubles the size");
  const deep = compile([{ ...it, z: 1000 }], { aperture: 20 }, 1920, 1080);
  const g = new Float32Array(deep.inst.buffer, deep.inst.byteOffset, 20);
  assert.ok(g[13] > 5, "a shape off the focal plane is blurred");
});

test("narration timing: from words without a recording, from the recording with one", () => {
  const a = sayTimeline(["one two three four five", "six"], { lead: 1 });
  near(a.cues[0].start, 1); assert.ok(a.cues[1].start > a.cues[0].end);
  const b = sayTimeline(["x", "y"], { timing: [{ segment: 0, start: 2, end: 3 }, { segment: 1, start: 4, end: 6.5 }] });
  assert.equal(b.cues[1].start, 4); assert.equal(b.cues[1].end, 6.5); assert.ok(b.recorded);
  assert.match(b.vtt(), /^WEBVTT\n\n00:00:02.000 --> 00:00:03.000\nx\n/);
});

test("walkthrough plan: steps in order, long output elided, marked lines lit", () => {
  const spec = { title: "t", steps: [{ say: "Build it.", keep: 2 }, { say: "Run it.", highlight: "^ok" }] };
  const cast = { steps: [{ cmd: "make", lines: [1, 2, 3, 4].map((i) => ({ t: i * 0.1, text: "line " + i })), exit: 0 },
    { cmd: "run", lines: [{ t: 0.2, text: "ok: done" }, { t: 0.3, text: "other" }], exit: 0 }] };
  const p = plan(spec, cast);
  assert.equal(p.steps.length, 2);
  assert.ok(p.steps[1].start >= p.steps[0].end);
  assert.equal(p.steps[0].lines.length, 3);
  assert.match(p.steps[0].lines[0].text, /2 more lines/);
  assert.deepEqual(p.steps[1].lines.map((l) => l.lit), [true, false]);
  assert.ok(p.duration > p.steps[1].end);
  assert.deepEqual(wrap("aa bb cc dd", 5), ["aa bb", "cc dd"]);
  const shown = plan({ title: "t", steps: [{ say: "x", show_exit: true }] }, { steps: [{ cmd: "c", lines: [{ t: 0.1, text: "out" }], exit: 3 }] });
  assert.equal(shown.steps[0].lines.at(-1).text, "exit 3");
});

test("audio: superstack.sound/1 quantize and loudness vectors, and the shared mix fixture", async () => {
  const { quantizeS16, integratedLufs, peakDbfs, mixTracks, kWeighting } = await import("./audio.mjs");
  const { createHash } = await import("node:crypto");
  const V = JSON.parse(readFileSync(new URL("../../third_party/superstack/vectors/sound.json", import.meta.url), "utf8"));
  for (const c of V.quantize) assert.equal(quantizeS16([c.in])[0], c.out, `quantize ${c.in}`);
  for (const [rate, bq] of Object.entries(V.k_weighting))
    kWeighting(Number(rate)).forEach((f, i) => f.forEach((x, j) => near(x, bq[i][j], V.k_tolerance * 10)));
  for (const v of V.loudness) {
    const n = v.frames, ch = v.channels, s = new Float64Array(n * ch);
    for (let i = 0; i < n; i++) for (let c = 0; c < ch; c++) {
      if (v.kind === "silence") continue;
      let amp = v.amp[c];
      if (v.kind === "gated" && i >= Math.floor(n / 2)) amp *= v.quiet_gain;
      s[i * ch + c] = amp * Math.sin(2 * Math.PI * v.freq * i / v.rate);
    }
    const l = integratedLufs(s, v.rate, ch);
    if (v.integrated_lufs === null) assert.equal(l, null, v.name); else near(l, v.integrated_lufs, v.tolerance_lu);
    if (v.peak_dbfs === null) assert.equal(peakDbfs(s), null); else near(peakDbfs(s), v.peak_dbfs, 1e-9);
  }
  const F = JSON.parse(readFileSync(new URL("../../tests/web/audio_mix_fixture.json", import.meta.url), "utf8"));
  const a = new Float64Array(F.frames), b = new Float64Array(F.frames * 2);
  for (let i = 0; i < F.frames; i++) {
    a[i] = ((i * 37) % 65536 - 32768) / 32768 * 0.6;
    b[2 * i] = ((i * 101) % 4096 - 2048) / 2048 * 0.3; b[2 * i + 1] = ((i * 7) % 1000 - 500) / 500 * 0.25;
  }
  const mix = mixTracks([{ samples: a, channels: 1, gain: 0.7071 }, { samples: b, channels: 2, gain: 0.5, offset: 1000 }], { channels: 2, frames: F.frames });
  const pcm = quantizeS16(mix);
  assert.equal(createHash("sha256").update(Buffer.from(pcm.buffer)).digest("hex"), F.pcm_sha256, "JS mix differs from the Python mix");
  near(integratedLufs(mix, F.rate, 2), F.integrated_lufs, 1e-9);
});
