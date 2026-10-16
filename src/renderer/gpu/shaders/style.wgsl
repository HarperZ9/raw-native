// raw-native style passes in the C++ core (ADR 0019): the one shader source of each ported web
// style pass. scripts/wgsl_to_hlsl.py generates hlsl/style_<name>.hlsl and style_layout.hpp from
// this file; the WebGPU build runs it as written. The float64 references are
// raw/renderer/style_ditherlight.hpp and style_paintover.hpp; the checks are
// src/renderer/gpu/style_parity.cpp, bounds in evidence/cpp-port-style-bounds.json.

// Dither-quantised lighting. a: cell, px (pixels a cell covers), pa (pixel angle), gamma;
// b: dark, hemi, fogDist, fogMax; ink and paper in linear light (w unused).
struct DitherParams {
    width: u32, height: u32, levels: u32, source: u32,
    anchor: u32, lights: u32, pad0: u32, pad1: u32,
    a: vec4f,
    b: vec4f,
    ink: vec4f,
    paper: vec4f,
}
// Paint-over projection. opt: facingPow, fMin, floor, tolBase; opt2: tolGrazing, the 8-bit divisor.
struct PaintParams {
    width: u32, height: u32, count: u32, pad0: u32,
    opt: vec4f,
    opt2: vec4f,
}

fn sat1(x: f32) -> f32 {
    return clamp(x, 0.0, 1.0);
}
fn smooth1(a: f32, b: f32, x: f32) -> f32 {
    let t: f32 = sat1((x - a) / (b - a));
    return t * t * (3.0 - 2.0 * t);
}
fn linear_to_srgb(l0: f32) -> f32 {
    let l: f32 = clamp(l0, 0.0, 1.0);
    if (l <= 0.0031308) { return l * 12.92; }
    return 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}
fn pack_rgb8(c: vec3f) -> u32 {
    let r: u32 = u32(floor(linear_to_srgb(c.x) * 255.0 + 0.5));
    let g: u32 = u32(floor(linear_to_srgb(c.y) * 255.0 + 0.5));
    let b: u32 = u32(floor(linear_to_srgb(c.z) * 255.0 + 0.5));
    return r | (g << 8u) | (b << 16u) | (255u << 24u);
}

//@pass style_ditherlight
// One invocation per pixel. gb: six vec4f a pixel: (albedo, kind, depth, 0), world position,
// normal, anchor position, anchor normal, the four lights' shadow visibility. lights: six vec4f
// a light: (kind 0 radial or 1 beam, intensity, radius or range, core or cos outer),
// (position, wrap), (axis, cos inner), (tangent, slats), (bitangent, duty), (phase, 0, 0, 0).
// bn: the blue-noise ranks, 64 x 64. Writes a level and an RGBA8 word a pixel.
@group(0) @binding(0) var<uniform> D: DitherParams;
@group(0) @binding(1) var<storage, read> gb: array<vec4f>;
@group(0) @binding(2) var<storage, read> lights: array<vec4f>;
@group(0) @binding(3) var<storage, read> bn: array<u32>;
@group(0) @binding(4) var<storage, read_write> lvl: array<u32>;
@group(0) @binding(5) var<storage, read_write> out8: array<u32>;

