//@pass art_ts_coc
// P23, the tilt-shift lens: each pixel's circle of confusion against the tilted plane of focus, and
// its axial depth. The float64 reference is raw::art::cocMap (raw/renderer/art_tiltshift.hpp).
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> dist: array<f32>;
@group(0) @binding(2) var<storage, read> Dw: array<f32>;
@group(0) @binding(3) var<storage, read_write> C: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let d: vec3f = vec3f(Dw[3u * p], Dw[3u * p + 1u], Dw[3u * p + 2u]);
    let cosA: f32 = dot(d, U.fwd.xyz);
    let dn: f32 = dot(d, U.pn.xyz);
    var tPlane: f32 = -1.0;
    if (abs(dn) > 1.0e-12) { tPlane = (U.pn.w - dot(U.eye.xyz, U.pn.xyz)) / dn; }
    let skyZ: bool = dist[p] < 0.0;
    let skyS: bool = tPlane <= 0.0;
    let fpx: f32 = U.eye.w;
    let fmm: f32 = U.fwd.w;
    let N: f32 = U.lens.y;
    var c: f32 = 0.0;
    let S: f32 = max(tPlane * cosA * U.lens.x, 2.0 * fmm);
    if (skyZ && !skyS) { c = fpx * fmm / (N * (S - fmm)); }
    if (!skyZ) {
        let Z: f32 = dist[p] * cosA * U.lens.x;
        if (skyS) { c = fpx * fmm / (N * Z); } else { c = fpx * fmm * abs(Z - S) / (N * Z * (S - fmm)); }
    }
    var z: f32 = 1.0e6;
    if (!skyZ) { z = dist[p] * cosA; }
    C[p] = vec4f(min(c, U.lens.z), z, 0.0, 0.0);
}

//@pass art_ts_weight
// The painted defocus's weight: smoothstep(c0, c1, c), zero within `rim` pixels of a nearer pixel in
// focus (the rim guard keeps an in-focus figure's ink contour). Reference raw::art::defocusWeight.
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> C: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> CW: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let x: i32 = i32(p % U.width);
    let y: i32 = i32(p / U.width);
    let c: f32 = C[p].x;
    let z: f32 = C[p].y;
    var wt: f32 = smoothstep(U.dp.x, U.dp.y, c);
    let R: i32 = i32(ceil(U.dp.w));
    for (var dy: i32 = -R; dy <= R; dy++) {
        for (var dx: i32 = -R; dx <= R; dx++) {
            if (f32(dx * dx + dy * dy) > U.dp.w * U.dp.w) { continue; }
            let q: vec4f = C[ts_index(x + dx, y + dy, U.width, U.height)];
            if (q.x <= U.dp.x && q.y < 0.98 * z) { wt = 0.0; }
        }
    }
    CW[p] = vec4f(c, z, wt, 0.0);
}

//@pass art_ts_prep
// The display colour's sRGB encoding and its Kubelka-Munk latent (concentrations and the residual
// against their decode), as the paint prep does. Reference: the prep of raw::art::paintDefocus.
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> pig: array<f32>;
@group(0) @binding(2) var<storage, read> img: array<vec4f>;
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
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let col: vec3f = img[p].xyz;
    let e: vec3f = vec3f(ts_srgb(col.x), ts_srgb(col.y), ts_srgb(col.z));
    let c: vec4f = lut_conc(e);
    sb[p] = vec4f(e, 0.0);
    lat[2u * p] = c;
    lat[2u * p + 1u] = vec4f(col - km_decode(c), 0.0);
}

//@pass art_ts_tensor
// The structure tensor of the display colour (Sobel on sRGB).
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> sb: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> T: array<vec4f>;

fn at(x: i32, y: i32) -> vec3f { return sb[ts_index(x, y, U.width, U.height)].xyz; }

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let x: i32 = i32(p % U.width);
    let y: i32 = i32(p / U.width);
    let gx: vec3f = (at(x + 1, y - 1) + 2.0 * at(x + 1, y) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2.0 * at(x - 1, y) - at(x - 1, y + 1)) / 4.0;
    let gy: vec3f = (at(x - 1, y + 1) + 2.0 * at(x, y + 1) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2.0 * at(x, y - 1) - at(x + 1, y - 1)) / 4.0;
    T[p] = vec4f(dot(gx, gx), dot(gx, gy), dot(gy, gy), 0.0);
}

