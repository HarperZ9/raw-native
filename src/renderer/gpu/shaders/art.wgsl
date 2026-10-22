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
