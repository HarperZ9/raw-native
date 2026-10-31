// The bytes of every owned scene without v3 fields, per compiler family, since the C runtimes' sin
// and cos differ in the last bit between them. Check c1 of evidence/rt-v3-bounds.json held these
// to their v2 bytes from v3 until interface v4 moved them on purpose (docs/rt-interface-v4.md: the
// energy-conserving lobe and the watertight triangle test); the pins now hold them to v4's bytes.
// A compiler with no recorded hash prints its hashes and passes only the parts it can check
// (determinism across threads).
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
// Recorded 2026-10-31 for interface v4: GCC 13.3 (Ubuntu 24.04) and MSVC 19.5x (VS 18). Earlier pins are in git (c819792's).
constexpr Pinned kPinned[] = {
    {"gcc", "retro_room", 0x594583490a5ff0f8ull}, {"gcc", "iso_street", 0x643b84aad166f7d8ull}, {"gcc", "dense_block", 0xcfdbe2beec95b76eull}, {"gcc", "furnace", 0x7e4b3ac30ea7d62cull},
    {"msvc", "retro_room", 0xa1881e9b26a766d4ull}, {"msvc", "iso_street", 0xc2e7a375212775fdull}, {"msvc", "dense_block", 0x8d186dfd04451a39ull}, {"msvc", "furnace", 0xbaaa2daf4dce42acull},
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
    std::printf("hashes checked against the v4 record: %d of %zu (compiler family %s)\n", checked, cases.size(), kFamily);
    return raw_test_summary();
}
