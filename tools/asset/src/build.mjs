// The whole slice in one call: sculpt file and paint file in; LOD game meshes, UV atlas, baked maps, painted
// albedo, review sheets and a measured bounds record out. Deterministic: same inputs, same bytes.
import { Field } from "./sdf.mjs";
import { extract } from "./extract.mjs";
import { retopo, meshError, quadQuality } from "./retopo.mjs";
import { triangulate } from "./extract.mjs";
import { unwrap } from "./uv.mjs";
import { Volume, makeSampler, bakeMaps, dilate, curvByte, cavByte } from "./bake.mjs";
import { adaptiveLod, triError } from "./decimate.mjs";
import { paintAt } from "./paint.mjs";
import { packAsset, sha256, stringifySculpt } from "./format.mjs";
import { encodePng, decodePng } from "./png.mjs";
import { renderView, hstack, upscale, savePng, eyeParts } from "./render.mjs";

export const LODS = [{ name: "portrait", cell: 0.003, relax: 3, budget: 40000, tol: 0.0015 }, { name: "play", cell: 0.006, baseCell: 0.004, relax: 3, budget: 9000, tol: 0.0033 }, { name: "far", cell: 0.010, baseCell: 0.004, relax: 2, budget: 3000, tol: 0.006 }];
const now = () => Number(process.hrtime.bigint()) / 1e6;
const map8 = (a, W, H, ch = 1) => ({ w: W, h: H, c: ch, data: a });

