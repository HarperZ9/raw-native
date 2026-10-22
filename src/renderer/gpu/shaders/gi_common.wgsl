// Split radiance cascades for diffuse indirect light (ROADMAP P6; evidence/p6-radiance-cascades-bounds.json):
// the shared code in front of gi.wgsl's and gi_merge.wgsl's passes.
// The GPU form of src/renderer/gi_scene.cpp, gi_trace.cpp and gi_cascades.cpp, in the same float32
// operation order up to the hit (the CPU continues in float64 after it). No compare-exchange: probes are
// allocated by a mark, a prefix sum (rt.wgsl's scan) and gi_keys; sums are u32 fixed point (1/4096).
//
// Buffers: GB G-buffer (2 vec4 a pixel: position and mask, normal); NI, NB the PLOC tree (rt.wgsl's
// layout); TA triangles (21 floats: 3 positions, 3 normals, albedo); CE cache (E rgb, frames seen) and CS
// its frame sums (r, g, b, count) a key; RR receiver rays (8 words: t, direction, albedo, cache key);
// SLOT the four cascades' key tables end to end (global slot or NONE); KEYS each global slot's key;
// ACC per cascade, slot and bin (entries, hits, r, g, b); RM merged radiance (4 floats a bin); SUB the
// cascade-0 sub-directions (bin, x, y, z, solid angle); SH 28 floats a cascade-0 slot; OUT I a pixel.

struct GiParams {
    w: u32, h: u32, stride: u32, frame: u32,
    rw: u32, rh: u32, root: u32, cascade: u32,
    count: u32, control: u32, nsub: u32, pad0: u32,
    origin: vec4f,          // xyz the tables' origin, w the cache cell
    light: vec4f,           // xyz towards the sun, w its intensity
    cell: vec4f,            // cascade cells 0..3
    rad: vec4f,             // interval ends 1..4
    misc: vec4f,            // x sky radiance
    cdim: vec4u,            // cache nx, ny, nz
    d0: vec4u, d1: vec4u, d2: vec4u, d3: vec4u,   // cascade nx, ny, nz, key base in SLOT
    sbase: vec4u,           // first global slot of each cascade
    nbin: vec4u,            // bins a side of each cascade
    abase: vec4u,           // first word in ACC of each cascade
    rbase: vec4u,           // first float in RM of each cascade
}
const BIG: f32 = 0x1.fffffep+127f;
const SLAB: f32 = 1.00000036;
const NONE: u32 = 0xFFFFFFFFu;
const FIXED: f32 = 4096.0;

