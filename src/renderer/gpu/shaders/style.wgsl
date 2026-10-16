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

// Palette dither: n palette entries; mode 0 none, 1 to 3 Bayer 2/4/8, 4 IGN, 5 blue noise, 6 to 8
// the mix modes over Bayer 4, Bayer 8 and blue noise.
struct DitherGpuParams {
    width: u32, height: u32, n: u32, mode: u32,
}

// Development adjacency. a: Dc, Db, dt, k; b: eta, r, beta, etaB; c: rB, gamma, H0, exposure;
// d: decay, the display gain.
struct AdjParams {
    width: u32, height: u32, s: u32, cw: u32,
    ch: u32, pad0: u32, pad1: u32, pad2: u32,
    a: vec4f,
    b: vec4f,
    c: vec4f,
    d: vec4f,
}

// crt-classic (the Studio's tube stage). a: bloom, halation, curvature, aberration; b: vignette.
// dir and radius are the box pass's axis (0 x, 1 y) and radius.
struct ClassicParams {
    width: u32, height: u32, cell: u32, phosphor: u32,
    flat_cell: u32, flat_even: u32, flat_odd: u32, q: u32,
    lw: u32, lh: u32, dir: u32, radius: u32,
    a: vec4f,
    b: vec4f,
}
// VHS. lh, ph, chh: half-widths of the luma, peaking and chroma taps (packed in that order).
// a: jitter, headSwitchLines, headSkew, tracking; b: lumaNoise, chromaNoise, peaking, delay;
// c: phaseNoise, dropouts x generations; the YIQ matrix and its inverse as rows.
struct VhsParams {
    n: u32, lines: u32, src_w: u32, src_h: u32,
    out_w: u32, out_h: u32, frame: u32, seed: u32,
    lh: u32, ph: u32, chh: u32, pad0: u32,
    a: vec4f,
    b: vec4f,
    c: vec4f,
    yiq0: vec4f,
    yiq1: vec4f,
    yiq2: vec4f,
    inv0: vec4f,
    inv1: vec4f,
    inv2: vec4f,
}
// Fibre-network watercolour. a: C, L, fibre width, md; b: spread, k0, k1, rate; c: floc, evap,
// dt, strength; paper: the paper colour and edgeDry.
struct FibreParams {
    width: u32, height: u32, per_cell: u32, pad0: u32,
    a: vec4f,
    b: vec4f,
    c: vec4f,
    paper: vec4f,
}

