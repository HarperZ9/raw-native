// Triangle scenes for the 3D radiance cascades and the path-traced reference.
//   const s = buildScene3d("leak-door");          // synthetic, in metres
//   const s = parseScene(bytes);                   // a scene file written by raw_native_cli gi-reference
//   const bytes = writeScene(s);                   // the same format, read back by gi-reference
// A scene: tris (Float32Array, 9 a triangle), nrm (vertex normals, 9 a triangle), alb and emit
// (3 a triangle), sunDir (towards the sun), sunE (sun irradiance), sky (radiance on escape) and a
// camera { eye, target, up, fovy }. Albedo is stored as byte / 255, as the path tracer reads it.
//
// File format (little endian): 8 bytes "RNGISC01", u32 triangle count, 16 f32 (sunDir 3, sunE 3,
// sky 3, eye 3, target 3, fovy 1), then 24 f32 a triangle (3 positions, 3 normals, albedo, emission).
// The camera's up is (0, 1, 0).

export const MAGIC = "RNGISC01";
const q8 = (v) => Math.round(Math.min(1, Math.max(0, v)) * 255) / 255;

export function sceneFrom(list, { sunDir = [0, 1, 0], sunE = [0, 0, 0], sky = [0, 0, 0], camera, name = "scene" }) {
  const n = list.length, tris = new Float32Array(9 * n), nrm = new Float32Array(9 * n), alb = new Float32Array(3 * n), emit = new Float32Array(3 * n);
  list.forEach((t, i) => {
    tris.set(t.p, 9 * i); nrm.set(t.n, 9 * i);
    alb.set(t.alb.map(q8), 3 * i); emit.set(t.emit || [0, 0, 0], 3 * i);
  });
  const m = Math.hypot(...sunDir);
  return { name, tris, nrm, alb, emit, sunDir: sunDir.map((v) => v / m), sunE, sky, camera: { up: [0, 1, 0], ...camera } };
}

// A box with outward winding and flat normals: 12 triangles.
export function box(lo, hi, alb, emit = [0, 0, 0]) {
  const out = [], c = (i) => [i & 1 ? hi[0] : lo[0], i & 2 ? hi[1] : lo[1], i & 4 ? hi[2] : lo[2]];
  // Faces as corner indices in counter-clockwise order seen from outside, with their normals.
  const faces = [[[0, 4, 6, 2], [-1, 0, 0]], [[1, 3, 7, 5], [1, 0, 0]], [[0, 1, 5, 4], [0, -1, 0]],
    [[2, 6, 7, 3], [0, 1, 0]], [[0, 2, 3, 1], [0, 0, -1]], [[4, 5, 7, 6], [0, 0, 1]]];
  for (const [[a, b, cc, d], n] of faces) {
    const nn = [...n, ...n, ...n];
    out.push({ p: [...c(a), ...c(b), ...c(cc)], n: nn, alb, emit }, { p: [...c(a), ...c(cc), ...c(d)], n: nn, alb, emit });
  }
  return out;
}

// The leak scene of ROADMAP P6: two rooms side by side, 10 x 3 x 5 m inside, split at x = 5 by a
// partition 4 cm thick. Room A (x < 5) has a 2 x 2 m skylight; the sun falls through it onto the
// floor and the partition's A face. "door": a 0.8 x 2.1 m doorway near the far wall lets light into
// room B. "closed": no door, so room B's exact radiance is 0.
function leakScene(door, camRoom) {
  const wall = [0.62, 0.6, 0.56], floor = [0.5, 0.48, 0.45], t = 0.2, e = 0.05, list = [];
  const add = (lo, hi, a) => list.push(...box(lo, hi, a));
  // Boxes overlap where they meet (e = 5 cm), so no two faces are coplanar: a ray meeting two
  // coplanar faces could take either, differently in f32 and f64.
  add([-t, -t, -t], [10 + t, 0, 5 + t], floor);                    // floor slab
  add([-t + 0.01, 3, -t + 0.01], [1.5, 3 + t, 5 + t - 0.01], wall); // roof around the skylight (x 1.5..3.5, z 1.5..3.5)
  add([3.5, 3, -t + 0.01], [10 + t - 0.01, 3 + t, 5 + t - 0.01], wall);
  add([1.5 - e, 3 + 0.01, -t + 0.02], [3.5 + e, 3 + t - 0.01, 1.5], wall);
  add([1.5 - e, 3 + 0.01, 3.5], [3.5 + e, 3 + t - 0.01, 5 + t - 0.02], wall);
  add([-t + 0.02, -e, -t + 0.02], [0, 3 + e, 5 + t - 0.02], wall); // end walls
  add([10, -e, -t + 0.02], [10 + t - 0.02, 3 + e, 5 + t - 0.02], wall);
  add([-e, -e, -t + 0.03], [10 + e, 3 + e, 0], wall);              // long walls
  add([-e, -e, 5], [10 + e, 3 + e, 5 + t - 0.03], wall);
  const px0 = 5, px1 = 5.04;                                       // the partition
  if (door) {
    add([px0, -e, -e], [px1, 3 + e, 3.8], wall);
    add([px0, -e, 4.6], [px1, 3 + e, 5 + e], wall);
    add([px0, 2.1, 3.8 - e], [px1 - 0.005, 3 + e, 4.6 + e], wall);
  } else add([px0, -e, -e], [px1, 3 + e, 5 + e], wall);
  add([7.2, -e, 1.2], [8.2, 0.9, 2.2], [0.7, 0.3, 0.25]);          // a block in each room
  add([1.0, -e, 3.6], [1.8, 1.2, 4.4], [0.25, 0.45, 0.7]);
  const camera = camRoom === "B" ? { eye: [9.6, 1.6, 0.4], target: [5.0, 0.9, 2.6], fovy: 1.1 } : { eye: [0.4, 1.6, 0.4], target: [5.0, 0.9, 2.6], fovy: 1.1 };
  return sceneFrom(list, { name: `leak-${door ? "door" : "closed"}-${camRoom}`, sunDir: [-0.42, 0.85, 0.3], sunE: [Math.PI, Math.PI, Math.PI], sky: [0.2, 0.2, 0.2], camera });
}

