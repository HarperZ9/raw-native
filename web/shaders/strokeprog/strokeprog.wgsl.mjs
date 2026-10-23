// Stroke program rasteriser, WGSL. Two compute passes, both 16 x 16 groups over output pixels.
//   pass "fine":   per pixel, walk the stroke list of the plate tile it sits in (paint order), blend each stroke the way
//                  painter.mjs does (analytic coverage from the distance to the stroke's centre segment, ramp in output
//                  pixels), write canvas rgb and stroke height to a vec4 buffer.
//   pass "relief": the painter's impasto relief and tooth from the height plane, packed to RGBA8.
// Buffers: P (params), S (stroke records, 4 u32 each, see gpuRecords), T (tile offsets then lists), I (object ids, u8 packed
// 4 per u32, plate resolution), U (underpaint, vec4 per texel, 1/8 size), CV (canvas, vec4 per output pixel), O (RGBA8 out).
// Params (f32 unless noted): [0] W [1] H [2] outW [3] outH [4] scale [5] seed [6] tiles x [7] tile size [8] underW [9] underH
// [10] relief [11] edge px [12..15] radii.
export const STROKEPROG_WGSL = /* wgsl */ `
struct Params { v: array<vec4<f32>, 4> };
@group(0) @binding(0) var<storage, read> P: array<f32>;
@group(0) @binding(1) var<storage, read> S: array<vec4<u32>>;
@group(0) @binding(2) var<storage, read> T: array<u32>;
@group(0) @binding(3) var<storage, read> I: array<u32>;
@group(0) @binding(4) var<storage, read> U: array<vec4<f32>>;
@group(0) @binding(5) var<storage, read_write> CV: array<vec4<f32>>;
@group(0) @binding(6) var<storage, read_write> O: array<u32>;

fn pcg(v: u32) -> u32 {
  let state = v * 747796405u + 2891336453u;
  let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}
fn hash2(a: i32, b: i32, s: i32) -> f32 { return f32(pcg(u32(a) ^ pcg(u32(b) ^ pcg(u32(s))))) / 4294967296.0; }
fn smoothq(t: f32) -> f32 { return t * t * (3.0 - 2.0 * t); }
fn vnoise(x: f32, y: f32, s: i32) -> f32 {
  let fx0 = floor(x); let fy0 = floor(y);
  let x0 = i32(fx0); let y0 = i32(fy0); let fx = smoothq(x - fx0); let fy = smoothq(y - fy0);
  let a = hash2(x0, y0, s); let b = hash2(x0 + 1, y0, s); let c = hash2(x0, y0 + 1, s); let d = hash2(x0 + 1, y0 + 1, s);
  return (a * (1.0 - fx) + b * fx) * (1.0 - fy) + (c * (1.0 - fx) + d * fx) * fy;
}
fn sat(x: f32) -> f32 { return clamp(x, 0.0, 1.0); }
fn s16lo(w: u32) -> f32 { return f32(bitcast<i32>(w << 16u) >> 16u); }
fn s16hi(w: u32) -> f32 { return f32(bitcast<i32>(w) >> 16u); }
fn objId(x: i32, y: i32) -> u32 {
  let W = i32(P[0]); let H = i32(P[1]);
  let i = u32(clamp(y, 0, H - 1) * W + clamp(x, 0, W - 1));
  return (I[i >> 2u] >> ((i & 3u) * 8u)) & 255u;
}
fn under(x: f32, y: f32) -> vec3<f32> {
  let uw = i32(P[8]); let uh = i32(P[9]);
  let fx = clamp((x - 4.0) / 8.0, 0.0, f32(uw - 1)); let fy = clamp((y - 4.0) / 8.0, 0.0, f32(uh - 1));
  let x0 = i32(floor(fx)); let y0 = i32(floor(fy)); let x1 = min(uw - 1, x0 + 1); let y1 = min(uh - 1, y0 + 1);
  let tx = fx - floor(fx); let ty = fy - floor(fy);
  let a = mix(U[y0 * uw + x0].xyz, U[y0 * uw + x1].xyz, tx); let b = mix(U[y1 * uw + x0].xyz, U[y1 * uw + x1].xyz, tx);
  return mix(a, b, ty);
}

@compute @workgroup_size(16, 16)
fn fine(@builtin(global_invocation_id) g: vec3<u32>) {
  let ow = u32(P[2]); let oh = u32(P[3]);
  if (g.x >= ow || g.y >= oh) { return; }
  let s = P[4]; let seed = i32(P[5]); let edge = P[11];
  let p = (vec2<f32>(f32(g.x), f32(g.y)) + 0.5) / s - 0.5;
  var col = under(p.x, p.y); var hgt = 0.0;
  let tx = i32(P[6]); let tsz = P[7];
  let tile = u32(clamp(i32(floor(p.y / tsz)), 0, 100000) * tx + clamp(i32(floor(p.x / tsz)), 0, tx - 1));
  let id2 = objId(i32(floor(f32(g.x) / s)), i32(floor(f32(g.y) / s)));
  let a0 = T[tile]; let a1 = T[tile + 1u]; let base = u32(P[16]);
  for (var q = a0; q < a1; q = q + 1u) {
    let rec = S[T[base + q]];
    if ((rec.w & 255u) != id2) { continue; }
    let ax = s16lo(rec.x) / 8.0; let ay = s16hi(rec.x) / 8.0;
    let sx = s16lo(rec.y) / 8.0; let sy = s16hi(rec.y) / 8.0;
    let halfW = f32(rec.z >> 24u) / 16.0;
    let layer = (rec.w >> 8u) & 255u; let id0 = f32(rec.w >> 16u);
    let sl = max(1e-6, sx * sx + sy * sy); let len = sqrt(sx * sx + sy * sy);
    var d = vec2<f32>(1.0, 0.0); if (len > 0.0) { d = vec2<f32>(sx, sy) / len; }
    let rel = p - vec2<f32>(ax, ay);
    let tt = clamp((rel.x * sx + rel.y * sy) / sl, 0.0, 1.0);
    let dist = length(rel - vec2<f32>(sx, sy) * tt);
    var a = sat((halfW - dist) * s / edge + 0.5);
    if (a <= 0.0) { continue; }
    let r = P[12u + layer]; let freq = 2.2 / max(1.0, r * 0.35);
    let perp = rel.x * -d.y + rel.y * d.x;
    let stripe = 0.8 + 0.2 * vnoise(perp * freq, tt * sl * 0.0004 + id0, seed + 17);
    a = a * (stripe + 0.12);
    let sc = vec3<f32>(f32(rec.z & 255u), f32((rec.z >> 8u) & 255u), f32((rec.z >> 16u) & 255u)) / 255.0;
    col = col * (1.0 - a) + clamp(sc * (0.95 + 0.1 * stripe), vec3<f32>(0.0), vec3<f32>(1.0)) * a;
    hgt = hgt * (1.0 - a) + a * (stripe * (0.55 + 0.45 * (1.0 - dist / halfW)));
  }
  CV[g.y * ow + g.x] = vec4<f32>(col, hgt);
}

@compute @workgroup_size(16, 16)
fn relief(@builtin(global_invocation_id) g: vec3<u32>) {
  let ow = i32(P[2]); let oh = i32(P[3]); let x = i32(g.x); let y = i32(g.y);
  if (x >= ow || y >= oh) { return; }
  let s = P[4]; let seed = i32(P[5]);
  let hx = (CV[y * ow + min(ow - 1, x + 1)].w - CV[y * ow + max(0, x - 1)].w) ;
  let hy = (CV[min(oh - 1, y + 1) * ow + x].w - CV[max(0, y - 1) * ow + x].w);
  let lit = 1.0 + P[10] * (-0.8 * hx - 0.6 * hy);
  let tooth = 0.965 + 0.035 * vnoise(f32(x) * 1.1, f32(y) * 1.1, seed + 41);
  let c = clamp(CV[y * ow + x].xyz * lit * tooth, vec3<f32>(0.0), vec3<f32>(1.0));
  let b = vec3<u32>(floor(c * 255.0 + 0.5));
  O[y * ow + x] = b.x | (b.y << 8u) | (b.z << 16u) | (255u << 24u);
}
`;
