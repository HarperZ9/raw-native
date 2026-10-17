// Perspective-stable pixel art: the probe, the splat, the resolve and the frame loop. See
// raw/renderer/style_pixel.hpp; operations in the order of web/shaders/pixel/probe.mjs, splat.mjs
// and pixel.mjs. A float here is a Float32Array element there.
#include "raw/renderer/style_pixel.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace raw::style {
namespace {
double hyp(const PV3& r) { return std::hypot(r[0], r[1], r[2]); }
double cheb(const PV3& r) { return std::max({std::abs(r[0]), std::abs(r[1]), std::abs(r[2])}); }
PV3 project(const PixelCamera& cam, const PV3& p) {
    const PV3 v{p[0] - cam.eye[0], p[1] - cam.eye[1], p[2] - cam.eye[2]};
    const double z = v[0] * cam.f[0] + v[1] * cam.f[1] + v[2] * cam.f[2];
    const double x = (v[0] * cam.r[0] + v[1] * cam.r[1] + v[2] * cam.r[2]) / (z * cam.ty * (double(cam.w) / cam.h));
    const double y = (v[0] * cam.u[0] + v[1] * cam.u[1] + v[2] * cam.u[2]) / (z * cam.ty);
    return {((x + 1) / 2) * cam.w, ((1 - y) / 2) * cam.h, z};
}
PV3 rayDir(const PixelCamera& cam, int px, int py) {
    const double x = ((2 * (px + 0.5)) / cam.w - 1) * cam.ty * (double(cam.w) / cam.h), y = (1 - (2 * (py + 0.5)) / cam.h) * cam.ty;
    PV3 d{};
    for (int c = 0; c < 3; ++c) d[std::size_t(c)] = cam.f[std::size_t(c)] + x * cam.r[std::size_t(c)] + y * cam.u[std::size_t(c)];
    const double l = hyp(d);
    for (double& v : d) v = v / l;
    return d;
}
double edgeF(const PV3& a, const PV3& b, double x, double y) { return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]); }
bool inTri(const PV3& a, const PV3& b, const PV3& c, double x, double y) {
    const double e0 = edgeF(a, b, x, y), e1 = edgeF(b, c, x, y), e2 = edgeF(c, a, x, y);
    return (e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0);
}
// The texel's screen quad: four corners and the centre (its view depth in [2]); false if culled.
bool texelQuad(const PixelProbe& pr, const PixelCamera& cam, std::size_t k, double expand, PV3 q[5]) {
    const std::size_t N = std::size_t(pr.N);
    const int f = int(k / (N * N)), j = int((k % (N * N)) / N), i = int(k % N);
    const double d = pr.depth[k];
    if (d < 0) return false;
    const double u = (2 * (i + 0.5)) / pr.N - 1, v = (2 * (j + 0.5)) / pr.N - 1, h = expand / pr.N;
    const double su[5] = {-1, 1, 1, -1, 0}, sv[5] = {-1, -1, 1, 1, 0};
    for (int s = 0; s < 5; ++s) {
        const PV3 r = pixelFaceDir(f, u + su[s] * h, v + sv[s] * h);
        const double m = cheb(r);
        const PV3 sp = project(cam, {pr.origin[0] + (d / m) * r[0], pr.origin[1] + (d / m) * r[1], pr.origin[2] + (d / m) * r[2]});
        if (sp[2] < 0.05) return false;
        q[s] = sp;
    }
    return true;
}
int bayer4(int x, int y) { constexpr int B[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5}; return B[(y & 3) * 4 + (x & 3)]; }
}  // namespace

PV3 pixelFaceDir(int f, double u, double v) {
    switch (f) {
        case 0: return {1, -v, -u};
        case 1: return {-1, -v, u};
        case 2: return {u, 1, v};
        case 3: return {u, -1, -v};
        case 4: return {u, -v, 1};
        default: return {-u, -v, -1};
    }
}
PV3 pixelSnapOrigin(const PV3& eye, double cell) { return {(std::floor(eye[0] / cell) + 0.5) * cell, (std::floor(eye[1] / cell) + 0.5) * cell, (std::floor(eye[2] / cell) + 0.5) * cell}; }

