// Threads: thread and fibre lines laid along the level sets of a form field and
// tone-mapped by density. A creative module: it has no CPU reference and writes
// no certificate (docs/architecture/adr/0010-web-host.md).
//
// One shader source for every backend. WebGPU runs it as written (web/ and the
// wasm build); D3D12 runs src/renderer/gpu/shaders/hlsl/threads_*.hlsl, which
// scripts/wgsl_to_hlsl.py generates from it. The code before the first pass
// marker is shared by every pass.
//
// The method, from the bottom up:
//  - A thread is a chain of eight nodes; a workgroup holds 32 chains. Position
//    based constraints (Mueller, Heidelberger, Hennix and Ratcliff 2007) hold
//    every node on one level line of the form, and distance constraints keep
//    each link at its rest length. The links are solved by Jacobi iterations
//    with constraint averaging in workgroup memory, the head pinned.
//  - The head steers by a tangent field: the structure tensor of the form's
//    gradient at the head and one step ahead, read by the double-angle formula.
//    Where coherence is low the heading relaxes toward a calm bend from the
//    noise module. This is the burin's orientation field from the techniques
//    library, after Jobard and Lefer (1997), moved onto the GPU.
//  - The head's pace follows the stepwise rope ease: hand-over-hand pulls with
//    a slip back, so a thread is drawn in pulls and never at an even rate.
//  - Each link is a stroke. Samples along it carry density per unit length and
//    share each sample bilinearly among four pixels, so a line reads continuous
//    and smooth at any speed. Its light is a fibre's: Beer-Lambert attenuation
//    from the head, a bright end face at the tail and, while one world crosses
//    to the next, dispersion along the fibre through the CIE fit of Wyman,
//    Sloan and Shirley (2013). Line weight follows the engraver: deeper cuts
//    near the form, an ink pool at pen-down and a draining reservoir.
//  - Hashing and noise are the engine noise module's (noise.wgsl).
//  - The finish stretches density by asinh with colour kept as a ratio
//    (Lupton, Gunn and Szalay 2004), adds a power-law bloom over a pyramid,
//    radial lateral chromatic aberration, an iris that opens on the spring
//    token, cos^4 natural vignetting and triangular-PDF dither.
// Splats are integer atomics into a fixed-point accumulator, so every pass
// runs on a backend with compute only.
//
// Lineage: the fifteen worlds are the forms of the One Step film (2026-10-04).
// An earlier version of this file ported that film's own offline GLSL renderer;
// this version keeps the worlds and their palettes and replaces the motion, the
// stroke, the shading and the finish.

struct Params {
    t: f32, dt: f32, fr: f32, aspect: f32,
    u: f32, ta: f32, tb: f32, spacing: f32,
    nlev: f32, gain: f32, scale: f32, keep: f32,
    expo: f32, spec: f32, dur: f32, fxs: f32,
    pr0: vec4f,
    pr1: vec4f,
    np: u32, wa: i32, wb: i32, ch: i32,
    w: u32, h: u32, substeps: u32, simw: u32,
    simh: u32, seedbase: u32, simt: f32, pad0: u32,
}

// One level of the bloom pyramid: full-frame size, the level written, and the
// scale from accumulator units to density.
struct Level {
    w: u32, h: u32, level: u32, inv: f32,
}

const PI: f32 = 3.14159265;

// The noise module's integer hash (noise.wgsl lowbias32) and its unit mapping.
fn lowbias32(x0: u32) -> u32 {
    var x: u32 = x0;
    x = x ^ (x >> 16u);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15u);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16u);
    return x;
}
fn unit01(h: u32) -> f32 { return f32(h >> 8u) * (1.0 / 16777216.0); }
fn hash3u(a: u32, b: u32, c: u32) -> u32 { return lowbias32(a ^ lowbias32(b ^ lowbias32(c))); }
// The spring token of web/motion/timeline.mjs: a critically damped spring
// settling from 0 to 1 as u runs from 0 to 1.
fn springTok(u0: f32) -> f32 {
    let u: f32 = clamp(u0, 0.0, 1.0);
    return 1.0 - (1.0 + 9.0 * u) * exp(-9.0 * u);
}
// The bloom pyramid: level l is max(1, size >> l) on each side. Odd levels live
// in one buffer and even levels in another, each packed after the earlier
// levels of the same parity, so a pass reads one buffer and writes the other.
fn levelDim(n: u32, l: u32) -> u32 { return max(1u, n >> l); }
fn levelBase(w: u32, h: u32, l: u32) -> u32 {
    var o: u32 = 0u;
    for (var j: u32 = 2u - (l % 2u); j < l; j = j + 2u) {
        o = o + levelDim(w, j) * levelDim(h, j);
    }
    return o;
}

//@pass threads_init
// Every node starts with a negative age, so the first step lays its chain on
// the field. The fourth lane is the node's seed.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read_write> pos: array<vec4f>;
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.np) { return; }
    pos[gid.x] = vec4f(0.0, 0.0, -1.0, unit01(lowbias32(gid.x + P.seedbase)));
}

//@pass threads_rd_seed
// The reaction-diffusion field behind the Morphogen world: u = 1 everywhere and
// v seeded in a centre spot and forty hashed spots.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read_write> dst: array<vec2f>;
@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.simw || gid.y >= P.simh) { return; }
    let q: vec2f = (vec2f(f32(gid.x), f32(gid.y)) - vec2f(f32(P.simw), f32(P.simh)) * 0.5) / f32(P.simh);
    var v: f32 = step(length(q), 0.02);
    for (var k: u32 = 0u; k < 40u; k++) {
        let sp: vec2f = vec2f(unit01(hash3u(k, 1u, 20261004u)) * 1.7 - 0.85, unit01(hash3u(k, 2u, 20261004u)) - 0.5);
        v = v + step(length(q - sp), 0.006);
    }
    dst[gid.y * P.simw + gid.x] = vec2f(1.0, min(v, 1.0));
}

//@pass threads_rd
// One Gray-Scott step (Pearson 1993) on a wrapping grid, with the isotropic
// nine-point Laplacian (4 x sides + corners - 20 x centre) / 20. The feed and
// kill rates move through four regimes on the film's clock, and the eye's lids
// damp v while they are open.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> src: array<vec2f>;
@group(0) @binding(2) var<storage, read_write> dst: array<vec2f>;
fn cell(x: i32, y: i32) -> vec2f {
    let w: i32 = i32(P.simw);
    let h: i32 = i32(P.simh);
    return src[u32((((y % h) + h) % h) * w + (((x % w) + w) % w))];
}
@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.simw || gid.y >= P.simh) { return; }
    let x: i32 = i32(gid.x);
    let y: i32 = i32(gid.y);
    let q: vec2f = (vec2f(f32(x), f32(y)) - vec2f(f32(P.simw), f32(P.simh)) * 0.5) / f32(P.simh);
    let s: vec2f = cell(x, y);
    let sides: vec2f = cell(x + 1, y) + cell(x - 1, y) + cell(x, y + 1) + cell(x, y - 1);
    let corners: vec2f = cell(x + 1, y + 1) + cell(x - 1, y + 1) + cell(x + 1, y - 1) + cell(x - 1, y - 1);
    let lap: vec2f = (4.0 * sides + corners - 20.0 * s) * 0.05;
    let ph: f32 = clamp(P.simt / 40.0, 0.0, 1.0);
    var fk: vec2f = mix(vec2f(0.029, 0.057), vec2f(0.0367, 0.0649), smoothstep(0.3, 0.45, ph));
    fk = mix(fk, vec2f(0.0545, 0.062), smoothstep(0.55, 0.68, ph));
    fk = mix(fk, vec2f(0.039, 0.058), smoothstep(0.78, 0.9, ph));
    let uvv: f32 = s.x * s.y * s.y;
    let u: f32 = s.x + lap.x - uvv + fk.x * (1.0 - s.x);
    var v: f32 = s.y + 0.5 * lap.y + uvv - (fk.x + fk.y) * s.y;
    let open: f32 = smoothstep(8.0, 14.0, P.simt) * (1.0 - smoothstep(34.0, 37.0, P.simt));
    let qx: f32 = q.x / 0.42;
    let lid: f32 = 0.16 * (1.0 - qx * qx) * open;
    if (abs(q.y) < lid && abs(q.x) < 0.42 && length(q) > 0.07 * open) { v = v * 0.6; }
    dst[gid.y * P.simw + gid.x] = vec2f(clamp(u, 0.0, 1.0), clamp(v, 0.0, 1.0));
}

