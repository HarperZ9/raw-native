// raw-native's own GPU triangle rasterizer (RT stage R1): the compute form of
// src/renderer/swr_setup.cpp and swr_raster.cpp, read with raw/renderer/swr.hpp. No
// fixed-function raster and no atomics: bins are counted, scanned and filled by
// invocations that each own one bin, so the lists come out in slot order and the result
// does not depend on scheduling. Edge functions are exact 64-bit integers carried as
// (lo, hi) pairs of u32, since WGSL has no 64-bit integers.

struct SwrParams {
    w: u32, h: u32, slots: u32, tile: u32,
    tilesX: u32, tilesY: u32, rule: u32, depthTest: u32,
    affine: u32, depthBits: u32, cullBack: u32, snapShift: u32,
    scale: f32, guard: f32, tris: u32, maxQ: f32,
    m0: vec4f, m1: vec4f, m2: vec4f, m3: vec4f,
    light: vec4f,
}
const SI_N: u32 = 12u;
const SF_N: u32 = 16u;
const SLOTS: u32 = 7u;

fn neg64(a: vec2u) -> vec2u {
    let lo: u32 = ~a.x + 1u;
    return vec2u(lo, ~a.y + select(0u, 1u, lo == 0u));
}
fn add64(a: vec2u, b: vec2u) -> vec2u {
    let lo: u32 = a.x + b.x;
    return vec2u(lo, a.y + b.y + select(0u, 1u, lo < a.x));
}
// The exact product of two i32 as a 64-bit (lo, hi) pair, two's complement.
fn mul64(a: i32, b: i32) -> vec2u {
    let ua: u32 = u32(abs(a));
    let ub: u32 = u32(abs(b));
    let a0: u32 = ua & 0xffffu;
    let a1: u32 = ua >> 16u;
    let b0: u32 = ub & 0xffffu;
    let b1: u32 = ub >> 16u;
    let p00: u32 = a0 * b0;
    let p01: u32 = a0 * b1;
    let mid: u32 = p01 + a1 * b0;
    let mc: u32 = select(0u, 0x10000u, mid < p01);
    let lo: u32 = p00 + (mid << 16u);
    let hi: u32 = a1 * b1 + (mid >> 16u) + mc + select(0u, 1u, lo < p00);
    var r: vec2u = vec2u(lo, hi);
    if ((a < 0) != (b < 0)) { r = neg64(r); }
    return r;
}
// E_ij(p) = (xj - xi)(py - yi) - (yj - yi)(px - xi)
fn edge64(xi: i32, yi: i32, xj: i32, yj: i32, px: i32, py: i32) -> vec2u {
    return add64(mul64(xj - xi, py - yi), neg64(mul64(yj - yi, px - xi)));
}
fn is_neg(e: vec2u) -> bool {
    return i32(e.y) < 0;
}
fn is_zero(e: vec2u) -> bool {
    return e.x == 0u && e.y == 0u;
}
// Every conversion exact, the two adds rounded: the CPU's edgeToFloat. e >= 0.
fn to_f(e: vec2u) -> f32 {
    return (f32(e.y) * 4294967296.0 + f32(e.x >> 16u) * 65536.0) + f32(e.x & 0xffffu);
}
fn top_left(dx: i32, dy: i32) -> bool {
    return (dy == 0 && dx > 0) || dy < 0;
}
fn covers(e: vec2u, tl: bool, rule: u32) -> bool {
    if (is_neg(e)) { return false; }
    if (!is_zero(e)) { return true; }
    if (rule == 0u) { return tl; }
    if (rule == 1u) { return !tl; }
    return rule == 2u;
}
// The 4 x 4 Bayer matrix, packed four bits an entry (0 8 2 10 / 12 4 14 6 / 3 11 1 9 / 15 7 13 5).
fn bayer4(x: u32, y: u32) -> u32 {
    let i: u32 = (y & 3u) * 4u + (x & 3u);
    let word: u32 = select(0x6e4ca280u, 0x5d7f91b3u, i >= 8u);
    return (word >> (4u * (i & 7u))) & 15u;
}
// The three edge functions of a slot at a sample: x, y, z are f(E_12), f(E_20), f(E_01) and
// w is 1 when the sample is covered under `rule`, -1 when it is not.
fn cover_edges(x0: i32, y0: i32, x1: i32, y1: i32, x2: i32, y2: i32, px: i32, py: i32, rule: u32) -> vec4f {
    let e12: vec2u = edge64(x1, y1, x2, y2, px, py);
    let e20: vec2u = edge64(x2, y2, x0, y0, px, py);
    let e01: vec2u = edge64(x0, y0, x1, y1, px, py);
    let a: bool = covers(e12, top_left(x2 - x1, y2 - y1), rule);
    let b: bool = covers(e20, top_left(x0 - x2, y0 - y2), rule);
    let c: bool = covers(e01, top_left(x1 - x0, y1 - y0), rule);
    if (!(a && b && c)) { return vec4f(0.0, 0.0, 0.0, -1.0); }
    return vec4f(to_f(e12), to_f(e20), to_f(e01), 1.0);
}
fn ceil_div256(v: i32) -> i32 {
    return -((-v) >> 8u);
}
fn plane_dist(x: f32, y: f32, z: f32, w: f32, plane: u32, g: f32) -> f32 {
    if (plane == 0u) { return z + w; }
    if (plane == 1u) { return w - z; }
    if (plane == 2u) { return g * w - x; }
    if (plane == 3u) { return g * w + x; }
    if (plane == 4u) { return g * w - y; }
    return g * w + y;
}