//@pass art_ts_blur
// One pass of the tensor's separable gaussian (U.dir 0 along x, 1 along y).
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> wts: array<f32>;
@group(0) @binding(2) var<storage, read> a: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> o: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let x: i32 = i32(p % U.width);
    let y: i32 = i32(p / U.width);
    let r: i32 = i32(U.blur_r);
    var s: vec4f = vec4f(0.0);
    for (var k: i32 = -r; k <= r; k++) {
        var q: u32 = ts_index(x + k, y, U.width, U.height);
        if (U.dir != 0u) { q = ts_index(x, y + k, U.width, U.height); }
        s = s + wts[u32(k + r)] * a[q];
    }
    o[p] = s;
}

//@pass art_ts_defocus
// The painted defocus: where the weight is above zero, an anisotropic Kuwahara of radius c / 2 along
// the flow over a fixed 48-sample Vogel disc (sector weights from the table), averaging pigment
// latents; a nearer sample counts only by its own reach (scatter as gather). Reference
// raw::art::paintDefocus (DefocusMode::Paint), run at half resolution on the unlined colour
// (U.lens.w = 0.5 scales the radius and reach); the compose mixes it in. The sample table (48 (x, y), then 48 x 8 sector
// weights) follows the 19,993 pigment floats in the same buffer.
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> pig: array<f32>;
@group(0) @binding(2) var<storage, read> img: array<vec4f>;
@group(0) @binding(3) var<storage, read> sb: array<vec4f>;
@group(0) @binding(4) var<storage, read> lat: array<vec4f>;
@group(0) @binding(5) var<storage, read> T: array<vec4f>;
@group(0) @binding(6) var<storage, read> CW: array<vec4f>;
@group(0) @binding(7) var<storage, read_write> outc: array<vec4f>;

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

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let src: vec4f = img[p];
    let cw: vec4f = CW[p];
    if (cw.z <= 0.0) { outc[p] = vec4f(src.xyz, 1.0); return; }
    let x: f32 = f32(p % U.width);
    let y: f32 = f32(p / U.width);
    // the flow: along edges, and the anisotropy
    let E: f32 = T[p].x;
    let F: f32 = T[p].y;
    let G: f32 = T[p].z;
    let tr: f32 = E + G;
    let dsc: f32 = sqrt((E - G) * (E - G) + 4.0 * F * F);
    let l1: f32 = 0.5 * (tr + dsc);
    let l2: f32 = 0.5 * (tr - dsc);
    var gx: f32 = F;
    var gy: f32 = l1 - E;
    if (gx * gx + gy * gy < (l1 - G) * (l1 - G) + F * F) { gx = l1 - G; gy = F; }
    let m: f32 = sqrt(gx * gx + gy * gy);
    var tx: f32 = 1.0;
    var ty: f32 = 0.0;
    var A: f32 = 0.0;
    if (m > 1.0e-12 && tr > 1.0e-12) { tx = -gy / m; ty = gx / m; A = (l1 - l2) / (l1 + l2); }
    let rad: f32 = 0.5 * cw.x * U.lens.w;
    let a: f32 = rad * (1.0 + A);
    let b: f32 = rad / (1.0 + A);
    var qs: array<u32, 32>;
    var wo: array<f32, 32>;
    var acc: array<f32, 56>;   // per sector: weight, mean (3), mean square (3)
    for (var s: i32 = 0; s < 32; s++) {
        let u: f32 = pig[19993 + 2 * s];
        let v: f32 = pig[19993 + 2 * s + 1];
        let ox: f32 = tx * a * u - ty * b * v;
        let oy: f32 = ty * a * u + tx * b * v;
        let q: u32 = ts_index(i32(floor(x + ox + 0.5)), i32(floor(y + oy + 0.5)), U.width, U.height);
        var o: f32 = 1.0;
        let cq: vec4f = CW[q];
        if (cq.y < cw.y) { o = clamp(0.5 * cq.x * U.lens.w - sqrt(ox * ox + oy * oy) + 1.0, 0.0, 1.0); }
        qs[s] = q;
        wo[s] = o;
        let sv: vec3f = sb[q].xyz;
        for (var k: i32 = 0; k < 8; k++) {
            let g: f32 = pig[20057 + s * 8 + k] * o;
            let j: i32 = k * 7;
            acc[j] = acc[j] + g;
            acc[j + 1] = acc[j + 1] + g * sv.x;
            acc[j + 2] = acc[j + 2] + g * sv.y;
            acc[j + 3] = acc[j + 3] + g * sv.z;
            acc[j + 4] = acc[j + 4] + g * sv.x * sv.x;
            acc[j + 5] = acc[j + 5] + g * sv.y * sv.y;
            acc[j + 6] = acc[j + 6] + g * sv.z * sv.z;
        }
    }
    var alpha: array<f32, 8>;
    for (var ka: i32 = 0; ka < 8; ka++) {
        let j: i32 = ka * 7;
        let W: f32 = acc[j];
        if (W <= 0.0) { alpha[ka] = 0.0; continue; }
        let mm: vec3f = vec3f(acc[j + 1], acc[j + 2], acc[j + 3]) / W;
        let m2: vec3f = vec3f(acc[j + 4], acc[j + 5], acc[j + 6]) / W;
        let v2: f32 = max(0.0, m2.x - mm.x * mm.x) + max(0.0, m2.y - mm.y * mm.y) + max(0.0, m2.z - mm.z * mm.z);
        let sd: f32 = sqrt(v2) * 255.0;
        var pw: f32 = 0.0;
        if (sd > 1.0e-6) { pw = pow(sd, U.dp.z); }
        alpha[ka] = 1.0 / (1.0 + pw) / W;
    }
    var L0: vec4f = vec4f(0.0);
    var L1: vec3f = vec3f(0.0);
    var ws: f32 = 0.0;
    for (var s2: i32 = 0; s2 < 32; s2++) {
        var c: f32 = 0.0;
        for (var kb: i32 = 0; kb < 8; kb++) { c = c + alpha[kb] * pig[20057 + s2 * 8 + kb]; }
        c = c * wo[s2];
        ws = ws + c;
        L0 = L0 + c * lat[2u * qs[s2]];
        L1 = L1 + c * lat[2u * qs[s2] + 1u].xyz;
    }
    if (ws <= 0.0) { outc[p] = vec4f(src.xyz, 1.0); return; }
    let res: vec3f = km_decode(L0 / ws) + L1 / ws;
    outc[p] = vec4f(res, 1.0);
}

