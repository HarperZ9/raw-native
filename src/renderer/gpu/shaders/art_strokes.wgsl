//@pass art_anchor
// P5, world-anchored strokes: each pixel's canvas coordinates from its surface point's triplanar
// chart (dominant normal axis, an offset per axis) times a = (scale, focal), or for sky the cube-map
// chart of its view direction times the focal length. Written as the paint passes' canvas layer 0.
// The float64 reference is raw/renderer/art_anchor.hpp; the offsets are its kAnchorOffset.
struct AnchorParams {
    width: u32, height: u32, pad0: u32, pad1: u32,
    a: vec4f,
}
@group(0) @binding(0) var<uniform> U: AnchorParams;
@group(0) @binding(1) var<storage, read> Pw: array<f32>;
@group(0) @binding(2) var<storage, read> Nw: array<f32>;
@group(0) @binding(3) var<storage, read> Dw: array<f32>;
@group(0) @binding(4) var<storage, read> dist: array<f32>;
@group(0) @binding(5) var<storage, read_write> C: array<vec4f>;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let sky: bool = dist[p] < 0.0;
    var v: vec3f = vec3f(Pw[3u * p], Pw[3u * p + 1u], Pw[3u * p + 2u]);
    var n: vec3f = vec3f(Nw[3u * p], Nw[3u * p + 1u], Nw[3u * p + 2u]);
    if (sky) {
        v = vec3f(Dw[3u * p], Dw[3u * p + 1u], Dw[3u * p + 2u]);
        n = v;
    }
    let ax: f32 = abs(n.x);
    let ay: f32 = abs(n.y);
    let az: f32 = abs(n.z);
    var q: vec2f = vec2f(v.x, v.y);
    var m: f32 = az;
    var off: vec2f = vec2f(523.0, 419.0);
    if (ay >= ax && ay >= az) {
        q = vec2f(v.x, v.z);
        m = ay;
        off = vec2f(0.0, 0.0);
    } else if (ax >= az) {
        q = vec2f(v.z, v.y);
        m = ax;
        off = vec2f(311.0, 157.0);
    }
    var uv: vec2f = q * U.a.x + off;
    if (sky) { uv = q / m * U.a.y + vec2f(1009.0, 787.0); }
    C[p] = vec4f(uv.x, uv.y, 0.0, 0.0);
}