fn vnorm(v: vec3f) -> vec3f {
    let l: f32 = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l > 0.0) { let s: f32 = 1.0 / l; return vec3f(v.x * s, v.y * s, v.z * s); }
    return v;
}
fn vcross(a: vec3f, b: vec3f) -> vec3f {
    return vec3f(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
fn vdot(a: vec3f, b: vec3f) -> f32 { return a.x * b.x + a.y * b.y + a.z * b.z; }
fn hash01(x: u32, y: u32, s: u32) -> f32 {
    var h: u32 = x * 374761393u + y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h = h ^ (h >> 16u);
    return f32(h & 0xFFFFFFu) / 16777216.0;
}
fn seedOf(f: u32, k: u32, axis: u32) -> u32 { return 2u * (f * 2u + k) + axis; }
// cos and sin of 2 pi u (x, y) by gi_scene.cpp's turn(): the octant centre rotated by a polynomial.
fn octant(o: u32) -> vec2f {
    if (o == 0u) { return vec2f(0.92387953, 0.38268343); }
    if (o == 1u) { return vec2f(0.38268343, 0.92387953); }
    if (o == 2u) { return vec2f(-0.38268343, 0.92387953); }
    if (o == 3u) { return vec2f(-0.92387953, 0.38268343); }
    if (o == 4u) { return vec2f(-0.92387953, -0.38268343); }
    if (o == 5u) { return vec2f(-0.38268343, -0.92387953); }
    if (o == 6u) { return vec2f(0.38268343, -0.92387953); }
    return vec2f(0.92387953, -0.38268343);
}
fn turn(u: f32) -> vec2f {
    let e: f32 = u * 8.0;
    let k: f32 = floor(e);
    let a: vec2f = octant(u32(clamp(k, 0.0, 7.0)));
    let x: f32 = (e - k - 0.5) * 0.78539816;
    let x2: f32 = x * x;
    let c0: f32 = 1.0 + x2 * (-0.5 + x2 * (0.041666668 + x2 * -0.0013888889));
    let s0: f32 = x * (1.0 + x2 * (-0.16666667 + x2 * (0.0083333338 + x2 * -0.00019841270)));
    return vec2f(a.x * c0 - a.y * s0, a.y * c0 + a.x * s0);
}
fn cosineDir(n: vec3f, u1: f32, u2: f32) -> vec3f {
    var a: vec3f = vec3f(1.0, 0.0, 0.0);
    if (abs(n.x) > 0.9) { a = vec3f(0.0, 1.0, 0.0); }
    let t: vec3f = vnorm(vcross(a, n));
    let b: vec3f = vcross(n, t);
    let r: f32 = sqrt(u1);
    let cs: vec2f = turn(u2);
    let lx: f32 = r * cs.x;
    let ly: f32 = r * cs.y;
    let lz: f32 = sqrt(max(0.0, 1.0 - u1));
    return vnorm(t * lx + b * ly + n * lz);
}
fn bucketOf(n: vec3f) -> u32 {
    let ax: f32 = abs(n.x);
    let ay: f32 = abs(n.y);
    let az: f32 = abs(n.z);
    if (ax >= ay && ax >= az) { return select(0u, 1u, n.x < 0.0); }
    if (ay >= az) { return select(2u, 3u, n.y < 0.0); }
    return select(4u, 5u, n.z < 0.0);
}
fn axisCell(v: f32, o: f32, cell: f32, n: u32) -> u32 {
    let f: f32 = floor((v - o) / cell);
    return u32(clamp(f, 0.0, f32(n - 1u)));
}
// A key of the dense table with dimensions d (xyz) and cell size cell.
fn keyOf(p: vec3f, b: u32, d: vec4u, cell: f32, o: vec3f) -> u32 {
    let x: u32 = axisCell(p.x, o.x, cell, d.x);
    let y: u32 = axisCell(p.y, o.y, cell, d.y);
    let z: u32 = axisCell(p.z, o.z, cell, d.z);
    return ((z * d.y + y) * d.x + x) * 6u + b;
}
fn signOf(v: f32) -> f32 { return select(1.0, -1.0, v < 0.0); }
fn octBin(d: vec3f, n: u32) -> u32 {
    let l1: f32 = abs(d.x) + abs(d.y) + abs(d.z);
    var u: f32 = d.x / l1;
    var v: f32 = d.y / l1;
    if (d.z < 0.0) {
        let a: f32 = (1.0 - abs(v)) * signOf(u);
        let b: f32 = (1.0 - abs(u)) * signOf(v);
        u = a;
        v = b;
    }
    let fn1: f32 = f32(n);
    let i: u32 = u32(clamp(floor((u * 0.5 + 0.5) * fn1), 0.0, fn1 - 1.0));
    let j: u32 = u32(clamp(floor((v * 0.5 + 0.5) * fn1), 0.0, fn1 - 1.0));
    return j * n + i;
}
fn dimOf(c: u32, d0: vec4u, d1: vec4u, d2: vec4u, d3: vec4u) -> vec4u {
    if (c == 0u) { return d0; }
    if (c == 1u) { return d1; }
    if (c == 2u) { return d2; }
    return d3;
}
fn pick(v: vec4f, c: u32) -> f32 {
    if (c == 0u) { return v.x; }
    if (c == 1u) { return v.y; }
    if (c == 2u) { return v.z; }
    return v.w;
}
fn pickU(v: vec4u, c: u32) -> u32 {
    if (c == 0u) { return v.x; }
    if (c == 1u) { return v.y; }
    if (c == 2u) { return v.z; }
    return v.w;
}
fn quantise(v: f32) -> u32 {
    if (v <= 0.0) { return 0u; }
    return u32(floor(v * FIXED + 0.5));
}
// The thread index of a one-dimensional dispatch folded into two dimensions (65,535 groups of 64 a row).
fn flat(gid: vec3u) -> u32 { return gid.y * 4194240u + gid.x; }
// Real spherical harmonics up to l = 2.
fn shY(n: vec3f, l: u32) -> f32 {
    if (l == 0u) { return 0.282095; }
    if (l == 1u) { return 0.488603 * n.y; }
    if (l == 2u) { return 0.488603 * n.z; }
    if (l == 3u) { return 0.488603 * n.x; }
    if (l == 4u) { return 1.092548 * n.x * n.y; }
    if (l == 5u) { return 1.092548 * n.y * n.z; }
    if (l == 6u) { return 0.315392 * (3.0 * n.z * n.z - 1.0); }
    if (l == 7u) { return 1.092548 * n.x * n.z; }
    return 0.546274 * (n.x * n.x - n.y * n.y);
}
