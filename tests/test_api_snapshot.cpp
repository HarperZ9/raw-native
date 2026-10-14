// The compatibility suite of API snapshot 3 (ADR 0015, roadmap M3 criterion 5): code written
// against the snapshot's headers, the way a user writes it, and the contracts it relies on.
// It checks that the snapshot still compiles and behaves as documented, not that the renderer's
// numbers stay fixed: a model may improve without breaking the snapshot.
//   test_api_snapshot [--json]
#include "raw/core/api_snapshot.hpp"
#include "raw/scene/scene.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/renderer/pbr.hpp"
#include "raw/renderer/lighting.hpp"
#include "raw/renderer/shadows.hpp"
#include "raw/renderer/post.hpp"
#include "raw/renderer/taa.hpp"
#include "raw/renderer/bvh.hpp"
#include "raw/renderer/bake.hpp"
#include "raw/renderer/colour.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace raw;

namespace {
bool finite3(const pbr::Rgb& c) { return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b); }
int checks = 0;
void expect(bool ok, int line = __builtin_LINE()) {
    ++checks;
    if (!ok) std::printf("contract at line %d does not hold\n", line);
    CHECK(ok);
}

void sceneAndRaster() {
    const Scene s = buildTestScene(64, 64);
    expect(!s.meshes.empty() && !s.lights.empty());
    const Mat4 vp = mul(s.camera.proj(), s.camera.view());
    bool finite = true;
    for (float v : vp.m) finite = finite && std::isfinite(v);
    expect(finite);
    RasterOptions ro; ro.perspectiveDepth = true;
    const GBuffer g = rasterize(s, 64, 64, nullptr, nullptr, ro);
    long covered = 0;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) covered += g.mask.at(x, y) != 0;
    expect(g.w == 64 && g.h == 64 && covered > 0);
}

void materials() {
    const pbr::Tables t;
    pbr::Material m;                                   // defaults: white metal, roughness 1
    m.roughness = 0.4; m.clearcoat = 0.5; m.sheenColor = {0.2, 0.1, 0.1}; m.sheenRoughness = 0.5;
    const pbr::D3 o{0.3, 0.1, std::sqrt(1.0 - 0.1)}, i{-0.2, 0.4, std::sqrt(1.0 - 0.2)};
    const pbr::Rgb a = pbr::eval(m, t, o, i), b = pbr::eval(m, t, i, o);
    expect(finite3(a) && a.r >= 0.0 && a.g >= 0.0 && a.b >= 0.0);
    expect(std::fabs(a.r - b.r) <= 1e-9 * (1.0 + std::fabs(a.r)));   // reciprocity, documented in pbr.hpp
    const pbr::IblResponse r = pbr::iblResponse(m, t, o);
    expect(finite3(r.specular) && finite3(r.irradiance) && r.roughness > 0.0);
    expect(t.E(0.5, 0.5) > 0.0 && t.E(0.5, 0.5) <= 1.0 + 1e-6);
    // Snapshot 2: the anisotropic table is embedded, not computed, and its grid constants
    // describe it (api/snapshot-2/MIGRATION.md).
    expect(pbr::Tables::anisoByteCount() == 2 * pbr::Tables::kAnAll * sizeof(float));
    const double e4 = t.A4(0.5, 0.3, 0.5, 0.5) + t.B4(0.5, 0.3, 0.5, 0.5);
    expect(e4 > 0.0 && e4 <= 1.0 + 1e-6);
}

void lights() {
    const double ev = lighting::ev100(16.0, 1.0 / 125.0, 100.0);
    expect(std::fabs(ev - std::log2(16.0 * 16.0 * 125.0)) < 1e-9);
    lighting::Light l; l.type = lighting::LightType::Point; l.position = {0, 0, 2}; l.intensity = 10.0;
    pbr::D3 to;
    const pbr::Rgb e = lighting::illuminance(l, {0, 0, 0}, to);
    expect(std::fabs(e.r - 10.0 / 4.0) < 1e-9 && std::fabs(to.z - 1.0) < 1e-12);   // inverse square, lux
}

