// A bounding volume hierarchy over a scene's triangles and its ray queries in float64 (the CPU
// reference). The WGSL in wgsl3d.mjs walks the same nodes in the same order.
//   const B = buildBvh(scene);    // B.nodes (Float32Array, 8 a node), B.order (scene triangle of each slot)
//   closestHit(B, o, d, tMin, tMax) -> { t, slot, u, v } or null;  occluded(B, o, d, tMax) -> bool
// Node layout: lo.xyz, A, hi.xyz, B with A and B read as u32. B = 0: interior, left child = node + 1,
// right child = A. B > 0: a leaf of B triangles from slot A. Binned SAH, 16 bins, at most 4 triangles
// a leaf. Boxes are rounded outward to f32 so an f32 ray test never misses what f64 hits.

const BINS = 16, LEAF = 4;

function f32Down(v) { const f = Math.fround(v); return f > v ? f - Math.max(Math.abs(f) * 2 ** -23, 1e-38) : f; }
function f32Up(v) { const f = Math.fround(v); return f < v ? f + Math.max(Math.abs(f) * 2 ** -23, 1e-38) : f; }

export function buildBvh(s) {
  const n = s.alb.length / 3, T = s.tris, cen = new Float64Array(3 * n), blo = new Float64Array(3 * n), bhi = new Float64Array(3 * n);
  for (let i = 0; i < n; i++) for (let k = 0; k < 3; k++) {
    const a = T[9 * i + k], b = T[9 * i + 3 + k], c = T[9 * i + 6 + k];
    blo[3 * i + k] = Math.min(a, b, c); bhi[3 * i + k] = Math.max(a, b, c); cen[3 * i + k] = (blo[3 * i + k] + bhi[3 * i + k]) / 2;
  }
  const idx = new Uint32Array(n); for (let i = 0; i < n; i++) idx[i] = i;
  const nodes = [], stack = [[0, n, -1]];
  const area = (lo, hi) => { const x = hi[0] - lo[0], y = hi[1] - lo[1], z = hi[2] - lo[2]; return x < 0 ? 0 : x * y + y * z + z * x; };
  while (stack.length) {
    const [a, b, parent] = stack.pop(), me = nodes.length;
    if (parent >= 0) nodes[parent].right = me;
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity], clo = [Infinity, Infinity, Infinity], chi = [-Infinity, -Infinity, -Infinity];
    for (let j = a; j < b; j++) for (let k = 0, i = idx[j]; k < 3; k++) {
      lo[k] = Math.min(lo[k], blo[3 * i + k]); hi[k] = Math.max(hi[k], bhi[3 * i + k]);
      clo[k] = Math.min(clo[k], cen[3 * i + k]); chi[k] = Math.max(chi[k], cen[3 * i + k]);
    }
    const node = { lo, hi, first: a, count: b - a, right: 0 };
    nodes.push(node);
    if (b - a <= LEAF) continue;
    let best = Infinity, bestAxis = -1, bestCut = 0;
    for (let k = 0; k < 3; k++) {
      const ext = chi[k] - clo[k];
      if (ext <= 0) continue;
      const cnt = new Int32Array(BINS), bl = [], bh = [];
      for (let q = 0; q < BINS; q++) { bl.push([Infinity, Infinity, Infinity]); bh.push([-Infinity, -Infinity, -Infinity]); }
      for (let j = a; j < b; j++) {
        const i = idx[j], q = Math.min(BINS - 1, Math.floor((cen[3 * i + k] - clo[k]) / ext * BINS));
        cnt[q]++;
        for (let m = 0; m < 3; m++) { bl[q][m] = Math.min(bl[q][m], blo[3 * i + m]); bh[q][m] = Math.max(bh[q][m], bhi[3 * i + m]); }
      }
      const rightCost = new Float64Array(BINS);
      { const l = [Infinity, Infinity, Infinity], h = [-Infinity, -Infinity, -Infinity]; let c = 0;
        for (let q = BINS - 1; q > 0; q--) { c += cnt[q]; for (let m = 0; m < 3; m++) { l[m] = Math.min(l[m], bl[q][m]); h[m] = Math.max(h[m], bh[q][m]); } rightCost[q] = c * area(l, h); } }
      const l = [Infinity, Infinity, Infinity], h = [-Infinity, -Infinity, -Infinity]; let c = 0;
      for (let q = 0; q < BINS - 1; q++) {
        c += cnt[q]; for (let m = 0; m < 3; m++) { l[m] = Math.min(l[m], bl[q][m]); h[m] = Math.max(h[m], bh[q][m]); }
        const cost = c * area(l, h) + rightCost[q + 1];
        if (c > 0 && c < b - a && cost < best) { best = cost; bestAxis = k; bestCut = q + 1; }
      }
    }
    let mid;
    if (bestAxis < 0) mid = (a + b) >> 1; // every centroid equal: split the list in two
    else {
      const k = bestAxis, ext = chi[k] - clo[k];
      let i = a, j = b - 1;
      while (i <= j) {
        const q = Math.min(BINS - 1, Math.floor((cen[3 * idx[i] + k] - clo[k]) / ext * BINS));
        if (q < bestCut) i++; else { const t = idx[i]; idx[i] = idx[j]; idx[j] = t; j--; }
      }
      mid = i;
      if (mid === a || mid === b) mid = (a + b) >> 1;
    }
    node.count = 0;
    stack.push([mid, b, me], [a, mid, -1]); // left is popped first, so it is node me + 1
  }
  const out = new Float32Array(8 * nodes.length), u = new Uint32Array(out.buffer);
  nodes.forEach((x, i) => {
    out.set([f32Down(x.lo[0]), f32Down(x.lo[1]), f32Down(x.lo[2])], 8 * i); out.set([f32Up(x.hi[0]), f32Up(x.hi[1]), f32Up(x.hi[2])], 8 * i + 4);
    u[8 * i + 3] = x.count ? x.first : x.right; u[8 * i + 7] = x.count;
  });
  return { nodes: out, order: idx, count: nodes.length };
}

