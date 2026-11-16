// The art_enbcrt pass continued (the art module concatenates its files in name order, so this file
// carries on the section art_enbcrt.wgsl opens): the CRT pipeline, the VHS pipeline and the entry.
fn fr(x: f32) -> f32 {
    return x - floor(x);
}
fn tri(ph: f32, centre: f32) -> f32 {
    return clamp(1.0 - abs(ph - centre) * 2.0, 0.0, 1.0);
}
fn mask_rgb(px: f32, py: f32, intensity: f32, luma: f32) -> vec3f {
    let ai: f32 = intensity * (1.0 - luma * 0.5);
    if (U.mask_type == 0u) {
        let ph: f32 = fr(px / 3.0) * 3.0;
        return (1.0 - ai) + (vec3f(1.0, 1.0, 1.0) - (1.0 - ai)) * vec3f(tri(ph, 0.5), tri(ph, 1.5), tri(ph, 2.5));
    }
    if (U.mask_type == 1u) {
        let row: f32 = floor(py - 2.0 * floor(py / 2.0)) * 1.5;
        let ph: f32 = fr((px + row) / 3.0) * 3.0;
        let gap: f32 = art_smooth(0.4, 0.5, abs(fr(py / 2.0) - 0.5));
        let g: f32 = 1.0 + (0.7 - 1.0) * gap;
        let lo: f32 = 1.0 - ai * 0.8;
        return lo + (1.0 - lo) * vec3f(tri(ph, 0.5) * g, tri(ph, 1.5) * g, tri(ph, 2.5) * g);
    }
    let hy: f32 = py * 0.866;
    let row: f32 = floor(hy - 2.0 * floor(hy / 2.0)) * 0.5;
    let ph: f32 = fr((px + row) / 3.0) * 3.0;
    let dx: f32 = fr(px) - 0.5;
    let dy: f32 = fr(hy) - 0.5;
    let d: f32 = art_smooth(0.45, 0.3, sqrt(dx * dx + dy * dy));
    var m: vec3f = vec3f(0.0, 0.0, 0.0);
    if (ph < 1.0) { m.x = d; }
    if (ph >= 1.0 && ph < 2.0) { m.y = d; }
    if (ph >= 2.0) { m.z = d; }
    let lo: f32 = 1.0 - ai * 0.6;
    return lo + (1.0 - lo) * m;
}
fn profile(c0: vec3f) -> vec3f {
    var c: vec3f = c0;
    if (U.profile == 1u) { c = c * vec3f(1.05, 1.02, 0.90); }
    if (U.profile == 2u) {
        c = c * vec3f(1.08, 1.04, 0.95);
        let l: f32 = lum(c);
        c = l + (c - l) * 1.15;
    }
    if (U.profile == 3u) { c = c * vec3f(0.92, 0.97, 1.12); }
    return c;
}
fn bloom(u: f32, v: f32) -> vec3f {
    let r: f32 = 1.5 / f32(U.width);
    let rv: f32 = 1.5 / f32(U.height);
    let dr: f32 = r * 0.7071;
    let drv: f32 = rv * 0.7071;
    var b: vec3f = src_linear(u + r, v) + src_linear(u - r, v) + src_linear(u, v + rv) + src_linear(u, v - rv);
    b = b + src_linear(u + dr, v + drv) * 0.7 + src_linear(u - dr, v + drv) * 0.7 + src_linear(u + dr, v - drv) * 0.7 + src_linear(u - dr, v - drv) * 0.7;
    return b / 6.8;
}
fn crt_sample(u: f32, v: f32) -> vec3f {
    if (U.c.w <= 0.01) { return src_linear(u, v); }
    let cx: f32 = u - 0.5;
    let cy: f32 = v - 0.5;
    let e: f32 = (cx * cx + cy * cy) * 4.0;
    let ox: f32 = U.c.w / f32(U.width) * e;
    let oy: f32 = U.c.w / f32(U.height) * e;
    return vec3f(src_linear(u + ox * -0.7, v + oy * -0.5).x, src_linear(u, v).y, src_linear(u + ox * 0.5, v + oy * 0.7).z);
}
fn crt(u0: f32, v0: f32, px: u32, py: u32) -> vec3f {
    var u: f32 = u0;
    var v: f32 = v0;
    if (U.a.x > 0.001) {
        let x: f32 = u * 2.0 - 1.0;
        let y: f32 = v * 2.0 - 1.0;
        let k: f32 = 1.0 + (x * x + y * y) * U.a.x;
        u = x * k * 0.5 + 0.5;
        v = y * k * 0.5 + 0.5;
    }
    u = (u - 0.5) * (1.0 + U.a.y * 2.0) + 0.5;
    v = (v - 0.5) * (1.0 + U.a.y * 2.0) + 0.5;
    if (min(u * (1.0 - u), v * (1.0 - v)) < 0.0) { return vec3f(0.0, 0.0, 0.0); }
    var col: vec3f = crt_sample(u, v);
    if (U.profile > 0u) { col = profile(col); }
    col = max((col * U.a.z - 0.5) * U.a.w + 0.5, vec3f(0.0, 0.0, 0.0));
    var luma: f32 = lum(col);
    col = max(luma + (col - luma) * U.b.x, vec3f(0.0, 0.0, 0.0));
    luma = lum(col);
    if (U.b.y > 0.01) {
        let d: f32 = fr(v * f32(U.height) / U.b.z) - 0.5;
        let sig: f32 = 0.35 + (0.7 - 0.35) * (luma * U.b.w);
        let shp: f32 = 2.0 + (6.0 - 2.0) * (luma * U.b.w);
        let beam: f32 = exp(-0.5 * pow(abs(d) / max(sig, 0.001), shp));
        col = col * (1.0 + (beam - 1.0) * U.b.y);
    }
    if (U.c.x > 0.01) { col = col * mask_rgb(u * f32(U.width) / U.c.y, v * f32(U.height) / U.c.y, U.c.x, luma); }
    if (U.c.z > 0.01) { col = col + (max(col, bloom(u, v)) - col) * U.c.z; }
    if (U.d.x > 0.01) { col = col + down_linear(u, v) * vec3f(1.15, 1.0, 0.85) * U.d.x * 0.3; }
    if (U.d.z > 0.01) {
        let n: f32 = art_hash01(px, py, U.frame * 100u / 3u, 17u) - 0.5;
        col = col + n * U.d.z * 0.15;
    }
    if (U.d.y > 0.001) {
        let e: f32 = art_smooth(0.0, U.d.y, u) * art_smooth(0.0, U.d.y, 1.0 - u) * art_smooth(0.0, U.d.y, v) * art_smooth(0.0, U.d.y, 1.0 - v);
        col = col * pow(e, 0.3);
    }
    return sat3(col);
}
fn vhs(px: u32, py: u32, uv: ptr<function, vec2f>) -> vec3f {
    let gen: f32 = 1.0 + f32(U.generation) * 0.4;
    let qual: f32 = 1.0 + f32(U.quality) * 0.3;
    var u: f32 = (*uv).x;
    let v: f32 = clamp((*uv).y, 0.0, 1.0);
    if (U.e.z > 0.01) {
        let b: u32 = ((2u * py + 1u) * 250u + U.frame * U.height) / (10u * U.height);
        var band: f32 = 0.0;
        if (art_hash01(b, 0u, 0u, 13u) >= 0.98) { band = 1.0; }
        u = u + sin(U.d.w * 7.0 + (*uv).y * 100.0) * (U.e.z * qual) * band * 0.03;
    }
    if (U.f.z > 0.01) {
        let amount: f32 = U.f.z * qual * 0.5;
        let ln: u32 = line_of(py, U.height, 480u);
        u = u + sin(f32(ln) * 0.7 + U.d.w * 50.0) * amount * (1.0 / f32(U.width)) * 2.0;
        u = u + (art_hash01(ln, U.frame * 2u, 0u, 16u) - 0.5) * amount * (1.0 / f32(U.width)) * 3.0;
    }
    u = clamp(u, 0.0, 1.0);
    var col: vec3f = src_linear(u, v);
    if (U.e.x > 0.01) { col = chroma_blur(u, v, U.e.x * qual, gen); }
    if (U.e.y > 0.01) { col = chroma_smear(col, u, v, U.e.y * qual); }
    if (U.e.w > 0.01) { col = head_switch(col, u, v, px, py, U.e.w); }
    if (U.f.x > 0.001 || U.f.y > 0.001) { col = tape_noise(col, u, px, py, U.f.x * gen, U.f.y * gen); }
    if (U.generation > 0u) {
        let loss: f32 = f32(U.generation) * 0.08;
        col = col + (0.5 - col) * loss;
        let l: f32 = lum(col);
        col = col + (l - col) * (loss * 0.5);
    }
    *uv = vec2f(u, v);
    return sat3(col);
}

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    let p: u32 = gid.x + gid.y * 2097152u;
    if (p >= U.width * U.height) { return; }
    let px: u32 = p % U.width;
    let py: u32 = p / U.width;
    var uv: vec2f = vec2f((f32(px) + 0.5) / f32(U.width), (f32(py) + 0.5) / f32(U.height));
    var col: vec3f = src_linear(uv.x, uv.y);
    if ((U.flags & 2u) != 0u) { col = vhs(px, py, &uv); }
    if ((U.flags & 1u) != 0u) { col = crt(uv.x, uv.y, px, py); }
    let q: vec3u = vec3u(floor(clamp(col, vec3f(0.0, 0.0, 0.0), vec3f(1.0, 1.0, 1.0)) * 255.0 + 0.5));
    Out[p] = q.x | (q.y << 8u) | (q.z << 16u) | (255u << 24u);
}
