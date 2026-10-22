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

// P23, the tilt-shift lens (raw/renderer/art_tiltshift.hpp): the parameters every art_ts_* pass
// reads, and the pure helpers they share.
struct TsParams {
    width: u32, height: u32, qw: u32, qh: u32,
    dir: u32, seed: u32, blur_r: u32, pad0: u32,
    eye: vec4f,    // xyz, focal length in pixels
    fwd: vec4f,    // xyz, focal length in mm
    pn: vec4f,     // the plane of focus: normal xyz, d
    lens: vec4f,   // mm a world unit, f-number, cMax, defocus radius scale (0.5 at half resolution)
    dp: vec4f,     // c0, c1, q, rim
    fx0: vec4f,    // vignette, mechanical vignette, halation, star
    fx1: vec4f,    // chromatic aberration, grain, print, halftone
    fx2: vec4f,    // misregistration, weave x, weave y, halftone cell
    fx3: vec4f,    // halo radius, star step (quarter pixels), threshold, knee
    fx4: vec4f,    // glow gain (0.5 under reduce bright flashes), saturation, hue cos, hue sin
    sd0: vec4f,    // star directions 0 and 1 (cos, sin)
    sd1: vec4f,    // star directions 2 and 3
}
fn ts_pcg(v: u32) -> u32 {
    let state: u32 = v * 747796405u + 2891336453u;
    let word: u32 = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
fn ts_hash3(x: u32, y: u32, z: u32) -> f32 { return f32(ts_pcg(x ^ ts_pcg(y ^ ts_pcg(z)))) / 4294967296.0; }
fn ts_srgb(v: f32) -> f32 {
    let l: f32 = clamp(v, 0.0, 1.0);
    if (l <= 0.0031308) { return l * 12.92; }
    return 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}
fn ts_luma(c: vec3f) -> f32 { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; }
fn ts_index(x: i32, y: i32, w: u32, h: u32) -> u32 { return u32(clamp(y, 0, i32(h) - 1)) * w + u32(clamp(x, 0, i32(w) - 1)); }
