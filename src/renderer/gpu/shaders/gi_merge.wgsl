// Split radiance cascades: the merge, the harmonics and the gather (gi_common.wgsl goes in front).

//@pass gi_merge
// One invocation per (slot, bin) of cascade P.cascade: its interval's mean radiance plus what passes
// it times the next cascade's merged radiance, interpolated over the eight next-cascade probes of the
// same bucket around this probe's cell centre and averaged over the four child bins (mergeOne).
// P.control 1 (the test-only control) ignores the interval's occlusion.
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read> ACC: array<u32>;
@group(0) @binding(2) var<storage, read> SLOT: array<u32>;
@group(0) @binding(3) var<storage, read> KEYS: array<u32>;
@group(0) @binding(4) var<storage, read_write> RM: array<f32>;

// The next cascade's merged radiance of child bins (2i..2i+1, 2j..2j+1), interpolated at q.
fn far(q: vec3f, b: u32, i: u32, j: u32) -> vec3f {
    let c: u32 = P.cascade + 1u;
    let d: vec4u = dimOf(c, P.d0, P.d1, P.d2, P.d3);
    let cell: f32 = pick(P.cell, c);
    let nb: u32 = pickU(P.nbin, c);
    let g: vec3f = (q - P.origin.xyz) / cell - vec3f(0.5);
    let i0: vec3f = floor(g);
    let fr: vec3f = g - i0;
    var sum: f32 = 0.0;
    var acc: vec3f = vec3f(0.0);
    for (var k: u32 = 0u; k < 8u; k++) {
        let dx: u32 = k & 1u;
        let dy: u32 = (k >> 1u) & 1u;
        let dz: u32 = (k >> 2u) & 1u;
        let ix: i32 = i32(i0.x) + i32(dx);
        let iy: i32 = i32(i0.y) + i32(dy);
        let iz: i32 = i32(i0.z) + i32(dz);
        if (ix < 0 || iy < 0 || iz < 0 || ix >= i32(d.x) || iy >= i32(d.y) || iz >= i32(d.z)) { continue; }
        let w: f32 = select(1.0 - fr.x, fr.x, dx == 1u) * select(1.0 - fr.y, fr.y, dy == 1u) * select(1.0 - fr.z, fr.z, dz == 1u);
        if (w <= 0.0) { continue; }
        let gs: u32 = SLOT[d.w + ((u32(iz) * d.y + u32(iy)) * d.x + u32(ix)) * 6u + b];
        if (gs == NONE) { continue; }
        let base: u32 = pickU(P.rbase, c) + (gs - pickU(P.sbase, c)) * nb * nb * 4u;
        var m: vec3f = vec3f(0.0);
        for (var q4: u32 = 0u; q4 < 4u; q4++) {
            let at: u32 = base + ((2u * j + (q4 >> 1u)) * nb + 2u * i + (q4 & 1u)) * 4u;
            m = m + vec3f(RM[at], RM[at + 1u], RM[at + 2u]);
        }
        acc = acc + m * (0.25 * w);
        sum = sum + w;
    }
    if (sum <= 0.0) { return vec3f(0.0); }
    return acc / sum;
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let idx: u32 = flat(gid);
    if (idx >= P.count) { return; }
    let c: u32 = P.cascade;
    let nb: u32 = pickU(P.nbin, c);
    let slot: u32 = idx / (nb * nb);
    let bin: u32 = idx % (nb * nb);
    let d: vec4u = dimOf(c, P.d0, P.d1, P.d2, P.d3);
    let key: u32 = KEYS[pickU(P.sbase, c) + slot] - d.w;
    let cellIndex: u32 = key / 6u;
    let cell: f32 = pick(P.cell, c);
    let q: vec3f = P.origin.xyz + vec3f(f32(cellIndex % d.x) + 0.5, f32((cellIndex / d.x) % d.y) + 0.5, f32(cellIndex / (d.x * d.y)) + 0.5) * cell;
    var fv: vec3f = vec3f(0.0);
    if (c < 3u) { fv = far(q, key % 6u, bin % nb, bin / nb); }
    let a: u32 = pickU(P.abase, c) + idx * 5u;
    var r: vec3f = fv;
    if (ACC[a] > 0u) {
        let n: f32 = f32(ACC[a]);
        var through: f32 = 1.0 - f32(ACC[a + 1u]) / n;
        if (P.control == 1u) { through = 1.0; }
        r = vec3f(f32(ACC[a + 2u]), f32(ACC[a + 3u]), f32(ACC[a + 4u])) / FIXED / n + fv * through;
    }
    let o: u32 = pickU(P.rbase, c) + idx * 4u;
    RM[o] = r.x;
    RM[o + 1u] = r.y;
    RM[o + 2u] = r.z;
    RM[o + 3u] = 0.0;
}

