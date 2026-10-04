//@pass setup
// Triangle setup (src/raster.cpp, the per-triangle half): clip space, perspective
// divide, screen mapping, bounds and signed area. One invocation per triangle.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> tris: array<f32>;
@group(0) @binding(2) var<storage, read_write> setupF: array<f32>;
@group(0) @binding(3) var<storage, read_write> setupI: array<i32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let t: u32 = gid.x;
    if (t >= P.ntri) { return; }
    let b: u32 = t * TRI_STRIDE;
    let o: u32 = t * SETUP_STRIDE;
    setupF[o] = 0.0;
    var sx: array<f32, 3>; var sy: array<f32, 3>; var sz: array<f32, 3>; var iw: array<f32, 3>;
    for (var k: u32 = 0u; k < 3u; k++) {
        let wp: vec4f = vec4f(tris[b + 3u * k], tris[b + 3u * k + 1u], tris[b + 3u * k + 2u], 1.0);
        let cs: vec4f = mmul(P.vp, wp);
        if (cs.w <= 1e-6) { return; }
        let inv: f32 = 1.0 / cs.w;
        let nx: f32 = cs.x * inv;
        let ny: f32 = cs.y * inv;
        sx[k] = (nx * 0.5 + 0.5) * f32(P.w);
        sy[k] = (1.0 - (ny * 0.5 + 0.5)) * f32(P.h);
        sz[k] = cs.w;
        iw[k] = inv;
    }
    let area: f32 = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
    if (abs(area) < 1e-9) { return; }
    for (var k: u32 = 0u; k < 3u; k++) {
        setupF[o + 1u + k] = sx[k]; setupF[o + 4u + k] = sy[k];
        setupF[o + 7u + k] = sz[k]; setupF[o + 10u + k] = iw[k];
    }
    setupF[o + 13u] = area;
    let oi: u32 = t * 4u;
    setupI[oi]      = max(0, i32(floor(min(min(sx[0], sx[1]), sx[2]))));
    setupI[oi + 1u] = min(i32(P.w) - 1, i32(ceil(max(max(sx[0], sx[1]), sx[2]))));
    setupI[oi + 2u] = max(0, i32(floor(min(min(sy[0], sy[1]), sy[2]))));
    setupI[oi + 3u] = min(i32(P.h) - 1, i32(ceil(max(max(sy[0], sy[1]), sy[2]))));
    setupF[o] = 1.0;
}

//@pass raster
// Rasterization (src/raster.cpp, the per-pixel half): every triangle in scene
// order, the same edge functions, the same strict depth test, perspective-correct
// position and normal. One invocation per pixel, so no atomics are needed and
// draw order is the CPU's.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> tris: array<f32>;
@group(0) @binding(2) var<storage, read> setupF: array<f32>;
@group(0) @binding(3) var<storage, read> setupI: array<i32>;
@group(0) @binding(4) var<storage, read_write> depth: array<f32>;
@group(0) @binding(5) var<storage, read_write> position: array<vec4f>;
@group(0) @binding(6) var<storage, read_write> normal: array<vec4f>;
@group(0) @binding(7) var<storage, read_write> albedoMask: array<vec4f>;   // xyz albedo, w mask

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let x: i32 = i32(gid.x); let y: i32 = i32(gid.y);
    var best: f32 = 0x1.fffffep+127f;  // largest f32; WGSL has no infinity. The host writes +inf for uncovered pixels.
    var pos: vec3f = vec3f(0.0); var nrm: vec3f = vec3f(0.0); var alb: vec3f = vec3f(0.0); var covered: f32 = 0.0;
    let px: f32 = f32(x) + 0.5; let py: f32 = f32(y) + 0.5;
    for (var t: u32 = 0u; t < P.ntri; t++) {
        let o: u32 = t * SETUP_STRIDE;
        if (setupF[o] == 0.0) { continue; }
        let oi: u32 = t * 4u;
        if (x < setupI[oi] || x > setupI[oi + 1u] || y < setupI[oi + 2u] || y > setupI[oi + 3u]) { continue; }
        let s0: vec2f = vec2f(setupF[o + 1u], setupF[o + 4u]);
        let s1: vec2f = vec2f(setupF[o + 2u], setupF[o + 5u]);
        let s2: vec2f = vec2f(setupF[o + 3u], setupF[o + 6u]);
        let area: f32 = setupF[o + 13u];
        let w0: f32 = ((s1.x - px) * (s2.y - py) - (s1.y - py) * (s2.x - px)) / area;
        let w1: f32 = ((s2.x - px) * (s0.y - py) - (s2.y - py) * (s0.x - px)) / area;
        let w2: f32 = 1.0 - w0 - w1;
        if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) { continue; }
        let i0: f32 = setupF[o + 10u]; let i1: f32 = setupF[o + 11u]; let i2: f32 = setupF[o + 12u];
        let iw: f32 = w0 * i0 + w1 * i1 + w2 * i2;
        let d: f32 = w0 * setupF[o + 7u] + w1 * setupF[o + 8u] + w2 * setupF[o + 9u];
        if (d >= best) { continue; }
        let b: u32 = t * TRI_STRIDE;
        var pc: array<f32, 6>;
        for (var c: u32 = 0u; c < 6u; c++) {
            // c < 3: position component c (tri floats 0..8); c >= 3: normal component c-3 (9..17)
            let base: u32 = select(b + 9u + (c - 3u), b + c, c < 3u);
            pc[c] = (w0 * tris[base] * i0 + w1 * tris[base + 3u] * i1 + w2 * tris[base + 6u] * i2) / iw;
        }
        best = d;
        pos = vec3f(pc[0], pc[1], pc[2]);
        nrm = vnorm(vec3f(pc[3], pc[4], pc[5]));
        alb = vec3f(tris[b + 18u], tris[b + 19u], tris[b + 20u]);
        covered = 1.0;
    }
    let i: u32 = pix(gid.x, gid.y);
    depth[i] = best;
    position[i] = vec4f(pos, 0.0);
    normal[i] = vec4f(nrm, 0.0);
    albedoMask[i] = vec4f(alb, covered);
}

