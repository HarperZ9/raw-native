// Captures Low Water's impact camera (game/engine/ui/fightcam.mjs fightCam, the "shake" treatment at 1920 x 1080)
// over the contact traces' shake events, for the C++ port's 1 px check (raw/renderer/impact.hpp ShakeCam,
// tests/test_action_impact.cpp). Each tick: the tick's blows in trace order, then tick(1/60); the offset after it.
//
//   node tools/action/shake_capture.mjs <low-water>/game/engine <contacts dir> > shake-capture.json
//
// Also three fixed cases: a full-weight blow (amp 1), the same blow while a tell shows (the 3 px hold), and a catch.
import { readFileSync, readdirSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [eng, dir] = process.argv.slice(2);
if (!eng || !dir) { console.error("usage: shake_capture.mjs <engine-dir> <contacts-dir>"); process.exit(2); }
const { fightCam } = await import(pathToFileURL(resolve(eng)).href + "/ui/fightcam.mjs");
const cam = () => fightCam({ prefs: { shake: "on" }, mode: "shake", layers: () => [], view: () => ({ w: 1920, h: 1080 }) });

// blows: Map tick -> [{amp, kind, dir}]; tells: Set of ticks a tell shows. Returns [[tick, dx, dy], ...] while moving.
function run(blows, end, tells = new Set()) {
  const C = cam(), out = [];
  for (let k = 1; k <= end; k++) {
    for (const b of blows.get(k) || []) C.blow({ amp: b.amp, kind: b.kind }, b.dir);
    C.tells(tells.has(k) ? [[960, 540]] : []);
    const { dx, dy } = C.tick(1 / 60);
    if (dx || dy) out.push([k, dx, dy]);
  }
  return out;
}

const traces = {};
for (const f of readdirSync(dir).filter((x) => x.endsWith(".jsonl")).sort()) {
  const blows = new Map(); let end = 0;
  for (const line of readFileSync(join(dir, f), "utf8").split("\n")) {
    if (!line) continue;
    const r = JSON.parse(line);
    if (r.t === "aend") end = r.tick;
    if (r.t === "aev" && r.n === "shake") (blows.get(r.tick) || blows.set(r.tick, []).get(r.tick)).push({ amp: r.d.amp, kind: r.d.kind, dir: r.d.dir });
  }
  traces[f] = run(blows, end);
}
const one = (amp, kind) => new Map([[1, [{ amp, kind, dir: [1, 0] }]]]);
const cases = {
  full: run(one(1), 60),
  tell: run(one(1), 60, new Set(Array.from({ length: 60 }, (_, i) => i + 1))),
  catch: run(one(0.6, "catch"), 60),
};
process.stdout.write(JSON.stringify({ format: "raw-shake-capture", version: 1, size: [1920, 1080], source: "game/engine/ui/fightcam.mjs", cases, traces }) + "\n");
