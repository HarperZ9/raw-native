// G5 telegraph retiming (STATE-g3g5 R1 to R4) over every move with a tell in clip-markers.json:
// the moves.json expansion (all sets) and the inner fights' moves.
//  - R1a: no retime shortens a lead below T9's need (first active frame minus tell onset, both on the
//    retimed clip); R1b: the moves whose flat lead is already short under needOf, recorded (amendment A2);
//  - R2: hit volumes keyed to the retimed clip are live on the move table's frames;
//  - R3: parryable tells have the slow-then-fast shape (0.25x to 0.75x, then 1.2x to 2.0x);
//  - R4: every ease keeps clip time non-decreasing and advances value * length across a region;
//  - planted: wind-up from frame 0, strike ended a frame early, strike 2.4x, no regions, a bad ease.
#include "action_tables.hpp"
#include "check.hpp"
#include "raw/game/action/retime.hpp"
#include <cmath>
#include <functional>

using namespace action_test;

struct Run { int moves = 0, parryable = 0, flat_short = 0, induced = 0, issues = 0, lead = 0, keyed = 0, shape = 0, lands = 0; std::vector<std::string> lines, short_keys; };
static Run lint_all(const MarkerSet& ms, const std::function<Retime(const MarkerClip&)>& author) {
    Run r;
    for (const MarkerClip& c : ms.clips) {
        if (!c.has_tell) continue;
        ++r.moves; r.parryable += c.parryable;
        const RetimeReport rep = lint_retime(c, author(c));
        if (rep.lead_flat < rep.need) { ++r.flat_short; r.short_keys.push_back(c.key); }
        r.induced += rep.lead < rep.need && rep.lead < rep.lead_flat;
        for (const std::string& l : rep.issues) {
            ++r.issues; r.lines.push_back(l);
            r.lead += l.find(" lead:") != std::string::npos; r.keyed += l.find(" keyed:") != std::string::npos;
            r.shape += l.find(" shape:") != std::string::npos; r.lands += l.find(" lands:") != std::string::npos;
        }
    }
    return r;
}
static void show(const char* name, const Run& r, size_t lines = 0) {
    std::printf("%s: %d tell moves (%d parryable), flat leads short %d, retime-induced short %d; issues %d (lead %d, keyed %d, shape %d, lands %d)\n",
                name, r.moves, r.parryable, r.flat_short, r.induced, r.issues, r.lead, r.keyed, r.shape, r.lands);
    for (size_t i = 0; i < lines && i < r.lines.size(); ++i) std::printf("  %s\n", r.lines[i].c_str());
}

// R4 over any curve: non-decreasing on [0, 1] and the region's advance exact.
static bool ease_ok(const std::function<double(double)>& curve) {
    double prev = curve(0);
    if (std::fabs(prev) > 1e-12 || std::fabs(curve(1) - 1) > 1e-12) return false;
    for (int i = 1; i <= 1000; ++i) { const double v = curve(i / 1000.0); if (v < prev - 1e-15) return false; prev = v; }
    return true;
}
static bool region_ok(Ease e) {
    Retime r; r.regions = {{0.5, 4, 14, e}, {1.75, 14, 22, e}};
    double prev = -1;
    for (int i = 0; i <= 3000; ++i) { const double c = clip_time(r, i / 100.0); if (c < prev - 1e-12) return false; prev = c; }
    return std::fabs(clip_time(r, 14) - (4 + 5)) < 1e-12 && std::fabs(clip_time(r, 22) - (4 + 5 + 14)) < 1e-12;
}

int main() {
    const MarkerSet ms = load_markers(slurp(DIR + "clip-markers.json"));
    auto fitted = [](const MarkerClip& c) { return fit_telegraph(c.startup(), c.tell_on); };
    const Run base = lint_all(ms, fitted);
    show("R1-R3 fitted 0.5x / solved strike", base, 12);
    // R1b: the data's own short leads (needOf asks 30 of an unblockable; tests/telegraph.test.mjs does not)
    const std::vector<std::string> known = {"base captain.hook", "base warden.hook"};
    CHECK(base.short_keys == known);
    CHECK(base.moves == 211 && base.induced == 0 && base.issues == base.flat_short && base.lead == base.flat_short);
    int lo = 0, hi = 0; double smin = 9, smax = 0;
    for (const MarkerClip& c : ms.clips) if (c.has_tell) { auto r = fit_telegraph(c.startup(), c.tell_on); if (r.regions.size() == 2) { smin = std::min(smin, r.regions[1].value); smax = std::max(smax, r.regions[1].value); ++hi; } else ++lo; }
    std::printf("strike values solved: %.4f to %.4f over %d moves; %d moves too short to fit\n", smin, smax, hi, lo);
    for (Ease e : {Ease::In, Ease::Out, Ease::InOut}) {
        const Run r = lint_all(ms, [e](const MarkerClip& c) { return fit_telegraph(c.startup(), c.tell_on, {0.5, 1.5, e}); });
        std::printf("  ease %s: issues %d\n", ease_name(e), r.issues);
        CHECK(r.induced == 0 && r.issues == base.flat_short);
    }
    // planted
    const Run p0 = lint_all(ms, [](const MarkerClip& c) { return fit_telegraph(c.startup(), 0); });
    show("planted wind-up from frame 0", p0);
    CHECK(p0.induced > 0);
    const Run p1 = lint_all(ms, [&](const MarkerClip& c) { Retime r = fitted(c); if (!r.regions.empty()) r.regions[1].end -= 1; return r; });
    show("planted strike ends a frame early", p1);
    CHECK(p1.lands > 0 && p1.keyed > 0);
    const Run p2 = lint_all(ms, [](const MarkerClip& c) { return fit_telegraph(c.startup(), c.tell_on, {0.5, 2.4}); });
    show("planted strike 2.4x", p2);
    CHECK(p2.shape > 0);
    const Run p3 = lint_all(ms, [](const MarkerClip&) { return Retime{}; });
    show("planted no regions", p3);
    CHECK(p3.shape == p3.parryable && p3.lead == base.flat_short && p3.keyed == 0);
    // R4
    bool eases = true;
    for (Ease e : {Ease::Linear, Ease::In, Ease::Out, Ease::InOut}) eases = eases && ease_ok([e](double u) { return ease_curve(e, u); }) && region_ok(e);
    const bool bad = ease_ok([](double u) { return 0.9 * u; });
    std::printf("R4: eases %s; planted end-at-0.9 curve %s\n", eases ? "hold" : "FAIL", bad ? "passed (WRONG)" : "caught");
    CHECK(eases && !bad);
    return raw_test_summary();
}