// The spectral film shader. a: 2^ev, um per pixel, weave x, weave y (output pixels); e0..e2 the
// exposure matrix rows; grain radii (um) and contrasts; halo strengths; the negative's and the
// print's curve constants; b: interimage, bleach, print base; the output matrix rows.
struct FilmParams {
    out_w: u32, out_h: u32, in_w: u32, in_h: u32,
    gw: u32, gh: u32, halo_q: u32, halo_r: u32,
    frame: u32, mtf_r: u32, mtf_dir: u32, pad0: u32,
    a: vec4f,
    e0: vec4f,
    e1: vec4f,
    e2: vec4f,
    grain_r: vec4f,
    grain_cs: vec4f,
    halo_s: vec4f,
    neg_dmin: vec4f,
    neg_span: vec4f,
    neg_k: vec4f,
    neg_x0: vec4f,
    b: vec4f,
    prt_dmin: vec4f,
    prt_span: vec4f,
    prt_k: vec4f,
    log_gain: vec4f,
    out0: vec4f,
    out1: vec4f,
    out2: vec4f,
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

// RGBA8 words: one channel as an integer, and three channels packed with alpha 255.
fn ch8(v: u32, c: u32) -> i32 {
    return i32((v >> (8u * c)) & 255u);
}
fn pack8(r: i32, g: i32, b: i32) -> u32 {
    return u32(clamp(r, 0, 255)) | (u32(clamp(g, 0, 255)) << 8u) | (u32(clamp(b, 0, 255)) << 16u) | (255u << 24u);
}
// A Box-Muller normal from three integer hashes (gauss of web/shaders/vhs/vhs.mjs).
fn gauss3(a: u32, b: u32, c: u32) -> f32 {
    let u1: f32 = max(1.0e-7, hash3(a, b, c));
    let u2: f32 = hash3(a, b, c + 7919u);
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
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

//@pass style_dither
// Palette quantisation and dither (raw/renderer/style_dither.hpp) in double-single arithmetic: a
// value is a vec2f (hi, lo), and the error-free transforms (Knuth two-sum, Dekker split and
// two-product) are written without fma, so the decisions can equal the float64 reference's.
// K: the constants as (hi, lo) pairs: the sRGB byte to linear table (256), the two OKLab matrices
// (9 each), epsilon, the pair penalty, the three IGN constants, brightness, strength. pal: the
// palette's OKLab as pairs, 3 an entry; palrgb: its RGBA8. bn: blue-noise ranks. src: the RGBA8
// plate. Writes a palette index and the entry's RGBA8 a pixel.
@group(0) @binding(0) var<uniform> U: DitherGpuParams;
@group(0) @binding(1) var<storage, read> K: array<vec2f>;
@group(0) @binding(2) var<storage, read> pal: array<vec2f>;
@group(0) @binding(3) var<storage, read> palrgb: array<u32>;
@group(0) @binding(4) var<storage, read> bn: array<u32>;
@group(0) @binding(5) var<storage, read> src: array<u32>;
@group(0) @binding(6) var<storage, read_write> idx: array<u32>;
@group(0) @binding(7) var<storage, read_write> out8: array<u32>;

fn two_sum(a: f32, b: f32) -> vec2f {
    let s: f32 = a + b;
    let bb: f32 = s - a;
    return vec2f(s, (a - (s - bb)) + (b - bb));
}
fn quick_two_sum(a: f32, b: f32) -> vec2f {
    let s: f32 = a + b;
    return vec2f(s, b - (s - a));
}
fn split_f(a: f32) -> vec2f {
    let c: f32 = 4097.0 * a;
    let big: f32 = c - a;
    let hi: f32 = c - big;
    return vec2f(hi, a - hi);
}
fn two_prod(a: f32, b: f32) -> vec2f {
    let p: f32 = a * b;
    let sa: vec2f = split_f(a);
    let sb: vec2f = split_f(b);
    let e: f32 = ((sa.x * sb.x - p) + sa.x * sb.y + sa.y * sb.x) + sa.y * sb.y;
    return vec2f(p, e);
}
fn ds(a: f32) -> vec2f {
    return vec2f(a, 0.0);
}
fn ds_add(a: vec2f, b: vec2f) -> vec2f {
    var s: vec2f = two_sum(a.x, b.x);
    let t: vec2f = two_sum(a.y, b.y);
    s.y = s.y + t.x;
    s = quick_two_sum(s.x, s.y);
    s.y = s.y + t.y;
    return quick_two_sum(s.x, s.y);
}
fn ds_sub(a: vec2f, b: vec2f) -> vec2f {
    return ds_add(a, -b);
}
fn ds_mul(a: vec2f, b: vec2f) -> vec2f {
    var p: vec2f = two_prod(a.x, b.x);
    p.y = p.y + (a.x * b.y + a.y * b.x);
    return quick_two_sum(p.x, p.y);
}
fn ds_div(a: vec2f, b: vec2f) -> vec2f {
    let q1: f32 = a.x / b.x;
    var r: vec2f = ds_sub(a, ds_mul(b, ds(q1)));
    let q2: f32 = r.x / b.x;
    r = ds_sub(r, ds_mul(b, ds(q2)));
    let q3: f32 = r.x / b.x;
    return ds_add(quick_two_sum(q1, q2), ds(q3));
}
fn ds_lt(a: vec2f, b: vec2f) -> bool {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
}
fn ds_gt(a: vec2f, b: vec2f) -> bool {
    return ds_lt(b, a);
}
fn ds_le(a: vec2f, b: vec2f) -> bool {
    return !ds_lt(b, a);
}
fn ds_abs(a: vec2f) -> vec2f {
    if (a.x < 0.0 || (a.x == 0.0 && a.y < 0.0)) { return -a; }
    return a;
}
fn ds_min(a: vec2f, b: vec2f) -> vec2f {
    if (ds_lt(b, a)) { return b; }
    return a;
}
fn ds_clamp01(a: vec2f) -> vec2f {
    if (ds_lt(a, ds(0.0))) { return ds(0.0); }
    if (ds_gt(a, ds(1.0))) { return ds(1.0); }
    return a;
}
fn ds_floor(a: vec2f) -> vec2f {
    let f: f32 = floor(a.x);
    if (f == a.x) { return quick_two_sum(f, floor(a.y)); }
    return ds(f);
}
// Cube root: an f32 estimate, then one Newton step in double-single.
fn ds_cbrt(x: vec2f) -> vec2f {
    if (x.x == 0.0) { return ds(0.0); }
    let y0: f32 = sign(x.x) * pow(abs(x.x), 1.0 / 3.0);
    let yy: vec2f = ds_mul(ds(y0), ds(y0));
    let y3: vec2f = ds_mul(yy, ds(y0));
    return ds_add(ds(y0), ds_div(ds_sub(x, y3), ds_mul(ds(3.0), yy)));
}
fn palc(i: u32, c: u32) -> vec2f {
    return pal[3u * i + c];
}
fn mat3ds(m: u32, x: vec2f, y: vec2f, z: vec2f, row: u32) -> vec2f {
    return ds_add(ds_add(ds_mul(K[m + 3u * row], x), ds_mul(K[m + 3u * row + 1u], y)), ds_mul(K[m + 3u * row + 2u], z));
}
fn lab3(r: vec2f, g: vec2f, b: vec2f, L: ptr<function, vec2f>, A: ptr<function, vec2f>, B: ptr<function, vec2f>) {
    let l: vec2f = ds_cbrt(mat3ds(256u, r, g, b, 0u));
    let m: vec2f = ds_cbrt(mat3ds(256u, r, g, b, 1u));
    let s: vec2f = ds_cbrt(mat3ds(256u, r, g, b, 2u));
    *L = mat3ds(265u, l, m, s, 0u);
    *A = mat3ds(265u, l, m, s, 1u);
    *B = mat3ds(265u, l, m, s, 2u);
}
fn dist2(L: vec2f, a: vec2f, b: vec2f, i: u32) -> vec2f {
    let dL: vec2f = ds_sub(L, palc(i, 0u));
    let da: vec2f = ds_sub(a, palc(i, 1u));
    let db: vec2f = ds_sub(b, palc(i, 2u));
    return ds_add(ds_add(ds_mul(dL, dL), ds_mul(da, da)), ds_mul(db, db));
}
fn nearest(L: vec2f, a: vec2f, b: vec2f) -> u32 {
    var best: u32 = 0u;
    var bestD: vec2f = dist2(L, a, b, 0u);
    for (var i: u32 = 1u; i < U.n; i++) {
        let d: vec2f = dist2(L, a, b, i);
        if (ds_lt(d, bestD)) {
            bestD = d;
            best = i;
        }
    }
    return best;
}
fn far_side(L: vec2f, a: vec2f, b: vec2f, i: u32) -> i32 {
    let dL: vec2f = ds_sub(L, palc(i, 0u));
    let da: vec2f = ds_sub(a, palc(i, 1u));
    let db: vec2f = ds_sub(b, palc(i, 2u));
    let d2: vec2f = ds_add(ds_add(ds_mul(dL, dL), ds_mul(da, da)), ds_mul(db, db));
    var best: i32 = -1;
    var bestS: vec2f = vec2f(0.0);
    var have: bool = false;
    for (var j: u32 = 0u; j < U.n; j++) {
        if (j == i) { continue; }
        let sL: vec2f = ds_sub(palc(j, 0u), palc(i, 0u));
        let sa: vec2f = ds_sub(palc(j, 1u), palc(i, 1u));
        let sb: vec2f = ds_sub(palc(j, 2u), palc(i, 2u));
        let len2: vec2f = ds_add(ds_add(ds_mul(sL, sL), ds_mul(sa, sa)), ds_mul(sb, sb));
        if (ds_lt(len2, K[274u])) { continue; }
        let u: vec2f = ds_div(ds_add(ds_add(ds_mul(dL, sL), ds_mul(da, sa)), ds_mul(db, sb)), len2);
        if (ds_le(u, ds(0.0))) { continue; }
        let uc: vec2f = ds_min(u, ds(1.0));
        let pen: vec2f = ds_mul(ds_mul(K[275u], len2), ds_add(ds_abs(ds_sub(uc, ds(0.5))), ds(0.5)));
        let score: vec2f = ds_add(ds_sub(d2, ds_mul(ds_mul(uc, uc), len2)), pen);
        if (!have || ds_lt(score, bestS)) {
            bestS = score;
            best = i32(j);
            have = true;
        }
    }
    return best;
}
fn bayer_rank(x: u32, y: u32, n: u32) -> u32 {
    if (n == 2u) { return (((x ^ y) & 1u) << 1u) | (y & 1u); }
    if (n == 4u) {
        let z: u32 = (x ^ y) & 3u;
        return ((z & 1u) << 3u) | ((y & 1u) << 2u) | (z & 2u) | ((y & 2u) >> 1u);
    }
    let xx: u32 = x & 7u;
    let yy: u32 = y & 7u;
    let v: u32 = xx ^ yy;
    return ((v & 1u) << 5u) | ((yy & 1u) << 4u) | ((v & 2u) << 2u) | ((yy & 2u) << 1u) | ((v & 4u) >> 1u) | ((yy & 4u) >> 2u);
}
// The threshold of a pixel: mode 1, 2, 3 Bayer 2, 4, 8; 4 IGN; 5 blue noise.
fn threshold(x: u32, y: u32, mode: u32) -> vec2f {
    if (mode == 1u) { return ds((f32(bayer_rank(x, y, 2u)) + 0.5) / 4.0); }
    if (mode == 2u) { return ds((f32(bayer_rank(x, y, 4u)) + 0.5) / 16.0); }
    if (mode == 3u) { return ds((f32(bayer_rank(x, y, 8u)) + 0.5) / 64.0); }
    if (mode == 4u) {
        let f: vec2f = ds_add(ds_mul(K[276u], ds(f32(x))), ds_mul(K[277u], ds(f32(y))));
        let v: vec2f = ds_mul(K[278u], ds_sub(f, ds_floor(f)));
        return ds_sub(v, ds_floor(v));
    }
    return ds((f32(bn[(y % 64u) * 64u + x % 64u]) + 0.5) / 4096.0);
}
fn pal_lin(k: u32, c: u32) -> vec2f {
    return K[(palrgb[k] >> (8u * c)) & 255u];
}
fn before(a: u32, b: u32) -> bool {
    let La: vec2f = palc(a, 0u);
    let Lb: vec2f = palc(b, 0u);
    if (ds_lt(La, Lb)) { return true; }
    if (ds_lt(Lb, La)) { return false; }
    return a < b;
}
fn mix_pick(r: vec2f, g: vec2f, b: vec2f, th: vec2f) -> u32 {
    var plan: array<u32, 16>;
    var sr: vec2f = ds(0.0);
    var sg: vec2f = ds(0.0);
    var sb: vec2f = ds(0.0);
    var TL: vec2f = vec2f(0.0);
    var TA: vec2f = vec2f(0.0);
    var TB: vec2f = vec2f(0.0);
    var CL: vec2f = vec2f(0.0);
    var CA: vec2f = vec2f(0.0);
    var CB: vec2f = vec2f(0.0);
    for (var i: u32 = 0u; i < 16u; i++) {
        let w: vec2f = ds(f32(i + 1u));
        lab3(ds_mul(w, r), ds_mul(w, g), ds_mul(w, b), &TL, &TA, &TB);
        var k: u32 = 0u;
        var bd: vec2f = vec2f(0.0);
        var have: bool = false;
        for (var j: u32 = 0u; j < U.n; j++) {
            lab3(ds_add(sr, pal_lin(j, 0u)), ds_add(sg, pal_lin(j, 1u)), ds_add(sb, pal_lin(j, 2u)), &CL, &CA, &CB);
            let dL: vec2f = ds_sub(CL, TL);
            let dA: vec2f = ds_sub(CA, TA);
            let dB: vec2f = ds_sub(CB, TB);
            let d: vec2f = ds_add(ds_add(ds_mul(dL, dL), ds_mul(dA, dA)), ds_mul(dB, dB));
            if (!have || ds_lt(d, bd)) {
                bd = d;
                k = j;
                have = true;
            }
        }
        var q: u32 = i;
        for (var st: u32 = 0u; st < 16u; st++) {
            if (q == 0u || !before(k, plan[q - 1u])) { break; }
            plan[q] = plan[q - 1u];
            q = q - 1u;
        }
        plan[q] = k;
        sr = ds_add(sr, pal_lin(k, 0u));
        sg = ds_add(sg, pal_lin(k, 1u));
        sb = ds_add(sb, pal_lin(k, 2u));
    }
    let f: vec2f = ds_floor(ds_mul(th, ds(16.0)));
    let pi: i32 = min(15, i32(f.x) + i32(f.y));
    return plan[u32(max(pi, 0))];
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x;
    if (p >= U.width * U.height) { return; }
    let x: u32 = p % U.width;
    let y: u32 = p / U.width;
    let mode: u32 = U.mode;
    let c: u32 = src[p];
    let r: vec2f = K[c & 255u];
    let g: vec2f = K[(c >> 8u) & 255u];
    let b: vec2f = K[(c >> 16u) & 255u];
    var L0: vec2f = vec2f(0.0);
    var A: vec2f = vec2f(0.0);
    var Bv: vec2f = vec2f(0.0);
    lab3(r, g, b, &L0, &A, &Bv);
    let L: vec2f = ds_clamp01(ds_add(L0, K[279u]));
    var i: u32 = nearest(L, A, Bv);
    let strength: vec2f = K[280u];
    if (mode >= 6u) {
        if (U.n >= 2u && ds_gt(strength, ds(0.0))) {
            var srcMode: u32 = 5u;
            if (mode == 6u) { srcMode = 2u; }
            if (mode == 7u) { srcMode = 3u; }
            let th: vec2f = ds_add(ds(0.5), ds_mul(ds_sub(threshold(x, y, srcMode), ds(0.5)), strength));
            i = mix_pick(r, g, b, th);
        }
        idx[p] = i;
        out8[p] = palrgb[i];
        return;
    }
    if (mode != 0u && U.n >= 2u && ds_gt(strength, ds(0.0))) {
        let j: i32 = far_side(L, A, Bv, i);
        if (j >= 0) {
            let ju: u32 = u32(j);
            let sL: vec2f = ds_sub(palc(ju, 0u), palc(i, 0u));
            let sa: vec2f = ds_sub(palc(ju, 1u), palc(i, 1u));
            let sb: vec2f = ds_sub(palc(ju, 2u), palc(i, 2u));
            let len2: vec2f = ds_add(ds_add(ds_mul(sL, sL), ds_mul(sa, sa)), ds_mul(sb, sb));
            if (!ds_lt(len2, K[274u])) {
                let num: vec2f = ds_add(ds_add(ds_mul(ds_sub(L, palc(i, 0u)), sL), ds_mul(ds_sub(A, palc(i, 1u)), sa)), ds_mul(ds_sub(Bv, palc(i, 2u)), sb));
                let f: vec2f = ds_clamp01(ds_div(num, len2));
                let th: vec2f = ds_add(ds(0.5), ds_mul(ds_sub(threshold(x, y, mode), ds(0.5)), strength));
                if (ds_gt(f, th)) { i = ju; }
            }
        }
    }
    idx[p] = i;
    out8[p] = palrgb[i];
}

//@pass style_adj_prep
// Development adjacency (raw/renderer/style_adjacency.hpp), first pass: the developable fraction
// a_k of each layer from the exposure, and D = 0.
@group(0) @binding(0) var<uniform> U: AdjParams;
@group(0) @binding(1) var<storage, read> scene: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> A: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> D: array<vec4f>;

fn developable(H: f32) -> f32 {
    if (H <= 0.0) { return 0.0; }
    let q: f32 = pow(U.c.z / H, U.c.y);
    return 1.0 / (1.0 + q);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let c: vec3f = scene[i].xyz * U.c.w;
    A[i] = vec4f(developable(c.x), developable(c.y), developable(c.z), 0.0);
    D[i] = vec4f(0.0);
}

//@pass style_adj_develop
// One Euler step of development at full resolution, reading the developer and bromide fields F
// (coarse, bilinear). D.w receives the step's development summed over the layers.
@group(0) @binding(0) var<uniform> U: AdjParams;
@group(0) @binding(1) var<storage, read> A: array<vec4f>;
@group(0) @binding(2) var<storage, read> F: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> D: array<vec4f>;

fn coarse_at(x: i32, y: i32) -> vec2f {
    let s: f32 = f32(U.s);
    let cw: i32 = i32(U.cw);
    let ch: i32 = i32(U.ch);
    let u: f32 = (f32(x) + 0.5) / s - 0.5;
    let v: f32 = (f32(y) + 0.5) / s - 0.5;
    let x0: i32 = clamp(i32(floor(u)), 0, cw - 1);
    let y0: i32 = clamp(i32(floor(v)), 0, ch - 1);
    let x1: i32 = min(cw - 1, x0 + 1);
    let y1: i32 = min(ch - 1, y0 + 1);
    let fx: f32 = clamp(u - f32(x0), 0.0, 1.0);
    let fy: f32 = clamp(v - f32(y0), 0.0, 1.0);
    let a: vec2f = F[u32(y0 * cw + x0)].xy;
    let b: vec2f = F[u32(y0 * cw + x1)].xy;
    let c: vec2f = F[u32(y1 * cw + x0)].xy;
    let d: vec2f = F[u32(y1 * cw + x1)].xy;
    return (a * (1.0 - fx) + b * fx) * (1.0 - fy) + (c * (1.0 - fx) + d * fx) * fy;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let cb: vec2f = coarse_at(x, y);
    let rate: f32 = (U.a.w * cb.x) / (1.0 + U.b.z * cb.y);
    var d: vec4f = D[i];
    let a: vec4f = A[i];
    let dt: f32 = U.a.z;
    let dr: f32 = rate * (a.x - d.x) * dt;
    d.x = d.x + dr;
    let dg: f32 = rate * (a.y - d.y) * dt;
    d.y = d.y + dg;
    let db: f32 = rate * (a.z - d.z) * dt;
    d.z = d.z + db;
    d.w = dr + dg + db;
    D[i] = d;
}

//@pass style_adj_chem
// One Euler step of the chemistry on the coarse grid: diffusion with reflecting borders, use by
// development, replenishment by the bath. Reads F, writes G.
@group(0) @binding(0) var<uniform> U: AdjParams;
@group(0) @binding(1) var<storage, read> D: array<vec4f>;
@group(0) @binding(2) var<storage, read> F: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> G: array<vec4f>;

fn at(i: i32, j: i32) -> vec2f {
    let cw: i32 = i32(U.cw);
    let ch: i32 = i32(U.ch);
    return F[u32(clamp(j, 0, ch - 1) * cw + clamp(i, 0, cw - 1))].xy;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= U.cw * U.ch) { return; }
    let i: i32 = i32(gid.x % U.cw);
    let j: i32 = i32(gid.x / U.cw);
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let s: i32 = i32(U.s);
    var used: f32 = 0.0;
    var cnt: f32 = 0.0;
    for (var yy: i32 = j * s; yy < min(h, j * s + s); yy++) {
        for (var xx: i32 = i * s; xx < min(w, i * s + s); xx++) {
            used = used + D[u32(yy * w + xx)].w;
            cnt = cnt + 1.0;
        }
    }
    used = used / cnt;
    let f0: vec2f = at(i, j);
    let dt: f32 = U.a.z;
    let l: vec2f = at(i - 1, j) + at(i + 1, j) + at(i, j - 1) + at(i, j + 1) - 4.0 * f0;
    let c: f32 = f0.x + U.a.x * l.x * dt - U.b.x * used + U.b.y * (1.0 - f0.x) * dt;
    let b: f32 = f0.y + U.a.y * l.y * dt + U.b.w * used - U.c.x * f0.y * dt;
    G[gid.x] = vec4f(c, b, 0.0, 0.0);
}

//@pass style_adj_out
// The output: scene colour times (D + eps) / (A (1 - decay) + eps), lab-encoded to RGBA8.
@group(0) @binding(0) var<uniform> U: AdjParams;
@group(0) @binding(1) var<storage, read> scene: array<vec4f>;
@group(0) @binding(2) var<storage, read> A: array<vec4f>;
@group(0) @binding(3) var<storage, read> D: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> out8: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let free: f32 = 1.0 - U.d.x;
    let eps: f32 = 1.0e-6;
    let o: vec3f = scene[i].xyz * ((D[i].xyz + vec3f(eps)) / (A[i].xyz * free + vec3f(eps)));
    out8[i] = lab_pack(o, U.d.y);
}

