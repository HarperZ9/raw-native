// The held-time demonstration scene: see raw/renderer/heldtime_scene.hpp.
#include "raw/renderer/heldtime_scene.hpp"
#include "raw/core/sha256.hpp"
#include "raw/renderer/style_hatch.hpp"
#include "raw/renderer/style_image.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace raw::heldtime {
namespace {
using style::V3;
constexpr double kPi = 3.14159265358979323846;
constexpr double kClipSeconds = 4.0;

double len3(double x, double y, double z) { return std::sqrt(x * x + y * y + z * z); }
double capsule(const V3& p, const std::array<double, 3>& a, const std::array<double, 3>& b, double r) {
    const double bx = b[0] - a[0], by = b[1] - a[1], bz = b[2] - a[2], px = p[0] - a[0], py = p[1] - a[1], pz = p[2] - a[2];
    const double h = std::clamp((px * bx + py * by + pz * bz) / (bx * bx + by * by + bz * bz), 0.0, 1.0);
    return len3(px - bx * h, py - by * h, pz - bz * h) - r;
}
// The ground is a round table top (radius 2.7): past its rim the ray misses and the frame closes to
// paper, so no distant hatching aliases at the horizon.
double table(const V3& p) { return std::max(p[1], std::sqrt(p[0] * p[0] + p[2] * p[2]) - 2.7); }
// The still life's torus and rounded box, moved aside for the figure.
double torus(const V3& p) {
    const double dx = p[0] - 1.35, dz = p[2] - 0.55, qx = std::sqrt(dx * dx + dz * dz) - 0.5, qy = p[1] - 0.18;
    return std::sqrt(qx * qx + qy * qy) - 0.18;
}
double box(const V3& p) {
    const double qx = std::abs(p[0] + 1.25) - 0.22, qy = std::abs(p[1] - 0.3) - 0.22, qz = std::abs(p[2] + 0.45) - 0.22;
    return len3(std::max(qx, 0.0), std::max(qy, 0.0), std::max(qz, 0.0)) + std::min(std::max({qx, qy, qz}), 0.0) - 0.08;
}
double figureSdf(const std::vector<std::array<double, 3>>& j, const V3& p) {
    const auto off = [&](int i, double dx) { return std::array<double, 3>{j[std::size_t(i)][0] + dx, j[std::size_t(i)][1], j[std::size_t(i)][2]}; };
    const std::array<double, 3> headTop{j[3][0], j[3][1] + 0.16, j[3][2]};
    double d = capsule(p, j[1], j[2], 0.2);
    d = std::min(d, capsule(p, j[2], j[3], 0.07));
    d = std::min(d, len3(p[0] - headTop[0], p[1] - headTop[1], p[2] - headTop[2]) - 0.19);
    d = std::min(d, capsule(p, j[4], j[6], 0.09));
    d = std::min(d, capsule(p, j[4], j[5], 0.07));
    d = std::min(d, capsule(p, j[6], j[7], 0.07));
    d = std::min(d, capsule(p, off(1, 0.13), j[8], 0.09));
    return std::min(d, capsule(p, off(1, -0.13), j[9], 0.09));
}
// A layer's tick reduced by its own whole period within the loop (or the raw tick when not whole).
std::int64_t loopTick(const anim::Layer& l, const DemoSpec& s, std::int64_t tick) {
    const std::int64_t n = anim::ticksPerLoop(l.rate, s.loop);
    return n > 0 ? ((tick % n) + n) % n : tick;
}
double clipTime(const anim::Layer& l, const DemoSpec& s, std::int64_t frame, std::int64_t& tick) {
    const anim::HeldSample hs = anim::sample(l, frame, s.loop.fps);
    tick = hs.tick;
    if (l.rate.kind == anim::RateKind::Continuous) {   // continuous: reduce frames by the clip's frames
        const std::int64_t clipFrames = std::int64_t(std::llround(kClipSeconds * double(s.loop.fps.num) / double(s.loop.fps.den)));
        return double(((frame % clipFrames) + clipFrames) % clipFrames) * double(s.loop.fps.den) / double(s.loop.fps.num);
    }
    const std::int64_t clipTicks = std::int64_t(std::llround(kClipSeconds * double(l.rate.hz.num) / double(l.rate.hz.den)));
    return double(((tick % clipTicks) + clipTicks) % clipTicks) * double(l.rate.hz.den) / double(l.rate.hz.num);
}
}  // namespace

