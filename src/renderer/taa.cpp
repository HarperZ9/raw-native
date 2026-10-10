// Temporal anti-aliasing on the CPU: see raw/renderer/taa.hpp.
#include "raw/renderer/taa.hpp"
#include "raw/renderer/raster.hpp"
#include <algorithm>
#include <cmath>
namespace raw::taa {
namespace {
double halton(int i, int b) {
    double f = 1.0, r = 0.0;
    for (; i > 0; i /= b) { f /= b; r += f * (i % b); }
    return r;
}
void toYcocg(const double c[3], double o[3]) {
    o[0] = 0.25 * c[0] + 0.5 * c[1] + 0.25 * c[2];
    o[1] = 0.5 * c[0] - 0.5 * c[2];
    o[2] = -0.25 * c[0] + 0.5 * c[1] - 0.25 * c[2];
}
void fromYcocg(const double c[3], double o[3]) {
    o[0] = c[0] + c[1] - c[2];
    o[1] = c[0] + c[2];
    o[2] = c[0] - c[1] - c[2];
}
// Catmull-Rom weights (B = 0, C = 0.5) of the taps at -1, 0, 1, 2 for a fraction t.
void catmullRom(double t, double wt[4]) {
    const double t2 = t * t, t3 = t2 * t;
    wt[0] = -0.5 * t3 + t2 - 0.5 * t;
    wt[1] = 1.5 * t3 - 2.5 * t2 + 1.0;
    wt[2] = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
    wt[3] = 0.5 * t3 - 0.5 * t2;
}
}  // namespace

void jitter(int k, float& jx, float& jy) {
    jx = float(halton(k % 16 + 1, 2) - 0.5);
    jy = float(halton(k % 16 + 1, 3) - 0.5);
}

Frame render(const Scene& scene, const Camera& prevCam, Vec3 moved, int w, int h, float jx, float jy, int ss) {
    Frame f; f.w = w; f.h = h;
    const int W = w * ss, H = h * ss;
    Buffer<uint16_t> ids;
    RasterOptions ro; ro.perspectiveDepth = true; ro.jitterX = jx * float(ss); ro.jitterY = jy * float(ss);
    const GBuffer g = rasterize(scene, W, H, nullptr, &ids, ro);
    const Vec3 L = normalize(scene.lights[0].dir);
    const uint16_t movingId = uint16_t(scene.meshes.size());
    const Mat4 pvp = mul(prevCam.proj(), prevCam.view());
    f.rgb.assign(std::size_t(w) * h * 3, 0.0f);
    f.depth.assign(std::size_t(w) * h, -1.0f);
    f.prev.assign(std::size_t(w) * h * 3, 0.0f);
    if (ss > 1) f.coverage.assign(std::size_t(w) * h, 0.0f);
    const float inv = 1.0f / float(ss * ss);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * w + x;
        for (int b = 0; b < ss; ++b) for (int a = 0; a < ss; ++a) {
            const int X = x * ss + a, Y = y * ss + b;
            Vec3 c{kBackground, kBackground, kBackground};
            if (g.mask.at(X, Y)) {
                const float lit = kAmbient + std::max(0.0f, -dot(normalize(g.normal.at(X, Y)), L));
                c = g.albedo.at(X, Y) * lit;
                if (ss > 1 && ids.at(X, Y) == movingId) f.coverage[i] += inv;
            }
            f.rgb[i * 3] += c.x * inv; f.rgb[i * 3 + 1] += c.y * inv; f.rgb[i * 3 + 2] += c.z * inv;
        }
        if (ss > 1) continue;
        // Where the history holds this pixel: the surface point's previous projection, plus the jitter.
        f.prev[i * 3] = float(x) + 0.5f; f.prev[i * 3 + 1] = float(y) + 0.5f; f.prev[i * 3 + 2] = -1.0f;
        if (!g.mask.at(x, y)) continue;
        f.depth[i] = g.depth.at(x, y);
        Vec3 p = g.position.at(x, y);
        if (ids.at(x, y) == movingId) p = p - moved;
        const Vec4 c = mul(pvp, Vec4{p.x, p.y, p.z, 1.0f});
        if (c.w <= 1e-6f) continue;
        f.prev[i * 3] = (c.x / c.w * 0.5f + 0.5f) * float(w) + jx;
        f.prev[i * 3 + 1] = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * float(h) + jy;
        f.prev[i * 3 + 2] = c.w;
    }
    return f;
}

