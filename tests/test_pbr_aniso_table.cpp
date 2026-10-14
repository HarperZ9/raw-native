// The committed anisotropic split-albedo table (method note 4 of 2026-10-12 in
// evidence/m3-materials2-bounds.json): the bytes the library embeds hash to the committed
// .sha256, and a seeded sample of entries regenerated with the generator's code
// (Tables::anisoEntry at kAnSamples) matches them within 1e-6. Controls: one flipped bit
// fails the hash, and entries regenerated at 1,024 samples miss the tolerance.
#include "raw/renderer/pbr.hpp"
#include "raw/core/sha256.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
using raw::pbr::Tables;

namespace {
std::uint64_t next(std::uint64_t& s) {   // splitmix64
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
float entry(const unsigned char* bytes, std::size_t at) { float f; std::memcpy(&f, bytes + at * sizeof(float), sizeof f); return f; }
}  // namespace

int main() {
    constexpr int kSampled = 64;
    constexpr double kTol = 1e-6;
    constexpr std::uint64_t kSeed = 20261012;
    const unsigned char* bytes = Tables::anisoBytes();
    const std::size_t count = Tables::anisoByteCount();
    CHECK(count == 2 * Tables::kAnAll * sizeof(float));
    if (count != 2 * Tables::kAnAll * sizeof(float)) return raw_test_summary();

    std::string committed;
    {
        std::ifstream h(std::string(RAW_SOURCE_DIR) + "/src/renderer/data/aniso_albedo.f32.sha256");
        h >> committed;
    }
    const std::string hash = raw::sha256Hex(bytes, count);
    CHECK(committed.size() == 64 && hash == committed);
    std::vector<unsigned char> flipped(bytes, bytes + count);
    flipped[count / 3] ^= 0x01;
    const bool controlHash = raw::sha256Hex(flipped.data(), flipped.size()) != committed;
    CHECK(controlHash);

    double worst = 0.0, controlWorst = 0.0;
    std::uint64_t s = kSeed;
    for (int n = 0; n < kSampled; ++n) {
        const std::size_t at = std::size_t(next(s) % Tables::kAnAll);
        const int im = int(at % Tables::kAnMu), ip = int(at / Tables::kAnMu % Tables::kAnPhi);
        const int ir = int(at / (std::size_t(Tables::kAnMu) * Tables::kAnPhi) % Tables::kAnRough);
        const int ik = int(at / (std::size_t(Tables::kAnMu) * Tables::kAnPhi * Tables::kAnRough));
        double a, b, ca, cb;
        Tables::anisoEntry(ik, ir, ip, im, Tables::kAnSamples, a, b);
        Tables::anisoEntry(ik, ir, ip, im, 1 << 10, ca, cb);
        const double ea = entry(bytes, at), eb = entry(bytes, Tables::kAnAll + at);
        worst = std::max({worst, std::fabs(ea - a), std::fabs(eb - b)});
        controlWorst = std::max({controlWorst, std::fabs(ea - ca), std::fabs(eb - cb)});
    }
    CHECK(worst <= kTol);
    CHECK(controlWorst > kTol);
    std::printf("{\"check\": \"pbr aniso table\", \"bytes\": %zu, \"sha256\": \"%s\", \"hash_matches\": %s, \"sampled\": %d, \"seed\": %llu,\n"
                " \"worst_abs\": %.3e, \"bound\": %.0e, \"control_flipped_bit_fails\": %s, \"control_1024_samples_worst\": %.3e}\n",
                count, hash.c_str(), hash == committed ? "true" : "false", kSampled, (unsigned long long)kSeed, worst, kTol,
                controlHash ? "true" : "false", controlWorst);
    return raw_test_summary();
}
