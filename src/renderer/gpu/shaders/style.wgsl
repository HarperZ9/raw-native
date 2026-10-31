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

// The lens suite. a: cx, cy, squeeze, iris roundness; b: cos and sin of the streak angle, streak
// length, streak thickness (px); c: star cell (px), star gain, threshold, display gain; phi per channel.
struct LensGpuParams {
    width: u32, height: u32, n_src: u32, n_ghost: u32,
    star_m: u32, tile: u32, tiles_x: u32, blades: u32,
    a: vec4f,
    b: vec4f,
    c: vec4f,
    phi: vec4f,
}

// Scan-causal EHT sag. a: S, alpha, focus, sigma0; b: bright, gamma, initial sag.
struct SagGpuParams {
    width: u32, height: u32, blank: u32, settle: u32,
    radius: u32, pad0: u32, pad1: u32, pad2: u32,
    a: vec4f,
    b: vec4f,
}
// Salience caricature. a: lambda cap, gain, display gain; w0..w2: the salience weight of each
// material id 0 to 9.
struct CariGpuParams {
    width: u32, height: u32, radius: u32, steps: u32,
    dir: u32, pad0: u32, pad1: u32, pad2: u32,
    a: vec4f,
    w0: vec4f,
    w1: vec4f,
    w2: vec4f,
}

// Perspective-stable pixel art. origin: the probe origin and the posterise bands; a: outline,
// crease, quad expansion, crossfade blend; eye and tan(fovy / 2); the camera's forward, right, up.
struct PixelGpuParams {
    width: u32, height: u32, n: u32, has_prev: u32,
    origin: vec4f,
    a: vec4f,
    eye: vec4f,
    f: vec4f,
    r: vec4f,
    u: vec4f,
}

