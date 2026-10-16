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

// The post stack's passes (web/motion/post.mjs PostPass): size (width, height, frame, time) and
// 16 parameter floats.
struct PostParams {
    size: vec4f,
    p0: vec4f,
    p1: vec4f,
    p2: vec4f,
    p3: vec4f,
}
// CRT purity. a: s, w (beam), mag, magScale; b: earth, degauss, tau, mains; c: field, blank,
// wobble, t; d: spread, 0, 0, 0.
struct PurityParams {
    width: u32, height: u32, pad0: u32, pad1: u32,
    a: vec4f,
    b: vec4f,
    c: vec4f,
    d: vec4f,
}
// Pigment-space lighting. a: Emid, t0, strength, shape; fog: colour and the display gain.
struct LightpaintParams {
    width: u32, height: u32, complement: u32, pad0: u32,
    a: vec4f,
    shadow: vec4f,
    lightp: vec4f,
    fog: vec4f,
}
// Interference glaze. a: d0, dVar, noiseScale, coverage; b: n0, n2, A, B; c: filmN, aspect, tan,
// gain; the camera's axes and eye; fog colour.
struct GlazeParams {
    width: u32, height: u32, dispersion: u32, pad0: u32,
    a: vec4f,
    b: vec4f,
    c: vec4f,
    fwd: vec4f,
    right: vec4f,
    up: vec4f,
    eye: vec4f,
    fog: vec4f,
}
// Curvature hatching. a: freq, width, umb, contour; ink; paper.
struct HatchParams {
    width: u32, height: u32, layers: u32, pad0: u32,
    a: vec4f,
    ink: vec4f,
    paper: vec4f,
}

// Hysteresis quantisation. mode 0 bands, 1 palette; a: N, margin, gain, depthTol; b: K.
struct HystParams {
    width: u32, height: u32, mode: u32, palSize: u32,
    hasState: u32, hasMotion: u32, on: u32, pad0: u32,
    a: vec4f,
    b: vec4f,
}
// Palette-locked lighting. a: gain, ambient, moon, cycle rate; b: time, moon direction; the lamp
// and two window glows as (position, radius), (core, intensity, wrap, 0).
struct PalettelightParams {
    width: u32, height: u32, steps: u32, voidSteps: u32,
    a: vec4f,
    b: vec4f,
    lampA: vec4f,
    lampB: vec4f,
    g0a: vec4f,
    g0b: vec4f,
    g1a: vec4f,
    g1b: vec4f,
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

// The shader library's hash and noise fade (web/shaders/common.mjs, purity.mjs).
fn pcg(v: u32) -> u32 {
    let state: u32 = v * 747796405u + 2891336453u;
    let word: u32 = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
fn hash3(x: u32, y: u32, z: u32) -> f32 {
    return f32(pcg(x ^ pcg(y ^ pcg(z)))) / 4294967296.0;
}
fn fade5(t: f32) -> f32 {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}
// The lab parity pages' display encode (web/shaders/lab/gpu-kit.mjs): a soft shoulder, sRGB, 8 bits.
fn lab_pack(c: vec3f, gain: f32) -> u32 {
    let r: u32 = u32(floor(linear_to_srgb(1.0 - exp(-max(0.0, c.x) * gain)) * 255.0 + 0.5));
    let g: u32 = u32(floor(linear_to_srgb(1.0 - exp(-max(0.0, c.y) * gain)) * 255.0 + 0.5));
    let b: u32 = u32(floor(linear_to_srgb(1.0 - exp(-max(0.0, c.z) * gain)) * 255.0 + 0.5));
    return r | (g << 8u) | (b << 16u) | (255u << 24u);
}

//@pass style_ditherlight
// One invocation per pixel. gb: six vec4f a pixel: (albedo, kind, depth, haze), world position,
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
    let hz: f32 = min(1.0, max(0.0, g0.w));
    if (hz > 0.0) { tone = 1.0 - (1.0 - tone) * (1.0 - hz); }
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

//@pass style_exposure
// The post stack's built-in exposure (web/motion/post.mjs): linear light times 2^ev, ev = p0.x.
// src, prev and dst are one vec4f a pixel; prev is unused.
@group(0) @binding(0) var<uniform> S: PostParams;
@group(0) @binding(1) var<storage, read> src: array<vec4f>;
@group(0) @binding(2) var<storage, read> prev: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> dst: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= u32(S.size.x) * u32(S.size.y)) { return; }
    // prev is read only behind a guard that never holds (p3.w is 0), so every backend keeps the
    // binding in the pass's layout: WebGPU's derived layout drops a binding the shader never uses.
    if (S.p3.w > 1.0e30) {
        dst[i] = prev[i];
        return;
    }
    let c: vec4f = src[i];
    dst[i] = vec4f(c.xyz * exp2(S.p0.x), c.w);
}

//@pass style_srgb_decode
// The sRGB decode (IEC 61966-2-1) the encoding contract inserts before a pass that reads linear
// light from display values; negative values pass through sign-symmetric.
@group(0) @binding(0) var<uniform> S: PostParams;
@group(0) @binding(1) var<storage, read> src: array<vec4f>;
@group(0) @binding(2) var<storage, read> prev: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> dst: array<vec4f>;

