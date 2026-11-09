// UV atlas: charts by dominant direction (front, back, left, right, top, bottom), small charts folded into
// their neighbours, planar projection at one global texel density, shelf packing. Vertices split at seams.
// The stretch of every triangle is measured against the ideal single density and reported.
const AXES = [[0, 0, 1], [0, 0, -1], [1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0]];
const PROJ = [(p) => [p[0], -p[1]], (p) => [-p[0], -p[1]], (p) => [-p[2], -p[1]], (p) => [p[2], -p[1]], (p) => [p[0], -p[2]], (p) => [p[0], p[2]]];
const ekey = (a, b) => (a < b ? a * 4294967296 + b : b * 4294967296 + a);

function quadNeighbours(M) {
  const by = new Map(), nq = M.quads.length / 4, nb = Array.from({ length: nq }, () => []);
  for (let i = 0; i < nq; i++) for (let e = 0; e < 4; e++) { const k = ekey(M.quads[i * 4 + e], M.quads[i * 4 + (e + 1) % 4]); if (by.has(k)) { const j = by.get(k); nb[i].push(j); nb[j].push(i); } else by.set(k, i); }
  return nb;
}
function components(label, nb) {
  const comp = new Int32Array(label.length).fill(-1); let n = 0;
  for (let s = 0; s < label.length; s++) { if (comp[s] >= 0) continue; const st = [s]; comp[s] = n; while (st.length) { const f = st.pop(); for (const g of nb[f]) if (comp[g] < 0 && label[g] === label[f]) { comp[g] = n; st.push(g); } } n++; }
  return { comp, n };
}

export function chartLabels(M, minFaces = 10) {
  const nq = M.quads.length / 4, label = new Uint8Array(nq), nb = quadNeighbours(M);
  for (let i = 0; i < nq; i++) { let n = [0, 0, 0]; for (let e = 0; e < 4; e++) for (let c = 0; c < 3; c++) n[c] += M.nrm[M.quads[i * 4 + e] * 3 + c]; let best = 0, bd = -2; AXES.forEach((a, k) => { const d = a[0] * n[0] + a[1] * n[1] + a[2] * n[2]; if (d > bd) { bd = d; best = k; } }); label[i] = best; }
  for (let pass = 0; pass < 2; pass++) { const nx = Uint8Array.from(label); for (let i = 0; i < nq; i++) { const c = new Array(6).fill(0); c[label[i]] += 1.5; for (const j of nb[i]) c[label[j]]++; nx[i] = c.indexOf(Math.max(...c)); } label.set(nx); }
  for (let pass = 0; pass < 3; pass++) {   // fold charts under minFaces into the neighbour label they touch most
    const { comp, n } = components(label, nb), size = new Int32Array(n); for (const c of comp) size[c]++;
    let changed = false;
    for (let c = 0; c < n; c++) { if (size[c] >= minFaces) continue; const votes = new Array(6).fill(0); let own = -1; for (let i = 0; i < nq; i++) if (comp[i] === c) { own = label[i]; for (const j of nb[i]) if (comp[j] !== c) votes[label[j]]++; } const to = votes.indexOf(Math.max(...votes)); if (votes[to] > 0 && to !== own) { for (let i = 0; i < nq; i++) if (comp[i] === c) label[i] = to; changed = true; } }
    if (!changed) break;
  }
  return { label, ...components(label, nb) };
}

function shelfPack(boxes, res, padTx) {   // boxes: [{w, h}] in metres; returns the largest density that fits and each origin in texels
  const fit = (s) => { const order = boxes.map((b, i) => i).sort((a, b) => boxes[b].h - boxes[a].h || a - b), at = new Array(boxes.length); let x = 0, y = 0, rowH = 0;
    for (const i of order) { const w = Math.ceil(boxes[i].w * s) + 2 * padTx, h = Math.ceil(boxes[i].h * s) + 2 * padTx; if (w > res) return null; if (x + w > res) { x = 0; y += rowH; rowH = 0; } if (y + h > res) return null; at[i] = [x + padTx, y + padTx]; x += w; rowH = Math.max(rowH, h); }
    return at; };
  let lo = 1, hi = res * 40, best = fit(lo); if (!best) throw new Error("uv: charts do not fit at 1 texel a metre");
  for (let i = 0; i < 40; i++) { const mid = (lo + hi) / 2, f = fit(mid); if (f) { lo = mid; best = f; } else hi = mid; }
  return { density: lo, at: best };
}

