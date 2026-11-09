// Telegraph retiming: see raw/game/action/retime.hpp.
#include "raw/game/action/retime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace raw::game::action {
namespace {

constexpr double EPS = 1e-9;

std::string fmt(double v) { char b[32]; std::snprintf(b, sizeof b, "%.4g", v); return b; }

void check_shape(const MarkerClip& c, const Retime& r, const LintBounds& b, std::vector<std::string>& out) {
    const std::string k = c.key + " shape: ";
    if (r.regions.size() != 2) { out.push_back(k + std::to_string(r.regions.size()) + " regions, want wind-up then strike"); return; }
    const TimeScale &w = r.regions[0], &s = r.regions[1];
    if (w.value < b.windup_lo || w.value > b.windup_hi) out.push_back(k + "wind-up " + fmt(w.value) + "x");
    if (s.value < b.strike_lo || s.value > b.strike_hi) out.push_back(k + "strike " + fmt(s.value) + "x");
    if (w.end != s.start) out.push_back(k + "wind-up and strike are not contiguous");
    if (s.end != c.startup()) out.push_back(k + "strike ends on " + std::to_string(s.end) + ", not the first active frame " + std::to_string(c.startup()));
}

void check_keyed(const MarkerClip& c, const Retime& r, std::vector<std::string>& out) {
    const int limit = c.end + 1;
    for (size_t i = 0; i < c.hits.size(); ++i) {
        const HitMarker& h = c.hits[i];
        const int on = sim_frame_at(r, h.on, limit), off = sim_frame_at(r, h.off + 1, limit) - 1;
        if (on != h.on || off != h.off)
            out.push_back(c.key + " keyed: hit" + std::to_string(i) + " live " + std::to_string(on) + ".." + std::to_string(off) +
                          ", move table " + std::to_string(h.on) + ".." + std::to_string(h.off));
    }
}

}  // namespace

const char* ease_name(Ease e) {
    switch (e) { case Ease::Linear: return "linear"; case Ease::In: return "in"; case Ease::Out: return "out"; case Ease::InOut: return "inout"; }
    return "?";
}

double ease_curve(Ease e, double u) {
    u = std::clamp(u, 0.0, 1.0);
    switch (e) {
        case Ease::Linear: return u;
        case Ease::In: return u * u;
        case Ease::Out: return 1 - (1 - u) * (1 - u);
        case Ease::InOut: return u * u * (3 - 2 * u);
    }
    return u;
}

double clip_time(const Retime& r, double f) {
    double c = 0, pos = 0;
    if (f <= 0) return f;
    for (const TimeScale& s : r.regions) {
        if (f <= s.start) return c + (f - pos);
        c += s.start - pos; pos = s.start;
        const double len = s.end - s.start;
        if (f < s.end) return c + s.value * len * ease_curve(s.ease, (f - s.start) / len);
        c += s.value * len; pos = s.end;
    }
    return c + (f - pos);
}

int sim_frame_at(const Retime& r, double clip_frame, int limit) {
    for (int f = 0; f <= limit; ++f) if (clip_time(r, f) >= clip_frame - EPS) return f;
    return limit + 1;
}

Retime fit_telegraph(int startup, int tell_on, const TellFit& fit) {
    Retime r;
    const int span = startup - tell_on;
    if (span < 2 || !(fit.strike > fit.windup)) return r;
    int k = tell_on + (int)std::lround(span * (fit.strike - 1) / (fit.strike - fit.windup));
    k = std::clamp(k, tell_on + 1, startup - 1);
    const double strike = (span - fit.windup * (k - tell_on)) / (startup - k);
    r.regions = {{fit.windup, tell_on, k, fit.ease}, {strike, k, startup, fit.ease}};
    return r;
}

int lead_need(const MarkerClip& c) {
    if (c.signature) return 36;
    return !c.parryable || !c.blockable ? 30 : 18;
}

RetimeReport lint_retime(const MarkerClip& c, const Retime& r, const LintBounds& b) {
    RetimeReport rep;
    rep.need = lead_need(c);
    const int limit = c.end + 1;
    rep.lead_flat = c.startup() - c.tell_on;
    rep.lead = sim_frame_at(r, c.startup(), limit) - sim_frame_at(r, c.tell_on, limit);
    if (rep.lead < rep.need)
        rep.issues.push_back(c.key + " lead: " + std::to_string(rep.lead) + " frames, need " + std::to_string(rep.need));
    if (std::fabs(clip_time(r, c.startup()) - c.startup()) > EPS)
        rep.issues.push_back(c.key + " lands: clip at " + fmt(clip_time(r, c.startup())) + " on the first active frame " + std::to_string(c.startup()));
    check_keyed(c, r, rep.issues);
    if (c.parryable && c.has_tell) check_shape(c, r, b, rep.issues);
    return rep;
}

}  // namespace raw::game::action