//@pass threads_decay
// Persistence: every accumulator cell keeps P.keep of its value.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read_write> acc: array<u32>;
@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let i: u32 = (gid.y * P.w + gid.x) * 4u;
    acc[i] = u32(f32(acc[i]) * P.keep);
    acc[i + 1u] = u32(f32(acc[i + 1u]) * P.keep);
    acc[i + 2u] = u32(f32(acc[i + 2u]) * P.keep);
    acc[i + 3u] = u32(f32(acc[i + 3u]) * P.keep);
}

//@pass threads_advance
// One frame of every chain, in substeps: the head steers or lays a new thread,
// every node is pinned to its level line, the links are solved, and each link
// strokes its light. Workgroup memory holds the 32 chains of the group.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read_write> pos: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> acc: array<atomic<u32>>;
@group(0) @binding(3) var<storage, read> sim: array<vec2f>;
var<workgroup> node: array<vec4f, 256>;
var<workgroup> chainInfo: array<vec4f, 32>;
// The noise module's 2D gradient noise (noise.wgsl perlin2, the same
// operations) and a four-octave sum of it centred on one half.
fn fade(t: f32) -> f32 { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
fn nlerp(a: f32, b: f32, t: f32) -> f32 { return a + (b - a) * t; }
fn grad2(h: u32, x: f32, y: f32) -> f32 {
    let k: u32 = h & 7u;
    if (k == 0u) { return x + y; }
    if (k == 1u) { return -x + y; }
    if (k == 2u) { return x - y; }
    if (k == 3u) { return -x - y; }
    if (k == 4u) { return x; }
    if (k == 5u) { return -x; }
    if (k == 6u) { return y; }
    return -y;
}
fn pg2(i: i32, j: i32, a: i32, b: i32, rx: f32, ry: f32, seed: u32) -> f32 {
    return grad2(lowbias32(u32(i + a) ^ lowbias32(u32(j + b) ^ lowbias32(0u ^ lowbias32(seed)))), rx - f32(a), ry - f32(b));
}
fn perlin2(x: f32, y: f32, seed: u32) -> f32 {
    let i: i32 = i32(floor(x));
    let j: i32 = i32(floor(y));
    let rx: f32 = x - f32(i);
    let ry: f32 = y - f32(j);
    let fx: f32 = fade(rx);
    let fy: f32 = fade(ry);
    return nlerp(nlerp(pg2(i, j, 0, 0, rx, ry, seed), pg2(i, j, 1, 0, rx, ry, seed), fx), nlerp(pg2(i, j, 0, 1, rx, ry, seed), pg2(i, j, 1, 1, rx, ry, seed), fx), fy) * 0.70710678;
}
fn fbm2(p0: vec2f) -> f32 {
    var q: vec2f = p0;
    var s: f32 = 0.0;
    var a: f32 = 0.5;
    for (var o: u32 = 0u; o < 4u; o++) {
        s = s + a * perlin2(q.x, q.y, 20261004u + o);
        q = q * 2.0;
        a = a * 0.5;
    }
    return 0.5 + s * 1.1;
}
// p turned clockwise by a.
fn rotv(p: vec2f, a: f32) -> vec2f {
    let c: f32 = cos(a);
    let s: f32 = sin(a);
    return vec2f(c * p.x + s * p.y, c * p.y - s * p.x);
}
// Floored remainder: the result takes the sign of the divisor.
fn fmodf(x: f32, y: f32) -> f32 { return x - y * floor(x / y); }
// Exact distances to a segment, a circle and a box. Credit: the box distance is
// the closed form Inigo Quilez publishes among his 2D distance functions.
fn seg(p: vec2f, a: vec2f, b: vec2f) -> f32 {
    let pa: vec2f = p - a;
    let ba: vec2f = b - a;
    let h: f32 = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}
fn ring(p: vec2f, c: vec2f, r: f32) -> f32 { return abs(length(p - c) - r); }
fn box2(p: vec2f, b: vec2f) -> f32 {
    let d: vec2f = abs(p) - b;
    return length(max(d, vec2f(0.0))) + min(max(d.x, d.y), 0.0);
}
fn figure(p0: vec2f, lean: f32) -> f32 {
    let p: vec2f = rotv(p0, lean);
    var d: f32 = ring(p, vec2f(0.0, 0.17), 0.035);
    d = min(d, seg(p, vec2f(0.0, 0.13), vec2f(0.0, 0.0)));
    d = min(d, seg(p, vec2f(0.0, 0.11), vec2f(-0.09, 0.03)));
    d = min(d, seg(p, vec2f(0.0, 0.11), vec2f(0.09, 0.03)));
    d = min(d, seg(p, vec2f(0.0, 0.0), vec2f(-0.045, -0.16)));
    d = min(d, seg(p, vec2f(0.0, 0.0), vec2f(0.045, -0.16)));
    return d;
}

// The fifteen worlds of the One Step film. Each returns a signed, distance-like
// scalar; the threads lie on its zero set and on the level lines around it.
// Squares are written as products, so no pow() meets a negative base.
fn fKomorebi(p: vec2f, t: f32) -> f32 {
    let w: vec2f = vec2f(sin(t * 0.4), cos(t * 0.31)) * 0.03;
    let l: f32 = fbm2((p + w) * 3.2 + vec2f(t * 0.04, 0.0));
    return (l - 0.55) * 0.35;
}
fn fDroste(p: vec2f, t: f32) -> f32 {
    let sc: f32 = log(4.0);
    var lz: vec2f = vec2f(log(max(length(p), 1e-3)), atan2(p.y, p.x));
    let al: f32 = atan(sc / (2.0 * PI));
    lz = rotv(lz, al) / cos(al);
    lz.x = fmodf(lz.x - t * 0.25, sc);
    var q: vec2f = exp(lz.x) * vec2f(cos(lz.y), sin(lz.y)) * 2.2;
    let m: f32 = 0.5 + 0.5 * sin(t * 0.15);
    q = q + m * 0.3 * vec2f(sin(q.y * PI), sin(q.x * PI));
    let f: vec2f = abs(fract(q) - 0.5);
    return (0.5 - max(f.x, f.y)) * 0.08 * length(p) / exp(lz.x);
}
fn fVoices(p: vec2f, t: f32) -> f32 {
    var q: vec2f = p * 1.3;
    q.x = abs(q.x);
    let w: vec2f = vec2f(fbm2(q * 2.1 + t * 0.06), fbm2(q * 2.1 - vec2f(t * 0.05, 3.1)));
    return (fbm2(q * 1.6 + w * 1.6) - 0.5) * 0.3 + max(length(p * vec2f(0.7, 1.0)) - 0.8, 0.0);
}
fn fMaking(p: vec2f, t0: f32) -> f32 {
    let t: f32 = t0 - 3.0;
    let rise: f32 = smoothstep(12.0, 17.0, t);
    var d: f32 = figure(p + vec2f(0.0, 0.05), (1.0 - rise) * 1.45);
    var a: vec2f = vec2f(0.0, 0.6);
    let strike: f32 = floor(t / 3.1);
    let fade: f32 = select(0.0, 0.3, t - strike * 3.1 > 0.6);
    for (var k: i32 = 0; k < 10; k++) {
        let b: vec2f = a + vec2f((unit01(hash3u(u32(i32(strike)), u32(k), 31u)) - 0.5) * 0.12, -0.055);
        d = min(d, seg(p, a, b) + 0.02 + fade);
        a = b;
    }
    let hy: f32 = 0.62 - 0.3 * smoothstep(2.0, 9.0, t);
    d = min(d, seg(p, vec2f(0.3, 0.7), vec2f(0.3, hy)));
    d = min(d, ring(p, vec2f(0.3, hy - 0.03), 0.02));
    for (var q: i32 = 0; q < 6; q++) {
        let fk: f32 = f32(q);
        let ph: vec2f = vec2f(-0.75 + fk * 0.3, 0.7 - fmodf(t * 0.05 + unit01(hash3u(u32(q), 7u, 37u)) * 1.4, 1.5));
        d = min(d, abs(box2(rotv(p - ph, sin(t * 0.8 + fk) * 0.6), vec2f(0.035, 0.045))));
    }
    return d;
}
fn fEye(p: vec2f, t: f32) -> f32 {
    let open: f32 = smoothstep(0.0, 3.0, t);
    let ex: f32 = p.x / 0.55;
    let lid: f32 = 0.22 * (1.0 - ex * ex) * open;
    var d: f32 = abs(abs(p.y) - lid) + max(abs(p.x) - 0.55, 0.0);
    let look: vec2f = 0.04 * vec2f(sin(t * 0.7), 0.4 * cos(t * 0.5));
    let r: f32 = length(p - look);
    d = min(d, abs(r - 0.18));
    d = min(d, abs(r - 0.055));
    let a: f32 = atan2(p.y - look.y, p.x - look.x);
    d = min(d, max(abs(sin(a * 60.0)) * r * 0.05, max(r - 0.18, 0.06 - r)));
    let va: vec2f = vec2f(0.0, 0.62);
    let vb: vec2f = vec2f(-0.9, -0.42);
    let vc: vec2f = vec2f(0.9, -0.42);
    d = min(d, min(min(seg(p, va, vb), seg(p, vb, vc)), seg(p, vc, va)));
    return d;
}
fn puppetString(p: vec2f, bar: vec2f, k: f32, j: vec2f, pull: f32) -> f32 {
    let top: vec2f = bar + vec2f(k * 0.06, 0.0);
    return seg(p, top, mix(j, top, pull)) + 0.0015;
}
fn fStrings(p: vec2f, t0: f32) -> f32 {
    let t: f32 = t0 - 3.0;
    let sw: f32 = sin(t * 1.3);
    let cut: f32 = smoothstep(15.0, 15.4, t);
    let bar: vec2f = vec2f(0.15 * sw, 0.55);
    let slump: f32 = cut * (1.0 - smoothstep(16.5, 19.0, t)) * 0.07;
    let sl: vec2f = vec2f(0.0, slump);
    let j0: vec2f = vec2f(0.0, 0.08) - sl;
    let j1: vec2f = vec2f(-0.1, sw * 0.03 * (1.0 - cut)) - sl;
    let j2: vec2f = vec2f(0.1, -sw * 0.03 * (1.0 - cut)) - sl;
    let j3: vec2f = vec2f(-0.05, -0.24) - sl;
    let j4: vec2f = vec2f(0.05, -0.24) - sl;
    var d: f32 = ring(p, j0 + vec2f(0.0, 0.05), 0.035);
    let hip: vec2f = vec2f(0.0, -0.1 - slump);
    d = min(d, seg(p, j0, hip));
    d = min(d, seg(p, j0, j1));
    d = min(d, seg(p, j0, j2));
    d = min(d, seg(p, hip, j3));
    d = min(d, seg(p, hip, j4));
    let pull: f32 = cut * smoothstep(15.0, 16.0, t);
    d = min(d, puppetString(p, bar, -2.0, j0, pull));
    d = min(d, puppetString(p, bar, -1.0, j1, pull));
    d = min(d, puppetString(p, bar, 0.0, j2, pull));
    d = min(d, puppetString(p, bar, 1.0, j3, pull));
    d = min(d, puppetString(p, bar, 2.0, j4, pull));
    d = min(d, seg(p, bar - vec2f(0.15, 0.0), bar + vec2f(0.15, 0.0)));
    let lie: f32 = max(smoothstep(4.0, 4.3, t) * (1.0 - smoothstep(6.0, 6.5, t)), smoothstep(8.0, 8.3, t) * (1.0 - smoothstep(11.0, 11.5, t)));
    let ns: vec2f = j0 + vec2f(0.03, 0.05);
    d = min(d, seg(p, ns, ns + vec2f(0.04 + 0.24 * lie, 0.0)));
    d = min(d, seg(p, ns + vec2f(0.0, -0.03), ns + vec2f(0.3, -0.03)) + 0.004);
    let tr: vec2f = p - vec2f(-0.6, -0.05);
    d = min(d, seg(tr, vec2f(0.0, -0.45), vec2f(0.02, 0.2)) + (1.0 - smoothstep(8.0, 10.0, t) * (1.0 - smoothstep(12.0, 14.0, t))) * 0.5);
    return d;
}
fn fStream(p: vec2f, t: f32) -> f32 {
    let bend: f32 = 0.08 * sin(p.x * 2.2 + t * 0.5);
    let bx: f32 = mix(-1.0, 1.0, smoothstep(0.0, 18.0, t));
    let dx: f32 = p.x - bx;
    let w: f32 = 0.09 + 0.06 * exp(-dx * dx * 20.0);
    var d: f32 = abs(abs(p.y - bend) - w);
    for (var k: i32 = 0; k < 9; k++) {
        let xk: f32 = -0.88 + f32(k) * 0.22;
        d = min(d, abs(length(vec2f((p.x - xk) * 5.0, p.y - bend)) - 0.2));
    }
    return d;
}
fn fBelly(p: vec2f, t: f32) -> f32 {
    var d: f32 = 1000.0;
    for (var k: i32 = 0; k < 9; k++) {
        let depth: f32 = fract((f32(k) - t * 0.3) / 9.0) * 9.0 + 0.6;
        let q: vec2f = p * depth;
        let e: vec2f = vec2f(q.x * 0.8, max(q.y + 0.1, 0.0) * 1.1 + min(q.y + 0.1, 0.0) * 3.0);
        d = min(d, abs(length(e) - 0.9) / depth);
    }
    d = min(d, ring(p, vec2f(0.03 * sin(t * 0.4), -0.14), 0.008));
    return d;
}
fn prob(k: i32, a: vec4f, b: vec4f) -> f32 {
    if (k == 0) { return a.x; }
    if (k == 1) { return a.y; }
    if (k == 2) { return a.z; }
    if (k == 3) { return a.w; }
    if (k == 4) { return b.x; }
    return b.y;
}
fn fDraw(p: vec2f, t: f32, ch: i32, pa: vec4f, pb: vec4f) -> f32 {
    let ro: f32 = 0.15;
    let grow: f32 = smoothstep(4.0, 9.0, t);
    let pick: f32 = smoothstep(12.6, 12.9, t);
    let chaos: f32 = 1.0 - smoothstep(2.0, 6.0, t);
    var d: f32 = abs(length(p) - ro * (1.0 + 0.08 * chaos * sin(atan2(p.y, p.x) * 23.0 + t * 9.0)));
    d = min(d, abs(length(p) - 0.33) + 0.004);
    for (var k: i32 = 0; k < 6; k++) {
        let ang: f32 = radians(215.0 + 110.0 * f32(k) / 5.0);
        let dir: vec2f = vec2f(cos(ang), sin(ang));
        let boost: f32 = select(1.0 - 0.8 * pick, 1.0 + pick, k == ch);
        let reach: f32 = ro * (0.3 + 3.4 * sqrt(prob(k, pa, pb))) * grow * boost;
        d = min(d, seg(p, dir * ro * 0.9, dir * (ro * 0.9 + reach)));
    }
    return d;
}
fn fElements(p: vec2f, t: f32) -> f32 {
    let app: f32 = smoothstep(0.0, 14.0, t);
    let sp: vec2f = vec2f(0.0, 0.62 - 0.32 * app);
    let sr: f32 = 0.06 + 0.2 * app;
    var d: f32 = abs(length(p - sp) - sr);
    let core: vec2f = vec2f(0.0, -0.2);
    for (var k: i32 = 0; k < 4; k++) {
        let fk: f32 = f32(k);
        let sx: f32 = select(-1.0, 1.0, (k % 2) == 1);
        let pk: vec2f = vec2f(sx * select(0.42, 0.22, k >= 2), select(-0.22, -0.34, k >= 2));
        let open: f32 = smoothstep(5.0 + fk * 1.6, 6.5 + fk * 1.6, t);
        let q: vec2f = p - pk;
        d = min(d, abs(max(abs(q.x) * 1.4 + q.y * 0.5 - 0.06, max(-q.y - 0.07, q.y - 0.11))));
        d = min(d, seg(p, pk + vec2f(0.0, 0.1), core) + (1.0 - open) * 0.5);
    }
    d = min(d, seg(p, core, sp) + (1.0 - smoothstep(11.5, 12.5, t)) * 0.5);
    return d;
}
fn fBurden(p: vec2f, t: f32) -> f32 {
    let ys: f32 = -0.34 + 0.36 * (p.x + 0.9) / 1.8;
    var d: f32 = abs(p.y - ys) * 0.95;
    let fx: f32 = mix(-0.2, 0.0, smoothstep(0.0, 18.0, t));
    let foot: vec2f = vec2f(fx, -0.34 + 0.36 * (fx + 0.9) / 1.8);
    d = min(d, figure(p - foot - vec2f(0.03, 0.16), -0.55));
    let bc: vec2f = vec2f(fx + 0.19, -0.34 + 0.36 * (fx + 1.09) / 1.8 + 0.095);
    d = min(d, abs(length(p - bc) - 0.095));
    d = min(d, abs(box2(p - vec2f(-0.48, 0.15), vec2f(0.045, 0.21))));
    d = min(d, ring(p, vec2f(0.42, 0.1), 0.17));
    return d;
}
fn fSwing(p: vec2f, t: f32) -> f32 {
    let pl: vec2f = vec2f(-0.25, 0.25);
    let pr: vec2f = vec2f(0.17, 0.25);
    var d: f32 = min(seg(p, pl, pr), min(seg(p, pl, pl - vec2f(0.08, 0.6)), seg(p, pr, pr + vec2f(0.08, -0.6))));
    let ang: f32 = 0.35 * sin(t * 1.1) * (1.0 - smoothstep(12.0, 17.0, t) * 0.7);
    let piv: vec2f = vec2f(-0.04, 0.25);
    let seat: vec2f = piv + rotv(vec2f(0.0, -0.45), ang);
    d = min(d, min(seg(p, piv + vec2f(-0.04, 0.0), seat + vec2f(-0.04, 0.0)), seg(p, piv + vec2f(0.04, 0.0), seat + vec2f(0.04, 0.0))));
    d = min(d, figure(rotv(p - seat, -ang) - vec2f(0.0, 0.11), 0.0));
    let lamp: vec2f = vec2f(0.3, 0.35);
    d = min(d, seg(p, vec2f(0.3, -0.35), lamp));
    d = min(d, min(seg(p, lamp, lamp + vec2f(-0.3, -0.7)), seg(p, lamp, lamp + vec2f(0.3, -0.7))) + 0.01);
    d = min(d, abs(p.y + 0.35) + 0.004);
    return d;
}
fn fForest(p: vec2f, t: f32) -> f32 {
    let logY: f32 = -0.18 + 0.03 * sin(p.x * 3.0);
    var d: f32 = abs(abs(p.y - logY) - 0.07) + max(abs(p.x) - 0.85, 0.0);
    d = min(d, abs(p.y + 0.27) + 0.003);
    for (var k: i32 = 0; k < 6; k++) {
        let x: f32 = -0.9 + f32(k) * 0.36;
        d = min(d, abs(p.x - x - 0.01 * sin(p.y * 7.0 + f32(k))) + max(-0.1 - p.y, 0.0));
    }
    for (var q: i32 = 0; q < 5; q++) {
        let sx: f32 = -0.5 + f32(q) * 0.25;
        let hh: f32 = 0.35 * smoothstep(5.0 + f32(q) * 1.5, 16.0 + f32(q), t);
        d = min(d, seg(p, vec2f(sx, logY + 0.07), vec2f(sx, logY + 0.07 + hh)) + select(0.0, 1.0, hh < 0.01));
    }
    let my: f32 = abs(fract(fbm2(p * 7.0 + 5.0) * 6.0) - 0.5) * 0.04 + max(p.y + 0.27, 0.0) * 2.0;
    d = min(d, my + (1.0 - smoothstep(2.0, 14.0, t)) * 0.2);
    let sd: vec2f = mix(vec2f(-0.3, 0.6), vec2f(0.05, logY + 0.08), smoothstep(3.0, 13.0, t));
    d = min(d, ring(p, sd, 0.008));
    d = min(d, seg(p, vec2f(0.05, logY + 0.08), vec2f(0.05, logY + 0.08 + 0.22 * smoothstep(13.0, 18.0, t))) + select(0.0, 1.0, t < 13.0));
    return d;
}
fn fMany(p: vec2f, t: f32) -> f32 {
    let tile: f32 = exp(-3.0 * smoothstep(0.0, 10.0, t)) * 0.9;
    let f: vec2f = fract(p / tile + 0.5) - 0.5;
    var d: f32 = abs(length(f) - 0.3) * tile;
    d = min(d, abs(max(abs(f.x), abs(f.y)) - 0.45) * tile);
    return d + smoothstep(9.0, 11.0, t) * length(p) * 0.3;
}
// A wavelength in 400 to 700 nm (lam in 0 to 1) as an RGB hue: the multi-lobe
// Gaussian fit to the CIE 1931 matching functions (Wyman, Sloan and Shirley
// 2013), then XYZ to linear sRGB, clipped and scaled to a peak of one.
fn lobe(x: f32, mu: f32, s1: f32, s2: f32) -> f32 {
    let z: f32 = (x - mu) / select(s2, s1, x < mu);
    return exp(-0.5 * z * z);
}
fn spectrum(lam: f32) -> vec3f {
    let w: f32 = 400.0 + 300.0 * clamp(lam, 0.0, 1.0);
    let cx: f32 = 1.056 * lobe(w, 599.8, 37.9, 31.0) + 0.362 * lobe(w, 442.0, 16.0, 26.7) - 0.065 * lobe(w, 501.1, 20.4, 26.2);
    let cy: f32 = 0.821 * lobe(w, 568.8, 46.9, 40.5) + 0.286 * lobe(w, 530.9, 16.3, 31.1);
    let cz: f32 = 1.217 * lobe(w, 437.0, 11.8, 36.0) + 0.681 * lobe(w, 459.0, 26.0, 13.8);
    let rgb: vec3f = vec3f(3.2406 * cx - 1.5372 * cy - 0.4986 * cz, -0.9689 * cx + 1.8758 * cy + 0.0415 * cz, 0.0557 * cx - 0.204 * cy + 1.057 * cz);
    let c: vec3f = max(rgb, vec3f(0.0));
    return c / max(max(c.x, c.y), max(c.z, 1e-3));
}
// The worlds' palettes, from the film.
fn palette(w: i32, p: vec2f, s: f32, lam: f32) -> vec3f {
    if (w == 0) { return mix(vec3f(1.0, 0.85, 0.5), vec3f(0.45, 0.75, 0.3), s); }
    if (w == 1) { return mix(vec3f(0.95, 0.88, 0.74), vec3f(0.66, 0.33, 0.2), step(0.85, s)); }
    if (w == 2) { return select(vec3f(1.0, 0.35, 0.65), vec3f(0.4, 0.85, 1.0), p.x < 0.0); }
    if (w == 3) { return mix(vec3f(1.0, 0.45, 0.12), vec3f(0.95, 0.2, 0.45), s); }
    if (w == 4) { return mix(vec3f(0.6, 0.8, 1.0), vec3f(1.0), s); }
    if (w == 5) { return mix(vec3f(0.9, 0.1, 0.12), vec3f(1.0, 0.75, 0.35), s); }
    if (w == 6) { return mix(vec3f(0.85, 0.55, 0.25), vec3f(0.95, 0.9, 0.8), step(0.6, s)); }
    if (w == 7) { return mix(vec3f(0.55, 0.35, 1.0), spectrum(lam), 0.5); }
    if (w == 8) { return mix(vec3f(0.75, 0.3, 0.22), vec3f(1.0, 0.65, 0.3), step(0.92, s)); }
    if (w == 9) { return mix(vec3f(1.0, 0.9, 0.75), vec3f(1.0, 0.28, 0.08), step(0.9, s)); }
    if (w == 10) {
        let k: i32 = i32(s * 4.0);
        if (k == 0) { return vec3f(0.55, 0.9, 0.3); }
        if (k == 1) { return vec3f(0.3, 0.7, 1.0); }
        if (k == 2) { return vec3f(0.9, 0.95, 1.0); }
        return vec3f(1.0, 0.45, 0.1);
    }
    if (w == 11) { return mix(vec3f(0.85, 0.08, 0.05), vec3f(1.0, 0.5, 0.2), s); }
    if (w == 12) { return mix(vec3f(0.8, 0.85, 1.0), vec3f(1.0, 0.8, 0.45), step(0.75, s)); }
    if (w == 13) { return mix(vec3f(0.35, 0.7, 0.25), vec3f(0.95, 0.92, 0.85), step(0.7, s)); }
    return spectrum(lam);
}
fn simV(u0: vec2f) -> f32 {
    let w: i32 = i32(P.simw);
    let h: i32 = i32(P.simh);
    let q: vec2f = fract(u0) * vec2f(f32(P.simw), f32(P.simh)) - 0.5;
    let fl: vec2f = floor(q);
    let f: vec2f = q - fl;
    let x0: i32 = ((i32(fl.x) % w) + w) % w;
    let y0: i32 = ((i32(fl.y) % h) + h) % h;
    let x1: i32 = (x0 + 1) % w;
    let y1: i32 = (y0 + 1) % h;
    let a: f32 = mix(sim[u32(y0 * w + x0)].y, sim[u32(y0 * w + x1)].y, f.x);
    let b: f32 = mix(sim[u32(y1 * w + x0)].y, sim[u32(y1 * w + x1)].y, f.x);
    return mix(a, b, f.y);
}
fn fMorph(p: vec2f, t: f32) -> f32 { return (simV(0.5 + p * vec2f(9.0 / 16.0, 1.0)) - 0.2) * 0.15; }
fn form(w: i32, p: vec2f, t: f32) -> f32 {
    if (w == 0) { return fKomorebi(p, t); }
    if (w == 1) { return fDroste(p, t); }
    if (w == 2) { return fVoices(p, t); }
    if (w == 3) { return fMorph(p, t); }
    if (w == 4) { return fMaking(p, t); }
    if (w == 5) { return fEye(p, t); }
    if (w == 6) { return fStrings(p, t); }
    if (w == 7) { return fStream(p, t); }
    if (w == 8) { return fBelly(p, t); }
    if (w == 9) { return fDraw(p, t, P.ch, P.pr0, P.pr1); }
    if (w == 10) { return fElements(p, t); }
    if (w == 11) { return fBurden(p, t); }
    if (w == 12) { return fSwing(p, t); }
    if (w == 13) { return fForest(p, t); }
    return fMany(p, t);
}
// The weft: a slow noise field each world tints, followed by the dim chains.
fn atmos(p: vec2f, w: i32, t: f32) -> f32 {
    return fbm2(p * 2.4 + vec2f(f32(w) * 7.31, f32(w) * 3.7) + vec2f(t * 0.03, -t * 0.02)) - 0.5;
}
// The field a chain follows: its world's form or, for weft chains, the noise
// field. While U runs from 0 to 1 the old world closes onto an iris and the new
// one opens out of it, both on the spring token.
fn fieldAt(p: vec2f, weft: bool, ta: f32, tb: f32) -> f32 {
    if (weft) { return atmos(p, select(P.wa, P.wb, P.u > 0.5), select(ta, tb, P.u > 0.5)) * 0.25; }
    if (P.u <= 0.0) { return form(P.wa, p, ta); }
    let closing: bool = P.u < 0.5;
    let k: f32 = select(springTok(2.0 * P.u - 1.0), springTok(2.0 * P.u), closing);
    let r: f32 = 0.55 * select(k, 1.0 - k, closing) + 0.004;
    let iris: f32 = abs(length(p) - r);
    if (closing) { return mix(form(P.wa, p, ta), iris, k); }
    return mix(iris, form(P.wb, p, tb), k);
}
// The field and its gradient by forward differences: three evaluations.
fn fieldGrad(p: vec2f, weft: bool, ta: f32, tb: f32) -> vec3f {
    let e: f32 = 1e-3;
    let f0: f32 = fieldAt(p, weft, ta, tb);
    let fx: f32 = fieldAt(p + vec2f(e, 0.0), weft, ta, tb);
    let fy: f32 = fieldAt(p + vec2f(0.0, e), weft, ta, tb);
    return vec3f(f0, (fx - f0) / e, (fy - f0) / e);
}
// The level line nearest a field value: its index, clamped to the hatch count.
fn levelOf(f: f32) -> f32 { return clamp(floor(f / P.spacing + 0.5), -P.nlev, P.nlev); }
// The level-set constraint C(p) = f(p) - goal, projected with stiffness k.
fn pinToLevel(p: vec2f, fg: vec3f, goal: f32, k: f32) -> vec2f {
    let g: vec2f = fg.yz;
    let dp: vec2f = (fg.x - goal) * g / max(dot(g, g), 1e-6);
    let n: f32 = length(dp);
    return p - k * dp * min(1.0, 0.05 / max(n, 1e-9));
}
// The stepwise rope ease (techniques library rope.js): four pulls, each
// overshooting by a slip of 0.18 of a pull and giving it back while the hand
// re-grips in the last 0.32 of the pull.
fn easeInOut3(u: f32) -> f32 {
    if (u < 0.5) { return 4.0 * u * u * u; }
    let v: f32 = 2.0 - 2.0 * u;
    return 1.0 - v * v * v * 0.5;
}
fn ropeEase(t0: f32) -> f32 {
    let t: f32 = clamp(t0, 0.0, 1.0);
    if (t >= 1.0) { return 1.0; }
    let seg: f32 = min(3.0, floor(t * 4.0));
    let u: f32 = t * 4.0 - seg;
    let s: f32 = select(0.045, 0.0, seg >= 3.0);
    let base: f32 = seg * 0.25;
    if (u < 0.68) { return base + (0.25 + s) * easeInOut3(u / 0.68); }
    let r: f32 = (u - 0.68) / 0.32;
    return base + 0.25 + s - s * (1.0 - (1.0 - r) * (1.0 - r));
}
// The rope's rate at phase ph; one on average over a cycle.
fn ropeRate(ph: f32) -> f32 { return (ropeEase(ph + 0.01) - ropeEase(ph)) / 0.01; }
// One bilinear tap into the fixed-point accumulator: premultiplied colour and
// density. A tap under half a unit is dropped.
fn tap(x: u32, y: u32, c: vec3f, g: f32) {
    if (g < 0.5) { return; }
    let i: u32 = (y * P.w + x) * 4u;
    atomicAdd(&acc[i], u32(c.x * g + 0.5));
    atomicAdd(&acc[i + 1u], u32(c.y * g + 0.5));
    atomicAdd(&acc[i + 2u], u32(c.z * g + 0.5));
    atomicAdd(&acc[i + 3u], u32(g + 0.5));
}
fn deposit(q: vec2f, c: vec3f, a: f32) {
    let fx: f32 = (q.x / P.aspect + 0.5) * f32(P.w) - 0.5;
    let fy: f32 = (0.5 - q.y) * f32(P.h) - 0.5;
    if (fx < 0.0 || fy < 0.0 || fx >= f32(P.w) - 1.0 || fy >= f32(P.h) - 1.0) { return; }
    let x0: u32 = u32(fx);
    let y0: u32 = u32(fy);
    let ux: f32 = fx - f32(x0);
    let uy: f32 = fy - f32(y0);
    let g: f32 = a * P.gain * P.fxs;
    tap(x0, y0, c, g * (1.0 - ux) * (1.0 - uy));
    tap(x0 + 1u, y0, c, g * ux * (1.0 - uy));
    tap(x0, y0 + 1u, c, g * (1.0 - ux) * uy);
    tap(x0 + 1u, y0 + 1u, c, g * ux * uy);
}
// A link as a stroke from a to b: up to four samples, density per pixel of length.
fn stroke(a: vec2f, b: vec2f, c: vec3f, lum: f32) {
    let px: f32 = length((b - a) * vec2f(f32(P.w) / P.aspect, f32(P.h)));
    let n: u32 = u32(clamp(ceil(px), 1.0, 4.0));
    let each: f32 = lum * clamp(px, 0.25, 8.0) / f32(n);
    for (var m: u32 = 0u; m < n; m++) {
        deposit(mix(a, b, (f32(m) + 0.5) / f32(n)) / P.scale, c, each);
    }
}
const CHAIN: u32 = 8u;      // nodes per thread; node 0 is the head
const REST: f32 = 0.012;    // rest length of a link, in frame heights
const BEND: f32 = 0.6;      // how far the calm heading turns from the contour, in radians
@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_id) lid: vec3u) {
    let i: u32 = gid.x;
    let live: bool = i < P.np;
    let li: u32 = lid.x;
    let j: u32 = li % CHAIN;
    let cl: u32 = li / CHAIN;
    let hb: u32 = cl * CHAIN;
    let chainId: u32 = i / CHAIN;
    let s: vec4f = pos[min(i, P.np - 1u)];
    var p: vec2f = s.xy;
    // A static use of binding 3, so the pipeline layout keeps it when the web host
    // compiles this pass for worlds that never read the field (web/threads.mjs).
    if (P.simw == 0u) { p = sim[0]; }
    node[li] = vec4f(p, s.z, s.w);
    workgroupBarrier();
    // Everything about a thread comes from its head's seed.
    let hs: vec4f = node[hb];
    let su: u32 = u32(hs.w * 16777216.0);
    let lam: f32 = unit01(hash3u(su, 1u, 0u));
    let weft: bool = unit01(hash3u(su, 2u, 0u)) < 0.3;
    let loose: bool = unit01(hash3u(su, 3u, 0u)) < 0.14;
    let side: f32 = select(1.0, -1.0, unit01(hash3u(su, 4u, 0u)) < 0.5);
    let pace: f32 = 0.05 + 0.08 * unit01(hash3u(su, 5u, 0u));
    let phase: f32 = unit01(hash3u(su, 6u, 0u));
    let life: f32 = 0.8 + 3.2 * unit01(hash3u(su, 7u, 0u));
    let tone: f32 = unit01(hash3u(su, 8u, 0u));
    let bright: f32 = select(1.0, 0.22, weft) * select(1.0, 0.35, loose) * (0.55 + 0.45 * unit01(hash3u(su, 9u, 0u)));
    let tint: vec3f = select(vec3f(1.0), vec3f(0.75, 0.8, 0.9), weft);
    let kLevel: f32 = select(0.9, 0.15, loose);
    let wcol: i32 = select(P.wa, P.wb, P.u > 0.5);
    var age: f32 = hs.z;
    for (var k: u32 = 0u; k < P.substeps; k++) {
        let fk: f32 = f32(k);
        let t: f32 = P.t + fk * P.dt;
        let ta: f32 = P.ta + fk * P.dt;
        let tb: f32 = P.tb + fk * P.dt;
        // 1. The head steers along the tangent field, or lays a new thread.
        if (j == 0u) {
            if (age < 0.0 || age > life || abs(p.x) > P.aspect * 0.55 || abs(p.y) > 0.55) {
                // A new start from the R2 sequence (Roberts 2018) in 32-bit fixed point,
                // indexed by frame and chain, so starts spread evenly over the frame. A
                // start whose level line cannot be reached (a flat field) tries again.
                var fg: vec3f = vec3f(0.0);
                var lv: f32 = 0.0;
                for (var tries: u32 = 0u; tries < 3u; tries++) {
                    let n: u32 = (u32(P.fr + fk) * 3u + tries) * 7919u + chainId;
                    p = vec2f((unit01(2147483648u + n * 3242174889u) - 0.5) * P.aspect, unit01(2147483648u + n * 2447445413u) - 0.5);
                    fg = fieldGrad(p, weft, ta, tb);
                    lv = levelOf(fg.x);
                    for (var ri: u32 = 0u; ri < 4u; ri++) {
                        p = pinToLevel(p, fg, lv * P.spacing, 1.0);
                        fg = fieldGrad(p, weft, ta, tb);
                    }
                    if (abs(fg.x - lv * P.spacing) < 0.1 * P.spacing) { break; }
                }
                let gl: f32 = max(length(fg.yz), 1e-5);
                chainInfo[cl] = vec4f(lv, 1.0, -fg.z / gl * side, fg.y / gl * side);
                age = 0.0;
            } else {
                // The structure tensor of the gradient here and one link ahead.
                let fg: vec3f = fieldGrad(p, weft, ta, tb);
                let g0: vec2f = fg.yz;
                let t0: vec2f = vec2f(-g0.y, g0.x) / max(length(g0), 1e-5) * side;
                let g1: vec2f = fieldGrad(p + t0 * REST, weft, ta, tb).yz;
                let jxx: f32 = g0.x * g0.x + g1.x * g1.x;
                let jxy: f32 = g0.x * g0.y + g1.x * g1.y;
                let jyy: f32 = g0.y * g0.y + g1.y * g1.y;
                let tr: f32 = jxx + jyy;
                let coh: f32 = select(0.0, sqrt((jxx - jyy) * (jxx - jyy) + 4.0 * jxy * jxy) / tr, tr > 1e-10);
                let q: f32 = clamp(coh * coh * 1.6, 0.0, 1.0);
                let th: f32 = 0.5 * atan2(2.0 * jxy, jxx - jyy) + 0.5 * PI;
                var u: vec2f = vec2f(cos(th), sin(th));
                if (dot(u, t0) < 0.0) { u = u * -1.0; }
                let ca: f32 = atan2(t0.y, t0.x) + BEND * perlin2(p.x * 1.7 + 11.0, p.y * 1.7 + t * 0.05, 7u);
                let calm: vec2f = vec2f(cos(ca), sin(ca));
                let dir: vec2f = normalize(q * u + (1.0 - q) * calm + vec2f(1e-6, 0.0));
                let rate: f32 = max(ropeRate(fract(t * 0.35 + phase)), -0.4);
                p = p + dir * pace * rate * P.dt;
                age = age + P.dt;
                chainInfo[cl] = vec4f(levelOf(fg.x), 0.0, dir);
            }
            node[li] = vec4f(p, age, hs.w);
        }
        workgroupBarrier();
        // 2. A new thread is laid behind its head; then every node is pinned to
        //    the chain's level line.
        let info: vec4f = chainInfo[cl];
        let head: vec4f = node[hb];
        age = head.z;
        if (j > 0u && info.y > 0.5) { p = head.xy - info.zw * (REST * f32(j)); }
        let goal: f32 = info.x * P.spacing;
        let fg: vec3f = fieldGrad(p, weft, ta, tb);
        p = pinToLevel(p, fg, goal, kLevel);
        workgroupBarrier();
        node[li] = vec4f(p, age, s.w);
        workgroupBarrier();
        // 3. The links, by Jacobi iterations with constraint averaging. The head is
        //    driven, so it never yields to its first link.
        for (var it: u32 = 0u; it < 3u; it++) {
            let me: vec2f = node[li].xy;
            var dp: vec2f = vec2f(0.0);
            var nc: f32 = 0.0;
            if (j > 0u) {
                let d: vec2f = me - node[li - 1u].xy;
                let l: f32 = max(length(d), 1e-6);
                dp = dp - select(0.5, 1.0, j == 1u) * (l - REST) * d / l;
                nc = nc + 1.0;
                if (j + 1u < CHAIN) {
                    let e: vec2f = me - node[li + 1u].xy;
                    let m: f32 = max(length(e), 1e-6);
                    dp = dp - 0.5 * (m - REST) * e / m;
                    nc = nc + 1.0;
                }
            }
            workgroupBarrier();
            if (j > 0u) { node[li] = vec4f(me + 1.5 * dp / max(nc, 1.0), age, s.w); }
            workgroupBarrier();
        }
        p = node[li].xy;
        // 4. Each link strokes its light, from the node before it to this one.
        if (live && j > 0u) {
            let fe: f32 = fieldAt(p, weft, ta, tb);
            let dist: f32 = abs(fe - goal) / max(length(fg.yz), 1e-4);
            let sigma: f32 = 1.25 / f32(P.h);
            let sharp: f32 = exp(-(dist * dist) / (sigma * sigma));
            // The engraver's cut: the keyline on the form itself, then shallower hatches.
            let depth: f32 = select(1.0 - abs(info.x) / (P.nlev + 1.0), 1.8, abs(info.x) < 0.5);
            let sf: f32 = (f32(j) - 0.5) / f32(CHAIN - 1u);
            let face: f32 = select(1.0, 2.5, j == CHAIN - 1u);
            let fibre: f32 = exp(-1.1 * sf) * face;
            let pool: f32 = 1.0 + 0.55 * exp(-age * 4.0);
            let ink: f32 = 0.72 + 0.28 * min(1.0, clamp(1.0 - age / life, 0.0, 1.0) * 2.5);
            let lum: f32 = depth * depth * sharp * fibre * pool * ink * bright * 0.8;
            var c: vec3f = palette(wcol, p, tone, lam) * tint;
            c = mix(c, spectrum(fract(lam + 0.35 * sf)), P.spec * 0.85);
            stroke(node[li - 1u].xy, p, c, lum);
        }
        workgroupBarrier();
    }
    if (live) { pos[i] = vec4f(p, age, s.w); }
}

