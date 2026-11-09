// Writes parity vectors for raw/scene/clip_phase (the C++ port of web/anim/clip.mjs layout,
// keyTime, phaseAt and warp), from this repository's own JS reference.
//
//   node tools/action/clip_vectors.mjs > tests/data/action/clip-warp-vectors.json
//
// Cases pair a move's phase layout with an art clip's layout (and the reverse), including missing
// phases, a zero-length phase and an unphased clip; each case samples frames from -1 past the end in
// steps of 0.37 and the phase boundaries exactly. JSON numbers round-trip doubles exactly.
import { layout, keyTime, phaseAt, warp } from "../../web/anim/clip.mjs";

const P = (phases, lengths) => ({ phases, lengths });
const CASES = [
  [P(["startup", "active", "recovery"], { startup: 21, active: 6, recovery: 27 }), P(["startup", "active", "recovery"], { startup: 9, active: 3, recovery: 14 })],
  [P(["startup", "active", "recovery"], { startup: 9, active: 3, recovery: 14 }), P(["startup", "active", "recovery"], { startup: 21, active: 6, recovery: 27 })],
  [P(["startup", "sweep", "active", "recovery"], { startup: 13, sweep: 8, active: 2, recovery: 30 }), P(["startup", "active", "recovery"], { startup: 22, active: 4, recovery: 31 })],
  [P(["startup", "active", "recovery"], { startup: 18, active: 0, recovery: 40 }), P(["startup", "active", "recovery"], { startup: 7, active: 5, recovery: 12 })],
  [P(["windup", "strike"], { windup: 30, strike: 6 }), P(["startup", "active", "recovery"], { startup: 30, active: 6, recovery: 20 })],
  [{ frames: 54 }, P(["startup", "active", "recovery"], { startup: 21, active: 6, recovery: 27 })],
];
const lay = (c) => (c.phases ? layout(c, c.lengths) : layout(c));
const cases = CASES.map(([a, b]) => {
  const A = lay(a), B = lay(b), fs = [];
  for (let f = -1; f <= A.length + 1; f += 0.37) fs.push(f);
  for (const [s, n] of Object.values(A.starts)) fs.push(s, s + n);
  const keys = Object.keys(A.starts).map((ph) => [ph, 0.3, keyTime([ph, 0.3], A)]);
  return {
    from: a.phases ? a.phases.map((p) => [p, a.lengths[p]]) : [], fromFrames: a.frames || 0,
    to: b.phases ? b.phases.map((p) => [p, b.lengths[p]]) : [], toFrames: b.frames || 0,
    f: fs, warp: fs.map((f) => warp(A, B, f)), phase: fs.map((f) => phaseAt(A, f).phase ?? ""), done: fs.map((f) => phaseAt(A, f).done),
    keys, whole: keyTime(0.42, A), length: A.length,
  };
});
process.stdout.write(JSON.stringify({ format: "raw-clip-warp-vectors", version: 1, source: "web/anim/clip.mjs", cases }) + String.fromCharCode(10));
