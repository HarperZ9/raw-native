// Surface nets on a sparse brick grid. Bricks the stroke boxes cannot reach, and bricks far from the
// surface by a Lipschitz margin, are never evaluated; the rest are sampled at every lattice point.
// Each cell vertex is then projected onto the zero set (Newton), which keeps the error under the
// DESIGN.md bound where a plain surface net would sit up to a cell diagonal off.
import { project } from "./sdf.mjs";

const BRICK = 8, LIP = 2.5;   // 8^3 lattice points a brick; field distances are first-order, so skip with 2.5x margin
export function sampleGrid(F, lo, hi, cell, { sparse = true } = {}) {
  const n = [0, 1, 2].map((i) => Math.ceil((hi[i] - lo[i]) / cell) + 1), [nx, ny, nz] = n, g = new Float32Array(nx * ny * nz);
  const idx = (i, j, k) => (k * ny + j) * nx + i;
  let evals = 0, bricks = 0, skipped = 0;
  for (let bk = 0; bk < nz; bk += BRICK) for (let bj = 0; bj < ny; bj += BRICK) for (let bi = 0; bi < nx; bi += BRICK) {
    const i1 = Math.min(bi + BRICK, nx), j1 = Math.min(bj + BRICK, ny), k1 = Math.min(bk + BRICK, nz);
    const blo = [lo[0] + bi * cell, lo[1] + bj * cell, lo[2] + bk * cell], bhi = [lo[0] + (i1 - 1) * cell, lo[1] + (j1 - 1) * cell, lo[2] + (k1 - 1) * cell];
    const half = Math.hypot(bhi[0] - blo[0], bhi[1] - blo[1], bhi[2] - blo[2]) / 2;
    let fill = null;
    if (sparse) {
      if (!F.touches([blo[0] - cell, blo[1] - cell, blo[2] - cell], [bhi[0] + cell, bhi[1] + cell, bhi[2] + cell], cell)) fill = 1;
      else { const dc = F.d((blo[0] + bhi[0]) / 2, (blo[1] + bhi[1]) / 2, (blo[2] + bhi[2]) / 2); evals++; if (Math.abs(dc) > half * LIP + cell) fill = Math.sign(dc) * Math.max(Math.abs(dc) - half * LIP, cell); }
    }
    bricks++;
    if (fill !== null) { skipped++; for (let k = bk; k < k1; k++) for (let j = bj; j < j1; j++) for (let i = bi; i < i1; i++) g[idx(i, j, k)] = fill; continue; }
    for (let k = bk; k < k1; k++) for (let j = bj; j < j1; j++) for (let i = bi; i < i1; i++) { g[idx(i, j, k)] = F.d(lo[0] + i * cell, lo[1] + j * cell, lo[2] + k * cell); evals++; }
  }
  return { g, n, lo, cell, idx, stats: { evals, bricks, skipped } };
}

const CORNER = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0], [0, 0, 1], [1, 0, 1], [0, 1, 1], [1, 1, 1]];
const EDGES = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
const AX = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];

function cellVertices(F, G, projectVerts, maxMove) {
  const [nx, ny, nz] = G.n, { g, idx, lo, cell } = G, cx = nx - 1, cy = ny - 1, cid = new Int32Array(cx * cy * (nz - 1)).fill(-1), pos = [];
  for (let k = 0; k < nz - 1; k++) for (let j = 0; j < ny - 1; j++) for (let i = 0; i < nx - 1; i++) {
    const v = CORNER.map(([a, b, c]) => g[idx(i + a, j + b, k + c)]); let neg = 0; for (let c = 0; c < 8; c++) if (v[c] < 0) neg++;
    if (neg === 0 || neg === 8) continue;
    let sx = 0, sy = 0, sz = 0, cnt = 0;
    for (const [a, b] of EDGES) { if ((v[a] < 0) === (v[b] < 0)) continue; const t = v[a] / (v[a] - v[b]); sx += CORNER[a][0] + t * (CORNER[b][0] - CORNER[a][0]); sy += CORNER[a][1] + t * (CORNER[b][1] - CORNER[a][1]); sz += CORNER[a][2] + t * (CORNER[b][2] - CORNER[a][2]); cnt++; }
    let p = [lo[0] + (i + sx / cnt) * cell, lo[1] + (j + sy / cnt) * cell, lo[2] + (k + sz / cnt) * cell];
    if (projectVerts) { const q = project(F, p, 4, maxMove ?? cell); p = [q[0], q[1], q[2]]; }
    cid[(k * cy + j) * cx + i] = pos.length / 3; pos.push(p[0], p[1], p[2]);
  }
  return { cid, pos };
}

export function extract(F, { lo, hi, cell, projectVerts = true, maxMove = null, sparse = true }) {
  const G = sampleGrid(F, lo, hi, cell, { sparse }), [nx, ny, nz] = G.n, { g, idx } = G, cx = nx - 1, cy = ny - 1;
  const { cid, pos } = cellVertices(F, G, projectVerts, maxMove), quads = [];
  const cellAt = (i, j, k) => (i < 0 || j < 0 || k < 0 || i >= cx || j >= cy || k >= nz - 1) ? -1 : cid[(k * cy + j) * cx + i];
  for (let k = 0; k < nz; k++) for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) for (let a = 0; a < 3; a++) {
    const i2 = i + AX[a][0], j2 = j + AX[a][1], k2 = k + AX[a][2]; if (i2 >= nx || j2 >= ny || k2 >= nz) continue;
    const d0 = g[idx(i, j, k)], d1 = g[idx(i2, j2, k2)]; if ((d0 < 0) === (d1 < 0)) continue;
    const b = (a + 1) % 3, c = (a + 2) % 3, off = (u, w) => { const o = [0, 0, 0]; o[b] = u; o[c] = w; return cellAt(i + o[0], j + o[1], k + o[2]); };
    const q = [off(0, 0), off(-1, 0), off(-1, -1), off(0, -1)]; if (q.includes(-1)) continue;
    quads.push(...(d0 < 0 ? q : [q[3], q[2], q[1], q[0]]));
  }
  const P = Float32Array.from(pos), N = new Float32Array(P.length);
  for (let v = 0; v < P.length; v += 3) { const n = F.grad(P[v], P[v + 1], P[v + 2]); N[v] = n[0]; N[v + 1] = n[1]; N[v + 2] = n[2]; }
  return { pos: P, nrm: N, quads: Uint32Array.from(quads), stats: G.stats, cell };
}

// two triangles a quad, split on the shorter diagonal
export function triangulate(M) {
  const tri = [], p = M.pos, dist = (a, b) => Math.hypot(p[a * 3] - p[b * 3], p[a * 3 + 1] - p[b * 3 + 1], p[a * 3 + 2] - p[b * 3 + 2]);
  for (let q = 0; q < M.quads.length; q += 4) { const [a, b, c, d] = [M.quads[q], M.quads[q + 1], M.quads[q + 2], M.quads[q + 3]]; if (dist(a, c) <= dist(b, d)) tri.push(a, b, c, a, c, d); else tri.push(a, b, d, b, c, d); }
  return Uint32Array.from(tri);
}
