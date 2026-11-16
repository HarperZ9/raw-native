// raw-native's art module: the stylised track's passes (P5, P16 to P21 in
// docs/architecture/ROADMAP.md). This file is the module's shared code; each art_<feature>.wgsl
// beside it holds that feature's passes. scripts/wgsl_to_hlsl.py translates this file followed by
// every art_*.wgsl in name order into hlsl/art_*.hlsl and art_layout.hpp; cmake/gpu.cmake embeds
// the same text for the WebGPU build. The float64 references are raw/renderer/art_*.hpp.
// Every 64-wide pass indexes gid.x + gid.y * 2097152, as the style passes do (rows of 32,768 groups).

// The 8-bit sRGB code of a display-linear value, as raw::art::encode8: clamp, sRGB, round half up.
fn art_encode8(v: f32) -> u32 {
    let l: f32 = clamp(v, 0.0, 1.0);
    var s: f32 = l * 12.92;
    if (l > 0.0031308) { s = 1.055 * pow(l, 1.0 / 2.4) - 0.055; }
    return u32(floor(s * 255.0 + 0.5));
}

// The noise module's integer hash (raw/renderer/noise.hpp hash: Wellons' lowbias32 chained over
// x, y, z and the seed) and its unit value in [0, 1), as raw::noise::hash and unit.
fn art_lowbias32(v: u32) -> u32 {
    var x: u32 = v;
    x = x ^ (x >> 16u);
    x = x * 0x7feb352du;
    x = x ^ (x >> 15u);
    x = x * 0x846ca68bu;
    x = x ^ (x >> 16u);
    return x;
}
fn art_hash01(x: u32, y: u32, z: u32, seed: u32) -> f32 {
    let h: u32 = art_lowbias32(x ^ art_lowbias32(y ^ art_lowbias32(z ^ art_lowbias32(seed))));
    return f32(h >> 8u) * (1.0 / 16777216.0);
}
fn art_smooth(e0: f32, e1: f32, x: f32) -> f32 {
    let t: f32 = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