fn mask_at(i: u32, pw: vec3f) -> f32 {
    let b: u32 = i * 6u;
    let l0: vec4f = lights[b];
    let l1: vec4f = lights[b + 1u];
    let v: vec3f = pw - l1.xyz;
    let r2: f32 = dot(v, v);
    if (l0.x < 0.5) {
        let q: f32 = r2 / (l0.z * l0.z);
        if (q >= 1.0) { return 0.0; }
        return (l0.y * (1.0 - q) * (1.0 - q)) / (1.0 + r2 / (l0.w * l0.w));
    }
    let r: f32 = sqrt(r2);
    if (r < 1.0e-4 || r >= l0.z) { return 0.0; }
    let l2: vec4f = lights[b + 2u];
    let l3: vec4f = lights[b + 3u];
    let l4: vec4f = lights[b + 4u];
    let l5: vec4f = lights[b + 5u];
    let c: f32 = dot(v, l2.xyz) / r;
    let cone: f32 = smooth1(l0.w, l2.w, c);
    if (cone <= 0.0) { return 0.0; }
    var blade: f32 = 1.0;
    if (l3.w > 0.0) {
        let phi: f32 = atan2(dot(v, l4.xyz), dot(v, l3.xyz));
        let c0: f32 = cos(3.141592653589793 * l4.w);
        blade = smooth1(c0 - 0.06, c0 + 0.06, cos(l3.w * phi + l5.x));
    }
    return l0.y * cone * blade * (1.0 - r / l0.z);
}
fn surface_term(i: u32, pw: vec3f, n: vec3f) -> f32 {
    let l1: vec4f = lights[i * 6u + 1u];
    let v: vec3f = l1.xyz - pw;
    let l: f32 = length(v);
    if (l < 1.0e-6) { return 1.0; }
    return sat1((dot(n, v) / l + l1.w) / (1.0 + l1.w));
}
fn bayer8(x0: i32, y0: i32) -> u32 {
    let x: u32 = u32(x0 & 7);
    let y: u32 = u32(y0 & 7);
    let v: u32 = x ^ y;
    return ((v & 1u) << 5u) | ((y & 1u) << 4u) | (((v >> 1u) & 1u) << 3u) | (((y >> 1u) & 1u) << 2u) | (((v >> 2u) & 1u) << 1u) | ((y >> 2u) & 1u);
}
fn threshold_at(cx: i32, cy: i32) -> f32 {
    if (D.source == 0u) { return (f32(bayer8(cx, cy)) + 0.5) / 64.0; }
    return (f32(bn[u32(((cy & 63) << 6u) | (cx & 63))]) + 0.5) / 4096.0;
}
fn octave_of(cell: f32, px: f32, pa: f32, depth: f32) -> f32 {
    let r: f32 = (px * pa * max(depth, 1.0e-6)) / cell;
    if (r <= 1.0) { return 0.0; }
    return ceil(log2(r));
}
fn level_lin(k: f32, n: f32, gamma: f32) -> f32 {
    if (k <= 0.0) { return 0.0; }
    return pow(k / (n - 1.0), gamma);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= D.width * D.height) { return; }
    let x: u32 = i % D.width;
    let y: u32 = i / D.width;
    let b: u32 = i * 6u;
    let g0: vec4f = gb[b];
    let pw: vec3f = gb[b + 1u].xyz;
    let nrm: vec3f = gb[b + 2u].xyz;
    let ap: vec3f = gb[b + 3u].xyz;
    let an: vec3f = gb[b + 4u].xyz;
    let vis: vec4f = gb[b + 5u];
    var tone: f32 = g0.x;
    if (g0.y > 1.5) {
        var e: f32 = D.b.x * (1.0 - D.b.y + D.b.y * (0.5 + 0.5 * nrm.y));
        for (var li: u32 = 0u; li < D.lights; li++) {
            let v: f32 = select(select(select(vis.x, vis.y, li == 1u), vis.z, li == 2u), vis.w, li == 3u);
            if (!(v > 0.0)) { continue; }
            let m: f32 = mask_at(li, pw);
            if (m > 0.0) { e += v * m * surface_term(li, pw, nrm); }
        }
        let lit: f32 = min(1.0, max(0.0, g0.x * e));
        let r: f32 = g0.z / D.b.z;
        let tf: f32 = D.b.w * (1.0 - exp(-r * r));
        tone = lit * (1.0 - tf) + tf;
    }
    var cx: i32 = i32(x);
    var cy: i32 = i32(y);
    if (D.anchor > 0u) {
        let ax: f32 = abs(an.x);
        let ay: f32 = abs(an.y);
        let az: f32 = abs(an.z);
        var u: f32 = ap.x;
        var w: f32 = ap.y;
        if (ax >= ay && ax >= az) {
            u = ap.z;
            w = ap.y;
        } else if (ay >= az) {
            u = ap.x;
            w = ap.z;
        }
        var size: f32 = D.a.x;
        if (D.anchor > 1u) { size = size * exp2(octave_of(D.a.x, D.a.y, D.a.z, g0.z)); }
        cx = i32(floor(u / size));
        cy = i32(floor(w / size));
    }
    let t: f32 = threshold_at(cx, cy);
    let n: f32 = f32(D.levels);
    let gamma: f32 = D.a.w;
    let yv: f32 = min(1.0, max(0.0, tone));
    var k: f32 = floor(pow(yv, 1.0 / gamma) * (n - 1.0));
    if (k > n - 2.0) { k = n - 2.0; }
    let lo: f32 = level_lin(k, n, gamma);
    let hi: f32 = level_lin(k + 1.0, n, gamma);
    let f: f32 = (yv - lo) / (hi - lo);
    if (f > t) { k = k + 1.0; }
    lvl[i] = u32(k);
    let l: f32 = level_lin(k, n, gamma);
    out8[i] = pack_rgb8(D.ink.xyz + (D.paper.xyz - D.ink.xyz) * l);
}

//@pass style_paintover
// One invocation per view pixel. gb: two vec4f a pixel: (position, hit), (normal, 0). cams: six
// vec4f a camera, the view camera first and then each projector: (ortho, position),
// (forward, scale), (right, fovY), (up, 0), (painting w, h, reach in radians, 0),
// (first painting word, first depth word, 0, 0). img: the paintings' RGBA8 words in turn; dep: the
// depth maps in turn. Writes RGBA8 with alpha 255 where some projector covers, else 0.
@group(0) @binding(0) var<uniform> Q: PaintParams;
@group(0) @binding(1) var<storage, read> gb: array<vec4f>;
@group(0) @binding(2) var<storage, read> cams: array<vec4f>;
@group(0) @binding(3) var<storage, read> img: array<u32>;
@group(0) @binding(4) var<storage, read> dep: array<f32>;
@group(0) @binding(5) var<storage, read_write> out8: array<u32>;

