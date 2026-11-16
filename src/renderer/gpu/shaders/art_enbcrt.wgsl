//@pass art_enbcrt_down
// The ENB CRT and VHS port: the 128 x 128 average the ENB host supplies as RenderTarget128. Each
// texel is the mean of the source pixels whose centres fall in it (raw::art::enbCrtDown).
struct EnbCrtParams {
    width: u32, height: u32, frame: u32, flags: u32,
    mask_type: u32, profile: u32, quality: u32, generation: u32,
    a: vec4f,
    b: vec4f,
    c: vec4f,
    d: vec4f,
    e: vec4f,
    f: vec4f,
}
@group(0) @binding(0) var<uniform> U: EnbCrtParams;
@group(0) @binding(1) var<storage, read> Src: array<vec4f>;
@group(0) @binding(2) var<storage, read_write> Down: array<vec4f>;

fn line_of(i: u32, n: u32, lines: u32) -> u32 {
    return ((2u * i + 1u) * lines) / (2u * n);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let t: u32 = gid.x + gid.y * 2097152u;
    if (t >= 16384u) { return; }
    let tx: u32 = t % 128u;
    let ty: u32 = t / 128u;
    let x0: u32 = (tx * U.width) / 128u;
    let x1: u32 = min(U.width, ((tx + 1u) * U.width) / 128u + 2u);
    let y0: u32 = (ty * U.height) / 128u;
    let y1: u32 = min(U.height, ((ty + 1u) * U.height) / 128u + 2u);
    var acc: vec3f = vec3f(0.0, 0.0, 0.0);
    var n: f32 = 0.0;
    for (var y: u32 = y0; y < y1; y = y + 1u) {
        if (line_of(y, U.height, 128u) != ty) { continue; }
        for (var x: u32 = x0; x < x1; x = x + 1u) {
            if (line_of(x, U.width, 128u) != tx) { continue; }
            acc = acc + Src[y * U.width + x].xyz;
            n = n + 1.0;
        }
    }
    Down[t] = vec4f(acc / max(n, 1.0), 1.0);
}

//@pass art_enbcrt
// The ENB CRT and VHS port, PS_CRTDisplay in one pass (raw/renderer/art_enbcrt.hpp, where the
// source and the deviations are named). a: curvature, overscan, brightness, contrast; b: saturation,
// scanline intensity, scanline width, beam width; c: mask intensity, mask scale, bloom, convergence;
// d: halation, corner radius, signal noise, time; e: chroma blur, chroma smear, tracking, head
// switch; f: tape noise, snow, jitter. flags: 1 CRT, 2 VHS. Output: RGBA8 codes, one word a pixel.
struct EnbCrtParams {
    width: u32, height: u32, frame: u32, flags: u32,
    mask_type: u32, profile: u32, quality: u32, generation: u32,
    a: vec4f,
    b: vec4f,
    c: vec4f,
    d: vec4f,
    e: vec4f,
    f: vec4f,
}
@group(0) @binding(0) var<uniform> U: EnbCrtParams;
@group(0) @binding(1) var<storage, read> Src: array<vec4f>;
@group(0) @binding(2) var<storage, read> Down: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> Out: array<u32>;