anim::Layer characterLayer(const DemoSpec& s) { return {"character", s.character, 0x51a7e5u}; }
anim::Layer cameraLayer() { return {"camera", anim::continuous(), 0xca3e7au}; }
anim::Layer effectsLayer() { return {"effects", anim::named(12), 0xf1c4e2u}; }

std::vector<anim::LoopTerm> demoTerms(const DemoSpec& s) {
    const anim::Layer ch = characterLayer(s), fx = effectsLayer();
    const auto clipTicks = [&](const anim::Layer& l) {
        return l.rate.kind == anim::RateKind::Continuous ? std::int64_t(0)
             : std::int64_t(std::llround(kClipSeconds * double(l.rate.hz.num) / double(l.rate.hz.den)));
    };
    const anim::LayerTerm c = anim::layerTerm(ch, s.loop, clipTicks(ch)), e = anim::layerTerm(fx, s.loop, 0);
    const std::int64_t clipFrames = std::int64_t(std::llround(kClipSeconds * double(s.loop.fps.num) / double(s.loop.fps.den)));
    const std::int64_t chPeriod = ch.rate.kind == anim::RateKind::Continuous ? clipFrames : (c.ok ? c.term.period : -1);
    return {{"character", chPeriod}, {"camera", s.cameraPeriod}, {"effects", e.ok ? e.term.period : -1}};
}

namespace {
Figure poseInto(const DemoSpec& s, std::int64_t frame, DemoFrame& out) {
    const anim::Layer ch = characterLayer(s);
    Figure fig = figureAt(clipTime(ch, s, frame, out.tick));
    out.poseDigest = sha256Hex(fig.poseBuffer.data(), fig.poseBuffer.size() * sizeof(float));
    out.boilSeed = anim::boilSeed(ch, anim::fnv64(fig.poseBuffer.data(), fig.poseBuffer.size() * sizeof(float)));
    return fig;
}
}  // namespace

DemoFrame characterPose(const DemoSpec& s, std::int64_t frame) { DemoFrame out; poseInto(s, frame, out); return out; }

std::array<double, 3> cameraEye(const DemoSpec& s, std::int64_t frame) {
    const double a = 0.55 + 0.35 * std::sin(2 * kPi * anim::phase(frame, s.cameraPeriod));   // camera on ones
    return {4.2 * std::sin(a), 2.6, 4.2 * std::cos(a)};
}

DemoFrame renderDemo(const DemoSpec& s, std::int64_t frame) {
    DemoFrame out;
    const anim::Layer fx = effectsLayer();
    const Figure fig = poseInto(s, frame, out);
    const std::int64_t fxTick = loopTick(fx, s, anim::sample(fx, frame, s.loop.fps).tick);
    out.fxSeed = anim::boilSeed(fx, anim::fnv64(&fxTick, sizeof fxTick));
    const std::array<double, 3> eye = cameraEye(s, frame);
    const double j1 = double(out.fxSeed & 0xFFFF) / 65535.0 - 0.5, j2 = double(out.fxSeed >> 16) / 65535.0 - 0.5;
    style::HatchField field{[&fig](const V3& p) { return std::min({table(p), figureSdf(fig.joints, p), torus(p), box(p)}); },
                            {eye[0], eye[1], eye[2]}, {0, 0.85, 0}, {-0.5 + 0.18 * j1, 0.8, 0.45 + 0.18 * j2}, 0.42};
    const style::HatchFrame hf = style::runHatchField(style::hatchPreset(s.preset), field, {out.boilSeed, s.boil, 5.5}, s.w, s.h);
    out.rgb.resize(std::size_t(s.w) * s.h * 3);
    for (std::size_t i = 0; i < std::size_t(s.w) * s.h; ++i)
        for (std::size_t k = 0; k < 3; ++k) out.rgb[i * 3 + k] = style::encodeDisplay8(hf.rgba[i * 4 + k]);
    return out;
}

}  // namespace raw::heldtime