//@pass style_crt_phosphor
// crt-classic (raw/renderer/style_crt_classic.hpp), first pass: beam scanlines and the phosphor
// mask in 8.8 fixed point, with the reference's own tables.
@group(0) @binding(0) var<uniform> U: ClassicParams;
@group(0) @binding(1) var<storage, read> beam: array<i32>;
@group(0) @binding(2) var<storage, read> mt: array<i32>;
@group(0) @binding(3) var<storage, read> src: array<u32>;
@group(0) @binding(4) var<storage, read_write> dst: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let v: u32 = src[i];
    if (U.phosphor == 0u) {
        dst[i] = v;
        return;
    }
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let cell: i32 = i32(U.cell);
    let row: i32 = y % cell;
    let ry: i32 = (y % 6) * 3;
    var flatf: i32 = 0;
    if (U.flat_cell != 0u) {
        flatf = select(i32(U.flat_odd), i32(U.flat_even), (y & 1) == 0);
    }
    let r: i32 = ch8(v, 0u);
    let g: i32 = ch8(v, 1u);
    let b: i32 = ch8(v, 2u);
    let p: u32 = u32((ry + x % 3) * 3);
    let k: i32 = max(max(r, g), b) >> 4u;
    var f: i32 = beam[u32(k * cell + row)];
    if (flatf > 0) { f = flatf; }
    dst[i] = pack8((r * f * mt[p] + 32768) >> 16u, (g * f * mt[p + 1u] + 32768) >> 16u, (b * f * mt[p + 2u] + 32768) >> 16u);
}

