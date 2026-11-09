// P1, the held-time clock: bounds A1 to A5 and controls C1 to C3 of evidence/heldtime-loop-bounds.json.
#include "check.hpp"
#include "raw/core/sha256.hpp"
#include "raw/renderer/heldtime_scene.hpp"
#include "raw/renderer/style_paint.hpp"
#include "raw/scene/held_time.hpp"
#include <cmath>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <vector>
using namespace raw;
using anim::Rational;

namespace {
struct Run { std::vector<std::string> digests; std::vector<bool> poseChanged, seedChanged; std::vector<std::int64_t> ticks; };
// 10 s of the character layer at `fps`; seedPerFrame plants control C1.
Run runLayer(const anim::Rate& rate, Rational fps, bool seedPerFrame = false) {
    heldtime::DemoSpec s; s.character = rate; s.loop.fps = fps;
    const std::int64_t frames = 10 * fps.num / fps.den;
    Run r;
    std::uint32_t lastSeed = 0;
    for (std::int64_t k = 0; k < frames; ++k) {
        const heldtime::DemoFrame f = heldtime::characterPose(s, k);
        const std::uint32_t seed = seedPerFrame ? anim::boilSeed(heldtime::characterLayer(s), std::uint64_t(k)) : f.boilSeed;
        r.poseChanged.push_back(k == 0 || f.poseDigest != r.digests.back());
        r.seedChanged.push_back(k == 0 || seed != lastSeed);
        r.digests.push_back(f.poseDigest);
        r.ticks.push_back(f.tick);
        lastSeed = seed;
    }
    return r;
}
int lateOnsets(const anim::Rate& rate, Rational fps, const std::vector<std::int64_t>& ticks) {
    int bad = 0;
    for (std::size_t k = 0; k < ticks.size(); ++k)
        if ((k == 0 || ticks[k] != ticks[k - 1]) && anim::onsetFrame(rate, ticks[k], fps) != std::int64_t(k)) ++bad;
    return bad;
}
// C3: a clock that accumulates time in float64.
std::vector<std::int64_t> floatClockTicks(double rateHz, int fps, int frames) {
    std::vector<std::int64_t> t; double time = 0;
    for (int k = 0; k < frames; ++k) { t.push_back(std::int64_t(std::floor(time * rateHz))); time += 1.0 / fps; }
    return t;
}
}  // namespace

