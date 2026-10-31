// raw-native's ray-tracing passes (RT stage R2): the compute PLOC build of
// src/renderer/rt_bvh.cpp, in the same float32 operation order, and BVH traversal. No
// atomics: the sort is bitonic, node ids and survivor positions come from prefix sums, so the
// tree does not depend on scheduling.
//
// Buffers: T triangles (9 floats each, input order); NI nodes (left, right, tri, pad; a leaf
// has tri >= 0); NB node boxes (lo xyz, hi xyz, pad 2); K, V sort keys (code, triangle);
// C cluster node ids; NN nearest neighbours; S prefix sums; G globals.

struct RtParams {
    n: u32, m: u32, radius: u32, nextId: u32,
    k: u32, j: u32, count: u32, mode: u32,
    farScale: f32, pad0: f32, pad1: f32, pad2: f32,
}
const BIG: f32 = 0x1.fffffep+127f;   // FLT_MAX exactly
const CHUNK: u32 = 256u;

fn box_area(lx: f32, ly: f32, lz: f32, hx: f32, hy: f32, hz: f32) -> f32 {
    let dx: f32 = hx - lx;
    let dy: f32 = hy - ly;
    let dz: f32 = hz - lz;
    return 2.0 * (dy * (dx + dz) + dz * dx);   // the CPU's factored form (rt_bvh.cpp)
}
fn spread(v0: u32) -> u32 {
    var v: u32 = v0;
    v = (v | (v << 16u)) & 0x030000FFu;
    v = (v | (v << 8u)) & 0x0300F00Fu;
    v = (v | (v << 4u)) & 0x030C30C3u;
    v = (v | (v << 2u)) & 0x09249249u;
    return v;
}

//@pass rt_bounds_chunk
// One invocation per chunk of 256 triangles: the centroid box of the chunk into G
// (6 floats a chunk, from G[8]).
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read> T: array<f32>;
@group(0) @binding(2) var<storage, read_write> G: array<f32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let c: u32 = gid.x;
    if (c * CHUNK >= P.n) { return; }
    var lo: vec3f = vec3f(BIG);
    var hi: vec3f = vec3f(-BIG);
    let end: u32 = min(P.n, (c + 1u) * CHUNK);
    for (var i: u32 = c * CHUNK; i < end; i++) {
        let b: u32 = i * 9u;
        let third: f32 = 0.333333343;
        let x: f32 = (T[b] + T[b + 3u] + T[b + 6u]) * third;
        let y: f32 = (T[b + 1u] + T[b + 4u] + T[b + 7u]) * third;
        let z: f32 = (T[b + 2u] + T[b + 5u] + T[b + 8u]) * third;
        lo = min(lo, vec3f(x, y, z));
        hi = max(hi, vec3f(x, y, z));
    }
    let o: u32 = 8u + c * 6u;
    G[o] = lo.x; G[o + 1u] = lo.y; G[o + 2u] = lo.z; G[o + 3u] = hi.x; G[o + 4u] = hi.y; G[o + 5u] = hi.z;
}

//@pass rt_bounds_final
// One invocation: the chunks' boxes into G[0..2] (lo) and the power-of-two scale G[3].
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read_write> G: array<f32>;

@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x != 0u) { return; }
    var lo: vec3f = vec3f(BIG);
    var hi: vec3f = vec3f(-BIG);
    let chunks: u32 = (P.n + CHUNK - 1u) / CHUNK;
    for (var c: u32 = 0u; c < chunks; c++) {
        let o: u32 = 8u + c * 6u;
        lo = min(lo, vec3f(G[o], G[o + 1u], G[o + 2u]));
        hi = max(hi, vec3f(G[o + 3u], G[o + 4u], G[o + 5u]));
    }
    let extent: f32 = max(hi.x - lo.x, max(hi.y - lo.y, hi.z - lo.z));
    var s: f32 = 1.0;
    if (extent > 0.0) {
        for (var a: u32 = 0u; a < 300u; a++) {
            if (extent * s > 1.0) { s = s * 0.5; } else { break; }
        }
        for (var b: u32 = 0u; b < 300u; b++) {
            if (extent * s <= 0.5) { s = s * 2.0; } else { break; }
        }
    }
    G[0] = lo.x; G[1] = lo.y; G[2] = lo.z; G[3] = s;
}

