// Split radiance cascades: allocation and the per-frame passes (gi_common.wgsl goes in front).

//@pass gi_clear
// One invocation per word: zero.
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read_write> X: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = flat(gid);
    if (i >= P.count) { return; }
    X[i] = 0u;
}

//@pass gi_mark
// One invocation per pixel: mark the receiver's key in every cascade's table.
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read> GB: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> SLOT: array<u32>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let i: u32 = gid.y * P.w + gid.x;
    let pm: vec4f = GB[2u * i];
    if (pm.w == 0.0) { return; }
    let b: u32 = bucketOf(GB[2u * i + 1u].xyz);
    for (var c: u32 = 0u; c < 4u; c++) {
        let d: vec4u = dimOf(c, P.d0, P.d1, P.d2, P.d3);
        SLOT[d.w + keyOf(pm.xyz, b, d, pick(P.cell, c), P.origin.xyz)] = 1u;
    }
}

//@pass gi_scan_add
// One invocation per entry, folded like gi_clear: add its chunk's offset (rt.wgsl's rt_scan_add,
// whose one-dimensional dispatch cannot cover the dense key tables).
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read_write> A: array<u32>;
@group(0) @binding(2) var<storage, read> B: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = flat(gid);
    if (i >= P.count) { return; }
    A[i] = A[i] + B[i / 256u];
}

//@pass gi_keys
// One invocation per key of SLOT, after the scan (MARK is the mark before it): each occupied key's
// global slot learns its key; unoccupied keys become NONE.
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read> MARK: array<u32>;
@group(0) @binding(2) var<storage, read_write> SLOT: array<u32>;
@group(0) @binding(3) var<storage, read_write> KEYS: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let k: u32 = flat(gid);
    if (k >= P.count) { return; }
    if (MARK[k] == 0u) { SLOT[k] = NONE; return; }
    KEYS[SLOT[k]] = k;
}

//@pass gi_trace
// One invocation per receiver ray: its first hit, and there one sun ray and one bounce ray whose
// estimate goes into the cache's frame sums (src/renderer/gi_trace.cpp traceFrame).
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read> GB: array<vec4f>;
@group(0) @binding(2) var<storage, read> NI: array<i32>;
@group(0) @binding(3) var<storage, read> NB: array<f32>;
@group(0) @binding(4) var<storage, read> TA: array<f32>;
@group(0) @binding(5) var<storage, read> CE: array<f32>;
@group(0) @binding(6) var<storage, read_write> CS: array<atomic<u32>>;
@group(0) @binding(7) var<storage, read_write> RR: array<u32>;

