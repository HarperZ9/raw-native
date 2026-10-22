// P5, world-anchored strokes (evidence/p5-world-strokes-bounds.json w4): the art scene's frame, the
// world anchor and the world-anchored paint frames hashed and pinned per compiler family; and the
// pieces under them: motion vectors, history, the anchor's scale and the warp metric.
//   test_art_strokes
#include "raw/renderer/art_strokes.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
using namespace raw;
using namespace raw::art;

namespace {
#if defined(_MSC_VER)
constexpr const char* kFamily = "msvc";
#elif defined(__clang__)
constexpr const char* kFamily = "clang";
#elif defined(__GNUC__)
constexpr const char* kFamily = "gcc";
#else
constexpr const char* kFamily = "other";
#endif
struct Pinned { const char* family; std::uint64_t lit, anchor, oil, watercolour; };
// Recorded 2026-10-22 on GCC 13.3 and MSVC (VS 18); the two agree.
constexpr Pinned kPinned[] = {{"gcc", 0x8e70a9159d2007bfull, 0x364d778712fdc849ull, 0x3745369e1e2a4c6full, 0x89864af512059e7cull},
                               {"msvc", 0x8e70a9159d2007bfull, 0x364d778712fdc849ull, 0x3745369e1e2a4c6full, 0x89864af512059e7cull}};

void checkPinned(const char* what, std::uint64_t got, std::uint64_t Pinned::*field) {
    std::printf("w4 %s %s 0x%016llxull\n", kFamily, what, static_cast<unsigned long long>(got));
    for (const Pinned& p : kPinned) if (!std::strcmp(p.family, kFamily) && p.*field) CHECK(got == p.*field);
}
void checkScene(const Frame& f) {
    int count[7] = {};
    for (std::uint8_t o : f.obj) if (o < 7) count[o]++;
    for (int o = 0; o < 7; ++o) { std::printf("scene object %d covers %d pixels\n", o, count[o]); CHECK(count[o] >= 100); }
}
void checkMotion(const Frame& f, const View& v) {
    const std::vector<float> mv = motionTo(f, v);   // the same view: no motion, every pixel valid
    double worst = 0;
    for (float m : mv) worst = std::fmax(worst, std::fabs(m));
    CHECK(worst < 1e-3);
    long valid = 0;
    for (std::uint8_t k : historyValid(f, f, v, mv)) valid += k;
    CHECK(valid == long(f.obj.size()));
}
void checkAnchorScale(const Frame& f, const AnchorParams& a) {
    // On the ground (y dominant) the chart is (x, z) times the scale, so a step of the point gives
    // the same step of the coordinates times the scale.
    const std::vector<float> C = anchorCanvas(f, a);
    const std::size_t p = std::size_t(130) * std::size_t(f.w) + 30, q = p + 1;
    CHECK(f.obj[p] == 1 && f.obj[q] == 1);
    const double du = double(C[q * 4]) - C[p * 4], dx = double(f.P[q * 3]) - f.P[p * 3];
    CHECK_NEAR(du, dx * a.scale, 1e-3);
}
void checkMetric() {
    const int w = 4, h = 3;
    std::vector<std::uint8_t> a(w * h * 4, 100), b(w * h * 4, 140), z(w * h, 1);
    std::vector<float> mv(w * h * 2, 0.0f);
    CHECK(instability(a, a, a, a, mv, z, w, h) == 0.0);
    CHECK(instability(b, a, b, a, mv, z, w, h) == 0.0);   // the style follows the scene's change exactly
    CHECK_NEAR(instability(b, a, a, a, mv, z, w, h), 40.0 / 255.0, 1e-12);
    const Paired p = pairedBootstrap({1, 1, 1, 1}, {2, 2, 2, 2}, 7, 1000);
    CHECK_NEAR(p.reduction, 0.5, 1e-12);
    CHECK(p.diffLo == -1.0 && p.diffHi == -1.0);
}
}  // namespace

int main() {
    const View v = orbitView(0, 256, 144);
    const Frame f = renderFrame(v, art::daylight(), 4);
    checkScene(f);
    checkMotion(f, v);
    const AnchorParams a = anchorParams(v);
    checkAnchorScale(f, a);
    checkMetric();
    checkPinned("lit", fnv1a(f.lit), &Pinned::lit);
    checkPinned("anchor", fnv1a(anchorCanvas(f, a)), &Pinned::anchor);
    checkPinned("oil", fnv1a(codes8(paintWorld(f, a, style::resolvePaint(style::paintPreset("oil"), f.w, f.h)))), &Pinned::oil);
    checkPinned("watercolour", fnv1a(codes8(paintWorld(f, a, style::resolvePaint(style::paintPreset("watercolour"), f.w, f.h)))), &Pinned::watercolour);
    return raw_test_summary();
}
