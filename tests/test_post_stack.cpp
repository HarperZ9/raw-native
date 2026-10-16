// Properties of the post stack's driver and the round 2 image references that need no exported
// fixture (web/motion/post-contract.test.mjs and the web passes' own tests, restated so every
// platform's ctest runs them). The cross-language comparison is raw_native_cli style-parity.
#include "check.hpp"
#include "raw/renderer/post_stack.hpp"
#include "raw/renderer/style_hatch.hpp"
#include "raw/renderer/style_purity.hpp"
#include "raw/renderer/style_relight.hpp"
#include <cmath>
#include <stdexcept>
using namespace raw;
using post_stack::Spec;

static std::vector<std::string> names(const std::vector<Spec>& v) {
    std::vector<std::string> o;
    for (const Spec& s : v) o.push_back(s.pass);
    return o;
}
static void contract() {
    using V = std::vector<std::string>;
    CHECK(names(post_stack::plan({{"film", {}}}, false).scene) == V({"srgb-decode", "film"}));
    CHECK(post_stack::plan({{"film", {}}}, true).refused);
    CHECK(post_stack::plan({{"film", {}}, {"film", {}}}, false).refused);
    const post_stack::Plan mixed = post_stack::plan({{"exposure", {{"ev", 1}}}, {"dither", {}}}, false);
    CHECK(names(mixed.scene) == V({"exposure"}) && names(mixed.display) == V({"dither"}));
    CHECK(post_stack::plan({{"no-such-pass", {}}}, false).refused);
}
static void runOrder() {
    using namespace post_stack;
    // Three scene passes, no target: ping, pong, ping; the result is the last ping-pong frame.
    const Schedule s = schedule({{"exposure", {}}, {"srgb-decode", {}}, {"exposure", {}}}, Stage::Scene, false);
    CHECK(s.steps.size() == 3);
    CHECK(s.steps[0].src == kIn && s.steps[0].dst == kPing0 && s.steps[1].src == kPing0 && s.steps[1].dst == kPing1);
    CHECK(s.steps[2].dst == kPing0 && s.result == kPing0);
    // With a target the last pass draws into it.
    const Schedule t = schedule({{"crt", {}}, {"dither", {}}}, Stage::Display, true);
    CHECK(t.steps.back().dst == kOut && t.result == kOut);
    bool refused = false;
    try { (void)schedule({{"crt", {}}}, Stage::Scene, false); } catch (const std::invalid_argument&) { refused = true; }
    CHECK(refused);
}
static void hostChain() {
    using namespace post_stack;
    HostFrame in{2, 1, {0.5f, 0.02f, -0.5f, 1.0f, 1.0f, 0.0f, 0.25f, 0.5f}};
    HostStack st;
    HostFrame out;
    std::string err;
    CHECK(st.run({{"srgb-decode", {}}, {"exposure", {{"ev", 1}}}}, Stage::Scene, in, 0, out, err));
    CHECK(err.empty());
    CHECK(out.rgba[0] == float(double(float(srgbDecode(0.5))) * 2.0));
    CHECK(out.rgba[2] < 0 && out.rgba[3] == 1.0f && out.rgba[7] == 0.5f);   // sign-symmetric decode; alpha untouched
    CHECK(!st.run({{"film", {}}}, Stage::Scene, in, 0, out, err));            // no C++ reference yet: refused, not skipped
}
static void imageProperties() {
    // Purity: no landing error leaves the picture unchanged (the web test's zero-field identity).
    style::PurityPlan p = style::purityPreset("magnetised");
    p.mag = 0; p.earth = 0; p.degauss = 0;
    std::vector<float> src(4 * 4 * 4, 0.0f);
    for (std::size_t i = 0; i < src.size(); ++i) src[i] = float((i * 37 % 11) / 10.0);
    const std::vector<double> o = style::runPurity(p, 4, 4, src, 0);
    double dev = 0;
    for (std::size_t i = 0; i < o.size(); ++i) if ((i & 3) != 3) dev = std::max(dev, std::abs(o[i] - src[i]));
    CHECK(dev <= 1e-12);
    // Lightpaint: strength 0 is albedo times irradiance.
    style::LightpaintPlan l = style::lightpaintPreset("disco");
    l.strength = 0;
    const style::V3 a{0.8, 0.7, 0.1}, E{0.3, 0.2, 0.15}, c = style::lightpaintShade(l, a, E);
    for (std::size_t k = 0; k < 3; ++k) CHECK_NEAR(c[k], a[k] * E[k], 1e-12);
    // Glaze: no flakes leave the body colour.
    style::GlazePlan g = style::glazePreset("beetle");
    g.coverage = 0;
    const style::V3 b = style::glazeColour(g, a, 0.8, 300);
    for (std::size_t k = 0; k < 3; ++k) CHECK_NEAR(b[k], a[k], 1e-12);
    // Hatch: a sphere point is an umbilic; the ground is too.
    CHECK(style::curvature({-0.9, 1.1, 0.2}, 0.12).umbilic);
    CHECK(style::curvature({-3.0, 0.0, 2.0}, 0.12).umbilic);
}

int main() {
    contract(); runOrder(); hostChain(); imageProperties();
    return raw_test_summary();
}
