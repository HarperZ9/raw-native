// Write a film's WebVTT captions from its script rows, the same rows the scene burns in.
//   node tools/explainer/captions.mjs docs/explainer/attention/script.mjs docs/explainer/attention/data.json OUT.vtt
import { readFile, writeFile } from "node:fs/promises";
import { pathToFileURL } from "node:url";
import { resolve } from "node:path";

const [scriptPath, dataPath, out] = process.argv.slice(2);
if (!out) { console.error("usage: captions.mjs SCRIPT.mjs DATA.json OUT.vtt"); process.exit(2); }
const { script } = await import(pathToFileURL(resolve(scriptPath)).href);
const d = JSON.parse(await readFile(dataPath, "utf8"));
const ts = (s) => { const m = Math.floor(s / 60), r = s - 60 * m; return `00:${String(m).padStart(2, "0")}:${r.toFixed(3).padStart(6, "0")}`; };
const rows = script(d);
const vtt = "WEBVTT\n\n" + rows.map(([a, b, text], i) => `${i + 1}\n${ts(a)} --> ${ts(b)}\n${text}\n`).join("\n");
await writeFile(out, vtt, "utf8");
console.log(`${out}: ${rows.length} cues`);