//@pass swr_setup
// One invocation per source triangle: transform, clip (near, far, guard band), snap to the
// fixed-point grid and fan into up to 7 slots, as raw::swr::setup does; unused slots are
// cleared. V: positions, three floats a vertex; X: indices, three a triangle.
@group(0) @binding(0) var<uniform> P: SwrParams;
@group(0) @binding(1) var<storage, read> V: array<f32>;
@group(0) @binding(2) var<storage, read> X: array<u32>;
@group(0) @binding(3) var<storage, read_write> SI: array<i32>;
@group(0) @binding(4) var<storage, read_write> SF: array<f32>;

fn clear_slot(slot: u32) {
    for (var k: u32 = 0u; k < SI_N; k++) { SI[slot * SI_N + k] = 0; }
    for (var j: u32 = 0u; j < SF_N; j++) { SF[slot * SF_N + j] = 0.0; }
    SI[slot * SI_N + 7u] = 1;
}

// One fan triangle (vertex columns a, b, c of the snapped arrays) into `slot`.
fn write_slot(slot: u32, src: u32, ax: i32, ay: i32, bx0: i32, by0: i32, cx0: i32, cy0: i32, za: f32, zb0: f32, zc0: f32, wa: f32, wb0: f32, wc0: f32, sa: f32, sb0: f32, sc0: f32, ta: f32, tb0: f32, tc0: f32) {
    var area: vec2u = edge64(ax, ay, bx0, by0, cx0, cy0);
    if (is_zero(area)) { return; }
    var bx: i32 = bx0;
    var by: i32 = by0;
    var cx: i32 = cx0;
    var cy: i32 = cy0;
    var zb: f32 = zb0;
    var zc: f32 = zc0;
    var wb: f32 = wb0;
    var wc: f32 = wc0;
    var sb: f32 = sb0;
    var sc: f32 = sc0;
    var tb: f32 = tb0;
    var tc: f32 = tc0;
    if (is_neg(area)) {
        if (P.cullBack == 1u) { return; }
        bx = cx0; by = cy0; cx = bx0; cy = by0;
        zb = zc0; zc = zb0; wb = wc0; wc = wb0; sb = sc0; sc = sb0; tb = tc0; tc = tb0;
        area = neg64(area);
    }
    let i: u32 = slot * SI_N;
    let f: u32 = slot * SF_N;
    SI[i] = ax; SI[i + 1u] = ay; SI[i + 2u] = bx; SI[i + 3u] = by; SI[i + 4u] = cx; SI[i + 5u] = cy;
    SI[i + 6u] = i32(src + 1u);
    SI[i + 7u] = max(0, ceil_div256(min(ax, min(bx, cx)) - 128));
    SI[i + 8u] = max(0, ceil_div256(min(ay, min(by, cy)) - 128));
    SI[i + 9u] = min(i32(P.w) - 1, (max(ax, max(bx, cx)) - 128) >> 8u);
    SI[i + 10u] = min(i32(P.h) - 1, (max(ay, max(by, cy)) - 128) >> 8u);
    SF[f] = za; SF[f + 1u] = zb; SF[f + 2u] = zc;
    SF[f + 3u] = wa; SF[f + 4u] = wb; SF[f + 5u] = wc;
    SF[f + 6u] = 1.0 / to_f(area);
    SF[f + 7u] = sa; SF[f + 8u] = ta; SF[f + 9u] = sb; SF[f + 10u] = tb; SF[f + 11u] = sc; SF[f + 12u] = tc;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let t: u32 = gid.x;
    if (t >= P.tris) { return; }
    for (var k: u32 = 0u; k < SLOTS; k++) { clear_slot(t * SLOTS + k); }
    var cx: array<f32, 9>;
    var cy: array<f32, 9>;
    var cz: array<f32, 9>;
    var cw: array<f32, 9>;
    var cs: array<f32, 9>;
    var ct: array<f32, 9>;
    var ox: array<f32, 9>;
    var oy: array<f32, 9>;
    var oz: array<f32, 9>;
    var ow: array<f32, 9>;
    var os: array<f32, 9>;
    var ot: array<f32, 9>;
    for (var v: u32 = 0u; v < 3u; v++) {
        let j: u32 = X[t * 3u + v] * 3u;
        let px: f32 = V[j];
        let py: f32 = V[j + 1u];
        let pz: f32 = V[j + 2u];
        cx[v] = P.m0.x * px + P.m0.y * py + P.m0.z * pz + P.m0.w;
        cy[v] = P.m1.x * px + P.m1.y * py + P.m1.z * pz + P.m1.w;
        cz[v] = P.m2.x * px + P.m2.y * py + P.m2.z * pz + P.m2.w;
        cw[v] = P.m3.x * px + P.m3.y * py + P.m3.z * pz + P.m3.w;
        cs[v] = select(0.0, 1.0, v == 1u);
        ct[v] = select(0.0, 1.0, v == 2u);
    }
    var n: u32 = 3u;
    for (var plane: u32 = 0u; plane < 6u; plane++) {
        if (n < 3u) { break; }
        var m: u32 = 0u;
        for (var i: u32 = 0u; i < n; i++) {
            let j: u32 = (i + 1u) % n;
            let da: f32 = plane_dist(cx[i], cy[i], cz[i], cw[i], plane, P.guard);
            let db: f32 = plane_dist(cx[j], cy[j], cz[j], cw[j], plane, P.guard);
            if (da >= 0.0) {
                ox[m] = cx[i]; oy[m] = cy[i]; oz[m] = cz[i]; ow[m] = cw[i]; os[m] = cs[i]; ot[m] = ct[i];
                m = m + 1u;
            }
            if ((da >= 0.0) != (db >= 0.0)) {
                let q: f32 = da / (da - db);
                ox[m] = cx[i] + (cx[j] - cx[i]) * q;
                oy[m] = cy[i] + (cy[j] - cy[i]) * q;
                oz[m] = cz[i] + (cz[j] - cz[i]) * q;
                ow[m] = cw[i] + (cw[j] - cw[i]) * q;
                os[m] = cs[i] + (cs[j] - cs[i]) * q;
                ot[m] = ct[i] + (ct[j] - ct[i]) * q;
                m = m + 1u;
            }
        }
        n = m;
        for (var c: u32 = 0u; c < n; c++) {
            cx[c] = ox[c]; cy[c] = oy[c]; cz[c] = oz[c]; cw[c] = ow[c]; cs[c] = os[c]; ct[c] = ot[c];
        }
    }
    if (n < 3u) { return; }
    var fx: array<i32, 9>;
    var fy: array<i32, 9>;
    let grid: i32 = i32(1u << P.snapShift);
    for (var s: u32 = 0u; s < n; s++) {
        let iw: f32 = 1.0 / cw[s];
        let nx: f32 = cx[s] * iw;
        let ny: f32 = cy[s] * iw;
        cz[s] = cz[s] * iw;
        cw[s] = iw;
        let sx: f32 = (nx * 0.5 + 0.5) * f32(P.w);
        let sy: f32 = (1.0 - (ny * 0.5 + 0.5)) * f32(P.h);
        fx[s] = i32(floor(sx * P.scale + 0.5)) * grid;
        fy[s] = i32(floor(sy * P.scale + 0.5)) * grid;
    }
    for (var k2: u32 = 1u; k2 + 1u < n; k2++) {
        let b: u32 = k2;
        let c2: u32 = k2 + 1u;
        write_slot(t * SLOTS + k2 - 1u, t, fx[0], fy[0], fx[b], fy[b], fx[c2], fy[c2], cz[0], cz[b], cz[c2], cw[0], cw[b], cw[c2], cs[0], cs[b], cs[c2], ct[0], ct[b], ct[c2]);
    }
}

