// P23, the tilt-shift lens (evidence/tiltshift-bounds.json g2): the town's float64 CoC and its
// tilt-shifted codes at 256 x 144 (town_map) hashed and pinned per compiler family; the lens's
// geometry identities; and the focus director's checks d1 to d5.
//   test_art_tiltshift
#include "raw/renderer/art_tiltshift_checks.hpp"
#include "raw/renderer/style_paint.hpp"
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
struct Pinned { const char* family; std::uint64_t coc, codes; };
// Recorded 2026-10-22 on MSVC (VS 18) and GCC 13.3; codes re-pinned under addendum C (half-resolution defocus).
constexpr Pinned kPinned[] = {{"msvc", 0x2c4cd941314f50b5ull, 0x1be05b9af2f1e66bull}, {"gcc", 0x2c4cd941314f50b5ull, 0x1be05b9af2f1e66bull}};

void checkPinned(const char* what, std::uint64_t got, std::uint64_t Pinned::*field) {
    std::printf("g2 %s %s 0x%016llxull\n", kFamily, what, static_cast<unsigned long long>(got));
    for (const Pinned& p : kPinned) if (!std::strcmp(p.family, kFamily) && p.*field) CHECK(got == p.*field);
}

void checkLens() {
    // An upright plane through the focus point facing the camera: the focus point is sharp, a point
    // behind it along the same ray is not, and the CoC grows with the distance from the plane.
    const View v = townView(1920, 1080);
    const LensGeom g = resolveLens(lensOf(tiltLook("town_map"), {0, 0.89, 0}, {1, 0, 0}), v);
    CHECK(cocAtPoint(g, {0, 0.89, 0}) < 1e-9);
    CHECK(cocAtPoint(g, {3, 0.89, 0}) < 1e-6);                 // along the street axis: in the plane
    const double a = cocAtPoint(g, {0, 0.89, -1}), b = cocAtPoint(g, {0, 0.89, -2});
    CHECK(a > 1 && b > a);
    // A plane parallel to the sensor (tilt chosen so the plane's normal is the view direction) has
    // no hinge.
    CHECK(hingeTiltDeg(g) > 0 && hingeTiltDeg(g) < 10);
}

void checkGoldens() {
    const View v = townView(256, 144);
    const Frame f = renderFrame(v, townDusk(), 4, SceneKind::Town);
    const LensGeom g = resolveLens(lensOf(tiltLook("town_map"), {0, 0.89, 0}, {1, 0, 0}), v);
    const std::vector<float> coc = cocMap(f, g);
    const std::vector<float> paint = style::runPaint(style::resolvePaint(style::paintPreset("oil"), 256, 144), f.lit, nullptr);
    const std::vector<float> ts = paintDefocus(paint, coc, 256, 144, {}, DefocusMode::Paint, 4);
    checkPinned("coc", fnv1a(coc), &Pinned::coc);
    checkPinned("codes", fnv1a(codes8(ts)), &Pinned::codes);
    // In-focus pixels are untouched (weight 0 below c0).
    const std::vector<float> w = defocusWeight(coc, 256, 144, {});
    long same = 0, sharp = 0;
    for (std::size_t i = 0; i < w.size(); ++i) if (coc[i * 2] <= 1.0) { ++sharp; same += ts[i * 4] == paint[i * 4] && ts[i * 4 + 1] == paint[i * 4 + 1] && ts[i * 4 + 2] == paint[i * 4 + 2]; }
    CHECK(sharp > 0 && same == sharp);
}

void checkDirector() {
    const DirectorReport r = runDirectorChecks();
    std::printf("d1 overshoot %.3g (underdamped %.3g); d2 fails %d of %d (lock off %d); d3 drifts %d calm %d/%d; d4 %.3f/%.3f s; d5 %.4g at %.3f Hz\n",
                r.maxOvershoot, r.controlOvershoot, r.lockFails, r.lockFramesChecked, r.controlLockFails, r.drifts, r.calmDrifts, r.calmAnimated,
                r.eventLatency, r.eventSettle, r.maxJitterStep, r.jitterHz);
    CHECK(r.maxOvershoot <= 1e-9 && r.controlOvershoot > 1e-3);
    CHECK(r.lockFails == 0 && r.controlLockFails >= 1 && r.eventsInLocks >= 3 && r.idleInLocks >= 20);
    CHECK(r.drifts >= 2 && r.calmDrifts == 0 && r.calmAnimated == 0);
    for (double d : r.dwells) CHECK(d >= 2 && d <= 6);
    for (double t : r.returns) CHECK(t <= 2);
    CHECK(r.eventLatency >= 0 && r.eventLatency <= 0.25 && r.eventSettle >= 0 && r.eventSettle <= 1.0);
    CHECK(r.maxJitterStep <= 0.02 && r.jitterHz <= 0.5 && r.calmJitter == 0);
}
}  // namespace

int main() {
    checkLens();
    checkGoldens();
    checkDirector();
    return raw_test_summary();
}
