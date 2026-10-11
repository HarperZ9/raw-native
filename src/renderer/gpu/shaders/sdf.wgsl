// raw-native's ray marcher on the GPU (RT stage R3): the float32 form of
// src/renderer/sdf_eval.cpp and sdf_march.cpp, read with raw/renderer/sdf.hpp. The SDF is a
// postfix program in N (16 floats a node); the evaluator keeps a distance stack and a stack of
// query frames as scalar arrays.

struct SdfParams {
    w: u32, h: u32, nodes: u32, flags: u32,
    count: u32, pad0: u32, pad1: u32, pad2: u32,
    tMax: f32, step: f32, softK: f32, fogA: f32,
    fogB: f32, fogMax: f32, near: f32, far: f32,
    eye: vec4f, fwd: vec4f, side: vec4f, up: vec4f,
    sun: vec4f,
}
// flags: 1 force sun visibility to 1 (M5 control), 2 SDF always in front (M6 control)
const PI: f32 = 3.14159265;

fn bulb_de(p: vec3f, iters: i32, power: f32) -> f32 {
    var z: vec3f = p;
    var dr: f32 = 1.0;
    var r: f32 = length(z);
    for (var i: i32 = 0; i < iters; i++) {
        r = length(z);
        if (r > 2.0) { break; }
        let theta: f32 = acos(clamp(z.z / r, -1.0, 1.0)) * power;
        let phi: f32 = atan2(z.y, z.x) * power;
        dr = pow(r, power - 1.0) * power * dr + 1.0;
        let zr: f32 = pow(r, power);
        z = vec3f(sin(theta) * cos(phi), sin(phi) * sin(theta), cos(theta)) * zr + p;
    }
    r = length(z);
    return 0.5 * log(r) * r / dr;
}
fn mandelbox_de(p: vec3f, iters: i32, scale: f32) -> f32 {
    var z: vec3f = p;
    var dr: f32 = 1.0;
    for (var i: i32 = 0; i < iters; i++) {
        z = clamp(z, vec3f(-1.0), vec3f(1.0)) * 2.0 - z;
        let r2: f32 = dot(z, z);
        if (r2 < 0.25) {
            z = z * 4.0;
            dr = dr * 4.0;
        } else {
            if (r2 < 1.0) {
                let k: f32 = 1.0 / r2;
                z = z * k;
                dr = dr * k;
            }
        }
        z = z * scale + p;
        dr = dr * abs(scale) + 1.0;
    }
    return length(z) / abs(dr);
}
// Every pass binds P at 0 and the program N at 1, so the shared evaluator below can read N.
fn prim(b: u32, p: vec3f) -> f32 {
    let op: i32 = i32(N[b]);
    if (op == 1) { return length(p) - N[b + 2u]; }
    if (op == 2 || op == 3) {
        let r: f32 = select(0.0, N[b + 5u], op == 3);
        let q: vec3f = abs(p) - (vec3f(N[b + 2u], N[b + 3u], N[b + 4u]) - vec3f(r));
        return length(max(q, vec3f(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
    }
    if (op == 4) {
        let a: f32 = sqrt(p.x * p.x + p.z * p.z) - N[b + 2u];
        return sqrt(a * a + p.y * p.y) - N[b + 3u];
    }
    if (op == 5) {
        let a: vec3f = vec3f(N[b + 2u], N[b + 3u], N[b + 4u]);
        let pa: vec3f = p - a;
        let ba: vec3f = vec3f(N[b + 5u], N[b + 6u], N[b + 7u]) - a;
        let hh: f32 = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
        return length(pa - ba * hh) - N[b + 8u];
    }
    if (op == 6) {
        let dx: f32 = sqrt(p.x * p.x + p.z * p.z) - N[b + 2u];
        let dy: f32 = abs(p.y) - N[b + 3u];
        return min(max(dx, dy), 0.0) + sqrt(max(dx, 0.0) * max(dx, 0.0) + max(dy, 0.0) * max(dy, 0.0));
    }
    if (op == 7) { return p.x * N[b + 2u] + p.y * N[b + 3u] + p.z * N[b + 4u] + N[b + 5u]; }
    if (op == 8) { return bulb_de(p, i32(N[b + 2u]), N[b + 3u]); }
    return mandelbox_de(p, i32(N[b + 2u]), N[b + 3u]);
}
fn rep1(v: f32, per: f32) -> f32 {
    if (per > 0.0) { return v - per * floor(v / per + 0.5); }
    return v;
}
// x: distance, y: material.
fn sdf_eval(q: vec3f) -> vec2f {
    var sd: array<f32, 16>;
    var sm: array<f32, 16>;
    var fx: array<f32, 16>;
    var fy: array<f32, 16>;
    var fz: array<f32, 16>;
    var fs: array<f32, 16>;
    var sp: u32 = 0u;
    var fsp: u32 = 0u;
    var p: vec3f = q;
    var scale: f32 = 1.0;
    for (var i: u32 = 0u; i < P.nodes; i++) {
        let b: u32 = i * 16u;
        let op: i32 = i32(N[b]);
        if (op < 20) {
            sd[sp] = prim(b, p) * scale;
            sm[sp] = N[b + 1u];
            sp = sp + 1u;
            continue;
        }
        if (op == 30) {
            fx[fsp] = p.x; fy[fsp] = p.y; fz[fsp] = p.z; fs[fsp] = scale;
            fsp = fsp + 1u;
            let d: vec3f = p - vec3f(N[b + 11u], N[b + 12u], N[b + 13u]);
            p = vec3f(N[b + 2u] * d.x + N[b + 3u] * d.y + N[b + 4u] * d.z, N[b + 5u] * d.x + N[b + 6u] * d.y + N[b + 7u] * d.z, N[b + 8u] * d.x + N[b + 9u] * d.y + N[b + 10u] * d.z) * (1.0 / N[b + 14u]);
            scale = scale * N[b + 14u];
            continue;
        }
        if (op == 31) {
            fsp = fsp - 1u;
            p = vec3f(fx[fsp], fy[fsp], fz[fsp]);
            scale = fs[fsp];
            continue;
        }
        if (op == 32) {
            p = vec3f(rep1(p.x, N[b + 2u]), rep1(p.y, N[b + 3u]), rep1(p.z, N[b + 4u]));
            continue;
        }
        let bd: f32 = sd[sp - 1u];
        let bm: f32 = sm[sp - 1u];
        let ad: f32 = sd[sp - 2u];
        let am: f32 = sm[sp - 2u];
        sp = sp - 2u;
        var rd: f32 = ad;
        var rm: f32 = am;
        if (op == 20) {
            if (bd < ad) { rd = bd; rm = bm; }
        } else if (op == 22) {
            rd = max(ad, -bd);
        } else if (op == 23) {
            if (bd > ad) { rd = bd; rm = bm; }
        } else {
            let k: f32 = N[b + 2u];
            let hh: f32 = clamp(0.5 + 0.5 * (bd - ad) / k, 0.0, 1.0);
            rd = bd + (ad - bd) * hh - k * hh * (1.0 - hh);
            rm = select(bm, am, hh > 0.5);
        }
        sd[sp] = rd;
        sm[sp] = rm;
        sp = sp + 1u;
    }
    if (sp == 0u) { return vec2f(1e30, -1.0); }
    return vec2f(sd[sp - 1u], sm[sp - 1u]);
}
fn sdf_normal(x: vec3f, t: f32) -> vec3f {
    let e: f32 = 1e-4 * (1.0 + t);
    let a: f32 = sdf_eval(vec3f(x.x + e, x.y - e, x.z - e)).x;
    let b: f32 = sdf_eval(vec3f(x.x - e, x.y - e, x.z + e)).x;
    let c: f32 = sdf_eval(vec3f(x.x - e, x.y + e, x.z - e)).x;
    let d: f32 = sdf_eval(vec3f(x.x + e, x.y + e, x.z + e)).x;
    return normalize(vec3f(a - b - c + d, -a - b + c + d, -a + b - c + d));
}
// Method notes 1 and 2 of evidence/rt-r3-bounds.json: bracket the surface by sign, bisect.
fn sdf_refine(o: vec3f, d: vec3f, t: f32, e: f32) -> f32 {
    var t2: f32 = t;
    var step: f32 = e;
    var inside: bool = false;
    for (var k: u32 = 0u; k < 64u; k++) {
        if (inside) { break; }
        t2 = t2 + step;
        inside = sdf_eval(o + d * t2).x < 0.0;
    }
    for (var k2: u32 = 0u; k2 < 16u; k2++) {
        if (inside) { break; }
        t2 = t2 + step;
        step = step * 2.0;
        inside = sdf_eval(o + d * t2).x < 0.0;
    }
    if (!inside) { return t; }
    var a: f32 = t;
    var b: f32 = t2;
    for (var i: u32 = 0u; i < 24u; i++) {
        let m: f32 = 0.5 * (a + b);
        if (sdf_eval(o + d * m).x > 0.0) { a = m; } else { b = m; }
    }
    return 0.5 * (a + b);
}
// x: t (-1 a miss), y: material, z: closest d / (1 + t), w: steps.
fn sdf_march(o: vec3f, d: vec3f, tMax: f32) -> vec4f {
    var t: f32 = 0.0;
    var closest: f32 = 1e30;
    for (var i: u32 = 0u; i < 512u; i++) {
        let s: vec2f = sdf_eval(o + d * t);
        closest = min(closest, s.x / (1.0 + t));
        if (s.x < 1e-4 * (1.0 + t)) {
            return vec4f(sdf_refine(o, d, t, 1e-4 * (1.0 + t)), s.y, closest, f32(i + 1u));
        }
        t = t + s.x * P.step;
        if (t > tMax) { break; }
    }
    return vec4f(-1.0, -1.0, closest, 512.0);
}
fn soft_shadow(x: vec3f, l: vec3f, k: f32) -> f32 {
    var res: f32 = 1.0;
    var t: f32 = 0.02;
    for (var i: u32 = 0u; i < 128u; i++) {
        let h: f32 = sdf_eval(x + l * t).x;
        res = min(res, k * h / t);
        if (res < 1e-3) { return 0.0; }
        t = t + clamp(h, 0.01, 0.5);
        if (t > 30.0) { break; }
    }
    return clamp(res, 0.0, 1.0);
}
fn ambient_occlusion(x: vec3f, n: vec3f) -> f32 {
    var occ: f32 = 0.0;
    var sca: f32 = 1.0;
    for (var i: u32 = 0u; i < 5u; i++) {
        let h: f32 = 0.02 + 0.12 * f32(i);
        occ = occ + (h - sdf_eval(x + n * h).x) * sca;
        sca = sca * 0.85;
    }
    return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}
fn hard_shadow(x: vec3f, l: vec3f) -> bool {
    return sdf_march(x + l * 0.02, l, 30.0).x >= 0.0;
}
fn fog_height(o: vec3f, d: vec3f, t: f32) -> f32 {
    let base: f32 = P.fogA * exp(-P.fogB * o.y);
    let k: f32 = P.fogB * d.y;
    var optical: f32 = base * t;
    if (abs(k) >= 1e-9) { optical = base * (1.0 - exp(-k * t)) / k; }
    return exp(-optical);
}
fn cam_ray(x: u32, y: u32) -> vec3f {
    let nx: f32 = (f32(x) + 0.5) / f32(P.w) * 2.0 - 1.0;
    let ny: f32 = 1.0 - (f32(y) + 0.5) / f32(P.h) * 2.0;
    return normalize(P.fwd.xyz + P.side.xyz * (nx * P.eye.w * P.fwd.w) + P.up.xyz * (ny * P.eye.w));
}

//@pass sdf_march
// One invocation per pixel: the camera ray's march. O, 8 floats a pixel: t (-1 a miss),
// material, closest d / (1 + t), steps, normal (3), pad.
@group(0) @binding(0) var<uniform> P: SdfParams;
@group(0) @binding(1) var<storage, read> N: array<f32>;
@group(0) @binding(2) var<storage, read_write> O: array<f32>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let p: u32 = (gid.y * P.w + gid.x) * 8u;
    let d: vec3f = cam_ray(gid.x, gid.y);
    let m: vec4f = sdf_march(P.eye.xyz, d, P.tMax);
    O[p] = m.x; O[p + 1u] = m.y; O[p + 2u] = m.z; O[p + 3u] = m.w;
    var n: vec3f = vec3f(0.0);
    if (m.x >= 0.0) { n = sdf_normal(P.eye.xyz + d * m.x, m.x); }
    O[p + 4u] = n.x; O[p + 5u] = n.y; O[p + 6u] = n.z; O[p + 7u] = 0.0;
}

//@pass sdf_terms
// One invocation per point of X (position, normal: 6 floats; P.count points): the soft shadow
// toward the sun with k = P.softK, and the ambient occlusion. O: 2 floats a point.
@group(0) @binding(0) var<uniform> P: SdfParams;
@group(0) @binding(1) var<storage, read> N: array<f32>;
@group(0) @binding(2) var<storage, read> X: array<f32>;
@group(0) @binding(3) var<storage, read_write> O: array<f32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= P.count) { return; }
    let x: vec3f = vec3f(X[i * 6u], X[i * 6u + 1u], X[i * 6u + 2u]);
    let n: vec3f = vec3f(X[i * 6u + 3u], X[i * 6u + 4u], X[i * 6u + 5u]);
    O[i * 2u] = soft_shadow(x, P.sun.xyz, P.softK);
    O[i * 2u + 1u] = ambient_occlusion(x, n);
}