//@pass style_crt_down
// crt-classic: box-downsample into linear light at 1/q (f32 sums in the reference's order), then
// the thresholded bright field.
@group(0) @binding(0) var<uniform> U: ClassicParams;
@group(0) @binding(1) var<storage, read> toLin: array<f32>;
@group(0) @binding(2) var<storage, read> src: array<u32>;
@group(0) @binding(3) var<storage, read_write> lin: array<f32>;
@group(0) @binding(4) var<storage, read_write> bright: array<f32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.lw * U.lh) { return; }
    let lx: i32 = i32(i % U.lw);
    let ly: i32 = i32(i / U.lw);
    let q: i32 = i32(U.q);
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    var s: vec3f = vec3f(0.0);
    for (var y: i32 = ly * q; y < min(h, ly * q + q); y++) {
        for (var x: i32 = lx * q; x < min(w, lx * q + q); x++) {
            let v: u32 = src[u32(y * w + x)];
            s = s + vec3f(toLin[u32(ch8(v, 0u))], toLin[u32(ch8(v, 1u))], toLin[u32(ch8(v, 2u))]);
        }
    }
    let n: f32 = 1.0 / f32(min(q, h - ly * q) * min(q, w - lx * q));
    let c: vec3f = s * n;
    let t: f32 = clamp((0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z - 0.18) / 0.3, 0.0, 1.0);
    let kk: f32 = t * t * (3.0 - 2.0 * t);
    let o: u32 = i * 3u;
    lin[o] = c.x;
    lin[o + 1u] = c.y;
    lin[o + 2u] = c.z;
    bright[o] = c.x * kk;
    bright[o + 1u] = c.y * kk;
    bright[o + 2u] = c.z * kk;
}

//@pass style_crt_box
// crt-classic: one box pass along x (dir 0) or y (dir 1) with clamped ends, as boxLine.
@group(0) @binding(0) var<uniform> U: ClassicParams;
@group(0) @binding(1) var<storage, read> a: array<f32>;
@group(0) @binding(2) var<storage, read_write> o: array<f32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.lw * U.lh) { return; }
    let lw: i32 = i32(U.lw);
    let lh: i32 = i32(U.lh);
    let x: i32 = i32(i % U.lw);
    let y: i32 = i32(i / U.lw);
    let r: i32 = i32(U.radius);
    let inv: f32 = 1.0 / f32(2 * r + 1);
    for (var c: i32 = 0; c < 3; c++) {
        var s: f32 = 0.0;
        for (var k: i32 = -r; k <= r; k++) {
            var xx: i32 = x;
            var yy: i32 = y;
            if (U.dir != 0u) { yy = clamp(y + k, 0, lh - 1); } else { xx = clamp(x + k, 0, lw - 1); }
            s = s + a[u32((yy * lw + xx) * 3 + c)];
        }
        o[u32((y * lw + x) * 3 + c)] = s * inv;
    }
}

//@pass style_crt_add
// crt-classic: glow = bright bloom 0.9 + lin halation 0.3, added back in linear light through a
// bilinear upsample.
@group(0) @binding(0) var<uniform> U: ClassicParams;
@group(0) @binding(1) var<storage, read> toLin: array<f32>;
@group(0) @binding(2) var<storage, read> toSrgb: array<u32>;
@group(0) @binding(3) var<storage, read> bl: array<f32>;
@group(0) @binding(4) var<storage, read> ha: array<f32>;
@group(0) @binding(5) var<storage, read> src: array<u32>;
@group(0) @binding(6) var<storage, read_write> dst: array<u32>;

fn glow(i: i32) -> vec3f {
    let o: u32 = u32(i * 3);
    var g: vec3f = vec3f(0.0);
    if (U.a.x > 0.0) { g = g + vec3f(bl[o], bl[o + 1u], bl[o + 2u]) * U.a.x * 0.9; }
    if (U.a.y > 0.0) { g = g + vec3f(ha[o], ha[o + 1u], ha[o + 2u]) * U.a.y * 0.3; }
    return g;
}
fn to_srgb(l: f32) -> i32 {
    if (l >= 1.0) { return i32(toSrgb[4096]); }
    if (l <= 0.0) { return i32(toSrgb[0]); }
    return i32(toSrgb[u32(l * 4096.0 + 0.5)]);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let v: u32 = src[i];
    if (U.a.x <= 0.0 && U.a.y <= 0.0) {
        dst[i] = v;
        return;
    }
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let q: f32 = f32(U.q);
    let lw: i32 = i32(U.lw);
    let lh: i32 = i32(U.lh);
    let fx: f32 = clamp((f32(x) + 0.5) / q - 0.5, 0.0, f32(lw - 1));
    let x0: i32 = i32(fx);
    let x1: i32 = min(lw - 1, x0 + 1);
    let tx: f32 = fx - f32(x0);
    let fy: f32 = clamp((f32(y) + 0.5) / q - 0.5, 0.0, f32(lh - 1));
    let y0: i32 = i32(fy);
    let y1: i32 = min(lh - 1, y0 + 1);
    let ty: f32 = fy - f32(y0);
    let g: vec3f = glow(y0 * lw + x0) * (1.0 - tx) * (1.0 - ty) + glow(y0 * lw + x1) * tx * (1.0 - ty) + glow(y1 * lw + x0) * (1.0 - tx) * ty + glow(y1 * lw + x1) * tx * ty;
    if (g.x + g.y + g.z < 0.0015) {
        dst[i] = v;
        return;
    }
    dst[i] = pack8(to_srgb(toLin[u32(ch8(v, 0u))] + g.x), to_srgb(toLin[u32(ch8(v, 1u))] + g.y), to_srgb(toLin[u32(ch8(v, 2u))] + g.z));
}

//@pass style_crt_tube
// crt-classic: barrel warp with bilinear 8.8 taps, a feathered rounded bezel, radial colour
// separation and the vignette.
@group(0) @binding(0) var<uniform> U: ClassicParams;
@group(0) @binding(1) var<storage, read> src: array<u32>;
@group(0) @binding(2) var<storage, read_write> dst: array<u32>;