// The painterly shader. medium 0 oil, 1 matte, 2 water; canvas_mode 0 offset, 1 advected; dir and
// blur_r for the tensor blur. a: exposure, radius, q, zeta; b: eta, impasto, gloss, push; c: broken,
// value bands, lines, line sigma; d: line sharpness, warp, granulation, edge; e: dilution, canvas
// offset x and y.
struct PaintGpuParams {
    width: u32, height: u32, medium: u32, canvas_mode: u32,
    dir: u32, stroke_len: u32, blur_r: u32, pad0: u32,
    a: vec4f,
    b: vec4f,
    c: vec4f,
    d: vec4f,
    e: vec4f,
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

// The pixel-art diorama (web/shaders/pixel/scene.mjs) and its OKLab styling, shared by the pixel
// passes: distance, material and object; the march (t < 0 for a miss); normal; shading; sky.
fn px_box(p: vec3f, c: vec3f, b: vec3f) -> f32 {
    let q: vec3f = abs(p - c) - b;
    return length(max(q, vec3f(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
}
fn px_sph(p: vec3f, c: vec3f, r: f32) -> f32 {
    return length(p - c) - r;
}
fn px_cyl(p: vec3f, cx: f32, cz: f32, y0: f32, y1: f32, r: f32) -> f32 {
    return max(length(vec2f(p.x - cx, p.z - cz)) - r, abs(p.y - (y0 + y1) / 2.0) - (y1 - y0) / 2.0);
}
fn px_map(p: vec3f) -> vec3f {
    var r: vec3f = vec3f(p.y, 0.0, 0.0);
    let pond: f32 = max(length(vec2f(p.x - 2.2, (p.z - 1.6) * 1.4)) - 0.9, abs(p.y + 0.02) - 0.03);
    if (pond < r.x) { r = vec3f(pond, 1.0, 1.0); }
    let walls: f32 = px_box(p, vec3f(-1.0, 0.8, -0.6), vec3f(1.2, 0.8, 0.9));
    if (walls < r.x) { r = vec3f(walls, 2.0, 2.0); }
    let rx: f32 = (p.x + 1.0) * 0.7071 + (p.y - 1.6) * 0.7071;
    let ry: f32 = -(p.x + 1.0) * 0.7071 + (p.y - 1.6) * 0.7071;
    let roof: f32 = max(px_box(vec3f(rx, ry, p.z), vec3f(0.0, 0.0, -0.6), vec3f(0.95, 0.95, 1.05)), -p.y + 1.6);
    if (roof < r.x) { r = vec3f(roof, 3.0, 3.0); }
    let door: f32 = px_box(p, vec3f(-0.5, 0.45, 0.32), vec3f(0.22, 0.45, 0.04));
    if (door < r.x) { r = vec3f(door, 4.0, 4.0); }
    let win: f32 = px_box(p, vec3f(-1.45, 0.95, 0.32), vec3f(0.25, 0.2, 0.03));
    if (win < r.x) { r = vec3f(win, 5.0, 5.0); }
    let trunk: f32 = px_cyl(p, 1.9, -1.2, 0.0, 1.1, 0.13);
    if (trunk < r.x) { r = vec3f(trunk, 6.0, 6.0); }
    let leaves: f32 = min(px_sph(p, vec3f(1.9, 1.5, -1.2), 0.62), px_sph(p, vec3f(2.25, 1.15, -0.95), 0.42));
    if (leaves < r.x) { r = vec3f(leaves, 7.0, 7.0); }
    let body: f32 = px_cyl(p, 0.6, 1.0, 0.0, 0.62, 0.16);
    if (body < r.x) { r = vec3f(body, 8.0, 8.0); }
    let head: f32 = px_sph(p, vec3f(0.6, 0.78, 1.0), 0.13);
    if (head < r.x) { r = vec3f(head, 9.0, 9.0); }
    let post: f32 = px_cyl(p, 0.2, 1.75, 0.0, 0.9, 0.035);
    if (post < r.x) { r = vec3f(post, 10.0, 10.0); }
    let lamp: f32 = px_box(p, vec3f(0.2, 0.98, 1.75), vec3f(0.08, 0.09, 0.08));
    if (lamp < r.x) { r = vec3f(lamp, 11.0, 11.0); }
    let rock: f32 = px_sph(p, vec3f(-2.3, 0.15, 1.4), 0.38) + 0.04 * sin(p.x * 9.0) * sin(p.z * 7.0);
    if (rock < r.x) { r = vec3f(rock, 12.0, 12.0); }
    return r;
}
fn px_albedo(m: i32) -> vec3f {
    if (m == 0) { return vec3f(0.17, 0.32, 0.1); }
    if (m == 1) { return vec3f(0.08, 0.2, 0.32); }
    if (m == 2) { return vec3f(0.62, 0.5, 0.36); }
    if (m == 3) { return vec3f(0.45, 0.13, 0.09); }
    if (m == 4) { return vec3f(0.28, 0.16, 0.08); }
    if (m == 5) { return vec3f(1.0, 0.8, 0.4); }
    if (m == 6) { return vec3f(0.25, 0.15, 0.08); }
    if (m == 7) { return vec3f(0.1, 0.3, 0.09); }
    if (m == 8) { return vec3f(0.15, 0.22, 0.45); }
    if (m == 9) { return vec3f(0.75, 0.55, 0.42); }
    if (m == 10) { return vec3f(0.1, 0.1, 0.1); }
    if (m == 11) { return vec3f(1.0, 0.75, 0.35); }
    return vec3f(0.4, 0.4, 0.42);
}
fn px_normal(p: vec3f) -> vec3f {
    let e: f32 = 1.0e-3;
    let nx: f32 = px_map(p + vec3f(e, 0.0, 0.0)).x - px_map(p - vec3f(e, 0.0, 0.0)).x;
    let ny: f32 = px_map(p + vec3f(0.0, e, 0.0)).x - px_map(p - vec3f(0.0, e, 0.0)).x;
    let nz: f32 = px_map(p + vec3f(0.0, 0.0, e)).x - px_map(p - vec3f(0.0, 0.0, e)).x;
    return normalize(vec3f(nx, ny, nz));
}
fn px_march(o: vec3f, r: vec3f, tmax: f32) -> vec3f {
    var t: f32 = 0.0;
    for (var i: i32 = 0; i < 160 && t < tmax; i++) {
        let h: vec3f = px_map(o + r * t);
        if (h.x < 1.0e-3 * (1.0 + t)) { return vec3f(t, h.y, h.z); }
        t = t + h.x * 0.9;
    }
    return vec3f(-1.0, -1.0, -1.0);
}
fn px_sky(r: vec3f) -> vec3f {
    let k: f32 = max(0.0, r.y);
    return vec3f(0.85 - 0.6 * k, 0.82 - 0.4 * k, 0.78 + 0.1 * k);
}
fn px_shade(p: vec3f, n: vec3f, m: i32) -> vec3f {
    if (m == 5) { return px_albedo(5) * 1.6; }
    if (m == 11) { return px_albedo(11) * 4.0; }
    var alb: vec3f = px_albedo(m);
    if (m == 0) {
        if (((i32(floor(p.x * 2.0)) + i32(floor(p.z * 2.0))) & 1) == 0) { alb = alb * 0.86; }
    }
    let L: vec3f = normalize(vec3f(-0.45, 0.8, 0.4));
    let sh: vec3f = px_march(p + L * 0.02, L, 12.0);
    let sun: f32 = max(0.0, dot(n, L)) * select(0.0, 1.0, sh.x < 0.0) * 2.6;
    let skyf: f32 = (0.55 + 0.45 * n.y) * 0.55;
    let lv: vec3f = vec3f(0.2, 0.98, 1.75) - p;
    let ld: f32 = length(lv);
    let lan: f32 = max(0.0, dot(n, lv) / ld) * 1.2 / (ld * ld + 0.2);
    return alb * (sun * vec3f(1.0, 0.93, 0.8) + skyf * vec3f(0.6, 0.75, 1.0) + lan * vec3f(1.0, 0.7, 0.35));
}
fn px_stylise(rgb: vec3f, bands: f32, dL: f32) -> vec3f {
    let c: vec3f = max(rgb, vec3f(0.0));
    let l: f32 = pow(0.4122214708 * c.x + 0.5363325363 * c.y + 0.0514459929 * c.z, 1.0 / 3.0);
    let m: f32 = pow(0.2119034982 * c.x + 0.6806995451 * c.y + 0.1073969566 * c.z, 1.0 / 3.0);
    let s: f32 = pow(0.0883024619 * c.x + 0.2817188376 * c.y + 0.6299787005 * c.z, 1.0 / 3.0);
    let L: f32 = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    let a: f32 = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    let b: f32 = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    let Lq: f32 = clamp(floor((L + dL) * bands + 0.5) / bands, 0.0, 1.0);
    let l2: f32 = Lq + 0.3963377774 * a + 0.2158037573 * b;
    let m2: f32 = Lq - 0.1055613458 * a - 0.0638541728 * b;
    let s2: f32 = Lq - 0.0894841775 * a - 1.2914855480 * b;
    let l3: f32 = l2 * l2 * l2;
    let m3: f32 = m2 * m2 * m2;
    let s3: f32 = s2 * s2 * s2;
    return max(vec3f(4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3, -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3, -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3), vec3f(0.0));
}
fn px_face_dir(f: u32, u: f32, v: f32) -> vec3f {
    if (f == 0u) { return vec3f(1.0, -v, -u); }
    if (f == 1u) { return vec3f(-1.0, -v, u); }
    if (f == 2u) { return vec3f(u, 1.0, v); }
    if (f == 3u) { return vec3f(u, -1.0, -v); }
    if (f == 4u) { return vec3f(u, -v, 1.0); }
    return vec3f(-u, -v, -1.0);
}
fn px_cheb(r: vec3f) -> f32 {
    return max(abs(r.x), max(abs(r.y), abs(r.z)));
}

// The painterly shader's noise fields (web/shaders/paint/noise.mjs), shared by its passes; and its
// flow field from a smoothed structure tensor (abstract.mjs flow).
fn pt_fade(t: f32) -> f32 {
    return t * t * (3.0 - 2.0 * t);
}
fn pt_vnoise(x: f32, y: f32, seed: u32) -> f32 {
    let xi: f32 = floor(x);
    let yi: f32 = floor(y);
    let tx: f32 = pt_fade(x - xi);
    let ty: f32 = pt_fade(y - yi);
    let ix: u32 = bitcast<u32>(i32(xi));
    let iy: u32 = bitcast<u32>(i32(yi));
    let a: f32 = hash3(ix, iy, seed);
    let b: f32 = hash3(ix + 1u, iy, seed);
    let c: f32 = hash3(ix, iy + 1u, seed);
    let d: f32 = hash3(ix + 1u, iy + 1u, seed);
    return (a + (b - a) * tx) * (1.0 - ty) + (c + (d - c) * tx) * ty;
}
fn pt_paper(x: f32, y: f32) -> f32 {
    let c1: f32 = 0.9397;
    let s1: f32 = 0.342;
    let c2: f32 = -0.342;
    let s2: f32 = 0.9397;
    let f1: f32 = pt_vnoise((x * c1 + y * s1) / 0.9, (-x * s1 + y * c1) / 7.0, 31u);
    let f2: f32 = pt_vnoise((x * c2 + y * s2) / 0.9, (-x * s2 + y * c2) / 7.0, 37u);
    return 0.45 * pt_vnoise(x / 2.2, y / 2.2, 11u) + 0.25 * pt_vnoise(x / 9.0, y / 9.0, 13u) + 0.15 * f1 + 0.15 * f2;
}
// Noise kind: 0 bristle, 1 stroke identity, 2 canvas weave, 3 paper, 4 paper at half frequency,
// 5 and 6 the warp fields.
fn pt_noise(kind: u32, x: f32, y: f32) -> f32 {
    if (kind == 0u) { return 0.7 * pt_vnoise(x / 1.3, y / 1.3, 53u) + 0.3 * pt_vnoise(x / 5.0, y / 5.0, 59u); }
    if (kind == 1u) { return pt_vnoise(x / 6.0, y / 6.0, 67u); }
    if (kind == 2u) { return 0.5 + 0.25 * sin(6.283185307179586 * x / 3.2) * cos(6.283185307179586 * y / 3.2) + 0.25 * (pt_vnoise(x / 1.5, y / 1.5, 61u) - 0.5); }
    if (kind == 3u) { return pt_paper(x, y); }
    if (kind == 4u) { return pt_paper(x / 2.0, y / 2.0); }
    if (kind == 5u) { return 2.0 * pt_vnoise(x / 23.0, y / 23.0, 71u) - 1.0; }
    return 2.0 * pt_vnoise(x / 23.0, y / 23.0, 73u) - 1.0;
}
fn pt_smooth(a: f32, b: f32, x: f32) -> f32 {
    let t: f32 = clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
fn pt_major(e: f32, f: f32, g: f32) -> vec3f {
    let tr: f32 = e + g;
    let l1: f32 = (tr + sqrt((e - g) * (e - g) + 4.0 * f * f)) / 2.0;
    let a: vec2f = vec2f(f, l1 - e);
    let b: vec2f = vec2f(l1 - g, f);
    let na: f32 = length(a);
    let nb: f32 = length(b);
    if (na >= nb) { return vec3f(a, na); }
    return vec3f(b, nb);
}
fn pt_flow(T: vec4f) -> vec3f {
    let E: f32 = T.x;
    let F: f32 = T.y;
    let G: f32 = T.z;
    let tr: f32 = E + G;
    let dsc: f32 = sqrt((E - G) * (E - G) + 4.0 * F * F);
    let l1: f32 = (tr + dsc) / 2.0;
    let l2: f32 = (tr - dsc) / 2.0;
    let sh: f32 = clamp((tr - 1.0e-7) / 9.0e-7, 0.0, 1.0);
    var A: f32 = 0.0;
    if (tr > 1.0e-12) { A = (l1 - l2) / (l1 + l2) * sh * sh * (3.0 - 2.0 * sh); }
    let s: f32 = clamp((A - 0.02) / 0.08, 0.0, 1.0);
    let k: f32 = s * s * (3.0 - 2.0 * s);
    var inv: f32 = 0.0;
    if (tr > 1.0e-12) { inv = 1.0 / tr; }
    let m: vec3f = pt_major(k * E * inv + (1.0 - k) * 0.5, k * F * inv - (1.0 - k) * 0.5, k * G * inv + (1.0 - k) * 0.5);
    if (m.z <= 1.0e-12) { return vec3f(0.7071067811865476, 0.7071067811865476, A); }
    return vec3f(-m.y / m.z, m.x / m.z, A);
}

// The physical CRT (raw/renderer/style_crt.hpp). The uniform carries the frame number; the
// parameter block of layout.mjs (crtPackParams, 122 values) and then the packed filter taps are
// one storage array P, indexed by these constants.
struct CrtpGpuParams {
    frame: u32, pad0: u32, pad1: u32, pad2: u32,
}
const CP_srcW: u32 = 0u;
const CP_N: u32 = 2u;
const CP_lines: u32 = 3u;
const CP_mode: u32 = 4u;
const CP_pal: u32 = 5u;
const CP_comb: u32 = 6u;
const CP_lineCyc: u32 = 7u;
const CP_frameCyc: u32 = 8u;
const CP_tapRgb: u32 = 10u;
const CP_tapEnc: u32 = 12u;
const CP_tapSep: u32 = 14u;
const CP_tapLum: u32 = 16u;
const CP_tapI: u32 = 18u;
const CP_tapQ: u32 = 20u;
const CP_fwd: u32 = 22u;
const CP_inv: u32 = 31u;
const CP_outW: u32 = 40u;
const CP_outH: u32 = 41u;
const CP_viewW: u32 = 42u;
const CP_viewH: u32 = 43u;
const CP_cx: u32 = 44u;
const CP_cy: u32 = 45u;
const CP_rx: u32 = 46u;
const CP_ry: u32 = 47u;
const CP_dist: u32 = 48u;
const CP_faceW: u32 = 49u;
const CP_faceH: u32 = 50u;
const CP_rasterW: u32 = 51u;
const CP_rasterH: u32 = 52u;
const CP_lineMm: u32 = 53u;
const CP_corner: u32 = 54u;
const CP_contrast: u32 = 55u;
const CP_brightness: u32 = 56u;
const CP_gamma: u32 = 57u;
const CP_spotLo: u32 = 58u;
const CP_spotHi: u32 = 59u;
const CP_spotExp: u32 = 60u;
const CP_maskType: u32 = 61u;
const CP_pitch: u32 = 62u;
const CP_fill: u32 = 63u;
const CP_slotH: u32 = 64u;
const CP_slotGap: u32 = 65u;
const CP_dampers: u32 = 66u;
const CP_nDampers: u32 = 68u;
const CP_damperW: u32 = 69u;
const CP_maskMix: u32 = 70u;
const CP_maskMean: u32 = 71u;
const CP_maskLock: u32 = 72u;
const CP_pitchPx: u32 = 73u;
const CP_conv: u32 = 74u;
const CP_mono: u32 = 79u;
const CP_interlace: u32 = 80u;
const CP_decay: u32 = 81u;
const CP_fieldT: u32 = 96u;
const CP_haloQ: u32 = 97u;
const CP_haloR: u32 = 98u;
const CP_haloDirect: u32 = 99u;
const CP_gw: u32 = 100u;
const CP_gh: u32 = 101u;
const CP_M: u32 = 102u;
const CP_luma: u32 = 111u;
const CP_ambient: u32 = 114u;
const CP_transmission: u32 = 115u;
const CP_reflection: u32 = 116u;
const CP_glassN: u32 = 117u;
const CP_bezel: u32 = 118u;
const CP_exposure: u32 = 121u;
const CP_taps: u32 = 122u;
fn cp_erf(x: f32) -> f32 {
    let s: f32 = select(1.0, -1.0, x < 0.0);
    let a: f32 = abs(x);
    let t: f32 = 1.0 / (1.0 + 0.3275911 * a);
    let y: f32 = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-a * a);
    return s * y;
}
fn cp_gauss_mass(a: f32, b: f32, mu: f32, sigma: f32) -> f32 {
    let k: f32 = 1.0 / (sigma * 1.4142135623730951);
    return 0.5 * (cp_erf((b - mu) * k) - cp_erf((a - mu) * k));
}
fn cp_tent_cdf(u: f32, a: f32) -> f32 {
    let v: f32 = u / a;
    if (v <= -1.0) { return 0.0; }
    if (v <= 0.0) { return 0.5 * (1.0 + v) * (1.0 + v); }
    if (v < 1.0) { return 1.0 - 0.5 * (1.0 - v) * (1.0 - v); }
    return 1.0;
}
fn cp_pulse_f(x: f32, pp: f32, w: f32) -> f32 {
    let n: f32 = floor(x / pp);
    return n * w + min(max(x - n * pp, 0.0), w);
}
fn cp_pulse_box(y0: f32, y1: f32, pp: f32, w: f32, s: f32) -> f32 {
    let n0: f32 = floor((y0 - s) / pp);
    let a: f32 = y0 - s - n0 * pp;
    let b: f32 = y1 - s - n0 * pp;
    return (cp_pulse_f(b, pp, w) - cp_pulse_f(a, pp, w)) / (y1 - y0);
}
fn cp_room_env(d: vec3f) -> f32 {
    let c: f32 = dot(d, vec3f(-0.38, -0.33, -0.864));
    let t: f32 = clamp((c - 0.975) / 0.02, 0.0, 1.0);
    return 0.04 + t * t * (3.0 - 2.0 * t);
}
fn cp_fresnel_air(n: f32, theta: f32) -> f32 {
    let s: f32 = sin(theta) / n;
    let ci: f32 = cos(theta);
    let ct: f32 = sqrt(1.0 - s * s);
    let rs: f32 = (ci - n * ct) / (ci + n * ct);
    let rp: f32 = (ct - n * ci) / (ct + n * ci);
    return 0.5 * (rs * rs + rp * rp);
}
fn cp_shoulder(x: f32) -> f32 {
    if (x <= 0.8) { return x; }
    // tanh from one exp (WARP's hyperbolic functions are imprecise; the paint glaze, bounds-11).
    let u: f32 = (x - 0.8) / 0.2;
    let e: f32 = exp(-2.0 * u);
    return 0.8 + 0.2 * (1.0 - e) / (1.0 + e);
}
fn cp_enc(v: f32, exposure: f32) -> u32 {
    let l: f32 = cp_shoulder(max(0.0, v * exposure));
    var s: f32 = 1.055 * pow(l, 1.0 / 2.4) - 0.055;
    if (l <= 0.0031308) { s = l * 12.92; }
    return u32(clamp(floor(s * 255.0 + 0.5), 0.0, 255.0));
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
fn fractal_rank(x: i32, y: i32) -> u32 {
    var r: u32 = 0u;
    for (var k: u32 = 0u; k < 6u; k++) {
        let xb: u32 = u32((x >> k) & 1);
        let yb: u32 = u32((y >> k) & 1);
        r = (r << 2u) | (((xb ^ yb) << 1u) | yb);
    }
    return r;
}
// Surface-stable fractal threshold (raw/renderer/style_ditherlight.hpp, fractalThreshold): a Bayer hierarchy over the
// chart at a continuous scale, the blend of two levels mapped through its own CDF. a: cell, px, pa.
fn fractal_blend(tc: f32, tf: f32, m: f32) -> f32 {
    let b: f32 = (1.0 - m) * tc + m * tf;
    let den: f32 = 1.0 - 0.75 * m;
    var s: f32 = 0.0;
    for (var d: u32 = 0u; d < 4u; d++) { s += clamp((b - m * f32(d) * 0.25) / den, 0.0, 1.0); }
    return s * 0.25;
}
fn fractal_threshold(u: f32, v: f32, depth: f32) -> f32 {
    let lam: f32 = clamp(log2((D.a.y * D.a.z * max(depth, 1.0e-6)) / D.a.x), -16.0, 16.0);
    let n0: f32 = floor(lam);
    let size: f32 = D.a.x * exp2(n0);
    let i: i32 = i32(floor(u / size));
    let j: i32 = i32(floor(v / size));
    let tf: f32 = (f32(fractal_rank(i, j)) + 0.5) / 4096.0;
    let tc: f32 = (f32(fractal_rank(i >> 1u, j >> 1u)) + 0.5) / 4096.0;
    return fractal_blend(tc, tf, 1.0 - (lam - n0));
}
fn level_lin(k: f32, n: f32, gamma: f32) -> f32 {
    if (k <= 0.0) { return 0.0; }
    return pow(k / (n - 1.0), gamma);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    var ft: f32 = 0.0;
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
        if (D.anchor == 2u) { size = size * exp2(octave_of(D.a.x, D.a.y, D.a.z, g0.z)); }
        if (D.anchor == 3u) { ft = fractal_threshold(u, w, g0.z); }
        cx = i32(floor(u / size));
        cy = i32(floor(w / size));
    }
    var t: f32 = threshold_at(cx, cy);
    if (D.anchor == 3u) { t = ft; }
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let p: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    if (gidx >= U.cw * U.ch) { return; }
    let i: i32 = i32(gidx % U.cw);
    let j: i32 = i32(gidx / U.cw);
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
    G[gidx] = vec4f(c, b, 0.0, 0.0);
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
        let j: u32 = u32(3 * (k + r));
        s = s + vec3f(mw[j], mw[j + 1u], mw[j + 2u]) * a[u32(yy * W + xx)].xyz;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
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

//@pass style_lens_core
// The lens suite (raw/renderer/style_lens.hpp), core pass: each pixel of a kept tile gives up phi
// (per channel) of its over-threshold light to the lens terms.
@group(0) @binding(0) var<uniform> U: LensGpuParams;
@group(0) @binding(1) var<storage, read> img: array<vec4f>;
@group(0) @binding(2) var<storage, read> keep: array<u32>;
@group(0) @binding(3) var<storage, read_write> core: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let x: u32 = i % U.width;
    let y: u32 = i / U.width;
    let c: vec4f = img[i];
    var k: f32 = 0.0;
    if (keep[(y / U.tile) * U.tiles_x + x / U.tile] != 0u) {
        let Y: f32 = 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
        if (Y > U.c.z) { k = (Y - U.c.z) / Y; }
    }
    core[i] = vec4f(c.x * (1.0 - U.phi.x * k), c.y * (1.0 - U.phi.y * k), c.z * (1.0 - U.phi.z * k), 1.0);
}

//@pass style_lens_gather
// The lens suite, gather pass: lateral dispersion resamples the core per wavelength bin, then
// every source adds its starburst, anamorphic streak, halation and ghosts; lab-encoded to RGBA8.
// bins: RGB weight (8 x 3), dispersion scale (8), halation gain and radius (8 x 2). srcs: 24
// floats a source (x, y, E, ghost factor per bin, streak factor per bin). ghosts: per pair and
// bin (m, radius, rotation, 1 / area).
@group(0) @binding(0) var<uniform> U: LensGpuParams;
@group(0) @binding(1) var<storage, read> core: array<vec4f>;
@group(0) @binding(2) var<storage, read> ghosts: array<f32>;
@group(0) @binding(3) var<storage, read> srcs: array<f32>;
@group(0) @binding(4) var<storage, read> star: array<f32>;
@group(0) @binding(5) var<storage, read> bins: array<f32>;
@group(0) @binding(6) var<storage, read_write> out8: array<u32>;

const LENS_TAU: f32 = 6.283185307179586;
const LENS_PI: f32 = 3.141592653589793;

fn rgbw(l: u32) -> vec3f {
    return vec3f(bins[l * 3u], bins[l * 3u + 1u], bins[l * 3u + 2u]);
}
fn samp(px: f32, py: f32) -> vec3f {
    let w: u32 = U.width;
    let h: u32 = U.height;
    let x: f32 = min(f32(w - 1u), max(0.0, px - 0.5));
    let y: f32 = min(f32(h - 1u), max(0.0, py - 0.5));
    let x0: u32 = min(w - 2u, u32(floor(x)));
    let y0: u32 = min(h - 2u, u32(floor(y)));
    let fx: f32 = x - f32(x0);
    let fy: f32 = y - f32(y0);
    let i: u32 = y0 * w + x0;
    return (core[i].xyz * (1.0 - fx) + core[i + 1u].xyz * fx) * (1.0 - fy) + (core[i + w].xyz * (1.0 - fx) + core[i + w + 1u].xyz * fx) * fy;
}
fn star_at(u: f32, v: f32) -> vec3f {
    let M: u32 = U.star_m;
    let mf: f32 = f32(M - 1u);
    if (u < 0.0 || v < 0.0 || u > mf || v > mf) { return vec3f(0.0); }
    let x0: u32 = min(M - 2u, u32(floor(u)));
    let y0: u32 = min(M - 2u, u32(floor(v)));
    let fx: f32 = u - f32(x0);
    let fy: f32 = v - f32(y0);
    let i: u32 = (y0 * M + x0) * 3u;
    let j: u32 = i + M * 3u;
    let a: vec3f = vec3f(star[i], star[i + 1u], star[i + 2u]);
    let b: vec3f = vec3f(star[i + 3u], star[i + 4u], star[i + 5u]);
    let c: vec3f = vec3f(star[j], star[j + 1u], star[j + 2u]);
    let d: vec3f = vec3f(star[j + 3u], star[j + 4u], star[j + 5u]);
    return (a * (1.0 - fx) + b * fx) * (1.0 - fy) + (c * (1.0 - fx) + d * fx) * fy;
}
fn edge_rot(phi: f32, n: f32, rnd: f32, rot: f32) -> f32 {
    let seg: f32 = LENS_TAU / n;
    let x: f32 = phi - rot;
    let a: f32 = x - floor(x / seg) * seg;
    let poly: f32 = cos(LENS_PI / n) / cos(a - LENS_PI / n);
    return poly + (1.0 - poly) * rnd;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let qx: f32 = f32(i % U.width) + 0.5;
    let qy: f32 = f32(i / U.width) + 0.5;
    let cx: f32 = U.a.x;
    let cy: f32 = U.a.y;
    var o: vec3f = vec3f(0.0);
    for (var l: u32 = 0u; l < 8u; l++) {
        let s: f32 = bins[24u + l];
        o = o + rgbw(l) * samp(cx + (qx - cx) / s, cy + (qy - cy) / s);
    }
    let n: f32 = f32(U.blades);
    let rnd: f32 = U.a.w;
    let ca: f32 = U.b.x;
    let sa: f32 = U.b.y;
    let L: f32 = U.b.z;
    let sg: f32 = U.b.w;
    let cell: f32 = U.c.x;
    let hm: f32 = (f32(U.star_m) - 1.0) * 0.5;
    let sinv: f32 = 1.0 / (cell * cell);
    for (var si: u32 = 0u; si < U.n_src; si++) {
        let b: u32 = si * 24u;
        let sx: f32 = srcs[b];
        let sy: f32 = srcs[b + 1u];
        let E: vec3f = vec3f(srcs[b + 2u], srcs[b + 3u], srcs[b + 4u]);
        let dx: f32 = qx - sx;
        let dy: f32 = qy - sy;
        o = o + E * U.c.y * star_at(dx / cell + hm, dy / cell + hm) * sinv;
        let a: f32 = ca * dx + sa * dy;
        let bb: f32 = -sa * dx + ca * dy;
        let k: f32 = (exp(-abs(a) / L) / (2.0 * L)) * (exp(-bb * bb / (2.0 * sg * sg)) / (sg * 2.5066282746310002));
        let r: f32 = sqrt(dx * dx + dy * dy);
        for (var l2: u32 = 0u; l2 < 8u; l2++) {
            let ha: f32 = bins[32u + l2 * 2u];
            let hr: f32 = bins[33u + l2 * 2u];
            let f: f32 = srcs[b + 13u + l2] * k + ha * exp(-r / hr) / (LENS_TAU * hr * hr);
            o = o + E * rgbw(l2) * f;
        }
        for (var g: u32 = 0u; g < U.n_ghost; g++) {
            for (var l3: u32 = 0u; l3 < 8u; l3++) {
                let gb: u32 = (g * 8u + l3) * 4u;
                let m: f32 = ghosts[gb];
                let rad: f32 = ghosts[gb + 1u];
                let gx: f32 = cx + m * (sx - cx);
                let gy: f32 = cy + m * (sy - cy);
                let vx: f32 = (qx - gx) * U.a.z;
                let vy: f32 = qy - gy;
                let rr: f32 = sqrt(vx * vx + vy * vy);
                if (rr > rad + 1.0) { continue; }
                let cov: f32 = clamp(rad * edge_rot(atan2(vy, vx), n, rnd, ghosts[gb + 2u]) - rr + 0.5, 0.0, 1.0);
                if (cov <= 0.0) { continue; }
                o = o + E * rgbw(l3) * (srcs[b + 5u + l3] * cov * ghosts[gb + 3u]);
            }
        }
    }
    out8[i] = lab_pack(o, U.c.w);
}

//@pass style_sag_current
// Scan-causal EHT sag (raw/renderer/style_sag.hpp): the beam current of one line, one workgroup a
// line, summed by a tree in workgroup memory.
@group(0) @binding(0) var<uniform> U: SagGpuParams;
@group(0) @binding(1) var<storage, read> src: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> I: array<f32>;
var<workgroup> part: array<f32, 64>;

@compute @workgroup_size(64)
fn main(@builtin(local_invocation_id) lid: vec3u, @builtin(workgroup_id) wid: vec3u) {
    let w: i32 = i32(U.width);
    let y: i32 = i32(wid.x);
    let g: f32 = U.b.y;
    var s: f32 = 0.0;
    for (var x: i32 = i32(lid.x); x < w; x += 64) {
        let c: vec3f = max(src[u32(y * w + x)].xyz, vec3f(0.0));
        s = s + (pow(c.x, g) + pow(c.y, g) + pow(c.z, g)) / 3.0;
    }
    part[lid.x] = s;
    workgroupBarrier();
    for (var st: u32 = 32u; st > 0u; st = st >> 1u) {
        if (lid.x < st) { part[lid.x] = part[lid.x] + part[lid.x + st]; }
        workgroupBarrier();
    }
    if (lid.x == 0u) { I[u32(y)] = part[0] / f32(w); }
}

//@pass style_sag_scan
// Scan-causal EHT sag: one invocation walks the lines in order (the recurrence is causal), twice
// with settle; L holds sag, gain, drawn height and spot sigma per line.
@group(0) @binding(0) var<uniform> U: SagGpuParams;
@group(0) @binding(1) var<storage, read> I: array<f32>;
@group(0) @binding(2) var<storage, read_write> L: array<vec4f>;

@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    if (gidx > 0u) { return; }
    let h: i32 = i32(U.height);
    let a: f32 = U.a.y;
    let S: f32 = U.a.x;
    var s: f32 = U.b.z;
    let cy: f32 = f32(h) / 2.0;
    for (var y: i32 = 0; y < h; y++) {
        s = s + a * (S * I[u32(y)] - s);
        L[u32(y)] = vec4f(s, 0.0, 0.0, 0.0);
    }
    if (U.settle != 0u) {
        s = s * pow(1.0 - a, f32(U.blank));
        for (var y2: i32 = 0; y2 < h; y2++) {
            s = s + a * (S * I[u32(y2)] - s);
            L[u32(y2)] = vec4f(s, 0.0, 0.0, 0.0);
        }
    }
    for (var y3: i32 = 0; y3 < h; y3++) {
        let v: f32 = L[u32(y3)].x;
        let k: f32 = 1.0 / sqrt(1.0 - v);
        L[u32(y3)] = vec4f(v, k, cy + (f32(y3) + 0.5 - cy) * k, U.a.w + U.a.z * v);
    }
}

//@pass style_sag_resample
// Scan-causal EHT sag: each output line read from the two drawn lines around it, each sampled at
// its own gain with a gaussian spot (in linear light), dimmed with the sag; signal8 to RGBA8.
@group(0) @binding(0) var<uniform> U: SagGpuParams;
@group(0) @binding(1) var<storage, read> src: array<vec4f>;
@group(0) @binding(2) var<storage, read> L: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> out8: array<u32>;

fn sample_line(y: i32, xs: f32, sigma: f32) -> vec3f {
    let w: i32 = i32(U.width);
    let R: i32 = i32(U.radius);
    let g: f32 = U.b.y;
    let x0: i32 = i32(floor(xs));
    var ws: f32 = 0.0;
    var acc: vec3f = vec3f(0.0);
    for (var j: i32 = x0 - R; j <= x0 + R; j++) {
        if (j < 0 || j >= w) { continue; }
        let d: f32 = (f32(j) + 0.5 - xs) / sigma;
        let wt: f32 = exp(-0.5 * d * d);
        let c: vec3f = max(src[u32(y * w + j)].xyz, vec3f(0.0));
        ws = ws + wt;
        acc = acc + wt * vec3f(pow(c.x, g), pow(c.y, g), pow(c.z, g));
    }
    if (ws > 0.0) { acc = acc / ws; }
    return acc;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let x: i32 = i32(i % U.width);
    let yo: i32 = i32(i / U.width);
    let yc: f32 = f32(yo) + 0.5;
    let cx: f32 = f32(w) / 2.0;
    if (yc < L[0].z || yc > L[u32(h - 1)].z) {
        out8[i] = 255u << 24u;
        return;
    }
    var lo: i32 = 0;
    var hi: i32 = h - 1;
    while (hi - lo > 1) {
        let m: i32 = (lo + hi) / 2;
        if (L[u32(m)].z <= yc) { lo = m; } else { hi = m; }
    }
    let y1: i32 = min(h - 1, lo + 1);
    let A: vec4f = L[u32(lo)];
    let Bq: vec4f = L[u32(y1)];
    var f: f32 = 0.0;
    if (y1 != lo) { f = (yc - A.z) / (Bq.z - A.z); }
    let xc: f32 = f32(x) + 0.5;
    let a: vec3f = sample_line(lo, cx + (xc - cx) / A.y, A.w) * pow(1.0 - A.x, U.b.x);
    let b: vec3f = sample_line(y1, cx + (xc - cx) / Bq.y, Bq.w) * pow(1.0 - Bq.x, U.b.x);
    let m3: vec3f = a * (1.0 - f) + b * f;
    let ig: f32 = 1.0 / U.b.y;
    let q: vec3f = clamp(vec3f(pow(m3.x, ig), pow(m3.y, ig), pow(m3.z, ig)), vec3f(0.0), vec3f(1.0));
    out8[i] = u32(floor(q.x * 255.0 + 0.5)) | (u32(floor(q.y * 255.0 + 0.5)) << 8u) | (u32(floor(q.z * 255.0 + 0.5)) << 16u) | (255u << 24u);
}

//@pass style_cari_sal
// Salience caricature (raw/renderer/style_sag.hpp): salience from the material ids.
@group(0) @binding(0) var<uniform> U: CariGpuParams;
@group(0) @binding(1) var<storage, read> mat: array<i32>;
@group(0) @binding(2) var<storage, read_write> s: array<f32>;

fn weight(m: i32) -> f32 {
    if (m < 0 || m > 9) { return 0.0; }
    if (m < 4) { return U.w0[m]; }
    if (m < 8) { return U.w1[m - 4]; }
    return U.w2[m - 8];
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    s[i] = weight(mat[i]);
}

//@pass style_cari_blur
// Salience caricature: one pass of the separable gaussian (dir 0 along x, 1 along y), clamped ends.
@group(0) @binding(0) var<uniform> U: CariGpuParams;
@group(0) @binding(1) var<storage, read> taps: array<f32>;
@group(0) @binding(2) var<storage, read> a: array<f32>;
@group(0) @binding(3) var<storage, read_write> b: array<f32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let R: i32 = i32(U.radius);
    var v: f32 = 0.0;
    for (var k: i32 = -R; k <= R; k++) {
        var j: i32 = y * w + clamp(x + k, 0, w - 1);
        if (U.dir != 0u) { j = clamp(y + k, 0, h - 1) * w + x; }
        v = v + taps[u32(k + R)] * a[u32(j)];
    }
    b[i] = v;
}

//@pass style_cari_grad
// Salience caricature: the central-difference gradient of the smoothed salience.
@group(0) @binding(0) var<uniform> U: CariGpuParams;
@group(0) @binding(1) var<storage, read> S: array<f32>;
@group(0) @binding(2) var<storage, read_write> g: array<vec2f>;

fn cl(x: i32, y: i32) -> u32 {
    return u32(clamp(y, 0, i32(U.height) - 1) * i32(U.width) + clamp(x, 0, i32(U.width) - 1));
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    g[i] = vec2f(0.5 * (S[cl(x + 1, y)] - S[cl(x - 1, y)]), 0.5 * (S[cl(x, y + 1)] - S[cl(x, y - 1)]));
}

//@pass style_cari_lambda
// Salience caricature: one workgroup finds the largest Frobenius norm of the gradient's derivative
// (a max tree in workgroup memory), then lambda, clamped so the composed map keeps det J >= 0.04.
@group(0) @binding(0) var<uniform> U: CariGpuParams;
@group(0) @binding(1) var<storage, read> g: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> L: array<f32>;
var<workgroup> part: array<f32, 256>;

fn cl(x: i32, y: i32) -> u32 {
    return u32(clamp(y, 0, i32(U.height) - 1) * i32(U.width) + clamp(x, 0, i32(U.width) - 1));
}

@compute @workgroup_size(256)
fn main(@builtin(local_invocation_id) lid: vec3u) {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    var m: f32 = 0.0;
    for (var i: i32 = i32(lid.x); i < w * h; i += 256) {
        let x: i32 = i % w;
        let y: i32 = i / w;
        let dx: vec2f = 0.5 * (g[cl(x + 1, y)] - g[cl(x - 1, y)]);
        let dy: vec2f = 0.5 * (g[cl(x, y + 1)] - g[cl(x, y - 1)]);
        m = max(m, sqrt(dx.x * dx.x + dy.x * dy.x + dx.y * dx.y + dy.y * dy.y));
    }
    part[lid.x] = m;
    workgroupBarrier();
    for (var st: u32 = 128u; st > 0u; st = st >> 1u) {
        if (lid.x < st) { part[lid.x] = max(part[lid.x], part[lid.x + st]); }
        workgroupBarrier();
    }
    if (lid.x == 0u) {
        let c: f32 = 1.0 - pow(0.04, 1.0 / (2.0 * f32(U.steps)));
        var lam: f32 = 0.0;
        if (part[0] > 0.0) { lam = min(U.a.x, (c * U.a.y) / part[0]); }
        L[0] = lam;
    }
}

//@pass style_cari_warp
// Salience caricature: each output pixel reads the source at y + lambda grad S (composed over the
// steps, the gradient read bilinearly at the moving point), lab-encoded to RGBA8.
@group(0) @binding(0) var<uniform> U: CariGpuParams;
@group(0) @binding(1) var<storage, read> src: array<vec4f>;
@group(0) @binding(2) var<storage, read> g: array<vec2f>;
@group(0) @binding(3) var<storage, read> L: array<f32>;
@group(0) @binding(4) var<storage, read_write> out8: array<u32>;

fn grad_at(fx: f32, fy: f32) -> vec2f {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let u: f32 = fx - 0.5;
    let v: f32 = fy - 0.5;
    let x0: i32 = clamp(i32(floor(u)), 0, w - 1);
    let y0: i32 = clamp(i32(floor(v)), 0, h - 1);
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(h - 1, y0 + 1);
    let tx: f32 = clamp(u - f32(x0), 0.0, 1.0);
    let ty: f32 = clamp(v - f32(y0), 0.0, 1.0);
    return (g[u32(y0 * w + x0)] * (1.0 - tx) + g[u32(y0 * w + x1)] * tx) * (1.0 - ty) + (g[u32(y1 * w + x0)] * (1.0 - tx) + g[u32(y1 * w + x1)] * tx) * ty;
}
fn sample_src(fx: f32, fy: f32) -> vec3f {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let u: f32 = fx - 0.5;
    let v: f32 = fy - 0.5;
    let x0: i32 = clamp(i32(floor(u)), 0, w - 1);
    let y0: i32 = clamp(i32(floor(v)), 0, h - 1);
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(h - 1, y0 + 1);
    let tx: f32 = clamp(u - f32(x0), 0.0, 1.0);
    let ty: f32 = clamp(v - f32(y0), 0.0, 1.0);
    return (src[u32(y0 * w + x0)].xyz * (1.0 - tx) + src[u32(y0 * w + x1)].xyz * tx) * (1.0 - ty) + (src[u32(y1 * w + x0)].xyz * (1.0 - tx) + src[u32(y1 * w + x1)].xyz * tx) * ty;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let x: f32 = f32(i % U.width);
    let y: f32 = f32(i / U.width);
    let lam: f32 = L[0];
    var f: vec2f = vec2f(x + 0.5, y + 0.5) + lam * g[i];
    for (var k: i32 = 1; k < i32(U.steps); k++) { f = f + lam * grad_at(f.x, f.y); }
    out8[i] = lab_pack(sample_src(f.x, f.y), U.a.z);
}

//@pass style_pixel_capture
// Perspective-stable pixel art (raw/renderer/style_pixel.hpp): the probe, one ray a texel from the
// snapped origin; geo holds the hit's Chebyshev distance (-1 for sky), material and object.
@group(0) @binding(0) var<uniform> U: PixelGpuParams;
@group(0) @binding(1) var<storage, read_write> geo: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> nrm: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let N: u32 = U.n;
    let k: u32 = gidx;
    if (k >= 6u * N * N) { return; }
    let f: u32 = k / (N * N);
    let j: u32 = (k % (N * N)) / N;
    let i: u32 = k % N;
    let r: vec3f = px_face_dir(f, 2.0 * (f32(i) + 0.5) / f32(N) - 1.0, 2.0 * (f32(j) + 0.5) / f32(N) - 1.0);
    let d: vec3f = normalize(r);
    let o: vec3f = U.origin.xyz;
    let hit: vec3f = px_march(o, d, 40.0);
    if (hit.x < 0.0) {
        geo[k] = vec4f(-1.0, -1.0, -1.0, 0.0);
        nrm[k] = vec4f(0.0);
        return;
    }
    let p: vec3f = o + d * hit.x;
    geo[k] = vec4f(px_cheb(p - o), hit.y, hit.z, 0.0);
    nrm[k] = vec4f(px_normal(p), 0.0);
}

//@pass style_pixel_shade
// Pixel art: the camera-independent colour of every texel, posterised, with outlines where a
// texel borders a farther object and creases where the normal turns.
@group(0) @binding(0) var<uniform> U: PixelGpuParams;
@group(0) @binding(1) var<storage, read> geo: array<vec4f>;
@group(0) @binding(2) var<storage, read> nrm: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> col: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let N: u32 = U.n;
    let k: u32 = gidx;
    if (k >= 6u * N * N) { return; }
    let f: u32 = k / (N * N);
    let j: i32 = i32((k % (N * N)) / N);
    let i: i32 = i32(k % N);
    let u: f32 = 2.0 * (f32(i) + 0.5) / f32(N) - 1.0;
    let v: f32 = 2.0 * (f32(j) + 0.5) / f32(N) - 1.0;
    let bands: f32 = U.origin.w;
    let g: vec4f = geo[k];
    let r: vec3f = px_face_dir(f, u, v);
    if (g.x < 0.0) {
        col[k] = vec4f(px_stylise(px_sky(normalize(r)), bands, 0.0), 1.0);
        return;
    }
    let p: vec3f = U.origin.xyz + (g.x / px_cheb(r)) * r;
    let n: vec3f = nrm[k].xyz;
    var dL: f32 = 0.0;
    for (var s: i32 = 0; s < 4; s++) {
        var di: i32 = 0;
        var dj: i32 = 0;
        if (s == 0) { di = 1; }
        if (s == 1) { di = -1; }
        if (s == 2) { dj = 1; }
        if (s == 3) { dj = -1; }
        let ii: i32 = i + di;
        let jj: i32 = j + dj;
        if (ii < 0 || jj < 0 || ii >= i32(N) || jj >= i32(N)) { continue; }
        let q: u32 = (f * N + u32(jj)) * N + u32(ii);
        let gq: vec4f = geo[q];
        if (gq.z != g.z && (gq.x < 0.0 || gq.x > g.x * 1.03)) {
            dL = -U.a.x;
        } else if (dL == 0.0 && gq.z == g.z && (di > 0 || dj > 0) && dot(n, nrm[q].xyz) < 0.6) {
            dL = -U.a.y;
        }
    }
    col[k] = vec4f(px_stylise(px_shade(p, n, i32(g.y)), bands, dL), 1.0);
}

