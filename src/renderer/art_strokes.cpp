// P5's orbit run: see raw/renderer/art_strokes.hpp.
#include "raw/renderer/art_strokes.hpp"
#include "raw/core/parallel.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <cmath>
namespace raw::art {
namespace {
using Seq = OrbitSeq;
}  // namespace
OrbitSeq renderOrbit(const StrokeOrbit& o, double exposure) {
    Seq s;
    const Lighting L = daylight();
    for (int t = 0; t < o.frames; ++t) {
        s.views.push_back(orbitView(t, o.w, o.h, o.step));
        s.frames.push_back(renderFrame(s.views.back(), L, o.threads));
        s.ref.push_back(paintReference(s.frames.back().lit, exposure));
        if (t == 0) { s.mv.emplace_back(std::size_t(o.w) * std::size_t(o.h) * 2, 0.0f); s.valid.emplace_back(); continue; }
        s.mv.push_back(motionTo(s.frames[std::size_t(t)], s.views[std::size_t(t - 1)]));
        s.valid.push_back(historyValid(s.frames[std::size_t(t)], s.frames[std::size_t(t - 1)], s.views[std::size_t(t - 1)], s.mv.back()));
    }
    return s;
}
// The advected canvas after each frame. Sky pixels get a far distance so their history can hold.
std::vector<style::PaintCanvas> advectOrbit(const OrbitSeq& s, int w, int h) {
    std::vector<style::PaintCanvas> out;
    style::PaintCanvas cv(w, h);
    const std::size_t n = std::size_t(w) * std::size_t(h);
    for (std::size_t t = 0; t < s.frames.size(); ++t) {
        const Frame& f = s.frames[t];
        std::vector<float> dist(n), distPrev(n);
        for (std::size_t p = 0; p < n; ++p) {
            dist[p] = f.dist[p] > 0 ? f.dist[p] : 1e4f;
            if (f.dist[p] <= 0 || t == 0) { distPrev[p] = dist[p]; continue; }
            const View& pv = s.views[t - 1];
            const double dx = f.P[p * 3] - pv.eye[0], dy = f.P[p * 3 + 1] - pv.eye[1], dz = f.P[p * 3 + 2] - pv.eye[2];
            distPrev[p] = float(std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        style::paintDecide(cv);
        style::paintAdvect(cv, s.mv[t], dist, distPrev);
        out.push_back(cv);
    }
    return out;
}
namespace {
std::vector<double> series(const Seq& s, const std::vector<std::vector<std::uint8_t>>& out, int first, int w, int h) {
    std::vector<double> v;
    for (std::size_t t = std::size_t(first); t < out.size(); ++t)
        v.push_back(instability(out[t], out[t - 1], s.ref[t], s.ref[t - 1], s.mv[t], s.valid[t], w, h));
    return v;
}
}  // namespace

std::vector<std::uint8_t> paintReference(const std::vector<float>& lit, double exposure) {
    std::vector<std::uint8_t> o(lit.size(), 255);
    for (std::size_t i = 0; i + 3 < lit.size(); i += 4) {
        const double r = std::max(0.0, double(lit[i])), g = std::max(0.0, double(lit[i + 1])), b = std::max(0.0, double(lit[i + 2]));
        const double m = std::max({r, g, b}), t = m * exposure;
        const double gain = t < 1e-3 ? exposure * (1 - t / 2 + (t * t) / 6) : (1 - std::exp(-t)) / m;
        o[i] = encode8(r * gain); o[i + 1] = encode8(g * gain); o[i + 2] = encode8(b * gain);
    }
    return o;
}

std::vector<float> paintWorld(const Frame& f, const AnchorParams& a, const style::PaintPlan& plan) {
    const style::PaintCanvas cv = worldCanvas(f.w, f.h, anchorCanvas(f, a));
    return style::runPaint(plan, f.lit, &cv);
}

StrokeOrbitResult runStrokeOrbit(const StrokeOrbit& o, std::uint64_t seed) {
    const style::PaintPlan plan = style::resolvePaint(style::paintPreset(o.preset), o.w, o.h);
    const Seq s = renderOrbit(o, plan.p.exposure);
    const std::vector<style::PaintCanvas> adv = advectOrbit(s, o.w, o.h);
    const AnchorParams a = anchorParams(s.views[0]);
    const int F = o.frames;
    std::vector<std::vector<float>> charts;
    for (const Frame& f : s.frames) charts.push_back(anchorCanvas(f, a));
    std::vector<std::vector<std::uint8_t>> outs(std::size_t(4 * F));
    parallelRows(4 * F, o.threads, [&](int job) {
        const int kind = job / F, t = job % F;
        const Frame& f = s.frames[std::size_t(t)];
        std::vector<float> img;
        if (kind == 0) { const style::PaintCanvas cv = worldCanvas(o.w, o.h, charts[std::size_t(t)]); img = style::runPaint(plan, f.lit, &cv); }
        else if (kind == 1) img = style::runPaint(plan, f.lit, &adv[std::size_t(t)]);
        else if (kind == 2) { const style::PaintCanvas cv = worldCanvas(o.w, o.h, charts[std::size_t((7 * t) % F)]); img = style::runPaint(plan, f.lit, &cv); }
        else img = style::runPaint(plan, f.lit, nullptr);
        outs[std::size_t(job)] = codes8(img);
    });
    auto part = [&](int kind) { return std::vector<std::vector<std::uint8_t>>(outs.begin() + kind * F, outs.begin() + (kind + 1) * F); };
    StrokeOrbitResult r;
    r.world = series(s, part(0), o.first, o.w, o.h);
    r.advected = series(s, part(1), o.first, o.w, o.h);
    r.shuffled = series(s, part(2), o.first, o.w, o.h);
    r.offset = series(s, part(3), o.first, o.w, o.h);
    r.worldVsAdvected = pairedBootstrap(r.world, r.advected, seed);
    r.shuffledVsAdvected = pairedBootstrap(r.shuffled, r.advected, seed);
    r.offsetVsAdvected = pairedBootstrap(r.offset, r.advected, seed);
    return r;
}

}  // namespace raw::art