//@pass sdf_fog
// One invocation per case of C (origin, direction, distance, pad: 8 floats; P.count cases):
// homogeneous transmittance (sigma = P.fogA), the height fog in closed form, and its numerical
// march with 256 and with 4 midpoint samples. O: 4 floats a case.
@group(0) @binding(0) var<uniform> P: SdfParams;
@group(0) @binding(1) var<storage, read> N: array<f32>;
@group(0) @binding(2) var<storage, read> C: array<f32>;
@group(0) @binding(3) var<storage, read_write> O: array<f32>;

fn fog_march(o: vec3f, d: vec3f, t: f32, n: u32) -> f32 {
    let dt: f32 = t / f32(n);
    var optical: f32 = 0.0;
    for (var i: u32 = 0u; i < n; i++) {
        optical = optical + P.fogA * exp(-P.fogB * (o.y + d.y * (f32(i) + 0.5) * dt)) * dt;
    }
    return exp(-optical);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= P.count) { return; }
    let o: vec3f = vec3f(C[i * 8u], C[i * 8u + 1u], C[i * 8u + 2u]);
    let d: vec3f = vec3f(C[i * 8u + 3u], C[i * 8u + 4u], C[i * 8u + 5u]);
    let t: f32 = C[i * 8u + 6u];
    O[i * 4u] = exp(-P.fogA * t) + N[0] * 0.0;   // N read so WebGPU keeps binding 1 in the layout
    O[i * 4u + 1u] = fog_height(o, d, t);
    O[i * 4u + 2u] = fog_march(o, d, t, 256u);
    O[i * 4u + 3u] = fog_march(o, d, t, 4u);
}

