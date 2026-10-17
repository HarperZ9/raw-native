// Fixtures of the one-ray visibility estimator and the Penumbra Yard (evidence/cpp-port-style-bounds-9.json).
import { join } from "node:path";
import { bitrev6, pascal6, concentric, visibility } from "../../web/shaders/ditherlight/rtvis.mjs";
import { BLOCKERS, occludedBy, yardLight, yardPoint, yardTone, YARD } from "../../web/shaders/fixtures/penumbra.mjs";
import { Bundle } from "./rnfx.mjs";

export function rtvis(out) {
  const B = new Bundle("rtvis"), net = new Uint32Array(128), disc = new Float64Array(64 * 64 * 2);
  for (let r = 0; r < 64; r++) { net[r * 2] = bitrev6(r); net[r * 2 + 1] = pascal6(r); }
  for (let j = 0; j < 64; j++) for (let i = 0; i < 64; i++) { const [x, y] = concentric((i + 0.5) / 64, (j + 0.5) / 64); disc[(j * 64 + i) * 2] = x; disc[(j * 64 + i) * 2 + 1] = y; }
  B.add("net", "u32", net); B.add("concentric", "f64", disc);
  const L = yardLight(), N = YARD.px;
  B.head.light = L; B.head.size = N; B.head.cases = [];
  for (const blocker of ["wall-x", "wall-z", "sphere"]) for (const method of ["net1", "rank1", "white1", "ref64"]) {
    const occ = occludedBy(BLOCKERS[blocker]), V = new Float64Array(N * N), T = new Float64Array(N * N);
    for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) { const P = yardPoint(x, y), v = visibility(method, L, P, x, y, 0, occ); V[y * N + x] = v; T[y * N + x] = yardTone(P, L, v); }
    const id = `${blocker}-${method}`;
    B.add(`${id}.V`, "f64", V); B.add(`${id}.tone`, "f64", T);
    B.head.cases.push({ id, blocker, method, frame: 0 });
  }
  B.write(join(out, "rtvis.rnfx"));
  return B.head.cases.length;
}
