// SPDX-License-Identifier: FSL-1.1-MIT
// Sound engine tests: the contract vector, the meters against BS.1770 and EBU
// Tech 3341/3342 cases, determinism, block-size independence, the float32
// live path, seeking, mastering to target and the true-peak ceiling.
//   node --test web/sound/sound.test.mjs
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import * as ss from "../../third_party/superstack/superstack.mjs";
import * as M from "./meter.mjs";
import { renderContract } from "./contract.mjs";
import { Mix } from "./mix.mjs";
import { Master } from "./master.mjs";
import { synth, CUE_TYPES } from "./cues.mjs";
import { panGains } from "./dsp.mjs";
import { validateSheet, interleaveS16 } from "./sheet.mjs";
import { resolve, report } from "./offline.mjs";
import { speechLike, testSheet } from "./fixtures.mjs";
import { sceneSheet } from "./scene-sheet.mjs";
import { fileURLToPath } from "node:url";

const here = (p) => new URL(p, import.meta.url);
const R = 48000;
const FIXTURE_PCM = "92a8c2ce93e485fe228ba5167e62c4043e0938e23571e54b30ef408aeac39dcb";
const tone = (secs, dbfs, f = 1000, rate = R) => { const a = 10 ** (dbfs / 20), n = Math.round(secs * rate), x = new Float64Array(n); for (let i = 0; i < n; i++) x[i] = a * Math.sin((2 * Math.PI * f * i) / rate); return x; };
const cat = (...xs) => { const n = xs.reduce((s, x) => s + x.length, 0), o = new Float64Array(n); let k = 0; for (const x of xs) { o.set(x, k); k += x.length; } return o; };
const st = (x) => [x, x];

test("the mixer reproduces the superstack.sound/1 example PCM bit for bit", () => {
  const scene = JSON.parse(readFileSync(here("../../third_party/superstack/examples/sound/sound.json"), "utf8"));
  for (const block of [128, 4096]) assert.equal(ss.sha256(ss.quantizeS16(renderContract(scene, block))), "692ead20dfdc1665251a0010a0fea40065adb46c03e841be38b32abe4aa2ac7b");
});

test("integrated loudness matches every superstack loudness vector", () => {
  const v = JSON.parse(readFileSync(here("../../third_party/superstack/vectors/sound.json"), "utf8"));
  for (const c of v.loudness) {
    const chans = c.amp.map((amp) => { const x = new Float64Array(c.frames); for (let i = 0; i < c.frames; i++) {
      const s = c.kind === "silence" ? 0 : amp * Math.sin((2 * Math.PI * c.freq * i) / c.rate);
      x[i] = c.kind === "gated" && i >= c.frames / 2 ? s * c.quiet_gain : s; } return x; });
    const got = M.integrated(chans, c.rate);
    if (c.integrated_lufs === null) assert.equal(got ?? null, null, c.name);
    else assert.ok(Math.abs(got - c.integrated_lufs) <= c.tolerance_lu, `${c.name}: ${got}`);
  }
});

test("EBU Tech 3341 cases 1-5, 9 and 12 read -23.0 within 0.1 LU (in-phase stereo 1 kHz)", () => {
  const c1 = tone(20, -23), c2 = tone(20, -33);
  assert.ok(Math.abs(M.integrated(st(c1), R) + 23) <= 0.1);
  assert.ok(Math.abs(M.integrated(st(c2), R) + 33) <= 0.1);
  for (const v of [...M.momentary(st(c1), R), ...M.shortTerm(st(c1), R)]) assert.ok(Math.abs(v + 23) <= 0.1);
  const c3 = cat(tone(10, -36), tone(60, -23), tone(10, -36));
  const c4 = cat(tone(10, -72), tone(10, -36), tone(60, -23), tone(10, -36), tone(10, -72));
  const c5 = cat(tone(20, -26), tone(20.1, -20), tone(20, -26));
  for (const [x, name] of [[c3, 3], [c4, 4], [c5, 5]]) assert.ok(Math.abs(M.integrated(st(x), R) + 23) <= 0.1, `case ${name}`);
  const rep = (n, parts) => cat(...Array.from({ length: n }, () => parts.map(([s, d]) => tone(s, d))).flat());
  const c9 = rep(5, [[1.34, -20], [1.66, -30]]), c12 = rep(25, [[0.18, -20], [0.22, -30]]);
  for (const v of M.shortTerm(st(c9), R)) assert.ok(Math.abs(v + 23) <= 0.1, `case 9 short-term ${v}`);
  for (const v of M.momentary(st(c12), R)) assert.ok(Math.abs(v + 23) <= 0.1, `case 12 momentary ${v}`);
});

