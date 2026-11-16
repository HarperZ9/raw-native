// The stylised track's shared camera sequence: see raw/renderer/art_orbit.hpp.
#include "raw/renderer/art_orbit.hpp"
namespace raw::art {

OrbitSeq renderOrbitSeq(int w, int h, int frames, double step, const Lighting& L, int threads) {
    OrbitSeq s;
    for (int t = 0; t < frames; ++t) {
        s.views.push_back(orbitView(t, w, h, step));
        s.frames.push_back(renderFrame(s.views.back(), L, threads));
        if (t == 0) { s.mv.emplace_back(std::size_t(w) * std::size_t(h) * 2, 0.0f); s.valid.emplace_back(); continue; }
        s.mv.push_back(motionTo(s.frames[std::size_t(t)], s.views[std::size_t(t - 1)]));
        s.valid.push_back(historyValid(s.frames[std::size_t(t)], s.frames[std::size_t(t - 1)], s.views[std::size_t(t - 1)], s.mv.back()));
    }
    return s;
}

std::vector<double> instabilitySeries(const OrbitSeq& s, const std::vector<std::vector<std::uint8_t>>& out,
                                      const std::vector<std::vector<std::uint8_t>>& ref, int first) {
    std::vector<double> v;
    const int w = s.frames[0].w, h = s.frames[0].h;
    for (std::size_t t = std::size_t(first); t < out.size(); ++t)
        v.push_back(instability(out[t], out[t - 1], ref[t], ref[t - 1], s.mv[t], s.valid[t], w, h));
    return v;
}

}  // namespace raw::art
