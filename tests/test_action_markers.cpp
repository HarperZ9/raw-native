// G3 marker test (STATE-g3g5 M1, M2): clip markers exported from Low Water's JS engine
// (tests/data/action/clip-markers.json, tools/action/export_markers.mjs) against the C++ move table.
//  - M1: every clip of the 230 moves.json moves has active, recovery, end, tell and every hit window
//    equal to the C++ move table's frames;
//  - M2: startup, active and recovery read from the markers through raw::anim::layout equal the table;
//  - planted: a hit's on marker +1, and an active marker -1, in a copy: exactly that clip is reported.
#include "action_tables.hpp"
#include "check.hpp"

using namespace action_test;

static std::vector<std::string> run(const MarkerSet& ms, const Tables& t, int& compared) {
    std::vector<std::string> out;
    compared = 0;
    for (const MarkerClip& c : ms.clips) {
        const Move* m = t.find(c.key);
        if (!m) continue;
        ++compared;
        for (auto& line : check_markers(c, *m)) out.push_back(line);
    }
    return out;
}

static int phase_mismatches(const MarkerSet& ms, const Tables& t) {
    int bad = 0;
    for (const MarkerClip& c : ms.clips) {
        const Move* m = t.find(c.key);
        if (!m) continue;
        const raw::anim::Layout L = c.phases();
        const auto *s = L.find("startup"), *a = L.find("active"), *r = L.find("recovery");
        if (!(s && a && r && s->n == m->startup && a->n == m->active && r->n == m->recovery && L.length == m->total)) ++bad;
    }
    return bad;
}

int main() {
    const MarkerSet ms = load_markers(slurp(DIR + "clip-markers.json"));
    const Tables t = load_tables();
    int compared = 0, hits = 0, tells = 0;
    const auto bad = run(ms, t, compared);
    for (const MarkerClip& c : ms.clips) if (t.find(c.key)) { hits += (int)c.hits.size(); tells += c.has_tell; }
    std::printf("M1: %zu clips exported (commit %s), %d in the C++ table of %zu, %d hits, %d tells, %zu mismatches\n",
                ms.clips.size(), ms.commit.c_str(), compared, t.where.size(), hits, tells, bad.size());
    for (auto& l : bad) std::printf("  %s\n", l.c_str());
    CHECK(compared == 230 && t.where.size() == 230 && bad.empty());
    const int ph = phase_mismatches(ms, t);
    std::printf("M2: phases read from markers through raw::anim::layout: %d of %d differ\n", ph, compared);
    CHECK(ph == 0);
    // planted (a): one hit's on marker +1
    MarkerSet pa = ms; std::string key_a;
    for (MarkerClip& c : pa.clips) if (t.find(c.key) && !c.hits.empty()) { c.hits[0].on += 1; key_a = c.key; break; }
    const auto ca = run(pa, t, compared);
    std::printf("planted hit0.on +1 on %s: %zu reported\n", key_a.c_str(), ca.size());
    CHECK(ca.size() == 1 && ca[0].rfind(key_a + " hit0.on", 0) == 0);
    // planted (b): one active marker -1
    MarkerSet pb = ms; std::string key_b;
    for (MarkerClip& c : pb.clips) if (t.find(c.key) && c.active > 0) { c.active -= 1; key_b = c.key; break; }
    const auto cb = run(pb, t, compared);
    std::printf("planted active -1 on %s: %zu reported\n", key_b.c_str(), cb.size());
    CHECK(cb.size() == 1 && cb[0].rfind(key_b + " active", 0) == 0);
    CHECK(phase_mismatches(pb, t) == 1);
    return raw_test_summary();
}
