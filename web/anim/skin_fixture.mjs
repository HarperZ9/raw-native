// The skinning fixture for the C++ reference (evidence/m4-skinning-cpu-bounds.json, k1).
// Builds a deterministic skeleton, poses and mesh, skins them with the JavaScript oracle
// (skeleton.mjs) and writes them as text that tests/test_skin.cpp reads.
//
//   node web/anim/skin_fixture.mjs tests/data/skin-fixture.txt
//
// Format, one record a line, numbers in JavaScript's shortest round-trip form:
//   skin-fixture 1
//   joints N, then N lines: parent tx ty tz rx ry rz rw
//   poses P, then for each pose N lines: hasR rx ry rz rw hasT tx ty tz
//   vertices V, then V lines: px py pz nx ny nz j0 j1 j2 j3 w0 w1 w2 w3
//   then for each pose V lines: px py pz nx ny nz (the oracle's float32 output)
import { writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { makeSkeleton, poseSkeleton, skinMatrices, skin, quatAxisAngle, quatEuler } from "./skeleton.mjs";

const SEED = 20261020, JOINTS = 12, POSES = 3, VERTS = 400, ZERO_WEIGHT = 5;

function lcg(seed) {
  let s = seed >>> 0;
  return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
}

export function buildFixture() {
  const rnd = lcg(SEED), u = (a, b) => a + (b - a) * rnd();
  // Two branches off a root: joints 1..5 chain from 0, joints 6..11 chain from 0 as well.
  const joints = [];
  for (let i = 0; i < JOINTS; i++) {
    const parent = i === 0 ? null : i === 6 ? "j0" : `j${i - 1}`;
    joints.push({ name: `j${i}`, parent, t: i === 0 ? [0, 1, 0] : [u(-0.1, 0.1), u(-0.5, -0.2), u(-0.1, 0.1)],
                  r: quatAxisAngle([u(-1, 1), u(-1, 1), u(-1, 1)], u(-0.6, 0.6)) });
  }
  const sk = makeSkeleton(joints);
  const poses = [];
  for (let p = 0; p < POSES; p++) {
    const pose = {};
    for (let i = 0; i < JOINTS; i++) {
      if (rnd() < 0.25) continue;   // some joints keep their rest rotation
      const e = { r: quatEuler(u(-1, 1), u(-1, 1), u(-0.5, 0.5)) };
      if (i === 0) e.t = [u(-0.3, 0.3), u(-0.1, 0.1), u(-0.3, 0.3)];   // only the root moves (k4)
      pose[`j${i}`] = e;
    }
    poses.push(pose);
  }
  const pos = new Float32Array(VERTS * 3), nrm = new Float32Array(VERTS * 3);
  const jnt = new Uint16Array(VERTS * 4), wgt = new Float32Array(VERTS * 4);
  for (let v = 0; v < VERTS; v++) {
    pos[v * 3] = u(-0.4, 0.4); pos[v * 3 + 1] = u(-1.5, 1); pos[v * 3 + 2] = u(-0.4, 0.4);
    const n = [u(-1, 1), u(-1, 1), u(-1, 1)], l = Math.hypot(n[0], n[1], n[2]) || 1;
    for (let c = 0; c < 3; c++) nrm[v * 3 + c] = n[c] / l;
    const k = 1 + Math.floor(rnd() * 4);
    for (let i = 0; i < 4; i++) {
      jnt[v * 4 + i] = Math.floor(rnd() * JOINTS);
      wgt[v * 4 + i] = v < ZERO_WEIGHT || i >= k ? 0 : u(0.05, 1);   // unnormalised on purpose
    }
  }
  const mesh = { pos, nrm, joints: jnt, weights: wgt };
  const lines = ["skin-fixture 1", `joints ${JOINTS}`];
  for (const j of sk.joints) lines.push([j.parent, ...j.t, ...j.r].join(" "));
  lines.push(`poses ${POSES}`);
  for (const pose of poses)
    for (const j of sk.joints) {
      const e = pose[j.name] || {};
      lines.push([e.r ? 1 : 0, ...(e.r || [0, 0, 0, 1]), e.t ? 1 : 0, ...(e.t || [0, 0, 0])].join(" "));
    }
  lines.push(`vertices ${VERTS}`);
  for (let v = 0; v < VERTS; v++)
    lines.push([...pos.slice(v * 3, v * 3 + 3), ...nrm.slice(v * 3, v * 3 + 3), ...jnt.slice(v * 4, v * 4 + 4), ...wgt.slice(v * 4, v * 4 + 4)].join(" "));
  for (const pose of poses) {
    const out = skin(mesh, skinMatrices(poseSkeleton(sk, pose), sk.invBind));
    for (let v = 0; v < VERTS; v++) lines.push([...out.pos.slice(v * 3, v * 3 + 3), ...out.nrm.slice(v * 3, v * 3 + 3)].join(" "));
  }
  return lines.join("\n") + "\n";
}

if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const out = process.argv[2];
  if (!out) { console.error("usage: node web/anim/skin_fixture.mjs <out.txt>"); process.exit(2); }
  writeFileSync(out, buildFixture());
  console.log(`skin_fixture: wrote ${out}`);
}