export function unwrap(M, { res = 1024, padTx = 6 } = {}) {
  const { label, comp, n } = chartLabels(M), P = M.pos, charts = [];
  for (let c = 0; c < n; c++) charts.push({ id: c, label: -1, lo: [1e9, 1e9], hi: [-1e9, -1e9], faces: [] });
  const nq = M.quads.length / 4;
  for (let i = 0; i < nq; i++) { const ch = charts[comp[i]]; ch.label = label[i]; ch.faces.push(i); for (let e = 0; e < 4; e++) { const v = M.quads[i * 4 + e], uv = PROJ[ch.label]([P[v * 3], P[v * 3 + 1], P[v * 3 + 2]]); for (let a = 0; a < 2; a++) { ch.lo[a] = Math.min(ch.lo[a], uv[a]); ch.hi[a] = Math.max(ch.hi[a], uv[a]); } } }
  const { density, at } = shelfPack(charts.map((c) => ({ w: c.hi[0] - c.lo[0], h: c.hi[1] - c.lo[1] })), res, padTx);
  const remap = new Map(), pos = [], nrm = [], uv = [], src = [], quads = [], chartOf = [];
  for (const ch of charts) for (const f of ch.faces) for (let e = 0; e < 4; e++) {
    const v = M.quads[f * 4 + e], key = v * 100000 + ch.id; let nvx = remap.get(key);
    if (nvx === undefined) { nvx = pos.length / 3; remap.set(key, nvx); const p = [P[v * 3], P[v * 3 + 1], P[v * 3 + 2]], t = PROJ[ch.label](p); pos.push(...p); nrm.push(M.nrm[v * 3], M.nrm[v * 3 + 1], M.nrm[v * 3 + 2]); uv.push((at[ch.id][0] + (t[0] - ch.lo[0]) * density) / res, (at[ch.id][1] + (t[1] - ch.lo[1]) * density) / res); src.push(v); }
    quads.push(nvx);
  }
  for (const ch of charts) for (let k = 0; k < ch.faces.length; k++) chartOf.push(ch.id);
  const out = { pos: Float32Array.from(pos), nrm: Float32Array.from(nrm), uv: Float32Array.from(uv), quads: Uint32Array.from(quads), src: Uint32Array.from(src), res, density, charts: n };
  out.stretch = uvStretch(out);
  return out;
}

export function uvStretch(U) {
  const r = [], p = U.pos, nq = U.quads.length / 4;
  for (let i = 0; i < nq; i++) for (const [a, b, c] of [[0, 1, 2], [0, 2, 3]]) {
    const v = [U.quads[i * 4 + a], U.quads[i * 4 + b], U.quads[i * 4 + c]], e1 = [0, 1, 2].map((k) => p[v[1] * 3 + k] - p[v[0] * 3 + k]), e2 = [0, 1, 2].map((k) => p[v[2] * 3 + k] - p[v[0] * 3 + k]);
    const area3 = Math.hypot(e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]) / 2;
    const u1 = [(U.uv[v[1] * 2] - U.uv[v[0] * 2]) * U.res, (U.uv[v[1] * 2 + 1] - U.uv[v[0] * 2 + 1]) * U.res], u2 = [(U.uv[v[2] * 2] - U.uv[v[0] * 2]) * U.res, (U.uv[v[2] * 2 + 1] - U.uv[v[0] * 2 + 1]) * U.res];
    const areaT = Math.abs(u1[0] * u2[1] - u1[1] * u2[0]) / 2; if (area3 < 1e-12) continue;
    const ratio = areaT / (area3 * U.density * U.density); r.push(Math.max(ratio, 1 / Math.max(ratio, 1e-9)));
  }
  r.sort((x, y) => x - y);
  return { p50: r[Math.floor(r.length * 0.5)], p95: r[Math.floor(r.length * 0.95)], p99: r[Math.floor(r.length * 0.99)], max: r[r.length - 1], tris: r.length };
}