//@pass art_ts_half
// Addendum C: the half-resolution input of the painted defocus. The 2 x 2 mean of the unlined colour
// (rgb / max(alpha, 0.05), alpha being the paint frame's line factor) and of the CoC and depth, and
// the half-resolution weight smoothstep(c0, c1, c). U is the half-resolution parameters; U.qw and
// U.qh carry the full frame's width and height. Reference raw::art::unlinedHalf.
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> img: array<vec4f>;
@group(0) @binding(2) var<storage, read> C: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> hu: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> hcw: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let x: i32 = i32(p % U.width);
    let y: i32 = i32(p / U.width);
    var c: vec3f = vec3f(0.0);
    var cz: vec2f = vec2f(0.0);
    for (var j: i32 = 0; j < 2; j++) {
        for (var i: i32 = 0; i < 2; i++) {
            let q: u32 = ts_index(2 * x + i, 2 * y + j, U.qw, U.qh);
            let v: vec4f = img[q];
            c = c + v.xyz / max(v.w, 0.05) / 4.0;
            cz = cz + C[q].xy / 4.0;
        }
    }
    hu[p] = vec4f(c, 1.0);
    hcw[p] = vec4f(cz.x, cz.y, smoothstep(U.dp.x, U.dp.y, cz.x), 0.0);
}