test("EBU Tech 3341 true-peak cases 15-19 read within +0.2/-0.4 dB", () => {
  const sine = (div, amp, phaseDeg) => { const n = R, x = new Float64Array(n), f = 480, ph = (phaseDeg * Math.PI) / 180;
    for (let i = 0; i < n; i++) { const fade = Math.min(1, i / f, (n - 1 - i) / f); x[i] = fade * amp * Math.sin((2 * Math.PI * i) / div + ph); } return x; };
  for (const [div, amp, ph, want] of [[4, 0.5, 0, -6.0], [4, 0.5, 45, -6.0], [6, 0.5, 60, -6.0], [8, 0.5, 67.5, -6.0], [4, 1.41, 45, 3.0]]) {
    const tp = M.truePeak(st(sine(div, amp, ph)));
    assert.ok(tp - want <= 0.2 && want - tp <= 0.4, `fs/${div} ${amp} ${ph}deg: ${tp}`);
  }
});

test("EBU Tech 3342 loudness range cases 1-4 read within 1 LU", () => {
  for (const [levels, want] of [[[-20, -30], 10], [[-20, -15], 5], [[-40, -20], 20], [[-50, -35, -20, -35, -50], 15]]) {
    const lra = M.loudnessRange(st(cat(...levels.map((d) => tone(20, d)))), R);
    assert.ok(Math.abs(lra - want) <= 1, `${levels}: ${lra}`);
  }
});

test("equal-power pan keeps power constant and puts the centre at -3.01 dB", () => {
  for (let p = -1; p <= 1; p += 0.125) { const [l, r] = panGains(p); assert.ok(Math.abs(l * l + r * r - 1) < 1e-12); }
  assert.ok(Math.abs(20 * Math.log10(panGains(0)[0]) + 3.0103) < 1e-3);
});

test("every cue type synthesises a finite sound with peak 1, and a texture caps its grains", () => {
  for (const type of CUE_TYPES) {
    const { mono } = synth({ type, dur: 1, n: 1e6 }, ss.rng("t/" + type), R);
    let peak = 0;
    for (const v of mono) { assert.ok(Number.isFinite(v)); peak = Math.max(peak, Math.abs(v)); }
    assert.ok(Math.abs(peak - 1) < 1e-12, type);
  }
  assert.ok(validateSheet({ ...testSheet(), cues: [{ at: 10, type: "land" }] }).some((e) => e.includes("why")));
});

test("rendering is deterministic and independent of block size", () => {
  const sheet = resolve(testSheet(), speechLike()).sheet, nar = speechLike();
  const hashes = [4096, 128, 777].map((b) => ss.sha256(interleaveS16(new Mix(sheet, { narration: nar }).renderAll(b))));
  assert.equal(new Set(hashes).size, 1, hashes.join(" "));
  assert.equal(ss.sha256(interleaveS16(new Mix(sheet, { narration: nar }).renderAll())), hashes[0]);
  // Pinned on Windows with Node 25.2. CI checks it on Ubuntu and Windows,
  // so a change in any platform's float results shows up here as a hash change.
  assert.equal(hashes[0], FIXTURE_PCM);
});