//@pass style_pixel_clear
// Pixel art: a splat buffer filled with 0xffffffff (no texel).
@group(0) @binding(0) var<uniform> U: PixelGpuParams;
@group(0) @binding(1) var<storage, read_write> a: array<u32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    if (gidx >= U.width * U.height) { return; }
    a[gidx] = 0xffffffffu;
}

//@pass style_pixel_splatz
// Pixel art: each texel's world-space quad projected into the view; the nearest depth key (view
// depth quantised to 1/4096) per pixel by atomicMin.
@group(0) @binding(0) var<uniform> U: PixelGpuParams;
@group(0) @binding(1) var<storage, read> geo: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> zb: array<atomic<u32>>;

fn cam_project(p: vec3f) -> vec3f {
    let v: vec3f = p - U.eye.xyz;
    let z: f32 = dot(v, U.f.xyz);
    let W: f32 = f32(U.width);
    let H: f32 = f32(U.height);
    let x: f32 = dot(v, U.r.xyz) / (z * U.eye.w * (W / H));
    let y: f32 = dot(v, U.u.xyz) / (z * U.eye.w);
    return vec3f((x + 1.0) / 2.0 * W, (1.0 - y) / 2.0 * H, z);
}
fn edgef(a: vec2f, b: vec2f, x: f32, y: f32) -> f32 {
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}
fn in_tri(a: vec2f, b: vec2f, c: vec2f, x: f32, y: f32) -> bool {
    let e0: f32 = edgef(a, b, x, y);
    let e1: f32 = edgef(b, c, x, y);
    let e2: f32 = edgef(c, a, x, y);
    return (e0 >= 0.0 && e1 >= 0.0 && e2 >= 0.0) || (e0 <= 0.0 && e1 <= 0.0 && e2 <= 0.0);
}
fn corner(f: u32, u: f32, v: f32, d: f32) -> vec3f {
    let r: vec3f = px_face_dir(f, u, v);
    return cam_project(U.origin.xyz + (d / px_cheb(r)) * r);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let N: u32 = U.n;
    let k: u32 = gidx;
    if (k >= 6u * N * N) { return; }
    let d: f32 = geo[k].x;
    if (d < 0.0) { return; }
    let f: u32 = k / (N * N);
    let j: u32 = (k % (N * N)) / N;
    let i: u32 = k % N;
    let u: f32 = 2.0 * (f32(i) + 0.5) / f32(N) - 1.0;
    let v: f32 = 2.0 * (f32(j) + 0.5) / f32(N) - 1.0;
    let h: f32 = U.a.z / f32(N);
    let c0: vec3f = corner(f, u - h, v - h, d);
    let c1: vec3f = corner(f, u + h, v - h, d);
    let c2: vec3f = corner(f, u + h, v + h, d);
    let c3: vec3f = corner(f, u - h, v + h, d);
    let cc: vec3f = corner(f, u, v, d);
    if (c0.z < 0.05 || c1.z < 0.05 || c2.z < 0.05 || c3.z < 0.05 || cc.z < 0.05) { return; }
    let W: i32 = i32(U.width);
    let H: i32 = i32(U.height);
    let x0: i32 = max(0, i32(floor(min(min(c0.x, c1.x), min(c2.x, c3.x)))));
    let x1: i32 = min(W - 1, i32(ceil(max(max(c0.x, c1.x), max(c2.x, c3.x)))));
    let y0: i32 = max(0, i32(floor(min(min(c0.y, c1.y), min(c2.y, c3.y)))));
    let y1: i32 = min(H - 1, i32(ceil(max(max(c0.y, c1.y), max(c2.y, c3.y)))));
    if (x1 - x0 > 63 || y1 - y0 > 63) { return; }
    let z: u32 = u32(floor(cc.z * 4096.0));
    for (var y: i32 = y0; y <= y1; y++) {
        for (var x: i32 = x0; x <= x1; x++) {
            let px: f32 = f32(x) + 0.5;
            let py: f32 = f32(y) + 0.5;
            if (!in_tri(c0.xy, c1.xy, c2.xy, px, py) && !in_tri(c0.xy, c2.xy, c3.xy, px, py)) { continue; }
            atomicMin(&zb[u32(y * W + x)], z);
        }
    }
}