//@pass rt_morton
// One invocation per sort slot (P.count, a power of two): the key (code, triangle) of each
// triangle; slots past n hold the largest key.
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read> T: array<f32>;
@group(0) @binding(2) var<storage, read> G: array<f32>;
@group(0) @binding(3) var<storage, read_write> K: array<u32>;
@group(0) @binding(4) var<storage, read_write> V: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= P.count) { return; }
    if (i >= P.n) {
        K[i] = 0xffffffffu;
        V[i] = 0xffffffffu;
        return;
    }
    let b: u32 = i * 9u;
    let third: f32 = 0.333333343;
    let s: f32 = G[3];
    let x: f32 = (T[b] + T[b + 3u] + T[b + 6u]) * third;
    let y: f32 = (T[b + 1u] + T[b + 4u] + T[b + 7u]) * third;
    let z: f32 = (T[b + 2u] + T[b + 5u] + T[b + 8u]) * third;
    let qx: u32 = min(1023u, u32((x - G[0]) * s * 1023.0));
    let qy: u32 = min(1023u, u32((y - G[1]) * s * 1023.0));
    let qz: u32 = min(1023u, u32((z - G[2]) * s * 1023.0));
    K[i] = (spread(qx) << 2u) | (spread(qy) << 1u) | spread(qz);
    V[i] = i;
}

//@pass rt_sort_step
// One invocation per slot: one compare-exchange step (P.k, P.j) of a bitonic sort by
// (code, triangle), ascending.
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read_write> K: array<u32>;
@group(0) @binding(2) var<storage, read_write> V: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    let l: u32 = i ^ P.j;
    if (i >= P.count || l <= i) { return; }
    let ka: u32 = K[i];
    let kb: u32 = K[l];
    let va: u32 = V[i];
    let vb: u32 = V[l];
    let greater: bool = ka > kb || (ka == kb && va > vb);
    let up: bool = (i & P.k) == 0u;
    if (greater == up) {
        K[i] = kb; K[l] = ka; V[i] = vb; V[l] = va;
    }
}

//@pass rt_leaves
// One invocation per sorted position: leaf node p holds triangle V[p]; cluster p is node p.
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read> T: array<f32>;
@group(0) @binding(2) var<storage, read> V: array<u32>;
@group(0) @binding(3) var<storage, read_write> NI: array<i32>;
@group(0) @binding(4) var<storage, read_write> NB: array<f32>;
@group(0) @binding(5) var<storage, read_write> C: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x;
    if (p >= P.n) { return; }
    let t: u32 = V[p];
    let b: u32 = t * 9u;
    NI[p * 4u] = -1; NI[p * 4u + 1u] = -1; NI[p * 4u + 2u] = i32(t); NI[p * 4u + 3u] = 0;
    for (var a: u32 = 0u; a < 3u; a++) {
        NB[p * 8u + a] = min(T[b + a], min(T[b + 3u + a], T[b + 6u + a]));
        NB[p * 8u + 3u + a] = max(T[b + a], max(T[b + 3u + a], T[b + 6u + a]));
    }
    C[p] = p;
}

//@pass rt_ploc_nn
// One invocation per cluster (P.m): its nearest neighbour within P.radius positions by the
// surface area of the merged box, ties to the lower position.
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read> NB: array<f32>;
@group(0) @binding(2) var<storage, read> C: array<u32>;
@group(0) @binding(3) var<storage, read_write> NN: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= P.m) { return; }
    let a: u32 = C[i] * 8u;
    let first: u32 = select(0u, i - P.radius, i > P.radius);
    let last: u32 = min(P.m - 1u, i + P.radius);
    var best: f32 = BIG;
    var nn: u32 = i;
    for (var j: u32 = first; j <= last; j++) {
        if (j == i) { continue; }
        let b: u32 = C[j] * 8u;
        let d: f32 = box_area(min(NB[a], NB[b]), min(NB[a + 1u], NB[b + 1u]), min(NB[a + 2u], NB[b + 2u]), max(NB[a + 3u], NB[b + 3u]), max(NB[a + 4u], NB[b + 4u]), max(NB[a + 5u], NB[b + 5u]));
        if (d < best) {
            best = d;
            nn = j;
        }
    }
    NN[i] = nn;
}

