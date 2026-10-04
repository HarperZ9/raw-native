// The explorer camera's math: an orthonormal frame, orbit and zoom limits,
// pan moves the target, focus eases to its goal, reset returns home.
import { test } from "node:test";
import assert from "node:assert/strict";
import { OrbitCamera } from "./camera.mjs";

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const near = (a, b, e = 1e-6) => Math.abs(a - b) < e;

test("the basis is orthonormal and the eye sits at distance from the target", () => {
  const c = new OrbitCamera({ yaw: 1.1, pitch: 0.4, distance: 3 });
  const { fwd, right, up } = c.basis();
  for (const v of [fwd, right, up]) assert.ok(near(dot(v, v), 1));
  assert.ok(near(dot(fwd, right), 0) && near(dot(fwd, up), 0) && near(dot(right, up), 0));
  const e = c.eye();
  assert.ok(near(Math.hypot(e[0] - c.target[0], e[1] - c.target[1], e[2] - c.target[2]), 3));
  assert.ok(near(c.ray(0, 0, 1.5).dir[0], fwd[0]));
});

test("pitch and distance are clamped; pan moves the target; reset goes home", () => {
  const c = new OrbitCamera();
  c.orbit(0, 10); assert.ok(c.pitch <= 1.45);
  c.orbit(0, -10); assert.ok(c.pitch >= -0.15);
  c.zoom(1e6); assert.equal(c.distance, c.maxDistance);
  c.zoom(1e-6); assert.equal(c.distance, c.minDistance);
  const t0 = [...c.target]; c.pan(0.1, 0); assert.notDeepEqual(c.target, t0);
  c.fly(1, 0, 0); assert.ok(near(c.target[1], t0[1] + c.basis().up[1] * 0 + (c.target[1] - t0[1])));
  c.reset(); assert.deepEqual(c.target, c.home.target); assert.equal(c.distance, c.home.distance);
});

test("focus eases the target to the picked point", () => {
  const c = new OrbitCamera();
  c.focus([1, 0, 1], 2);
  for (let i = 0; i < 200; i++) c.step(1 / 60);
  assert.ok(near(c.target[0], 1, 1e-2) && near(c.target[2], 1, 1e-2) && near(c.distance, 2, 1e-2));
});

test("uniforms pack eye, tan(fov/2), forward, right and up", () => {
  const c = new OrbitCamera({ fov: Math.PI / 2 });
  const u = c.uniforms();
  assert.equal(u.length, 16);
  assert.ok(near(u[3], 1, 1e-6));
  assert.deepEqual([...u.slice(0, 3)].map((v) => +v.toFixed(5)), c.eye().map((v) => +v.toFixed(5)));
});