fn tap(su: f32, sv: f32, c: u32) -> i32 {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let sx: f32 = clamp((su + 1.0) * f32(w - 1) * 0.5, 0.0, f32(w - 1));
    let sy: f32 = clamp((sv + 1.0) * f32(h - 1) * 0.5, 0.0, f32(h - 1));
    let x0: i32 = i32(sx);
    let y0: i32 = i32(sy);
    let tx: i32 = i32((sx - f32(x0)) * 256.0);
    let ty: i32 = i32((sy - f32(y0)) * 256.0);
    let dx: i32 = select(0, 1, x0 + 1 < w);
    let dy: i32 = select(0, w, y0 + 1 < h);
    let p: i32 = y0 * w + x0;
    let top: i32 = ch8(src[u32(p)], c) * (256 - tx) + ch8(src[u32(p + dx)], c) * tx;
    let bot: i32 = ch8(src[u32(p + dy)], c) * (256 - tx) + ch8(src[u32(p + dy + dx)], c) * tx;
    return (top * (256 - ty) + bot * ty) >> 16u;
}
fn bezel_alpha(asu: f32, asv: f32, rc: f32, feather: f32) -> f32 {
    if (rc <= 0.0) { return select(0.0, 1.0, asu <= 1.0 && asv <= 1.0); }
    let qx: f32 = asu - (1.0 - rc);
    let qy: f32 = asv - (1.0 - rc);
    let d: f32 = length(vec2f(max(qx, 0.0), max(qy, 0.0))) + min(max(qx, qy), 0.0) - rc;
    return clamp(-d / feather, 0.0, 1.0);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let curv: f32 = U.a.z;
    let ab: f32 = U.a.w;
    let vig: f32 = U.b.x;
    let W2: f32 = f32((w - 1) * (w - 1));
    let H2: f32 = f32((h - 1) * (h - 1));
    let nv: f32 = 1.0 / (W2 + H2);
    let u: f32 = (f32(x) / f32(w - 1)) * 2.0 - 1.0;
    let v: f32 = (f32(y) / f32(h - 1)) * 2.0 - 1.0;
    let VX: f32 = u * u * W2 * nv;
    let vy: f32 = v * v * H2 * nv;
    let vg: f32 = vig * 256.0;
    if (curv <= 0.0 && ab <= 0.0) {
        if (vig <= 0.0) {
            dst[i] = src[i];
            return;
        }
        let f: i32 = i32(256.0 - vg * (VX + vy));
        let s: u32 = src[i];
        dst[i] = pack8((ch8(s, 0u) * f) >> 8u, (ch8(s, 1u) * f) >> 8u, (ch8(s, 2u) * f) >> 8u);
        return;
    }
    let k: f32 = curv * 0.35;
    let dab: f32 = ab * 0.012;
    var rc: f32 = 0.0;
    if (curv > 0.0) { rc = 0.02 + 0.08 * min(1.0, curv); }
    let feather: f32 = 1.5 / (f32(w + h) / 4.0);
    let safe: f32 = 1.0 - rc - feather * 2.0;
    let fr: f32 = 1.0 - dab;
    let fb: f32 = 1.0 + dab;
    let ff: f32 = 1.0 + k * (u * u + v * v);
    let su: f32 = u * ff;
    let sv: f32 = v * ff;
    var alpha: f32 = 1.0;
    if (!(abs(su) <= safe && abs(sv) <= safe)) { alpha = bezel_alpha(abs(su), abs(sv), rc, feather); }
    if (alpha <= 0.0) {
        dst[i] = pack8(0, 0, 0);
        return;
    }
    let vf: i32 = i32(alpha * (256.0 - vg * (VX + vy)));
    dst[i] = pack8((tap(su * fr, sv * fr, 0u) * vf) >> 8u, (tap(su, sv, 1u) * vf) >> 8u, (tap(su * fb, sv * fb, 2u) * vf) >> 8u);
}

//@pass style_vhs_sample
// VHS (raw/renderer/style_vhs.hpp), pass 1, the transport: each line sampled at 4 fsc with its
// time-base displacement, as Y'IQ.
@group(0) @binding(0) var<uniform> U: VhsParams;
@group(0) @binding(1) var<storage, read> src: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> sm: array<vec4f>;

fn tracking_band(y: i32, f: f32) -> f32 {
    let nl: f32 = f32(U.lines);
    let c: f32 = ((f * 3.7) % (nl + 40.0)) - 20.0;
    let d: f32 = abs(f32(y) - c) / 6.0;
    return U.a.w * max(0.0, 1.0 - d * d);
}
fn line_shift(y: i32, f: u32) -> f32 {
    let s: u32 = U.seed;
    var d: f32 = U.a.x * (0.6 * sin(f32(y) * 0.031 + f32(f) * 0.7 + f32(s)) + 0.4 * gauss3(u32(y), f, 11u + s));
    let from_bottom: i32 = i32(U.lines) - 1 - y;
    let hs: f32 = U.a.y;
    if (f32(from_bottom) < hs) {
        let t: f32 = (hs - f32(from_bottom)) / hs;
        d = d + U.a.z * t * t;
    }
    if (U.a.w > 0.0) { d = d + tracking_band(y, f32(f)) * 12.0 * gauss3(u32(y), f, 23u + s); }
    return d;
}
fn held(y: i32, k: i32) -> vec3f {
    let n: i32 = i32(U.n);
    let kk: i32 = clamp(k, 0, n - 1);
    let sw: i32 = i32(U.src_w);
    let x: i32 = clamp(i32(floor((f32(kk) + 0.5) * f32(sw) / f32(n))), 0, sw - 1);
    let c: vec3f = src[u32(y * sw + x)].xyz;
    return vec3f(dot(U.yiq0.xyz, c), dot(U.yiq1.xyz, c), dot(U.yiq2.xyz, c));
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.n * U.lines) { return; }
    let k: i32 = i32(i % U.n);
    let y: i32 = i32(i / U.n);
    let kf: f32 = f32(k) - line_shift(y, U.frame);
    let k0: f32 = floor(kf);
    let t: f32 = kf - k0;
    sm[i] = vec4f(held(y, i32(k0)) * (1.0 - t) + held(y, i32(k0) + 1) * t, 0.0);
}

//@pass style_vhs_tape
// VHS, pass 2, the tape: luma through the FM channel (bandwidth, emphasis ringing, rising noise),
// chroma through colour-under (narrow bandwidth, delay, streaky noise, phase wobble).
@group(0) @binding(0) var<uniform> U: VhsParams;
@group(0) @binding(1) var<storage, read> taps: array<f32>;
@group(0) @binding(2) var<storage, read> sm: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> tp: array<vec4f>;

fn tracking_band(y: i32, f: f32) -> f32 {
    let nl: f32 = f32(U.lines);
    let c: f32 = ((f * 3.7) % (nl + 40.0)) - 20.0;
    let d: f32 = abs(f32(y) - c) / 6.0;
    return U.a.w * max(0.0, 1.0 - d * d);
}
fn at(y: i32, k: f32) -> vec3f {
    let n: i32 = i32(U.n);
    let kf: f32 = clamp(k, 0.0, f32(n - 1));
    let k0: i32 = i32(floor(kf));
    let t: f32 = kf - f32(k0);
    let k1: i32 = min(n - 1, k0 + 1);
    return sm[u32(y * n + k0)].xyz * (1.0 - t) + sm[u32(y * n + k1)].xyz * t;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.n * U.lines) { return; }
    let k: i32 = i32(i % U.n);
    let y: i32 = i32(i / U.n);
    let f: u32 = U.frame;
    let s: u32 = U.seed;
    let rot: f32 = U.c.x * gauss3(u32(y), f, 31u + s);
    let cr: f32 = cos(rot);
    let sr: f32 = sin(rot);
    let lh: i32 = i32(U.lh);
    let ph: i32 = i32(U.ph);
    let chh: i32 = i32(U.chh);
    let po: i32 = 2 * lh + 1;
    let co: i32 = po + 2 * ph + 1;
    var Y: f32 = 0.0;
    for (var j: i32 = -lh; j <= lh; j++) { Y = Y + taps[u32(j + lh)] * at(y, f32(k + j)).x; }
    var Yl: f32 = 0.0;
    for (var j2: i32 = -ph; j2 <= ph; j2++) { Yl = Yl + taps[u32(po + j2 + ph)] * at(y, f32(k + j2)).x; }
    let gk: u32 = u32(y) * 4096u + u32(k);
    let nz: f32 = U.b.x * (gauss3(gk, f, 41u + s) - 0.5 * (gauss3(gk - 1u, f, 41u + s) + gauss3(gk + 1u, f, 41u + s)));
    let Yo: f32 = Y + U.b.z * (Y - Yl) + nz;
    let kd: f32 = f32(k) - U.b.w;
    var I: f32 = 0.0;
    var Q: f32 = 0.0;
    for (var j3: i32 = -chh; j3 <= chh; j3++) {
        let v: vec3f = at(y, kd + f32(j3));
        I = I + taps[u32(co + j3 + chh)] * v.y;
        Q = Q + taps[u32(co + j3 + chh)] * v.z;
    }
    let m: f32 = f32(k) / 24.0;
    let m0: f32 = floor(m);
    let t: f32 = m - m0;
    let mu: u32 = u32(m0);
    let cn0: f32 = U.b.y * ((1.0 - t) * gauss3(u32(y), mu, 53u + s) + t * gauss3(u32(y), mu + 1u, 53u + s));
    let cn1: f32 = U.b.y * ((1.0 - t) * gauss3(u32(y), mu, 54u + s) + t * gauss3(u32(y), mu + 1u, 54u + s));
    var tn: f32 = 0.0;
    if (U.a.w > 0.0) { tn = tracking_band(y, f32(f)) * 0.5 * gauss3(gk, f, 61u + s); }
    tp[i] = vec4f(Yo + tn, I * cr - Q * sr + cn0, I * sr + Q * cr + cn1, 0.0);
}