//@pass swr_bin_count
// One invocation per bin (P.tile pixels square): the number of slots whose pixel box meets it.
// C holds the counts (bins), then the exclusive offsets (bins + 1).
@group(0) @binding(0) var<uniform> P: SwrParams;
@group(0) @binding(1) var<storage, read> SI: array<i32>;
@group(0) @binding(2) var<storage, read_write> C: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let bin: u32 = gid.x;
    if (bin >= P.tilesX * P.tilesY) { return; }
    let x0: i32 = i32((bin % P.tilesX) * P.tile);
    let y0: i32 = i32((bin / P.tilesX) * P.tile);
    let x1: i32 = x0 + i32(P.tile) - 1;
    let y1: i32 = y0 + i32(P.tile) - 1;
    var n: u32 = 0u;
    for (var s: u32 = 0u; s < P.slots; s++) {
        let i: u32 = s * SI_N;
        if (SI[i + 6u] == 0) { continue; }
        if (SI[i + 7u] <= x1 && SI[i + 9u] >= x0 && SI[i + 8u] <= y1 && SI[i + 10u] >= y0) { n = n + 1u; }
    }
    C[bin] = n;
}

//@pass swr_bin_scan
// One invocation: the exclusive prefix sum of the counts; C[2 bins] is the total.
@group(0) @binding(0) var<uniform> P: SwrParams;
@group(0) @binding(1) var<storage, read_write> C: array<u32>;