//@pass art_ts_compose
// Addendum C: the full-resolution compose. The paint frame mixed with the bilinearly upsampled
// half-resolution defocus by the full-resolution weight (with its rim guard), so in-focus pixels
// stay exact and the ink fades as (1 - w). U is the full-resolution parameters; U.qw and U.qh are
// unused here, the half frame's size is (width + 1) / 2 by (height + 1) / 2.
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> img: array<vec4f>;
@group(0) @binding(2) var<storage, read> hc: array<vec4f>;
@group(0) @binding(3) var<storage, read> CW: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> outc: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let src: vec4f = img[p];
    let wi: f32 = CW[p].z;
    if (wi <= 0.0) { outc[p] = src; return; }
    let hw: u32 = (U.width + 1u) / 2u;
    let hh: u32 = (U.height + 1u) / 2u;
    let fx: f32 = clamp((f32(p % U.width) + 0.5) / 2.0 - 0.5, 0.0, f32(hw - 1u));
    let fy: f32 = clamp((f32(p / U.width) + 0.5) / 2.0 - 0.5, 0.0, f32(hh - 1u));
    let x0: u32 = min(u32(floor(fx)), hw - 1u);
    let y0: u32 = min(u32(floor(fy)), hh - 1u);
    let x1: u32 = min(x0 + 1u, hw - 1u);
    let y1: u32 = min(y0 + 1u, hh - 1u);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    let r: vec3f = (hc[y0 * hw + x0].xyz * (1.0 - tx) + hc[y0 * hw + x1].xyz * tx) * (1.0 - ty) + (hc[y1 * hw + x0].xyz * (1.0 - tx) + hc[y1 * hw + x1].xyz * tx) * ty;
    outc[p] = vec4f(src.xyz * (1.0 - wi) + r * wi, src.w * (1.0 - wi) + wi);
}

//@pass art_ts_bright
// The effects' warm bright pass at quarter resolution: the 4 x 4 block's mean through a soft knee,
// weighted by warmth. Reference raw::art::fxBright.
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> img: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> B: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.qw * U.qh) { return; }
    let x: u32 = p % U.qw;
    let y: u32 = p / U.qw;
    var c: vec3f = vec3f(0.0);
    for (var j: u32 = 0u; j < 4u; j++) {
        for (var i: u32 = 0u; i < 4u; i++) {
            let q: u32 = min(4u * y + j, U.height - 1u) * U.width + min(4u * x + i, U.width - 1u);
            c = c + img[q].xyz / 16.0;
        }
    }
    let L: f32 = ts_luma(c);
    var e: f32 = 0.0;
    if (L > U.fx3.z) { e = (L - U.fx3.z) * (L - U.fx3.z) / (L - U.fx3.z + U.fx3.w); }
    let warm: f32 = clamp(3.0 * (c.x - c.z) / (c.x + c.y + c.z + 1.0e-4), 0.0, 1.0);
    B[p] = vec4f(c * (e / max(L, 1.0e-4) * warm), 0.0);
}

//@pass art_ts_glow
// Halation (a 24-tap disc) and the star filter (four lines, 16 taps each way, after design
// reference 17) at quarter resolution. Reference raw::art::fxGlow.
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> ht: array<f32>;
@group(0) @binding(2) var<storage, read> B: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> G: array<vec4f>;

