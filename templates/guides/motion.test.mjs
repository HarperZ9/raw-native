// node --test templates/guides/motion.test.mjs
// The clips and framings, checked without a GPU: every loop ends where it starts, the actions
// do what they claim at the times they claim, and pixel framings keep one art pixel per
// frame pixel.
import { test } from "node:test";
import assert from "node:assert/strict";
import { clipBeat, framing, CLIPS } from "./motion.mjs";
import { BEATS } from "./beats.mjs";

const HOME = { tallow: "thriller", pylon: "painted", rerun: "pixel" };
const near = (a, b) => (Array.isArray(a) ? a.every((v, i) => near(v, b[i])) : typeof a === "number" ? Math.abs(a - b) < 1e-9 : a === b);

test("idle and talk loops repeat exactly: the pose at the end equals the pose at the start", () => {
  for (const [g, st] of Object.entries(HOME)) for (const c of ["idle", "talk"]) {
    const a = clipBeat(g, c, BEATS[g][st], 0), b = clipBeat(g, c, BEATS[g][st], CLIPS[c]);
    for (const k of Object.keys(a.o)) assert.ok(near(a.o[k], b.o[k]), `${g} ${c}: ${k} ${JSON.stringify(a.o[k])} vs ${JSON.stringify(b.o[k])}`);
  }
});

test("the signature actions happen: the flame gutters, the cone is set down, the claw strikes", () => {
  const f = (t) => clipBeat("tallow", "action", BEATS.tallow.thriller, t).o.flame;
  assert.ok(f(1.5) < 0.5 && f(0) > 0.95 && f(3.2) > 0.95, `flame ${f(0)}, ${f(1.5)}, ${f(3.2)}`);
  const p = (t) => clipBeat("pylon", "action", BEATS.pylon.painted, t).o;
  assert.ok(p(0.8).flipR > p(0).flipR + 50, "the flipper rises with the cone");
  assert.ok(p(1.6).coneT >= 1.5, "the cone is placed after 1.5 s");
  const r = (t) => clipBeat("rerun", "action", BEATS.rerun.pixel, t).o.clawR;
  assert.ok(r(0.6) > 60 && r(0.85) < 10 && r(1.2) > 60, `claw ${r(0.6)}, ${r(0.85)}, ${r(1.2)}`);
});

test("pixel framings render at a size whose integer scale fills 1920 x 1080, with a matching zoom", () => {
  for (const name of ["medium", "close"]) {
    const f = framing("rerun", "pixel", BEATS.rerun.pixel, name, {});
    assert.equal(1920 % f.pixel.width, 0); assert.equal(1080 % f.pixel.height, 0);
    assert.equal(f.camera.zoom, 480 / f.pixel.width, `${name}: one art pixel per frame pixel`);
  }
});