//@pass style_vhs_play
// VHS, pass 3, playback: dropouts filled from the line above (the deck's compensator), then
// Y'IQ to R'G'B'. w is the dropout flag.
@group(0) @binding(0) var<uniform> U: VhsParams;
@group(0) @binding(1) var<storage, read> tp: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> sig: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.n * U.lines) { return; }
    let n: i32 = i32(U.n);
    let k: i32 = i32(i % U.n);
    let y: i32 = i32(i / U.n);
    let f: u32 = U.frame;
    let seg: u32 = u32(k / 64);
    let hit: bool = hash3(u32(y), seg, f * 131u + 71u) < U.c.y;
    let run: f32 = hash3(u32(y), seg, f * 131u + 73u) * 64.0;
    let dropped: bool = hit && f32(k - i32(seg) * 64) < run && y > 0;
    var yy: i32 = y;
    if (dropped) { yy = y - 1; }
    let c: vec3f = tp[u32(yy * n + k)].xyz;
    sig[i] = vec4f(dot(U.inv0.xyz, c), dot(U.inv1.xyz, c), dot(U.inv2.xyz, c), select(0.0, 1.0, dropped));
}

//@pass style_vhs_show
// VHS, display: the signal lines resampled to the output size (linear across, each line held
// down), clamped and rounded to RGBA8.
@group(0) @binding(0) var<uniform> U: VhsParams;
@group(0) @binding(1) var<storage, read> sig: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> out8: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.out_w * U.out_h) { return; }
    let w: i32 = i32(U.out_w);
    let h: i32 = i32(U.out_h);
    let x: i32 = i32(i % U.out_w);
    let y: i32 = i32(i / U.out_w);
    let n: i32 = i32(U.n);
    let nl: i32 = i32(U.lines);
    let ly: i32 = min(nl - 1, (y * nl) / h);
    let kf: f32 = (f32(x) + 0.5) * f32(n) / f32(w) - 0.5;
    let k0: i32 = clamp(i32(floor(kf)), 0, n - 1);
    let k1: i32 = min(n - 1, k0 + 1);
    let t: f32 = clamp(kf - f32(k0), 0.0, 1.0);
    let v: vec3f = clamp(sig[u32(ly * n + k0)].xyz * (1.0 - t) + sig[u32(ly * n + k1)].xyz * t, vec3f(0.0), vec3f(1.0));
    let q: vec3u = vec3u(floor(v * 255.0 + 0.5));
    out8[i] = q.x | (q.y << 8u) | (q.z << 16u) | (255u << 24u);
}

//@pass style_fibre_field
// Fibre-network watercolour (raw/renderer/style_fibre.hpp): the paper, rasterised per pixel from
// the fibres of the 3 x 3 cells around it: coverage and the coverage-weighted orientation tensor
// in F, the crossing density in X.
@group(0) @binding(0) var<uniform> U: FibreParams;
@group(0) @binding(1) var<storage, read_write> F: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> X: array<f32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let C: f32 = U.a.x;
    let qx: f32 = f32(x) + 0.5;
    let qy: f32 = f32(y) + 0.5;
    let cx: i32 = i32(floor(qx / C));
    let cy: i32 = i32(floor(qy / C));
    var cov: f32 = 0.0;
    var cov2: f32 = 0.0;
    var t: vec3f = vec3f(0.0);
    for (var j: i32 = cy - 1; j <= cy + 1; j++) {
        for (var ii: i32 = cx - 1; ii <= cx + 1; ii++) {
            for (var k: i32 = 0; k < i32(U.per_cell); k++) {
                let a: u32 = u32(ii + 8192);
                let b: u32 = u32(j + 8192);
                let s: u32 = u32(k * 97 + 13);
                let fx: f32 = (f32(ii) + hash3(a, b, s)) * C;
                let fy: f32 = (f32(j) + hash3(a, b, s + 1u)) * C;
                let th: f32 = 3.141592653589793 * (U.a.w + U.b.x * (hash3(a, b, s + 2u) - 0.5));
                let dx: f32 = cos(th);
                let dy: f32 = sin(th);
                let hl: f32 = U.a.y * (0.6 + 0.8 * hash3(a, b, s + 3u)) / 2.0;
                let rx: f32 = qx - fx;
                let ry: f32 = qy - fy;
                let tt: f32 = clamp(rx * dx + ry * dy, -hl, hl);
                let ex: f32 = rx - tt * dx;
                let ey: f32 = ry - tt * dy;
                let c: f32 = clamp(0.5 * U.a.z + 0.5 - sqrt(ex * ex + ey * ey), 0.0, 1.0);
                if (c > 0.0) {
                    cov = cov + c;
                    cov2 = cov2 + c * c;
                    t = t + c * vec3f(dx * dx, dx * dy, dy * dy);
                }
            }
        }
    }
    F[i] = vec4f(cov, t);
    X[i] = 0.5 * (cov * cov - cov2);
}

//@pass style_fibre_tensor
// Fibre-network watercolour: the conductivity tensor (Kxx, Kxy, Kyy) from the field smoothed over
// a 7 x 7 box.
@group(0) @binding(0) var<uniform> U: FibreParams;
@group(0) @binding(1) var<storage, read> F: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> K: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    var acc: vec4f = vec4f(0.0);
    var n: f32 = 0.0;
    for (var yy: i32 = max(0, y - 3); yy <= min(h - 1, y + 3); yy++) {
        for (var xx: i32 = max(0, x - 3); xx <= min(w - 1, x + 3); xx++) {
            acc = acc + F[u32(yy * w + xx)];
            n = n + 1.0;
        }
    }
    let cm: f32 = acc.x / n;
    var s: f32 = 0.0;
    if (acc.x > 1.0e-12) { s = 1.0 / acc.x; }
    let g: f32 = U.b.z * min(1.0, cm);
    K[i] = vec4f(U.b.y + g * acc.y * s, g * acc.z * s, U.b.y + g * acc.w * s, 0.0);
}

//@pass style_fibre_init
// Fibre-network watercolour: the wash's pigment (absorbance against the paper) and its starting
// water, dry across strong edges; the deposit starts at zero.
@group(0) @binding(0) var<uniform> U: FibreParams;
@group(0) @binding(1) var<storage, read> src: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> S: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> D: array<vec4f>;

fn lum_at(x: i32, y: i32) -> f32 {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let c: vec3f = src[u32(clamp(y, 0, h - 1) * w + clamp(x, 0, w - 1))].xyz;
    return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let c: vec3f = src[i].xyz;
    let a: vec3f = -log(max(vec3f(0.02), min(vec3f(1.0), c / U.paper.xyz))) * U.c.w;
    let gx: f32 = 0.5 * (lum_at(x + 1, y) - lum_at(x - 1, y));
    let gy: f32 = 0.5 * (lum_at(x, y + 1) - lum_at(x, y - 1));
    S[i] = vec4f(a, clamp(1.0 - U.paper.w * sqrt(gx * gx + gy * gy), 0.05, 1.0));
    D[i] = vec4f(0.0);
}

//@pass style_fibre_step
// Fibre-network watercolour: one explicit step of pigment flux along the eight links (the
// monotone stencil of the tensor), deposition raised at crossings, evaporation. Reads S, writes Sn.
@group(0) @binding(0) var<uniform> U: FibreParams;
@group(0) @binding(1) var<storage, read> K: array<vec4f>;
@group(0) @binding(2) var<storage, read> X: array<f32>;
@group(0) @binding(3) var<storage, read> S: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> Sn: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> D: array<vec4f>;

fn link_g(k: vec4f, l: i32) -> f32 {
    let ax: f32 = max(0.0, k.x - abs(k.y));
    let ay: f32 = max(0.0, k.z - abs(k.y));
    let dg: f32 = abs(k.y) / 2.0;
    let pos: bool = k.y >= 0.0;
    if (l < 2) { return ax; }
    if (l < 4) { return ay; }
    if (l < 6) { return select(0.0, dg, pos); }
    return select(dg, 0.0, pos);
}
fn link_d(l: i32) -> vec2i {
    if (l == 0) { return vec2i(1, 0); }
    if (l == 1) { return vec2i(-1, 0); }
    if (l == 2) { return vec2i(0, 1); }
    if (l == 3) { return vec2i(0, -1); }
    if (l == 4) { return vec2i(1, 1); }
    if (l == 5) { return vec2i(-1, -1); }
    if (l == 6) { return vec2i(1, -1); }
    return vec2i(-1, 1);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let si: vec4f = S[i];
    let ki: vec4f = K[i];
    let dt: f32 = U.c.z;
    var flux: vec3f = vec3f(0.0);
    for (var l: i32 = 0; l < 8; l++) {
        let d: vec2i = link_d(l);
        let xx: i32 = x + d.x;
        let yy: i32 = y + d.y;
        if (xx < 0 || yy < 0 || xx >= w || yy >= h) { continue; }
        let j: u32 = u32(yy * w + xx);
        let sj: vec4f = S[j];
        let g: f32 = 0.5 * (link_g(ki, l) + link_g(K[j], l ^ 1)) * min(si.w, sj.w) * dt;
        flux = flux + g * (sj.xyz - si.xyz);
    }
    let dep: f32 = U.b.w * (1.0 + U.c.x * X[i]) * dt;
    let pc: vec3f = si.xyz + flux;
    let dd: vec3f = min(pc, dep * pc);
    Sn[i] = vec4f(pc - dd, max(0.0, si.w - U.c.y));
    D[i] = vec4f(D[i].xyz + dd, 0.0);
}