//@pass style_pixel_splatid
// Pixel art: the lowest texel index among the quads at each pixel's nearest depth key (atomicMin);
// the depth keys are read plainly, written by the previous pass.
@group(0) @binding(0) var<uniform> U: PixelGpuParams;
@group(0) @binding(1) var<storage, read> geo: array<vec4f>;
@group(0) @binding(2) var<storage, read> zb: array<u32>;
@group(0) @binding(3) var<storage, read_write> idb: array<atomic<u32>>;

fn cam_project(p: vec3f) -> vec3f {
    let v: vec3f = p - U.eye.xyz;
    let z: f32 = dot(v, U.f.xyz);
    let W: f32 = f32(U.width);
    let H: f32 = f32(U.height);
    let x: f32 = dot(v, U.r.xyz) / (z * U.eye.w * (W / H));
    let y: f32 = dot(v, U.u.xyz) / (z * U.eye.w);
    return vec3f((x + 1.0) / 2.0 * W, (1.0 - y) / 2.0 * H, z);
}
fn edgef(a: vec2f, b: vec2f, x: f32, y: f32) -> f32 {
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}
fn in_tri(a: vec2f, b: vec2f, c: vec2f, x: f32, y: f32) -> bool {
    let e0: f32 = edgef(a, b, x, y);
    let e1: f32 = edgef(b, c, x, y);
    let e2: f32 = edgef(c, a, x, y);
    return (e0 >= 0.0 && e1 >= 0.0 && e2 >= 0.0) || (e0 <= 0.0 && e1 <= 0.0 && e2 <= 0.0);
}
fn corner(f: u32, u: f32, v: f32, d: f32) -> vec3f {
    let r: vec3f = px_face_dir(f, u, v);
    return cam_project(U.origin.xyz + (d / px_cheb(r)) * r);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let N: u32 = U.n;
    let k: u32 = gidx;
    if (k >= 6u * N * N) { return; }
    let d: f32 = geo[k].x;
    if (d < 0.0) { return; }
    let f: u32 = k / (N * N);
    let j: u32 = (k % (N * N)) / N;
    let i: u32 = k % N;
    let u: f32 = 2.0 * (f32(i) + 0.5) / f32(N) - 1.0;
    let v: f32 = 2.0 * (f32(j) + 0.5) / f32(N) - 1.0;
    let h: f32 = U.a.z / f32(N);
    let c0: vec3f = corner(f, u - h, v - h, d);
    let c1: vec3f = corner(f, u + h, v - h, d);
    let c2: vec3f = corner(f, u + h, v + h, d);
    let c3: vec3f = corner(f, u - h, v + h, d);
    let cc: vec3f = corner(f, u, v, d);
    if (c0.z < 0.05 || c1.z < 0.05 || c2.z < 0.05 || c3.z < 0.05 || cc.z < 0.05) { return; }
    let W: i32 = i32(U.width);
    let H: i32 = i32(U.height);
    let x0: i32 = max(0, i32(floor(min(min(c0.x, c1.x), min(c2.x, c3.x)))));
    let x1: i32 = min(W - 1, i32(ceil(max(max(c0.x, c1.x), max(c2.x, c3.x)))));
    let y0: i32 = max(0, i32(floor(min(min(c0.y, c1.y), min(c2.y, c3.y)))));
    let y1: i32 = min(H - 1, i32(ceil(max(max(c0.y, c1.y), max(c2.y, c3.y)))));
    if (x1 - x0 > 63 || y1 - y0 > 63) { return; }
    let z: u32 = u32(floor(cc.z * 4096.0));
    for (var y: i32 = y0; y <= y1; y++) {
        for (var x: i32 = x0; x <= x1; x++) {
            let px: f32 = f32(x) + 0.5;
            let py: f32 = f32(y) + 0.5;
            if (!in_tri(c0.xy, c1.xy, c2.xy, px, py) && !in_tri(c0.xy, c2.xy, c3.xy, px, py)) { continue; }
            let p: u32 = u32(y * W + x);
            if (zb[p] == z) { atomicMin(&idb[p], k); }
        }
    }
}

//@pass style_pixel_resolve
// Pixel art: each pixel's texel colour, or an eye-ray fill where no texel reaches; during a cell
// change a 4 x 4 Bayer threshold picks the previous probe. sRGB to RGBA8.
@group(0) @binding(0) var<uniform> U: PixelGpuParams;
@group(0) @binding(1) var<storage, read> col0: array<vec4f>;
@group(0) @binding(2) var<storage, read> col1: array<vec4f>;
@group(0) @binding(3) var<storage, read> idb0: array<u32>;
@group(0) @binding(4) var<storage, read> idb1: array<u32>;
@group(0) @binding(5) var<storage, read_write> out8: array<u32>;

fn bayer4(x: u32, y: u32) -> f32 {
    let i: u32 = (y & 3u) * 4u + (x & 3u);
    let a: vec4f = vec4f(0.0, 8.0, 2.0, 10.0);
    let b: vec4f = vec4f(12.0, 4.0, 14.0, 6.0);
    let c: vec4f = vec4f(3.0, 11.0, 1.0, 9.0);
    let d: vec4f = vec4f(15.0, 7.0, 13.0, 5.0);
    if (i < 4u) { return a[i]; }
    if (i < 8u) { return b[i - 4u]; }
    if (i < 12u) { return c[i - 8u]; }
    return d[i - 12u];
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let p: u32 = gidx;
    if (p >= U.width * U.height) { return; }
    let x: u32 = p % U.width;
    let y: u32 = p / U.width;
    let use_prev: bool = U.has_prev != 0u && (bayer4(x, y) + 0.5) / 16.0 >= U.a.w;
    var t: u32 = idb0[p];
    if (use_prev) { t = idb1[p]; }
    var c: vec3f = vec3f(0.0);
    if (t != 0xffffffffu) {
        if (use_prev) { c = col1[t].xyz; } else { c = col0[t].xyz; }
    } else {
        let W: f32 = f32(U.width);
        let H: f32 = f32(U.height);
        let sx: f32 = (2.0 * (f32(x) + 0.5) / W - 1.0) * U.eye.w * (W / H);
        let sy: f32 = (1.0 - 2.0 * (f32(y) + 0.5) / H) * U.eye.w;
        let d: vec3f = normalize(U.f.xyz + sx * U.r.xyz + sy * U.u.xyz);
        let e: vec3f = U.eye.xyz;
        let hit: vec3f = px_march(e, d, 40.0);
        if (hit.x < 0.0) {
            c = px_stylise(px_sky(d), U.origin.w, 0.0);
        } else {
            let q: vec3f = e + d * hit.x;
            c = px_stylise(px_shade(q, px_normal(q), i32(hit.y)), U.origin.w, 0.0);
        }
    }
    out8[p] = pack_rgb8(c);
}

//@pass style_paint_prep
// The painterly shader (raw/renderer/style_paint.hpp), first pass: scene light through a soft
// shoulder on the largest channel to a display colour, its sRGB encoding and luminance, and its
// Kubelka-Munk pigment latent (concentrations and the RGB residual, two vec4 a pixel).
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read> pig: array<f32>;
@group(0) @binding(2) var<storage, read> scene: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> sb: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> lat: array<vec4f>;

fn km_decode(c: vec4f) -> vec3f {
    var o: vec3f = vec3f(0.0);
    for (var w: u32 = 0u; w < 31u; w++) {
        let K: f32 = dot(c, vec4f(pig[4u * w], pig[4u * w + 1u], pig[4u * w + 2u], pig[4u * w + 3u]));
        let S: f32 = dot(c, vec4f(pig[124u + 4u * w], pig[124u + 4u * w + 1u], pig[124u + 4u * w + 2u], pig[124u + 4u * w + 3u]));
        let q: f32 = K / max(S, 1.0e-9);
        let R: f32 = 1.0 + q - sqrt(q * q + 2.0 * q);
        o = o + R * vec3f(pig[248u + 3u * w], pig[248u + 3u * w + 1u], pig[248u + 3u * w + 2u]);
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
                c = c + w * vec4f(pig[o], pig[o + 1u], pig[o + 2u], pig[o + 3u]);
            }
        }
    }
    return c;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let k: f32 = U.a.x;
    let sc: vec3f = max(scene[i].xyz, vec3f(0.0));
    let m: f32 = max(sc.x, max(sc.y, sc.z));
    let t: f32 = m * k;
    var gain: f32 = k * (1.0 - t / 2.0 + t * t / 6.0);
    if (t >= 1.0e-3) { gain = (1.0 - exp(-t)) / m; }
    let D: vec3f = sc * gain;
    let e: vec3f = vec3f(linear_to_srgb(D.x), linear_to_srgb(D.y), linear_to_srgb(D.z));
    let c: vec4f = lut_conc(e);
    let dec: vec3f = km_decode(c);
    sb[i] = vec4f(e, 0.2126 * D.x + 0.7152 * D.y + 0.0722 * D.z);
    lat[2u * i] = c;
    lat[2u * i + 1u] = vec4f(D - dec, 0.0);
}

//@pass style_paint_tensor
// The painterly shader: the structure tensor (Sobel on the sRGB colour).
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read> sb: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> T: array<vec4f>;

fn at(x: i32, y: i32) -> vec3f {
    return sb[u32(clamp(y, 0, i32(U.height) - 1) * i32(U.width) + clamp(x, 0, i32(U.width) - 1))].xyz;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let gx: vec3f = (at(x + 1, y - 1) + 2.0 * at(x + 1, y) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2.0 * at(x - 1, y) - at(x - 1, y + 1)) / 4.0;
    let gy: vec3f = (at(x - 1, y + 1) + 2.0 * at(x, y + 1) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2.0 * at(x, y - 1) - at(x + 1, y - 1)) / 4.0;
    T[i] = vec4f(dot(gx, gx), dot(gx, gy), dot(gy, gy), 0.0);
}