fn dec(v: f32) -> f32 {
    let a: f32 = abs(v);
    var l: f32 = a / 12.92;
    if (a > 0.04045) { l = pow((a + 0.055) / 1.055, 2.4); }
    return select(l, -l, v < 0.0);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= u32(S.size.x) * u32(S.size.y)) { return; }
    // prev is read only behind a guard that never holds (p3.w is 0), so every backend keeps the
    // binding in the pass's layout: WebGPU's derived layout drops a binding the shader never uses.
    if (S.p3.w > 1.0e30) {
        dst[i] = prev[i];
        return;
    }
    let c: vec4f = src[i];
    dst[i] = vec4f(dec(c.x), dec(c.y), dec(c.z), c.w);
}

//@pass style_purity
// CRT colour purity and degauss (raw/renderer/style_purity.hpp). src: display-linear RGBA; writes
// the sRGB-encoded RGBA8 word a pixel.
@group(0) @binding(0) var<uniform> U: PurityParams;
@group(0) @binding(1) var<storage, read> src: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> out8: array<u32>;

fn centre(k: i32) -> f32 {
    return f32(k - 1) / 3.0;
}
fn overlap(a0: f32, a1: f32, b0: f32, b1: f32) -> f32 {
    return max(0.0, min(a1, b1) - max(a0, b0));
}
fn vnoise2(x: f32, y: f32, seed: u32) -> f32 {
    let fx: f32 = floor(x);
    let fy: f32 = floor(y);
    let xi: i32 = i32(fx);
    let yi: i32 = i32(fy);
    let u: f32 = fade5(x - fx);
    let v: f32 = fade5(y - fy);
    let a: f32 = hash3(u32(xi + 4096), u32(yi + 4096), seed);
    let b: f32 = hash3(u32(xi + 4097), u32(yi + 4096), seed);
    let c: f32 = hash3(u32(xi + 4096), u32(yi + 4097), seed);
    let d: f32 = hash3(u32(xi + 4097), u32(yi + 4097), seed);
    return (a * (1.0 - u) + b * u) * (1.0 - v) + (c * (1.0 - u) + d * u) * v;
}
// The fraction of gun k's light on stripe j for its landing error ek.
fn landing_f(k: i32, j: i32, ek: f32) -> f32 {
    let a0: f32 = centre(k) + ek - U.a.y / 2.0;
    let a1: f32 = centre(k) + ek + U.a.y / 2.0;
    var f: f32 = 0.0;
    for (var m: i32 = -1; m <= 1; m++) {
        f += overlap(a0, a1, centre(j) + f32(m) - U.a.x / 2.0, centre(j) + f32(m) + U.a.x / 2.0) / U.a.y;
    }
    return f;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let wf: f32 = f32(U.width);
    let hf: f32 = f32(U.height);
    let u: f32 = ((f32(x) + 0.5) / wf) * 2.0 - 1.0;
    let v: f32 = ((f32(y) + 0.5) / hf) * 2.0 - 1.0;
    let asp: f32 = wf / hf;
    let r2: f32 = (u * u * asp * asp + v * v) / (asp * asp + 1.0) * 2.0;
    let sc: f32 = U.a.w * wf;
    let m: f32 = U.a.z * ((0.65 * vnoise2(f32(x) / sc, f32(y) / sc, 31u) + 0.35 * vnoise2(f32(x) / (0.4 * sc), f32(y) / (0.4 * sc), 37u)) - 0.5) * 2.0;
    let stat: f32 = m + U.b.x * u * r2;
    let tl: f32 = U.c.w + (f32(y) / hf) * (1.0 - U.c.y) / U.c.x;
    let dg: f32 = U.b.y * exp(-tl / U.b.z) * sin(2.0 * 3.141592653589793 * U.b.w * tl) * (0.3 + r2);
    let e: f32 = stat + dg;
    let xs: f32 = f32(x) - U.c.z * (dg / max(U.b.y, 1.0e-9));
    let x0f: f32 = floor(xs);
    let x0: i32 = i32(x0f);
    let fx: f32 = xs - x0f;
    let wl: i32 = i32(U.width) - 1;
    let row: u32 = u32(y) * U.width;
    let c0: vec3f = src[row + u32(clamp(x0, 0, wl))].xyz;
    let c1: vec3f = src[row + u32(clamp(x0 + 1, 0, wl))].xyz;
    let I: vec3f = c0 * (1.0 - fx) + c1 * fx;
    var o: vec3f = vec3f(0.0);
    for (var k: i32 = 0; k < 3; k++) {
        let ek: f32 = e * (1.0 + U.d.x * f32(k - 1));
        let ik: f32 = select(select(I.x, I.y, k == 1), I.z, k == 2);
        o += vec3f(landing_f(k, 0, ek), landing_f(k, 1, ek), landing_f(k, 2, ek)) * ik;
    }
    out8[i] = pack_rgb8(o);
}

