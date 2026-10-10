// Owned scenes for RT stage R2: see raw/renderer/rt_scenes.hpp.
#include "raw/renderer/rt_scenes.hpp"
#include "raw/renderer/swr_scenes.hpp"
#include "swr_builder.hpp"
namespace raw::rt {
namespace {
std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
float unit(std::uint32_t h) { return float(h >> 8) / 16777216.0f; }
}  // namespace

swr::Scene denseBlock() {
    swr::Scene s;
    s.name = "dense_block";
    s.tex = swr::proceduralTextures();
    swr::Builder b{s.geo};
    constexpr int N = 36;
    constexpr float lot = 5.0f;
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const std::uint32_t h = hash(std::uint32_t(j * N + i) * 2654435761u + 17u);
            const float x0 = float(i - N / 2) * lot, z0 = float(j - N / 2) * lot;
            b.quad({x0, 0, z0 + lot}, {x0 + lot, 0, z0 + lot}, {x0 + lot, 0, z0}, {x0, 0, z0}, 2, 2, swr::kStone);
            const float height = 2.0f + 12.0f * unit(h);
            const std::uint32_t wall = (h & 1u) ? swr::kBrick : swr::kPlaster;
            b.box({x0 + 1, 0, z0 + 1}, {x0 + 4, height, z0 + 4}, wall, 1.5f);
            for (float y = 3.0f; y + 0.5f < height; y += 3.0f)                        // ledges every floor
                b.box({x0 + 0.85f, y, z0 + 0.85f}, {x0 + 4.15f, y + 0.2f, z0 + 4.15f}, swr::kStone, 0.5f);
            if (h & 2u) b.cylinder({x0 + 2.5f, height, z0 + 2.5f}, 0.6f, 1.2f, 16, swr::kBrass);   // a roof tank
            b.box({x0 + 4.2f, 0, z0 + 0.2f}, {x0 + 4.7f, 0.5f, z0 + 0.7f}, swr::kPlanks, 0.5f);   // crates on the street
            b.box({x0 + 0.2f, 0, z0 + 4.2f}, {x0 + 0.6f, 0.4f, z0 + 4.6f}, swr::kPlanks, 0.5f);
        }
    s.eye = {58, 40, 62}; s.target = {0, 2, 0}; s.fovy = 0.7f; s.nearZ = 0.5f; s.farZ = 400.0f;
    s.light = {-0.4f, 0.85f, 0.35f};
    return s;
}

swr::Scene furnaceBox() {
    swr::Scene s;
    s.name = "furnace_box";
    std::vector<std::uint32_t> white(16, 0xffffffffu);
    s.tex.add(4, 4, white);
    swr::Builder b{s.geo};
    b.box({-1, -1, -1}, {1, 1, 1}, 0, 1.0f);
    for (Vec3& n : s.geo.nrm) n = n * -1.0f;                                          // seen from inside
    s.eye = {0.2f, -0.1f, 0.3f}; s.target = {-0.5f, 0.3f, -1.0f}; s.fovy = 1.3f; s.nearZ = 0.01f; s.farZ = 10.0f;
    return s;
}

std::vector<Tri> soup(std::size_t count, std::uint64_t seed) {
    std::vector<Tri> t(count);
    std::uint32_t r = std::uint32_t(seed * 2654435761u + 1u);
    const auto next = [&r] { r = hash(r + 0x9e3779b9u); return unit(r); };
    for (Tri& x : t) {
        const Vec3 c{24.0f * next() - 12.0f, 24.0f * next() - 12.0f, 24.0f * next() - 12.0f};
        x.a = c + Vec3{2 * next() - 1, 2 * next() - 1, 2 * next() - 1};
        x.b = c + Vec3{2 * next() - 1, 2 * next() - 1, 2 * next() - 1};
        x.c = c + Vec3{2 * next() - 1, 2 * next() - 1, 2 * next() - 1};
    }
    return t;
}

std::vector<Tri> trianglesOf(const swr::Geometry& g) {
    std::vector<Tri> t(g.triangles());
    for (std::size_t i = 0; i < t.size(); ++i) t[i] = {g.pos[g.idx[i * 3]], g.pos[g.idx[i * 3 + 1]], g.pos[g.idx[i * 3 + 2]]};
    return t;
}

}  // namespace raw::rt