//@pass rt_ploc_flags
// One invocation per cluster: S holds 1 where the cluster starts a merge (the lower of a
// mutual pair), SV holds 1 where it survives (not the upper of a mutual pair).
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read> NN: array<u32>;
@group(0) @binding(2) var<storage, read_write> S: array<u32>;
@group(0) @binding(3) var<storage, read_write> SV: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= P.m) { return; }
    let j: u32 = NN[i];
    let mutual: bool = NN[j] == i;
    S[i] = select(0u, 1u, mutual && i < j);
    SV[i] = select(1u, 0u, mutual && i > j);
}

//@pass rt_scan_chunk
// One invocation per chunk of 256 entries of A (P.count entries): the exclusive prefix sum
// within the chunk, in place, and the chunk's total in B.
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read_write> A: array<u32>;
@group(0) @binding(2) var<storage, read_write> B: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let c: u32 = gid.x;
    if (c * CHUNK >= P.count) { return; }
    var sum: u32 = 0u;
    let end: u32 = min(P.count, (c + 1u) * CHUNK);
    for (var i: u32 = c * CHUNK; i < end; i++) {
        let v: u32 = A[i];
        A[i] = sum;
        sum = sum + v;
    }
    B[c] = sum;
}

//@pass rt_scan_top
// One invocation: the exclusive prefix sum of the chunk totals, in place; B[chunks] is the total.
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read_write> B: array<u32>;

@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x != 0u) { return; }
    let chunks: u32 = (P.count + CHUNK - 1u) / CHUNK;
    var sum: u32 = 0u;
    for (var c: u32 = 0u; c < chunks; c++) {
        let v: u32 = B[c];
        B[c] = sum;
        sum = sum + v;
    }
    B[chunks] = sum;
}

//@pass rt_scan_add
// One invocation per entry: add its chunk's offset.
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read_write> A: array<u32>;
@group(0) @binding(2) var<storage, read> B: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= P.count) { return; }
    A[i] = A[i] + B[i / CHUNK];
}

//@pass rt_ploc_merge
// One invocation per cluster: the lower of a mutual pair makes node P.nextId + S[i] from the
// pair; every survivor lands at SV[i] in D. The last invocation writes the survivor and merge
// counts into W.
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read> NN: array<u32>;
@group(0) @binding(2) var<storage, read> C: array<u32>;
@group(0) @binding(3) var<storage, read> S: array<u32>;
@group(0) @binding(4) var<storage, read> SV: array<u32>;
@group(0) @binding(5) var<storage, read_write> NI: array<i32>;
@group(0) @binding(6) var<storage, read_write> NB: array<f32>;
@group(0) @binding(7) var<storage, read_write> D: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= P.m) { return; }
    let j: u32 = NN[i];
    let mutual: bool = NN[j] == i;
    let merges: bool = mutual && i < j;
    let survives: bool = !(mutual && i > j);
    if (i == P.m - 1u) {
        D[P.m] = SV[i] + select(0u, 1u, survives);
        D[P.m + 1u] = S[i] + select(0u, 1u, merges);
    }
    if (!survives) { return; }
    if (!merges) {
        D[SV[i]] = C[i];
        return;
    }
    let id: u32 = P.nextId + S[i];
    let a: u32 = C[i];
    let b: u32 = C[j];
    NI[id * 4u] = i32(a); NI[id * 4u + 1u] = i32(b); NI[id * 4u + 2u] = -1; NI[id * 4u + 3u] = 0;
    for (var k: u32 = 0u; k < 3u; k++) {
        NB[id * 8u + k] = min(NB[a * 8u + k], NB[b * 8u + k]);
        NB[id * 8u + 3u + k] = max(NB[a * 8u + 3u + k], NB[b * 8u + 3u + k]);
    }
    D[SV[i]] = id;
}

