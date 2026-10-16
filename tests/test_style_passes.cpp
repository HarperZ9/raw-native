// Properties of the C++ core's style references (ADR 0019) that need no exported fixture: the
// ones web/shaders/ditherlight/ditherlight.test.mjs, paintover/paintover.test.mjs and
// web/look/keys.test.mjs assert, restated here so every platform's ctest runs them. The
// cross-language comparison is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/style_ditherlight.hpp"
#include "raw/renderer/style_paintover.hpp"
#include "raw/renderer/look_keys.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
using namespace raw::style;

static void thresholds() {
    // Bayer 8 x 8, first row and column of the classic matrix
    const int row0[8] = {0, 32, 8, 40, 2, 34, 10, 42}, col0[8] = {0, 48, 12, 60, 3, 51, 15, 63};
    for (int i = 0; i < 8; ++i) { CHECK(bayer8Rank(i, 0) == row0[i]); CHECK(bayer8Rank(0, i) == col0[i]); }
    std::set<int> b, n;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) b.insert(bayer8Rank(x, y));
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) n.insert(blueRank(x, y));
    CHECK(b.size() == 64); CHECK(n.size() == 4096);
    CHECK(bayer8Rank(-1, -3) == bayer8Rank(7, 5)); CHECK(blueRank(-1, -3) == blueRank(63, 61));
}
static void meanPreservation() {
    for (int levels : {2, 3, 4, 8})
        for (Source s : {Source::Bayer8, Source::Blue64}) {
            DitherPlan p = ditherPreset("ink", 180, 1.2);
            p.levels = levels; p.source = s;
            const int M = s == Source::Bayer8 ? 64 : 4096, S = s == Source::Bayer8 ? 8 : 64;
            for (double Y : {0.013, 0.1, 0.27, 0.5, 0.77, 0.95}) {
                double sum = 0;
                for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) sum += levelLinear(p, quantise(p, Y, thresholdAt(s, x, y)));
                const int k = std::min(levels - 2, int(std::floor(std::pow(Y, 1 / p.gamma) * (levels - 1))));
                const double step = levelLinear(p, k + 1) - levelLinear(p, k);
                CHECK(std::abs(sum / (S * S) - Y) <= (0.5 / M) * step + 1e-12);
            }
        }
}
static void oneBitAndFog() {
    const DitherPlan p = ditherPreset("ink", 180, 1.2);
    for (int i = 0; i < 64; ++i) {
        const double t = thresholdAt(Source::Bayer8, i & 7, i >> 3);
        CHECK(quantise(p, 0, t) == 0); CHECK(quantise(p, 1, t) == 1);
        int prev = 0;
        for (double Y = 0; Y <= 1; Y += 0.01) { const int k = quantise(p, Y, t); CHECK(k >= prev); prev = k; }
    }
    DitherPixel px; px.kind = 2; px.albedo = 0.05; px.depth = 5000; px.nrm = {0, 1, 0};
    CHECK(toneOf(p, {}, px) == 1);
}
static void masks() {
    const DitherLight r = makeRadial({0, 0, 0}, 5, 1, 1.2);
    CHECK(maskAt(r, {5, 0, 0}) == 0); CHECK(maskAt(r, {9, 0, 0}) == 0);
    double prev = 1e9;
    for (double d = 0.1; d < 5; d += 0.1) { const double m = maskAt(r, {d, 0, 0}); CHECK(m <= prev && m >= 0 && m <= 1.2); prev = m; }
    const DitherLight b = makeBeam({0, 0, 0}, {0, 0, -1}, 10, 4, 20, 1);
    CHECK(maskAt(b, {0, 0, 25}) == 0); CHECK(maskAt(b, {0, 0, 10}) == 0);
    CHECK(maskAt(b, {0, 0, -10}) > 0.4); CHECK(maskAt(b, {5, 0, -10}) == 0);
    CHECK(std::abs(maskAt(b, {1, 0, -10}) - maskAt(b, {-1, 0, -10})) < 1e-12);
    CHECK(surfaceTerm(r, {1, 0, 0}, {-1, 0, 0}) == 1); CHECK(surfaceTerm(r, {1, 0, 0}, {1, 0, 0}) == 0);
}
static void anchoring() {
    DitherPlan wf = ditherPreset("ink", 180, 1.2);
    wf.anchor = Anchor::WorldFixed; wf.cell = 0.1;
    DitherPixel px; px.ix = 3; px.iy = 4; px.apos = {1.234, 0.5, 2.5}; px.anrm = {0, 1, 0}; px.depth = 4;
    CHECK((anchorCell(wf, px) == std::array<int, 2>{12, 25}));
    px.anrm = {1, 0, 0};
    CHECK((anchorCell(wf, px) == std::array<int, 2>{25, 5}));
    const double pa = pixelAngle(1.2, 180);
    CHECK(octaveOf(0.1, 2, pa, 0.1) == 0); CHECK(octaveOf(0.1, 2, pa, 40) > octaveOf(0.1, 2, pa, 10));
}
// A flat painted plane seen from the projector's own camera: the projected colour is the painting.
static void paintRoundTrip() {
    const int W = 32, H = 18;
    Projector pr;
    pr.cam = isoCamera(45, 35.264, {0, 0, 0}, 60, 4);
    pr.w = W; pr.h = H; pr.rgba.resize(std::size_t(W) * H * 4); pr.depth.resize(std::size_t(W) * H);
    for (int i = 0; i < W * H; ++i) for (int c = 0; c < 3; ++c) pr.rgba[std::size_t(i) * 4 + c] = std::uint8_t((i * 37 + c * 91) & 255);
    // the depth of the plane y = 0 seen through each pixel centre
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const double u = (((x + 0.5) / W) * 2 - 1) * (double(W) / H), v = 1 - ((y + 0.5) / H) * 2;
        V3 o; for (int k = 0; k < 3; ++k) o[k] = pr.cam.pos[k] + (pr.cam.right[k] * u + pr.cam.up[k] * v) * pr.cam.scale;
        const double t = -o[1] / pr.cam.fwd[1];
        pr.depth[std::size_t(y) * W + x] = float(t);
    }
    const PaintOptions o;
    const std::vector<const Projector*> set{&pr};
    const std::vector<double> reach = reaches(set, o);
    int covered = 0, worst = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const double u = (((x + 0.5) / W) * 2 - 1) * (double(W) / H), v = 1 - ((y + 0.5) / H) * 2;
        V3 P; for (int k = 0; k < 3; ++k) P[k] = pr.cam.pos[k] + (pr.cam.right[k] * u + pr.cam.up[k] * v) * pr.cam.scale;
        const double t = -P[1] / pr.cam.fwd[1];
        for (int k = 0; k < 3; ++k) P[k] += pr.cam.fwd[k] * t;
        const PaintShade s = shadePoint(set, reach, P, {0, 1, 0}, pr.cam, o);
        if (!s.covered) continue;
        covered++;
        for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(int(std::floor(s.rgb[c] * 255 + 0.5)) - pr.rgba[std::size_t(y * W + x) * 4 + c]));
    }
    CHECK(covered == W * H); CHECK(worst == 0);
    // a depth map that hides every surface covers nothing
    for (float& d : pr.depth) d -= 10;
    CHECK(!shadePoint(set, reach, {0, 0, 0}, {0, 1, 0}, pr.cam, o).covered);
}
static void lookKeys() {
    const std::vector<double> e8{0.2, 0.15, 0.6, 1.0, 1.2, 0.9, 0.5, 0.25};
    TermSpec t; for (double v : e8) t.values.push_back({v});
    const LookKeys k(slots8(), {{"exposure", t}});
    for (std::size_t i = 0; i < 8; ++i) CHECK(k.term("exposure", slots8()[i].hour)[0] == e8[i]);
    for (double h = 0; h < 24; h += 0.05) {
        const Located L = locate(slots8(), h);
        const double v = k.term("exposure", h)[0], lo = std::min(e8[std::size_t(L.i)], e8[std::size_t(L.j)]), hi = std::max(e8[std::size_t(L.i)], e8[std::size_t(L.j)]);
        CHECK(v >= lo - 1e-12 && v <= hi + 1e-12);
    }
    CHECK(k.term("exposure", 24)[0] == k.term("exposure", 0)[0]);
    CHECK(k.term("exposure", -2)[0] == k.term("exposure", 22)[0]);
    bool refused = false;
    try { LookKeys(std::vector<Slot>(slots8().begin(), slots8().begin() + 5), {}); } catch (const std::invalid_argument&) { refused = true; }
    CHECK(refused);
    refused = false;
    try { (void)k.term("nope", 3); } catch (const std::invalid_argument&) { refused = true; }
    CHECK(refused);
}

int main() {
    thresholds(); meanPreservation(); oneBitAndFog(); masks(); anchoring(); paintRoundTrip(); lookKeys();
    return raw_test_summary();
}
