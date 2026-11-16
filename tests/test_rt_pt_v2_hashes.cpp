// Path tracer interface v3's compatibility promise (docs/rt-interface-v3.md, check c1 of
// evidence/rt-v3-bounds.json): every v2 scene renders byte for byte as it did under v2. The
// hashes below were recorded on main before the v3 code (c819792), per compiler family, since
// the C runtimes' sin and cos differ in the last bit between them. A compiler with no recorded
// hash prints its hashes and passes only the parts it can check (determinism across threads).
//   test_rt_pt_v2_hashes
#include "raw/renderer/rt_pathtrace.hpp"
#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace raw::rt;

namespace {
std::uint64_t fnv(std::uint64_t h, const void* p, std::size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
template <class T> std::uint64_t fnvv(std::uint64_t h, const std::vector<T>& v) { return fnv(h, v.data(), v.size() * sizeof(T)); }
std::uint64_t hashOf(const PathTraceOutput& o) {
    std::uint64_t h = 1469598103934665603ull;
    for (const auto* v : {&o.radiance, &o.albedo, &o.normal, &o.depth, &o.variance, &o.motion, &o.diffuse, &o.specular,
                          &o.emission, &o.diffuseAlbedo, &o.specularAlbedo, &o.diffuseVariance, &o.specularVariance}) h = fnvv(h, *v);
    return fnvv(h, o.triangle);
}
PathTraceOutput render(const PtScene& s, PtCamera cam, int threads) {
    PathTraceDesc d;
    d.width = 48; d.height = 32; d.spp = 4; d.maxBounces = 4; d.seed = 7;
    d.camera = d.previous = cam;
    return pathTraceCpu(s, d, threads);
}
#if defined(_MSC_VER)
constexpr const char* kFamily = "msvc";
#elif defined(__EMSCRIPTEN__)
constexpr const char* kFamily = "emscripten";
#elif defined(__clang__)
constexpr const char* kFamily = "clang";
#elif defined(__GNUC__)
constexpr const char* kFamily = "gcc";
#else
constexpr const char* kFamily = "other";
#endif
struct Pinned { const char* family; const char* scene; std::uint64_t hash; };
// Recorded on main c819792 before any v3 code (2026-10-21): GCC 13.3 (Ubuntu 24.04) and MSVC 19.5x (VS 18).
constexpr Pinned kPinned[] = {
    {"gcc", "retro_room", 0x1dbddfac1787b0b7ull}, {"gcc", "iso_street", 0x5bf3adf36f36aa40ull}, {"gcc", "dense_block", 0xb1107bf96a6627beull}, {"gcc", "furnace", 0x6bdde867ecf6767dull},
    {"msvc", "retro_room", 0x0ull}, {"msvc", "iso_street", 0x0ull}, {"msvc", "dense_block", 0x0ull}, {"msvc", "furnace", 0x0ull},
};
}  // namespace

int main() {
    struct Case { const char* name; PtScene s; PtCamera cam; };
    std::vector<Case> cases;
    cases.push_back({"retro_room", ptRetroRoom(), {}});
    cases.push_back({"iso_street", ptIsoStreet(), {}});
    cases.push_back({"dense_block", ptDenseBlock(), {}});
    cases.push_back({"furnace", ptFurnace(1.0f, 0.5f), {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 1.2f}});
    cases[0].cam = {{0.0f, 1.6f, -1.0f}, {0.0f, 1.4f, -8.0f}, {0.0f, 1.0f, 0.0f}, 1.1f};
    cases[1].cam = {{6.0f, 6.0f, 6.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.9f};
    cases[2].cam = {{0.0f, 3.0f, 9.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f};
    int checked = 0;
    for (const Case& c : cases) {
        const PathTraceOutput a = render(c.s, c.cam, 1), b = render(c.s, c.cam, 3);
        CHECK(a.error.empty());
        CHECK(a.version == kPathTraceVersion);
        const std::uint64_t ha = hashOf(a), hb = hashOf(b);
        CHECK(ha == hb);   // any thread count, the same bytes
        std::printf("%s %s 0x%016llxull\n", kFamily, c.name, static_cast<unsigned long long>(ha));
        for (const Pinned& p : kPinned)
            if (!std::strcmp(p.family, kFamily) && !std::strcmp(p.scene, c.name) && p.hash != 0) { CHECK(ha == p.hash); ++checked; }
    }
    std::printf("v2 hashes checked against the pre-v3 record: %d of %zu (compiler family %s)\n", checked, cases.size(), kFamily);
    return raw_test_summary();
}