//@pass style_paint_blur
// The painterly shader: one pass of the tensor's separable gaussian (dir 0 along x, 1 along y).
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read> wts: array<f32>;
@group(0) @binding(2) var<storage, read> a: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> o: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    let r: i32 = i32(U.blur_r);
    var s: vec4f = vec4f(0.0);
    for (var k: i32 = -r; k <= r; k++) {
        var xx: i32 = x;
        var yy: i32 = y;
        if (U.dir != 0u) { yy = clamp(y + k, 0, h - 1); } else { xx = clamp(x + k, 0, w - 1); }
        s = s + wts[u32(k + r)] * a[u32(yy * w + xx)];
    }
    o[i] = s;
}

//@pass style_paint_akf
// The painterly shader: the anisotropic Kuwahara filter, eight polynomial-weighted sectors of an
// ellipse along the flow, averaging pigment latents (so the filter mixes paint).
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read> sb: array<vec4f>;
@group(0) @binding(2) var<storage, read> lat: array<vec4f>;
@group(0) @binding(3) var<storage, read> T: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> olat: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> os: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let p: u32 = gidx;
    if (p >= U.width * U.height) { return; }
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let x: i32 = i32(p % U.width);
    let y: i32 = i32(p / U.width);
    let f: vec3f = pt_flow(T[p]);
    let tx: f32 = f.x;
    let ty: f32 = f.y;
    let A: f32 = f.z;
    let r: f32 = U.a.y;
    let zeta: f32 = U.a.w;
    let eta: f32 = U.b.x;
    let a: f32 = r * (1.0 + A);
    let b: f32 = r / (1.0 + A);
    let ex: i32 = i32(ceil(sqrt(a * a * tx * tx + b * b * ty * ty)));
    let ey: i32 = i32(ceil(sqrt(a * a * ty * ty + b * b * tx * tx)));
    var acc: array<f32, 112>;
    for (var dy: i32 = -ey; dy <= ey; dy++) {
        for (var dx: i32 = -ex; dx <= ex; dx++) {
            let u: f32 = (tx * f32(dx) + ty * f32(dy)) / a;
            let v: f32 = (-ty * f32(dx) + tx * f32(dy)) / b;
            let rr: f32 = u * u + v * v;
            if (rr > 1.0) { continue; }
            let qi: u32 = u32(clamp(y + dy, 0, h - 1) * w + clamp(x + dx, 0, w - 1));
            let g: f32 = exp(-2.0 * rr);
            let l0: vec4f = lat[2u * qi];
            let l1: vec4f = lat[2u * qi + 1u];
            let sv: vec3f = sb[qi].xyz;
            for (var k: i32 = 0; k < 8; k++) {
                let ang: f32 = -6.283185307179586 * f32(k) / 8.0;
                let xk: f32 = u * cos(ang) - v * sin(ang);
                let yk: f32 = u * sin(ang) + v * cos(ang);
                let z: f32 = xk + zeta - eta * yk * yk;
                if (z <= 0.0) { continue; }
                let wk: f32 = z * z * g;
                let o: i32 = k * 14;
                acc[o] = acc[o] + wk;
                acc[o + 1] = acc[o + 1] + wk * l0.x;
                acc[o + 2] = acc[o + 2] + wk * l0.y;
                acc[o + 3] = acc[o + 3] + wk * l0.z;
                acc[o + 4] = acc[o + 4] + wk * l0.w;
                acc[o + 5] = acc[o + 5] + wk * l1.x;
                acc[o + 6] = acc[o + 6] + wk * l1.y;
                acc[o + 7] = acc[o + 7] + wk * l1.z;
                acc[o + 8] = acc[o + 8] + wk * sv.x;
                acc[o + 9] = acc[o + 9] + wk * sv.y;
                acc[o + 10] = acc[o + 10] + wk * sv.z;
                acc[o + 11] = acc[o + 11] + wk * sv.x * sv.x;
                acc[o + 12] = acc[o + 12] + wk * sv.y * sv.y;
                acc[o + 13] = acc[o + 13] + wk * sv.z * sv.z;
            }
        }
    }
    var wsum: f32 = 0.0;
    var L0: vec4f = vec4f(0.0);
    var L1: vec3f = vec3f(0.0);
    var S3: vec3f = vec3f(0.0);
    for (var k2: i32 = 0; k2 < 8; k2++) {
        let o: i32 = k2 * 14;
        let Wk: f32 = acc[o];
        if (Wk <= 0.0) { continue; }
        let m: vec3f = vec3f(acc[o + 8], acc[o + 9], acc[o + 10]) / Wk;
        let m2: vec3f = vec3f(acc[o + 11], acc[o + 12], acc[o + 13]) / Wk;
        let v2: f32 = max(m2.x - m.x * m.x, 0.0) + max(m2.y - m.y * m.y, 0.0) + max(m2.z - m.z * m.z, 0.0);
        let sd: f32 = sqrt(v2) * 255.0;
        var pw: f32 = 0.0;
        if (sd > 1.0e-6) { pw = pow(sd, U.a.z); }
        let alpha: f32 = 1.0 / (1.0 + pw);
        wsum = wsum + alpha;
        L0 = L0 + alpha * vec4f(acc[o + 1], acc[o + 2], acc[o + 3], acc[o + 4]) / Wk;
        L1 = L1 + alpha * vec3f(acc[o + 5], acc[o + 6], acc[o + 7]) / Wk;
        S3 = S3 + alpha * m;
    }
    olat[2u * p] = L0 / wsum;
    olat[2u * p + 1u] = vec4f(L1 / wsum, 0.0);
    os[p] = vec4f(S3 / wsum, A);
}

//@pass style_paint_stroke
// The painterly shader: brush relief and stroke identity, bristle and identity noise averaged
// along the flow both ways with triangle weights, fading out where the flow turns near 90 degrees.
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read> T: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> Hs: array<vec2f>;
@group(0) @binding(3) var<storage, read> Cv: array<vec4f>;
@group(0) @binding(4) var<storage, read> Kv: array<f32>;

fn layer_weight(k: u32) -> f32 {
    let age: f32 = Kv[5u * k];
    if (age < 0.0) { return 0.0; }
    return min(1.0, (age + 1.0) / 8.0) * max(0.02, 1.0 - Kv[5u * k + 3u] / 0.35);
}
fn cv_at(x: f32, y: f32) -> vec4f {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let fx: f32 = clamp(x, 0.0, f32(w - 1));
    let fy: f32 = clamp(y, 0.0, f32(h - 1));
    let x0: i32 = i32(floor(fx));
    let y0: i32 = i32(floor(fy));
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(h - 1, y0 + 1);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    return (Cv[u32(y0 * w + x0)] * (1.0 - tx) + Cv[u32(y0 * w + x1)] * tx) * (1.0 - ty) + (Cv[u32(y1 * w + x0)] * (1.0 - tx) + Cv[u32(y1 * w + x1)] * tx) * ty;
}
fn cnoise(kind: u32, x: f32, y: f32, m: f32) -> f32 {
    if (U.canvas_mode == 0u) { return pt_noise(kind, x + U.e.y, y + U.e.z); }
    let q: vec4f = cv_at(x, y);
    let w0: f32 = layer_weight(0u);
    let w1: f32 = layer_weight(1u);
    if (w1 == 0.0) { return pt_noise(kind, q.x, q.y); }
    if (w0 == 0.0) { return pt_noise(kind, q.z, q.w); }
    let n0: f32 = pt_noise(kind, q.x, q.y) - m;
    let n1: f32 = pt_noise(kind, q.z, q.w) - m;
    return m + (w0 * n0 + w1 * n1) / sqrt(w0 * w0 + w1 * w1);
}
fn dir_at(px: f32, py: f32) -> vec3f {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let fx: f32 = clamp(px, 0.0, f32(w - 1));
    let fy: f32 = clamp(py, 0.0, f32(h - 1));
    let x0: i32 = i32(floor(fx));
    let y0: i32 = i32(floor(fy));
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(h - 1, y0 + 1);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    return pt_flow((T[u32(y0 * w + x0)] * (1.0 - tx) + T[u32(y0 * w + x1)] * tx) * (1.0 - ty) + (T[u32(y1 * w + x0)] * (1.0 - tx) + T[u32(y1 * w + x1)] * tx) * ty);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let x: f32 = f32(i % U.width);
    let y: f32 = f32(i / U.width);
    let L: i32 = i32(U.stroke_len);
    var acc: f32 = cnoise(0u, x, y, 0.5) * f32(L + 1);
    var ids: f32 = cnoise(1u, x, y, 0.5) * f32(L + 1);
    var ws: f32 = f32(L + 1);
    for (var sg: i32 = 0; sg < 2; sg++) {
        let sgn: f32 = select(-1.0, 1.0, sg == 0);
        var px: f32 = x;
        var py: f32 = y;
        let d0: vec3f = dir_at(px, py);
        var tx: f32 = d0.x * sgn;
        var ty: f32 = d0.y * sgn;
        var cont: f32 = 1.0;
        for (var k: i32 = 1; k <= L; k++) {
            px = px + tx;
            py = py + ty;
            let nd: vec3f = dir_at(px, py);
            let d: f32 = nd.x * tx + nd.y * ty;
            tx = select(nd.x, -nd.x, d < 0.0);
            ty = select(nd.y, -nd.y, d < 0.0);
            let a: f32 = clamp((abs(d) - 0.05) / 0.25, 0.0, 1.0);
            cont = cont * a * a * (3.0 - 2.0 * a);
            let wk: f32 = f32(L + 1 - k) * cont;
            acc = acc + wk * cnoise(0u, px, py, 0.5);
            ids = ids + wk * cnoise(1u, px, py, 0.5);
            ws = ws + wk;
        }
    }
    Hs[i] = vec2f(acc / ws, ids / ws);
}

//@pass style_paint_relief
// The painterly shader: per pixel the XDoG line, the lit impasto (shade and sheen) and the
// watercolour wet edge; compose reads it bilinearly at the warped position.
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read> os: array<vec4f>;
@group(0) @binding(2) var<storage, read> Hs: array<vec2f>;
@group(0) @binding(3) var<storage, read> T: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> Rl: array<vec4f>;
@group(0) @binding(5) var<storage, read> Cv: array<vec4f>;
@group(0) @binding(6) var<storage, read> Kv: array<f32>;

fn layer_weight(k: u32) -> f32 {
    let age: f32 = Kv[5u * k];
    if (age < 0.0) { return 0.0; }
    return min(1.0, (age + 1.0) / 8.0) * max(0.02, 1.0 - Kv[5u * k + 3u] / 0.35);
}
fn cv_at(x: f32, y: f32) -> vec4f {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let fx: f32 = clamp(x, 0.0, f32(w - 1));
    let fy: f32 = clamp(y, 0.0, f32(h - 1));
    let x0: i32 = i32(floor(fx));
    let y0: i32 = i32(floor(fy));
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(h - 1, y0 + 1);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    return (Cv[u32(y0 * w + x0)] * (1.0 - tx) + Cv[u32(y0 * w + x1)] * tx) * (1.0 - ty) + (Cv[u32(y1 * w + x0)] * (1.0 - tx) + Cv[u32(y1 * w + x1)] * tx) * ty;
}
fn cnoise(kind: u32, x: f32, y: f32, m: f32) -> f32 {
    if (U.canvas_mode == 0u) { return pt_noise(kind, x + U.e.y, y + U.e.z); }
    let q: vec4f = cv_at(x, y);
    let w0: f32 = layer_weight(0u);
    let w1: f32 = layer_weight(1u);
    if (w1 == 0.0) { return pt_noise(kind, q.x, q.y); }
    if (w0 == 0.0) { return pt_noise(kind, q.z, q.w); }
    let n0: f32 = pt_noise(kind, q.x, q.y) - m;
    let n1: f32 = pt_noise(kind, q.z, q.w) - m;
    return m + (w0 * n0 + w1 * n1) / sqrt(w0 * w0 + w1 * w1);
}
fn xdog(x: i32, y: i32) -> f32 {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let sg: f32 = U.c.w;
    let k: f32 = 1.6;
    let R: i32 = i32(ceil(3.0 * sg * k));
    var a: f32 = 0.0;
    var b: f32 = 0.0;
    var wa: f32 = 0.0;
    var wb: f32 = 0.0;
    for (var dy: i32 = -R; dy <= R; dy++) {
        for (var dx: i32 = -R; dx <= R; dx++) {
            let q: vec3f = os[u32(clamp(y + dy, 0, h - 1) * w + clamp(x + dx, 0, w - 1))].xyz;
            let l: f32 = 0.2126 * q.x + 0.7152 * q.y + 0.0722 * q.z;
            let r2: f32 = f32(dx * dx + dy * dy);
            let g1: f32 = exp(-r2 / (2.0 * sg * sg));
            let g2: f32 = exp(-r2 / (2.0 * sg * sg * k * k));
            a = a + g1 * l;
            wa = wa + g1;
            b = b + g2 * l;
            wb = wb + g2;
        }
    }
    let D: f32 = a / wa - 0.985 * (b / wb);
    if (D >= -0.004) { return 1.0; }
    return 1.0 + tanh(U.d.x * (D + 0.004));
}
fn hs(x: i32, y: i32) -> f32 {
    let xx: i32 = clamp(x, 0, i32(U.width) - 1);
    let yy: i32 = clamp(y, 0, i32(U.height) - 1);
    return U.b.y * Hs[u32(yy * i32(U.width) + xx)].x + 0.12 * cnoise(2u, f32(xx), f32(yy), 0.5);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let x: i32 = i32(i % U.width);
    let y: i32 = i32(i / U.width);
    var lineq: f32 = 1.0;
    if (U.c.z > 0.0) { lineq = xdog(x, y); }
    let nx: f32 = -(hs(x + 1, y) - hs(x - 1, y)) * 2.5;
    let ny: f32 = -(hs(x, y + 1) - hs(x, y - 1)) * 2.5;
    let n: vec3f = vec3f(nx, ny, 1.0) / length(vec3f(nx, ny, 1.0));
    let L: vec3f = vec3f(-0.5, -0.6, 0.62) / length(vec3f(-0.5, -0.6, 0.62));
    let ndl: f32 = dot(n, L);
    let rz: f32 = 2.0 * ndl * n.z - L.z;
    Rl[i] = vec4f(lineq, 1.0 + 0.9 * (ndl - L.z), U.b.z * pow(max(0.0, rz), 24.0) * 0.35, pt_smooth(0.012, 0.05, sqrt(T[i].x + T[i].z)));
}

//@pass style_paint_compose
// The painterly shader's compose: warp, temperature push and broken colour in pigment space,
// Kubelka-Munk decode as a ratio, value massing, then the medium (lit impasto, or a watercolour
// glaze over paper) and the lines; sRGB to RGBA8.
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read> pig: array<f32>;
@group(0) @binding(2) var<storage, read> olat: array<vec4f>;
@group(0) @binding(3) var<storage, read> Hs: array<vec2f>;
@group(0) @binding(4) var<storage, read> Rl: array<vec4f>;
@group(0) @binding(5) var<storage, read> Cv: array<vec4f>;
@group(0) @binding(6) var<storage, read> Kv: array<f32>;
@group(0) @binding(7) var<storage, read_write> out8: array<u32>;

