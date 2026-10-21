// node --test web/anim/skin_fixture.test.mjs
// The committed C++ skinning fixture is what the JavaScript oracle writes today (no drift).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { buildFixture } from "./skin_fixture.mjs";

test("tests/data/skin-fixture.txt equals the oracle's output", () => {
  const committed = readFileSync(new URL("../../tests/data/skin-fixture.txt", import.meta.url), "utf8").replace(/\r\n/g, "\n");
  assert.equal(committed, buildFixture(), "regenerate with: node web/anim/skin_fixture.mjs tests/data/skin-fixture.txt");
});