//@pass style_fibre_out
// Fibre-network watercolour: the dried wash, paper exp(-(suspended + deposited)), sRGB to RGBA8.
@group(0) @binding(0) var<uniform> U: FibreParams;
@group(0) @binding(1) var<storage, read> S: array<vec4f>;
@group(0) @binding(2) var<storage, read> D: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> out8: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.width * U.height) { return; }
    let o: vec3f = U.paper.xyz * exp(-(S[i].xyz + D[i].xyz));
    out8[i] = pack_rgb8(o);
}

//@pass style_film_expose
// The spectral film shader (raw/renderer/style_film.hpp), first pass: gate weave, then the camera
// negative's three layer exposures from scene-linear Rec.709 (bilinear, clamped).
@group(0) @binding(0) var<uniform> U: FilmParams;
@group(0) @binding(1) var<storage, read> scene: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> H: array<vec4f>;

fn scene_at(x: f32, y: f32) -> vec3f {
    let W: i32 = i32(U.in_w);
    let Hh: i32 = i32(U.in_h);
    let fx: f32 = clamp(x, 0.0, f32(W - 1));
    let fy: f32 = clamp(y, 0.0, f32(Hh - 1));
    let x0: i32 = i32(floor(fx));
    let y0: i32 = i32(floor(fy));
    let x1: i32 = min(W - 1, x0 + 1);
    let y1: i32 = min(Hh - 1, y0 + 1);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    let a: vec3f = scene[u32(y0 * W + x0)].xyz;
    let b: vec3f = scene[u32(y0 * W + x1)].xyz;
    let c: vec3f = scene[u32(y1 * W + x0)].xyz;
    let d: vec3f = scene[u32(y1 * W + x1)].xyz;
    return (a * (1.0 - tx) + b * tx) * (1.0 - ty) + (c * (1.0 - tx) + d * tx) * ty;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.out_w * U.out_h) { return; }
    let x: f32 = f32(i % U.out_w);
    let y: f32 = f32(i / U.out_w);
    let sx: f32 = f32(U.in_w) / f32(U.out_w);
    let sy: f32 = f32(U.in_h) / f32(U.out_h);
    let c: vec3f = scene_at((x + 0.5 - U.a.z) * sx - 0.5, (y + 0.5 - U.a.w) * sy - 0.5);
    let k: f32 = U.a.x;
    H[i] = vec4f(k * dot(U.e0.xyz, c), k * dot(U.e1.xyz, c), k * dot(U.e2.xyz, c), 0.0);
}

//@pass style_film_mtf
// The film: one separable pass of the emulsion-scatter blur (mtf_dir 0 along x, 1 along y).
@group(0) @binding(0) var<uniform> U: FilmParams;
@group(0) @binding(1) var<storage, read> mw: array<f32>;
@group(0) @binding(2) var<storage, read> a: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> o: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.out_w * U.out_h) { return; }
    let W: i32 = i32(U.out_w);
    let Hh: i32 = i32(U.out_h);
    let x: i32 = i32(i % U.out_w);
    let y: i32 = i32(i / U.out_w);
    let r: i32 = i32(U.mtf_r);
    var s: vec3f = vec3f(0.0);
    for (var k: i32 = -r; k <= r; k++) {
        var xx: i32 = x;
        var yy: i32 = y;
        if (U.mtf_dir != 0u) { yy = clamp(y + k, 0, Hh - 1); } else { xx = clamp(x + k, 0, W - 1); }
        s = s + mw[u32(k + r)] * a[u32(yy * W + xx)].xyz;
    }
    o[i] = vec4f(s, 0.0);
}

//@pass style_film_halo_down
// The film: box-average the exposure into the halo grid (q x q cells).
@group(0) @binding(0) var<uniform> U: FilmParams;
@group(0) @binding(1) var<storage, read> em: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> hd: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.gw * U.gh) { return; }
    let gx: u32 = i % U.gw;
    let gy: u32 = i / U.gw;
    let q: u32 = U.halo_q;
    var s: vec3f = vec3f(0.0);
    var n: f32 = 0.0;
    for (var y: u32 = gy * q; y < min(U.out_h, gy * q + q); y++) {
        for (var x: u32 = gx * q; x < min(U.out_w, gx * q + q); x++) {
            s = s + em[y * U.out_w + x].xyz;
            n = n + 1.0;
        }
    }
    hd[i] = vec4f(s / n, 0.0);
}

//@pass style_film_halo_conv
// The film: the halo grid convolved with the kernel derived from the base's optics (zero outside
// the frame).
@group(0) @binding(0) var<uniform> U: FilmParams;
@group(0) @binding(1) var<storage, read> kern: array<f32>;
@group(0) @binding(2) var<storage, read> hd: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> hc: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.gw * U.gh) { return; }
    let gw: i32 = i32(U.gw);
    let gh: i32 = i32(U.gh);
    let R: i32 = i32(U.halo_r);
    let S: i32 = 2 * R + 1;
    let x: i32 = i32(i % U.gw);
    let y: i32 = i32(i / U.gw);
    var s: vec3f = vec3f(0.0);
    for (var dy: i32 = -R; dy <= R; dy++) {
        let yy: i32 = y + dy;
        if (yy < 0 || yy >= gh) { continue; }
        for (var dx: i32 = -R; dx <= R; dx++) {
            let xx: i32 = x + dx;
            if (xx < 0 || xx >= gw) { continue; }
            s = s + kern[u32((dy + R) * S + dx + R)] * hd[u32(yy * gw + xx)].xyz;
        }
    }
    hc[i] = vec4f(s, 0.0);
}

//@pass style_film_develop
// The film: each layer's developed dye fraction from its exposure plus the halation returned
// through the base, through the characteristic curve and interimage.
@group(0) @binding(0) var<uniform> U: FilmParams;
@group(0) @binding(1) var<storage, read> H: array<vec4f>;
@group(0) @binding(2) var<storage, read> hc: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> F: array<vec4f>;

fn pow10(x: f32) -> f32 {
    return exp2(x * 3.321928094887362);
}
fn log10f(x: f32) -> f32 {
    return log2(x) * 0.3010299956639812;
}
fn halo_up(x: u32, y: u32) -> vec3f {
    let q: f32 = f32(U.halo_q);
    let gw: i32 = i32(U.gw);
    let gh: i32 = i32(U.gh);
    let fx: f32 = clamp((f32(x) + 0.5) / q - 0.5, 0.0, f32(gw - 1));
    let fy: f32 = clamp((f32(y) + 0.5) / q - 0.5, 0.0, f32(gh - 1));
    let x0: i32 = i32(floor(fx));
    let y0: i32 = i32(floor(fy));
    let x1: i32 = min(gw - 1, x0 + 1);
    let y1: i32 = min(gh - 1, y0 + 1);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    let a: vec3f = hc[u32(y0 * gw + x0)].xyz;
    let b: vec3f = hc[u32(y0 * gw + x1)].xyz;
    let c: vec3f = hc[u32(y1 * gw + x0)].xyz;
    let d: vec3f = hc[u32(y1 * gw + x1)].xyz;
    return (a * (1.0 - tx) + b * tx) * (1.0 - ty) + (c * (1.0 - tx) + d * tx) * ty;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.out_w * U.out_h) { return; }
    let hl: vec3f = halo_up(i % U.out_w, i / U.out_w);
    let h: vec3f = max(vec3f(1.0e-6), H[i].xyz + U.halo_s.xyz * hl);
    let lh: vec3f = vec3f(log10f(h.x), log10f(h.y), log10f(h.z));
    let D: vec3f = U.neg_dmin.xyz + U.neg_span.xyz / (vec3f(1.0) + exp(-U.neg_k.xyz * (lh - U.neg_x0.xyz)));
    let d: vec3f = D - U.neg_dmin.xyz;
    let m: f32 = (d.x + d.y + d.z) / 3.0;
    let Di: vec3f = U.neg_dmin.xyz + d + U.b.x * (d - vec3f(m));
    let dd: vec3f = min(U.neg_span.xyz, max(vec3f(0.0), Di - U.neg_dmin.xyz));
    let num: vec3f = vec3f(1.0 - pow10(-dd.x), 1.0 - pow10(-dd.y), 1.0 - pow10(-dd.z));
    let den: vec3f = vec3f(1.0 - pow10(-U.neg_span.x), 1.0 - pow10(-U.neg_span.y), 1.0 - pow10(-U.neg_span.z));
    F[i] = vec4f(num / den, 0.0);
}