@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x != 0u) { return; }
    let bins: u32 = P.tilesX * P.tilesY;
    var sum: u32 = 0u;
    for (var b: u32 = 0u; b < bins; b++) {
        C[bins + b] = sum;
        sum = sum + C[b];
    }
    C[2u * bins] = sum;
}

//@pass swr_bin_fill
// One invocation per bin: its slots, in slot order, at its offset in Q.
@group(0) @binding(0) var<uniform> P: SwrParams;
@group(0) @binding(1) var<storage, read> SI: array<i32>;
@group(0) @binding(2) var<storage, read> C: array<u32>;
@group(0) @binding(3) var<storage, read_write> Q: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let bins: u32 = P.tilesX * P.tilesY;
    let bin: u32 = gid.x;
    if (bin >= bins) { return; }
    let x0: i32 = i32((bin % P.tilesX) * P.tile);
    let y0: i32 = i32((bin / P.tilesX) * P.tile);
    let x1: i32 = x0 + i32(P.tile) - 1;
    let y1: i32 = y0 + i32(P.tile) - 1;
    var at: u32 = C[bins + bin];
    for (var s: u32 = 0u; s < P.slots; s++) {
        let i: u32 = s * SI_N;
        if (SI[i + 6u] == 0) { continue; }
        if (SI[i + 7u] <= x1 && SI[i + 9u] >= x0 && SI[i + 8u] <= y1 && SI[i + 10u] >= y0) {
            Q[at] = s;
            at = at + 1u;
        }
    }
}