//@pass threads_pyr_first
// Bloom level 1 from the accumulator: a 2 x 2 box, in density units.
@group(0) @binding(0) var<uniform> L: Level;
@group(0) @binding(1) var<storage, read> acc: array<u32>;
@group(0) @binding(2) var<storage, read_write> dst: array<vec4f>;
fn accAt(x: u32, y: u32) -> vec4f {
    let i: u32 = (min(y, L.h - 1u) * L.w + min(x, L.w - 1u)) * 4u;
    return vec4f(f32(acc[i]), f32(acc[i + 1u]), f32(acc[i + 2u]), f32(acc[i + 3u]));
}
@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let dw: u32 = levelDim(L.w, 1u);
    let dh: u32 = levelDim(L.h, 1u);
    if (gid.x >= dw || gid.y >= dh) { return; }
    let x: u32 = gid.x * 2u;
    let y: u32 = gid.y * 2u;
    let s: vec4f = accAt(x, y) + accAt(x + 1u, y) + accAt(x, y + 1u) + accAt(x + 1u, y + 1u);
    dst[gid.y * dw + gid.x] = s * (0.25 * L.inv);
}

//@pass threads_pyr_next
// Bloom level L.level from level L.level - 1, which lives in the other buffer.
@group(0) @binding(0) var<uniform> L: Level;
@group(0) @binding(1) var<storage, read> src: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> dst: array<vec4f>;
@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let l: u32 = L.level;
    let dw: u32 = levelDim(L.w, l);
    let dh: u32 = levelDim(L.h, l);
    if (gid.x >= dw || gid.y >= dh) { return; }
    let sw: u32 = levelDim(L.w, l - 1u);
    let sh: u32 = levelDim(L.h, l - 1u);
    let sb: u32 = levelBase(L.w, L.h, l - 1u);
    let x0: u32 = min(gid.x * 2u, sw - 1u);
    let x1: u32 = min(gid.x * 2u + 1u, sw - 1u);
    let y0: u32 = min(gid.y * 2u, sh - 1u);
    let y1: u32 = min(gid.y * 2u + 1u, sh - 1u);
    let s: vec4f = src[sb + y0 * sw + x0] + src[sb + y0 * sw + x1] + src[sb + y1 * sw + x0] + src[sb + y1 * sw + x1];
    dst[levelBase(L.w, L.h, l) + gid.y * dw + gid.x] = s * 0.25;
}

