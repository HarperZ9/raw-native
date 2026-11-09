// The held-time clock: see raw/scene/held_time.hpp.
#include "raw/scene/held_time.hpp"
#include "raw/core/sha256.hpp"
#include <stdexcept>
namespace raw::anim {
namespace {
// lowbias32 (C. Wellons, "Prospecting for Hash Functions", 2018), the mixer raw::noise uses.
std::uint32_t mix(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
void requirePositive(const Rational& r, const char* what) {
    if (r.num <= 0 || r.den <= 0) throw std::invalid_argument(std::string("held_time: non-positive ") + what);
}
// floor(a / b) for b > 0.
std::int64_t floorDiv(std::int64_t a, std::int64_t b) { const std::int64_t q = a / b; return (a % b != 0 && a < 0) ? q - 1 : q; }
}  // namespace

Rate continuous() { return {RateKind::Continuous, {0, 1}}; }
Rate onN(int n, Rational base) {
    if (n <= 0) throw std::invalid_argument("held_time: on N needs N >= 1");
    requirePositive(base, "base rate");
    return {RateKind::Held, {base.num, base.den * n}};
}
Rate named(std::int64_t num, std::int64_t den) {
    const Rate r{RateKind::Held, {num, den}};
    requirePositive(r.hz, "named rate");
    return r;
}
const char* rateName(const Rate& r) { return r.kind == RateKind::Continuous ? "continuous" : "held"; }

HeldSample sample(const Layer& layer, std::int64_t frame, Rational fps) {
    requirePositive(fps, "fps");
    if (layer.rate.kind == RateKind::Continuous) return {frame, double(frame) * double(fps.den) / double(fps.num)};
    const Rational& r = layer.rate.hz;
    requirePositive(r, "layer rate");
    const std::int64_t tick = floorDiv(frame * fps.den * r.num, fps.num * r.den);
    return {tick, double(tick) * double(r.den) / double(r.num)};
}

std::int64_t onsetFrame(const Rate& rate, std::int64_t tick, Rational fps) {
    requirePositive(fps, "fps");
    if (rate.kind == RateKind::Continuous) return tick;
    // smallest k with k * fps.den * num >= tick * fps.num * den
    const std::int64_t a = tick * fps.num * rate.hz.den, b = fps.den * rate.hz.num;
    return -floorDiv(-a, b);
}

std::uint64_t fnv64(const void* data, std::size_t len) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < len; ++i) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}

std::uint32_t boilSeed(const Layer& layer, std::uint64_t poseHash) {
    return mix(std::uint32_t(poseHash) ^ mix(std::uint32_t(poseHash >> 32) ^ mix(layer.seed)));
}

HoldStats holdStats(const std::vector<std::string>& digests) {
    HoldStats s;
    s.frames = int(digests.size());
    std::string chain;
    for (std::size_t i = 0; i < digests.size(); ++i) {
        if (i == 0 || digests[i] != digests[i - 1]) { ++s.changes; chain += digests[i]; }
    }
    s.heldRatio = s.frames ? double(s.frames - s.changes) / double(s.frames) : 0.0;
    s.sequenceSha = sha256Hex(chain);
    return s;
}

int subsetExceptions(const std::vector<bool>& poseChanged, const std::vector<bool>& seedChanged) {
    if (poseChanged.size() != seedChanged.size()) throw std::invalid_argument("held_time: flag sizes differ");
    int n = 0;
    for (std::size_t i = 0; i < poseChanged.size(); ++i) if (seedChanged[i] && !poseChanged[i]) ++n;
    return n;
}

}  // namespace raw::anim
