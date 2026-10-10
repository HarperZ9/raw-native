// WGSL for palette quantisation with ordered, noise and blue-noise dither. It mirrors
// reference/retro-dither.mjs (orderedPick, farSideIndex, nearestIndex) and retro-engine's
// quantizeGrid front half, decision for decision, in double-single arithmetic so that its
// palette indices equal the f64 reference's.
export const DITHER_WGSL = /* wgsl */ `
@group(0) @binding(0) var<storage, read> P: array<f32>;
@group(0) @binding(1) var<storage, read> K: array<vec2f>;     // constants: lin[256], M1[9], M2[9], misc
@group(0) @binding(2) var<storage, read> pal: array<vec2f>;   // palette OKLab, 3 per entry
@group(0) @binding(3) var<storage, read> palrgb: array<u32>;
@group(0) @binding(4) var<storage, read> bn: array<u32>;      // blue-noise ranks, 64 x 64
@group(0) @binding(5) var<storage, read> src: array<u32>;     // RGBA8 plate
@group(0) @binding(6) var<storage, read_write> idx: array<u32>;
@group(0) @binding(7) var<storage, read_write> out8: array<u32>;
const K_M1: u32 = 256u; const K_M2: u32 = 265u; const K_EPS: u32 = 274u; const K_PEN: u32 = 275u;
const K_IGN: u32 = 276u; const K_BRIGHT: u32 = 279u; const K_STRENGTH: u32 = 280u;
fn palc(i: u32, c: u32) -> vec2f { return pal[3u * i + c]; }
fn mat3ds(m: u32, x: vec2f, y: vec2f, z: vec2f, row: u32) -> vec2f {
  return ds_add(ds_add(ds_mul(K[m + 3u * row], x), ds_mul(K[m + 3u * row + 1u], y)), ds_mul(K[m + 3u * row + 2u], z));
}
fn ds_floor(a: vec2f) -> vec2f { let f = floor(a.x); if (f == a.x) { return quick_two_sum(f, floor(a.y)); } return ds(f); }
fn dist2(L: vec2f, a: vec2f, b: vec2f, i: u32) -> vec2f {
  let dL = ds_sub(L, palc(i, 0u)); let da = ds_sub(a, palc(i, 1u)); let db = ds_sub(b, palc(i, 2u));
  return ds_add(ds_add(ds_mul(dL, dL), ds_mul(da, da)), ds_mul(db, db));
}
fn nearest(L: vec2f, a: vec2f, b: vec2f, n: u32) -> u32 {
  var best = 0u; var bestD = dist2(L, a, b, 0u);
  for (var i = 1u; i < n; i++) { let d = dist2(L, a, b, i); if (ds_lt(d, bestD)) { bestD = d; best = i; } }
  return best;
}
fn far_side(L: vec2f, a: vec2f, b: vec2f, i: u32, n: u32) -> i32 {
  let dL = ds_sub(L, palc(i, 0u)); let da = ds_sub(a, palc(i, 1u)); let db = ds_sub(b, palc(i, 2u));
  let d2 = ds_add(ds_add(ds_mul(dL, dL), ds_mul(da, da)), ds_mul(db, db));
  var best = -1; var bestS = vec2f(0.0); var have = false;
  for (var j = 0u; j < n; j++) {
    if (j == i) { continue; }
    let sL = ds_sub(palc(j, 0u), palc(i, 0u)); let sa = ds_sub(palc(j, 1u), palc(i, 1u)); let sb = ds_sub(palc(j, 2u), palc(i, 2u));
    let len2 = ds_add(ds_add(ds_mul(sL, sL), ds_mul(sa, sa)), ds_mul(sb, sb));
    if (ds_lt(len2, K[K_EPS])) { continue; }
    let u = ds_div(ds_add(ds_add(ds_mul(dL, sL), ds_mul(da, sa)), ds_mul(db, sb)), len2);
    if (ds_le(u, ds(0.0))) { continue; }
    let uc = ds_min(u, ds(1.0));
    let pen = ds_mul(ds_mul(K[K_PEN], len2), ds_add(ds_abs(ds_sub(uc, ds(0.5))), ds(0.5)));
    let score = ds_add(ds_sub(d2, ds_mul(ds_mul(uc, uc), len2)), pen);
    if (!have || ds_lt(score, bestS)) { bestS = score; best = i32(j); have = true; }
  }
  return best;
}
fn threshold(x: u32, y: u32, mode: u32) -> vec2f {
  if (mode == 1u) { let B = array<u32, 4>(0u, 2u, 3u, 1u); return ds((f32(B[(y % 2u) * 2u + x % 2u]) + 0.5) / 4.0); }
  if (mode == 2u) {
    let B = array<u32, 16>(0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u);
    return ds((f32(B[(y % 4u) * 4u + x % 4u]) + 0.5) / 16.0);
  }
  if (mode == 3u) {
    // Bayer 8 from bit interleaving: the same matrix as BAYER[8] in the reference.
    let xx = x % 8u; let yy = y % 8u; let z = xx ^ yy;
    let v = ((z & 1u) << 5u) | ((yy & 1u) << 4u) | ((z & 2u) << 2u) | ((yy & 2u) << 1u) | ((z & 4u) >> 1u) | ((yy & 4u) >> 2u);
    return ds((f32(v) + 0.5) / 64.0);
  }
  if (mode == 4u) {
    // Interleaved gradient noise (Jimenez 2014), in double-single as the f64 reference computes it.
    let f = ds_add(ds_mul(K[K_IGN], ds(f32(x))), ds_mul(K[K_IGN + 1u], ds(f32(y))));
    let v = ds_mul(K[K_IGN + 2u], ds_sub(f, ds_floor(f)));
    return ds_sub(v, ds_floor(v));
  }
  return ds((f32(bn[(y % 64u) * 64u + x % 64u]) + 0.5) / 4096.0);
}
@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) id: vec3u) {
  let W = u32(P[0]); let H = u32(P[1]); let n = u32(P[2]); let mode = u32(P[3]);
  if (id.x >= W || id.y >= H) { return; }
  let p = id.y * W + id.x; let c = src[p];
  let r = K[c & 255u]; let g = K[(c >> 8u) & 255u]; let b = K[(c >> 16u) & 255u];
  let l = ds_cbrt(mat3ds(K_M1, r, g, b, 0u)); let m = ds_cbrt(mat3ds(K_M1, r, g, b, 1u)); let s = ds_cbrt(mat3ds(K_M1, r, g, b, 2u));
  let L = ds_clamp01(ds_add(mat3ds(K_M2, l, m, s, 0u), K[K_BRIGHT]));
  let A = mat3ds(K_M2, l, m, s, 1u); let Bv = mat3ds(K_M2, l, m, s, 2u);
  var i = nearest(L, A, Bv, n);
  let strength = K[K_STRENGTH];
  if (mode != 0u && n >= 2u && ds_gt(strength, ds(0.0))) {
    let j = far_side(L, A, Bv, i, n);
    if (j >= 0) {
      let ju = u32(j);
      let sL = ds_sub(palc(ju, 0u), palc(i, 0u)); let sa = ds_sub(palc(ju, 1u), palc(i, 1u)); let sb = ds_sub(palc(ju, 2u), palc(i, 2u));
      let len2 = ds_add(ds_add(ds_mul(sL, sL), ds_mul(sa, sa)), ds_mul(sb, sb));
      if (!ds_lt(len2, K[K_EPS])) {
        let num = ds_add(ds_add(ds_mul(ds_sub(L, palc(i, 0u)), sL), ds_mul(ds_sub(A, palc(i, 1u)), sa)), ds_mul(ds_sub(Bv, palc(i, 2u)), sb));
        let f = ds_clamp01(ds_div(num, len2));
        let th = ds_add(ds(0.5), ds_mul(ds_sub(threshold(id.x, id.y, mode), ds(0.5)), strength));
        if (ds_gt(f, th)) { i = ju; }
      }
    }
  }
  idx[p] = i; out8[p] = palrgb[i];
}`;