void shadowMaps() {
    const Scene s = buildTestScene(64, 64);
    const shadows::CascadeSet cs = shadows::fitCascades(s.camera, {0.4, -0.8, 0.3});
    bool increasing = true;
    for (int k = 1; k < shadows::kCascades; ++k) increasing = increasing && cs.c[std::size_t(k)].split > cs.c[std::size_t(k - 1)].split;
    expect(increasing && shadows::cascadeOf(cs, cs.c[0].split * 0.5) == 0);
    const shadows::ShadowMap m = shadows::rasterizeCascade(s, cs, 1, 8);
    expect(m.size == cs.size && m.depth.size() == std::size_t(m.size) * m.size);
}

void screenSpace() {
    const Scene s = buildTestScene(64, 64);
    RasterOptions ro; ro.perspectiveDepth = true;
    const GBuffer g = rasterize(s, 64, 64, nullptr, nullptr, ro);
    const post::ViewGBuffer v = post::viewGBuffer(g, s.camera);
    const std::vector<double> ao = post::gtao(v);
    const std::vector<int> hit = post::ssr(v);
    bool inRange = ao.size() == 64u * 64u && hit.size() == 64u * 64u;
    for (std::size_t i = 0; i < ao.size() && inRange; ++i) {
        inRange = std::isfinite(ao[i]);
        if (v.depth[i] < 0.0f) inRange = inRange && ao[i] == 1.0 && hit[i] == -1;   // documented: 1 and -1 where nothing was drawn
    }
    expect(inRange);
    // Snapshot 3: the back layer and the hidden-entry rule (api/snapshot-3/MIGRATION.md).
    post::ViewGBuffer vb = v;
    post::addBackLayer(vb, s, g);
    expect(vb.back.size() == vb.depth.size());
    post::SsrParams old; old.maxDistance = 20.0; old.steps = 64; old.hiddenEntry = false;
    expect(post::ssr(vb, old).size() == 64u * 64u && post::ssr(vb).size() == 64u * 64u);
    float jx, jy;
    taa::jitter(3, jx, jy);
    expect(jx >= -0.5f && jx < 0.5f && jy >= -0.5f && jy < 0.5f);
    const taa::Frame f = taa::render(s, s.camera, {}, 32, 32, jx, jy);
    const std::vector<float> first = taa::resolve(f, {}, {});
    expect(first == f.rgb);                                           // documented: the first frame is the frame
}

void tracing() {
    Bvh bvh;
    bvh.build({Tri{{-1, -1, -2}, {1, -1, -2}, {0, 1, -2}}});
    Hit h;
    expect(bvh.closest(Ray{{0, 0, 0}, {0, 0, -1}}, 1e30f, h) && std::fabs(h.t - 2.0f) < 1e-5f && h.tri == 0);
    const bake::Tracer tr(bake::testRoom());
    const pbr::Rgb e = tr.irradiance({0, 0.5, 0}, {0, 1, 0}, 64, 7);
    expect(finite3(e) && e.r >= 0.0);
}

void colours() {
    expect(std::fabs(colour::pqDecode(colour::pqEncode(0.37)) - 0.37) < 1e-9);
    const colour::Mat3 id = colour::conversion(colour::kRec709, colour::kRec709);
    expect(std::fabs(id[0] - 1.0) < 1e-9 && std::fabs(id[1]) < 1e-9);
}
}  // namespace

int main(int argc, char** argv) {
    const bool json = argc > 1 && !std::strcmp(argv[1], "--json");
    sceneAndRaster();
    materials();
    lights();
    shadowMaps();
    screenSpace();
    tracing();
    colours();
    std::printf("{\n \"api_snapshot\": %d,\n \"checks\": %d,\n \"failures\": %d\n}\n", kApiSnapshot, checks, raw_test_failures());
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
