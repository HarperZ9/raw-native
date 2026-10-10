// raw-native's path tracer on the GPU (RT stage R2): the compute form of
// src/renderer/rt_pt_cpu.cpp and rt_pt_math.hpp, interface v1 (raw/renderer/rt_pathtrace.hpp).
// pt_trace accumulates a slice of samples per pixel into ACC; pt_finish writes the outputs.
//
// ACC, 16 floats a pixel: radiance sum (3), luminance Welford n, mean, M2 (3), albedo sum (3),
// normal sum (3), depth, motion (2), triangle. O, 16 floats a pixel: radiance (3), albedo (3),
// normal (3), depth, motion (2), variance, triangle (-1 a miss), pad (2).

struct PtParams {
    w: u32, h: u32, sppBegin: u32, sppCount: u32,
    maxBounces: u32, flags: u32, seedLo: u32, seedHi: u32,
    root: u32, emitters: u32, emitterOffset: u32, emitterArea: f32,
    eye: vec4f, fwd: vec4f, side: vec4f, up: vec4f,
    peye: vec4f, pfwd: vec4f, pside: vec4f, pup: vec4f,
    sun: vec4f, sunIrr: vec4f, sky: vec4f,
}
const PI: f32 = 3.14159265;
const BIG: f32 = 0x1.fffffep+127f;
const SLAB: f32 = 1.00000036;
const F_JITTER: u32 = 1u;
const F_RR: u32 = 2u;
const F_DROPCOS: u32 = 4u;
const F_MISONE: u32 = 8u;
const F_FIRST: u32 = 16u;

