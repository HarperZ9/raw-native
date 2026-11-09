// The closed-loop contract: see raw/scene/loop.hpp.
#include "raw/scene/loop.hpp"
#include <stdexcept>
namespace raw::anim {

double phase(std::int64_t frame, std::int64_t period) {
    if (period <= 0) return 0.0;
    const std::int64_t m = ((frame % period) + period) % period;
    return double(m) / double(period);
}

std::int64_t ticksPerLoop(const Rate& rate, const LoopSpec& spec) {
    if (rate.kind == RateKind::Continuous) return spec.frames;
    // N frames last N * fps.den / fps.num seconds; ticks = that * num / den.
    const std::int64_t a = spec.frames * spec.fps.den * rate.hz.num, b = spec.fps.num * rate.hz.den;
    return (b > 0 && a % b == 0) ? a / b : -1;
}

LayerTerm layerTerm(const Layer& layer, const LoopSpec& spec, std::int64_t clipTicks) {
    LayerTerm t{{layer.name, 0}, false, {}};
    const std::int64_t ticks = ticksPerLoop(layer.rate, spec);
    if (ticks < 0) { t.why = "ticks a loop are not whole"; return t; }
    if (clipTicks > 0 && ticks % clipTicks != 0) { t.why = "clip length does not divide the ticks a loop"; return t; }
    t.term.period = spec.frames;
    t.ok = true;
    return t;
}

LoopCheck checkLoop(const LoopSpec& spec, const std::vector<LoopTerm>& terms) {
    if (spec.frames <= 0 || spec.fps.num <= 0 || spec.fps.den <= 0) throw std::invalid_argument("loop: bad spec");
    LoopCheck c;
    for (const LoopTerm& t : terms) {
        if (t.period == 0) continue;
        if (t.period < 0 || spec.frames % t.period != 0) {
            c.ok = false;
            c.refused.push_back(t.name + ": period " + std::to_string(t.period) + " does not divide " + std::to_string(spec.frames));
        }
    }
    return c;
}

}  // namespace raw::anim
