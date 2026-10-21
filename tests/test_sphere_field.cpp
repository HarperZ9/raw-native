// The sphere field (evidence/p10-sphere-field-bounds.json): f1 coverage of every look and the
// ground, and f2 the CPU frame's hash pinned per compiler family (recorded on this branch's first
// build). A compiler with no recorded hash prints its hash and checks the rest.
//   test_sphere_field
#include "raw/renderer/gallery.hpp"
#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace raw;

namespace {
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
struct Pinned { const char* family; std::uint64_t hash; };
constexpr Pinned kPinned[] = {{"gcc", 0x5eb27f10c75144ecull}, {"msvc", 0x89b2e8cef8c18189ull}};   // recorded 2026-10-21: GCC 13.3, MSVC (VS 18)
}  // namespace

int main() {
    // f1 at 256 x 160, 2 x 2 samples a pixel: fully covered pixels per group.
    const gallery::Gallery big = gallery::buildField(256, 160, 2);
    std::vector<int> pixelGroup(std::size_t(256) * 160, -2);
    for (std::size_t q = 0; q < big.samples.size(); ++q) {
        int& pg = pixelGroup[std::size_t(big.pixel[q])];
        pg = pg == -2 ? big.group[q] : (pg == big.group[q] ? pg : -1);
    }
    std::vector<int> full(big.groupLabels.size(), 0);
    for (std::size_t i = 0; i < pixelGroup.size(); ++i) if (pixelGroup[i] >= 0 && big.hits[i] == 4) ++full[std::size_t(pixelGroup[i])];
    CHECK(big.groupLabels.size() == 13);
    for (std::size_t k = 0; k < full.size(); ++k) {
        std::printf("f1 %-28s %d fully covered pixels\n", big.groupLabels[k].c_str(), full[k]);
        CHECK(full[k] >= 100);
    }
    // f2 at 96 x 60, 1 sample a pixel.
    const gallery::Gallery g = gallery::buildField(96, 60, 1);
    const pbr::Tables tables;
    lighting::Cube pre = g.env;
    lighting::prefilter(pre, lighting::kPrefilterSamples);
    const std::vector<lighting::Rgb> img = gallery::resolve(g, gallery::shadeCpu(g, tables, pre, 2));
    std::uint64_t h = 1469598103934665603ull;
    for (const lighting::Rgb& c : img) for (double v : {c.r, c.g, c.b}) {
        const unsigned char* q = reinterpret_cast<const unsigned char*>(&v);
        for (std::size_t k = 0; k < sizeof v; ++k) { h ^= q[k]; h *= 1099511628211ull; }
    }
    std::printf("f2 %s frame hash 0x%016llxull\n", kFamily, static_cast<unsigned long long>(h));
    int checked = 0;
    for (const Pinned& p : kPinned) if (!std::strcmp(p.family, kFamily) && p.hash) { CHECK(h == p.hash); ++checked; }
    std::printf("f2 checked against the record: %d\n", checked);
    return raw_test_summary();
}