const Rational f24{24, 1};
void checkA1() {
    // A1: held ratio over 10 s at 24 fps.
    const anim::HoldStats ones = anim::holdStats(runLayer(anim::onN(1), f24).digests);
    const anim::HoldStats twos = anim::holdStats(runLayer(anim::onN(2), f24).digests);
    const anim::HoldStats threes = anim::holdStats(runLayer(anim::onN(3), f24).digests);
    std::printf("A1 held ratio: ones %.4f twos %.4f threes %.4f (frames %d)\n", ones.heldRatio, twos.heldRatio, threes.heldRatio, twos.frames);
    CHECK(twos.frames == 240);
    CHECK(ones.heldRatio == 0.0);
    CHECK(std::abs(twos.heldRatio - 0.50) <= 0.01);
    CHECK(std::abs(threes.heldRatio - 0.67) <= 0.01);
}
void checkA2() {
    // A2: camera continuous, ratio 0 at every render rate.
    for (const std::int64_t fps : {24, 30, 60, 144}) {
        heldtime::DemoSpec s; s.loop.fps = {fps, 1}; s.cameraPeriod = 4 * fps;
        std::vector<std::string> d;
        for (std::int64_t k = 0; k < 10 * fps; ++k) { const auto e = heldtime::cameraEye(s, k); d.push_back(sha256Hex(e.data(), sizeof e)); }
        const double r = anim::holdStats(d).heldRatio;
        std::printf("A2 camera ratio at %lld fps: %.4f\n", (long long)fps, r);
        CHECK(r == 0.0);
    }
}
void checkA345() {
    // A3, A4, A5 across render rates, for ones, twos, threes and the named 12 Hz rate.
    const anim::Rate rates[] = {anim::onN(1), anim::onN(2), anim::onN(3), anim::named(12)};
    const char* names[] = {"ones", "twos", "threes", "12 Hz"};
    for (int i = 0; i < 4; ++i) {
        std::string first;
        for (const std::int64_t fps : {24, 30, 60, 144}) {
            const Run r = runLayer(rates[i], {fps, 1});
            const int ex = anim::subsetExceptions(r.poseChanged, r.seedChanged), late = lateOnsets(rates[i], {fps, 1}, r.ticks);
            const std::string sha = anim::holdStats(r.digests).sequenceSha;
            std::printf("%s at %lld fps: A3 exceptions %d, A5 late onsets %d, A4 sequence %.16s\n", names[i], (long long)fps, ex, late, sha.c_str());
            CHECK(ex == 0);
            CHECK(late == 0);
            if (fps == 24) continue;   // A4 compares 30, 60 and 144
            if (first.empty()) first = sha; else CHECK(sha == first);
        }
    }
}
void checkControls() {
    // C1: a seed hashed from the render frame breaks the subset rule.
    const Run c1 = runLayer(anim::onN(2), f24, true);
    const int c1ex = anim::subsetExceptions(c1.poseChanged, c1.seedChanged);
    std::printf("C1 seed per frame: exceptions %d (must be > 0)\n", c1ex);
    CHECK(c1ex > 0);
    // C2: an unheld character fails the twos band and differs across render rates.
    const double c2ratio = anim::holdStats(runLayer(anim::continuous(), f24).digests).heldRatio;
    const std::string c2a = anim::holdStats(runLayer(anim::continuous(), {30, 1}).digests).sequenceSha;
    const std::string c2b = anim::holdStats(runLayer(anim::continuous(), {144, 1}).digests).sequenceSha;
    std::printf("C2 unheld: ratio %.4f, 30 and 144 fps sequences %s\n", c2ratio, c2a == c2b ? "equal" : "differ");
    CHECK(std::abs(c2ratio - 0.50) > 0.01);
    CHECK(c2a != c2b);
    // C3 (exploratory): a float64-accumulating 12 Hz clock at 144 fps.
    const std::vector<std::int64_t> c3 = floatClockTicks(12.0, 144, 1440);
    const int c3late = lateOnsets(anim::named(12), {144, 1}, c3);
    std::printf("C3 float clock at 144 fps: late or early onsets %d\n", c3late);
}
void checkUnits() {
    // Clock unit checks: exact ticks at awkward rates, onsets, refusals.
    CHECK(anim::sample({"x", anim::named(24000, 1001)}, 1001, {24, 1}).tick == 1000);
    CHECK(anim::sample({"x", anim::onN(3)}, 143, {144, 1}).tick == 7);
    CHECK(anim::onsetFrame(anim::onN(2), 5, {60, 1}) == 25);
    CHECK(anim::onsetFrame(anim::named(8), 1, {30, 1}) == 4);
    bool threw = false;
    try { (void)anim::onN(0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // The same vectors web/anim/heldtime.test.mjs checks, and clip.mjs's values for the demo clip.
    CHECK(anim::fnv64("a", 1) == 0xaf63dc4c8601ec8cULL);
    const std::uint64_t h = anim::fnv64("held-time", 9);
    CHECK(h == 0x5d1f91e1100c04a0ULL);
    CHECK(anim::boilSeed({"x", anim::onN(2), 0x51a7e5u}, h) == 0xca77cef2u);
    const struct { double t; double v[6]; } js[] = {
        {0.3, {0.1636799999999999, 0.08927999999999994, 0.22319999999999984, 0.14879999999999993, 1.862399999999999, -0.4487999999999999}},
        {1.7, {0.1166399999999999, -0.07571428571428558, -0.2663294117647057, 0.16199999999999987, 0.3504, -1.5959999999999988}},
        {3.9, {0.0231999999999999, -0.0231999999999999, 0.0463999999999998, 0.0231999999999999, 0.508799999999999, -0.46239999999999926}},
        {4.25, {0.1375, 0.075, 0.1875, 0.125, 1.6124999999999998, -0.425}}};
    for (const auto& c : js) {
        const std::vector<double> v = anim::sampleClip(heldtime::demoClip(), c.t);
        for (int i = 0; i < 6; ++i) CHECK_NEAR(v[std::size_t(i)], c.v[i], 1e-12);
    }
}

// Paint boil: the same held pose paints the same pixels; a new pose moves the noise.
void checkPaintBoil() {
    const int w = 40, h = 24;
    std::vector<float> scene(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float* px = &scene[(std::size_t(y) * w + x) * 4];
        px[0] = 0.2f + 0.6f * float(x) / w; px[1] = 0.3f + 0.4f * float(y) / h; px[2] = 0.5f; px[3] = 1.0f;
    }
    heldtime::DemoSpec s;
    const auto paint = [&](std::int64_t frame) {
        style::PaintParams p = style::paintPreset("oil");
        p.canvasOffset = style::paintBoilOffset(heldtime::characterPose(s, frame).boilSeed);
        return style::runPaint(style::resolvePaint(p, w, h), scene, nullptr);
    };
    const auto f0 = paint(0), f1 = paint(1), f2 = paint(2);
    std::printf("paint boil: frames 0 and 1 (one held pose) %s, frames 1 and 2 %s\n", f0 == f1 ? "equal" : "differ", f1 == f2 ? "equal" : "differ");
    CHECK(f0 == f1);
    CHECK(f1 != f2);
}

int main() {
    checkA1(); checkA2(); checkA345(); checkControls(); checkUnits(); checkPaintBoil();
    return raw_test_summary();
}