fn layer_weight(k: u32) -> f32 {
    let age: f32 = Kv[5u * k];
    if (age < 0.0) { return 0.0; }
    return min(1.0, (age + 1.0) / 8.0) * max(0.02, 1.0 - Kv[5u * k + 3u] / 0.35);
}
fn bil_index(x: f32, y: f32) -> vec4f {
    let fx: f32 = clamp(x, 0.0, f32(i32(U.width) - 1));
    let fy: f32 = clamp(y, 0.0, f32(i32(U.height) - 1));
    return vec4f(floor(fx), floor(fy), fx - floor(fx), fy - floor(fy));
}
fn cv_at(x: f32, y: f32) -> vec4f {
    let w: i32 = i32(U.width);
    let b: vec4f = bil_index(x, y);
    let x0: i32 = i32(b.x);
    let y0: i32 = i32(b.y);
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(i32(U.height) - 1, y0 + 1);
    return (Cv[u32(y0 * w + x0)] * (1.0 - b.z) + Cv[u32(y0 * w + x1)] * b.z) * (1.0 - b.w) + (Cv[u32(y1 * w + x0)] * (1.0 - b.z) + Cv[u32(y1 * w + x1)] * b.z) * b.w;
}
fn cnoise(kind: u32, x: f32, y: f32, m: f32) -> f32 {
    if (U.canvas_mode == 0u) { return pt_noise(kind, x + U.e.y, y + U.e.z); }
    let q: vec4f = cv_at(x, y);
    let w0: f32 = layer_weight(0u);
    let w1: f32 = layer_weight(1u);
    if (w1 == 0.0) { return pt_noise(kind, q.x, q.y); }
    if (w0 == 0.0) { return pt_noise(kind, q.z, q.w); }
    let n0: f32 = pt_noise(kind, q.x, q.y) - m;
    let n1: f32 = pt_noise(kind, q.z, q.w) - m;
    return m + (w0 * n0 + w1 * n1) / sqrt(w0 * w0 + w1 * w1);
}
fn lat_at(x: f32, y: f32, c: u32) -> vec4f {
    let w: i32 = i32(U.width);
    let b: vec4f = bil_index(x, y);
    let x0: i32 = i32(b.x);
    let y0: i32 = i32(b.y);
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(i32(U.height) - 1, y0 + 1);
    return (olat[2u * u32(y0 * w + x0) + c] * (1.0 - b.z) + olat[2u * u32(y0 * w + x1) + c] * b.z) * (1.0 - b.w) + (olat[2u * u32(y1 * w + x0) + c] * (1.0 - b.z) + olat[2u * u32(y1 * w + x1) + c] * b.z) * b.w;
}
fn hs_at(x: f32, y: f32) -> vec2f {
    let w: i32 = i32(U.width);
    let b: vec4f = bil_index(x, y);
    let x0: i32 = i32(b.x);
    let y0: i32 = i32(b.y);
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(i32(U.height) - 1, y0 + 1);
    return (Hs[u32(y0 * w + x0)] * (1.0 - b.z) + Hs[u32(y0 * w + x1)] * b.z) * (1.0 - b.w) + (Hs[u32(y1 * w + x0)] * (1.0 - b.z) + Hs[u32(y1 * w + x1)] * b.z) * b.w;
}
fn rl_at(x: f32, y: f32) -> vec4f {
    let w: i32 = i32(U.width);
    let b: vec4f = bil_index(x, y);
    let x0: i32 = i32(b.x);
    let y0: i32 = i32(b.y);
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(i32(U.height) - 1, y0 + 1);
    return (Rl[u32(y0 * w + x0)] * (1.0 - b.z) + Rl[u32(y0 * w + x1)] * b.z) * (1.0 - b.w) + (Rl[u32(y1 * w + x0)] * (1.0 - b.z) + Rl[u32(y1 * w + x1)] * b.z) * b.w;
}
fn km_decode(c: vec4f) -> vec3f {
    var o: vec3f = vec3f(0.0);
    for (var w: u32 = 0u; w < 31u; w++) {
        let K: f32 = dot(c, vec4f(pig[4u * w], pig[4u * w + 1u], pig[4u * w + 2u], pig[4u * w + 3u]));
        let S: f32 = dot(c, vec4f(pig[124u + 4u * w], pig[124u + 4u * w + 1u], pig[124u + 4u * w + 2u], pig[124u + 4u * w + 3u]));
        let q: f32 = K / max(S, 1.0e-9);
        let R: f32 = 1.0 + q - sqrt(q * q + 2.0 * q);
        o = o + R * vec3f(pig[248u + 3u * w], pig[248u + 3u * w + 1u], pig[248u + 3u * w + 2u]);
    }
    return o;
}
fn glaze(c: vec4f, X: f32, Rg: f32) -> vec3f {
    var o: vec3f = vec3f(0.0);
    for (var w: u32 = 0u; w < 31u; w++) {
        let K: f32 = dot(c, vec4f(pig[4u * w], pig[4u * w + 1u], pig[4u * w + 2u], pig[4u * w + 3u]));
        let S: f32 = max(dot(c, vec4f(pig[124u + 4u * w], pig[124u + 4u * w + 1u], pig[124u + 4u * w + 2u], pig[124u + 4u * w + 3u])), 1.0e-6);
        // sinh and cosh from one exp: WARP's sinh and cosh were off by about 1e-4 relative at z
        // near 13, enough to flip a nearly cancelled channel (yellow's blue), whose glaze ratio is
        // floored at 1e-4 and so magnifies an absolute error by 1e4 (bounds-11 runs, first WARP run).
        // b and a small z keep f32 precision in forms equal to the reference's.
        let ks: f32 = K / S;
        let a: f32 = 1.0 + ks;
        let b: f32 = sqrt(ks * (2.0 + ks));
        let z: f32 = min(b * S * X, 30.0);
        let ez: f32 = exp(z);
        var sh: f32 = 0.5 * (ez - 1.0 / ez);
        if (z < 0.1) { sh = z * (1.0 + z * z * (1.0 / 6.0 + z * z / 120.0)); }
        let chh: f32 = 0.5 * (ez + 1.0 / ez);
        let den: f32 = a * sh + b * chh;
        let R: f32 = sh / den;
        let Tt: f32 = b / den;
        let Rt: f32 = R + (Tt * Tt * Rg) / (1.0 - R * Rg);
        o = o + Rt * vec3f(pig[248u + 3u * w], pig[248u + 3u * w + 1u], pig[248u + 3u * w + 2u]);
    }
    return o;
}
fn wpow(v: f32, e: f32) -> f32 {
    if (v <= 0.0) { return 0.0; }
    return pow(v, e);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let i: u32 = gidx;
    if (i >= U.width * U.height) { return; }
    let x: f32 = f32(i % U.width);
    let y: f32 = f32(i / U.width);
    var wv: vec2f = vec2f(0.0);
    if (U.d.y > 0.0) { wv = vec2f(cnoise(5u, x, y, 0.0), cnoise(6u, x, y, 0.0)); }
    let sx: f32 = x + wv.x * U.d.y;
    let sy: f32 = y + wv.y * U.d.y;
    var c: vec4f = lat_at(sx, sy, 0u);
    let res: vec3f = lat_at(sx, sy, 1u).xyz;
    let dec0: vec3f = km_decode(c);
    let D: vec3f = max(dec0 + res, vec3f(0.0));
    let v: f32 = clamp(0.2126 * D.x + 0.7152 * D.y + 0.0722 * D.z, 0.0, 1.0);
    let mx: f32 = max(D.x, max(D.y, D.z));
    let mn: f32 = min(D.x, min(D.y, D.z));
    let keep: f32 = pt_smooth(0.15, 0.6, (mx - mn) / max(mx, 1.0e-3));
    let pk: f32 = U.b.w * (1.0 - 0.85 * keep);
    c.w = c.w + pk * (1.0 - v) * (1.0 - v);
    c.z = c.z + 0.6 * pk * (1.0 - v) * (1.0 - v);
    c.y = c.y + pk * 0.08 * v * v;
    let jit: f32 = (hs_at(sx, sy).y - 0.5) * 2.0;
    let rel: vec4f = rl_at(sx, sy);
    let bk: f32 = U.c.x * (1.0 - 0.85 * v * v) * (1.0 - 0.7 * keep);
    c.y = c.y + bk * max(jit, 0.0);
    c.w = c.w + bk * max(-jit, 0.0);
    c.z = c.z + 0.5 * bk * abs(jit) * (1.0 - v);
    c = c / (c.x + c.y + c.z + c.w);
    var rgb: vec3f = D * min(vec3f(4.0), (km_decode(c) + vec3f(0.01)) / (dec0 + vec3f(0.01)));
    if (U.c.y > 0.0) {
        let vy: f32 = 0.2126 * rgb.x + 0.7152 * rgb.y + 0.0722 * rgb.z;
        let f: f32 = min(vy, 1.0) * U.c.y;
        let fl: f32 = floor(f);
        let vq: f32 = max(min(vy, 0.02), (fl + pt_smooth(0.3, 0.7, f - fl)) / U.c.y);
        let g: f32 = 1.0 + (vq / max(vy, 1.0e-6) - 1.0) * (1.0 - 0.6 * keep);
        rgb = rgb * g;
    }
    let paper: f32 = cnoise(3u, x, y, 0.5);
    if (U.medium == 2u) {
        let valley: f32 = cnoise(4u, x, y, 0.5);
        let Rg: f32 = 0.86 * (0.93 + 0.07 * paper);
        let X0: f32 = 1.5;
        let X: f32 = X0 * (1.0 + U.d.z * (0.5 - valley) * 2.0) * (1.0 + U.d.w * rel.w);
        let g0: vec3f = glaze(c, X0, 0.86);
        let g1: vec3f = glaze(c, X, Rg);
        let lw: vec3f = vec3f(0.2126, 0.7152, 0.0722);
        let sr: f32 = dot(lw, g1) / max(dot(lw, g0), 1.0e-4);
        let ratio: vec3f = clamp(vec3f(sr) + (g1 / max(g0, vec3f(1.0e-4)) - vec3f(sr)) * keep, vec3f(0.5), vec3f(2.0));
        let wc: vec3f = clamp(rgb * ratio, vec3f(0.0), vec3f(1.0));
        rgb = vec3f(wpow(wc.x, U.e.x), wpow(wc.y, U.e.x), wpow(wc.z, U.e.x));
    } else {
        rgb = rgb * rel.y + vec3f(rel.z);
    }
    let t: f32 = 1.0 - U.c.z * (1.0 - rel.x);
    // alpha carries the line factor for P23's defocus; colour checks skip alpha
    out8[i] = (pack_rgb8(max(rgb * t, vec3f(0.0))) & 0x00ffffffu) | (u32(floor(clamp(t, 0.0, 1.0) * 255.0 + 0.5)) << 24u);
}

//@pass style_canvas_decide
// The advected canvas: one invocation reads last frame's distortion sums, ages the two layers and
// regenerates a distorted one with a fresh seed. K: per layer age, seed x, seed y, mean
// distortion, reset (5 a layer), then the frame count at 10. S: distortion sums and counts.
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read_write> K: array<f32>;
@group(0) @binding(2) var<storage, read_write> S: array<u32>;

@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    if (gidx > 0u || U.width == 0u) { return; }
    let frame: f32 = K[10];
    for (var k: u32 = 0u; k < 2u; k++) {
        let n: u32 = S[2u + k];
        var md: f32 = 0.0;
        if (n > 0u) { md = f32(S[k]) / 4096.0 / f32(n); }
        K[5u * k + 3u] = md;
        if (K[5u * k] >= 0.0) { K[5u * k] = min(1.0e6, K[5u * k] + 1.0); }
        K[5u * k + 4u] = 0.0;
    }
    // The layer to wake (2: none). Written once after the choice: dxc refuses a UAV store from a
    // helper called inside the search loop.
    var wk: u32 = 2u;
    if (frame == 0.0) {
        wk = 0u;
    } else {
        if (K[0] >= 0.0 && K[3] > 0.35 && (K[5] < 0.0 || (K[5] >= 12.0 && K[8] > 0.175))) { wk = 1u; }
        else if (K[5] >= 0.0 && K[8] > 0.35 && (K[0] < 0.0 || (K[0] >= 12.0 && K[3] > 0.175))) { wk = 0u; }
    }
    if (wk < 2u) {
        K[5u * wk] = 0.0;
        K[5u * wk + 3u] = 0.0;
        K[5u * wk + 4u] = 1.0;
        K[5u * wk + 1u] = hash3(u32(frame), wk, 101u) * 997.0;
        K[5u * wk + 2u] = hash3(u32(frame), wk, 103u) * 997.0;
    }
    if (frame == 0.0) { K[0] = 8.0; }
    for (var j: u32 = 0u; j < 4u; j++) { S[j] = 0u; }
    K[10] = frame + 1.0;
}

//@pass style_canvas_advect
// The advected canvas: each pixel fetches last frame's canvas coordinates along its motion vector
// (bilinear), or starts fresh at the pixel plus the layer seed where reprojection fails the depth
// test or the layer was regenerated.
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read> K: array<f32>;
@group(0) @binding(2) var<storage, read> Cprev: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> Cnew: array<vec4f>;
@group(0) @binding(4) var<storage, read> mv: array<vec2f>;
@group(0) @binding(5) var<storage, read> dd: array<vec2f>;
@group(0) @binding(6) var<storage, read> pd_in: array<f32>;
@group(0) @binding(7) var<storage, read_write> pd_out: array<f32>;

fn cprev(x: f32, y: f32) -> vec4f {
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let fx: f32 = clamp(x, 0.0, f32(w - 1));
    let fy: f32 = clamp(y, 0.0, f32(h - 1));
    let x0: i32 = i32(floor(fx));
    let y0: i32 = i32(floor(fy));
    let x1: i32 = min(w - 1, x0 + 1);
    let y1: i32 = min(h - 1, y0 + 1);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    return (Cprev[u32(y0 * w + x0)] * (1.0 - tx) + Cprev[u32(y0 * w + x1)] * tx) * (1.0 - ty) + (Cprev[u32(y1 * w + x0)] * (1.0 - tx) + Cprev[u32(y1 * w + x1)] * tx) * ty;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let p: u32 = gidx;
    if (p >= U.width * U.height) { return; }
    let w: i32 = i32(U.width);
    let h: i32 = i32(U.height);
    let x: f32 = f32(p % U.width);
    let y: f32 = f32(p / U.width);
    let px: f32 = x - mv[p].x;
    let py: f32 = y - mv[p].y;
    let q: u32 = u32(clamp(i32(floor(py + 0.5)), 0, h - 1) * w + clamp(i32(floor(px + 0.5)), 0, w - 1));
    let inside: bool = px >= 0.0 && py >= 0.0 && px <= f32(w - 1) && py <= f32(h - 1);
    let valid: bool = inside && pd_in[q] > 0.0 && abs(pd_in[q] - dd[p].y) <= 0.02 * dd[p].y;
    let old: vec4f = cprev(px, py);
    var o: vec4f = old;
    if (K[4] > 0.5 || !valid) {
        o.x = x + K[1];
        o.y = y + K[2];
    }
    if (K[9] > 0.5 || !valid) {
        o.z = x + K[6];
        o.w = y + K[7];
    }
    Cnew[p] = o;
    pd_out[p] = dd[p].x;
}

//@pass style_canvas_distort
// The advected canvas: each layer's mean distortion (the departure of the coordinate field's local
// scale from 1, rotation free), summed as integers so CPU and GPU sums agree.
@group(0) @binding(0) var<uniform> U: PaintGpuParams;
@group(0) @binding(1) var<storage, read> Cn: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> S: array<atomic<u32>>;

fn departure(a: f32, b: f32, c: f32, d: f32) -> f32 {
    let E: f32 = a * a + b * b + c * c + d * d;
    let det: f32 = abs(a * d - b * c);
    let disc: f32 = sqrt(max(0.0, E * E - 4.0 * det * det));
    let s1: f32 = sqrt(max(1.0e-12, (E + disc) / 2.0));
    let s2: f32 = sqrt(max(1.0e-12, (E - disc) / 2.0));
    return abs(log(s1)) + abs(log(s2));
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let p: u32 = gidx;
    if (p >= U.width * U.height) { return; }
    let w: u32 = U.width;
    let x: u32 = p % w;
    let y: u32 = p / w;
    if (x < 1u || y < 1u || x >= w - 1u || y >= U.height - 1u) { return; }
    let r: vec4f = Cn[p + 1u];
    let l: vec4f = Cn[p - 1u];
    let dn: vec4f = Cn[p + w];
    let up: vec4f = Cn[p - w];
    let D0: f32 = departure((r.x - l.x) / 2.0, (dn.x - up.x) / 2.0, (r.y - l.y) / 2.0, (dn.y - up.y) / 2.0);
    if (D0 <= 1.0) {
        atomicAdd(&S[0], u32(floor(D0 * 4096.0)));
        atomicAdd(&S[2], 1u);
    }
    let D1: f32 = departure((r.z - l.z) / 2.0, (dn.z - up.z) / 2.0, (r.w - l.w) / 2.0, (dn.w - up.w) / 2.0);
    if (D1 <= 1.0) {
        atomicAdd(&S[1], u32(floor(D1 * 4096.0)));
        atomicAdd(&S[3], 1u);
    }
}