//@pass gi_sh
// One invocation per cascade-0 slot: its merged bins projected onto spherical harmonics up to l = 2
// through the sub-direction table (shOf).
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read> RM: array<f32>;
@group(0) @binding(2) var<storage, read> SUB: array<f32>;
@group(0) @binding(3) var<storage, read_write> SH: array<f32>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let s: u32 = flat(gid);
    if (s >= P.count) { return; }
    let nb: u32 = P.nbin.x;
    var acc: array<f32, 27>;
    for (var k: u32 = 0u; k < 27u; k++) { acc[k] = 0.0; }
    for (var e: u32 = 0u; e < P.nsub; e++) {
        let bin: u32 = u32(SUB[e * 5u]);
        let dir: vec3f = vec3f(SUB[e * 5u + 1u], SUB[e * 5u + 2u], SUB[e * 5u + 3u]);
        let w: f32 = SUB[e * 5u + 4u];
        let at: u32 = P.rbase.x + (s * nb * nb + bin) * 4u;
        let m: vec3f = vec3f(RM[at], RM[at + 1u], RM[at + 2u]) * w;
        for (var l: u32 = 0u; l < 9u; l++) {
            let y: f32 = shY(dir, l);
            acc[l * 3u] = acc[l * 3u] + m.x * y;
            acc[l * 3u + 1u] = acc[l * 3u + 1u] + m.y * y;
            acc[l * 3u + 2u] = acc[l * 3u + 2u] + m.z * y;
        }
    }
    for (var k2: u32 = 0u; k2 < 27u; k2++) { SH[s * 28u + k2] = acc[k2]; }
}

//@pass gi_gather
// One invocation per pixel: I from the eight cascade-0 probes around it, through the clamped-cosine
// convolution of their harmonics at its normal (gather).
@group(0) @binding(0) var<uniform> P: GiParams;
@group(0) @binding(1) var<storage, read> GB: array<vec4f>;
@group(0) @binding(2) var<storage, read> SLOT: array<u32>;
@group(0) @binding(3) var<storage, read> SH: array<f32>;
@group(0) @binding(4) var<storage, read_write> OUT: array<vec4f>;

fn band(l: u32) -> f32 {
    if (l == 0u) { return 1.0; }
    if (l < 4u) { return 2.0 / 3.0; }
    return 0.25;
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= P.w || gid.y >= P.h) { return; }
    let i: u32 = gid.y * P.w + gid.x;
    OUT[i] = vec4f(0.0);
    let pm: vec4f = GB[2u * i];
    if (pm.w == 0.0) { return; }
    let n: vec3f = GB[2u * i + 1u].xyz;
    let b: u32 = bucketOf(n);
    let d: vec4u = P.d0;
    let g: vec3f = (pm.xyz - P.origin.xyz) / P.cell.x - vec3f(0.5);
    let i0: vec3f = floor(g);
    let fr: vec3f = g - i0;
    var sum: f32 = 0.0;
    var o: vec3f = vec3f(0.0);
    for (var k: u32 = 0u; k < 8u; k++) {
        let dx: u32 = k & 1u;
        let dy: u32 = (k >> 1u) & 1u;
        let dz: u32 = (k >> 2u) & 1u;
        let ix: i32 = i32(i0.x) + i32(dx);
        let iy: i32 = i32(i0.y) + i32(dy);
        let iz: i32 = i32(i0.z) + i32(dz);
        if (ix < 0 || iy < 0 || iz < 0 || ix >= i32(d.x) || iy >= i32(d.y) || iz >= i32(d.z)) { continue; }
        let w: f32 = select(1.0 - fr.x, fr.x, dx == 1u) * select(1.0 - fr.y, fr.y, dy == 1u) * select(1.0 - fr.z, fr.z, dz == 1u);
        if (w <= 0.0) { continue; }
        let gs: u32 = SLOT[d.w + ((u32(iz) * d.y + u32(iy)) * d.x + u32(ix)) * 6u + b];
        if (gs == NONE) { continue; }
        let base: u32 = gs * 28u;
        var e: vec3f = vec3f(0.0);
        for (var l: u32 = 0u; l < 9u; l++) {
            let y: f32 = band(l) * shY(n, l);
            e = e + vec3f(SH[base + l * 3u], SH[base + l * 3u + 1u], SH[base + l * 3u + 2u]) * y;
        }
        o = o + e * w;
        sum = sum + w;
    }
    if (sum > 0.0) { o = o / sum; }
    OUT[i] = vec4f(max(o, vec3f(0.0)), 0.0);
}