fn bq(fx0: f32, fy0: f32) -> vec3f {
    let fx: f32 = clamp(fx0, 0.0, f32(U.qw - 1u));
    let fy: f32 = clamp(fy0, 0.0, f32(U.qh - 1u));
    let x0: u32 = min(u32(floor(fx)), U.qw - 1u);
    let y0: u32 = min(u32(floor(fy)), U.qh - 1u);
    let x1: u32 = min(x0 + 1u, U.qw - 1u);
    let y1: u32 = min(y0 + 1u, U.qh - 1u);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    return (B[y0 * U.qw + x0].xyz * (1.0 - tx) + B[y0 * U.qw + x1].xyz * tx) * (1.0 - ty) + (B[y1 * U.qw + x0].xyz * (1.0 - tx) + B[y1 * U.qw + x1].xyz * tx) * ty;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.qw * U.qh) { return; }
    let x: f32 = f32(p % U.qw);
    let y: f32 = f32(p / U.qw);
    var halo: vec3f = vec3f(0.0);
    for (var i: u32 = 0u; i < 24u; i++) { halo = halo + bq(x + U.fx3.x * ht[2u * i], y + U.fx3.x * ht[2u * i + 1u]) / 24.0; }
    var star: vec3f = vec3f(0.0);
    for (var j: i32 = 0; j < 4; j++) {
        var d: vec2f = U.sd0.xy;
        if (j == 1) { d = U.sd0.zw; }
        if (j == 2) { d = U.sd1.xy; }
        if (j == 3) { d = U.sd1.zw; }
        for (var sg: i32 = -1; sg <= 1; sg += 2) {
            for (var m: i32 = 1; m <= 16; m++) {
                let f: f32 = 1.0 - f32(m) / 17.0;
                let s: f32 = f32(sg * m) * U.fx3.y;
                star = star + (f * f / 8.0) * bq(x + s * d.x, y + s * d.y);
            }
        }
    }
    G[2u * p] = vec4f(halo, 0.0);
    G[2u * p + 1u] = vec4f(star, 0.0);
}

//@pass art_ts_finish
// The full-resolution finish: gate weave, edge chromatic aberration and misregistration in the
// sampling, halation and star added, print look, halftone, optical and mechanical vignette, grain;
// written as 8-bit sRGB codes. Reference raw::art::fxFinish.
@group(0) @binding(0) var<uniform> U: TsParams;
@group(0) @binding(1) var<storage, read> img: array<vec4f>;
@group(0) @binding(2) var<storage, read> G: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> out8: array<u32>;

