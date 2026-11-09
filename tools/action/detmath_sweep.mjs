// Writes detmath-sweep.json for tests/test_action_math.cpp: Low Water's JS combat math
// (combat/sim/math.mjs) over 1,001 inputs a function, each function's result bits hashed.
// The 12 golden calls of DETMATH section 3 cannot see a one-ulp change in a sine coefficient;
// this sweep can.
//   node tools/action/detmath_sweep.mjs <low-water>/game/engine/combat > detmath-sweep.json
import { createHash } from "node:crypto";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const M = await import(pathToFileURL(resolve(process.argv[2])).href + "/sim/math.mjs");
const N = 1001, buf = new DataView(new ArrayBuffer(8));
const hex = (d) => { buf.setFloat64(0, d); return buf.getBigUint64(0).toString(16).padStart(16, "0"); };
const fns = {
  dsin: (x) => M.dsin(x), dcos: (x) => M.dcos(x), datan: (x) => M.datan(x), datan2: (x, i) => M.datan2(x, 1.5 - i * 0.003),
  dasin: (x) => M.dasin(x / 20), dacos: (x) => M.dacos(x / 20), dexp: (x) => M.dexp(x), dhypot: (x) => M.dhypot(x, 0.5 * x + 1),
};
const out = { n: N, x: "-20 + i * 0.04", fns: {} };
for (const [name, f] of Object.entries(fns)) {
  const bits = []; for (let i = 0; i < N; i++) bits.push(hex(f(-20 + i * 0.04, i)));
  out.fns[name] = createHash("sha256").update(bits.join("\n")).digest("hex");
}
process.stdout.write(JSON.stringify(out, null, 1) + "\n");