fn vtx(t: u32, k: u32) -> vec3f {
    return vec3f(TA[t * 21u + k * 3u], TA[t * 21u + k * 3u + 1u], TA[t * 21u + k * 3u + 2u]);
}
fn nrm(t: u32, k: u32) -> vec3f {
    return vec3f(TA[t * 21u + 9u + k * 3u], TA[t * 21u + 10u + k * 3u], TA[t * 21u + 11u + k * 3u]);
}
fn albedoOf(t: u32) -> vec3f {
    return vec3f(TA[t * 21u + 18u], TA[t * 21u + 19u], TA[t * 21u + 20u]);
}
// Moeller-Trumbore in the order of raw/math/primitives.hpp: x t, y u, z v, w 1 on a hit.
fn hitTri(t: u32, o: vec3f, d: vec3f) -> vec4f {
    let a: vec3f = vtx(t, 0u);
    let e1: vec3f = vtx(t, 1u) - a;
    let e2: vec3f = vtx(t, 2u) - a;
    let p: vec3f = vcross(d, e2);
    let det: f32 = vdot(e1, p);
    if (det > -1e-7 && det < 1e-7) { return vec4f(0.0); }
    let inv: f32 = 1.0 / det;
    let tv: vec3f = o - a;
    let u: f32 = vdot(tv, p) * inv;
    if (u < 0.0 || u > 1.0) { return vec4f(0.0); }
    let q: vec3f = vcross(tv, e1);
    let v: f32 = vdot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0) { return vec4f(0.0); }
    let tt: f32 = vdot(e2, q) * inv;
    return vec4f(tt, u, v, select(0.0, 1.0, tt > 1e-4));
}
fn invC(x: f32) -> f32 {
    if (x != 0.0) { return 1.0 / x; }
    return select(-BIG, BIG, x >= 0.0);
}
// The nearest hit as rt::closest finds it: x t, y u, z v, w triangle (-1 none).
fn closest(o: vec3f, d: vec3f) -> vec4f {
    let iv: vec3f = vec3f(invC(d.x), invC(d.y), invC(d.z));
    var best: vec4f = vec4f(1e30, 0.0, 0.0, -1.0);
    var stack: array<u32, 64>;
    var sp: u32 = 1u;
    stack[0] = P.root;
    for (var guard: u32 = 0u; guard < 1000000u; guard++) {
        if (sp == 0u) { break; }
        sp = sp - 1u;
        let n: u32 = stack[sp];
        let lo: vec3f = (vec3f(NB[n * 8u], NB[n * 8u + 1u], NB[n * 8u + 2u]) - o) * iv;
        let hi: vec3f = (vec3f(NB[n * 8u + 3u], NB[n * 8u + 4u], NB[n * 8u + 5u]) - o) * iv;
        let mn: vec3f = min(lo, hi);
        let mx: vec3f = max(lo, hi);
        let t0: f32 = max(max(max(0.0, mn.x), mn.y), mn.z);
        let t1: f32 = min(min(min(best.x, mx.x), mx.y), mx.z);
        if (!(t0 <= t1 * SLAB)) { continue; }
        let leaf: i32 = NI[n * 4u + 2u];
        if (leaf >= 0) {
            let h: vec4f = hitTri(u32(leaf), o, d);
            if (h.w > 0.0 && (h.x < best.x || (best.w >= 0.0 && h.x == best.x && f32(leaf) < best.w))) {
                best = vec4f(h.x, h.y, h.z, f32(leaf));
            }
            continue;
        }
        if (sp + 2u > 64u) { break; }
        stack[sp] = u32(NI[n * 4u + 1u]);
        stack[sp + 1u] = u32(NI[n * 4u]);
        sp = sp + 2u;
    }
    return best;
}
// The hit's interpolated normal, turned to face the origin of the ray.
fn hitNormal(h: vec4f, d: vec3f) -> vec3f {
    let t: u32 = u32(h.w);
    let w: f32 = 1.0 - h.y - h.z;
    var n: vec3f = vnorm(nrm(t, 0u) * w + nrm(t, 1u) * h.y + nrm(t, 2u) * h.z);
    if (vdot(n, d) > 0.0) { n = n * -1.0; }
    return n;
}
fn cacheKey(p: vec3f, n: vec3f) -> u32 {
    return keyOf(p, bucketOf(n), P.cdim, P.origin.w, P.origin.xyz);
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.rw || gid.y >= P.rh) { return; }
    let r: u32 = gid.y * P.rw + gid.x;
    let px: u32 = gid.x * P.stride;
    let py: u32 = gid.y * P.stride;
    let i: u32 = py * P.w + px;
    RR[r * 8u] = bitcast<u32>(1e30);
    RR[r * 8u + 7u] = NONE;
    let pm: vec4f = GB[2u * i];
    if (pm.w == 0.0) { return; }
    let n: vec3f = GB[2u * i + 1u].xyz;
    let dir: vec3f = cosineDir(n, hash01(px, py, seedOf(P.frame, 0u, 0u)), hash01(px, py, seedOf(P.frame, 0u, 1u)));
    RR[r * 8u + 1u] = bitcast<u32>(dir.x);
    RR[r * 8u + 2u] = bitcast<u32>(dir.y);
    RR[r * 8u + 3u] = bitcast<u32>(dir.z);
    let o0: vec3f = pm.xyz + n * 1e-3;
    let h1: vec4f = closest(o0, dir);
    if (h1.w < 0.0) { return; }
    let p1: vec3f = o0 + dir * h1.x;
    let n1: vec3f = hitNormal(h1, dir);
    let a1: vec3f = albedoOf(u32(h1.w));
    let k1: u32 = cacheKey(p1, n1);
    RR[r * 8u] = bitcast<u32>(h1.x);
    RR[r * 8u + 4u] = bitcast<u32>(a1.x);
    RR[r * 8u + 5u] = bitcast<u32>(a1.y);
    RR[r * 8u + 6u] = bitcast<u32>(a1.z);
    RR[r * 8u + 7u] = k1;
    let o1: vec3f = p1 + n1 * 1e-3;
    var sunTerm: f32 = 0.0;
    let ndl: f32 = vdot(n1, P.light.xyz);
    if (ndl > 0.0) {
        let hs: vec4f = closest(o1, P.light.xyz);
        if (hs.w < 0.0) { sunTerm = P.light.w * ndl; }
    }
    let d2: vec3f = cosineDir(n1, hash01(px, py, seedOf(P.frame, 1u, 0u)), hash01(px, py, seedOf(P.frame, 1u, 1u)));
    let h2: vec4f = closest(o1, d2);
    var lin: vec3f = vec3f(P.misc.x);
    if (h2.w >= 0.0) {
        let n2: vec3f = hitNormal(h2, d2);
        let k2: u32 = cacheKey(o1 + d2 * h2.x, n2);
        let a2: vec3f = albedoOf(u32(h2.w));
        lin = vec3f(a2.x * CE[4u * k2], a2.y * CE[4u * k2 + 1u], a2.z * CE[4u * k2 + 2u]);
    }
    atomicAdd(&CS[4u * k1], quantise(lin.x + sunTerm));
    atomicAdd(&CS[4u * k1 + 1u], quantise(lin.y + sunTerm));
    atomicAdd(&CS[4u * k1 + 2u], quantise(lin.z + sunTerm));
    atomicAdd(&CS[4u * k1 + 3u], 1u);
}

