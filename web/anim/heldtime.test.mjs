// node --test web/anim/heldtime.test.mjs
// The JavaScript held-time clock against the C++ one (tests/test_held_time.cpp): the same ticks,
// onsets and seeds, and P1's held ratios on a clip.mjs clip.
import { test } from "node:test";
import assert from "node:assert/strict";
import { continuous, onN, named, sampleHeld, onsetFrame, sampleHeldClip, fnv64, boilSeed, heldRatio } from "./heldtime.mjs";
import { layout } from "./clip.mjs";

test("ticks and onsets match the C++ clock's vectors", () => {
  assert.equal(sampleHeld({ rate: named(24000, 1001) }, 1001, 24).tick, 1000);
  assert.equal(sampleHeld({ rate: onN(3) }, 143, 144).tick, 7);
  assert.equal(onsetFrame(onN(2), 5, 60), 25);
  assert.equal(onsetFrame(named(8), 1, 30), 4);
  assert.equal(sampleHeld({ rate: continuous() }, 7, 30).tick, 7);
  assert.throws(() => onN(0));
});

test("every tick arrives on its onset frame at 30, 60 and 144 fps", () => {
  for (const rate of [onN(1), onN(2), onN(3), named(12)]) for (const fps of [30, 60, 144]) {
    let prev = -1;
    for (let k = 0; k < 10 * fps; k++) {
      const { tick } = sampleHeld({ rate }, k, fps);
      if (tick !== prev) { assert.equal(onsetFrame(rate, tick, fps), k); assert.ok(tick === prev + 1); prev = tick; }
    }
  }
});

test("FNV-1a and the boil seed match the C++ values", () => {
  assert.equal(fnv64(new TextEncoder().encode("a")), 0xaf63dc4c8601ec8cn);
  const h = fnv64(new TextEncoder().encode("held-time"));
  assert.equal(h, 0x5d1f91e1100c04a0n);
  assert.equal(boilSeed(0x51a7e5, h), 0xca77cef2);
});

test("held ratio of a clip over 10 s at 24 fps: twos 0.50, threes 0.67, ones 0", () => {
  const clip = { frames: 96, loop: true, keys: [{ at: 0, value: { a: 0, b: 1 } }, { at: 0.3, value: { a: 1, b: 0 } }, { at: 0.7, value: { a: -0.5, b: 0.4 } }] };
  const L = layout(clip, { frames: 96 });
  const ratio = (rate) => heldRatio(Array.from({ length: 240 }, (_, k) => JSON.stringify(sampleHeldClip(clip, L, { rate }, k, 24, 24))));
  assert.equal(ratio(onN(1)), 0);
  assert.ok(Math.abs(ratio(onN(2)) - 0.5) <= 0.01);
  assert.ok(Math.abs(ratio(onN(3)) - 0.67) <= 0.01);
  assert.equal(ratio(continuous()), 0);
});