std::vector<float> resolve(const Frame& f, const std::vector<float>& hist, const std::vector<float>& prevDepth, const Options& o) {
    const int w = f.w, h = f.h;
    std::vector<float> out(f.rgb);
    if (hist.empty()) return out;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const std::size_t i = std::size_t(y) * w + x;
        const double px = f.prev[i * 3], py = f.prev[i * 3 + 1], ed = f.prev[i * 3 + 2];
        if (px < 0.5 || py < 0.5 || px > w - 0.5 || py > h - 0.5) continue;            // off screen: the current frame
        if (o.depthTest) {
            // Method note 6: the expected depth against the range of the previous depths over 3 x 3 pixels.
            const int nx = std::min(w - 1, int(px)), ny = std::min(h - 1, int(py));
            double lo = 1e30, hi = -1.0;
            bool empty = false;
            for (int b = -1; b <= 1; ++b) for (int a = -1; a <= 1; ++a) {
                const int qx = std::clamp(nx + a, 0, w - 1), qy = std::clamp(ny + b, 0, h - 1);
                const double pd = prevDepth[std::size_t(qy) * w + qx];
                if (pd < 0.0) { empty = true; continue; }
                lo = std::min(lo, pd); hi = std::max(hi, pd);
            }
            const double tol = 0.02 * ed + 0.02;
            if (ed < 0.0 ? !empty : (hi < 0.0 || ed < lo - tol || ed > hi + tol)) continue;   // disoccluded
        }
        // Catmull-Rom history at (px, py) over 4 x 4 pixels, centres at +0.5 (method note 5).
        const double fx = px - 0.5, fy = py - 0.5;
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        double wx[4], wy[4];
        catmullRom(fx - x0, wx); catmullRom(fy - y0, wy);
        double hc[3] = {0, 0, 0};
        for (int b = 0; b < 4; ++b) for (int a = 0; a < 4; ++a) {
            const int qx = std::clamp(x0 - 1 + a, 0, w - 1), qy = std::clamp(y0 - 1 + b, 0, h - 1);
            const std::size_t j = (std::size_t(qy) * w + qx) * 3;
            for (int c = 0; c < 3; ++c) hc[c] += wx[a] * wy[b] * hist[j + std::size_t(c)];
        }
        if (o.clip) {   // clip toward the neighbourhood mean, to the box of mean +- 1.25 sigma in YCoCg
            double yc[9][3], m[3] = {0, 0, 0}, var[3] = {0, 0, 0};
            int n = 0;
            for (int b = -1; b <= 1; ++b) for (int a = -1; a <= 1; ++a) {
                const int qx = x + a, qy = y + b;
                if (qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
                const std::size_t j = (std::size_t(qy) * w + qx) * 3;
                const double c[3] = {f.rgb[j], f.rgb[j + 1], f.rgb[j + 2]};
                toYcocg(c, yc[n]);
                for (int k = 0; k < 3; ++k) m[k] += yc[n][k];
                ++n;
            }
            for (int k = 0; k < 3; ++k) m[k] /= n;
            for (int q = 0; q < n; ++q) for (int k = 0; k < 3; ++k) var[k] += (yc[q][k] - m[k]) * (yc[q][k] - m[k]);   // two passes (note 8)
            double hy[3], scale = 0.0;
            toYcocg(hc, hy);
            for (int k = 0; k < 3; ++k) {
                const double ext = kClipSigma * std::sqrt(var[k] / n) + 1e-7;
                scale = std::max(scale, std::fabs(hy[k] - m[k]) / ext);
            }
            if (scale > 1.0) for (int k = 0; k < 3; ++k) hy[k] = m[k] + (hy[k] - m[k]) / scale;
            fromYcocg(hy, hc);
        }
        for (int c = 0; c < 3; ++c) out[i * 3 + std::size_t(c)] = float(hc[c] + (f.rgb[i * 3 + std::size_t(c)] - hc[c]) * kBlend);
    }
    return out;
}

}  // namespace raw::taa