//@pass sdf_god
// One invocation per pixel: single scattering of the sun along the camera ray up to
// min(hit, P.fogMax), 64 midpoint samples, each with a hard SDF shadow ray (P.flags 1: none).
// M: sdf_march's output. O: 4 floats a pixel: in-scatter, clear samples, end distance, pad.
@group(0) @binding(0) var<uniform> P: SdfParams;
@group(0) @binding(1) var<storage, read> N: array<f32>;
@group(0) @binding(2) var<storage, read> M: array<f32>;
@group(0) @binding(3) var<storage, read_write> O: array<f32>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let px: u32 = gid.y * P.w + gid.x;
    let d: vec3f = cam_ray(gid.x, gid.y);
    var tEnd: f32 = P.fogMax;
    if (M[px * 8u] >= 0.0) { tEnd = min(tEnd, M[px * 8u]); }
    let dt: f32 = tEnd / 64.0;
    let g: f32 = 0.6;
    let c: f32 = dot(d, P.sun.xyz);
    let phase: f32 = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * c, 1.5));
    var l: f32 = 0.0;
    var clear: u32 = 0u;
    for (var i: u32 = 0u; i < 64u; i++) {
        let t: f32 = (f32(i) + 0.5) * dt;
        let x: vec3f = P.eye.xyz + d * t;
        if ((P.flags & 1u) == 0u) {
            if (hard_shadow(x, P.sun.xyz)) { continue; }
        }
        clear = clear + 1u;
        l = l + P.fogA * exp(-P.fogB * x.y) * fog_height(P.eye.xyz, d, t) * phase * dt;
    }
    O[px * 4u] = l;
    O[px * 4u + 1u] = f32(clear);
    O[px * 4u + 2u] = tEnd;
    O[px * 4u + 3u] = 0.0;
}