//@pass style_crtp_encode
// The physical CRT (raw/renderer/style_crt.hpp), signal pass 1: a line of the source sampled at
// 4 fsc; RGB through the video bandwidth, or luma and the modulated chroma on the wire.
@group(0) @binding(0) var<uniform> U: CrtpGpuParams;
@group(0) @binding(1) var<storage, read> P: array<f32>;
@group(0) @binding(2) var<storage, read> src: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> enc: array<vec4f>;

fn cp_i(i: u32) -> i32 {
    return i32(floor(P[i] + 0.5));
}
fn frac1(x: f32) -> f32 {
    return x - floor(x);
}
fn phase(k: i32, y: i32) -> f32 {
    return 6.283185307179586 * (f32(k & 3) / 4.0 + frac1(f32(y) * P[CP_lineCyc]) + frac1(f32(U.frame) * P[CP_frameCyc]));
}
fn pal_sign(y: i32) -> f32 {
    return select(-1.0, 1.0, (y & 1) == 0);
}
fn tap(which: u32, j: i32) -> f32 {
    return P[CP_taps + u32(cp_i(which) + j + cp_i(which + 1u))];
}
fn held(y: i32, k: i32, c: u32) -> f32 {
    let n: i32 = cp_i(CP_N);
    let kk: i32 = clamp(k, 0, n - 1);
    let sw: i32 = cp_i(CP_srcW);
    let x: i32 = min(sw - 1, ((2 * kk + 1) * sw) / (2 * n));
    let v: vec4f = src[u32(y * sw + x)];
    if (c == 0u) { return v.x; }
    if (c == 1u) { return v.y; }
    return v.z;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let n: i32 = cp_i(CP_N);
    let nl: i32 = cp_i(CP_lines);
    let idx: i32 = i32(gidx);
    if (idx >= n * nl) { return; }
    let k: i32 = idx % n;
    let y: i32 = idx / n;
    if (cp_i(CP_mode) == 0) {
        let hr: i32 = cp_i(CP_tapRgb + 1u);
        var s: vec3f = vec3f(0.0);
        for (var jr: i32 = -hr; jr <= hr; jr++) {
            let t: f32 = tap(CP_tapRgb, jr);
            s = s + t * vec3f(held(y, k + jr, 0u), held(y, k + jr, 1u), held(y, k + jr, 2u));
        }
        enc[u32(idx)] = vec4f(s, 0.0);
        return;
    }
    let r: f32 = held(y, k, 0u);
    let g: f32 = held(y, k, 1u);
    let b: f32 = held(y, k, 2u);
    let h: i32 = cp_i(CP_tapEnc + 1u);
    var a: f32 = 0.0;
    var q: f32 = 0.0;
    for (var j: i32 = -h; j <= h; j++) {
        let rr: f32 = held(y, k + j, 0u);
        let gg: f32 = held(y, k + j, 1u);
        let bb: f32 = held(y, k + j, 2u);
        let ca: f32 = P[CP_fwd + 3u] * rr + P[CP_fwd + 4u] * gg + P[CP_fwd + 5u] * bb;
        let cq: f32 = P[CP_fwd + 6u] * rr + P[CP_fwd + 7u] * gg + P[CP_fwd + 8u] * bb;
        let t: f32 = tap(CP_tapEnc, j);
        a = a + t * ca;
        q = q + t * cq;
    }
    let ph: f32 = phase(k, y);
    var c: f32 = a * cos(ph) + q * sin(ph);
    if (cp_i(CP_pal) == 1) { c = a * sin(ph) + pal_sign(y) * q * cos(ph); }
    enc[u32(idx)] = vec4f(0.299 * r + 0.587 * g + 0.114 * b, c, 0.0, 0.0);
}

//@pass style_crtp_separate
// Signal pass 2: the decoder separates luma and chroma with a one-line comb or a notch.
@group(0) @binding(0) var<uniform> U: CrtpGpuParams;
@group(0) @binding(1) var<storage, read> P: array<f32>;
@group(0) @binding(2) var<storage, read> enc: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> sep: array<vec4f>;

fn cp_i(i: u32) -> i32 {
    return i32(floor(P[i] + 0.5));
}
fn tap(which: u32, j: i32) -> f32 {
    return P[CP_taps + u32(cp_i(which) + j + cp_i(which + 1u))];
}
fn comp(y: i32, k: i32) -> f32 {
    let n: i32 = cp_i(CP_N);
    let e: vec4f = enc[u32(y * n + clamp(k, 0, n - 1))];
    return e.x + e.y;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let n: i32 = cp_i(CP_N);
    let nl: i32 = cp_i(CP_lines);
    let idx: i32 = i32(gidx);
    if (idx >= n * nl || U.pad0 == 7u) { return; }
    let k: i32 = idx % n;
    let y: i32 = idx / n;
    if (cp_i(CP_mode) == 1) {
        sep[u32(idx)] = vec4f(enc[u32(idx)].x, enc[u32(idx)].y, 0.0, 0.0);
        return;
    }
    var yl: f32 = 0.0;
    if (cp_i(CP_comb) == 1) {
        let y2: i32 = select(min(1, nl - 1), y - 1, y > 0);
        yl = 0.5 * (comp(y, k) + comp(y2, k));
    } else {
        let h: i32 = cp_i(CP_tapSep + 1u);
        for (var j: i32 = -h; j <= h; j++) {
            yl = yl + tap(CP_tapSep, j) * comp(y, k + j);
        }
    }
    sep[u32(idx)] = vec4f(yl, comp(y, k) - yl, 0.0, 0.0);
}

//@pass style_crtp_demod
// Signal pass 3: the chroma demodulated with the I and Q (or U and V) low-passes, back to R'G'B'.
@group(0) @binding(0) var<uniform> U: CrtpGpuParams;
@group(0) @binding(1) var<storage, read> P: array<f32>;
@group(0) @binding(2) var<storage, read> sep: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> sig: array<vec4f>;

fn cp_i(i: u32) -> i32 {
    return i32(floor(P[i] + 0.5));
}
fn frac1(x: f32) -> f32 {
    return x - floor(x);
}
fn phase(k: i32, y: i32) -> f32 {
    return 6.283185307179586 * (f32(k & 3) / 4.0 + frac1(f32(y) * P[CP_lineCyc]) + frac1(f32(U.frame) * P[CP_frameCyc]));
}
fn pal_sign(y: i32) -> f32 {
    return select(-1.0, 1.0, (y & 1) == 0);
}
fn tap(which: u32, j: i32) -> f32 {
    return P[CP_taps + u32(cp_i(which) + j + cp_i(which + 1u))];
}
fn chroma(y: i32, k: i32) -> f32 {
    let n: i32 = cp_i(CP_N);
    return sep[u32(y * n + clamp(k, 0, n - 1))].y;
}
fn demod_line(y: i32, k: i32) -> vec2f {
    let pal: bool = cp_i(CP_pal) == 1;
    var a: f32 = 0.0;
    var b: f32 = 0.0;
    let hi: i32 = cp_i(CP_tapI + 1u);
    for (var j: i32 = -hi; j <= hi; j++) {
        let ph: f32 = phase(k + j, y);
        a = a + tap(CP_tapI, j) * 2.0 * chroma(y, k + j) * select(cos(ph), sin(ph), pal);
    }
    let hq: i32 = cp_i(CP_tapQ + 1u);
    for (var jq: i32 = -hq; jq <= hq; jq++) {
        let phq: f32 = phase(k + jq, y);
        b = b + tap(CP_tapQ, jq) * 2.0 * chroma(y, k + jq) * select(sin(phq), pal_sign(y) * cos(phq), pal);
    }
    return vec2f(a, b);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let n: i32 = cp_i(CP_N);
    let nl: i32 = cp_i(CP_lines);
    let idx: i32 = i32(gidx);
    if (idx >= n * nl) { return; }
    let k: i32 = idx % n;
    let y: i32 = idx / n;
    let h: i32 = cp_i(CP_tapLum + 1u);
    var yy: f32 = 0.0;
    for (var j: i32 = -h; j <= h; j++) {
        yy = yy + tap(CP_tapLum, j) * sep[u32(y * n + clamp(k + j, 0, n - 1))].x;
    }
    var ab: vec2f = demod_line(y, k);
    if (cp_i(CP_pal) == 1 && y > 0) { ab = 0.5 * (ab + demod_line(y - 1, k)); }
    let m: u32 = CP_inv;
    sig[u32(idx)] = vec4f(P[m] * yy + P[m + 1u] * ab.x + P[m + 2u] * ab.y, P[m + 3u] * yy + P[m + 4u] * ab.x + P[m + 5u] * ab.y, P[m + 6u] * yy + P[m + 7u] * ab.x + P[m + 8u] * ab.y, 0.0);
}

//@pass style_crtp_beam
// The beams and the phosphor: each gun's current through the tube's power law, a gaussian spot
// that grows with current integrated over the output pixel's footprint on the curved face, the
// mask, and two-term phosphor decay with the history carried between frames.
@group(0) @binding(0) var<uniform> U: CrtpGpuParams;
@group(0) @binding(1) var<storage, read> P: array<f32>;
@group(0) @binding(2) var<storage, read> sig: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> hist: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> em: array<vec4f>;

