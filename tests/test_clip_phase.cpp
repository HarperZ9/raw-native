// raw/scene/clip_phase against web/anim/clip.mjs (G3, M2 of the 2026-11-08 G3/G5 state):
//  - every vector of tests/data/action/clip-warp-vectors.json (tools/action/clip_vectors.mjs) gives the
//    JS layout length, key times, phase, done flag and warp bit for bit;
//  - planted: one expected warp value moved by one ulp is caught.
#include "check.hpp"
#include "raw/scene/clip_phase.hpp"
#include "raw/story/story.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

using raw::story::Json;
static const std::string DIR = std::string(RAW_SOURCE_DIR) + "/tests/data/action/";
static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static bool same(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

static raw::anim::Layout lay(const Json& phases, double frames) {
    std::vector<std::pair<std::string, double>> v;
    for (const Json& p : phases.a) v.emplace_back(p.a[0].s, p.a[1].n);
    return raw::anim::layout(v, frames);
}

// Mismatches in one case; `bump` moves expected warp value 0 by one ulp (the planted check).
static int run_case(const Json& c, bool bump) {
    const auto A = lay(*c.get("from"), c.get("fromFrames")->n), B = lay(*c.get("to"), c.get("toFrames")->n);
    int bad = 0;
    if (!same(A.length, c.get("length")->n)) ++bad;
    if (!same(raw::anim::keyTime("", 0.42, A), c.get("whole")->n)) ++bad;
    for (const Json& k : c.get("keys")->a) if (!same(raw::anim::keyTime(k.a[0].s, k.a[1].n, A), k.a[2].n)) ++bad;
    const auto& fs = c.get("f")->a;
    for (size_t i = 0; i < fs.size(); ++i) {
        double want = c.get("warp")->a[i].n;
        if (bump && i == 0) want = std::nextafter(want, 1e300);
        if (!same(raw::anim::warp(A, B, fs[i].n), want)) ++bad;
        const auto p = raw::anim::phaseAt(A, fs[i].n);
        if (p.phase != c.get("phase")->a[i].s || p.done != c.get("done")->a[i].b) ++bad;
    }
    return bad;
}

int main() {
    const Json v = raw::story::parse_json(slurp(DIR + "clip-warp-vectors.json"));
    int bad = 0, n = 0;
    for (const Json& c : v.get("cases")->a) { bad += run_case(c, false); n += (int)c.get("f")->a.size(); }
    std::printf("clip_phase: %zu cases, %d frames, %d mismatches against clip.mjs\n", v.get("cases")->a.size(), n, bad);
    CHECK(bad == 0 && n > 100);
    const int planted = run_case(v.get("cases")->a[0], true);
    std::printf("planted one-ulp warp: %d caught\n", planted);
    CHECK(planted == 1);
    // refusals
    bool threw = false;
    try { raw::anim::layout({}, 0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    return raw_test_summary();
}
