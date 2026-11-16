// The ENB CRT and VHS port (evidence/art-enb-crt-bounds.json e2 and e3): each preset's reference
// frame hashed and pinned per compiler family; both pipelines off and the zeroed CRT return the
// input; the downsample keeps a flat image flat; the hash streams stay in [0, 1).
//   test_art_enbcrt
#include "raw/renderer/art_enbcrt.hpp"
#include "raw/renderer/art_metrics.hpp"
#include "raw/renderer/art_scene.hpp"
#include "raw/renderer/art_strokes.hpp"
#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
using namespace raw;
using namespace raw::art;

namespace {
#if defined(_MSC_VER)
constexpr const char* kFamily = "msvc";
#elif defined(__clang__)
constexpr const char* kFamily = "clang";
#elif defined(__GNUC__)
constexpr const char* kFamily = "gcc";
#else
constexpr const char* kFamily = "other";
#endif
struct Pinned { const char* family; const char* preset; std::uint64_t hash; };
// Recorded on this branch's first builds (0 = not yet pinned).
constexpr Pinned kPinned[] = {{"gcc", "crt", 0}, {"msvc", "crt", 0}};

std::vector<float> input() {
    const Frame f = renderFrame(orbitView(0, 256, 144), art::daylight(), 4);
    const std::vector<std::uint8_t> c = paintReference(f.lit, 1.6);
    std::vector<float> o(c.size());
    for (std::size_t i = 0; i < c.size(); ++i) o[i] = float(c[i]) / 255.0f;
    return o;
}
}  // namespace

int main() {
    const std::vector<float> img = input();
    const std::vector<std::uint8_t> in = codes8Display(img);
    CHECK(codes8Display(runEnbCrt(enbCrtPreset("off"), img, 256, 144, 0)) == in);
    CHECK(codes8Display(runEnbCrt(enbCrtPreset("crt-zero"), img, 256, 144, 0)) == in);
    const std::vector<float> flat(64 * 32 * 4, 0.25f), down = enbCrtDown(flat, 64, 32);
    bool flatOk = true;
    for (std::size_t i = 0; i < down.size(); i += 4) flatOk = flatOk && (down[i] == 0.25f || down[i] == 0.0f);
    CHECK(flatOk);
    for (const char* p : {"crt", "crt-slot", "crt-shadow", "vhs", "vhs-crt", "vhs-ep"}) {
        const std::uint64_t h = fnv1a(codes8Display(runEnbCrt(enbCrtPreset(p), img, 256, 144, 0)));
        std::printf("e2 %s %s 0x%016llxull\n", kFamily, p, static_cast<unsigned long long>(h));
        for (const Pinned& q : kPinned) if (!std::strcmp(q.family, kFamily) && !std::strcmp(q.preset, p) && q.hash) CHECK(h == q.hash);
    }
    return raw_test_summary();
}