fn cp_i(i: u32) -> i32 {
    return i32(floor(P[i] + 0.5));
}
fn face_at(px: f32, py: f32) -> vec2f {
    let x: f32 = (px - P[CP_outW] / 2.0) * (P[CP_viewW] / P[CP_outW]) + P[CP_cx];
    let y: f32 = (py - P[CP_outH] / 2.0) * (P[CP_viewH] / P[CP_outH]) + P[CP_cy];
    let d: f32 = P[CP_dist];
    let a: f32 = x * x / (2.0 * P[CP_rx]) + y * y / (2.0 * P[CP_ry]);
    let t: f32 = 2.0 * d / (d + sqrt(max(0.0, d * d - 4.0 * a * d)));
    return vec2f(t * x, t * y);
}
fn footprint(px: f32, py: f32) -> vec4f {
    let c: vec2f = face_at(px + 0.5, py + 0.5);
    let xp: vec2f = face_at(px + 1.0, py + 0.5);
    let xm: vec2f = face_at(px, py + 0.5);
    let yp: vec2f = face_at(px + 0.5, py + 1.0);
    let ym: vec2f = face_at(px + 0.5, py);
    let hx: f32 = 0.5 * (abs(xp.x - xm.x) + abs(yp.x - ym.x));
    let hy: f32 = 0.5 * (abs(xp.y - xm.y) + abs(yp.y - ym.y));
    return vec4f(c, max(hx, 1.0e-4), max(hy, 1.0e-4));
}
fn face_alpha(sx: f32, sy: f32, hx: f32, hy: f32) -> f32 {
    let r: f32 = P[CP_corner];
    let qx: f32 = abs(sx) - (P[CP_faceW] / 2.0 - r);
    let qy: f32 = abs(sy) - (P[CP_faceH] / 2.0 - r);
    let d: f32 = length(vec2f(max(qx, 0.0), max(qy, 0.0))) + min(max(qx, qy), 0.0) - r;
    return clamp(0.5 - d / (2.0 * max(hx, hy)), 0.0, 1.0);
}
fn dampen(cov0: f32, sy: f32, hy: f32) -> f32 {
    var cov: f32 = cov0;
    if (cp_i(CP_maskType) != 1) { return cov; }
    for (var i: i32 = 0; i < cp_i(CP_nDampers); i++) {
        let dc: f32 = (P[CP_dampers + u32(i)] - 0.5) * P[CP_faceH];
        let lo: f32 = max(sy - hy, dc - P[CP_damperW] / 2.0);
        let hi: f32 = min(sy + hy, dc + P[CP_damperW] / 2.0);
        if (hi > lo) { cov = cov * (1.0 - (hi - lo) / (2.0 * hy)); }
    }
    return cov;
}
fn stripe_mask(sx: f32, sy: f32, hx: f32, hy: f32, c: u32) -> f32 {
    let pp: f32 = P[CP_pitch];
    let fill: f32 = P[CP_fill];
    let w: f32 = fill * pp / 3.0;
    let s: f32 = f32(c) * pp / 3.0 + (1.0 - fill) * pp / 6.0;
    let a: f32 = 2.0 * hx;
    let m0: i32 = i32(floor((sx - a - s - w) / pp));
    let m1: i32 = i32(floor((sx + a - s) / pp));
    if (m1 - m0 > 32) { return P[CP_maskMean]; }
    var cov: f32 = 0.0;
    for (var k: i32 = m0; k <= m1; k++) {
        let x0: f32 = f32(k) * pp + s - sx;
        let xw: f32 = cp_tent_cdf(x0 + w, a) - cp_tent_cdf(x0, a);
        if (xw <= 0.0) { continue; }
        var yw: f32 = 1.0;
        if (cp_i(CP_maskType) == 2) {
            let py: f32 = P[CP_slotH] + P[CP_slotGap];
            yw = cp_pulse_box(sy - hy, sy + hy, py, P[CP_slotH], f32(k & 1) * py * 0.5);
        }
        cov = cov + xw * yw;
    }
    return dampen(cov, sy, hy);
}
fn pixel_stripe_mask(px: f32, py: f32, sy: f32, hy: f32, c: u32) -> f32 {
    let pp: f32 = P[CP_pitchPx];
    let fill: f32 = P[CP_fill];
    let w: f32 = fill * pp / 3.0;
    let s: f32 = f32(c) * pp / 3.0 + (1.0 - fill) * pp / 6.0;
    let k0: i32 = i32(floor((px - s - w) / pp));
    let k1: i32 = i32(floor((px + 1.0 - s) / pp));
    var cov: f32 = 0.0;
    for (var k: i32 = k0; k <= k1; k++) {
        let x0: f32 = max(px, f32(k) * pp + s);
        let x1: f32 = min(px + 1.0, f32(k) * pp + s + w);
        if (x1 <= x0) { continue; }
        var yw: f32 = 1.0;
        if (cp_i(CP_maskType) == 2) {
            let u: f32 = pp / P[CP_pitch];
            let pyy: f32 = (P[CP_slotH] + P[CP_slotGap]) * u;
            yw = cp_pulse_box(py, py + 1.0, pyy, P[CP_slotH] * u, f32(k & 1) * pyy * 0.5);
        }
        cov = cov + (x1 - x0) * yw;
    }
    return dampen(cov, sy, hy);
}
fn delta_mask(sx: f32, sy: f32, hx: f32, hy: f32) -> vec3f {
    let d: f32 = P[CP_pitch] / sqrt(3.0);
    let r: f32 = P[CP_fill] * d / 2.0;
    let rowh: f32 = d * sqrt(3.0) / 2.0;
    var o: vec3f = vec3f(0.0);
    let aa: f32 = min(0.75 * (hx + hy), r);
    for (var j: i32 = 0; j < 4; j++) {
        for (var i: i32 = 0; i < 4; i++) {
            let x: f32 = sx + ((f32(i) + 0.5) / 4.0 - 0.5) * 3.0 * hx;
            let y: f32 = sy + ((f32(j) + 0.5) / 4.0 - 0.5) * 3.0 * hy;
            let lj: i32 = i32(floor(y / rowh));
            for (var dj: i32 = 0; dj <= 1; dj++) {
                let jj: i32 = lj + dj;
                let li: i32 = i32(floor(x / d - f32(jj) / 2.0));
                for (var di: i32 = 0; di <= 1; di++) {
                    let ii: i32 = li + di;
                    let cx: f32 = (f32(ii) + f32(jj) / 2.0) * d;
                    let cy: f32 = f32(jj) * rowh;
                    let e: f32 = clamp(0.5 + (r - length(vec2f(x - cx, y - cy))) / aa, 0.0, 1.0);
                    let col: i32 = (((ii - jj) % 3) + 3) % 3;
                    if (e > 0.0 && col == 0) { o.x = o.x + e / 16.0; }
                    if (e > 0.0 && col == 1) { o.y = o.y + e / 16.0; }
                    if (e > 0.0 && col == 2) { o.z = o.z + e / 16.0; }
                }
            }
        }
    }
    let t: f32 = clamp((hx / P[CP_pitch] - 0.7) / 0.3, 0.0, 1.0);
    let bl: f32 = t * t * (3.0 - 2.0 * t);
    return o + (vec3f(P[CP_maskMean]) - o) * bl;
}
fn mask_cover(sx: f32, sy: f32, hx: f32, hy: f32, px: f32, py: f32) -> vec3f {
    let t: i32 = cp_i(CP_maskType);
    if (t == 0) { return vec3f(1.0); }
    var cov: vec3f = vec3f(stripe_mask(sx, sy, hx, hy, 0u), stripe_mask(sx, sy, hx, hy, 1u), stripe_mask(sx, sy, hx, hy, 2u));
    if (t == 3) { cov = delta_mask(sx, sy, hx, hy); }
    if (t != 3 && cp_i(CP_maskLock) == 1) { cov = vec3f(pixel_stripe_mask(px, py, sy, hy, 0u), pixel_stripe_mask(px, py, sy, hy, 1u), pixel_stripe_mask(px, py, sy, hy, 2u)); }
    let mx: f32 = P[CP_maskMix];
    return vec3f(1.0 - mx) + mx * cov / P[CP_maskMean];
}
fn gun_current(v: f32) -> f32 {
    return pow(max(P[CP_contrast] * v + P[CP_brightness], 0.0), P[CP_gamma]);
}
fn spot_sigma(i: f32) -> f32 {
    return P[CP_lineMm] * (P[CP_spotLo] + (P[CP_spotHi] - P[CP_spotLo]) * pow(min(i, 1.5), P[CP_spotExp]));
}
fn drive(c: u32, j: i32, k: i32) -> f32 {
    let s: vec4f = sig[u32(j * cp_i(CP_N) + k)];
    if (cp_i(CP_mono) == 1) { return 0.299 * s.x + 0.587 * s.y + 0.114 * s.z; }
    if (c == 0u) { return s.x; }
    if (c == 1u) { return s.y; }
    return s.z;
}
fn deposit(c: u32, rx: f32, ry: f32, hx: f32, hy: f32) -> f32 {
    let n: i32 = cp_i(CP_N);
    let nl: i32 = cp_i(CP_lines);
    let lmm: f32 = P[CP_lineMm];
    let rw: f32 = P[CP_rasterW];
    let rh: f32 = P[CP_rasterH];
    let dx: f32 = rw / f32(n);
    let smax: f32 = spot_sigma(1.5);
    let reachy: f32 = 3.0 * smax + hy;
    let reachx: f32 = 3.0 * smax + hx;
    let yl: f32 = (ry + rh / 2.0) / lmm - 0.5;
    let j0: i32 = max(0, i32(floor(yl - reachy / lmm)));
    let j1: i32 = min(nl - 1, i32(ceil(yl + reachy / lmm)));
    let kc: f32 = (rx + rw / 2.0) / dx - 0.5;
    let k0: i32 = max(0, i32(floor(kc - reachx / dx)));
    let k1: i32 = min(n - 1, i32(ceil(kc + reachx / dx)));
    let fld: i32 = i32(U.frame);
    var e: f32 = 0.0;
    for (var j: i32 = j0; j <= j1; j++) {
        if (cp_i(CP_interlace) == 1 && (j & 1) != (fld & 1)) { continue; }
        let yj: f32 = -rh / 2.0 + (f32(j) + 0.5) * lmm;
        for (var k: i32 = k0; k <= k1; k++) {
            let cur: f32 = gun_current(drive(c, j, k));
            if (cur <= 0.0) { continue; }
            let s: f32 = spot_sigma(cur);
            let xk: f32 = -rw / 2.0 + (f32(k) + 0.5) * dx;
            e = e + cur * cp_gauss_mass(rx - hx, rx + hx, xk, s) * cp_gauss_mass(ry - hy, ry + hy, yj, s);
        }
    }
    return e * dx * lmm / (4.0 * hx * hy);
}
// One component c: (emission this frame, history term 1, history term 2).
fn component(c: u32, e: f32, r1: f32, r2: f32, t0: f32) -> vec3f {
    let tt: f32 = P[CP_fieldT];
    let b: u32 = CP_decay + 5u * c;
    let w: f32 = P[b];
    let e1: f32 = P[b + 1u];
    let e2: f32 = P[b + 2u];
    let n1: f32 = exp(-(tt - t0) / P[b + 3u]);
    let n2: f32 = exp(-(tt - t0) / P[b + 4u]);
    return vec3f(r1 * (1.0 - e1) + r2 * (1.0 - e2) + e * (w * (1.0 - n1) + (1.0 - w) * (1.0 - n2)), r1 * e1 + e * w * n1, r2 * e2 + e * (1.0 - w) * n2);
}
fn conv_off(c: u32, sx: f32, sy: f32) -> vec2f {
    let k: f32 = P[CP_conv + 4u] / (P[CP_faceW] / 2.0);
    if (c == 0u) { return vec2f(P[CP_conv] + k * sx, P[CP_conv + 1u] + k * sy); }
    if (c == 2u) { return vec2f(P[CP_conv + 2u] - k * sx, P[CP_conv + 3u] - k * sy); }
    return vec2f(0.0);
}
fn energy(c: u32, lit: bool, mono: bool, mono_e: f32, cov: f32, sx: f32, sy: f32, hx: f32, hy: f32) -> f32 {
    if (lit && mono) { return mono_e * cov; }
    if (!lit) { return 0.0; }
    let o: vec2f = conv_off(c, sx, sy);
    return deposit(c, sx - o.x, sy - o.y, hx, hy) * cov;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let ww: u32 = u32(cp_i(CP_outW));
    let hh: u32 = u32(cp_i(CP_outH));
    let pix: u32 = gidx;
    if (pix >= ww * hh) { return; }
    let px: f32 = f32(pix % ww);
    let py: f32 = f32(pix / ww);
    let fp: vec4f = footprint(px, py);
    let sx: f32 = fp.x;
    let sy: f32 = fp.y;
    let hx: f32 = fp.z;
    let hy: f32 = fp.w;
    let alpha: f32 = face_alpha(sx, sy, hx, hy);
    let cover: vec3f = mask_cover(sx, sy, hx, hy, px, py);
    let t0: f32 = clamp((sy + P[CP_rasterH] / 2.0) / P[CP_rasterH], 0.0, 1.0) * P[CP_fieldT];
    let lit: bool = alpha > 0.0 && abs(sx) < P[CP_rasterW] / 2.0 + 3.0 * hx && abs(sy) < P[CP_rasterH] / 2.0 + 3.0 * hy;
    let mono: bool = cp_i(CP_mono) == 1;
    var mono_e: f32 = 0.0;
    if (lit && mono) { mono_e = deposit(0u, sx, sy, hx, hy); }
    let ra: vec4f = hist[2u * pix];
    let rb: vec4f = hist[2u * pix + 1u];
    let cr: vec3f = component(0u, energy(0u, lit, mono, mono_e, cover.x, sx, sy, hx, hy), ra.x, rb.x, t0);
    let cg: vec3f = component(1u, energy(1u, lit, mono, mono_e, cover.y, sx, sy, hx, hy), ra.y, rb.y, t0);
    let cb: vec3f = component(2u, energy(2u, lit, mono, mono_e, cover.z, sx, sy, hx, hy), ra.z, rb.z, t0);
    hist[2u * pix] = vec4f(cr.y, cg.y, cb.y, ra.w);
    hist[2u * pix + 1u] = vec4f(cr.z, cg.z, cb.z, rb.w);
    em[pix] = vec4f(cr.x, cg.x, cb.x, alpha);
}

//@pass style_crtp_halo_down
// The faceplate halo: the emission box-averaged into the halo grid.
@group(0) @binding(0) var<uniform> U: CrtpGpuParams;
@group(0) @binding(1) var<storage, read> P: array<f32>;
@group(0) @binding(2) var<storage, read> em: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> hd: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let gw: u32 = u32(floor(P[CP_gw] + 0.5));
    let gh: u32 = u32(floor(P[CP_gh] + 0.5));
    let q: u32 = u32(floor(P[CP_haloQ] + 0.5));
    let ww: u32 = u32(floor(P[CP_outW] + 0.5));
    let hh: u32 = u32(floor(P[CP_outH] + 0.5));
    if (gidx >= gw * gh || U.pad0 == 7u) { return; }
    let gx: u32 = gidx % gw;
    let gy: u32 = gidx / gw;
    var s: vec3f = vec3f(0.0);
    var n: f32 = 0.0;
    for (var y: u32 = gy * q; y < min(hh, gy * q + q); y++) {
        for (var x: u32 = gx * q; x < min(ww, gx * q + q); x++) {
            s = s + em[y * ww + x].xyz;
            n = n + 1.0;
        }
    }
    hd[gidx] = vec4f(s / n, 0.0);
}

//@pass style_crtp_halo_conv
// The halo grid convolved with the kernel derived from the glass optics (zero outside the frame).
@group(0) @binding(0) var<uniform> U: CrtpGpuParams;
@group(0) @binding(1) var<storage, read> P: array<f32>;
@group(0) @binding(2) var<storage, read> kern: array<f32>;
@group(0) @binding(3) var<storage, read> hd: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> hc: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let gw: i32 = i32(floor(P[CP_gw] + 0.5));
    let gh: i32 = i32(floor(P[CP_gh] + 0.5));
    let r: i32 = i32(floor(P[CP_haloR] + 0.5));
    let sz: i32 = 2 * r + 1;
    let idx: i32 = i32(gidx);
    if (idx >= gw * gh || U.pad0 == 7u) { return; }
    let x: i32 = idx % gw;
    let y: i32 = idx / gw;
    var s: vec3f = vec3f(0.0);
    for (var dy: i32 = -r; dy <= r; dy++) {
        let yy: i32 = y + dy;
        if (yy < 0 || yy >= gh) { continue; }
        for (var dx: i32 = -r; dx <= r; dx++) {
            let xx: i32 = x + dx;
            if (xx < 0 || xx >= gw) { continue; }
            s = s + kern[u32((dy + r) * sz + dx + r)] * hd[u32(yy * gw + xx)].xyz;
        }
    }
    hc[u32(idx)] = vec4f(s, 0.0);
}

//@pass style_crtp_compose
// The faceplate: the phosphor colour of the direct light and the halo, the room reflected in the
// curved glass, the black level it lights up and the bezel; then the display encode to RGBA8.
@group(0) @binding(0) var<uniform> U: CrtpGpuParams;
@group(0) @binding(1) var<storage, read> P: array<f32>;
@group(0) @binding(2) var<storage, read> em: array<vec4f>;
@group(0) @binding(3) var<storage, read> hc: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> out8: array<u32>;

fn face_at(px: f32, py: f32) -> vec2f {
    let x: f32 = (px - P[CP_outW] / 2.0) * (P[CP_viewW] / P[CP_outW]) + P[CP_cx];
    let y: f32 = (py - P[CP_outH] / 2.0) * (P[CP_viewH] / P[CP_outH]) + P[CP_cy];
    let d: f32 = P[CP_dist];
    let a: f32 = x * x / (2.0 * P[CP_rx]) + y * y / (2.0 * P[CP_ry]);
    let t: f32 = 2.0 * d / (d + sqrt(max(0.0, d * d - 4.0 * a * d)));
    return vec2f(t * x, t * y);
}
fn halo_at(px: f32, py: f32) -> vec3f {
    let q: f32 = P[CP_haloQ];
    let gw: i32 = i32(floor(P[CP_gw] + 0.5));
    let gh: i32 = i32(floor(P[CP_gh] + 0.5));
    let fx: f32 = clamp((px + 0.5) / q - 0.5, 0.0, f32(gw - 1));
    let fy: f32 = clamp((py + 0.5) / q - 0.5, 0.0, f32(gh - 1));
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
fn glass_reflection(sx: f32, sy: f32) -> f32 {
    let rx: f32 = P[CP_rx];
    let ry: f32 = P[CP_ry];
    let dd: f32 = P[CP_dist];
    let sz: f32 = sx * sx / (2.0 * rx) + sy * sy / (2.0 * ry);
    let iv: vec3f = normalize(vec3f(sx, sy, sz + dd));
    let nv: vec3f = normalize(vec3f(sx / rx, sy / ry, -1.0));
    let d: f32 = dot(iv, nv);
    return cp_fresnel_air(P[CP_glassN], acos(min(1.0, abs(d)))) * cp_room_env(iv - 2.0 * d * nv);
}
fn gamut_fit(rgb: vec3f) -> vec3f {
    let lw: vec3f = vec3f(P[CP_luma], P[CP_luma + 1u], P[CP_luma + 2u]);
    let y: f32 = max(0.0, dot(lw, rgb));
    let m: f32 = min(rgb.x, min(rgb.y, rgb.z));
    if (m >= 0.0) { return rgb; }
    return vec3f(y) + (y / (y - m)) * (rgb - vec3f(y));
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    // Groups fold into two dimensions (style_graph::Passes::add): 32,768 64-wide groups a row.
    let gidx: u32 = gid.x + gid.y * 2097152u;
    let ww: u32 = u32(floor(P[CP_outW] + 0.5));
    let hh: u32 = u32(floor(P[CP_outH] + 0.5));
    let pix: u32 = gidx;
    if (pix >= ww * hh || U.pad0 == 7u) { return; }
    let px: f32 = f32(pix % ww);
    let py: f32 = f32(pix / ww);
    let e: vec4f = em[pix];
    let a: f32 = e.w;
    let c: vec2f = face_at(px + 0.5, py + 0.5);
    let gun: vec3f = P[CP_haloDirect] * e.xyz + halo_at(px, py);
    let m: u32 = CP_M;
    let rgb: vec3f = gamut_fit(vec3f(P[m] * gun.x + P[m + 1u] * gun.y + P[m + 2u] * gun.z, P[m + 3u] * gun.x + P[m + 4u] * gun.y + P[m + 5u] * gun.z, P[m + 6u] * gun.x + P[m + 7u] * gun.y + P[m + 8u] * gun.z));
    let black: f32 = P[CP_ambient] * P[CP_transmission];
    let spec: f32 = P[CP_reflection] * glass_reflection(c.x, c.y);
    let bezel: vec3f = vec3f(P[CP_bezel], P[CP_bezel + 1u], P[CP_bezel + 2u]);
    let o: vec3f = a * (rgb + vec3f(black + spec)) + (1.0 - a) * bezel;
    let ex: f32 = P[CP_exposure];
    out8[pix] = cp_enc(o.x, ex) | (cp_enc(o.y, ex) << 8u) | (cp_enc(o.z, ex) << 16u) | (255u << 24u);
}
