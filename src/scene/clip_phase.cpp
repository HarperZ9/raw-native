// Phase-timed clips: see raw/scene/clip_phase.hpp. Operation order follows web/anim/clip.mjs.
#include "raw/scene/clip_phase.hpp"
#include <stdexcept>
namespace raw::anim {

const PhaseSpan* Layout::find(const std::string& phase) const {
    for (const PhaseSpan& s : starts) if (s.phase == phase) return &s;
    return nullptr;
}

Layout layout(const std::vector<std::pair<std::string, double>>& phases, double frames) {
    Layout L;
    if (phases.empty()) {
        if (!(frames > 0)) throw std::invalid_argument("clip: no length");
        L.length = frames;
        L.starts.push_back({"all", 0, frames});
        return L;
    }
    double f = 0;
    for (const auto& [ph, n] : phases) {
        if (!(n >= 0)) throw std::invalid_argument("clip: no length for phase " + ph);
        L.starts.push_back({ph, f, n});
        f += n;
    }
    L.length = f;
    return L;
}

double keyTime(const std::string& phase, double u, const Layout& L) {
    if (phase.empty()) return u * L.length;
    const PhaseSpan* s = L.find(phase);
    if (!s) throw std::invalid_argument("clip: unknown phase " + phase);
    return s->from + u * s->n;
}

PhaseAt phaseAt(const Layout& L, double f) {
    PhaseAt out;
    for (const PhaseSpan& s : L.starts) if (f >= s.from && f < s.from + s.n) out.phase = s.phase;
    out.done = f >= L.length;
    return out;
}

double warp(const Layout& from, const Layout& to, double f) {
    std::vector<double> a{0}, b{0};
    for (const PhaseSpan& p : from.starts) {
        const PhaseSpan* q = to.find(p.phase);
        if (!q) continue;
        a.push_back(p.from + p.n);
        b.push_back(q->from + q->n);
    }
    if (a.size() == 1) return (f / from.length) * to.length;
    if (a.back() < from.length) { a.push_back(from.length); b.push_back(to.length); }
    if (f <= 0) return 0;
    if (f >= a.back()) return b.back();
    std::size_t i = 0;
    while (f > a[i + 1]) ++i;
    const double span = a[i + 1] - a[i];
    return span > 0 ? b[i] + ((b[i + 1] - b[i]) * (f - a[i])) / span : b[i + 1];
}

}  // namespace raw::anim