//@pass style_film_print
// The film's last pass: Boolean-model grain on the negative (Monte Carlo over cloud discs, or the
// model's gaussian when clouds are much smaller than a pixel), then the full spectral print and
// projection, encoded to sRGB RGBA8.
@group(0) @binding(0) var<uniform> U: FilmParams;
@group(0) @binding(1) var<storage, read> tab: array<f32>;
@group(0) @binding(2) var<storage, read> pois: array<u32>;
@group(0) @binding(3) var<storage, read> F: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> out8: array<u32>;

const T_NEGPRIM: u32 = 0u;
const T_NEGCONST: u32 = 123u;
const T_PRINTW: u32 = 164u;
const T_PRINTPRIM: u32 = 287u;
const T_PROJV: u32 = 410u;
const T_J: u32 = 533u;
const FILM_NL: u32 = 41u;
const FILM_PI: f32 = 3.141592653589793;

fn pow10(x: f32) -> f32 {
    return exp2(x * 3.321928094887362);
}
fn log10f(x: f32) -> f32 {
    return log2(x) * 0.3010299956639812;
}
fn u_at(x: i32, y: i32, l: u32) -> f32 {
    let v: vec4f = F[u32(y) * U.out_w + u32(x)];
    if (l == 0u) { return v.x; }
    if (l == 1u) { return v.y; }
    return v.z;
}
fn table_at(u: f32) -> f32 {
    let f: f32 = clamp(u * 32.0, 0.0, 32.0);
    let i: u32 = min(31u, u32(floor(f)));
    return tab[T_J + i] + (tab[T_J + i + 1u] - tab[T_J + i]) * (f - f32(i));
}
fn pick(v: vec4f, l: u32) -> f32 {
    if (l == 0u) { return v.x; }
    if (l == 1u) { return v.y; }
    return v.z;
}
fn grain_mc(px: u32, py: u32, l: u32, r: f32) -> f32 {
    let um: f32 = U.a.y;
    let rpx: f32 = r / um;
    let W: i32 = i32(U.out_w);
    let Hh: i32 = i32(U.out_h);
    let ns: u32 = u32(min(64.0, max(8.0, ceil(12.0 / (FILM_PI * rpx * rpx)))));
    let seed: u32 = U.frame * 3u + l;
    let mu_max: f32 = 2.198806796638283;
    var hit: f32 = 0.0;
    for (var s: u32 = 0u; s < ns; s++) {
        let x: f32 = (f32(px) + hash3(px, py, seed * 131u + 2u * s)) * um;
        let y: f32 = (f32(py) + hash3(px, py, seed * 131u + 2u * s + 1u)) * um;
        let cx0: i32 = i32(floor(x / r));
        let cy0: i32 = i32(floor(y / r));
        var cov: f32 = 0.0;
        for (var cy: i32 = cy0 - 1; cy <= cy0 + 1 && cov < 1.0; cy++) {
            for (var cx: i32 = cx0 - 1; cx <= cx0 + 1 && cov < 1.0; cx++) {
                let qx: i32 = clamp(i32(floor((f32(cx) + 0.5) * r / um)), 0, W - 1);
                let qy: i32 = clamp(i32(floor((f32(cy) + 0.5) * r / um)), 0, Hh - 1);
                let u: f32 = min(0.999, u_at(qx, qy, l));
                let mu: f32 = -log(1.0 - u) / FILM_PI;
                let ucx: u32 = bitcast<u32>(cx);
                let ucy: u32 = bitcast<u32>(cy);
                let hu: u32 = pcg(ucx ^ pcg(ucy ^ pcg(seed * 977u + 1u)));
                let mq: u32 = u32(clamp(floor(mu / mu_max * 512.0), 0.0, 511.0));
                var k: u32 = 0u;
                for (var kk: u32 = 0u; kk < 16u; kk++) {
                    if (hu < pois[mq * 16u + kk]) { break; }
                    k = k + 1u;
                }
                for (var g: u32 = 0u; g < k; g++) {
                    let gx: f32 = (f32(cx) + hash3(ucx, ucy, seed * 977u + 2u + 2u * g)) * r;
                    let gy: f32 = (f32(cy) + hash3(ucx, ucy, seed * 977u + 3u + 2u * g)) * r;
                    let e: f32 = clamp(0.5 + (r - length(vec2f(x - gx, y - gy))) / (0.05 * r), 0.0, 1.0);
                    cov = max(cov, e);
                }
            }
        }
        hit = hit + cov;
    }
    return hit / f32(ns);
}
fn grain_analytic(px: u32, py: u32, l: u32, u: f32, r: f32) -> f32 {
    let seed: u32 = U.frame * 3u + l;
    let u1: f32 = max(1.0e-7, hash3(px, py, seed * 131u + 200u));
    let u2: f32 = hash3(px, py, seed * 131u + 201u);
    let z: f32 = sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
    let um: f32 = U.a.y;
    return u + sqrt(table_at(u) * r * r / (um * um)) * z;
}
fn grain_at(px: u32, py: u32, l: u32) -> f32 {
    let u: f32 = u_at(i32(px), i32(py), l);
    let r: f32 = pick(U.grain_r, l);
    if (r <= 0.0) { return u; }
    let t: f32 = clamp((r / U.a.y - 0.25) / 0.15, 0.0, 1.0);
    let w: f32 = t * t * (3.0 - 2.0 * t);
    var v: f32 = u;
    if (w >= 1.0) {
        v = grain_mc(px, py, l, r);
    } else if (w <= 0.0) {
        v = grain_analytic(px, py, l, u, r);
    } else {
        let a: f32 = grain_mc(px, py, l, r) - u;
        let b: f32 = grain_analytic(px, py, l, u, r) - u;
        v = u + w * a + sqrt(1.0 - w * w) * b;
    }
    return clamp(u + (v - u) * pick(U.grain_cs, l), 0.0, 1.0);
}
fn neg_density(px: u32, py: u32, l: u32) -> f32 {
    let g: f32 = grain_at(px, py, l);
    let span: f32 = pick(U.neg_span, l);
    return pick(U.neg_dmin, l) - log10f(max(1.0e-6, 1.0 - g * (1.0 - pow10(-span))));
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let i: u32 = gid.x;
    if (i >= U.out_w * U.out_h) { return; }
    let px: u32 = i % U.out_w;
    let py: u32 = i / U.out_w;
    let D: vec3f = vec3f(neg_density(px, py, 0u), neg_density(px, py, 1u), neg_density(px, py, 2u));
    var hp: vec3f = vec3f(0.0);
    for (var w: u32 = 0u; w < FILM_NL; w++) {
        let p: vec3f = vec3f(tab[T_NEGPRIM + 3u * w], tab[T_NEGPRIM + 3u * w + 1u], tab[T_NEGPRIM + 3u * w + 2u]);
        let T: f32 = pow10(-(dot(D, p) + tab[T_NEGCONST + w]));
        hp = hp + T * vec3f(tab[T_PRINTW + 3u * w], tab[T_PRINTW + 3u * w + 1u], tab[T_PRINTW + 3u * w + 2u]);
    }
    let lh: vec3f = vec3f(log10f(max(hp.x, 1.0e-12)), log10f(max(hp.y, 1.0e-12)), log10f(max(hp.z, 1.0e-12))) + U.log_gain.xyz;
    let dp: vec3f = U.prt_dmin.xyz + U.prt_span.xyz / (vec3f(1.0) + exp(-U.prt_k.xyz * lh));
    let dm: vec3f = U.prt_dmin.xyz;
    let silver: f32 = U.b.y * (dp.x + dp.y + dp.z - dm.x - dm.y - dm.z) / 3.0;
    var xyz: vec3f = vec3f(0.0);
    for (var w2: u32 = 0u; w2 < FILM_NL; w2++) {
        let p: vec3f = vec3f(tab[T_PRINTPRIM + 3u * w2], tab[T_PRINTPRIM + 3u * w2 + 1u], tab[T_PRINTPRIM + 3u * w2 + 2u]);
        let T: f32 = pow10(-(dot(dp, p) + U.b.z + silver));
        xyz = xyz + T * vec3f(tab[T_PROJV + 3u * w2], tab[T_PROJV + 3u * w2 + 1u], tab[T_PROJV + 3u * w2 + 2u]);
    }
    let rgb: vec3f = vec3f(dot(U.out0.xyz, xyz), dot(U.out1.xyz, xyz), dot(U.out2.xyz, xyz));
    out8[i] = pack_rgb8(rgb);
}