PixelCamera pixelCamera(const PV3& eye, const PV3& target, double fovy, int w, int h) {
    PixelCamera c;
    c.eye = eye; c.w = w; c.h = h;
    PV3 f{target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const double fl = hyp(f);
    for (double& v : f) v = v / fl;
    PV3 r{f[2], 0, -f[0]};
    const double rl = hyp(r);
    for (double& v : r) v = -v / rl;
    c.f = f; c.r = r;
    c.u = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    c.ty = std::tan(fovy / 2);
    return c;
}

PixelProbe pixelCaptureProbe(const PV3& origin, const PixelOptions& o) {
    PixelProbe pr;
    pr.origin = origin; pr.N = o.N;
    const int N = o.N;
    const std::size_t n = std::size_t(6) * N * N;
    pr.depth.assign(n, 0.0f); pr.nrm.assign(n * 3, 0.0f); pr.mat.assign(n, 0); pr.obj.assign(n, 0); pr.col.assign(n * 3, 0.0f);
    for (int f = 0; f < 6; ++f) for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i) {
        const std::size_t k = (std::size_t(f) * N + std::size_t(j)) * N + std::size_t(i);
        const PV3 r = pixelFaceDir(f, (2 * (i + 0.5)) / N - 1, (2 * (j + 0.5)) / N - 1);
        const double l = hyp(r);
        const SceneHit hit = pixelMarch(origin, {r[0] / l, r[1] / l, r[2] / l});
        if (!hit.hit) { pr.depth[k] = -1; pr.mat[k] = -1; pr.obj[k] = -1; continue; }
        PV3 p{};
        for (int c = 0; c < 3; ++c) p[std::size_t(c)] = origin[std::size_t(c)] + (r[std::size_t(c)] / l) * hit.t;
        pr.depth[k] = float(std::max({std::abs(p[0] - origin[0]), std::abs(p[1] - origin[1]), std::abs(p[2] - origin[2])}));
        pr.mat[k] = hit.mat; pr.obj[k] = hit.obj;
        const PV3 nn = pixelSceneNormal(p);
        for (int c = 0; c < 3; ++c) pr.nrm[k * 3 + std::size_t(c)] = float(nn[std::size_t(c)]);
    }
    for (int f = 0; f < 6; ++f) for (int j = 0; j < N; ++j) for (int i = 0; i < N; ++i) {
        const std::size_t k = (std::size_t(f) * N + std::size_t(j)) * N + std::size_t(i);
        const double u = (2 * (i + 0.5)) / N - 1, v = (2 * (j + 0.5)) / N - 1;
        PV3 c{};
        if (pr.depth[k] < 0) {
            const PV3 r = pixelFaceDir(f, u, v);
            const double l = hyp(r);
            c = pixelStylise(pixelSky({r[0] / l, r[1] / l, r[2] / l}), o.bands, 0);
        } else {
            const PV3 r = pixelFaceDir(f, u, v);
            const double m = cheb(r), d = pr.depth[k];
            const PV3 p{origin[0] + (d / m) * r[0], origin[1] + (d / m) * r[1], origin[2] + (d / m) * r[2]};
            const PV3 nk{pr.nrm[k * 3], pr.nrm[k * 3 + 1], pr.nrm[k * 3 + 2]};
            double dL = 0;
            const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& dd : D) {
                const int ii = i + dd[0], jj = j + dd[1];
                if (ii < 0 || jj < 0 || ii >= N || jj >= N) continue;
                const std::size_t q = (std::size_t(f) * N + std::size_t(jj)) * N + std::size_t(ii);
                if (pr.obj[q] != pr.obj[k] && (pr.depth[q] < 0 || double(pr.depth[q]) > double(pr.depth[k]) * o.outlineRatio)) dL = -o.outline;
                else if (dL == 0 && pr.obj[q] == pr.obj[k] && (dd[0] > 0 || dd[1] > 0) &&
                         nk[0] * double(pr.nrm[q * 3]) + nk[1] * double(pr.nrm[q * 3 + 1]) + nk[2] * double(pr.nrm[q * 3 + 2]) < 0.6) dL = -o.crease;
            }
            c = pixelStylise(pixelShadePoint(p, nk, pr.mat[k]), o.bands, dL);
        }
        for (int cc = 0; cc < 3; ++cc) pr.col[k * 3 + std::size_t(cc)] = float(c[std::size_t(cc)]);
    }
    return pr;
}