//@pass threads_finish
// Asinh stretch, lateral chromatic aberration, power-law bloom, the iris,
// natural vignetting and triangular dither, packed to RGBA8.
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> acc: array<u32>;
@group(0) @binding(2) var<storage, read> pa: array<vec4f>;
@group(0) @binding(3) var<storage, read> pb: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> frame: array<u32>;
fn texel(l: u32, base: u32, lw: u32, x: u32, y: u32) -> vec4f {
    if (l == 0u) {
        let i: u32 = (y * P.w + x) * 4u;
        return vec4f(f32(acc[i]), f32(acc[i + 1u]), f32(acc[i + 2u]), f32(acc[i + 3u])) / P.fxs;
    }
    if (l % 2u == 1u) { return pa[base + y * lw + x]; }
    return pb[base + y * lw + x];
}
fn bilin(l: u32, u: vec2f) -> vec4f {
    let lw: u32 = levelDim(P.w, l);
    let lh: u32 = levelDim(P.h, l);
    let base: u32 = levelBase(P.w, P.h, l);
    let q: vec2f = clamp(u * vec2f(f32(lw), f32(lh)) - 0.5, vec2f(0.0), vec2f(f32(lw) - 1.0, f32(lh) - 1.0));
    let x0: u32 = u32(q.x);
    let y0: u32 = u32(q.y);
    let x1: u32 = min(x0 + 1u, lw - 1u);
    let y1: u32 = min(y0 + 1u, lh - 1u);
    let fx: f32 = q.x - f32(x0);
    let fy: f32 = q.y - f32(y0);
    let top: vec4f = mix(texel(l, base, lw, x0, y0), texel(l, base, lw, x1, y0), fx);
    let bottom: vec4f = mix(texel(l, base, lw, x0, y1), texel(l, base, lw, x1, y1), fx);
    return mix(top, bottom, fy);
}
fn asinhf(x: f32) -> f32 { return log(x + sqrt(x * x + 1.0)); }
// Lupton, Gunn and Szalay (2004): stretch the total density by asinh and keep
// the colour as its ratio to the total, so bright cores do not wash to white.
fn stretch(a: vec4f) -> vec3f {
    let hue: vec3f = a.xyz / max(a.w, 1e-6);
    return hue * (asinhf(a.w * P.expo * 2.0) / asinhf(P.expo * 80.0));
}
@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let fw: f32 = f32(P.w);
    let fh: f32 = f32(P.h);
    let uv: vec2f = (vec2f(f32(gid.x), f32(gid.y)) + 0.5) / vec2f(fw, fh);
    let d: vec2f = uv - 0.5;
    let r: f32 = length(d * vec2f(fw / fh, 1.0));
    // Radial lateral aberration (the k1 r^2 term of Brown-Conrady): red lands
    // outside, blue inside, more while worlds cross.
    let k1: f32 = 0.01 + 0.05 * P.spec;
    let ca: vec2f = d * (k1 * r * r);
    var c: vec3f = vec3f(stretch(bilin(0u, uv + ca)).x, stretch(bilin(0u, uv)).y, stretch(bilin(0u, uv - ca)).z);
    // Bloom as a power law: each octave of radius carries 2^-0.5 of the one inside it.
    var b: vec3f = vec3f(0.0);
    var wl: f32 = 0.18;
    for (var l: u32 = 1u; l < 8u; l++) {
        b = b + stretch(bilin(l, uv)) * wl;
        wl = wl * 0.70710678;
    }
    c = c + b;
    // The iris opens on the spring token and closes before the end; its rim glows
    // while it moves.
    let open: f32 = springTok((P.t - 0.3) / 2.4) * springTok((P.dur - 2.0 - P.t) / 1.6);
    let reach: f32 = 0.5 * length(vec2f(fw / fh, 1.0)) + 0.01;
    let ir: f32 = reach * open;
    let mask: f32 = 1.0 - smoothstep(ir - 0.004, ir, r);
    let rim: f32 = exp(-abs(r - ir) / 0.003) * (1.0 - open) * 2.5;
    c = c * mask + vec3f(0.85, 0.95, 1.0) * rim;
    // Natural vignetting, cos^4 of the angle off axis for a lens of focal length 1.6.
    let cs: f32 = 1.0 / (1.0 + (r / 1.6) * (r / 1.6));
    c = c * cs * cs;
    // Extended Reinhard shoulder (Reinhard et al. 2002) with white at 4.
    c = c * (1.0 + c / 16.0) / (1.0 + c);
    // Triangular-PDF dither of one 8-bit step.
    let hx: u32 = hash3u(gid.x, gid.y, u32(P.fr));
    c = c + (unit01(hx) + unit01(lowbias32(hx)) - 1.0) / 255.0;
    c = clamp(c, vec3f(0.0), vec3f(1.0));
    let rr: u32 = u32(c.x * 255.0 + 0.5);
    let gg: u32 = u32(c.y * 255.0 + 0.5);
    let bb: u32 = u32(c.z * 255.0 + 0.5);
    frame[gid.y * P.w + gid.x] = rr | (gg << 8u) | (bb << 16u) | (255u << 24u);
}