//@pass swr_tile
// One invocation per pixel: walk the pixel's bin list in slot order, test coverage exactly,
// keep the strictly nearest depth (or dithered integer depth). B: per pixel slot + 1 and the
// integer depth; D: the float depth.
@group(0) @binding(0) var<uniform> P: SwrParams;
@group(0) @binding(1) var<storage, read> SI: array<i32>;
@group(0) @binding(2) var<storage, read> SF: array<f32>;
@group(0) @binding(3) var<storage, read> C: array<u32>;
@group(0) @binding(4) var<storage, read> Q: array<u32>;
@group(0) @binding(5) var<storage, read_write> B: array<u32>;
@group(0) @binding(6) var<storage, read_write> D: array<f32>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let x: i32 = i32(gid.x);
    let y: i32 = i32(gid.y);
    let px: i32 = x * 256 + 128;
    let py: i32 = y * 256 + 128;
    let bins: u32 = P.tilesX * P.tilesY;
    let bin: u32 = (gid.y / P.tile) * P.tilesX + gid.x / P.tile;
    var best: u32 = 0u;
    var bestD: f32 = 3.0e38;
    var bestQ: u32 = 0xffffffffu;
    let first: u32 = C[bins + bin];
    let last: u32 = C[bins + bin + 1u];
    for (var k: u32 = first; k < last; k++) {
        let s: u32 = Q[k];
        let i: u32 = s * SI_N;
        if (x < SI[i + 7u] || x > SI[i + 9u] || y < SI[i + 8u] || y > SI[i + 10u]) { continue; }
        let e: vec4f = cover_edges(SI[i], SI[i + 1u], SI[i + 2u], SI[i + 3u], SI[i + 4u], SI[i + 5u], px, py, P.rule);
        if (e.w < 0.0) { continue; }
        let f: u32 = s * SF_N;
        let w1: f32 = e.y * SF[f + 6u];
        let w2: f32 = e.z * SF[f + 6u];
        let depth: f32 = SF[f] + w1 * (SF[f + 1u] - SF[f]) + w2 * (SF[f + 2u] - SF[f]);
        var q: u32 = 0u;
        if (P.depthBits > 0u) {
            let d01: f32 = clamp(depth * 0.5 + 0.5, 0.0, 1.0);
            q = u32(floor(d01 * P.maxQ + f32(bayer4(gid.x, gid.y)) * 0.0625));
        }
        if (P.depthTest == 1u) {
            if (P.depthBits > 0u) {
                if (!(q < bestQ)) { continue; }
            } else {
                if (!(depth < bestD)) { continue; }
            }
        }
        best = s + 1u;
        bestD = depth;
        bestQ = q;
    }
    let p: u32 = gid.y * P.w + gid.x;
    B[p * 2u] = best;
    B[p * 2u + 1u] = bestQ;
    D[p] = bestD;
}

//@pass swr_resolve
// One invocation per pixel: perspective-correct (or affine) weights of the visible slot, the
// source triangle's attributes (A: 16 floats a triangle, u v nx ny nz of each vertex, then the
// texture), a nearest wrapping texel (T: per texture offset width height, then texels) and the
// lit colour. OF: uv and texel coordinate (4 floats a pixel); OU: texel id and colour.
@group(0) @binding(0) var<uniform> P: SwrParams;
@group(0) @binding(1) var<storage, read> SI: array<i32>;
@group(0) @binding(2) var<storage, read> SF: array<f32>;
@group(0) @binding(3) var<storage, read> B: array<u32>;
@group(0) @binding(4) var<storage, read> A: array<f32>;
@group(0) @binding(5) var<storage, read> T: array<u32>;
@group(0) @binding(6) var<storage, read_write> OF: array<f32>;
@group(0) @binding(7) var<storage, read_write> OU: array<u32>;