PixelVis pixelSplat(const PixelProbe& pr, const PixelCamera& cam, double expand) {
    const int w = cam.w, h = cam.h;
    const std::size_t n = std::size_t(6) * pr.N * pr.N;
    PixelVis vis;
    vis.zbuf.assign(std::size_t(w) * h, std::numeric_limits<float>::infinity());
    vis.idb.assign(std::size_t(w) * h, 0xffffffffu);
    for (int pass = 0; pass < 2; ++pass) for (std::size_t k = 0; k < n; ++k) {
        PV3 q[5];
        if (!texelQuad(pr, cam, k, expand, q)) continue;
        const int x0 = std::max(0, int(std::floor(std::min({q[0][0], q[1][0], q[2][0], q[3][0]})))), x1 = std::min(w - 1, int(std::ceil(std::max({q[0][0], q[1][0], q[2][0], q[3][0]}))));
        const int y0 = std::max(0, int(std::floor(std::min({q[0][1], q[1][1], q[2][1], q[3][1]})))), y1 = std::min(h - 1, int(std::ceil(std::max({q[0][1], q[1][1], q[2][1], q[3][1]}))));
        if (x1 - x0 > 63 || y1 - y0 > 63) continue;
        const double z = std::floor(q[4][2] * 4096);
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            if (!inTri(q[0], q[1], q[2], x + 0.5, y + 0.5) && !inTri(q[0], q[2], q[3], x + 0.5, y + 0.5)) continue;
            const std::size_t p = std::size_t(y) * w + std::size_t(x);
            if (pass == 0) { if (z < vis.zbuf[p]) vis.zbuf[p] = float(z); }
            else if (z == double(vis.zbuf[p]) && k < vis.idb[p]) vis.idb[p] = std::uint32_t(k);
        }
    }
    return vis;
}

PixelRunner::Frame PixelRunner::frame(const PV3& eye, const PV3& target, double fovy) {
    const PV3 origin = pixelSnapOrigin(eye, o_.cell);
    if (!cur_ || origin != cur_->origin) {
        prev_ = std::move(cur_);
        cur_ = std::make_unique<PixelProbe>(pixelCaptureProbe(origin, o_));
        fade_ = prev_ ? 0 : 1;
    }
    const double moved = haveLast_ ? std::hypot(eye[0] - lastEye_[0], eye[1] - lastEye_[1], eye[2] - lastEye_[2]) : 0;
    fade_ = std::min(1.0, fade_ + std::max(1 / o_.fadeFrames, moved / (o_.fadeCells * o_.cell)));
    lastEye_ = eye; haveLast_ = true;
    Frame F;
    F.cam = pixelCamera(eye, target, fovy, w_, h_);
    F.vis = pixelSplat(*cur_, F.cam, o_.expand);
    F.probe = cur_.get();
    F.blend = fade_;
    F.hasPrev = prev_ && fade_ < 1;
    if (F.hasPrev) { F.prevVis = pixelSplat(*prev_, F.cam, o_.expand); F.prev = prev_.get(); }
    F.img.assign(std::size_t(w_) * h_ * 4, 0.0f);
    for (int y = 0; y < h_; ++y) for (int x = 0; x < w_; ++x) {
        const std::size_t p = std::size_t(y) * w_ + std::size_t(x);
        const bool usePrev = F.hasPrev && (bayer4(x, y) + 0.5) / 16 >= fade_;
        const PixelProbe& pr = usePrev ? *prev_ : *cur_;
        const std::uint32_t id = usePrev ? F.prevVis.idb[p] : F.vis.idb[p];
        PV3 c{};
        if (id != 0xffffffffu) c = {pr.col[std::size_t(id) * 3], pr.col[std::size_t(id) * 3 + 1], pr.col[std::size_t(id) * 3 + 2]};
        else {
            const PV3 d = rayDir(F.cam, x, y);
            const SceneHit hit = pixelMarch(eye, d);
            if (!hit.hit) c = pixelStylise(pixelSky(d), o_.bands, 0);
            else {
                const PV3 q{eye[0] + d[0] * hit.t, eye[1] + d[1] * hit.t, eye[2] + d[2] * hit.t};
                c = pixelStylise(pixelShadePoint(q, pixelSceneNormal(q), hit.mat), o_.bands, 0);
            }
        }
        for (int k = 0; k < 3; ++k) F.img[p * 4 + std::size_t(k)] = float(c[std::size_t(k)]);
        F.img[p * 4 + 3] = 1;
    }
    return F;
}

std::uint8_t pixelEncode8(double v) {
    const double l = std::min(1.0, std::max(0.0, v));
    return std::uint8_t(std::floor((l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055) * 255 + 0.5));
}

}  // namespace raw::style
