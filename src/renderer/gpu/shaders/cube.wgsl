// raw-native GPU shared fragment: cube-map addressing, the orthonormal basis, the
// radical inverse and the cluster index, line for line as raw/renderer/lighting_cube.hpp
// and src/renderer/lighting_clusters.cpp. Pure functions only (no bindings), so every
// module may put it in front of its shared code: pbr.wgsl and light.wgsl do.

// Levels stop at 8 texels a face (raw::lighting::cube::kMinLevelSize).
fn cube_level_size(size: u32, level: u32) -> u32 { return max(min(size, 8u), size >> level); }
// Floats before level `level` of a cube stored as RGB texels, faces in order.
fn cube_level_offset(size: u32, level: u32) -> u32 {
    var off: u32 = 0u;
    for (var k: u32 = 0u; k < level; k++) {
        let n: u32 = cube_level_size(size, k);
        off = off + 18u * n * n;
    }
    return off;
}
// Face index in x, and u, v in [0, 1] in y and z (+x -x +y -y +z -z; D3D / OpenGL rules).
fn cube_face(d: vec3f) -> vec3f {
    let ax: f32 = abs(d.x);
    let ay: f32 = abs(d.y);
    let az: f32 = abs(d.z);
    var f: f32 = 0.0; var ma: f32 = 0.0; var sc: f32 = 0.0; var tc: f32 = 0.0;
    if (ax >= ay && ax >= az) {
        f = select(1.0, 0.0, d.x >= 0.0); ma = ax; sc = select(d.z, -d.z, d.x >= 0.0); tc = -d.y;
    } else if (ay >= az) {
        f = select(3.0, 2.0, d.y >= 0.0); ma = ay; sc = d.x; tc = select(-d.z, d.z, d.y >= 0.0);
    } else {
        f = select(5.0, 4.0, d.z >= 0.0); ma = az; sc = select(-d.x, d.x, d.z >= 0.0); tc = -d.y;
    }
    return vec3f(f, 0.5 * (sc / ma + 1.0), 0.5 * (tc / ma + 1.0));
}
fn cube_dir(f: u32, u: f32, v: f32) -> vec3f {
    let sc: f32 = 2.0 * u - 1.0;
    let tc: f32 = 2.0 * v - 1.0;
    var d: vec3f = vec3f(-sc, -tc, -1.0);
    if (f == 0u) { d = vec3f(1.0, -tc, -sc); }
    if (f == 1u) { d = vec3f(-1.0, -tc, sc); }
    if (f == 2u) { d = vec3f(sc, 1.0, tc); }
    if (f == 3u) { d = vec3f(sc, -1.0, -tc); }
    if (f == 4u) { d = vec3f(sc, -tc, 1.0); }
    return d / sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}
// The texel a bilinear tap lands on, seamless across faces (raw::lighting::cube::tap).
// Returns (face, y, x) for a tap at row y, column x of face f, possibly past its edge.
fn cube_tap(n: u32, f: u32, y: i32, x: i32) -> vec3u {
    let ni: i32 = i32(n);
    if (x >= 0 && x < ni && y >= 0 && y < ni) { return vec3u(f, u32(y), u32(x)); }
    let d: vec3f = cube_dir(f, (f32(x) + 0.5) / f32(n), (f32(y) + 0.5) / f32(n));
    let fu: vec3f = cube_face(d);
    let xx: i32 = clamp(i32(fu.y * f32(n)), 0, ni - 1);
    let yy: i32 = clamp(i32(fu.z * f32(n)), 0, ni - 1);
    return vec3u(u32(fu.x), u32(yy), u32(xx));
}
// Duff et al. 2017.
fn onb(n: vec3f, t: ptr<function, vec3f>, b: ptr<function, vec3f>) {
    let s: f32 = select(-1.0, 1.0, n.z >= 0.0);
    let a: f32 = -1.0 / (s + n.z);
    let c: f32 = n.x * n.y * a;
    *t = vec3f(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    *b = vec3f(c, s + n.y * n.y * a, -n.y);
}
fn radical_inverse(bits_in: u32) -> f32 {
    var b: u32 = (bits_in << 16u) | (bits_in >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return f32(b) * 2.3283064365386963e-10;
}
// The cluster of a view-space point, or -1 outside the frustum. grid: tan(fovy / 2), aspect,
// near, far; 16 x 9 x 24 clusters (raw::lighting::ClusterGrid).
fn cluster_of(p: vec3f, grid: vec4f) -> i32 {
    let d: f32 = -p.z;
    if (!(d >= grid.z && d < grid.w)) { return -1; }
    let nx: f32 = p.x / (d * grid.x * grid.y);
    let ny: f32 = p.y / (d * grid.x);
    if (!(abs(nx) <= 1.0 && abs(ny) <= 1.0)) { return -1; }
    let ix: i32 = min(i32((nx + 1.0) * 0.5 * 16.0), 15);
    let iy: i32 = min(i32((ny + 1.0) * 0.5 * 9.0), 8);
    let iz: i32 = min(i32(log(d / grid.z) / log(grid.w / grid.z) * 24.0), 23);
    return (iz * 9 + iy) * 16 + ix;
}