fn attr(src: u32, k: u32, b0: f32, b1: f32, b2: f32) -> f32 {
    let a: u32 = src * 16u + k;
    return A[a] * b0 + A[a + 5u] * b1 + A[a + 10u] * b2;
}

fn channel(c: u32, k: u32, shade: f32) -> u32 {
    let ch: f32 = f32((c >> (8u * k)) & 255u) * shade;
    return min(255u, u32(floor(ch + 0.5))) << (8u * k);
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let p: u32 = gid.y * P.w + gid.x;
    let slot1: u32 = B[p * 2u];
    if (slot1 == 0u) {
        OF[p * 4u] = 0.0;
        OF[p * 4u + 1u] = 0.0;
        OF[p * 4u + 2u] = 0.0;
        OF[p * 4u + 3u] = 0.0;
        OU[p * 2u] = 0xffffffffu;
        OU[p * 2u + 1u] = 0u;
        return;
    }
    let s: u32 = slot1 - 1u;
    let i: u32 = s * SI_N;
    let f: u32 = s * SF_N;
    let e: vec4f = cover_edges(SI[i], SI[i + 1u], SI[i + 2u], SI[i + 3u], SI[i + 4u], SI[i + 5u], i32(gid.x) * 256 + 128, i32(gid.y) * 256 + 128, 2u);
    let w0: f32 = e.x * SF[f + 6u];
    let w1: f32 = e.y * SF[f + 6u];
    let w2: f32 = e.z * SF[f + 6u];
    var l0: f32 = 0.0;
    var l1: f32 = 0.0;
    var l2: f32 = 0.0;
    if (P.affine == 1u) {
        let sum: f32 = w0 + w1 + w2;
        l0 = w0 / sum;
        l1 = w1 / sum;
        l2 = w2 / sum;
    } else {
        let q0: f32 = w0 * SF[f + 3u];
        let q1: f32 = w1 * SF[f + 4u];
        let q2: f32 = w2 * SF[f + 5u];
        let den: f32 = q0 + q1 + q2;
        l0 = q0 / den;
        l1 = q1 / den;
        l2 = q2 / den;
    }
    let b1: f32 = l0 * SF[f + 7u] + l1 * SF[f + 9u] + l2 * SF[f + 11u];
    let b2: f32 = l0 * SF[f + 8u] + l1 * SF[f + 10u] + l2 * SF[f + 12u];
    let b0: f32 = (1.0 - b1) - b2;
    let src: u32 = u32(SI[i + 6u] - 1);
    let u: f32 = attr(src, 0u, b0, b1, b2);
    let v: f32 = attr(src, 1u, b0, b1, b2);
    let nx: f32 = attr(src, 2u, b0, b1, b2);
    let ny: f32 = attr(src, 3u, b0, b1, b2);
    let nz: f32 = attr(src, 4u, b0, b1, b2);
    let nl: f32 = 1.0 / sqrt(nx * nx + ny * ny + nz * nz);
    let shade: f32 = 0.35 + 0.65 * max(0.0, (nx * P.light.x + ny * P.light.y + nz * P.light.z) * nl);
    let tex: u32 = u32(A[src * 16u + 15u]);
    let tw: u32 = T[tex * 3u + 1u];
    let th: u32 = T[tex * 3u + 2u];
    let tu: f32 = u * f32(tw);
    let tv: f32 = v * f32(th);
    let tx: u32 = u32(i32(floor(tu))) & (tw - 1u);
    let ty: u32 = u32(i32(floor(tv))) & (th - 1u);
    let c: u32 = T[T[tex * 3u] + ty * tw + tx];
    OF[p * 4u] = u;
    OF[p * 4u + 1u] = v;
    OF[p * 4u + 2u] = tu;
    OF[p * 4u + 3u] = tv;
    OU[p * 2u] = tx | (ty << 12u) | (tex << 24u);
    OU[p * 2u + 1u] = 0xff000000u | channel(c, 0u, shade) | channel(c, 1u, shade) | channel(c, 2u, shade);
}

