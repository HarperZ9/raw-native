// Look keys: see raw/renderer/look_keys.hpp. Operations are in the order of web/look/keys.mjs.
#include "raw/renderer/look_keys.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace raw::style {
namespace {
double mod24(double h) { return std::fmod(std::fmod(h, 24.0) + 24.0, 24.0); }

// Fritsch-Carlson tangents for periodic data y at the slot hours, one component.
std::vector<double> tangents(const std::vector<Slot>& slots, const std::vector<double>& y) {
    const std::size_t n = y.size();
    std::vector<double> d(n), m(n);
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t j = (k + 1) % n;
        const double span = slots[j].hour + (j <= k ? 24 : 0) - slots[k].hour;
        d[k] = (y[j] - y[k]) / span;
    }
    for (std::size_t k = 0; k < n; ++k) {
        const double p = d[(k + n - 1) % n], q = d[k];
        if (p * q <= 0) { m[k] = 0; continue; }
        const double wp = slots[k].hour - slots[(k + n - 1) % n].hour + (k == 0 ? 24 : 0);
        const double wq = slots[(k + 1) % n].hour + ((k + 1) % n <= k ? 24 : 0) - slots[k].hour;
        const double w1 = 2 * wq + wp, w2 = wq + 2 * wp;
        m[k] = (w1 + w2) / (w1 / p + w2 / q);
    }
    // keep each segment inside the monotone region
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t j = (k + 1) % n;
        if (d[k] == 0) { m[k] = 0; m[j] = 0; continue; }
        const double a = m[k] / d[k], b = m[j] / d[k], s = a * a + b * b;
        if (s > 9) { const double tau = 3 / std::sqrt(s); m[k] = tau * a * d[k]; m[j] = tau * b * d[k]; }
    }
    return m;
}
double hermite(double y0, double y1, double m0, double m1, double span, double t) {
    const double t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * span * m0 + (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * span * m1;
}
}  // namespace

const std::vector<Slot>& slots8() {
    static const std::vector<Slot> s{{"midnight", 0}, {"predawn", 4}, {"dawn", 6}, {"morning", 9},
                                     {"noon", 12.5}, {"golden", 17}, {"dusk", 19.5}, {"night", 22}};
    return s;
}
const std::vector<Slot>& slots6() {
    static const std::vector<Slot> s{{"night", 0}, {"dawn", 5.5}, {"morning", 9.5}, {"noon", 13}, {"dusk", 18.5}, {"late", 21.5}};
    return s;
}
const std::vector<Slot>& checkSlots(const std::vector<Slot>& s) {
    if (int(s.size()) < kMinSlots)
        throw std::invalid_argument("look keys: " + std::to_string(s.size()) + " slots is too few; use at least 6 (8 by default)");
    double prev = -std::numeric_limits<double>::infinity();
    for (const Slot& x : s) {
        if (!(x.hour >= 0 && x.hour < 24) || !(x.hour > prev))
            throw std::invalid_argument("look keys: slot hours must increase inside [0, 24); " + x.name + " breaks that");
        prev = x.hour;
    }
    return s;
}

Located locate(const std::vector<Slot>& slots, double hour) {
    const double h = mod24(hour);
    const int n = int(slots.size());
    int i = n - 1;
    for (int k = 0; k < n; ++k) if (slots[std::size_t(k)].hour <= h) i = k;
    const int j = (i + 1) % n;
    const double a = slots[std::size_t(i)].hour, b = slots[std::size_t(j)].hour + (j <= i ? 24 : 0), x = h < a ? h + 24 : h;
    return {i, j, (x - a) / (b - a), b - a};
}

LookKeys::LookKeys(std::vector<Slot> slots, const std::map<std::string, TermSpec>& terms) : slots_(std::move(slots)) {
    checkSlots(slots_);
    for (const auto& [name, spec] : terms) {
        if (spec.values.size() != slots_.size())
            throw std::invalid_argument("look keys: " + name + " needs " + std::to_string(slots_.size()) + " values, one per slot");
        const std::size_t dim = spec.values[0].size();
        for (const auto& v : spec.values) {
            if (v.size() != dim || dim == 0) throw std::invalid_argument("look keys: " + name + " has a bad value");
            for (double x : v) if (!std::isfinite(x)) throw std::invalid_argument("look keys: " + name + " has a bad value");
        }
        Series s;
        s.min = spec.min; s.max = spec.max;
        for (std::size_t c = 0; c < dim; ++c) {
            std::vector<double> y;
            for (const auto& v : spec.values) y.push_back(v[c]);
            s.tans.push_back(tangents(slots_, y));
            s.comps.push_back(std::move(y));
        }
        series_.emplace(name, std::move(s));
    }
}
std::vector<std::string> LookKeys::names() const {
    std::vector<std::string> n;
    for (const auto& kv : series_) n.push_back(kv.first);
    return n;
}
std::vector<double> LookKeys::term(const std::string& name, double hour) const {
    const auto it = series_.find(name);
    if (it == series_.end()) throw std::invalid_argument("look keys: unknown term " + name);
    const Series& s = it->second;
    const Located L = locate(slots_, hour);
    std::vector<double> out;
    for (std::size_t c = 0; c < s.comps.size(); ++c) {
        const auto& y = s.comps[c];
        const auto& m = s.tans[c];
        double v = L.t == 0 ? y[std::size_t(L.i)] : hermite(y[std::size_t(L.i)], y[std::size_t(L.j)], m[std::size_t(L.i)], m[std::size_t(L.j)], L.span, L.t);
        v = std::min(s.max.value_or(std::numeric_limits<double>::infinity()), std::max(s.min.value_or(-std::numeric_limits<double>::infinity()), v));
        out.push_back(v);
    }
    return out;
}

}  // namespace raw::style
