// raw-native's noise module on the GPU (ROADMAP P8 stage 2; evidence/p8-noise-gpu-bounds.json):
// the float32 forms of src/renderer/noise.cpp, operation for operation, so the parity check can
// hold them to 1e-5. If-chains stand where the C++ uses switch.

struct NoiseParams {
    w: u32, h: u32, func: u32, seed: u32,
    scale: f32, z: f32, strength: f32, pad: f32,
}

fn lowbias32(x0: u32) -> u32 {
    var x: u32 = x0;
    x = x ^ (x >> 16u);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15u);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16u);
    return x;
}
fn nhash(x: u32, y: u32, z: u32, seed: u32) -> u32 {
    return lowbias32(x ^ lowbias32(y ^ lowbias32(z ^ lowbias32(seed))));
}
fn unit01(h: u32) -> f32 {
    return f32(h >> 8u) * (1.0 / 16777216.0);
}
fn fade(t: f32) -> f32 {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}
fn nlerp(a: f32, b: f32, t: f32) -> f32 {
    return a + (b - a) * t;
}
fn fl(v: f32) -> i32 {
    return i32(floor(v));
}
fn white(x: i32, y: i32, z: i32, seed: u32) -> f32 {
    return unit01(nhash(u32(x), u32(y), u32(z), seed));
}
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
fn grad3(h: u32, x: f32, y: f32, z: f32) -> f32 {
    let k: u32 = (h >> 4u) % 12u;
    if (k == 0u) { return x + y; }
    if (k == 1u) { return -x + y; }
    if (k == 2u) { return x - y; }
    if (k == 3u) { return -x - y; }
    if (k == 4u) { return x + z; }
    if (k == 5u) { return -x + z; }
    if (k == 6u) { return x - z; }
    if (k == 7u) { return -x - z; }
    if (k == 8u) { return y + z; }
    if (k == 9u) { return -y + z; }
    if (k == 10u) { return y - z; }
    return -y - z;
}
fn value2(x: f32, y: f32, seed: u32) -> f32 {
    let i: i32 = fl(x);
    let j: i32 = fl(y);
    let fx: f32 = fade(x - f32(i));
    let fy: f32 = fade(y - f32(j));
    let a: f32 = white(i, j, 0, seed);
    let b: f32 = white(i + 1, j, 0, seed);
    let c: f32 = white(i, j + 1, 0, seed);
    let d: f32 = white(i + 1, j + 1, 0, seed);
    return nlerp(nlerp(a, b, fx), nlerp(c, d, fx), fy);
}
fn value3(x: f32, y: f32, z: f32, seed: u32) -> f32 {
    let i: i32 = fl(x);
    let j: i32 = fl(y);
    let k: i32 = fl(z);
    let fx: f32 = fade(x - f32(i));
    let fy: f32 = fade(y - f32(j));
    let fz: f32 = fade(z - f32(k));
    let lo: f32 = nlerp(nlerp(white(i, j, k, seed), white(i + 1, j, k, seed), fx), nlerp(white(i, j + 1, k, seed), white(i + 1, j + 1, k, seed), fx), fy);
    let hi: f32 = nlerp(nlerp(white(i, j, k + 1, seed), white(i + 1, j, k + 1, seed), fx), nlerp(white(i, j + 1, k + 1, seed), white(i + 1, j + 1, k + 1, seed), fx), fy);
    return nlerp(lo, hi, fz);
}
fn pg2(i: i32, j: i32, a: i32, b: i32, rx: f32, ry: f32, seed: u32) -> f32 {
    return grad2(nhash(u32(i + a), u32(j + b), 0u, seed), rx - f32(a), ry - f32(b));
}
fn perlin2(x: f32, y: f32, seed: u32) -> f32 {
    let i: i32 = fl(x);
    let j: i32 = fl(y);
    let rx: f32 = x - f32(i);
    let ry: f32 = y - f32(j);
    let fx: f32 = fade(rx);
    let fy: f32 = fade(ry);
    return nlerp(nlerp(pg2(i, j, 0, 0, rx, ry, seed), pg2(i, j, 1, 0, rx, ry, seed), fx), nlerp(pg2(i, j, 0, 1, rx, ry, seed), pg2(i, j, 1, 1, rx, ry, seed), fx), fy) * 0.70710678;
}
fn pg3(i: i32, j: i32, k: i32, a: i32, b: i32, c: i32, rx: f32, ry: f32, rz: f32, seed: u32) -> f32 {
    return grad3(nhash(u32(i + a), u32(j + b), u32(k + c), seed), rx - f32(a), ry - f32(b), rz - f32(c));
}
fn perlin3(x: f32, y: f32, z: f32, seed: u32) -> f32 {
    let i: i32 = fl(x);
    let j: i32 = fl(y);
    let k: i32 = fl(z);
    let rx: f32 = x - f32(i);
    let ry: f32 = y - f32(j);
    let rz: f32 = z - f32(k);
    let fx: f32 = fade(rx);
    let fy: f32 = fade(ry);
    let fz: f32 = fade(rz);
    let lo: f32 = nlerp(nlerp(pg3(i, j, k, 0, 0, 0, rx, ry, rz, seed), pg3(i, j, k, 1, 0, 0, rx, ry, rz, seed), fx), nlerp(pg3(i, j, k, 0, 1, 0, rx, ry, rz, seed), pg3(i, j, k, 1, 1, 0, rx, ry, rz, seed), fx), fy);
    let hi: f32 = nlerp(nlerp(pg3(i, j, k, 0, 0, 1, rx, ry, rz, seed), pg3(i, j, k, 1, 0, 1, rx, ry, rz, seed), fx), nlerp(pg3(i, j, k, 0, 1, 1, rx, ry, rz, seed), pg3(i, j, k, 1, 1, 1, rx, ry, rz, seed), fx), fy);
    return nlerp(lo, hi, fz);
}
fn simplex2(x: f32, y: f32, seed: u32) -> f32 {
    let F2: f32 = 0.36602540378;
    let G2: f32 = 0.21132486541;
    let s: f32 = (x + y) * F2;
    let i: i32 = fl(x + s);
    let j: i32 = fl(y + s);
    let t: f32 = f32(i + j) * G2;
    let x0: f32 = x - (f32(i) - t);
    let y0: f32 = y - (f32(j) - t);
    let i1: i32 = select(0, 1, x0 > y0);
    let j1: i32 = 1 - i1;
    var xs: array<f32, 3>;
    var ys: array<f32, 3>;
    var di: array<i32, 3>;
    var dj: array<i32, 3>;
    xs[0] = x0; xs[1] = x0 - f32(i1) + G2; xs[2] = x0 - 1.0 + 2.0 * G2;
    ys[0] = y0; ys[1] = y0 - f32(j1) + G2; ys[2] = y0 - 1.0 + 2.0 * G2;
    di[0] = 0; di[1] = i1; di[2] = 1;
    dj[0] = 0; dj[1] = j1; dj[2] = 1;
    var n: f32 = 0.0;
    for (var c: u32 = 0u; c < 3u; c++) {
        var r: f32 = 0.5 - xs[c] * xs[c] - ys[c] * ys[c];
        if (r <= 0.0) { continue; }
        r = r * r;
        n = n + r * r * grad2(nhash(u32(i + di[c]), u32(j + dj[c]), 0u, seed), xs[c], ys[c]);
    }
    return 70.0 * n * 0.70710678;
}
fn simplex3(x: f32, y: f32, z: f32, seed: u32) -> f32 {
    let F3: f32 = 1.0 / 3.0;
    let G3: f32 = 1.0 / 6.0;
    let s: f32 = (x + y + z) * F3;
    let i: i32 = fl(x + s);
    let j: i32 = fl(y + s);
    let k: i32 = fl(z + s);
    let t: f32 = f32(i + j + k) * G3;
    let x0: f32 = x - (f32(i) - t);
    let y0: f32 = y - (f32(j) - t);
    let z0: f32 = z - (f32(k) - t);
    var a: vec3i = vec3i(0, 0, 0);
    var b: vec3i = vec3i(0, 0, 0);
    if (x0 >= y0) {
        if (y0 >= z0) { a = vec3i(1, 0, 0); b = vec3i(1, 1, 0); } else if (x0 >= z0) { a = vec3i(1, 0, 0); b = vec3i(1, 0, 1); } else { a = vec3i(0, 0, 1); b = vec3i(1, 0, 1); }
    } else {
        if (y0 < z0) { a = vec3i(0, 0, 1); b = vec3i(0, 1, 1); } else if (x0 < z0) { a = vec3i(0, 1, 0); b = vec3i(0, 1, 1); } else { a = vec3i(0, 1, 0); b = vec3i(1, 1, 0); }
    }
    var ox: array<i32, 4>;
    var oy: array<i32, 4>;
    var oz: array<i32, 4>;
    ox[0] = 0; ox[1] = a.x; ox[2] = b.x; ox[3] = 1;
    oy[0] = 0; oy[1] = a.y; oy[2] = b.y; oy[3] = 1;
    oz[0] = 0; oz[1] = a.z; oz[2] = b.z; oz[3] = 1;
    var n: f32 = 0.0;
    for (var c: u32 = 0u; c < 4u; c++) {
        let g: f32 = G3 * f32(c);
        let px: f32 = x0 - f32(ox[c]) + g;
        let py: f32 = y0 - f32(oy[c]) + g;
        let pz: f32 = z0 - f32(oz[c]) + g;
        var r: f32 = 0.6 - px * px - py * py - pz * pz;
        if (r <= 0.0) { continue; }
        r = r * r;
        n = n + r * r * grad3(nhash(u32(i + ox[c]), u32(j + oy[c]), u32(k + oz[c]), seed), px, py, pz);
    }
    return 32.0 * n;
}
fn worley2(x: f32, y: f32, seed: u32) -> vec2f {
    let i: i32 = fl(x);
    let j: i32 = fl(y);
    var d1: f32 = 1e30;
    var d2: f32 = 1e30;
    for (var b: i32 = -1; b <= 1; b++) {
        for (var a: i32 = -1; a <= 1; a++) {
            let h: u32 = nhash(u32(i + a), u32(j + b), 0x5eedu, seed);
            let px: f32 = f32(i + a) + unit01(h);
            let py: f32 = f32(j + b) + unit01(lowbias32(h));
            let dx: f32 = px - x;
            let dy: f32 = py - y;
            let d: f32 = dx * dx + dy * dy;
            if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) { d2 = d; }
        }
    }
    return vec2f(sqrt(d1), sqrt(d2));
}
fn worley3(x: f32, y: f32, z: f32, seed: u32) -> vec2f {
    let i: i32 = fl(x);
    let j: i32 = fl(y);
    let k: i32 = fl(z);
    var d1: f32 = 1e30;
    var d2: f32 = 1e30;
    for (var c: i32 = -1; c <= 1; c++) {
        for (var b: i32 = -1; b <= 1; b++) {
            for (var a: i32 = -1; a <= 1; a++) {
                let h: u32 = nhash(u32(i + a), u32(j + b), u32(k + c), seed ^ 0x5eedu);
                let h2: u32 = lowbias32(h);
                let px: f32 = f32(i + a) + unit01(h);
                let py: f32 = f32(j + b) + unit01(h2);
                let pz: f32 = f32(k + c) + unit01(lowbias32(h2));
                let dx: f32 = px - x;
                let dy: f32 = py - y;
                let dz: f32 = pz - z;
                let d: f32 = dx * dx + dy * dy + dz * dz;
                if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) { d2 = d; }
            }
        }
    }
    return vec2f(sqrt(d1), sqrt(d2));
}
// base: 0 Perlin, 1 simplex, 2 value (mapped to [-1, 1]).
fn base3(base: u32, x: f32, y: f32, z: f32, seed: u32) -> f32 {
    if (base == 0u) { return perlin3(x, y, z, seed); }
    if (base == 1u) { return simplex3(x, y, z, seed); }
    return value3(x, y, z, seed) * 2.0 - 1.0;
}
fn fbm(base: u32, x: f32, y: f32, z: f32, seed: u32) -> f32 {
    var sum: f32 = 0.0;
    var amp: f32 = 1.0;
    var norm: f32 = 0.0;
    var f: f32 = 1.0;
    for (var k: u32 = 0u; k < 5u; k++) {
        sum = sum + amp * base3(base, x * f, y * f, z * f, seed + k);
        norm = norm + amp;
        amp = amp * 0.5;
        f = f * 2.0;
    }
    return sum / norm;
}
fn ridged(base: u32, x: f32, y: f32, z: f32, seed: u32) -> f32 {
    var sum: f32 = 0.0;
    var amp: f32 = 1.0;
    var norm: f32 = 0.0;
    var f: f32 = 1.0;
    var weight: f32 = 1.0;
    for (var k: u32 = 0u; k < 5u; k++) {
        var s: f32 = 1.0 - abs(base3(base, x * f, y * f, z * f, seed + k));
        s = s * s * weight;
        weight = select(select(s * 2.0, 0.0, s * 2.0 < 0.0), 1.0, s * 2.0 > 1.0);
        sum = sum + amp * s;
        norm = norm + amp;
        amp = amp * 0.5;
        f = f * 2.0;
    }
    return sum / norm;
}
fn billow(base: u32, x: f32, y: f32, z: f32, seed: u32) -> f32 {
    var sum: f32 = 0.0;
    var amp: f32 = 1.0;
    var norm: f32 = 0.0;
    var f: f32 = 1.0;
    for (var k: u32 = 0u; k < 5u; k++) {
        sum = sum + amp * (2.0 * abs(base3(base, x * f, y * f, z * f, seed + k)) - 1.0);
        norm = norm + amp;
        amp = amp * 0.5;
        f = f * 2.0;
    }
    return sum / norm;
}
fn hybrid(base: u32, x: f32, y: f32, z: f32, seed: u32) -> f32 {
    var amp: f32 = 1.0;
    var f: f32 = 1.0;
    var result: f32 = base3(base, x, y, z, seed) + 0.7;
    var weight: f32 = result;
    for (var k: u32 = 1u; k < 5u; k++) {
        amp = amp * 0.5;
        f = f * 2.0;
        if (weight > 1.0) { weight = 1.0; }
        let signal: f32 = (base3(base, x * f, y * f, z * f, seed + k) + 0.7) * amp;
        result = result + weight * signal;
        weight = weight * signal;
    }
    return select(result, 0.0, result < 0.0);
}
fn warp(x: f32, y: f32, z: f32, seed: u32, strength: f32) -> vec3f {
    let wx: f32 = fbm(1u, x + 1.7, y + 9.2, z + 3.1, seed + 101u);
    let wy: f32 = fbm(1u, x + 8.3, y + 2.8, z + 6.5, seed + 202u);
    let wz: f32 = fbm(1u, x + 4.6, y + 7.4, z + 0.9, seed + 303u);
    return vec3f(x + strength * wx, y + strength * wy, z + strength * wz);
}