//@pass swr_gbuffer
// One invocation per pixel: world position, geometric and shading normal of the visible slot,
// by the perspective-correct source weights of swr_resolve (RT stage R2, hybrid rays).
// VP: positions, 9 floats a source triangle; A: swr_resolve's attributes; G: 12 floats a
// pixel (position, geometric normal, shading normal, 1 when covered, pad 2).
@group(0) @binding(0) var<uniform> P: SwrParams;
@group(0) @binding(1) var<storage, read> SI: array<i32>;
@group(0) @binding(2) var<storage, read> SF: array<f32>;
@group(0) @binding(3) var<storage, read> B: array<u32>;
@group(0) @binding(4) var<storage, read> VP: array<f32>;
@group(0) @binding(5) var<storage, read> A: array<f32>;
@group(0) @binding(6) var<storage, read_write> G: array<f32>;

fn vp(t: u32, k: u32) -> vec3f {
    return vec3f(VP[t * 9u + k * 3u], VP[t * 9u + k * 3u + 1u], VP[t * 9u + k * 3u + 2u]);
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let p: u32 = gid.y * P.w + gid.x;
    for (var z: u32 = 0u; z < 12u; z++) { G[p * 12u + z] = 0.0; }
    let slot1: u32 = B[p * 2u];
    if (slot1 == 0u) { return; }
    let s: u32 = slot1 - 1u;
    let i: u32 = s * SI_N;
    let f: u32 = s * SF_N;
    let e: vec4f = cover_edges(SI[i], SI[i + 1u], SI[i + 2u], SI[i + 3u], SI[i + 4u], SI[i + 5u], i32(gid.x) * 256 + 128, i32(gid.y) * 256 + 128, 2u);
    let q0: f32 = e.x * SF[f + 6u] * SF[f + 3u];
    let q1: f32 = e.y * SF[f + 6u] * SF[f + 4u];
    let q2: f32 = e.z * SF[f + 6u] * SF[f + 5u];
    let den: f32 = q0 + q1 + q2;
    let l0: f32 = q0 / den;
    let l1: f32 = q1 / den;
    let l2: f32 = q2 / den;
    let b1: f32 = l0 * SF[f + 7u] + l1 * SF[f + 9u] + l2 * SF[f + 11u];
    let b2: f32 = l0 * SF[f + 8u] + l1 * SF[f + 10u] + l2 * SF[f + 12u];
    let b0: f32 = (1.0 - b1) - b2;
    let t: u32 = u32(SI[i + 6u] - 1);
    let pos: vec3f = vp(t, 0u) * b0 + vp(t, 1u) * b1 + vp(t, 2u) * b2;
    let ng: vec3f = normalize(cross(vp(t, 1u) - vp(t, 0u), vp(t, 2u) - vp(t, 0u)));
    let a: u32 = t * 16u;
    let n: vec3f = normalize(vec3f(A[a + 2u] * b0 + A[a + 7u] * b1 + A[a + 12u] * b2, A[a + 3u] * b0 + A[a + 8u] * b1 + A[a + 13u] * b2, A[a + 4u] * b0 + A[a + 9u] * b1 + A[a + 14u] * b2));
    G[p * 12u] = pos.x; G[p * 12u + 1u] = pos.y; G[p * 12u + 2u] = pos.z;
    G[p * 12u + 3u] = ng.x; G[p * 12u + 4u] = ng.y; G[p * 12u + 5u] = ng.z;
    G[p * 12u + 6u] = n.x; G[p * 12u + 7u] = n.y; G[p * 12u + 8u] = n.z;
    G[p * 12u + 9u] = 1.0;
}
