// P2 closed-loop contract: refusals (K1, K2) and closure measured on rendered frames (B1) at a
// small size; the full-size demonstration is measured by `raw_native_cli held-time`.
// evidence/heldtime-loop-bounds.json.
#include "check.hpp"
#include "raw/renderer/heldtime_scene.hpp"
#include "raw/scene/loop.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
using namespace raw;

namespace {
int maxDiff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    int m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) m = std::max(m, std::abs(int(a[i]) - int(b[i])));
    return m;
}
heldtime::DemoSpec small(const anim::Rate& r) { heldtime::DemoSpec s; s.character = r; s.w = 64; s.h = 36; return s; }
int closure(const heldtime::DemoSpec& s) {
    const std::int64_t n = s.loop.frames;
    return std::max(maxDiff(heldtime::renderDemo(s, n).rgb, heldtime::renderDemo(s, 0).rgb),
                    maxDiff(heldtime::renderDemo(s, n + 1).rgb, heldtime::renderDemo(s, 1).rgb));
}
void checkContract() {
    for (const anim::Rate& r : {anim::onN(1), anim::onN(2), anim::onN(3), anim::continuous()})
        CHECK(anim::checkLoop(small(r).loop, heldtime::demoTerms(small(r))).ok);
    heldtime::DemoSpec k1 = small(anim::onN(2)); k1.cameraPeriod = 97;
    const anim::LoopCheck c1 = anim::checkLoop(k1.loop, heldtime::demoTerms(k1));
    std::printf("K1 open camera: %s\n", c1.ok ? "accepted (wrong)" : c1.refused.front().c_str());
    CHECK(!c1.ok);
    heldtime::DemoSpec k2 = small(anim::onN(2)); k2.loop.frames = 99;
    const anim::LoopCheck c2 = anim::checkLoop(k2.loop, heldtime::demoTerms(k2));
    std::printf("K2 49.5 ticks a loop: %s\n", c2.ok ? "accepted (wrong)" : "refused");
    CHECK(!c2.ok);
    CHECK(anim::ticksPerLoop(anim::onN(2), k2.loop) == -1);
    CHECK(anim::ticksPerLoop(anim::onN(3), small(anim::onN(3)).loop) == 32);
    CHECK(anim::phase(-1, 96) == 95.0 / 96.0 && anim::phase(192, 96) == 0.0);
}
void checkClosure() {
    const char* names[] = {"ones", "twos", "threes"};
    int i = 0;
    for (const anim::Rate& r : {anim::onN(1), anim::onN(2), anim::onN(3)}) {
        const int d = closure(small(r));
        std::printf("B1 %s: max 8-bit difference frame N vs 0 and N+1 vs 1: %d\n", names[i++], d);
        CHECK(d == 0);
    }
    heldtime::DemoSpec k1 = small(anim::onN(2)); k1.cameraPeriod = 97;
    const int d = closure(k1);
    std::printf("K1 open camera: max difference %d (must be > 0)\n", d);
    CHECK(d > 0);
    // The loop's interior is not trivially static: frame 0 and frame N/2 differ.
    const heldtime::DemoSpec s = small(anim::onN(2));
    CHECK(maxDiff(heldtime::renderDemo(s, 0).rgb, heldtime::renderDemo(s, 48).rgb) > 0);
}
}  // namespace

int main() {
    checkContract();
    checkClosure();
    return raw_test_summary();
}
