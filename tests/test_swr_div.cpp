// R1 method note 3 (evidence/rt-r1-bounds.json): div_rn, the correctly rounded binary32 division
// of src/renderer/gpu/shaders/swr.wgsl, mirrored line for line in u32 arithmetic and checked
// against the C++ division (IEEE 754, round to nearest even) on random and edge operands.
//   test_swr_div [out.json]
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace {
std::uint32_t bitsOf(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
float floatOf(std::uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

// The mirror of swr.wgsl's div_rn (one exit, no branches). dropSticky is the control.
float divRn(float a, float b, bool dropSticky = false) {
    const std::uint32_t ua = bitsOf(a), ub = bitsOf(b);
    const std::uint32_t sgn = (ua ^ ub) & 0x80000000u;
    const std::uint32_t ea = (ua >> 23) & 255u, eb = (ub >> 23) & 255u, fa = ua & 0x7fffffu, fb = ub & 0x7fffffu;
    const bool isNan = (ea == 255u && fa != 0u) || (eb == 255u && fb != 0u) || (ea == 255u && eb == 255u) || (ea == 0u && fa == 0u && eb == 0u && fb == 0u);
    const bool isInf = !isNan && (ea == 255u || (eb == 0u && fb == 0u));
    const bool isZero = !isNan && !isInf && ((ea == 0u && fa == 0u) || eb == 255u);
    std::uint32_t ma = ea == 0u ? fa : (fa | 0x800000u), mb = eb == 0u ? fb : (fb | 0x800000u);
    std::int32_t xa = ea == 0u ? 1 : std::int32_t(ea), xb = eb == 0u ? 1 : std::int32_t(eb);
    for (std::uint32_t g = 0; g < 24u; ++g) {
        const bool na = ma != 0u && ma < 0x800000u;
        ma = na ? ma << 1 : ma; xa = na ? xa - 1 : xa;
        const bool nb = mb != 0u && mb < 0x800000u;
        mb = nb ? mb << 1 : mb; xb = nb ? xb - 1 : xb;
    }
    const bool lt = ma < mb;
    ma = lt ? ma << 1 : ma;
    const std::int32_t e = lt ? xa - xb + 126 : xa - xb + 127;
    std::uint32_t q = 0u, r = ma;
    for (std::uint32_t k = 0; k < 25u; ++k) {
        const bool ge = r >= mb;
        q = (q << 1) | (ge ? 1u : 0u);
        r = (ge ? r - mb : r) << 1;
    }
    const std::uint32_t sh = std::uint32_t(std::clamp(1 - e, 0, 26));
    const bool sticky = !dropSticky && (r != 0u || (q & ((1u << sh) - 1u)) != 0u);
    q >>= sh;
    const std::uint32_t biased = e <= 0 ? 0u : std::uint32_t(std::max(e, 1));
    const bool rnd = (q & 1u) != 0u;
    q >>= 1;
    q += (rnd && (sticky || (q & 1u) != 0u)) ? 1u : 0u;
    std::uint32_t bits = biased == 0u ? q : ((biased - 1u) << 23) + q;
    bits = e >= 255 ? 0x7f800000u : bits;
    bits = sgn | bits;
    bits = isInf ? (sgn | 0x7f800000u) : bits;
    bits = isZero ? sgn : bits;
    bits = isNan ? (ua | 0x7fc00000u) : bits;
    return floatOf(bits);
}
struct Rng { std::uint64_t s; std::uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return std::uint32_t(s >> 16); } };
bool same(float x, float y) { return (std::isnan(x) && std::isnan(y)) || bitsOf(x) == bitsOf(y); }
}  // namespace

int main(int argc, char** argv) {
    std::vector<float> edge;
    for (std::uint32_t u : {0u, 1u, 2u, 3u, 0x7fffffu, 0x400000u, 0x800000u, 0x800001u, 0x7f7fffffu, 0x7f800000u, 0x7fc00000u,
                            0x3f800000u, 0x3f800001u, 0x3f7fffffu, 0x40000000u, 0x3fc00000u, 0x40400000u, 0x34000000u, 0x00ffffffu,
                            0x01000000u, 0x0b800000u, 0x73800000u, 0x7e800000u, 0x00000400u, 0x3eaaaaabu, 0x42f60000u})
        for (std::uint32_t s : {0u, 0x80000000u}) edge.push_back(floatOf(u | s));
    long pairs = 0, diffs = 0, ctlDiffs = 0;
    for (float a : edge) for (float b : edge) {
        ++pairs;
        diffs += !same(divRn(a, b), a / b);
        ctlDiffs += !same(divRn(a, b, true), a / b);
    }
    Rng r{0x5eed1234abcdULL};
    const long kRandom = 1L << 26;
    long randDiffs = 0;
    for (long k = 0; k < kRandom; ++k) {
        float a = floatOf(r.next()), b = floatOf(r.next());
        // Half the pairs from nearby exponents, where quotients land on every rounding case.
        if (k & 1) b = floatOf((bitsOf(b) & 0x807fffffu) | (bitsOf(a) & 0x7f800000u));
        const float g = divRn(a, b), w = a / b;
        if (!same(g, w)) {
            if (randDiffs < 8) std::printf("  differs: %08x / %08x -> %08x, want %08x\n", bitsOf(a), bitsOf(b), bitsOf(g), bitsOf(w));
            ++randDiffs;
        }
        if (!(k & 0xff)) ctlDiffs += !same(divRn(a, b, true), w);
    }
    std::printf("div_rn mirror: edge pairs %ld, differing %ld; random pairs %ld, differing %ld; control (no sticky) differing %ld\n",
                pairs, diffs, kRandom, randDiffs, ctlDiffs);
    CHECK(diffs == 0 && randDiffs == 0);
    CHECK(ctlDiffs > 0);
    if (argc > 1)
        std::ofstream(argv[1]) << "{\n \"schema\": \"raw-native.evidence/1\",\n \"criterion\": \"RT R1 method note 3: div_rn mirror against C++ division\",\n"
                               << " \"edge_pairs\": " << pairs << ", \"edge_differing\": " << diffs << ", \"random_pairs\": " << kRandom
                               << ", \"random_differing\": " << randDiffs << ", \"control_no_sticky_differing\": " << ctlDiffs
                               << ",\n \"pass\": " << (raw_test_failures() == 0 ? "true" : "false") << "\n}\n";
    return raw_test_summary();
}
