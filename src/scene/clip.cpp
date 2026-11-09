// Keyed channel clips: see raw/scene/clip.hpp. Operation order follows web/anim/clip.mjs.
#include "raw/scene/clip.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace raw::anim {

double monotoneCubic(const std::vector<double>& ts, const std::vector<double>& vs, double t) {
    const std::size_t n = ts.size();
    if (n == 1 || t <= ts[0]) return vs[0];
    if (t >= ts[n - 1]) return vs[n - 1];
    std::size_t i = 0;
    while (t > ts[i + 1]) ++i;
    auto d = [&](std::size_t k) { return (vs[k + 1] - vs[k]) / std::max(1e-9, ts[k + 1] - ts[k]); };
    auto tan = [&](std::size_t k) {
        if (k == 0) return d(0);
        if (k == n - 1) return d(n - 2);
        const double a = d(k - 1), b = d(k);
        return a * b <= 0 ? 0.0 : (2 * a * b) / (a + b);
    };
    const double h = ts[i + 1] - ts[i], s = (t - ts[i]) / h, m0 = tan(i) * h, m1 = tan(i + 1) * h, s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * vs[i] + (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) * vs[i + 1] + (s3 - s2) * m1;
}

std::vector<double> sampleClip(const Clip& clip, double t) {
    if (clip.keys.empty() || !(clip.length > 0)) throw std::invalid_argument("clip: no keys or no length");
    std::vector<double> ts;
    for (const ClipKey& k : clip.keys) ts.push_back(k.t);
    std::vector<const ClipKey*> keys;
    for (const ClipKey& k : clip.keys) keys.push_back(&k);
    if (clip.loop) {
        t = std::fmod(std::fmod(t, clip.length) + clip.length, clip.length);
        ts.push_back(clip.length);
        keys.push_back(&clip.keys.front());
    }
    const std::size_t channels = clip.keys.front().v.size();
    std::vector<double> out(channels), vs(keys.size());
    for (std::size_t c = 0; c < channels; ++c) {
        for (std::size_t i = 0; i < keys.size(); ++i) vs[i] = keys[i]->v.at(c);
        out[c] = monotoneCubic(ts, vs, t);
    }
    return out;
}

}  // namespace raw::anim
