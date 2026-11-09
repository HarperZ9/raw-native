// Writes the frame-data table of Low Water's moves as the JS engine expands them, in the text form
// raw_native_cli action-dump table prints (docs/action-timeline.md), for the C++ frame-data test.
//
//   node tools/action/dump_frames.mjs <low-water>/game/engine/combat > frames.txt
//
// One line a move, sorted: <set> <id> startup= active= recovery= free= cancel=<action>:<from>-<to>,...
// <set> is base, or tool/<tool id>/<lamb|slow|foe>. free is the first frame after the move
// (startup + active + recovery), the frame on which the body may act again without a cancel.
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const dir = process.argv[2];
if (!dir) { console.error("usage: dump_frames.mjs <combat-dir>"); process.exit(2); }
const { expandAll } = await import(pathToFileURL(resolve(dir)).href + "/tools/export-data.mjs");

const line = (set, m) => {
  const c = Object.keys(m.cancel).sort().map((k) => `${k}:${m.cancel[k][0]}-${m.cancel[k][1]}`).join(",");
  return `${set} ${m.id} startup=${m.startup} active=${m.active} recovery=${m.recovery} free=${m.total} cancel=${c || "-"}`;
};
const e = expandAll(), out = [];
for (const m of Object.values(e.base)) out.push(line("base", m));
for (const [tool, v] of Object.entries(e.tools)) for (const [k, t] of Object.entries(v)) for (const m of Object.values(t)) out.push(line(`tool/${tool}/${k}`, m));
out.sort();
process.stdout.write(out.join(String.fromCharCode(10)) + String.fromCharCode(10));