//@pass gi_resolve
// One invocation per cache key: blend the frame mean into E and clear the sums (resolveCache).
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read_write> CS: array<u32>;
@group(0) @binding(2) var<storage, read_write> CE: array<f32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let k: u32 = flat(gid);
    if (k >= P.count) { return; }
    let n: u32 = CS[4u * k + 3u];
    if (n == 0u) { return; }
    let seen: f32 = CE[4u * k + 3u];
    let w: f32 = max(1.0 / (seen + 1.0), 0.125);
    for (var c: u32 = 0u; c < 3u; c++) {
        let m: f32 = f32(CS[4u * k + c]) / FIXED / f32(n);
        CE[4u * k + c] = CE[4u * k + c] + w * (m - CE[4u * k + c]);
        CS[4u * k + c] = 0u;
    }
    CS[4u * k + 3u] = 0u;
    CE[4u * k + 3u] = seen + 1.0;
}

//@pass gi_split
// One invocation per receiver ray: its radiance (albedo x E of its first hit, 0 on escape) split by
// hit distance into the cascades (splitRays).
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read> GB: array<vec4f>;
@group(0) @binding(2) var<storage, read> RR: array<u32>;
@group(0) @binding(3) var<storage, read> CE: array<f32>;
@group(0) @binding(4) var<storage, read> SLOT: array<u32>;
@group(0) @binding(5) var<storage, read_write> ACC: array<atomic<u32>>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.rw || gid.y >= P.rh) { return; }
    let r: u32 = gid.y * P.rw + gid.x;
    let i: u32 = (gid.y * P.stride) * P.w + gid.x * P.stride;
    let pm: vec4f = GB[2u * i];
    if (pm.w == 0.0) { return; }
    let t: f32 = bitcast<f32>(RR[r * 8u]);
    let dir: vec3f = vec3f(bitcast<f32>(RR[r * 8u + 1u]), bitcast<f32>(RR[r * 8u + 2u]), bitcast<f32>(RR[r * 8u + 3u]));
    let k1: u32 = RR[r * 8u + 7u];
    var L: vec3u = vec3u(0u);
    if (k1 != NONE) {
        let a: vec3f = vec3f(bitcast<f32>(RR[r * 8u + 4u]), bitcast<f32>(RR[r * 8u + 5u]), bitcast<f32>(RR[r * 8u + 6u]));
        L = vec3u(quantise(a.x * CE[4u * k1]), quantise(a.y * CE[4u * k1 + 1u]), quantise(a.z * CE[4u * k1 + 2u]));
    }
    let b: u32 = bucketOf(GB[2u * i + 1u].xyz);
    for (var c: u32 = 0u; c < 4u; c++) {
        if (c > 0u && !(t >= pick(P.rad, c - 1u))) { break; }
        let d: vec4u = dimOf(c, P.d0, P.d1, P.d2, P.d3);
        let slot: u32 = SLOT[d.w + keyOf(pm.xyz, b, d, pick(P.cell, c), P.origin.xyz)] - pickU(P.sbase, c);
        let nb: u32 = pickU(P.nbin, c);
        let at: u32 = pickU(P.abase, c) + (slot * nb * nb + octBin(dir, nb)) * 5u;
        atomicAdd(&ACC[at], 1u);
        if (t < pick(P.rad, c)) {
            atomicAdd(&ACC[at + 1u], 1u);
            atomicAdd(&ACC[at + 2u], L.x);
            atomicAdd(&ACC[at + 3u], L.y);
            atomicAdd(&ACC[at + 4u], L.z);
        }
    }
}