//@pass style_lightpaint
// Pigment-space lighting (raw/renderer/style_relight.hpp). pig: the pigment tables as
// style_image.hpp packPigments (K at 0, S at 124, W at 248, the latent lookup at 341); alb, light:
// albedo and irradiance; aux: (fog fraction, kind, view distance, 0). Writes the lab-encoded RGBA8.
@group(0) @binding(0) var<uniform> U: LightpaintParams;
@group(0) @binding(1) var<storage, read> pig: array<f32>;
@group(0) @binding(2) var<storage, read> alb: array<vec4f>;
@group(0) @binding(3) var<storage, read> light: array<vec4f>;
@group(0) @binding(4) var<storage, read> aux: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> out8: array<u32>;

fn km_decode(c: vec4f) -> vec3f {
    var o: vec3f = vec3f(0.0);
    for (var w: u32 = 0u; w < 31u; w++) {
        let K: f32 = dot(c, vec4f(pig[4u * w], pig[4u * w + 1u], pig[4u * w + 2u], pig[4u * w + 3u]));
        let S: f32 = dot(c, vec4f(pig[124u + 4u * w], pig[124u + 4u * w + 1u], pig[124u + 4u * w + 2u], pig[124u + 4u * w + 3u]));
        let q: f32 = K / max(S, 1.0e-9);
        let R: f32 = 1.0 + q - sqrt(q * q + 2.0 * q);
        o += R * vec3f(pig[248u + 3u * w], pig[248u + 3u * w + 1u], pig[248u + 3u * w + 2u]);
    }
    return o;
}
fn lut_conc(s: vec3f) -> vec4f {
    let f: vec3f = clamp(s, vec3f(0.0), vec3f(1.0)) * 16.0;
    let i0: vec3i = vec3i(min(vec3f(15.0), floor(f)));
    let t: vec3f = f - vec3f(i0);
    var c: vec4f = vec4f(0.0);
    for (var dz: i32 = 0; dz < 2; dz++) {
        for (var dy: i32 = 0; dy < 2; dy++) {
            for (var dx: i32 = 0; dx < 2; dx++) {
                let w: f32 = select(1.0 - t.x, t.x, dx == 1) * select(1.0 - t.y, t.y, dy == 1) * select(1.0 - t.z, t.z, dz == 1);
                let o: u32 = 341u + u32((((i0.z + dz) * 17 + i0.y + dy) * 17 + i0.x + dx) * 4);
                c += w * vec4f(pig[o], pig[o + 1u], pig[o + 2u], pig[o + 3u]);
            }
        }
    }
    return c;
}
fn srgb3(c: vec3f) -> vec3f {
    return vec3f(linear_to_srgb(c.x), linear_to_srgb(c.y), linear_to_srgb(c.z));
}
fn lum(c: vec3f) -> f32 {
    return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
}
fn complement_of(lc: vec3f) -> vec3f {
    let mx: f32 = max(lc.x, max(lc.y, lc.z));
    let mn: f32 = min(lc.x, min(lc.y, lc.z));
    let q: vec3f = vec3f(mx + mn) - lc;
    let m: f32 = max(max(q.x, max(q.y, q.z)), 1.0e-9);
    return 0.3 * q / m;
}
fn shade(a: vec3f, E: vec3f) -> vec3f {
    let e: f32 = lum(E);
    if (!(e > 1.0e-12)) { return vec3f(0.0); }
    let lc: vec3f = E / e;
    let t: f32 = e / (e + U.a.x);
    let t0: f32 = U.a.y;
    var ws: f32 = 0.0;
    var wl: f32 = 0.0;
    if (t < t0) { ws = U.a.z * pow((t0 - t) / t0, U.a.w); }
    if (t > t0) { wl = U.a.z * pow((t - t0) / (1.0 - t0), U.a.w); }
    let ca: vec4f = lut_conc(srgb3(a));
    let ra: vec3f = a - km_decode(ca);
    var cs: vec4f = U.shadow;
    if (U.complement > 0u) { cs = lut_conc(srgb3(complement_of(lc))); }
    let wa: f32 = 1.0 - ws - wl;
    let c: vec4f = wa * ca + ws * cs + wl * U.lightp;
    let paint: vec3f = clamp(km_decode(c) + wa * ra, vec3f(0.0), vec3f(1.0));
    let tint: vec3f = paint * lc;
    let yt: f32 = lum(tint);
    let tgt: f32 = lum(a * E);
    if (!(yt > 1.0e-9)) { return a * E; }
    return tint * (tgt / yt);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let E: vec3f = light[i].xyz;
    let k: vec4f = aux[i];
    var c: vec3f = E;
    if (k.y > 1.5) { c = shade(alb[i].xyz, E); }
    let o: vec3f = c * (1.0 - k.x) + U.fog.xyz * k.x;
    out8[i] = lab_pack(o, U.fog.w);
}

//@pass style_glaze
// Interference glazes (raw/renderer/style_relight.hpp). pig, alb, light, aux as style_lightpaint;
// nrm: the surface normal a pixel. Writes the lab-encoded RGBA8.
@group(0) @binding(0) var<uniform> U: GlazeParams;
@group(0) @binding(1) var<storage, read> pig: array<f32>;
@group(0) @binding(2) var<storage, read> alb: array<vec4f>;
@group(0) @binding(3) var<storage, read> light: array<vec4f>;
@group(0) @binding(4) var<storage, read> aux: array<vec4f>;
@group(0) @binding(5) var<storage, read> nrm: array<vec4f>;
@group(0) @binding(6) var<storage, read_write> out8: array<u32>;