fn bi(fx0: f32, fy0: f32) -> vec4f {
    let fx: f32 = clamp(fx0, 0.0, f32(U.width - 1u));
    let fy: f32 = clamp(fy0, 0.0, f32(U.height - 1u));
    let x0: u32 = min(u32(floor(fx)), U.width - 1u);
    let y0: u32 = min(u32(floor(fy)), U.height - 1u);
    let x1: u32 = min(x0 + 1u, U.width - 1u);
    let y1: u32 = min(y0 + 1u, U.height - 1u);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    return (img[y0 * U.width + x0] * (1.0 - tx) + img[y0 * U.width + x1] * tx) * (1.0 - ty) + (img[y1 * U.width + x0] * (1.0 - tx) + img[y1 * U.width + x1] * tx) * ty;
}
fn bg(fx0: f32, fy0: f32, k: u32) -> vec3f {
    let fx: f32 = clamp(fx0, 0.0, f32(U.qw - 1u));
    let fy: f32 = clamp(fy0, 0.0, f32(U.qh - 1u));
    let x0: u32 = min(u32(floor(fx)), U.qw - 1u);
    let y0: u32 = min(u32(floor(fy)), U.qh - 1u);
    let x1: u32 = min(x0 + 1u, U.qw - 1u);
    let y1: u32 = min(y0 + 1u, U.qh - 1u);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    return (G[2u * (y0 * U.qw + x0) + k].xyz * (1.0 - tx) + G[2u * (y0 * U.qw + x1) + k].xyz * tx) * (1.0 - ty) +
           (G[2u * (y1 * U.qw + x0) + k].xyz * (1.0 - tx) + G[2u * (y1 * U.qw + x1) + k].xyz * tx) * ty;
}
fn cat_eye(e: f32, rb: f32) -> f32 {
    if (e <= rb - 1.0) { return 1.0; }
    if (e >= rb + 1.0) { return 0.0; }
    let a1: f32 = acos(clamp((e * e + 1.0 - rb * rb) / (2.0 * e), -1.0, 1.0));
    let a2: f32 = acos(clamp((e * e + rb * rb - 1.0) / (2.0 * e * rb), -1.0, 1.0));
    let k: f32 = sqrt(max(0.0, (-e + 1.0 + rb) * (e + 1.0 - rb) * (e - 1.0 + rb) * (e + 1.0 + rb)));
    return (a1 + rb * rb * a2 - 0.5 * k) / 3.141592653589793;
}
fn enc(v: f32) -> u32 { return u32(floor(ts_srgb(v) * 255.0 + 0.5)); }

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let xi: u32 = p % U.width;
    let yi: u32 = p / U.width;
    let W: f32 = f32(U.width);
    let H: f32 = f32(U.height);
    let px: f32 = f32(xi) + 0.5;
    let py: f32 = f32(yi) + 0.5;
    let cx: f32 = 0.5 * W;
    let cy: f32 = 0.5 * H;
    let dx: f32 = px - cx;
    let dy: f32 = py - cy;
    let R2: f32 = cx * cx + cy * cy;
    let rn2: f32 = (dx * dx + dy * dy) / R2;
    let k: f32 = U.fx1.x * (H / 1080.0) * rn2 / sqrt(R2);
    let mx: f32 = U.fx2.x * H / 1080.0;
    let bx: f32 = f32(xi) + U.fx2.y;
    let by: f32 = f32(yi) + U.fx2.z;
    var c: vec3f = vec3f(bi(bx + k * dx + mx, by + k * dy + 0.5 * mx).x, bi(bx, by).y, bi(bx - k * dx - mx, by - k * dy - 0.3 * mx).z);
    let gx: f32 = px / 4.0 - 0.5;
    let gy: f32 = py / 4.0 - 0.5;
    c = c + U.fx4.x * (U.fx0.z * bg(gx, gy, 0u) * vec3f(1.0, 0.55, 0.22) + U.fx0.w * bg(gx, gy, 1u) * vec3f(1.0, 0.82, 0.5));
    let lift: vec3f = vec3f(0.05, 0.04, 0.035);
    c = c + U.fx1.z * (lift + (vec3f(0.95, 0.90, 0.80) - lift) * c - c);
    if (U.fx4.y != 1.0 || U.fx4.w != 0.0) {
        let hc: f32 = U.fx4.z;
        let hs: f32 = U.fx4.w;
        let k1: f32 = (1.0 - hc) / 3.0;
        let k2: f32 = hs / sqrt(3.0);
        let r0: f32 = c.x * (hc + k1) + c.y * (k1 - k2) + c.z * (k1 + k2);
        let r1: f32 = c.x * (k1 + k2) + c.y * (hc + k1) + c.z * (k1 - k2);
        let r2: f32 = c.x * (k1 - k2) + c.y * (k1 + k2) + c.z * (hc + k1);
        let r: vec3f = vec3f(r0, r1, r2);
        let y0: f32 = ts_luma(r);
        c = vec3f(y0) + U.fx4.y * (r - vec3f(y0));
    }
    if (U.fx1.w > 0.0) {
        let cell: f32 = U.fx2.w;
        let u: f32 = (px + py) * 0.7071067811865476 / cell;
        let v: f32 = (py - px) * 0.7071067811865476 / cell;
        let fu: f32 = u - floor(u) - 0.5;
        let fv: f32 = v - floor(v) - 0.5;
        let d: f32 = sqrt(fu * fu + fv * fv);
        let rad: f32 = 0.7071067811865476 * sqrt(clamp(1.0 - ts_luma(c), 0.0, 1.0));
        let ink: f32 = clamp((rad - d) * cell + 0.5, 0.0, 1.0);
        let pp: vec3f = vec3f(0.90, 0.87, 0.80);
        c = c + U.fx1.w * (pp + (vec3f(0.08, 0.08, 0.10) - pp) * ink - c);
    }
    let tan2: f32 = (dx * dx + dy * dy) / (U.eye.w * U.eye.w);
    let vnat: f32 = 1.0 / ((1.0 + tan2) * (1.0 + tan2));
    let vig: f32 = (1.0 + U.fx0.x * (vnat - 1.0)) * (1.0 + U.fx0.y * (cat_eye(sqrt(rn2), 1.15) - 1.0));
    let n: f32 = ts_hash3(xi, yi, U.seed) - 0.5;
    let lum: f32 = clamp(ts_luma(c), 0.0, 1.0);
    let o: vec3f = max(vec3f(0.0), c * vig + vec3f(U.fx1.y * n * (0.25 + lum)));
    out8[p] = enc(o.x) | (enc(o.y) << 8u) | (enc(o.z) << 16u) | (255u << 24u);
}
