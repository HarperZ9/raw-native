// Fuzz the tile loaders (ROADMAP M2 criterion 10): mutate the valid seeds and check that
// every input either loads or throws a TileError, within a time limit.
//
//   node tools/fuzz/tiles_fuzz.mjs [--n 1000000] [--seed raw-tiles] [--out evidence.json]
//
// Mutations, chosen at random per input, one to eight of them: flip a byte, insert a
// random byte or a structural token (< > " , = { } [ ] and digits), delete a run,
// duplicate a run, truncate, or replace a number with an extreme one (0, -1, 2^31,
// 2^32 - 1, 1e300, NaN). A finding is any other exception, or an input that takes
// longer than --limit-ms. The loaders are synchronous, so time is measured after the
// call returns; a true hang would stop the run, and CI's job timeout catches that.
import { inflateSync, gunzipSync } from "node:zlib";
import { writeFileSync } from "node:fs";
import { parseTMX, parseLDtk, TileError } from "../../web/world/tiles.mjs";
import { SAMPLES } from "./tile_samples.mjs";
import { rng } from "../../third_party/superstack/superstack.mjs";

const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const N = Number(arg("--n", 1000000)), SEED = arg("--seed", "raw-tiles"), LIMIT = Number(arg("--limit-ms", 1000)), OUT = arg("--out", null);
const inflate = (b, kind) => new Uint8Array(kind === "gzip" ? gunzipSync(b, { maxOutputLength: 1 << 26 }) : inflateSync(b, { maxOutputLength: 1 << 26 }));
const r = rng(SEED), R = () => r.nextFloat(), pick = (a) => a[Math.floor(R() * a.length)];
const TOKENS = ["<", ">", "/", '"', "'", "=", ",", "{", "}", "[", "]", ":", "0", "9", "-", ".", "e", "&", ";", "\n", " "];
const EXTREME = ["0", "-1", "2147483648", "4294967295", "4294967296", "1e300", "NaN", "-0", "99999999999999999999"];

function mutate(s) {
  let t = s;
  for (let k = 1 + Math.floor(R() * 8); k > 0; k--) {
    const i = Math.floor(R() * (t.length + 1)), op = Math.floor(R() * 7);
    if (op === 0 && t.length) t = t.slice(0, i) + String.fromCharCode(t.charCodeAt(Math.min(i, t.length - 1)) ^ (1 << Math.floor(R() * 7))) + t.slice(i + 1);
    else if (op === 1) t = t.slice(0, i) + String.fromCharCode(Math.floor(R() * 128)) + t.slice(i);
    else if (op === 2) t = t.slice(0, i) + pick(TOKENS) + t.slice(i);
    else if (op === 3) t = t.slice(0, i) + t.slice(i + 1 + Math.floor(R() * 16));
    else if (op === 4) { const n = 1 + Math.floor(R() * 32); t = t.slice(0, i) + t.slice(i, i + n) + t.slice(i); }
    else if (op === 5) t = t.slice(0, i);
    else {
      const nums = [...t.matchAll(/-?\d+/g)];
      if (nums.length) { const m = pick(nums); t = t.slice(0, m.index) + pick(EXTREME) + t.slice(m.index + m[0].length); }
    }
  }
  return t;
}

const counts = { loaded: 0, refused: 0, findings: 0, slow: 0 }, findings = [], t0 = performance.now();
let maxMs = 0;
for (let n = 0; n < N; n++) {
  const ldtk = R() < 0.4, src = mutate(pick(ldtk ? SAMPLES.ldtk : SAMPLES.tmx));
  const a = performance.now();
  try { if (ldtk) parseLDtk(src); else parseTMX(src, { inflate }); counts.loaded++; }
  catch (e) {
    if (e instanceof TileError) counts.refused++;
    else { counts.findings++; if (findings.length < 20) findings.push({ n, loader: ldtk ? "ldtk" : "tmx", error: String(e && e.stack || e).slice(0, 300), input: src.slice(0, 400) }); }
  }
  const ms = performance.now() - a;
  maxMs = Math.max(maxMs, ms);
  if (ms > LIMIT) { counts.slow++; if (findings.length < 20) findings.push({ n, slow_ms: ms, input: src.slice(0, 400) }); }
}
const res = { schema: "raw-native.evidence/1", criterion: "M2 exit criterion 10 (loaders survive one million fuzzed inputs)", inputs: N, seed: SEED,
  mutations: "byte flips, random and structural insertions, deletions, duplications, truncation, extreme numbers; one to eight per input, from valid TMX (csv, base64, zlib, gzip, xml) and LDtk seeds",
  pass_rule: "every input loads or throws TileError, none over the time limit", limit_ms: LIMIT, ...counts, max_ms: +maxMs.toFixed(2),
  seconds: +((performance.now() - t0) / 1000).toFixed(1), node: process.version, pass: counts.findings === 0 && counts.slow === 0, examples: findings,
  does_not_prove: "Robustness against these mutations of these seeds. Not against inputs far from them, and not that a loaded map is what its editor meant." };
console.log(JSON.stringify(res, null, 1));
if (OUT) writeFileSync(OUT, JSON.stringify(res, null, 1) + "\n");
process.exit(res.pass ? 0 : 1);
