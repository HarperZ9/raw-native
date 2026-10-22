// P23's measures: see raw/renderer/art_tiltshift_checks.hpp.
#include "raw/renderer/art_tiltshift_checks.hpp"
#include "raw/core/parallel.hpp"
#include "raw/renderer/art_strokes.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art {
namespace {
double lum3(const std::vector<float>& a, std::size_t i) { return 0.2126 * a[i * 4] + 0.7152 * a[i * 4 + 1] + 0.0722 * a[i * 4 + 2]; }
std::vector<double> gradMag(const std::vector<double>& Y, int w, int h) {
    std::vector<double> g(Y.size(), 0.0);
    auto at = [&](int x, int y) { return Y[std::size_t(std::clamp(y, 0, h - 1)) * std::size_t(w) + std::size_t(std::clamp(x, 0, w - 1))]; };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const double gx = (at(x + 1, y - 1) + 2 * at(x + 1, y) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2 * at(x - 1, y) - at(x - 1, y + 1)) / 4;
        const double gy = (at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1) - at(x - 1, y - 1) - 2 * at(x, y - 1) - at(x + 1, y - 1)) / 4;
        g[std::size_t(y) * std::size_t(w) + std::size_t(x)] = std::sqrt(gx * gx + gy * gy);
    }
    return g;
}
double p95(std::vector<double> v) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[std::size_t(0.95 * double(v.size() - 1))]; }
}  // namespace

std::vector<double> lumaOf(const std::vector<float>& rgba) {
    std::vector<double> y(rgba.size() / 4);
    for (std::size_t i = 0; i < y.size(); ++i) y[i] = lum3(rgba, i);
    return y;
}

std::vector<float> displayOf(const std::vector<float>& lit, double exposure) {
    std::vector<float> o(lit.size(), 1.0f);
    for (std::size_t i = 0; i + 3 < lit.size(); i += 4) {
        const double r = std::max(0.0, double(lit[i])), g = std::max(0.0, double(lit[i + 1])), b = std::max(0.0, double(lit[i + 2]));
        const double m = std::max({r, g, b}), t = m * exposure, gain = t < 1e-3 ? exposure * (1 - t / 2 + (t * t) / 6) : (1 - std::exp(-t)) / m;
        o[i] = float(r * gain); o[i + 1] = float(g * gain); o[i + 2] = float(b * gain);
    }
    return o;
}

double inkWeight(const std::vector<float>& lines, const std::vector<float>& noLines, const std::vector<std::uint8_t>& mask) {
    double s = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) if (mask[i]) s += std::max(0.0, lum3(noLines, i) - lum3(lines, i));
    return s;
}

std::vector<std::uint8_t> dilate(const std::vector<std::uint8_t>& m, int w, int h, int r) {
    std::vector<std::uint8_t> o(m.size(), 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        if (!m[std::size_t(y) * std::size_t(w) + std::size_t(x)]) continue;
        for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) {
            const int xx = x + dx, yy = y + dy;
            if (xx >= 0 && yy >= 0 && xx < w && yy < h && dx * dx + dy * dy <= r * r) o[std::size_t(yy) * std::size_t(w) + std::size_t(xx)] = 1;
        }
    }
    return o;
}

GradStats gradStats(const std::vector<float>& out, const std::vector<float>& in, const std::vector<std::uint8_t>& mask, int w, int h) {
    const std::vector<double> go = gradMag(lumaOf(out), w, h), gi = gradMag(lumaOf(in), w, h);
    std::vector<double> vo, vi;
    double so = 0, si = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) if (mask[i]) { vo.push_back(go[i]); vi.push_back(gi[i]); so += go[i]; si += gi[i]; }
    GradStats s;
    s.n = long(vo.size());
    if (si > 0) s.D = so / si;
    const double pi = p95(vi);
    if (pi > 0) s.E = p95(vo) / pi;
    return s;
}

int maxFlashes(const std::vector<std::vector<float>>& frames, int w, int h, int fps) {
    const int ww = w / 4, wh = h / 4;
    int worst = 0;
    for (int wy = 0; wy + wh <= h; wy += wh / 2) for (int wx = 0; wx + ww <= w; wx += ww / 2) {
        std::vector<double> m;
        for (const auto& f : frames) {
            double s = 0;
            for (int y = wy; y < wy + wh; ++y) for (int x = wx; x < wx + ww; ++x) s += lum3(f, std::size_t(y) * std::size_t(w) + std::size_t(x));
            m.push_back(s / (double(ww) * wh));
        }
        for (std::size_t a = 0; a < m.size(); ++a) {
            int trans = 0, dir = 0;
            double ext = m[a];
            for (std::size_t t = a + 1; t < m.size() && t < a + std::size_t(fps); ++t) {
                const double hi = std::max(ext, m[t]), lo = std::min(ext, m[t]);
                const int d = m[t] > ext ? 1 : -1;
                if (hi - lo >= 0.1 * hi && lo < 0.8) { if (d != dir) { ++trans; dir = d; } ext = m[t]; }
                else if ((dir == 1 && m[t] > ext) || (dir == -1 && m[t] < ext)) ext = m[t];
            }
            worst = std::max(worst, trans / 2);
        }
    }
    return worst;
}

