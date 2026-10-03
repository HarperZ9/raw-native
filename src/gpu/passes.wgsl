//@pass setup
// Triangle setup (src/raster.cpp, the per-triangle half): clip space, perspective
// divide, screen mapping, bounds and signed area. One invocation per triangle.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> tris: array<f32>;
@group(0) @binding(2) var<storage, read_write> setupF: array<f32>;
@group(0) @binding(3) var<storage, read_write> setupI: array<i32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let t = gid.x;
    if (t >= P.ntri) { return; }
    let b = t * TRI_STRIDE;
    let o = t * SETUP_STRIDE;
    setupF[o] = 0.0;
    var sx: array<f32, 3>; var sy: array<f32, 3>; var sz: array<f32, 3>; var iw: array<f32, 3>;
    for (var k = 0u; k < 3u; k++) {
        let wp = vec4f(tris[b + 3u * k], tris[b + 3u * k + 1u], tris[b + 3u * k + 2u], 1.0);
        let cs = mmul(P.vp, wp);
        if (cs.w <= 1e-6) { return; }
        let inv = 1.0 / cs.w;
        let nx = cs.x * inv;
        let ny = cs.y * inv;
        sx[k] = (nx * 0.5 + 0.5) * f32(P.w);
        sy[k] = (1.0 - (ny * 0.5 + 0.5)) * f32(P.h);
        sz[k] = cs.w;
        iw[k] = inv;
    }
    let area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
    if (abs(area) < 1e-9) { return; }
    for (var k = 0u; k < 3u; k++) {
        setupF[o + 1u + k] = sx[k]; setupF[o + 4u + k] = sy[k];
        setupF[o + 7u + k] = sz[k]; setupF[o + 10u + k] = iw[k];
    }
    setupF[o + 13u] = area;
    let oi = t * 4u;
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
    let x = i32(gid.x); let y = i32(gid.y);
    var best = 0x1.fffffep+127f;  // largest f32; WGSL has no infinity. The host writes +inf for uncovered pixels.
    var pos = vec3f(0.0); var nrm = vec3f(0.0); var alb = vec3f(0.0); var covered = 0.0;
    let px = f32(x) + 0.5; let py = f32(y) + 0.5;
    for (var t = 0u; t < P.ntri; t++) {
        let o = t * SETUP_STRIDE;
        if (setupF[o] == 0.0) { continue; }
        let oi = t * 4u;
        if (x < setupI[oi] || x > setupI[oi + 1u] || y < setupI[oi + 2u] || y > setupI[oi + 3u]) { continue; }
        let s0 = vec2f(setupF[o + 1u], setupF[o + 4u]);
        let s1 = vec2f(setupF[o + 2u], setupF[o + 5u]);
        let s2 = vec2f(setupF[o + 3u], setupF[o + 6u]);
        let area = setupF[o + 13u];
        let w0 = ((s1.x - px) * (s2.y - py) - (s1.y - py) * (s2.x - px)) / area;
        let w1 = ((s2.x - px) * (s0.y - py) - (s2.y - py) * (s0.x - px)) / area;
        let w2 = 1.0 - w0 - w1;
        if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) { continue; }
        let i0 = setupF[o + 10u]; let i1 = setupF[o + 11u]; let i2 = setupF[o + 12u];
        let iw = w0 * i0 + w1 * i1 + w2 * i2;
        let d = w0 * setupF[o + 7u] + w1 * setupF[o + 8u] + w2 * setupF[o + 9u];
        if (d >= best) { continue; }
        let b = t * TRI_STRIDE;
        var pc: array<f32, 6>;
        for (var c = 0u; c < 6u; c++) {
            // c < 3: position component c (tri floats 0..8); c >= 3: normal component c-3 (9..17)
            let base = select(b + 9u + (c - 3u), b + c, c < 3u);
            pc[c] = (w0 * tris[base] * i0 + w1 * tris[base + 3u] * i1 + w2 * tris[base + 6u] * i2) / iw;
        }
        best = d;
        pos = vec3f(pc[0], pc[1], pc[2]);
        nrm = vnorm(vec3f(pc[3], pc[4], pc[5]));
        alb = vec3f(tris[b + 18u], tris[b + 19u], tris[b + 20u]);
        covered = 1.0;
    }
    let i = pix(gid.x, gid.y);
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
    let cs = mmul(m, vec4f(wp, 1.0));
    if (cs.w <= 1e-6) { *ok = false; return vec2f(0.0); }
    let inv = 1.0 / cs.w;
    return vec2f(cs.x * inv * 0.5 + 0.5, 1.0 - (cs.y * inv * 0.5 + 0.5));
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let i = pix(gid.x, gid.y);
    motion[i] = vec4f(0.0);
    if (albedoMask[i].w == 0.0) { return; }
    let wp = position[i].xyz;
    var ok = true;
    let cur = uvOf(P.vp, wp, &ok);
    if (!ok) { return; }
    let prev = uvOf(P.pvp, wp, &ok);
    if (!ok) { return; }
    let mv = cur - prev;
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
    let i = pix(gid.x, gid.y);
    if (albedoMask[i].w == 0.0) { aoSS[i] = 1.0; return; }
    let p = position[i].xyz;
    let n = normal[i].xyz;
    let R = P.misc.y;
    var occ = 0; var used = 0;
    for (var s = 0u; s < P.ss_samples; s++) {
        let a = hash01(gid.x, gid.y, s) * 6.2831853;
        let rad = (0.3 + 0.7 * hash01(gid.x, gid.y, s + 97u)) * R;
        let sx = i32(gid.x) + lround(cos(a) * rad);
        let sy = i32(gid.y) + lround(sin(a) * rad);
        if (sx < 0 || sy < 0 || sx >= i32(P.w) || sy >= i32(P.h)) { continue; }
        let j = pix(u32(sx), u32(sy));
        if (albedoMask[j].w == 0.0) { continue; }
        used++;
        let d = position[j].xyz - p;
        let dist = vlen(d);
        if (dist < 1e-4 || dist > P.misc.x) { continue; }
        let ndl = vdot(n, vnorm(d));
        if (ndl > 0.15) { occ++; }
    }
    var v = 1.0;
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
    let b = t * TRI_STRIDE + 3u * k;
    return vec3f(tris[b], tris[b + 1u], tris[b + 2u]);
}
fn hit(o: vec3f, d: vec3f, t: u32, maxDist: f32) -> bool {
    let a = triVert(t, 0u);
    let e1 = triVert(t, 1u) - a;
    let e2 = triVert(t, 2u) - a;
    let p = vcross(d, e2);
    let det = vdot(e1, p);
    if (det > -1e-7 && det < 1e-7) { return false; }
    let inv = 1.0 / det;
    let tv = o - a;
    let u = vdot(tv, p) * inv;
    if (u < 0.0 || u > 1.0) { return false; }
    let q = vcross(tv, e1);
    let v = vdot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0) { return false; }
    let dist = vdot(e2, q) * inv;
    return dist > 1e-4 && dist < maxDist;
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let i = pix(gid.x, gid.y);
    if (albedoMask[i].w == 0.0) { aoRT[i] = 1.0; return; }
    let p = position[i].xyz;
    let n = normal[i].xyz;
    var ax = vec3f(1.0, 0.0, 0.0);
    if (abs(n.x) > 0.9) { ax = vec3f(0.0, 1.0, 0.0); }
    let tb = vnorm(vcross(ax, n));
    let bb = vcross(n, tb);
    let origin = p + n * 1e-3;
    var open = 0u;
    for (var s = 0u; s < P.rt_samples; s++) {
        let u1 = hash01(gid.x, gid.y, 2u * s);
        let u2 = hash01(gid.x, gid.y, 2u * s + 1u);
        let r = sqrt(u1);
        let phi = 6.2831853 * u2;
        let lx = r * cos(phi);
        let ly = r * sin(phi);
        let lz = sqrt(max(0.0, 1.0 - u1));
        let dir = vnorm(tb * lx + bb * ly + n * lz);
        var blocked = false;
        for (var t = 0u; t < P.ntri; t++) {
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
    let i = pix(gid.x, gid.y);
    let am = albedoMask[i];
    if (am.w == 0.0) { frame[i] = vec4f(0.0); hdr[i] = vec4f(0.0); return; }
    let n = normal[i].xyz;
    let ldir = P.light.xyz;
    let ndl = max(0.0, vdot(n, ldir * -1.0)) * P.light.w;
    let lit = (0.2 + ndl) * ao[i];
    let rgb = vec3f(am.x * lit, am.y * lit, am.z * lit);
    hdr[i] = vec4f(rgb, 0.0);
    frame[i] = vec4f(clamp(rgb, vec3f(0.0), vec3f(1.0)), 0.0);
}