// Ray against a node's box: entry distance, or Infinity.
function slab(N, i, o, inv, tMin, tMax) {
  let t0 = tMin, t1 = tMax;
  for (let k = 0; k < 3; k++) {
    let a = (N[8 * i + k] - o[k]) * inv[k], b = (N[8 * i + 4 + k] - o[k]) * inv[k];
    if (a > b) { const t = a; a = b; b = t; }
    if (a > t0) t0 = a; if (b < t1) t1 = b;
    if (t0 > t1) return Infinity;
  }
  return t0;
}
// Moller-Trumbore, two-sided: { t, u, v } with t in (tMin, tMax), or null.
export function triHit(P, o, d, tMin, tMax) {
  const e1 = [P[3] - P[0], P[4] - P[1], P[5] - P[2]], e2 = [P[6] - P[0], P[7] - P[1], P[8] - P[2]];
  const p = [d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]];
  const det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
  if (det === 0) return null;
  const id = 1 / det, s = [o[0] - P[0], o[1] - P[1], o[2] - P[2]], u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * id;
  if (u < 0 || u > 1) return null;
  const q = [s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]];
  const v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * id;
  if (v < 0 || u + v > 1) return null;
  const t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * id;
  return t > tMin && t < tMax ? { t, u, v } : null;
}
const inv3 = (d) => d.map((x) => 1 / (Math.abs(x) < 1e-20 ? (x < 0 ? -1e-20 : 1e-20) : x));

// B.tris holds the triangles in slot order (see prepare); `any` stops at the first hit.
function walk(B, o, d, tMin, tMax, any) {
  const N = B.nodes, U = B.u32, inv = inv3(d), st = [0];
  let best = null, tBest = tMax;
  const P = new Float64Array(9);
  while (st.length) {
    const i = st.pop();
    if (slab(N, i, o, inv, tMin, tBest) === Infinity) continue;
    const cnt = U[8 * i + 7];
    if (cnt) {
      for (let s = U[8 * i + 3], e = s + cnt; s < e; s++) {
        for (let k = 0; k < 9; k++) P[k] = B.tris[9 * s + k];
        const h = triHit(P, o, d, tMin, tBest);
        if (h) { best = { ...h, slot: s }; tBest = h.t; if (any) return best; }
      }
      continue;
    }
    const l = i + 1, r = U[8 * i + 3];
    const tl = slab(N, l, o, inv, tMin, tBest), tr = slab(N, r, o, inv, tMin, tBest);
    // Push the far child first so the near one is walked first (the WGSL does the same).
    if (tl <= tr) { if (tr !== Infinity) st.push(r); if (tl !== Infinity) st.push(l); }
    else { if (tl !== Infinity) st.push(l); st.push(r); }
  }
  return best;
}
export function closestHit(B, o, d, tMin, tMax) { return walk(B, o, d, tMin, tMax, false); }
export function occluded(B, o, d, tMax) { return walk(B, o, d, 0, tMax, true) !== null; }

// The scene's triangle data in slot order, for the CPU walk and for the GPU upload.
export function prepare(s, B) {
  const n = B.order.length, tris = new Float32Array(9 * n), nrm = new Float32Array(9 * n), alb = new Float32Array(3 * n), emit = new Float32Array(3 * n);
  for (let j = 0; j < n; j++) {
    const i = B.order[j];
    tris.set(s.tris.subarray(9 * i, 9 * i + 9), 9 * j); nrm.set(s.nrm.subarray(9 * i, 9 * i + 9), 9 * j);
    alb.set(s.alb.subarray(3 * i, 3 * i + 3), 3 * j); emit.set(s.emit.subarray(3 * i, 3 * i + 3), 3 * j);
  }
  return { ...B, u32: new Uint32Array(B.nodes.buffer), tris, nrm, alb, emit };
}
