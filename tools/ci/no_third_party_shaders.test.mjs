import test from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, writeFileSync, mkdirSync, rmSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { scan } from "./no_third_party_shaders.mjs";

function tree(files) {
  const root = mkdtempSync(join(tmpdir(), "ntps-"));
  for (const [name, text] of Object.entries(files)) { mkdirSync(join(root, name, ".."), { recursive: true }); writeFileSync(join(root, name), text); }
  return root;
}
const kinds = (files, allow) => { const r = tree(files); try { return scan(r, allow).map((f) => `${f.file}:${f.kind}`); } finally { rmSync(r, { recursive: true, force: true }); } };

test("the real tree under web/shaders has no third-party shader source, licence header or package import", () => {
  const found = scan("web/shaders", JSON.parse(readFileSync("tools/ci/third_party_allowlist.json", "utf8")));
  assert.deepEqual(found, [], JSON.stringify(found.slice(0, 5)));
});
test("falsification: each kind of third-party marker is caught", () => {
  const bad = {
    "gpl.fx": "// This program is free software: GNU General Public License v3\nfloat4 main() { return 0; }\n",
    "mit.mjs": "/* Permission is hereby granted, free of charge, to any person */\nexport const a = 1;\n",
    "spdx.wgsl": "// SPDX-License-Identifier: Apache-2.0\nfn f() {}\n",
    "copy.mjs": "// Copyright (c) 2019 Someone Else\nexport const a = 1;\n",
    "imp.mjs": "import { x } from \"some-shader-lib\";\nexport const a = x;\n",
    "inc.fxh": "#include \"ReShade.fxh\"\nfloat f() { return 1; }\n",
    "origin.mjs": "// Ported from a Shadertoy by Inigo Quilez (shadertoy.com/view/xyz)\nexport const a = 1;\n",
    "cc.mjs": "// Creative Commons BY-NC-SA\nexport const a = 1;\n",
  };
  for (const [name, text] of Object.entries(bad)) assert.ok(kinds({ [name]: text }).length > 0, `${name} was not caught`);
});
test("clean code passes: relative imports, node imports, the author's copyright, long data lines", () => {
  const ok = {
    "a.mjs": "import { b } from \"./b.mjs\";\nimport { c } from \"../c.mjs\";\nimport fs from \"node:fs\";\n// Copyright (c) 2026 Zain Dana Harper\nexport const a = b + c;\n",
    "b.wgsl": "fn f(x: f32) -> f32 { return x * 2.0; } // original\n",
    "data.mjs": `export const D = "${"A".repeat(400)} GPL Apache";\n`,
    "text.mjs": "// the word import appears here and a string \"require(x)\" too\nexport const s = 'import x from \"y\"';\n",
  };
  assert.deepEqual(kinds(ok), []);
});
test("an allowlist entry needs to name the file, and exempts only that file", () => {
  const files = { "v.mjs": "// Copyright (c) 2019 Vendor\nexport const a = 1;\n", "w.mjs": "// Copyright (c) 2019 Vendor\nexport const a = 1;\n" };
  assert.deepEqual(kinds(files, [{ path: "v.mjs", reason: "test" }]), ["w.mjs:licence"]);
});
test("the allowlist file is tracked by git, so CI sees the same allowlist as a local run", async () => {
  const { execFileSync } = await import("node:child_process");
  let tracked = true;
  try { execFileSync("git", ["ls-files", "--error-unmatch", "tools/ci/third_party_allowlist.json"], { stdio: "pipe" }); } catch (e) { tracked = e.code === "ENOENT"; }
  assert.ok(tracked, "tools/ci/third_party_allowlist.json is not tracked (check .gitignore)");
});