//@pass sdf_composite
// One invocation per pixel: the nearer of the R1 visibility buffer (B: slot + 1 and integer
// depth; D: its z/w) and the SDF hit (M: sdf_march's output), the SDF's view distance turned
// into z/w by the same projection (P.near, P.far). P.flags 2 puts the SDF in front (control).
// O: 4 floats a pixel: winner (0 raster, 1 SDF, -1 none), the SDF's z/w, pad 2.
@group(0) @binding(0) var<uniform> P: SdfParams;
@group(0) @binding(1) var<storage, read> N: array<f32>;
@group(0) @binding(2) var<storage, read> B: array<u32>;
@group(0) @binding(3) var<storage, read> D: array<f32>;
@group(0) @binding(4) var<storage, read> M: array<f32>;
@group(0) @binding(5) var<storage, read_write> O: array<f32>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let px: u32 = gid.y * P.w + gid.x;
    let raster: bool = B[px * 2u] != 0u;
    let t: f32 = M[px * 8u];
    var zs: f32 = 2.0;
    if (t >= 0.0) {
        let zv: f32 = t * dot(cam_ray(gid.x, gid.y), P.fwd.xyz);
        zs = (P.far + P.near) / (P.far - P.near) - 2.0 * P.far * P.near / ((P.far - P.near) * zv);
    }
    var winner: f32 = -1.0;
    if (raster) { winner = 0.0; }
    if (t >= 0.0) {
        if (!raster || zs < D[px] || (P.flags & 2u) != 0u) { winner = 1.0; }
    }
    O[px * 4u] = winner;
    O[px * 4u + 1u] = zs;
    O[px * 4u + 2u] = N[0] * 0.0;   // N read so WebGPU keeps binding 1 in the layout
    O[px * 4u + 3u] = 0.0;
}
