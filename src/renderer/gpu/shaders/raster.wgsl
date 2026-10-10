// Raster passes: each "//@raster <name>" section is one module with a vertex entry
// point vs and a fragment entry point fs. scripts/wgsl_to_hlsl.py translates each into
// shaders/hlsl/<name>.hlsl (compiled twice, vs_6_0 and ps_6_0) and writes the binding
// layout into raster_layout.hpp. Bindings are group 0 in order: an optional uniform
// first, then textures and samplers.

//@raster texture_identity
// ROADMAP M2 criterion 3: one full-screen triangle; each target pixel samples the source
// at uv = pixel centre / target size * scale + offset, mip level 0. The CPU twin is
// raw/renderer/sampler.hpp; the check is src/renderer/gpu/texture_identity.cpp.
struct Map {
    size: vec2f,
    scale: vec2f,
    offset: vec2f,
    pad: vec2f,
}
@group(0) @binding(0) var<uniform> M: Map;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(0) @binding(2) var smp: sampler;

@vertex
fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let x: f32 = f32((i << 1u) & 2u);
    let y: f32 = f32(i & 2u);
    return vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
}

@fragment
fn fs(@builtin(position) q: vec4f) -> @location(0) vec4f {
    let uv: vec2f = q.xy / M.size * M.scale + M.offset;
    return textureSampleLevel(src, smp, uv, 0.0);
}
