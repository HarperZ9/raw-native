// Threads: the shader keeps the interface its callers use, and its method is
// not the ported one renamed. Run: node --test web/
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { wgslPasses } from "./raw-gpu.mjs";
import { advanceFor, WORLDS } from "./threads.mjs";
import { fingerprint, jaccard, methodOf, tokens } from "./threads-fingerprint.mjs";

const src = readFileSync(new URL("../src/renderer/gpu/shaders/threads.wgsl", import.meta.url), "utf8").replace(/\r\n/g, "\n");
const legacy = JSON.parse(readFileSync(new URL("./test/threads-legacy.fingerprint.json", import.meta.url), "utf8"));
const layout = readFileSync(new URL("../src/renderer/gpu/shaders/threads_layout.hpp", import.meta.url), "utf8");
// Below this share of eight-token shingles, two passes are different algorithms.
// The ported version measured 1.0 against itself and 0.18 (advance) and 0.31
// (finish) against this one on 2026-10-23.
const SAME_ALGORITHM = 0.35;

test("the eight passes keep their names and binding counts", () => {
  const passes = wgslPasses(src);
  const want = { threads_init: 2, threads_rd_seed: 2, threads_rd: 3, threads_decay: 2, threads_advance: 4,
    threads_pyr_first: 3, threads_pyr_next: 3, threads_finish: 5 };
  assert.deepEqual(Object.keys(passes).sort(), Object.keys(want).sort());
  for (const [name, n] of Object.entries(want)) {
    const binds = [...passes[name].matchAll(/@group\(0\) @binding\((\d+)\)/g)].map((m) => +m[1]);
    assert.deepEqual(binds, [...Array(n).keys()], name);
    assert.match(layout, new RegExp(`\\{"${name}", ${n}, `), name);
  }
});

test("the Params block is the 144-byte layout threads.cpp and threads.mjs write", () => {
  const body = /struct Params \{([\s\S]*?)\n\}/.exec(src)[1];
  const fields = [...body.matchAll(/(\w+): (f32|u32|i32|vec4f)/g)].map((m) => m[2] === "vec4f" ? 16 : 4);
  assert.equal(fields.reduce((a, b) => a + b, 0), 144);
});

test("the advance pass specializes for every world and every crossing", () => {
  const adv = wgslPasses(src).threads_advance;
  assert.equal(WORLDS.length, 15);
  for (let a = 0; a < 15; a++) {
    const one = advanceFor(adv, a);
    assert.notEqual(one, adv);
    assert.equal((one.match(/fn form\(/g) || []).length, 1);
    assert.match(advanceFor(adv, a, (a + 1) % 15), new RegExp(`if \\(w == ${a}\\)`));
  }
});

test("the method is not the ported one renamed", () => {
  for (const pass of ["threads_advance", "threads_finish"]) {
    const j = jaccard(legacy.passes[pass], fingerprint(methodOf(src, pass)));
    assert.ok(j < SAME_ALGORITHM, `${pass}: ${j.toFixed(3)} of its shingles match the ported version`);
  }
});

test("control: renaming every identifier does not change the fingerprint", () => {
  const adv = methodOf(src, "threads_advance");
  let k = 0;
  const names = new Map();
  const renamed = adv.replace(/\b[A-Za-z_]\w*\b/g, (w) => {
    if (tokens(w)[0] !== "ID") return w;
    if (!names.has(w)) names.set(w, `n${k++}`);
    return names.get(w);
  });
  assert.notEqual(renamed, adv);
  assert.equal(jaccard(fingerprint(adv), fingerprint(renamed)), 1);
});

test("the ported constructs are gone and the new primitives are present", () => {
  for (const gone of [/43758\.5453/, /fn curl\(/, /exp\(-abs\(fl\) \/ 0\.0016\)/, /log\(1\.0 \+ a\.w \* P\.expo\)/,
    /1\.0 - exp\(-c \* 1\.6\)/, /Ported from/i]) assert.doesNotMatch(src, gone);
  for (const here of [/workgroupBarrier\(\)/, /fn pinToLevel\(/, /atan2\(2\.0 \* jxy, jxx - jyy\)/, /fn ropeEase\(/,
    /fn spectrum\(/, /fn asinhf\(/, /fn lowbias32\(/, /fn springTok\(/]) assert.match(src, here);
});