// The planted check (a white furnace): a closed 4 x 3 x 5 m box with a block inside, albedo 128/255 and
// emission 1 on every face, no sun. Its fixed point is L = 1 / (1 - rho) everywhere. The sky is set
// to that value too, so a probe outside the box sees it as well: started from the fixed point (cache
// = pi L), every correct estimator returns L exactly at every pixel, whatever it can see, which
// checks every normalisation in the method. The path tracer's answer for this box is
// sum_{k=0..8} rho^k (8 bounces, the sky never reached), its own closed form.
function furnaceScene() {
  const a = [0.5, 0.5, 0.5], e = [1, 1, 1], list = [];
  const add = (lo, hi) => list.push(...box(lo, hi, a, e));
  add([-0.2, -0.2, -0.2], [4.2, 0, 5.2]); add([-0.2, 3, -0.2], [4.2, 3.2, 5.2]);
  add([-0.2, 0, -0.2], [0, 3, 5.2]); add([4, 0, -0.2], [4.2, 3, 5.2]);
  add([0, 0, -0.2], [4, 3, 0]); add([0, 0, 5], [4, 3, 5.2]);
  add([1.5, 0, 2.0], [2.6, 1.4, 3.1]);
  const L = 1 / (1 - Math.fround(q8(0.5))); // the albedo as stored (f32)
  return sceneFrom(list, { name: "furnace", sky: [L, L, L], camera: { eye: [0.3, 2.2, 0.3], target: [2.2, 0.8, 2.6], fovy: 1.2 } });
}
export function furnaceExact(rho, bounces = Infinity) { if (bounces === Infinity) return 1 / (1 - rho); let s = 0; for (let k = 0; k <= bounces; k++) s += rho ** k; return s; }

export function buildScene3d(name) {
  if (name === "furnace") return furnaceScene();
  const m = /^leak-(door|closed)(?:-(A|B))?$/.exec(name);
  if (m) return leakScene(m[1] === "door", m[2] || "B");
  throw new Error(`unknown scene ${name}`);
}

export function writeScene(s) {
  const n = s.alb.length / 3, buf = new ArrayBuffer(12 + 64 + 96 * n), u8 = new Uint8Array(buf), dv = new DataView(buf);
  for (let i = 0; i < 8; i++) u8[i] = MAGIC.charCodeAt(i);
  dv.setUint32(8, n, true);
  const f = new Float32Array(buf, 12), c = s.camera;
  f.set([...s.sunDir, ...s.sunE, ...s.sky, ...c.eye, ...c.target, c.fovy], 0);
  for (let i = 0; i < n; i++) {
    const o = 16 + 24 * i;
    f.set(s.tris.subarray(9 * i, 9 * i + 9), o); f.set(s.nrm.subarray(9 * i, 9 * i + 9), o + 9);
    f.set(s.alb.subarray(3 * i, 3 * i + 3), o + 18); f.set(s.emit.subarray(3 * i, 3 * i + 3), o + 21);
  }
  return u8;
}

export function parseScene(bytes, name = "file") {
  const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (String.fromCharCode(...u8.subarray(0, 8)) !== MAGIC) throw new Error("not a gi scene file");
  const dv = new DataView(u8.buffer, u8.byteOffset), n = dv.getUint32(8, true);
  const f = new Float32Array(u8.buffer.slice(u8.byteOffset + 12, u8.byteOffset + 12 + 64 + 96 * n));
  const tris = new Float32Array(9 * n), nrm = new Float32Array(9 * n), alb = new Float32Array(3 * n), emit = new Float32Array(3 * n);
  for (let i = 0; i < n; i++) {
    const o = 16 + 24 * i;
    tris.set(f.subarray(o, o + 9), 9 * i); nrm.set(f.subarray(o + 9, o + 18), 9 * i);
    alb.set(f.subarray(o + 18, o + 21), 3 * i); emit.set(f.subarray(o + 21, o + 24), 3 * i);
  }
  const g = (k, m = 3) => Array.from(f.subarray(k, k + m));
  return { name, tris, nrm, alb, emit, sunDir: g(0), sunE: g(3), sky: g(6), camera: { eye: g(9), target: g(12), up: [0, 1, 0], fovy: f[15] } };
}

export function bounds(s) {
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < s.tris.length; i += 3) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], s.tris[i + k]); hi[k] = Math.max(hi[k], s.tris[i + k]); }
  return { lo, hi };
}

// The camera of the path tracer (rt_pt_cpu.cpp camOf and dirOf): pixel centre (x + 0.5, y + 0.5).
export function cameraBasis(c, W, H) {
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], nz = (v) => { const m = Math.hypot(...v); return v.map((x) => x / m); };
  const cr = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const f = nz(sub(c.target, c.eye)), s = nz(cr(f, c.up)), u = cr(s, f);
  return { eye: c.eye, f, s, u, th: Math.tan(c.fovy / 2), aspect: W / H };
}
export function pixelDir(k, x, y, W, H) {
  const nx = (x + 0.5) / W * 2 - 1, ny = 1 - (y + 0.5) / H * 2;
  const d = [0, 1, 2].map((i) => k.f[i] + k.s[i] * (nx * k.th * k.aspect) + k.u[i] * (ny * k.th)), m = Math.hypot(...d);
  return d.map((v) => v / m);
}