test("the float32 live path and a seek stay within the PCM tolerance of the offline render", () => {
  const sheet = resolve(testSheet(), speechLike()).sheet, nar = speechLike();
  const ref = new Mix(sheet, { narration: nar }).renderAll();
  const live = new Mix(sheet, { narration: nar }), L = new Float32Array(ref[0].length), Rr = new Float32Array(ref[0].length);
  for (let i = 0; i < L.length; i += 128) live.process(L.subarray(i, i + 128), Rr.subarray(i, i + 128), Math.min(128, L.length - i));
  const rec = ss.reconcilePcmS16(interleaveS16(ref), interleaveS16([L, Rr]));
  assert.equal(rec.tolerance.verdict, "verified");
  assert.ok(rec.tolerance.metrics.max_abs_lsb <= 1);
  const at = 5 * R, n = 2 * R, sk = new Mix(sheet, { narration: nar });
  sk.seek(at);
  const a = new Float64Array(n), b = new Float64Array(n);
  sk.process(a, b, n);
  const r2 = ss.reconcilePcmS16(interleaveS16([ref[0].subarray(at, at + n), ref[1].subarray(at, at + n)]), interleaveS16([a, b]));
  assert.equal(r2.tolerance.verdict, "verified");
});

test("mastering hits -16 LUFS within 0.1 LU under a -1.5 dBTP ceiling, music ducked under speech", () => {
  const nar = speechLike(), { sheet } = resolve(testSheet(), nar);
  const mix = new Mix(sheet, { narration: nar }).renderAll();
  const rep = report(sheet, nar, mix, [[R * 1, R * 4], [R * 6, R * 9]]);
  assert.ok(Math.abs(rep.integrated_lufs + 16) <= 0.1, String(rep.integrated_lufs));
  assert.ok(rep.true_peak_dbtp <= -1.5, String(rep.true_peak_dbtp));
  assert.equal(rep.loudness_verdict, "verified");
  assert.ok(rep.music_under_speech_lu >= 16, String(rep.music_under_speech_lu));
  assert.ok(rep.sfx_peak_under_dialog_peak_db >= 6, String(rep.sfx_peak_under_dialog_peak_db));
});

test("the limiter holds a +3 dBTP signal under its ceiling", () => {
  const m = new Master({ eq: [], comp: null, gain_db: 0, ceiling_dbtp: -1 }, R), n = R;
  const L = new Float64Array(n), Rr = new Float64Array(n), io = [0, 0];
  for (let i = 0; i < n + m.latency; i++) {
    const x = i < n ? 1.41 * Math.sin((Math.PI * i) / 2 + Math.PI / 4) * Math.min(1, i / 480) : 0;
    io[0] = x; io[1] = x; m.step(io);
    if (i >= m.latency) { L[i - m.latency] = io[0]; Rr[i - m.latency] = io[1]; }
  }
  assert.ok(M.truePeak([L, Rr]) <= -1, String(M.truePeak([L, Rr])));
  // Control: the same signal with the limiter bypassed must read over the ceiling,
  // or this test could pass without the limiter doing anything.
  const raw = new Float64Array(n);
  for (let i = 0; i < n; i++) raw[i] = 1.41 * Math.sin((Math.PI * i) / 2 + Math.PI / 4) * Math.min(1, i / 480);
  assert.ok(M.truePeak([raw, raw]) > 2.5);
});

test("a scene's own sound() cues and chapters become a sheet, on exact samples", async () => {
  const sheet = await sceneSheet(fileURLToPath(new URL(".", import.meta.url)), "fixture.scene.mjs");
  assert.deepEqual(validateSheet(sheet), []);
  assert.equal(sheet.duration_samples, 6 * R);
  assert.deepEqual(sheet.cues.map((c) => c.at), [48000, 120000, 145600]); // 3.0333 s is frame 91 at 30 fps: 91 x 1,600 samples
  assert.deepEqual(sheet.score.sections.map((s) => s.at), [0, 96000, 192000]);
});