fn pcg2d(v0: vec2u) -> vec2u {
    var v: vec2u = v0 * 1664525u + 1013904223u;
    v.x = v.x + v.y * 1664525u;
    v.y = v.y + v.x * 1664525u;
    v = v ^ (v >> vec2u(16u));
    v.x = v.x + v.y * 1664525u;
    v.y = v.y + v.x * 1664525u;
    v = v ^ (v >> vec2u(16u));
    return v;
}
fn rand2(seedLo: u32, seedHi: u32, px: u32, smp: u32, dim: u32) -> vec2f {
    let v: vec2u = pcg2d(vec2u(px ^ (seedLo * 0x9E3779B9u), (smp * 64u + dim) ^ seedHi));
    return vec2f(f32(v.x >> 8u), f32(v.y >> 8u)) * 5.96046448e-8;
}
fn lum(c: vec3f) -> f32 {
    return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
}
fn ggx_d(a2: f32, c: f32) -> f32 {
    let d: f32 = c * c * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
fn lambda_g(a2: f32, c: f32) -> f32 {
    let c2: f32 = c * c;
    return (-1.0 + sqrt(1.0 + a2 * (1.0 - c2) / c2)) * 0.5;
}
fn lobe_prob(metal: f32, spec: f32) -> f32 {
    return metal + (1.0 - metal) * 0.5 * spec;
}
// f cos and the mixture pdf, local frame: xyz f cos, w pdf.
fn eval_bsdf(base: vec3f, rough: f32, metal: f32, spec: f32, wo: vec3f, wi: vec3f, dropCos: bool) -> vec4f {
    if (wo.z <= 0.0 || wi.z <= 0.0) { return vec4f(0.0); }
    let ps: f32 = lobe_prob(metal, spec);
    let cosI: f32 = select(wi.z, 1.0, dropCos);
    var f: vec3f = base * ((1.0 - metal) / PI);
    var pdf: f32 = (1.0 - ps) * wi.z / PI;
    if (ps > 0.0) {
        let a: f32 = max(1e-4, rough * rough);
        let a2: f32 = a * a;
        let h: vec3f = normalize(wo + wi);
        let oh: f32 = dot(wo, h);
        let dd: f32 = ggx_d(a2, h.z);
        let g2: f32 = 1.0 / (1.0 + lambda_g(a2, wo.z) + lambda_g(a2, wi.z));
        let g1: f32 = 1.0 / (1.0 + lambda_g(a2, wo.z));
        let x1: f32 = clamp(1.0 - oh, 0.0, 1.0);
        let fw: f32 = x1 * x1 * x1 * x1 * x1;
        let f0: vec3f = mix(vec3f(0.04 * spec), base, vec3f(metal));
        f = f + (f0 + (vec3f(1.0) - f0) * fw) * (dd * g2 / (4.0 * wo.z * wi.z));
        pdf = pdf + ps * g1 * dd / (4.0 * wo.z);
    }
    return vec4f(f * cosI, pdf);
}
fn sample_vndf(wo: vec3f, a: f32, u1: f32, u2: f32) -> vec3f {
    let vh: vec3f = normalize(vec3f(a * wo.x, a * wo.y, wo.z));
    let lensq: f32 = vh.x * vh.x + vh.y * vh.y;
    var t1: vec3f = vec3f(1.0, 0.0, 0.0);
    if (lensq > 0.0) { t1 = vec3f(-vh.y, vh.x, 0.0) * (1.0 / sqrt(lensq)); }
    let t2: vec3f = cross(vh, t1);
    let r: f32 = sqrt(u1);
    let phi: f32 = 2.0 * PI * u2;
    let p1: f32 = r * cos(phi);
    let s: f32 = 0.5 * (1.0 + vh.z);
    let p2: f32 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * (r * sin(phi));
    let nh: vec3f = t1 * p1 + t2 * p2 + vh * sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2));
    return normalize(vec3f(a * nh.x, a * nh.y, max(0.0, nh.z)));
}
// xyz the sampled direction (local), w 1 when it is above the surface.
fn sample_bsdf(rough: f32, metal: f32, spec: f32, wo: vec3f, pick: f32, u1: f32, u2: f32) -> vec4f {
    var wi: vec3f = vec3f(0.0);
    if (pick < lobe_prob(metal, spec)) {
        let h: vec3f = sample_vndf(wo, max(1e-4, rough * rough), u1, u2);
        wi = h * (2.0 * dot(wo, h)) - wo;
    } else {
        let r: f32 = sqrt(u1);
        let phi: f32 = 2.0 * PI * u2;
        wi = vec3f(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
    }
    return vec4f(wi, select(0.0, 1.0, wi.z > 0.0));
}
// Duff et al. 2017: tangent and bitangent of n.
fn basis_t(n: vec3f) -> vec3f {
    let s: f32 = select(-1.0, 1.0, n.z >= 0.0);
    let a: f32 = -1.0 / (s + n.z);
    return vec3f(1.0 + s * n.x * n.x * a, s * n.x * n.y * a, -s * n.x);
}
fn basis_b(n: vec3f) -> vec3f {
    let s: f32 = select(-1.0, 1.0, n.z >= 0.0);
    let a: f32 = -1.0 / (s + n.z);
    return vec3f(n.x * n.y * a, s + n.y * n.y * a, -n.y);
}

//@pass pt_trace
// One invocation per pixel: samples [P.sppBegin, P.sppBegin + P.sppCount) added into ACC.
// NI, NB the BVH (rt.wgsl's layout, root P.root); T triangles (9 floats); A attributes
// (16 floats a triangle: u v nx ny nz of each vertex, texture); TX textures (offset, width,
// height per texture, then texels); M materials (8 floats a texture: factor, roughness,
// metallic, specular, emission rgb, pad) then from P.emitterOffset (triangle, cdf) pairs.
@group(0) @binding(0) var<uniform> P: PtParams;
@group(0) @binding(1) var<storage, read> NI: array<i32>;
@group(0) @binding(2) var<storage, read> NB: array<f32>;
@group(0) @binding(3) var<storage, read> T: array<f32>;
@group(0) @binding(4) var<storage, read> A: array<f32>;
@group(0) @binding(5) var<storage, read> TX: array<u32>;
@group(0) @binding(6) var<storage, read> M: array<f32>;
@group(0) @binding(7) var<storage, read_write> ACC: array<f32>;

fn vtx(t: u32, k: u32) -> vec3f {
    return vec3f(T[t * 9u + k * 3u], T[t * 9u + k * 3u + 1u], T[t * 9u + k * 3u + 2u]);
}
// Moeller-Trumbore, raw/math/primitives.hpp's order: x t, y u, z v, w 1 on a hit.
fn hit_tri(t: u32, o: vec3f, d: vec3f) -> vec4f {
    let a: vec3f = vtx(t, 0u);
    let e1: vec3f = vtx(t, 1u) - a;
    let e2: vec3f = vtx(t, 2u) - a;
    let p: vec3f = vec3f(d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x);
    let det: f32 = e1.x * p.x + e1.y * p.y + e1.z * p.z;
    if (det > -1e-7 && det < 1e-7) { return vec4f(0.0); }
    let inv: f32 = 1.0 / det;
    let tv: vec3f = o - a;
    let u: f32 = (tv.x * p.x + tv.y * p.y + tv.z * p.z) * inv;
    if (u < 0.0 || u > 1.0) { return vec4f(0.0); }
    let q: vec3f = vec3f(tv.y * e1.z - tv.z * e1.y, tv.z * e1.x - tv.x * e1.z, tv.x * e1.y - tv.y * e1.x);
    let v: f32 = (d.x * q.x + d.y * q.y + d.z * q.z) * inv;
    if (v < 0.0 || u + v > 1.0) { return vec4f(0.0); }
    let tt: f32 = (e2.x * q.x + e2.y * q.y + e2.z * q.z) * inv;
    return vec4f(tt, u, v, select(0.0, 1.0, tt > 1e-4));
}
fn inv_c(x: f32) -> f32 {
    if (x != 0.0) { return 1.0 / x; }
    return select(-BIG, BIG, x >= 0.0);
}
// The nearest hit with t < tMax (any hit when anyHit): x t, y u, z v, w triangle (-1 none).
fn trace(o: vec3f, d: vec3f, tMax: f32, anyHit: bool) -> vec4f {
    let iv: vec3f = vec3f(inv_c(d.x), inv_c(d.y), inv_c(d.z));
    var best: vec4f = vec4f(tMax, 0.0, 0.0, -1.0);
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
            let h: vec4f = hit_tri(u32(leaf), o, d);
            if (h.w > 0.0 && (h.x < best.x || (best.w >= 0.0 && h.x == best.x && f32(leaf) < best.w))) {
                best = vec4f(h.x, h.y, h.z, f32(leaf));
                if (anyHit) { break; }
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
fn attr(t: u32, k: u32, b: vec3f) -> f32 {
    return A[t * 16u + k] * b.x + A[t * 16u + 5u + k] * b.y + A[t * 16u + 10u + k] * b.z;
}
fn texel(tex: u32, u: f32, v: f32) -> vec3f {
    let w: u32 = TX[tex * 3u + 1u];
    let h: u32 = TX[tex * 3u + 2u];
    let tx: u32 = u32(i32(floor(u * f32(w)))) & (w - 1u);
    let ty: u32 = u32(i32(floor(v * f32(h)))) & (h - 1u);
    let c: u32 = TX[TX[tex * 3u] + ty * w + tx];
    return vec3f(f32(c & 255u), f32((c >> 8u) & 255u), f32((c >> 16u) & 255u));
}
fn offset_p(p: vec3f, ng: vec3f, dir: vec3f) -> vec3f {
    let e: f32 = 2e-4 * (1.0 + max(abs(p.x), max(abs(p.y), abs(p.z))));
    return p + ng * select(-e, e, dot(ng, dir) > 0.0);
}
fn cam_dir(px: f32, py: f32, eye: vec4f, fwd: vec4f, side: vec4f, up: vec4f) -> vec3f {
    let nx: f32 = px / f32(P.w) * 2.0 - 1.0;
    let ny: f32 = 1.0 - py / f32(P.h) * 2.0;
    return normalize(fwd.xyz + side.xyz * (nx * eye.w * fwd.w) + up.xyz * (ny * eye.w));
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let px: u32 = gid.y * P.w + gid.x;
    let o: u32 = px * 16u;
    if ((P.flags & F_FIRST) != 0u) {
        for (var z: u32 = 0u; z < 16u; z++) { ACC[o + z] = 0.0; }
        ACC[o + 15u] = -1.0;
        let cd: vec3f = cam_dir(f32(gid.x) + 0.5, f32(gid.y) + 0.5, P.eye, P.fwd, P.side, P.up);
        let ch: vec4f = trace(P.eye.xyz, cd, BIG, false);
        if (ch.w >= 0.0) {
            let hp: vec3f = P.eye.xyz + cd * ch.x;
            ACC[o + 12u] = dot(hp - P.eye.xyz, P.fwd.xyz);
            ACC[o + 15u] = ch.w;
            let v: vec3f = hp - P.peye.xyz;
            let z: f32 = dot(v, P.pfwd.xyz);
            let nx: f32 = dot(v, P.pside.xyz) / (z * P.peye.w * P.pfwd.w);
            let ny: f32 = dot(v, P.pup.xyz) / (z * P.peye.w);
            ACC[o + 13u] = (nx * 0.5 + 0.5) * f32(P.w) - (f32(gid.x) + 0.5);
            ACC[o + 14u] = (1.0 - (ny * 0.5 + 0.5)) * f32(P.h) - (f32(gid.y) + 0.5);
        }
    }
    let dropCos: bool = (P.flags & F_DROPCOS) != 0u;
    let misOne: bool = (P.flags & F_MISONE) != 0u;
    for (var k: u32 = 0u; k < P.sppCount; k++) {
        let smp: u32 = P.sppBegin + k;
        var j: vec2f = vec2f(0.5);
        if ((P.flags & F_JITTER) != 0u) { j = rand2(P.seedLo, P.seedHi, px, smp, 0u); }
        var ro: vec3f = P.eye.xyz;
        var rd: vec3f = cam_dir(f32(gid.x) + j.x, f32(gid.y) + j.y, P.eye, P.fwd, P.side, P.up);
        var tp: vec3f = vec3f(1.0);
        var l: vec3f = vec3f(0.0);
        var prevPdf: f32 = 0.0;
        for (var b: u32 = 0u; b <= P.maxBounces; b++) {
            let h: vec4f = trace(ro, rd, BIG, false);
            if (h.w < 0.0) {
                l = l + tp * P.sky.xyz;
                break;
            }
            let t: u32 = u32(h.w);
            let bc: vec3f = vec3f(1.0 - h.y - h.z, h.y, h.z);
            let p: vec3f = ro + rd * h.x;
            var ng: vec3f = normalize(cross(vtx(t, 1u) - vtx(t, 0u), vtx(t, 2u) - vtx(t, 0u)));
            var n: vec3f = normalize(vec3f(attr(t, 2u, bc), attr(t, 3u, bc), attr(t, 4u, bc)));
            let tex: u32 = u32(A[t * 16u + 15u]);
            let mb: u32 = tex * 8u;
            let base: vec3f = texel(tex, attr(t, 0u, bc), attr(t, 1u, bc)) * (M[mb] / 255.0);
            if (dot(ng, rd) > 0.0) { ng = -ng; }
            if (dot(n, ng) < 0.0) { n = -n; }
            if (b == 0u) {
                ACC[o + 6u] = ACC[o + 6u] + base.x; ACC[o + 7u] = ACC[o + 7u] + base.y; ACC[o + 8u] = ACC[o + 8u] + base.z;
                ACC[o + 9u] = ACC[o + 9u] + n.x; ACC[o + 10u] = ACC[o + 10u] + n.y; ACC[o + 11u] = ACC[o + 11u] + n.z;
            }
            let le: vec3f = vec3f(M[mb + 4u], M[mb + 5u], M[mb + 6u]);
            if (max(le.x, max(le.y, le.z)) > 0.0) {
                var w: f32 = 1.0;
                if (b > 0u && !misOne) {
                    let pl: f32 = h.x * h.x / (abs(dot(ng, rd)) * P.emitterArea);
                    w = prevPdf * prevPdf / (prevPdf * prevPdf + pl * pl);
                }
                l = l + tp * le * w;
            }
            if (b == P.maxBounces) { break; }
            let tb: vec3f = basis_t(n);
            let bb: vec3f = basis_b(n);
            let wo: vec3f = vec3f(dot(-rd, tb), dot(-rd, bb), dot(-rd, n));
            if (wo.z <= 0.0) { break; }
            let rough: f32 = M[mb + 1u];
            let metal: f32 = M[mb + 2u];
            let spec: f32 = M[mb + 3u];
            let dim: u32 = 2u + b * 4u;
            // Next-event estimation: the sun, then one emitter sample.
            if (P.sun.w > 0.0) {
                let ls: vec3f = P.sun.xyz;
                let es: vec4f = eval_bsdf(base, rough, metal, spec, wo, vec3f(dot(ls, tb), dot(ls, bb), dot(ls, n)), dropCos);
                if (max(es.x, max(es.y, es.z)) > 0.0) {
                    if (trace(offset_p(p, ng, ls), ls, BIG, true).w < 0.0) { l = l + tp * es.xyz * P.sunIrr.xyz; }
                }
            }
            if (P.emitters > 0u) {
                let ra: vec2f = rand2(P.seedLo, P.seedHi, px, smp, dim + 2u);
                let rb: vec2f = rand2(P.seedLo, P.seedHi, px, smp, dim + 3u);
                let want: f32 = ra.x * P.emitterArea;
                var lo: u32 = 0u;
                var hi: u32 = P.emitters;
                for (var it: u32 = 0u; it < 32u; it++) {
                    if (lo >= hi) { break; }
                    let mid: u32 = (lo + hi) / 2u;
                    if (M[P.emitterOffset + mid * 2u + 1u] <= want) { lo = mid + 1u; } else { hi = mid; }
                }
                let et: u32 = u32(M[P.emitterOffset + min(lo, P.emitters - 1u) * 2u]);
                let su: f32 = sqrt(ra.y);
                let q: vec3f = vtx(et, 0u) * (1.0 - su) + vtx(et, 1u) * (su * (1.0 - rb.x)) + vtx(et, 2u) * (su * rb.x);
                let to: vec3f = q - p;
                let dist: f32 = length(to);
                let wl: vec3f = to * (1.0 / dist);
                let cosL: f32 = abs(dot(normalize(cross(vtx(et, 1u) - vtx(et, 0u), vtx(et, 2u) - vtx(et, 0u))), wl));
                if (cosL > 0.0) {
                    let pl: f32 = dist * dist / (cosL * P.emitterArea);
                    let ee: vec4f = eval_bsdf(base, rough, metal, spec, wo, vec3f(dot(wl, tb), dot(wl, bb), dot(wl, n)), dropCos);
                    if (max(ee.x, max(ee.y, ee.z)) > 0.0 && trace(offset_p(p, ng, wl), wl, dist * (1.0 - 1e-3), true).w < 0.0) {
                        var wm: f32 = 1.0;
                        if (!misOne) { wm = pl * pl / (pl * pl + ee.w * ee.w); }
                        let em: u32 = u32(A[et * 16u + 15u]) * 8u;
                        l = l + tp * ee.xyz * vec3f(M[em + 4u], M[em + 5u], M[em + 6u]) * (wm / pl);
                    }
                }
            }
            // BSDF sampling.
            let u: vec2f = rand2(P.seedLo, P.seedHi, px, smp, dim);
            let r: vec2f = rand2(P.seedLo, P.seedHi, px, smp, dim + 1u);
            let s: vec4f = sample_bsdf(rough, metal, spec, wo, r.x, u.x, u.y);
            if (s.w <= 0.0) { break; }
            let e: vec4f = eval_bsdf(base, rough, metal, spec, wo, s.xyz, dropCos);
            if (e.w <= 0.0) { break; }
            tp = tp * e.xyz * (1.0 / e.w);
            prevPdf = e.w;
            if ((P.flags & F_RR) != 0u && b >= 3u) {
                let qq: f32 = min(0.95, max(tp.x, max(tp.y, tp.z)));
                if (r.y >= qq) { break; }
                tp = tp * (1.0 / qq);
            }
            let wi: vec3f = tb * s.x + bb * s.y + n * s.z;
            ro = offset_p(p, ng, wi);
            rd = wi;
        }
        ACC[o] = ACC[o] + l.x; ACC[o + 1u] = ACC[o + 1u] + l.y; ACC[o + 2u] = ACC[o + 2u] + l.z;
        let y: f32 = lum(l);
        let cnt: f32 = ACC[o + 3u] + 1.0;
        let dl: f32 = y - ACC[o + 4u];
        ACC[o + 3u] = cnt;
        ACC[o + 4u] = ACC[o + 4u] + dl / cnt;
        ACC[o + 5u] = ACC[o + 5u] + dl * (y - ACC[o + 4u]);
    }
}

//@pass pt_finish
// One invocation per pixel: means, the variance of the mean, and the centre-ray AOVs into O.
@group(0) @binding(0) var<uniform> P: PtParams;
@group(0) @binding(1) var<storage, read> ACC: array<f32>;
@group(0) @binding(2) var<storage, read_write> O: array<f32>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let o: u32 = (gid.y * P.w + gid.x) * 16u;
    let n: f32 = ACC[o + 3u];
    let inv: f32 = select(0.0, 1.0 / n, n > 0.0);
    for (var k: u32 = 0u; k < 3u; k++) {
        O[o + k] = ACC[o + k] * inv;
        O[o + 3u + k] = ACC[o + 6u + k] * inv;
        O[o + 6u + k] = ACC[o + 9u + k] * inv;
    }
    O[o + 9u] = ACC[o + 12u];
    O[o + 10u] = ACC[o + 13u];
    O[o + 11u] = ACC[o + 14u];
    O[o + 12u] = select(0.0, ACC[o + 5u] / (n - 1.0) / n, n > 1.0);
    O[o + 13u] = ACC[o + 15u];
    O[o + 14u] = 0.0;
    O[o + 15u] = 0.0;
}

//@pass pt_hybrid
// One invocation per pixel of a raster G-buffer (swr_gbuffer): a sun shadow ray (any hit) and
// one mirror reflection ray about the shading normal (closest hit) through the BVH. P.flags
// bit 32 drops the origin offset (H1 control), bit 64 tilts the shading normal by 0.05 rad (H2 control).
// O: 4 floats a pixel: 1 lit / 0 shadowed (-1 uncovered), reflected triangle (-1 none), its t, pad.
@group(0) @binding(0) var<uniform> P: PtParams;
@group(0) @binding(1) var<storage, read> NI: array<i32>;
@group(0) @binding(2) var<storage, read> NB: array<f32>;
@group(0) @binding(3) var<storage, read> T: array<f32>;
@group(0) @binding(4) var<storage, read> G: array<f32>;
@group(0) @binding(5) var<storage, read_write> O: array<f32>;

fn vtx(t: u32, k: u32) -> vec3f {
    return vec3f(T[t * 9u + k * 3u], T[t * 9u + k * 3u + 1u], T[t * 9u + k * 3u + 2u]);
}
fn hit_tri(t: u32, o: vec3f, d: vec3f) -> vec4f {
    let a: vec3f = vtx(t, 0u);
    let e1: vec3f = vtx(t, 1u) - a;
    let e2: vec3f = vtx(t, 2u) - a;
    let p: vec3f = vec3f(d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x);
    let det: f32 = e1.x * p.x + e1.y * p.y + e1.z * p.z;
    if (det > -1e-7 && det < 1e-7) { return vec4f(0.0); }
    let inv: f32 = 1.0 / det;
    let tv: vec3f = o - a;
    let u: f32 = (tv.x * p.x + tv.y * p.y + tv.z * p.z) * inv;
    if (u < 0.0 || u > 1.0) { return vec4f(0.0); }
    let q: vec3f = vec3f(tv.y * e1.z - tv.z * e1.y, tv.z * e1.x - tv.x * e1.z, tv.x * e1.y - tv.y * e1.x);
    let v: f32 = (d.x * q.x + d.y * q.y + d.z * q.z) * inv;
    if (v < 0.0 || u + v > 1.0) { return vec4f(0.0); }
    let tt: f32 = (e2.x * q.x + e2.y * q.y + e2.z * q.z) * inv;
    return vec4f(tt, u, v, select(0.0, 1.0, tt > 1e-4));
}
fn inv_c(x: f32) -> f32 {
    if (x != 0.0) { return 1.0 / x; }
    return select(-BIG, BIG, x >= 0.0);
}
fn trace(o: vec3f, d: vec3f, tMax: f32, anyHit: bool) -> vec4f {
    let iv: vec3f = vec3f(inv_c(d.x), inv_c(d.y), inv_c(d.z));
    var best: vec4f = vec4f(tMax, 0.0, 0.0, -1.0);
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
            let h: vec4f = hit_tri(u32(leaf), o, d);
            if (h.w > 0.0 && (h.x < best.x || (best.w >= 0.0 && h.x == best.x && f32(leaf) < best.w))) {
                best = vec4f(h.x, h.y, h.z, f32(leaf));
                if (anyHit) { break; }
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
fn offset_h(p: vec3f, ng: vec3f, dir: vec3f) -> vec3f {
    if ((P.flags & 32u) != 0u) { return p; }
    let e: f32 = 2e-4 * (1.0 + max(abs(p.x), max(abs(p.y), abs(p.z))));
    return p + ng * select(-e, e, dot(ng, dir) > 0.0);
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let p: u32 = gid.y * P.w + gid.x;
    O[p * 4u] = -1.0;
    O[p * 4u + 1u] = -1.0;
    O[p * 4u + 2u] = 0.0;
    O[p * 4u + 3u] = 0.0;
    if (G[p * 12u + 9u] == 0.0) { return; }
    let pos: vec3f = vec3f(G[p * 12u], G[p * 12u + 1u], G[p * 12u + 2u]);
    let d: vec3f = normalize(pos - P.eye.xyz);
    var ng: vec3f = vec3f(G[p * 12u + 3u], G[p * 12u + 4u], G[p * 12u + 5u]);
    var n: vec3f = vec3f(G[p * 12u + 6u], G[p * 12u + 7u], G[p * 12u + 8u]);
    if (dot(ng, d) > 0.0) { ng = -ng; }
    if (dot(n, ng) < 0.0) { n = -n; }
    if ((P.flags & 64u) != 0u) { n = normalize(n + basis_t(n) * 0.05); }
    let l: vec3f = P.sun.xyz;
    O[p * 4u] = select(0.0, 1.0, trace(offset_h(pos, ng, l), l, BIG, true).w < 0.0);
    let r: vec3f = d - n * (2.0 * dot(d, n));
    let h: vec4f = trace(offset_h(pos, ng, r), r, BIG, false);
    O[p * 4u + 1u] = h.w;
    O[p * 4u + 2u] = select(0.0, h.x, h.w >= 0.0);
}