//@pass motion
// Motion vectors by reprojection (src/motion.cpp). w of the output is 1 for a
// valid vector, 0 otherwise; the host counts them.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> position: array<vec4f>;
@group(0) @binding(2) var<storage, read> albedoMask: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> motion: array<vec4f>;

fn uvOf(m: array<vec4f, 4>, wp: vec3f, ok: ptr<function, bool>) -> vec2f {
    let cs: vec4f = mmul(m, vec4f(wp, 1.0));
    if (cs.w <= 1e-6) { *ok = false; return vec2f(0.0); }
    let inv: f32 = 1.0 / cs.w;
    return vec2f(cs.x * inv * 0.5 + 0.5, 1.0 - (cs.y * inv * 0.5 + 0.5));
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let i: u32 = pix(gid.x, gid.y);
    motion[i] = vec4f(0.0);
    if (albedoMask[i].w == 0.0) { return; }
    let wp: vec3f = position[i].xyz;
    var ok: bool = true;
    let cur: vec2f = uvOf(P.vp, wp, &ok);
    if (!ok) { return; }
    let prev: vec2f = uvOf(P.pvp, wp, &ok);
    if (!ok) { return; }
    let mv: vec2f = cur - prev;
    if (!(abs(mv.x) <= 0x1.fffffep+127f) || !(abs(mv.y) <= 0x1.fffffep+127f)) { return; }
    motion[i] = vec4f(mv, 0.0, 1.0);
}

//@pass ssao
// Screen-space AO (src/ssao.cpp).
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> position: array<vec4f>;
@group(0) @binding(2) var<storage, read> normal: array<vec4f>;
@group(0) @binding(3) var<storage, read> albedoMask: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> aoSS: array<f32>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let i: u32 = pix(gid.x, gid.y);
    if (albedoMask[i].w == 0.0) { aoSS[i] = 1.0; return; }
    let p: vec3f = position[i].xyz;
    let n: vec3f = normal[i].xyz;
    let R: f32 = P.misc.y;
    var occ: i32 = 0; var used: i32 = 0;
    for (var s: u32 = 0u; s < P.ss_samples; s++) {
        let a: f32 = hash01(gid.x, gid.y, s) * 6.2831853;
        let rad: f32 = (0.3 + 0.7 * hash01(gid.x, gid.y, s + 97u)) * R;
        let sx: i32 = i32(gid.x) + lround(cos(a) * rad);
        let sy: i32 = i32(gid.y) + lround(sin(a) * rad);
        if (sx < 0 || sy < 0 || sx >= i32(P.w) || sy >= i32(P.h)) { continue; }
        let j: u32 = pix(u32(sx), u32(sy));
        if (albedoMask[j].w == 0.0) { continue; }
        used++;
        let d: vec3f = position[j].xyz - p;
        let dist: f32 = vlen(d);
        if (dist < 1e-4 || dist > P.misc.x) { continue; }
        let ndl: f32 = vdot(n, vnorm(d));
        if (ndl > 0.15) { occ++; }
    }
    var v: f32 = 1.0;
    if (used > 0) { v = 1.0 - f32(occ) / f32(used); }
    aoSS[i] = select(v, 0.0, v < 0.0);
}