fn project_c(b: u32, w: f32, h: f32, p: vec3f) -> vec3f {
    let c0: vec4f = cams[b];
    let c1: vec4f = cams[b + 1u];
    let c2: vec4f = cams[b + 2u];
    let c3: vec4f = cams[b + 3u];
    let r: vec3f = p - c0.yzw;
    let z: f32 = dot(r, c1.xyz);
    let asp: f32 = w / h;
    let a: f32 = dot(r, c2.xyz);
    let bb: f32 = dot(r, c3.xyz);
    var u: f32 = a / (c1.w * asp);
    var v: f32 = bb / c1.w;
    if (c0.x < 0.5) {
        let t: f32 = tan(c2.w / 2.0);
        u = a / (z * t * asp);
        v = bb / (z * t);
    }
    return vec3f(((u + 1.0) / 2.0) * w - 0.5, ((1.0 - v) / 2.0) * h - 0.5, z);
}
fn to_camera(b: u32, p: vec3f) -> vec3f {
    if (cams[b].x > 0.5) { return -cams[b + 1u].xyz; }
    return normalize(cams[b].yzw - p);
}
fn texel(off: u32, w: u32, x: u32, y: u32) -> vec3f {
    let px: u32 = img[off + y * w + x];
    return vec3f(f32(px & 255u), f32((px >> 8u) & 255u), f32((px >> 16u) & 255u));
}
fn sample_rgb(off: u32, w: u32, h: u32, x: f32, y: f32) -> vec3f {
    let xc: f32 = clamp(x, 0.0, f32(w - 1u));
    let yc: f32 = clamp(y, 0.0, f32(h - 1u));
    let x0f: f32 = floor(xc);
    let y0f: f32 = floor(yc);
    let x0: u32 = u32(x0f);
    let y0: u32 = u32(y0f);
    let x1: u32 = min(w - 1u, x0 + 1u);
    let y1: u32 = min(h - 1u, y0 + 1u);
    let fx: f32 = xc - x0f;
    let fy: f32 = yc - y0f;
    let a: vec3f = texel(off, w, x0, y0);
    let b: vec3f = texel(off, w, x1, y0);
    let d: vec3f = texel(off, w, x0, y1);
    let e: vec3f = texel(off, w, x1, y1);
    return ((a * (1.0 - fx) + b * fx) * (1.0 - fy) + (d * (1.0 - fx) + e * fx) * fy) / Q.opt2.y;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= Q.width * Q.height) { return; }
    let g0: vec4f = gb[i * 2u];
    let g1: vec4f = gb[i * 2u + 1u];
    if (g0.w < 0.5) {
        out8[i] = 0u;
        return;
    }
    let pw: vec3f = g0.xyz;
    let nrm: vec3f = g1.xyz;
    let vdir: vec3f = to_camera(0u, pw);
    var wsum: f32 = 0.0;
    var col: vec3f = vec3f(0.0);
    for (var k: u32 = 0u; k < Q.count; k++) {
        let b: u32 = 6u * (k + 1u);
        let c4: vec4f = cams[b + 4u];
        let c5: vec4f = cams[b + 5u];
        let iw: f32 = c4.x;
        let ih: f32 = c4.y;
        let p: vec3f = project_c(b, iw, ih, pw);
        if (!(p.x >= -0.5 && p.x <= iw - 0.5 && p.y >= -0.5 && p.y <= ih - 0.5)) { continue; }
        let to: vec3f = to_camera(b, pw);
        let f: f32 = dot(nrm, to);
        if (f < Q.opt.y) { continue; }
        let ix: u32 = u32(min(iw - 1.0, max(0.0, floor(p.x + 0.5))));
        let iy: u32 = u32(min(ih - 1.0, max(0.0, floor(p.y + 0.5))));
        let dm: f32 = dep[u32(c5.y) + iy * u32(iw) + ix];
        var pxw: f32 = 2.0 * cams[b + 1u].w / ih;
        if (cams[b].x < 0.5) { pxw = 2.0 * tan(cams[b + 2u].w / 2.0) * p.z / ih; }
        let tol: f32 = pxw * (Q.opt.w + Q.opt2.x * (1.0 - f));
        if (p.z - dm > tol) { continue; }
        let ang: f32 = acos(clamp(dot(to, vdir), -1.0, 1.0));
        let a: f32 = ang / c4.z;
        let t: f32 = max(0.0, 1.0 - a * a);
        let wk: f32 = pow(f, Q.opt.x) * max(t * t, Q.opt.z);
        col += wk * sample_rgb(u32(c5.x), u32(iw), u32(ih), p.x, p.y);
        wsum += wk;
    }
    if (wsum > 0.0) {
        let c: vec3f = clamp(col / wsum, vec3f(0.0), vec3f(1.0));
        out8[i] = u32(floor(c.x * 255.0 + 0.5)) | (u32(floor(c.y * 255.0 + 0.5)) << 8u) | (u32(floor(c.z * 255.0 + 0.5)) << 16u) | (255u << 24u);
    } else {
        out8[i] = 0u;
    }
}
