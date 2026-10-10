// raw-native GPU shadow passes (ROADMAP M3 shadows): cascaded shadow-map lookups (hard, PCF
// and PCSS) and the screen-space contact-shadow march, the float32 forms of
// raw::shadows::lookup and raw::shadows::contactShadows (src/renderer/shadows.cpp and
// src/renderer/shadow_raster.cpp). The maps come from the shadow_depth raster pass.

struct ShadowParams {
    count: u32, size: u32, width: u32, height: u32,
    steps: u32, pad0: u32, pad1: u32, pad2: u32,
    right: vec4f,
    up: vec4f,
    light: vec4f,
    box: array<vec4f, 4>,      // per cascade: light-space centre x, y, radius, split (view depth)
    span: array<vec4f, 4>,     // per cascade: zNear, zFar
    cfg: vec4f,                // tan(sun angle), normal offset (texels), depth bias (texels), contact thickness
    vp: array<vec4f, 4>,       // contact: the camera's view-projection rows
    march: vec4f,              // contact: direction to the light (unit), march length
}
// A point: 8 floats. 0..2 world position, 3 view depth, 4..6 unit normal, 7 valid (1) or not (0).
const POINT: u32 = 8u;

//@pass shadow_lookup
// One invocation per point: its cascade, then the hard, PCF (5 x 5 tent) and PCSS
// visibilities, written as three floats. M holds the four maps' normalised depths, size x size
// each, in cascade order; T the tap table of raw::shadows::poissonTaps (16 blocker taps, then
// 25 filter taps, as x, y pairs).
@group(0) @binding(0) var<uniform> P: ShadowParams;
@group(0) @binding(1) var<storage, read> M: array<f32>;
@group(0) @binding(2) var<storage, read> Q: array<f32>;
@group(0) @binding(3) var<storage, read> T: array<f32>;
@group(0) @binding(4) var<storage, read_write> O: array<f32>;

fn cascade_of(depth: f32) -> i32 {
    for (var k: i32 = 0; k < 4; k++) {
        if (depth <= P.box[k].w) { return k; }
    }
    return -1;
}
fn map_depth(k: i32, ix: i32, iy: i32) -> f32 {
    let s: i32 = i32(P.size);
    return M[u32(k * s * s + iy * s + ix)];
}
fn inside(ix: i32, iy: i32) -> bool {
    let s: i32 = i32(P.size);
    return ix >= 0 && iy >= 0 && ix < s && iy < s;
}
fn lit(k: i32, x: f32, y: f32, d: f32) -> f32 {
    let ix: i32 = i32(floor(x));
    let iy: i32 = i32(floor(y));
    if (!inside(ix, iy)) { return 1.0; }
    return select(0.0, 1.0, d <= map_depth(k, ix, iy));
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= P.count) { return; }
    let b: u32 = i * POINT;
    O[3u * i] = 1.0;
    O[3u * i + 1u] = 1.0;
    O[3u * i + 2u] = 1.0;
    if (Q[b + 7u] == 0.0) { return; }
    let k: i32 = cascade_of(Q[b + 3u]);
    if (k < 0) { return; }
    let cx: f32 = P.box[k].x;
    let cy: f32 = P.box[k].y;
    let r: f32 = P.box[k].z;
    let zn: f32 = P.span[k].x;
    let range: f32 = P.span[k].y - zn;
    let sz: f32 = f32(P.size);
    let tw: f32 = 2.0 * r / sz;
    let off: f32 = P.cfg.y * tw;
    let q: vec3f = vec3f(Q[b] + Q[b + 4u] * off, Q[b + 1u] + Q[b + 5u] * off, Q[b + 2u] + Q[b + 6u] * off);
    let lx: f32 = q.x * P.right.x + q.y * P.right.y + q.z * P.right.z;
    let ly: f32 = q.x * P.up.x + q.y * P.up.y + q.z * P.up.z;
    let lz: f32 = q.x * P.light.x + q.y * P.light.y + q.z * P.light.z;
    let u: f32 = (lx - cx) / r;
    let v: f32 = (ly - cy) / r;
    let d: f32 = (lz - zn) / range - P.cfg.z * tw / range;
    let fx: f32 = (u * 0.5 + 0.5) * sz;
    let fy: f32 = (1.0 - (v * 0.5 + 0.5)) * sz;
    O[3u * i] = lit(k, fx, fy, d);
    var s: f32 = 0.0;
    for (var bb: i32 = -2; bb <= 2; bb++) {
        for (var aa: i32 = -2; aa <= 2; aa++) {
            s += f32((3 - abs(aa)) * (3 - abs(bb))) * lit(k, fx + f32(aa), fy + f32(bb), d);
        }
    }
    O[3u * i + 1u] = s / 81.0;
    let tanSun: f32 = P.cfg.x;
    let rs: f32 = clamp(10.0 * tanSun / tw, 1.0, 16.0);
    var blocker: f32 = 0.0;
    var found: i32 = 0;
    for (var t: u32 = 0u; t < 16u; t++) {
        let ix: i32 = i32(floor(fx + T[2u * t] * rs));
        let iy: i32 = i32(floor(fy + T[2u * t + 1u] * rs));
        if (inside(ix, iy)) {
            let z: f32 = map_depth(k, ix, iy);
            if (z < d) { blocker += z; found += 1; }
        }
    }
    if (found == 0) { return; }
    let penumbra: f32 = (d - blocker / f32(found)) * range * tanSun;
    let rk: f32 = clamp(penumbra / tw, 1.0, 16.0);
    var f: f32 = 0.0;
    for (var j: u32 = 16u; j < 41u; j++) {
        f += lit(k, fx + T[2u * j] * rk, fy + T[2u * j + 1u] * rk, d);
    }
    O[3u * i + 2u] = f / 25.0;
}