//@pass rtao
// Ray-traced AO reference (src/ray_ao.cpp with src/accel.cpp and the
// Moller-Trumbore test in raw/primitives.hpp).
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> tris: array<f32>;
@group(0) @binding(2) var<storage, read> position: array<vec4f>;
@group(0) @binding(3) var<storage, read> normal: array<vec4f>;
@group(0) @binding(4) var<storage, read> albedoMask: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> aoRT: array<f32>;

fn triVert(t: u32, k: u32) -> vec3f {
    let b: u32 = t * TRI_STRIDE + 3u * k;
    return vec3f(tris[b], tris[b + 1u], tris[b + 2u]);
}
fn hit(o: vec3f, d: vec3f, t: u32, maxDist: f32) -> bool {
    let a: vec3f = triVert(t, 0u);
    let e1: vec3f = triVert(t, 1u) - a;
    let e2: vec3f = triVert(t, 2u) - a;
    let p: vec3f = vcross(d, e2);
    let det: f32 = vdot(e1, p);
    if (det > -1e-7 && det < 1e-7) { return false; }
    let inv: f32 = 1.0 / det;
    let tv: vec3f = o - a;
    let u: f32 = vdot(tv, p) * inv;
    if (u < 0.0 || u > 1.0) { return false; }
    let q: vec3f = vcross(tv, e1);
    let v: f32 = vdot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0) { return false; }
    let dist: f32 = vdot(e2, q) * inv;
    return dist > 1e-4 && dist < maxDist;
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let i: u32 = pix(gid.x, gid.y);
    if (albedoMask[i].w == 0.0) { aoRT[i] = 1.0; return; }
    let p: vec3f = position[i].xyz;
    let n: vec3f = normal[i].xyz;
    var ax: vec3f = vec3f(1.0, 0.0, 0.0);
    if (abs(n.x) > 0.9) { ax = vec3f(0.0, 1.0, 0.0); }
    let tb: vec3f = vnorm(vcross(ax, n));
    let bb: vec3f = vcross(n, tb);
    let origin: vec3f = p + n * 1e-3;
    var open: u32 = 0u;
    for (var s: u32 = 0u; s < P.rt_samples; s++) {
        let u1: f32 = hash01(gid.x, gid.y, 2u * s);
        let u2: f32 = hash01(gid.x, gid.y, 2u * s + 1u);
        let r: f32 = sqrt(u1);
        let phi: f32 = 6.2831853 * u2;
        let lx: f32 = r * cos(phi);
        let ly: f32 = r * sin(phi);
        let lz: f32 = sqrt(max(0.0, 1.0 - u1));
        let dir: vec3f = vnorm(tb * lx + bb * ly + n * lz);
        var blocked: bool = false;
        for (var t: u32 = 0u; t < P.ntri; t++) {
            if (hit(origin, dir, t, P.misc.x)) { blocked = true; break; }
        }
        if (!blocked) { open++; }
    }
    aoRT[i] = f32(open) / f32(P.rt_samples);
}

//@pass shade
// Lighting (src/composite.cpp): the clamped frame and the unclamped HDR frame.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> normal: array<vec4f>;
@group(0) @binding(2) var<storage, read> albedoMask: array<vec4f>;
@group(0) @binding(3) var<storage, read> ao: array<f32>;
@group(0) @binding(4) var<storage, read_write> frame: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> hdr: array<vec4f>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let i: u32 = pix(gid.x, gid.y);
    let am: vec4f = albedoMask[i];
    if (am.w == 0.0) { frame[i] = vec4f(0.0); hdr[i] = vec4f(0.0); return; }
    let n: vec3f = normal[i].xyz;
    let ldir: vec3f = P.light.xyz;
    let ndl: f32 = max(0.0, vdot(n, ldir * -1.0)) * P.light.w;
    let lit: f32 = (0.2 + ndl) * ao[i];
    let rgb: vec3f = vec3f(am.x * lit, am.y * lit, am.z * lit);
    hdr[i] = vec4f(rgb, 0.0);
    frame[i] = vec4f(clamp(rgb, vec3f(0.0), vec3f(1.0)), 0.0);
}
