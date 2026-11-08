#pragma once
// The held-time clock (PROPOSALS P1 of the 2026-10-29 game-art study; evidence/heldtime-loop-bounds.json).
// One clock samples animation, surface noise and effects at an authored rate per layer, independent
// of the render rate: on ones, twos or threes of a 24 fps film base, a named rate such as 12 Hz, or
// continuous (every render frame, which is where the camera sits by default).
//
// Render frame k at fps R shows tick floor(k * R.den * rate.num / (R.num * rate.den)), computed in
// 64-bit integers, so 30, 60 and 144 fps show the same ticks in the same order and each tick arrives
// on the first frame whose time reaches it. The held time is tick / rate.
//
// A layer's boil seed is a hash of the layer's seed and its pose buffer's bytes, so the seed changes
// only when the pose changes. A replacement-frame swap is a change of the pose buffer (put the
// replacement index in it), so boil and swaps come from one rule.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::anim {

struct Rational { std::int64_t num{24}, den{1}; };   // per second; den > 0
enum class RateKind : std::uint8_t { Continuous, Held };
struct Rate { RateKind kind{RateKind::Held}; Rational hz{24, 1}; };

Rate continuous();                                    // every render frame
Rate onN(int n, Rational base = {24, 1});             // base / n: ones, twos, threes
Rate named(std::int64_t num, std::int64_t den = 1);   // e.g. named(12) for 12 Hz
const char* rateName(const Rate& r);                  // "continuous", "held"

struct Layer { std::string name; Rate rate; std::uint32_t seed{0}; };
struct HeldSample {
    std::int64_t tick{0};   // continuous: the render frame
    double time{0};         // seconds: tick / rate, or frame / fps when continuous
};
// Refuses (throws std::invalid_argument) a zero or negative rate or fps.
HeldSample sample(const Layer& layer, std::int64_t frame, Rational fps);
// The first render frame that shows tick n: ceil(n * fps / rate).
std::int64_t onsetFrame(const Rate& rate, std::int64_t tick, Rational fps);

std::uint64_t fnv64(const void* data, std::size_t len);
std::uint32_t boilSeed(const Layer& layer, std::uint64_t poseHash);

// Per-frame measures over one layer's pose buffers.
struct HoldStats {
    int frames{0}, changes{0};    // frame 0 counts as a change
    double heldRatio{0};          // (frames - changes) / frames
    std::string sequenceSha;      // SHA-256 over the pose digests with consecutive repeats removed
};
// digests: one 32-byte digest (any fixed width) a frame, as bytes.
HoldStats holdStats(const std::vector<std::string>& digests);
// Frames where `seedChanged` is true and `poseChanged` is false. Sizes must match.
int subsetExceptions(const std::vector<bool>& poseChanged, const std::vector<bool>& seedChanged);

}  // namespace raw::anim