int redWindows(const std::vector<float>& img, int w, int h) {
    const int ww = w / 4, wh = h / 4;
    int n = 0;
    for (int wy = 0; wy + wh <= h; wy += wh / 2) for (int wx = 0; wx + ww <= w; wx += ww / 2) {
        double r = 0, g = 0, b = 0;
        for (int y = wy; y < wy + wh; ++y) for (int x = wx; x < wx + ww; ++x) {
            const std::size_t i = (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4;
            r += img[i]; g += img[i + 1]; b += img[i + 2];
        }
        const double k = 1.0 / (double(ww) * wh);
        r *= k; g *= k; b *= k;
        n += r / std::max(1e-9, r + g + b) >= 0.8 && r > 0.5;
    }
    return n;
}

long redPixels(const std::vector<float>& img) {
    long n = 0;
    for (std::size_t i = 0; i + 3 < img.size(); i += 4) n += img[i] / std::max(1e-9f, img[i] + img[i + 1] + img[i + 2]) >= 0.8f && img[i] > 0.5f;
    return n;
}

TemporalRun runTiltOrbit(int w, int h, int frames, int first, int threads, std::uint64_t seed, bool rack) {
    StrokeOrbit o;
    o.w = w; o.h = h; o.frames = frames; o.threads = threads; o.step = rack ? 0.0 : 1.0;
    const style::PaintPlan plan = style::resolvePaint(style::paintPreset("oil"), w, h);
    const OrbitSeq s = renderOrbit(o, plan.p.exposure);
    const std::vector<style::PaintCanvas> adv = advectOrbit(s, w, h);
    TiltLook look = tiltLook("town_map");
    look.streetAxis = false;
    // The rack: walk on the small sphere, examine the column from frame 18, walk again from 34.
    std::vector<LensGeom> geoms;
    focus::Director dir(focus::Character::Naturalist, lensOf(look, {0, 1, 0}, {1, 0, 0}), s.views[0]);
    for (int t = 0; t < frames; ++t) {
        if (!rack) { geoms.push_back(resolveLens(lensOf(look, {0, 1, 0}, {1, 0, 0}), s.views[std::size_t(t)])); continue; }
        focus::Inputs in;
        in.player = {2.6, 0.6, 1.0}; in.prop = {-2.8, 1.6, -1.4};
        in.intent = t >= 18 && t < 34 ? focus::Intent::Examine : focus::Intent::Walk;
        in.playerMoving = true;
        dir.step(in, 1.0 / 24);
        geoms.push_back(dir.geom());
    }
    const std::size_t nf = static_cast<std::size_t>(frames);
    std::vector<std::vector<std::uint8_t>> ours(nf), base(nf), oref(nf);
    parallelRows(frames, threads, [&](int t) {
        const Frame& f = s.frames[std::size_t(t)];
        const std::vector<float> paint = style::runPaint(plan, f.lit, &adv[std::size_t(t)]);
        const std::vector<float> coc = cocMap(f, geoms[std::size_t(t)]);
        base[std::size_t(t)] = codes8(paint);
        ours[std::size_t(t)] = codes8(paintDefocus(paint, coc, w, h, {}, DefocusMode::Paint, 1));
        oref[std::size_t(t)] = codes8(paintDefocus(displayOf(f.lit, plan.p.exposure), coc, w, h, {}, DefocusMode::Gaussian, 1));
    });
    TemporalRun r;
    for (int t = first; t < frames; ++t) {
        const std::size_t i = std::size_t(t);
        const std::vector<std::uint8_t>& valid = s.valid[i];   // a held camera: every pixel's history is valid
        r.base.push_back(instability(base[i], base[i - 1], s.ref[i], s.ref[i - 1], s.mv[i], valid, w, h));
        r.ours.push_back(instability(ours[i], ours[i - 1], oref[i], oref[i - 1], s.mv[i], valid, w, h));
    }
    r.oursVsBase = pairedBootstrap(r.ours, r.base, seed);
    return r;
}

}  // namespace raw::art
