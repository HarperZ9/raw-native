// raw-native GPU lighting passes: clustered light assignment and the GGX prefilter of an
// environment cube, the float32 forms of src/renderer/lighting_clusters.cpp and
// raw/renderer/lighting_cube.hpp. cube.wgsl sits in front of this module's shared code.

struct LightParams {
    nlights: u32, nclusters: u32, size: u32, level: u32,
    samples: u32, levels: u32, pad0: u32, pad1: u32,
    grid: vec4f,
}
// A light: 16 floats. 0 type (0 directional, 1 point, 2 spot), 1..3 position, 4..6 direction,
// 7..9 colour, 10 intensity, 11 range (0: unlimited), 12 spot scale, 13 spot offset.
const LIGHT: u32 = 16u;
const MAX_PER_CLUSTER: u32 = 256u;

//@pass light_cluster
// One invocation per cluster: its view-space box from the same corners as
// raw::lighting::ClusterGrid::bounds, then Arvo's sphere-box test against every light in
// index order. K holds the counts (nclusters u32) and then the lists, 256 slots a cluster.
@group(0) @binding(0) var<uniform> P: LightParams;
@group(0) @binding(1) var<storage, read> L: array<f32>;
@group(0) @binding(2) var<storage, read_write> K: array<u32>;

fn axis_d2(c: f32, a: f32, b: f32) -> f32 {
    if (c < a) { return (a - c) * (a - c); }
    if (c > b) { return (c - b) * (c - b); }
    return 0.0;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let c: u32 = gid.x;
    if (c >= P.nclusters) { return; }
    let ix: u32 = c % 16u;
    let iy: u32 = (c / 16u) % 9u;
    let iz: u32 = c / 144u;
    let th: f32 = P.grid.x;
    let x0: f32 = -1.0 + 2.0 * f32(ix) / 16.0;
    let x1: f32 = -1.0 + 2.0 * f32(ix + 1u) / 16.0;
    let y0: f32 = -1.0 + 2.0 * f32(iy) / 9.0;
    let y1: f32 = -1.0 + 2.0 * f32(iy + 1u) / 9.0;
    let ratio: f32 = P.grid.w / P.grid.z;
    let d0: f32 = P.grid.z * pow(ratio, f32(iz) / 24.0);
    let d1: f32 = P.grid.z * pow(ratio, f32(iz + 1u) / 24.0);
    var lo: vec3f = vec3f(3.0e38);
    var hi: vec3f = vec3f(-3.0e38);
    for (var k: u32 = 0u; k < 8u; k++) {
        let d: f32 = select(d0, d1, (k & 1u) == 1u);
        let nx: f32 = select(x0, x1, (k & 2u) == 2u);
        let ny: f32 = select(y0, y1, (k & 4u) == 4u);
        let q: vec3f = vec3f(nx * d * th * P.grid.y, ny * d * th, -d);
        lo = min(lo, q);
        hi = max(hi, q);
    }
    var n: u32 = 0u;
    let base: u32 = P.nclusters + c * MAX_PER_CLUSTER;
    for (var i: u32 = 0u; i < P.nlights; i++) {
        let b: u32 = i * LIGHT;
        let range: f32 = L[b + 11u];
        var touch: bool = L[b] == 0.0 || range <= 0.0;
        if (!touch) {
            let d2: f32 = axis_d2(L[b + 1u], lo.x, hi.x) + axis_d2(L[b + 2u], lo.y, hi.y) + axis_d2(L[b + 3u], lo.z, hi.z);
            touch = d2 <= range * range;
        }
        if (touch && n < MAX_PER_CLUSTER) {
            K[base + n] = i;
            n = n + 1u;
        }
    }
    K[c] = n;
}

//@pass light_prefilter
// One invocation per texel of level P.level: the GGX prefilter of
// raw::lighting::cube::prefilterTexel, reading level 0 bilinearly. The half-vector
// samples come from the host (S: cos theta, sin theta cos phi, sin theta sin phi for each
// sample of this level), computed in double: in-shader sin and cos carry about 1e-6 error
// on SwiftShader, and the sun's 1e5 nits turned that into a 1.2e-3 relative error.
@group(0) @binding(0) var<uniform> P: LightParams;
@group(0) @binding(1) var<storage, read_write> E: array<f32>;
@group(0) @binding(2) var<storage, read> S: array<f32>;

fn texel0(f: u32, y: i32, x: i32) -> vec3f {
    let t: vec3u = cube_tap(P.size, f, y, x);
    let i: u32 = ((t.x * P.size + t.y) * P.size + t.z) * 3u;
    return vec3f(E[i], E[i + 1u], E[i + 2u]);
}
fn sample0(d: vec3f) -> vec3f {
    let fu: vec3f = cube_face(d);
    let f: u32 = u32(fu.x);
    let fx: f32 = fu.y * f32(P.size) - 0.5;
    let fy: f32 = fu.z * f32(P.size) - 0.5;
    let x0: i32 = i32(floor(fx));
    let y0: i32 = i32(floor(fy));
    let ax: f32 = fx - f32(x0);
    let ay: f32 = fy - f32(y0);
    let a: vec3f = texel0(f, y0, x0);
    let b: vec3f = texel0(f, y0, x0 + 1);
    let c: vec3f = texel0(f, y0 + 1, x0);
    let e: vec3f = texel0(f, y0 + 1, x0 + 1);
    return (a * (1.0 - ax) + b * ax) * (1.0 - ay) + (c * (1.0 - ax) + e * ax) * ay;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let n: u32 = cube_level_size(P.size, P.level);
    if (gid.x >= 6u * n * n) { return; }
    let f: u32 = gid.x / (n * n);
    let y: u32 = (gid.x / n) % n;
    let x: u32 = gid.x % n;
    let nd: vec3f = cube_dir(f, (f32(x) + 0.5) / f32(n), (f32(y) + 0.5) / f32(n));
    var t: vec3f = vec3f(0.0);
    var bb: vec3f = vec3f(0.0);
    onb(nd, &t, &bb);
    var sum: vec3f = vec3f(0.0);
    var w: f32 = 0.0;
    for (var k: u32 = 0u; k < P.samples; k++) {
        let s3: u32 = k * 3u;
        let h: vec3f = t * S[s3 + 1u] + bb * S[s3 + 2u] + nd * S[s3];
        let nh: f32 = dot(nd, h);
        let l: vec3f = 2.0 * nh * h - nd;
        let nl: f32 = dot(nd, l);
        if (nl <= 0.0) { continue; }
        sum = sum + sample0(l) * nl;
        w = w + nl;
    }
    let o: u32 = cube_level_offset(P.size, P.level) + gid.x * 3u;
    let v: vec3f = sum / w;
    E[o] = v.x; E[o + 1u] = v.y; E[o + 2u] = v.z;
}