fn lut_conc(s: vec3f) -> vec4f {
    let f: vec3f = clamp(s, vec3f(0.0), vec3f(1.0)) * 16.0;
    let i0: vec3i = vec3i(min(vec3f(15.0), floor(f)));
    let t: vec3f = f - vec3f(i0);
    var c: vec4f = vec4f(0.0);
    for (var dz: i32 = 0; dz < 2; dz++) {
        for (var dy: i32 = 0; dy < 2; dy++) {
            for (var dx: i32 = 0; dx < 2; dx++) {
                let w: f32 = select(1.0 - t.x, t.x, dx == 1) * select(1.0 - t.y, t.y, dy == 1) * select(1.0 - t.z, t.z, dz == 1);
                let o: u32 = 341u + u32((((i0.z + dz) * 17 + i0.y + dy) * 17 + i0.x + dx) * 4);
                c += w * vec4f(pig[o], pig[o + 1u], pig[o + 2u], pig[o + 3u]);
            }
        }
    }
    return c;
}
fn airy(r01: f32, r12: f32, cd: f32) -> f32 {
    return (r01 * r01 + r12 * r12 + 2.0 * r01 * r12 * cd) / (1.0 + r01 * r01 * r12 * r12 + 2.0 * r01 * r12 * cd);
}
fn film_reflectance(lam: f32, cos_air: f32, d: f32) -> f32 {
    let lm: f32 = lam / 1000.0;
    var n1: f32 = U.c.x;
    if (U.dispersion > 0u) { n1 = U.b.z + U.b.w / (lm * lm); }
    let n0: f32 = U.b.x;
    let n2: f32 = U.b.y;
    let sa: f32 = sqrt(max(0.0, 1.0 - cos_air * cos_air));
    let s0: f32 = sa / n0;
    let c0: f32 = sqrt(1.0 - s0 * s0);
    let s1: f32 = sa / n1;
    let c1: f32 = sqrt(1.0 - s1 * s1);
    let s2: f32 = sa / n2;
    let c2: f32 = sqrt(1.0 - s2 * s2);
    let cd: f32 = cos((4.0 * 3.141592653589793 * n1 * d * c1) / lam);
    let rs: f32 = airy((n0 * c0 - n1 * c1) / (n0 * c0 + n1 * c1), (n1 * c1 - n2 * c2) / (n1 * c1 + n2 * c2), cd);
    let rp: f32 = airy((n1 * c0 - n0 * c1) / (n1 * c0 + n0 * c1), (n2 * c1 - n1 * c2) / (n2 * c1 + n1 * c2), cd);
    return 0.5 * (rs + rp);
}
fn vlayer(xi: i32, yi: i32, u: f32, v: f32, kz: u32) -> f32 {
    let a: f32 = hash3(u32(xi + 4096), u32(yi + 4096), kz);
    let b: f32 = hash3(u32(xi + 4097), u32(yi + 4096), kz);
    let c: f32 = hash3(u32(xi + 4096), u32(yi + 4097), kz);
    let d: f32 = hash3(u32(xi + 4097), u32(yi + 4097), kz);
    return (a * (1.0 - u) + b * u) * (1.0 - v) + (c * (1.0 - u) + d * u) * v;
}
fn vnoise3(x: f32, y: f32, z: f32, seed: u32) -> f32 {
    let fx: f32 = floor(x);
    let fy: f32 = floor(y);
    let fz: f32 = floor(z);
    let zi: i32 = i32(fz);
    let u: f32 = fade5(x - fx);
    let v: f32 = fade5(y - fy);
    let w: f32 = fade5(z - fz);
    let a0: f32 = vlayer(i32(fx), i32(fy), u, v, u32(zi + 4096) * 131u + seed);
    let a1: f32 = vlayer(i32(fx), i32(fy), u, v, u32(zi + 4097) * 131u + seed);
    return a0 * (1.0 - w) + a1 * w;
}
fn thickness_at(q: vec3f) -> f32 {
    let s1: vec3f = q / U.a.z;
    let s2: vec3f = q / (0.37 * U.a.z);
    return U.a.x + U.a.y * ((0.65 * vnoise3(s1.x, s1.y, s1.z, 17u) + 0.35 * vnoise3(s2.x, s2.y, s2.z, 29u)) - 0.5) * 2.0;
}
fn glaze_colour(a: vec3f, cos_air: f32, d: f32) -> vec3f {
    let c: vec4f = lut_conc(vec3f(linear_to_srgb(a.x), linear_to_srgb(a.y), linear_to_srgb(a.z)));
    var o: vec3f = a;
    for (var w: u32 = 0u; w < 31u; w++) {
        let K: f32 = dot(c, vec4f(pig[4u * w], pig[4u * w + 1u], pig[4u * w + 2u], pig[4u * w + 3u]));
        let S: f32 = dot(c, vec4f(pig[124u + 4u * w], pig[124u + 4u * w + 1u], pig[124u + 4u * w + 2u], pig[124u + 4u * w + 3u]));
        let q: f32 = K / max(S, 1.0e-9);
        let Rb: f32 = 1.0 + q - sqrt(q * q + 2.0 * q);
        let Rf: f32 = U.a.w * film_reflectance(400.0 + 10.0 * f32(w), cos_air, d);
        let Rt: f32 = Rf + ((1.0 - Rf) * (1.0 - Rf) * Rb) / (1.0 - Rf * Rb);
        o += vec3f(pig[248u + 3u * w], pig[248u + 3u * w + 1u], pig[248u + 3u * w + 2u]) * (Rt - Rb);
    }
    return o;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let x: u32 = i % U.width;
    let y: u32 = i / U.width;
    let E: vec3f = light[i].xyz;
    let k: vec4f = aux[i];
    var c: vec3f = E;
    if (k.y > 1.5) {
        let sx: f32 = ((f32(x) + 0.5) / f32(U.width) - 0.5) * 2.0 * U.c.y * U.c.z;
        let sy: f32 = (0.5 - (f32(y) + 0.5) / f32(U.height)) * 2.0 * U.c.z;
        let r: vec3f = U.fwd.xyz + U.right.xyz * sx + U.up.xyz * sy;
        let rl: f32 = length(r);
        let cos_air: f32 = min(1.0, abs(dot(nrm[i].xyz, r)) / rl);
        let q: vec3f = U.eye.xyz + r * (k.z / rl);
        c = glaze_colour(alb[i].xyz, cos_air, thickness_at(q)) * E;
    }
    let o: vec3f = c * (1.0 - k.x) + U.fog.xyz * k.x;
    out8[i] = lab_pack(o, U.c.w);
}