//@pass rt_trace
// One invocation per ray (R: origin, direction, tMax, pad): closest hit (P.mode 0) or any hit
// (1) through the tree rooted at P.nextId. O: t (or -1), the triangle (-1 none, -2 stack
// overflow) as a float-exact integer, and the steps taken. The slab exit is widened by
// P.farScale (1 + 2 gamma(3), Ize 2013; the control shortens it).
@group(0) @binding(0) var<uniform> P: RtParams;
@group(0) @binding(1) var<storage, read> NI: array<i32>;
@group(0) @binding(2) var<storage, read> NB: array<f32>;
@group(0) @binding(3) var<storage, read> T: array<f32>;
@group(0) @binding(4) var<storage, read> R: array<f32>;
@group(0) @binding(5) var<storage, read_write> O: array<f32>;

fn inv_dir(x: f32) -> f32 {
    if (x != 0.0) { return 1.0 / x; }
    return select(-BIG, BIG, x >= 0.0);
}

// Watertight ray-triangle intersection (Woop, Benthin and Wald, JCGT 2013) by the operations of
// raw/math/primitives.hpp: the ray's frame (largest |d| as z, x and y swapped when it is negative),
// then the signs of the three sheared 2D edge functions. x t, y u, z v, w 1 on a hit (t > 1e-4).
fn wt_frame(v: vec3f, kz: u32, sw: bool) -> vec3f {
    var p: vec3f = v;
    if (kz == 0u) { p = vec3f(v.y, v.z, v.x); }
    if (kz == 1u) { p = vec3f(v.z, v.x, v.y); }
    if (sw) { p = vec3f(p.y, p.x, p.z); }
    return p;
}
fn wt_hit(o: vec3f, d: vec3f, a: vec3f, b: vec3f, c: vec3f) -> vec4f {
    let ad: vec3f = abs(d);
    var kz: u32 = 2u;
    if (ad.x > ad.y && ad.x > ad.z) { kz = 0u; }
    if (ad.x <= ad.y && ad.y > ad.z) { kz = 1u; }
    let dk: vec3f = wt_frame(d, kz, false);
    let sw: bool = dk.z < 0.0;
    let dr: vec3f = wt_frame(d, kz, sw);
    let sx: f32 = dr.x / dr.z;
    let sy: f32 = dr.y / dr.z;
    let sz: f32 = 1.0 / dr.z;
    let pa: vec3f = wt_frame(a - o, kz, sw);
    let pb: vec3f = wt_frame(b - o, kz, sw);
    let pc: vec3f = wt_frame(c - o, kz, sw);
    let ax: f32 = pa.x - sx * pa.z;
    let ay: f32 = pa.y - sy * pa.z;
    let bx: f32 = pb.x - sx * pb.z;
    let by: f32 = pb.y - sy * pb.z;
    let cx: f32 = pc.x - sx * pc.z;
    let cy: f32 = pc.y - sy * pc.z;
    let eu: f32 = cx * by - cy * bx;
    let ev: f32 = ax * cy - ay * cx;
    let ew: f32 = bx * ay - by * ax;
    if ((eu < 0.0 || ev < 0.0 || ew < 0.0) && (eu > 0.0 || ev > 0.0 || ew > 0.0)) { return vec4f(0.0); }
    let det: f32 = eu + ev + ew;
    if (det == 0.0) { return vec4f(0.0); }
    // The signs decided the hit. t, u and v come from Moeller-Trumbore's operations, whose t carries
    // less rounding than the sheared sum; the sheared values only where its determinant is unusable.
    let e1: vec3f = b - a;
    let e2: vec3f = c - a;
    let p: vec3f = vec3f(d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x);
    let dm: f32 = e1.x * p.x + e1.y * p.y + e1.z * p.z;
    if (dm > -1e-7 && dm < 1e-7) {
        let tz: f32 = eu * (sz * pa.z) + ev * (sz * pb.z) + ew * (sz * pc.z);
        let inv0: f32 = 1.0 / det;
        let t0: f32 = tz * inv0;
        return vec4f(t0, ev * inv0, ew * inv0, select(0.0, 1.0, t0 > 1e-4));
    }
    let inv: f32 = 1.0 / dm;
    let tv: vec3f = o - a;
    var u: f32 = max(0.0, (tv.x * p.x + tv.y * p.y + tv.z * p.z) * inv);
    let q: vec3f = vec3f(tv.y * e1.z - tv.z * e1.y, tv.z * e1.x - tv.x * e1.z, tv.x * e1.y - tv.y * e1.x);
    var v: f32 = max(0.0, (d.x * q.x + d.y * q.y + d.z * q.z) * inv);
    let tt: f32 = (e2.x * q.x + e2.y * q.y + e2.z * q.z) * inv;
    let s: f32 = u + v;
    if (s > 1.0) {
        u = u / s;
        v = v / s;
    }
    return vec4f(tt, u, v, select(0.0, 1.0, tt > 1e-4));
}
// x: t, y: 1 when hit.
fn hit_tri(t: u32, ox: f32, oy: f32, oz: f32, dx: f32, dy: f32, dz: f32) -> vec2f {
    let b: u32 = t * 9u;
    let h: vec4f = wt_hit(vec3f(ox, oy, oz), vec3f(dx, dy, dz), vec3f(T[b], T[b + 1u], T[b + 2u]), vec3f(T[b + 3u], T[b + 4u], T[b + 5u]), vec3f(T[b + 6u], T[b + 7u], T[b + 8u]));
    return vec2f(h.x, h.w);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let r: u32 = gid.x;
    if (r >= P.count) { return; }
    let ox: f32 = R[r * 8u];
    let oy: f32 = R[r * 8u + 1u];
    let oz: f32 = R[r * 8u + 2u];
    let dx: f32 = R[r * 8u + 3u];
    let dy: f32 = R[r * 8u + 4u];
    let dz: f32 = R[r * 8u + 5u];
    let ix: f32 = inv_dir(dx);
    let iy: f32 = inv_dir(dy);
    let iz: f32 = inv_dir(dz);
    var best: f32 = R[r * 8u + 6u];
    var tri: i32 = -1;
    var steps: u32 = 0u;
    var stack: array<u32, 64>;
    var sp: u32 = 1u;
    stack[0] = P.nextId;
    for (var guard: u32 = 0u; guard < 1000000u; guard++) {
        if (sp == 0u) { break; }
        sp = sp - 1u;
        let n: u32 = stack[sp];
        steps = steps + 1u;
        let a0: f32 = (NB[n * 8u] - ox) * ix;
        let b0: f32 = (NB[n * 8u + 3u] - ox) * ix;
        let a1: f32 = (NB[n * 8u + 1u] - oy) * iy;
        let b1: f32 = (NB[n * 8u + 4u] - oy) * iy;
        let a2: f32 = (NB[n * 8u + 2u] - oz) * iz;
        let b2: f32 = (NB[n * 8u + 5u] - oz) * iz;
        let t0: f32 = max(max(max(0.0, min(a0, b0)), min(a1, b1)), min(a2, b2));
        let t1: f32 = min(min(min(best, max(a0, b0)), max(a1, b1)), max(a2, b2));
        if (!(t0 <= t1 * P.farScale)) { continue; }
        let leaf: i32 = NI[n * 4u + 2u];
        if (leaf >= 0) {
            let h: vec2f = hit_tri(u32(leaf), ox, oy, oz, dx, dy, dz);
            if (h.y > 0.0 && (h.x < best || (tri >= 0 && h.x == best && leaf < tri))) {
                best = h.x;
                tri = leaf;
                if (P.mode == 1u) { break; }
            }
            continue;
        }
        if (sp + 2u > 64u) {
            tri = -2;
            break;
        }
        stack[sp] = u32(NI[n * 4u + 1u]);
        stack[sp + 1u] = u32(NI[n * 4u]);
        sp = sp + 2u;
    }
    O[r * 4u] = select(-1.0, best, tri >= 0);
    O[r * 4u + 1u] = f32(tri);
    O[r * 4u + 2u] = f32(steps);
    O[r * 4u + 3u] = 0.0;
}
