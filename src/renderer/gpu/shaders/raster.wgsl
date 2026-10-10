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

//@raster fmt_pattern
// RHI version 3 format round trip (evidence/m3-rhi3-bounds.json): every channel of every
// pixel gets n * scale with n = (7x + 13y + 5c) & 255, values each format holds exactly
// (scale 1/255 for RGBA8Unorm, 1/256 for the float formats).
struct Pat {
    scale: f32,
    pad0: f32,
    pad1: f32,
    pad2: f32,
}
@group(0) @binding(0) var<uniform> U: Pat;

@vertex
fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let x: f32 = f32((i << 1u) & 2u);
    let y: f32 = f32(i & 2u);
    return vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
}

@fragment
fn fs(@builtin(position) q: vec4f) -> @location(0) vec4f {
    let x: u32 = u32(q.x);
    let y: u32 = u32(q.y);
    let r: f32 = f32((7u * x + 13u * y) & 255u);
    let g: f32 = f32((7u * x + 13u * y + 5u) & 255u);
    let b: f32 = f32((7u * x + 13u * y + 10u) & 255u);
    let a: f32 = f32((7u * x + 13u * y + 15u) & 255u);
    return vec4f(r, g, b, a) * U.scale;
}

//@raster depth_tris
// RHI version 3 depth test: triangles pulled from a storage buffer (12 floats each: three
// clip-space xyz corners, then RGB), drawn into one target with a Depth32Float target.
// (No uniform: WebGPU's derived layout drops a binding the shader never reads.)
@group(0) @binding(0) var<storage, read> T: array<f32>;
struct DtOut {
    @builtin(position) clip: vec4f,
    @location(0) @interpolate(flat) rgb: vec3f,
}

@vertex
fn vs(@builtin(vertex_index) i: u32) -> DtOut {
    let b: u32 = (i / 3u) * 12u;
    let k: u32 = i % 3u;
    var o: DtOut;
    o.clip = vec4f(T[b + 3u * k], T[b + 3u * k + 1u], T[b + 3u * k + 2u], 1.0);
    o.rgb = vec3f(T[b + 9u], T[b + 10u], T[b + 11u]);
    return o;
}

@fragment
fn fs(v: DtOut) -> @location(0) vec4f {
    return vec4f(v.rgb, 1.0);
}

//@raster gbuffer
// The hardware G-buffer (ROADMAP M3): triangles pulled from storage in the layout of
// src/renderer/gpu/frame_pack.cpp (24 floats: world positions, normals, albedo), transformed
// by the view-projection rows, depth-tested in hardware. Clip z is moved from [-w, w] (the
// engine's projection) to [0, w] (D3D12 and WebGPU). Targets: world position, normal,
// albedo with coverage, and (triangle index + 1, view distance).
struct Cam {
    vp: array<vec4f, 4>,
}
@group(0) @binding(0) var<uniform> U: Cam;
@group(0) @binding(1) var<storage, read> T: array<f32>;
struct GbOut {
    @builtin(position) clip: vec4f,
    @location(0) wpos: vec3f,
    @location(1) nrm: vec3f,
    @location(2) @interpolate(flat) alb: vec3f,
    @location(3) @interpolate(flat) tri: u32,
    @location(4) dist: f32,
}
struct GbTargets {
    @location(0) pos: vec4f,
    @location(1) nrm: vec4f,
    @location(2) alb: vec4f,
    @location(3) id: vec4f,
}
fn mrow(r: vec4f, v: vec4f) -> f32 { return r.x * v.x + r.y * v.y + r.z * v.z + r.w * v.w; }

@vertex
fn vs(@builtin(vertex_index) i: u32) -> GbOut {
    let t: u32 = i / 3u;
    let k: u32 = i % 3u;
    let b: u32 = t * 24u;
    let wp: vec4f = vec4f(T[b + 3u * k], T[b + 3u * k + 1u], T[b + 3u * k + 2u], 1.0);
    let c: vec4f = vec4f(mrow(U.vp[0], wp), mrow(U.vp[1], wp), mrow(U.vp[2], wp), mrow(U.vp[3], wp));
    var o: GbOut;
    o.clip = vec4f(c.x, c.y, 0.5 * (c.z + c.w), c.w);
    o.wpos = wp.xyz;
    o.nrm = vec3f(T[b + 9u + 3u * k], T[b + 10u + 3u * k], T[b + 11u + 3u * k]);
    o.alb = vec3f(T[b + 18u], T[b + 19u], T[b + 20u]);
    o.tri = t + 1u;
    o.dist = c.w;
    return o;
}

@fragment
fn fs(v: GbOut) -> GbTargets {
    var o: GbTargets;
    let l: f32 = sqrt(v.nrm.x * v.nrm.x + v.nrm.y * v.nrm.y + v.nrm.z * v.nrm.z);
    o.pos = vec4f(v.wpos, 1.0);
    o.nrm = vec4f(v.nrm / l, 0.0);
    o.alb = vec4f(v.alb, 1.0);
    o.id = vec4f(f32(v.tri), v.dist, 0.0, 1.0);
    return o;
}

//@raster shadow_depth
// One cascade of a directional light's shadow map (ROADMAP M3 shadows): the triangles of
// the gbuffer pass, through the cascade's orthographic light-space matrix (rows; clip z is
// already in [0, 1] and w = 1), depth-tested in hardware. Target: (triangle index + 1,
// normalised depth); the CPU twin is raw::shadows::rasterizeCascade.
struct Light {
    m: array<vec4f, 4>,
}
@group(0) @binding(0) var<uniform> U: Light;
@group(0) @binding(1) var<storage, read> T: array<f32>;
struct SdOut {
    @builtin(position) clip: vec4f,
    @location(0) @interpolate(flat) tri: u32,
    @location(1) z: f32,
}
fn mrow(r: vec4f, v: vec4f) -> f32 { return r.x * v.x + r.y * v.y + r.z * v.z + r.w * v.w; }

@vertex
fn vs(@builtin(vertex_index) i: u32) -> SdOut {
    let t: u32 = i / 3u;
    let k: u32 = i % 3u;
    let b: u32 = t * 24u;
    let wp: vec4f = vec4f(T[b + 3u * k], T[b + 3u * k + 1u], T[b + 3u * k + 2u], 1.0);
    var o: SdOut;
    o.z = mrow(U.m[2], wp);
    o.clip = vec4f(mrow(U.m[0], wp), mrow(U.m[1], wp), o.z, 1.0);
    o.tri = t + 1u;
    return o;
}

@fragment
fn fs(v: SdOut) -> @location(0) vec4f {
    return vec4f(f32(v.tri), v.z, 0.0, 1.0);
}
