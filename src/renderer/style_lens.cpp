// The lens suite's sources and passes: see raw/renderer/style_lens.hpp. Operations are in the
// order of web/shaders/lens/plan.mjs (extractSources, prepareSources) and lens.mjs.
#include "raw/renderer/style_lens.hpp"
#include <algorithm>
#include <cmath>
namespace raw::style {
namespace {
constexpr double kPi = 3.141592653589793;
double binNm(int l) { return 420 + 40 * l; }
double overOf(double r, double g, double b, double thr) {
    const double Y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    if (!(Y > thr)) return 0;
    return (Y - thr) / Y;
}
double sampleCore(const std::vector<double>& core, int W, int H, double px, double py, int c) {
    const double x = std::min(double(W - 1), std::max(0.0, px - 0.5)), y = std::min(double(H - 1), std::max(0.0, py - 0.5));
    const int x0 = std::min(W - 2, int(std::floor(x))), y0 = std::min(H - 2, int(std::floor(y)));
    const double fx = x - x0, fy = y - y0;
    const std::size_t i = (std::size_t(y0) * W + std::size_t(x0)) * 4 + std::size_t(c), w4 = std::size_t(W) * 4;
    return (core[i] * (1 - fx) + core[i + 4] * fx) * (1 - fy) + (core[i + w4] * (1 - fx) + core[i + w4 + 4] * fx) * fy;
}
double edgeRot(double phi, int N, double round, double rot) {
    const double seg = (2 * kPi) / N;
    double a = std::fmod(phi - rot, seg);
    if (a < 0) a += seg;
    const double poly = std::cos(kPi / N) / std::cos(a - kPi / N);
    return poly + (1 - poly) * round;
}
double starAt(const LensStar& st, double u, double v, int k) {
    const int M = st.M;
    if (u < 0 || v < 0 || u > M - 1 || v > M - 1) return 0;
    const int x0 = std::min(M - 2, int(std::floor(u))), y0 = std::min(M - 2, int(std::floor(v)));
    const double fx = u - x0, fy = v - y0;
    const std::size_t i = (std::size_t(y0) * M + std::size_t(x0)) * 3 + std::size_t(k), m3 = std::size_t(M) * 3;
    const auto& d = st.data;
    return (d[i] * (1 - fx) + d[i + 3] * fx) * (1 - fy) + (d[i + m3] * (1 - fx) + d[i + m3 + 3] * fx) * fy;
}
}  // namespace

std::vector<LensRawSource> extractLensSources(const LensPlan& plan, const std::vector<double>& d, std::vector<std::uint8_t>& keep) {
    const LensParams& p = plan.p;
    const int W = plan.W, H = plan.H, T = p.tile, tw = (W + T - 1) / T, th = (H + T - 1) / T;
    struct Tile { int t; double x, y; std::array<double, 3> E; double Y; };
    std::vector<Tile> tiles;
    for (int ty = 0; ty < th; ++ty) for (int tx = 0; tx < tw; ++tx) {
        std::array<double, 3> E{0, 0, 0};
        double sw = 0, sx = 0, sy = 0;
        for (int y = ty * T; y < std::min(H, ty * T + T); ++y) for (int x = tx * T; x < std::min(W, tx * T + T); ++x) {
            const std::size_t i = (std::size_t(y) * W + std::size_t(x)) * 4;
            const double f = overOf(d[i], d[i + 1], d[i + 2], p.threshold);
            if (f <= 0) continue;
            const double e0 = d[i] * f, e1 = d[i + 1] * f, e2 = d[i + 2] * f, y0 = 0.2126 * e0 + 0.7152 * e1 + 0.0722 * e2;
            E[0] += e0; E[1] += e1; E[2] += e2; sw += y0; sx += (x + 0.5) * y0; sy += (y + 0.5) * y0;
        }
        if (sw > 0) tiles.push_back({ty * tw + tx, sx / sw, sy / sw, E, sw});
    }
    std::stable_sort(tiles.begin(), tiles.end(), [](const Tile& a, const Tile& b) { return a.Y != b.Y ? a.Y > b.Y : a.t < b.t; });
    keep.assign(std::size_t(tw) * th, 0);
    std::vector<LensRawSource> out;
    for (std::size_t k = 0; k < tiles.size() && int(k) < p.maxSources; ++k) {
        keep[std::size_t(tiles[k].t)] = 1;
        out.push_back({tiles[k].x, tiles[k].y, tiles[k].E});
    }
    return out;
}

std::vector<LensSource> prepareLensSources(const LensPlan& plan, const std::vector<LensRawSource>& list) {
    const LensParams& p = plan.p;
    std::vector<LensSource> out;
    for (std::size_t k = 0; k < list.size() && int(k) < p.maxSources; ++k) {
        const LensRawSource& s = list[k];
        LensSource o;
        o.x = s.x; o.y = s.y; o.E = s.E;
        o.theta = std::atan((std::hypot(s.x - plan.cx, s.y - plan.cy) / plan.D) * plan.tanHalf);
        for (int l = 0; l < kLensBins; ++l) {
            const double r = lensFilmReflectance(binNm(l), o.theta, p.coating);
            o.ghost[std::size_t(l)] = p.ghostGain * r * r;
            o.streak[std::size_t(l)] = (p.streakGain * r) / plan.Rbar;
        }
        out.push_back(o);
    }
    return out;
}

std::vector<double> runLens(const LensPlan& plan, const std::vector<double>& d, const std::vector<LensSource>& srcs, const std::vector<std::uint8_t>& keep) {
    const LensParams& p = plan.p;
    const int W = plan.W, H = plan.H, tw = (W + p.tile - 1) / p.tile;
    std::vector<double> core(static_cast<std::size_t>(W) * H * 4), out(core.size());
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const std::size_t i = (std::size_t(y) * W + std::size_t(x)) * 4;
        const double k = !keep.empty() && keep[std::size_t((y / p.tile) * tw + x / p.tile)] ? overOf(d[i], d[i + 1], d[i + 2], p.threshold) : 0;
        for (int c = 0; c < 3; ++c) core[i + std::size_t(c)] = d[i + std::size_t(c)] * (1 - plan.phi[std::size_t(c)] * k);
        core[i + 3] = 1;
    }
    const int N = p.iris.blades;
    const double rnd = p.iris.roundness, hM = (plan.star.M - 1) / 2.0, ca = std::cos(p.streakAngle), sa = std::sin(p.streakAngle);
    const double L = p.streakLenPx, sg = p.streakThickPx, sInv = 1 / (plan.starCell * plan.starCell);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const double qx = x + 0.5, qy = y + 0.5;
        double o[3] = {0, 0, 0};
        for (int l = 0; l < kLensBins; ++l) {
            const double s = plan.caScale[std::size_t(l)], sx = plan.cx + (qx - plan.cx) / s, sy = plan.cy + (qy - plan.cy) / s;
            for (int c = 0; c < 3; ++c) o[c] += plan.binRgb[std::size_t(l)][std::size_t(c)] * sampleCore(core, W, H, sx, sy, c);
        }
        for (const LensSource& s : srcs) {
            const double dx = qx - s.x, dy = qy - s.y;
            for (int c = 0; c < 3; ++c) o[c] += s.E[std::size_t(c)] * p.starGain * starAt(plan.star, dx / plan.starCell + hM, dy / plan.starCell + hM, c) * sInv;
            const double a = ca * dx + sa * dy, b = -sa * dx + ca * dy;
            const double k = (std::exp(-std::abs(a) / L) / (2 * L)) * (std::exp((-b * b) / (2 * sg * sg)) / (sg * 2.5066282746310002));
            const double r = std::hypot(dx, dy);
            for (int l = 0; l < kLensBins; ++l) {
                const double hk = (plan.halo[std::size_t(l)][0] * std::exp(-r / plan.halo[std::size_t(l)][1])) / (6.283185307179586 * plan.halo[std::size_t(l)][1] * plan.halo[std::size_t(l)][1]);
                const double f = s.streak[std::size_t(l)] * k + hk;
                for (int c = 0; c < 3; ++c) o[c] += s.E[std::size_t(c)] * plan.binRgb[std::size_t(l)][std::size_t(c)] * f;
            }
            for (const auto& g : plan.ghosts) for (int l = 0; l < kLensBins; ++l) {
                const auto& gl = g[std::size_t(l)];
                const double m = gl[0], rad = gl[1], rot = gl[2], inv = gl[3];
                const double gx = plan.cx + m * (s.x - plan.cx), gy = plan.cy + m * (s.y - plan.cy);
                const double vx = (qx - gx) * p.squeeze, vy = qy - gy, rr = std::hypot(vx, vy);
                if (rr > rad + 1) continue;
                const double cov = std::min(1.0, std::max(0.0, rad * edgeRot(std::atan2(vy, vx), N, rnd, rot) - rr + 0.5));
                if (cov <= 0) continue;
                const double f = s.ghost[std::size_t(l)] * cov * inv;
                for (int c = 0; c < 3; ++c) o[c] += s.E[std::size_t(c)] * plan.binRgb[std::size_t(l)][std::size_t(c)] * f;
            }
        }
        const std::size_t i = (std::size_t(y) * W + std::size_t(x)) * 4;
        out[i] = o[0]; out[i + 1] = o[1]; out[i + 2] = o[2]; out[i + 3] = 1;
    }
    return out;
}

}  // namespace raw::style