fn src_at(x: i32, y: i32) -> vec3f {
    let cx: i32 = clamp(x, 0, i32(U.width) - 1);
    let cy: i32 = clamp(y, 0, i32(U.height) - 1);
    return Src[u32(cy) * U.width + u32(cx)].xyz;
}
fn src_linear(u: f32, v: f32) -> vec3f {
    let fx: f32 = clamp(u * f32(U.width) - 0.5, 0.0, f32(U.width - 1u));
    let fy: f32 = clamp(v * f32(U.height) - 0.5, 0.0, f32(U.height - 1u));
    let x0: i32 = i32(floor(fx));
    let y0: i32 = i32(floor(fy));
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    return (src_at(x0, y0) * (1.0 - tx) + src_at(x0 + 1, y0) * tx) * (1.0 - ty) + (src_at(x0, y0 + 1) * (1.0 - tx) + src_at(x0 + 1, y0 + 1) * tx) * ty;
}
fn src_point(u: f32, v: f32) -> vec3f {
    return src_at(i32(floor(u * f32(U.width))), i32(floor(v * f32(U.height))));
}
fn down_linear(u: f32, v: f32) -> vec3f {
    let fx: f32 = clamp(u * 128.0 - 0.5, 0.0, 127.0);
    let fy: f32 = clamp(v * 128.0 - 0.5, 0.0, 127.0);
    let x0: u32 = u32(floor(fx));
    let y0: u32 = u32(floor(fy));
    let x1: u32 = min(127u, x0 + 1u);
    let y1: u32 = min(127u, y0 + 1u);
    let tx: f32 = fx - f32(x0);
    let ty: f32 = fy - f32(y0);
    return (Down[y0 * 128u + x0].xyz * (1.0 - tx) + Down[y0 * 128u + x1].xyz * tx) * (1.0 - ty) + (Down[y1 * 128u + x0].xyz * (1.0 - tx) + Down[y1 * 128u + x1].xyz * tx) * ty;
}
fn line_of(i: u32, n: u32, lines: u32) -> u32 {
    return ((2u * i + 1u) * lines) / (2u * n);
}
fn to_yiq(c: vec3f) -> vec3f {
    return vec3f(c.x * 0.2989 + c.y * 0.5870 + c.z * 0.1140, c.x * 0.5959 - c.y * 0.2744 - c.z * 0.3216, c.x * 0.2115 - c.y * 0.5229 + c.z * 0.3114);
}
fn from_yiq(c: vec3f) -> vec3f {
    return vec3f(c.x + c.y * 0.9563 + c.z * 0.6210, c.x - c.y * 0.2721 - c.z * 0.6474, c.x - c.y * 1.1070 + c.z * 1.7046);
}
fn sat3(c: vec3f) -> vec3f {
    return clamp(c, vec3f(0.0, 0.0, 0.0), vec3f(1.0, 1.0, 1.0));
}
fn lum(c: vec3f) -> f32 {
    return c.x * 0.2126 + c.y * 0.7152 + c.z * 0.0722;
}
fn chroma_blur(u: f32, v: f32, amount: f32, gen: f32) -> vec3f {
    var yiq: vec3f = to_yiq(src_point(u, v));
    let r: f32 = (2.0 + amount * 4.0) * gen;
    var acc: vec2f = vec2f(0.0, 0.0);
    var tw: f32 = 0.0;
    for (var i: i32 = -8; i <= 8; i = i + 1) {
        let w: f32 = exp(-0.5 * f32(i * i) / max(r * r, 0.01));
        let s: vec3f = to_yiq(src_point(u + f32(i) * 2.0 / f32(U.width), v));
        acc = acc + s.yz * w;
        tw = tw + w;
    }
    yiq = vec3f(yiq.x, acc / tw);
    return sat3(from_yiq(yiq));
}
fn chroma_smear(col: vec3f, u: f32, v: f32, amount: f32) -> vec3f {
    var yiq: vec3f = to_yiq(col);
    var acc: vec2f = vec2f(0.0, 0.0);
    var tw: f32 = 0.0;
    for (var i: i32 = 0; i < 8; i = i + 1) {
        let t: f32 = f32(i) / 8.0;
        let w: f32 = exp(-t * 3.0);
        let s: vec3f = to_yiq(src_point(u + t * amount * 8.0 / f32(U.width), v));
        acc = acc + s.yz * w;
        tw = tw + w;
    }
    yiq = vec3f(yiq.x, acc / tw);
    return sat3(from_yiq(yiq));
}
fn head_switch(col: vec3f, u: f32, v: f32, px: u32, py: u32, intensity: f32) -> vec3f {
    let zone: f32 = art_smooth(0.92, 0.98, v);
    if (zone < 0.001) { return col; }
    let jitter: f32 = (art_hash01(line_of(py, U.height, 480u), U.frame * 10u / 3u, 0u, 11u) - 0.5) * 2.0;
    let du: f32 = clamp(u + jitter * 0.05 * zone * intensity, 0.0, 1.0);
    let n: f32 = art_hash01(px, py, U.frame, 12u);
    var r: vec3f = src_linear(du, clamp(v, 0.0, 1.0));
    r = r + (vec3f(n, n, n) - r) * (zone * intensity * 0.5);
    return col + (r - col) * zone;
}
fn tape_noise(c0: vec3f, u: f32, px: u32, py: u32, rate: f32, snow: f32) -> vec3f {
    var col: vec3f = c0;
    let ln: u32 = line_of(py, U.height, 480u);
    let drop: bool = art_hash01(ln, U.frame, 0u, 14u) >= 1.0 - rate;
    var bright: f32 = 0.0;
    if (art_hash01(ln, U.frame, 1u, 14u) >= 0.5) { bright = 1.0; }
    let width: f32 = art_hash01(ln, U.frame, 2u, 14u) * 0.3 + 0.05;
    let x0: f32 = art_hash01(ln, U.frame, 3u, 14u);
    var inside: f32 = 0.0;
    if (drop && u >= x0 && u <= x0 + width) { inside = 1.0; }
    col = col + (vec3f(bright, bright, bright) - col) * (inside * 0.8);
    if (snow > 0.001) {
        let s: f32 = art_hash01(px, py, U.frame * 100u / 3u, 15u);
        col = col + (vec3f(s, s, s) - col) * (snow * 0.15);
    }
    return col;
}