//@pass noise_eval
// One invocation per pixel of a w x h grid: function P.func at (x, y) x P.scale, slice P.z, seed
// P.seed, the same parameters as raw::noiseGoldens. B holds the 64 x 64 blue-noise ranks.
@group(0) @binding(0) var<uniform> P: NoiseParams;
@group(0) @binding(1) var<storage, read> B: array<u32>;
@group(0) @binding(2) var<storage, read_write> O: array<f32>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let x: f32 = f32(gid.x) * P.scale;
    let y: f32 = f32(gid.y) * P.scale;
    let ix: i32 = i32(gid.x);
    let iy: i32 = i32(gid.y);
    var v: f32 = 0.0;
    if (P.func == 0u) { v = white(ix, iy, 0, P.seed); }
    if (P.func == 1u) { v = value2(x, y, P.seed); }
    if (P.func == 2u) { v = value3(x, y, P.z, P.seed); }
    if (P.func == 3u) { v = perlin2(x, y, P.seed); }
    if (P.func == 4u) { v = perlin3(x, y, P.z, P.seed); }
    if (P.func == 5u) { v = simplex2(x, y, P.seed); }
    if (P.func == 6u) { v = simplex3(x, y, P.z, P.seed); }
    if (P.func == 7u) { v = worley2(x, y, P.seed).x; }
    if (P.func == 8u) { let c: vec2f = worley2(x, y, P.seed); v = c.y - c.x; }
    if (P.func == 9u) { v = worley3(x, y, P.z, P.seed).x; }
    if (P.func == 10u) { v = fbm(0u, x, y, P.z, P.seed); }
    if (P.func == 11u) { v = ridged(1u, x, y, P.z, P.seed); }
    if (P.func == 12u) { v = billow(0u, x, y, P.z, P.seed); }
    if (P.func == 13u) { v = hybrid(1u, x, y, P.z, P.seed); }
    if (P.func == 14u) { let w: vec3f = warp(x, y, P.z, P.seed, P.strength); v = fbm(1u, w.x, w.y, w.z, P.seed + 2u); }
    if (P.func == 15u) { v = (f32(B[(gid.y & 63u) * 64u + (gid.x & 63u)]) + 0.5) * (1.0 / 4096.0); }
    O[gid.y * P.w + gid.x] = v;
}
