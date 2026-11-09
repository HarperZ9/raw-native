// Writes clip markers for every move of Low Water's JS combat engine, for the C++ marker test of
// raw/game/action/markers (G3) and the telegraph retime lint (G5), docs/action-hitvol.md.
//
//   node tools/action/export_markers.mjs <low-water>/game/engine/combat > clip-markers.json
//
// One clip a move, at 60 Hz: the move's phase markers (active: its first active frame, recovery:
// its first recovery frame, end: its free frame), its tell window and every hit window [on, off]
// (inclusive, as the JS `at`) and the tell's kind, with the hit's ring (arc in degrees, radii r0..r1, height band),
// and the authored smear frames where the move has a swing travel (`sweep`).
// Sets: base and tool/<id>/<lamb|slow|foe> as dump_frames.mjs names them (the moves.json expansion),
// and inner/<flaw> for the inner fights' moves (flaws.mjs specials, through defineMoves), which carry
// the T9 fields (parryable, blockable, signature). Output keys are sorted; the file is deterministic.
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { resolve, join } from "node:path";
import { pathToFileURL } from "node:url";

const dir = process.argv[2];
if (!dir) { console.error("usage: export_markers.mjs <combat-dir>"); process.exit(2); }
const C = pathToFileURL(resolve(dir)).href + "/";
const { expandAll } = await import(C + "tools/export-data.mjs");
const { defineMoves } = await import(C + "sim/moves.mjs");
const { FLAWS, HOST } = await import(C + "data/flaws.mjs");

const sortKeys = (v) => (Array.isArray(v) ? v.map(sortKeys) : v && typeof v === "object" ? Object.fromEntries(Object.keys(v).sort().map((k) => [k, sortKeys(v[k])])) : v);
const sha = (p) => createHash("sha256").update(readFileSync(join(resolve(dir), p))).digest("hex");

const clip = (m) => ({
  markers: { active: m.startup, recovery: m.startup + m.active, end: m.total, ...(m.tell ? { tell: [m.tell.at[0], m.tell.at[1]] } : {}) },
  hits: m.hits.map((h) => ({ on: h.at[0], off: h.at[1], arc: h.arc, r: h.r, y: h.y })),
  parryable: m.parryable !== false, blockable: m.blockable !== false, signature: !!m.signature,
  ...(m.tell ? { tellKind: m.tell.kind || "" } : {}),
  // the art's long swing travel, [from, startup) in the JS (sim/moves.mjs `sweep`): the authored smear frames,
  // written inclusive [from, to] like the other windows (2026-11-09, G6)
  ...(m.sweep ? { smear: [m.sweep[0], m.sweep[1] - 1] } : {}),
});

const clips = {};
const e = expandAll();
for (const m of Object.values(e.base)) clips[`base ${m.id}`] = clip(m);
for (const [tool, v] of Object.entries(e.tools)) for (const [k, t] of Object.entries(v)) for (const m of Object.values(t)) clips[`tool/${tool}/${k} ${m.id}`] = clip(m);
const inner = [...Object.entries(FLAWS), ["host", HOST]];
let innerCount = 0;
for (const [name, f] of inner) {
  const defined = defineMoves(f.specials || {});
  for (const m of Object.values(defined)) { clips[`inner/${name} ${m.id}`] = clip(m); innerCount++; }
}
const files = ["sim/moves.mjs", "data/moves.json", "data/tools.mjs", "data/flaws.mjs", "data/innermoves.mjs", "data/fights.mjs", "data/fights-a.mjs", "data/fights-b.mjs", "data/fights-c.mjs", "data/fights-host.mjs", "tools/export-data.mjs"];
const doc = {
  format: "raw-clip-markers", version: 1, fps: 60,
  source: { repo: "Low Water", commit: process.env.LW_COMMIT || "unknown", files: Object.fromEntries(files.map((p) => [p, sha(p)])) },
  counts: { clips: Object.keys(clips).length, inner: innerCount },
  clips,
};
process.stdout.write(JSON.stringify(sortKeys(doc)) + String.fromCharCode(10));