//@pass style_hatch
// Curvature hatching (raw/renderer/style_hatch.hpp) over the owned still life of
// web/shaders/fixtures/stilllife.mjs, seen from its camera. Writes the sRGB-encoded RGBA8.
@group(0) @binding(0) var<uniform> U: HatchParams;
@group(0) @binding(1) var<storage, read_write> out8: array<u32>;

fn sd_sphere(p: vec3f) -> f32 {
    return length(p - vec3f(-0.9, 0.55, 0.2)) - 0.55;
}
fn sd_torus(p: vec3f) -> f32 {
    let qx: f32 = length(vec2f(p.x - 0.75, p.z - 0.45)) - 0.5;
    let qy: f32 = p.y - 0.18;
    return sqrt(qx * qx + qy * qy) - 0.18;
}
fn sd_capsule(p: vec3f) -> f32 {
    let pa: vec3f = p - vec3f(0.05, 0.25, -0.95);
    let ba: vec3f = vec3f(0.0, 1.3, 0.0);
    let h: f32 = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - 0.25;
}
fn sd_round_box(p: vec3f) -> f32 {
    let q: vec3f = abs(p - vec3f(1.65, 0.3, -0.6)) - vec3f(0.22, 0.22, 0.22);
    return length(max(q, vec3f(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0) - 0.08;
}
fn still_sdf(p: vec3f) -> f32 {
    return min(min(min(min(p.y, sd_sphere(p)), sd_torus(p)), sd_capsule(p)), sd_round_box(p));
}
fn mixed(p: vec3f, ei: vec3f, ej: vec3f) -> f32 {
    return (still_sdf(p + ei + ej) - still_sdf(p + ei - ej) - still_sdf(p - ei + ej) + still_sdf(p - ei - ej)) / (4.0 * 0.01 * 0.01);
}
// The surface normal and the line direction's normal partner at p; returns 1 at an umbilic.
fn curvature(p: vec3f, nout: ptr<function, vec3f>, other: ptr<function, vec3f>) -> f32 {
    let ex: vec3f = vec3f(0.01, 0.0, 0.0);
    let ey: vec3f = vec3f(0.0, 0.01, 0.0);
    let ez: vec3f = vec3f(0.0, 0.0, 0.01);
    let f0: f32 = still_sdf(p);
    let fxp: f32 = still_sdf(p + ex);
    let fxm: f32 = still_sdf(p - ex);
    let fyp: f32 = still_sdf(p + ey);
    let fym: f32 = still_sdf(p - ey);
    let fzp: f32 = still_sdf(p + ez);
    let fzm: f32 = still_sdf(p - ez);
    let g: vec3f = vec3f(fxp - fxm, fyp - fym, fzp - fzm) / (2.0 * 0.01);
    let gl: f32 = length(g);
    let n: vec3f = g / gl;
    let hxy: f32 = mixed(p, ex, ey);
    let hxz: f32 = mixed(p, ex, ez);
    let hyz: f32 = mixed(p, ey, ez);
    let r0: vec3f = vec3f((fxp - 2.0 * f0 + fxm) / (0.01 * 0.01), hxy, hxz);
    let r1: vec3f = vec3f(hxy, (fyp - 2.0 * f0 + fym) / (0.01 * 0.01), hyz);
    let r2: vec3f = vec3f(hxz, hyz, (fzp - 2.0 * f0 + fzm) / (0.01 * 0.01));
    var aux: vec3f = vec3f(1.0, 0.0, 0.0);
    if (abs(n.y) < 0.9) { aux = vec3f(0.0, 1.0, 0.0); }
    let t1: vec3f = normalize(cross(n, aux));
    let t2: vec3f = cross(n, t1);
    let h1: vec3f = vec3f(dot(r0, t1), dot(r1, t1), dot(r2, t1));
    let h2: vec3f = vec3f(dot(r0, t2), dot(r1, t2), dot(r2, t2));
    let a: f32 = dot(t1, h1) / gl;
    let b: f32 = dot(t1, h2) / gl;
    let d: f32 = dot(t2, h2) / gl;
    let m: f32 = 0.5 * (a + d);
    let r: f32 = sqrt(0.25 * (a - d) * (a - d) + b * b);
    let k1: f32 = m + r;
    let k2: f32 = m - r;
    var kmax: f32 = k2;
    if (abs(k1) >= abs(k2)) { kmax = k1; }
    var ex2: f32 = b;
    var ey2: f32 = kmax - a;
    if (abs(kmax - d) > abs(kmax - a)) {
        ex2 = kmax - d;
        ey2 = b;
    }
    let el: f32 = sqrt(ex2 * ex2 + ey2 * ey2);
    var umb: f32 = 0.0;
    if (r < U.a.z * max(max(abs(k1), abs(k2)), 0.05) || el < 1.0e-12) { umb = 1.0; }
    var dir: vec3f = t1;
    if (el > 0.0) { dir = normalize(t1 * (ex2 / el) + t2 * (ey2 / el)); }
    if (umb > 0.5) {
        let up: vec3f = vec3f(0.0, 1.0, 0.0) - n * n.y;
        dir = t1;
        if (length(up) > 1.0e-6) { dir = normalize(up); }
    }
    *nout = n;
    *other = cross(n, dir);
    return umb;
}
fn line_cov(s: f32, freq: f32, half_w: f32, px: f32) -> f32 {
    let u: f32 = s * freq;
    let fr: f32 = u - floor(u);
    let dist: f32 = min(fr, 1.0 - fr) / freq;
    return clamp((half_w - dist) / px + 0.5, 0.0, 1.0) * min(1.0, (2.0 * half_w) / px);
}
fn hatch_lines(s: f32, freq: f32, px: f32, half_w: f32) -> f32 {
    let lev: f32 = max(0.0, log2((px * freq) / 0.25));
    let l0: f32 = floor(lev);
    let t: f32 = lev - l0;
    let f0: f32 = freq / exp2(l0);
    return line_cov(s, f0, half_w, px) * (1.0 - t) + line_cov(s, f0 / 2.0, half_w, px) * t;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let x: f32 = f32(i % U.width);
    let y: f32 = f32(i / U.width);
    let wf: f32 = f32(U.width);
    let hf: f32 = f32(U.height);
    let eye: vec3f = vec3f(3.2, 2.3, 3.7);
    let fwd: vec3f = normalize(vec3f(0.25, 0.5, -0.15) - eye);
    let right: vec3f = normalize(vec3f(-fwd.z, 0.0, fwd.x));
    let up: vec3f = cross(right, fwd);
    let sx: f32 = ((x + 0.5) / wf - 0.5) * 2.0 * (wf / hf) * 0.42;
    let sy: f32 = (0.5 - (y + 0.5) / hf) * 2.0 * 0.42;
    let rd: vec3f = normalize(fwd + right * sx + up * sy);
    var t: f32 = 0.0;
    var hit: f32 = -1.0;
    for (var it: i32 = 0; it < 160; it++) {
        let d: f32 = still_sdf(eye + rd * t);
        if (d < 1.0e-4 * (1.0 + t)) {
            hit = t;
            break;
        }
        t += d;
        if (t > 30.0) { break; }
    }
    var col: vec3f = U.paper.xyz;
    if (hit > 0.0) {
        let q: vec3f = eye + rd * hit;
        var cn: vec3f = vec3f(0.0);
        var co: vec3f = vec3f(0.0);
        let umb: f32 = curvature(q, &cn, &co);
        let tone: f32 = 0.12 + 0.88 * max(0.0, dot(cn, normalize(vec3f(-0.5, 0.8, 0.45))));
        let px: f32 = (hit * 2.0 * 0.42) / hf;
        let half_w: f32 = 0.5 * U.a.y * px;
        let dark: f32 = 1.0 - tone;
        let second: f32 = dot(q, vec3f(0.6246950475544243, 0.4685212856658182, -0.6246950475544243));
        var s1: f32 = dot(q, co);
        var s2: f32 = second;
        if (umb > 0.5 && abs(cn.y) > 0.95) {
            s1 = dot(q, vec3f(0.9578262852211514, 0.0, 0.28734788556634544));
            s2 = dot(q, vec3f(-0.28734788556634544, 0.0, 0.9578262852211514));
        } else if (umb > 0.5) {
            s1 = q.y;
        }
        var ink: f32 = hatch_lines(s1, U.a.x, px, half_w * (0.4 + 1.6 * dark));
        if (U.layers > 1u && tone < 0.5) { ink = 1.0 - (1.0 - ink) * (1.0 - hatch_lines(s2, U.a.x, px, half_w * (0.4 + 2.0 * (0.5 - tone)))); }
        if (U.a.w > 0.0) {
            let nv: f32 = abs(dot(cn, rd));
            if (nv < U.a.w) { ink = max(ink, min(1.0, (U.a.w - nv) / (0.5 * U.a.w))); }
        }
        col = U.paper.xyz * (1.0 - ink) + U.ink.xyz * ink;
    }
    out8[i] = pack_rgb8(col);
}

//@pass style_hysteresis
// Hysteresis quantisation (raw/renderer/style_quantise.hpp), one dispatch a frame. pal: palette
// OKLab (palette mode); img: scene-linear RGBA; motion: (mx, my, expected previous distance, this
// frame's distance); prev and next: the state, (decision, persistence count, distance, 0), the
// previous frame's read and this frame's written. Writes the sRGB RGBA8 word a pixel.
@group(0) @binding(0) var<uniform> U: HystParams;
@group(0) @binding(1) var<storage, read> pal: array<vec4f>;
@group(0) @binding(2) var<storage, read> img: array<vec4f>;
@group(0) @binding(3) var<storage, read> motion: array<vec4f>;
@group(0) @binding(4) var<storage, read> prev: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> next: array<vec4f>;
@group(0) @binding(6) var<storage, read_write> out8: array<u32>;

fn cbrt_p(x: f32) -> f32 {
    return select(0.0, pow(x, 1.0 / 3.0), x > 0.0);
}
fn shoulder(v: f32) -> f32 {
    return 1.0 - exp(-max(0.0, v) * U.a.z);
}
fn to_oklab(c: vec3f) -> vec3f {
    let l: f32 = cbrt_p(0.4122214708 * c.x + 0.5363325363 * c.y + 0.0514459929 * c.z);
    let m: f32 = cbrt_p(0.2119034982 * c.x + 0.6806995451 * c.y + 0.1073969566 * c.z);
    let s: f32 = cbrt_p(0.0883024619 * c.x + 0.2817188376 * c.y + 0.6299787005 * c.z);
    return vec3f(0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s, 1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s, 0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s);
}
fn from_oklab(q: vec3f) -> vec3f {
    let l0: f32 = q.x + 0.3963377774 * q.y + 0.2158037573 * q.z;
    let m0: f32 = q.x - 0.1055613458 * q.y - 0.0638541728 * q.z;
    let s0: f32 = q.x - 0.0894841775 * q.y - 1.291485548 * q.z;
    let l: f32 = l0 * l0 * l0;
    let m: f32 = m0 * m0 * m0;
    let s: f32 = s0 * s0 * s0;
    return vec3f(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s, -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s);
}
fn plain_decision(lab: vec3f) -> i32 {
    if (U.mode == 0u) { return i32(clamp(floor(lab.x * U.a.x + 0.5), 0.0, U.a.x)); }
    var best: i32 = 0;
    var bd: f32 = 1.0e30;
    for (var k: u32 = 0u; k < U.palSize; k++) {
        let d: f32 = length(lab - pal[k].xyz);
        if (d < bd) {
            bd = d;
            best = i32(k);
        }
    }
    return best;
}
fn keep(lab: vec3f, p: i32, pl: i32) -> bool {
    if (U.mode == 0u) {
        let x: f32 = lab.x * U.a.x;
        return x >= f32(p) - 0.5 - U.a.y && x <= f32(p) + 0.5 + U.a.y;
    }
    return length(lab - pal[u32(p)].xyz) <= length(lab - pal[u32(pl)].xyz) + U.a.y;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let c: vec3f = img[i].xyz;
    let lab: vec3f = to_oklab(vec3f(shoulder(c.x), shoulder(c.y), shoulder(c.z)));
    let pl: i32 = plain_decision(lab);
    var d: i32 = pl;
    var cnt: f32 = 0.0;
    let mo: vec4f = motion[i];
    if (U.on > 0u && U.hasState > 0u) {
        var mx: f32 = 0.0;
        var my: f32 = 0.0;
        if (U.hasMotion > 0u) {
            mx = mo.x;
            my = mo.y;
        }
        let px: i32 = i32(floor(f32(x) + 0.5 - mx));
        let py: i32 = i32(floor(f32(y) + 0.5 - my));
        if (px >= 0 && py >= 0 && px < w && py < h) {
            let s: vec4f = prev[u32(py * w + px)];
            let expect: f32 = mo.z;
            if (abs(s.z - expect) <= U.a.w * expect) {
                let pv: i32 = i32(floor(s.x + 0.5));
                if (pv != pl && keep(lab, pv, pl)) {
                    let cc: f32 = s.y + 1.0;
                    if (cc < U.b.x) {
                        d = pv;
                        cnt = cc;
                    }
                }
            }
        }
    }
    next[i] = vec4f(f32(d), cnt, mo.w, 0.0);
    var rgb: vec3f = vec3f(0.0);
    if (U.mode == 0u) {
        let Lq: f32 = f32(d) / U.a.x;
        var sc: f32 = 0.0;
        if (lab.x > 1.0e-9) { sc = Lq / lab.x; }
        rgb = from_oklab(vec3f(Lq, lab.y * sc, lab.z * sc));
    } else {
        rgb = from_oklab(pal[u32(d)].xyz);
    }
    out8[i] = pack_rgb8(rgb);
}

//@pass style_palettelight
// Palette-locked lighting (raw/renderer/style_quantise.hpp). gb: four vec4f a pixel: (hit, class,
// detail, pool phase), (P, vis moon), (N, vis lamp), (vis window, 0, 0, 0). tbl: palette indices,
// (class * 2 + variant) * K + step for the four lit classes, then the void ramp, three window
// entries, two lamp entries, the night entry. ent: RGBA8 of every entry. Writes an index and RGBA8.
@group(0) @binding(0) var<uniform> U: PalettelightParams;
@group(0) @binding(1) var<storage, read> gb: array<vec4f>;
@group(0) @binding(2) var<storage, read> tbl: array<u32>;
@group(0) @binding(3) var<storage, read> ent: array<u32>;
@group(0) @binding(4) var<storage, read_write> outIdx: array<u32>;
@group(0) @binding(5) var<storage, read_write> out8: array<u32>;

fn bayer8(x0: i32, y0: i32) -> u32 {
    let x: u32 = u32(x0 & 7);
    let y: u32 = u32(y0 & 7);
    let v: u32 = x ^ y;
    return ((v & 1u) << 5u) | ((y & 1u) << 4u) | (((v >> 1u) & 1u) << 3u) | (((y >> 1u) & 1u) << 2u) | (((v >> 2u) & 1u) << 1u) | ((y >> 2u) & 1u);
}
fn thr(x: i32, y: i32) -> f32 {
    return (f32(bayer8(x, y)) + 0.5) / 64.0;
}
fn radial(la: vec4f, lb: vec4f, p: vec3f, n: vec3f, vis: f32) -> f32 {
    let v: vec3f = p - la.xyz;
    let r2: f32 = dot(v, v);
    let q: f32 = r2 / (la.w * la.w);
    if (q >= 1.0) { return 0.0; }
    let m: f32 = (lb.y * (1.0 - q) * (1.0 - q)) / (1.0 + r2 / (lb.x * lb.x));
    let tov: vec3f = la.xyz - p;
    let l: f32 = length(tov);
    var term: f32 = 1.0;
    if (l >= 1.0e-6) { term = sat1((dot(n, tov) / l + lb.z) / (1.0 + lb.z)); }
    return vis * m * term;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let ix: i32 = i32(i % U.width);
    let iy: i32 = i32(i / U.width);
    let b: u32 = i * 4u;
    let v0: vec4f = gb[b];
    let v1: vec4f = gb[b + 1u];
    let v2: vec4f = gb[b + 2u];
    let v3: vec4f = gb[b + 3u];
    let K: u32 = U.steps;
    let vs: u32 = U.voidSteps;
    var idx: u32 = tbl[8u * K + vs + 5u];
    let T1: f32 = thr(ix, iy);
    let T2: f32 = thr(ix + 11, iy + 7);
    let time: f32 = U.b.x;
    if (v0.x > 0.5) {
        let cls: u32 = u32(v0.y + 0.5);
        let voidBase: u32 = 8u * K;
        let winBase: u32 = voidBase + vs;
        let lampBase: u32 = winBase + 3u;
        if (cls == 4u) {
            let pos0: f32 = v0.w * f32(vs) + time * U.a.w * f32(vs);
            let pos: f32 = pos0 - floor(pos0 / f32(vs)) * f32(vs);
            let lo: u32 = u32(floor(pos));
            let f: f32 = pos - floor(pos);
            var st: u32 = lo;
            if (f > T1) { st = lo + 1u; }
            idx = tbl[voidBase + (st % vs)];
        } else if (cls == 5u) {
            let s: f32 = 1.4 + 0.6 * sin(time * 5.0 + v1.x * 3.0) + 0.4 * (v0.z - 0.5);
            let tone: f32 = clamp(s, 0.0, 2.0);
            let lo: u32 = min(1u, u32(floor(tone)));
            var st: u32 = lo;
            if (tone - f32(lo) > T1) { st = lo + 1u; }
            idx = tbl[winBase + st];
        } else if (cls == 6u) {
            var st: u32 = 0u;
            if (0.5 + 0.2 * sin(time * 7.0) > T1) { st = 1u; }
            idx = tbl[lampBase + st];
        } else {
            let p: vec3f = v1.xyz;
            let n: vec3f = v2.xyz;
            let nl: f32 = max(0.0, dot(n, U.b.yzw));
            let cool: f32 = U.a.y * (0.7 + 0.3 * (0.5 + 0.5 * n.y)) + U.a.z * nl * v1.w;
            let warm: f32 = radial(U.lampA, U.lampB, p, n, v2.w) + radial(U.g0a, U.g0b, p, n, v3.x) + radial(U.g1a, U.g1b, p, n, v3.x);
            let E: f32 = cool + warm;
            let tone: f32 = 1.0 - exp(-U.a.x * E * v0.z);
            let s: f32 = min(f32(K - 1u), max(0.0, tone * f32(K - 1u)));
            var wf: f32 = 0.0;
            if (E > 1.0e-9) { wf = warm / E; }
            let lo: u32 = min(K - 2u, u32(floor(s)));
            var st: u32 = lo;
            if (s - f32(lo) > T1) { st = lo + 1u; }
            var variant: u32 = 0u;
            if (wf >= 0.7) {
                variant = 1u;
            } else if (wf > 0.3) {
                if ((wf - 0.3) / 0.4 > T2) { variant = 1u; }
            }
            idx = tbl[(cls * 2u + variant) * K + st];
        }
    }
    outIdx[i] = idx;
    out8[i] = ent[idx];
}