//@pass shadow_contact
// One invocation per pixel: march from the pixel's surface toward the light; a step that lands
// behind the depth buffer's surface by less than the thickness is an occluder. D holds the
// view depth per pixel, negative where nothing was drawn.
@group(0) @binding(0) var<uniform> P: ShadowParams;
@group(0) @binding(1) var<storage, read> D: array<f32>;
@group(0) @binding(2) var<storage, read> Q: array<f32>;
@group(0) @binding(3) var<storage, read_write> O: array<f32>;

fn mrow(r: vec4f, v: vec4f) -> f32 { return r.x * v.x + r.y * v.y + r.z * v.z + r.w * v.w; }

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= P.width * P.height) { return; }
    var vis: f32 = 1.0;
    if (D[i] >= 0.0) {
        let b: u32 = i * POINT;
        let w: i32 = i32(P.width);
        let h: i32 = i32(P.height);
        for (var s: u32 = 1u; s <= P.steps; s++) {
            let t: f32 = P.march.w * f32(s) / f32(P.steps);
            let p: vec4f = vec4f(Q[b] + P.march.x * t, Q[b + 1u] + P.march.y * t, Q[b + 2u] + P.march.z * t, 1.0);
            let cw: f32 = mrow(P.vp[3], p);
            if (cw <= 1.0e-6) { break; }
            let sx: i32 = i32(floor((mrow(P.vp[0], p) / cw * 0.5 + 0.5) * f32(w)));
            let sy: i32 = i32(floor((1.0 - (mrow(P.vp[1], p) / cw * 0.5 + 0.5)) * f32(h)));
            if (sx < 0 || sy < 0 || sx >= w || sy >= h) { break; }
            let d: f32 = D[u32(sy * w + sx)];
            if (d >= 0.0 && cw > d + 1.0e-3 && cw - d < P.cfg.w) { vis = 0.0; break; }
        }
    }
    O[i] = vis;
}