export function buildAsset({ sculpt, paint, res = 1024, lods = LODS, refCell = 0.0015, log = () => {} }) {
  const T = {}, tick = (k, t0) => { T[k] = Math.round(now() - t0); log(`${k}: ${T[k]} ms`); };
  let t0 = now(); const F = new Field(sculpt.strokes), Fskin = new Field(sculpt.strokes, { exclude: new Set(["hair", "stache"]) }), B = F.bounds(0.004); tick("field", t0);
  const bounds = { sculpt: { strokes: sculpt.strokes.length, hash: sha256(stringifySculpt(sculpt)) }, lods: [], timingMs: T };
  if (refCell) { t0 = now(); const ref = extract(F, { lo: B.lo, hi: B.hi, cell: refCell }); bounds.reference = { cell: refCell, quads: ref.quads.length / 4, tris: ref.quads.length / 2, error: meshError(F, ref), quality: quadQuality(ref), gridEvals: ref.stats.evals, bricksSkipped: ref.stats.skipped, bricks: ref.stats.bricks }; tick("reference", t0); }
  t0 = now(); const vol = new Volume(F, B.lo, B.hi); const sampler = makeSampler(F, Fskin, vol); tick("volume", t0);
  const outLods = [], arrays = {};
  for (const L of lods) {
    t0 = now(); const M = retopo(F, { lo: B.lo, hi: B.hi, cell: L.cell, relax: L.relax }), V = M.pos.length / 3, col = new Float32Array(V * 3), curv = new Uint8Array(V), cav = new Uint8Array(V), hair = new Uint8Array(V);
    for (let v = 0; v < V; v++) { const S = sampler([M.pos[v * 3], M.pos[v * 3 + 1], M.pos[v * 3 + 2]], v); const c = paintAt(paint, S); col.set(c, v * 3); curv[v] = curvByte(S.kappa); cav[v] = cavByte(S.cav); hair[v] = S.hair; }
    let tri = triangulate(M), out = M, adaptive = null;
    const uniform = { tris: tri.length / 3, error: meshError(F, M) };
    if (L.budget && L.adaptive !== false) {   // curvature-adaptive: quadric collapse against the field, to the budget inside the tolerance
      const D = adaptiveLod(F, B, { ...L, base: { ...M, tri } }), W = D.pos.length / 3, c2 = new Float32Array(W * 3), cv2 = new Uint8Array(W), ca2 = new Uint8Array(W), h2 = new Uint8Array(W);
      for (let v = 0; v < W; v++) { const S = sampler([D.pos[v * 3], D.pos[v * 3 + 1], D.pos[v * 3 + 2]], v); c2.set(paintAt(paint, S), v * 3); cv2[v] = curvByte(S.kappa); ca2[v] = cavByte(S.cav); h2[v] = S.hair; }
      adaptive = { baseCell: D.baseCell, baseTris: D.baseTris, tris: D.tri.length / 3, error: triError(F, D), stats: D.stats, budget: L.budget, budgetMet: D.tri.length / 3 <= L.budget, tol: L.tol }; out = { ...M, pos: D.pos, nrm: D.nrm, quads: new Uint32Array(0) }; tri = D.tri; outLods.push({ ...L, M: out, uvM: M, col: c2, curv: cv2, cav: ca2, hair: h2, tri }); 
    } else outLods.push({ ...L, M, uvM: M, col, curv, cav, hair, tri });
    bounds.lods.push({ name: L.name, cell: L.cell, vertices: out.pos.length / 3, tris: tri.length / 3, uniform, adaptive, error: adaptive ? adaptive.error : meshError(F, M), quality: quadQuality(M) }); tick(`lod_${L.name}`, t0);
  }
  const P0 = outLods[0]; t0 = now(); const U = unwrap(P0.uvM, { res }); tick("uv", t0);
  bounds.uv = { res, charts: U.charts, texelsPerMetre: Math.round(U.density), stretch: U.stretch, seamVertices: U.pos.length / 3 - P0.uvM.pos.length / 3 };
  t0 = now(); const maps = bakeMaps(U, sampler, res); tick("bake", t0); bounds.bake = maps.stats;
  t0 = now(); const albedo = new Uint8Array(res * res * 3);
  for (let t = 0; t < res * res; t++) if (maps.cover[t]) { const S = { P: [maps.pos[t * 3], maps.pos[t * 3 + 1], maps.pos[t * 3 + 2]], N: [maps.nrmW[t * 3], maps.nrmW[t * 3 + 1], maps.nrmW[t * 3 + 2]], ao: maps.ao[t] / 255, thick: maps.thick[t] / 255 * 0.04, kappa: (maps.curv[t] === 128 ? 0 : Math.atanh((maps.curv[t] - 128) / 127) * 60), cav: (maps.meta[t] >> 1) / 127, hair: maps.meta[t] & 1 }; albedo.set(paintAt(paint, S).map((v) => Math.round(255 * Math.max(0, Math.min(1, v)))), t * 3); }
  for (const [img, ch] of [[albedo, 3], [maps.normal, 3], [maps.ao, 1], [maps.curv, 1], [maps.thick, 1]]) dilate(img, ch, maps.cover, res);
  tick("paint", t0);
  const eyes = eyeParts(sculpt), tex = { albedo, normal: maps.normal, ao: maps.ao, curv: maps.curv, thick: maps.thick };
  for (const L of outLods) { const k = L.name; arrays[`${k}.pos`] = L.M.pos; arrays[`${k}.nrm`] = L.M.nrm; arrays[`${k}.tri`] = L.tri; arrays[`${k}.col`] = L.col; arrays[`${k}.curv`] = L.curv; arrays[`${k}.cav`] = L.cav; arrays[`${k}.hair`] = L.hair; }
  arrays["portrait.uv.pos"] = U.pos; arrays["portrait.uv.nrm"] = U.nrm; arrays["portrait.uv.uv"] = U.uv; arrays["portrait.uv.quads"] = U.quads; arrays["portrait.uv.src"] = U.src;
  eyes.forEach((e, i) => { arrays[`eye${i}.pos`] = e.pos; arrays[`eye${i}.nrm`] = e.nrm; arrays[`eye${i}.tri`] = e.tri; });
  const meta = { name: sculpt.name, frame: sculpt.frame, sculptHash: bounds.sculpt.hash, lods: outLods.map((l) => ({ name: l.name, cell: l.cell })), eyes: eyes.map((e, i) => ({ array: `eye${i}`, material: e.material, color: e.color, spec: e.spec })), texRes: res };
  return { F, Fskin, B, bounds, outLods, U, maps, tex, eyes, arrays, meta, rna: packAsset(meta, arrays), res };
}

// review sheet: front, three-quarter, profile, and the play-size crop (700 px a metre, the figure pipeline's near)
export function reviewSheet(built, { centre = [0, -0.02, 0], height = 0.31, ref = null } = {}) {
  const { U, tex, eyes, res } = built, tri = triQuads(U.quads);
  const skin = { pos: U.pos, nrm: U.nrm, tri, uv: U.uv, albedo: map8(tex.albedo, res, res, 3), normalMap: map8(tex.normal, res, res, 3), ao: map8(tex.ao, res, res, 1), thin: map8(tex.thick.map((v) => 255 - v), res, res, 1) };
  const parts = [skin, ...eyes];
  const views = [0, 40, 90].map((yaw) => renderView(parts, { yaw, centre, height, W: 360, H: 440 }));
  const play = upscale(renderView(parts, { yaw: 25, centre, height: 0.31, W: 150, H: 217, ss: 2 }), 2);
  return hstack([...views, play]);
}
export const triQuads = (q) => { const t = []; for (let i = 0; i < q.length; i += 4) t.push(q[i], q[i + 1], q[i + 2], q[i], q[i + 2], q[i + 3]); return Uint32Array.from(t); };
export { encodePng, decodePng, savePng };
