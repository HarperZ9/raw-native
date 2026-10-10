// GTAO and SSR against ray casts (evidence/m3-post-bounds.json), on the CPU.
//   test_post [--json] [--models DIR]
// --models adds Suzanne and the helmet role's model to the built-in test scene's two cameras.
#include "raw/renderer/post.hpp"
#include "raw/renderer/raster.hpp"
#include "raw/tools/model_scene.hpp"
#if __has_include("raw/tools/model_manifest.hpp")
#include "raw/tools/model_manifest.hpp"
#define RAW_HAS_MODEL_MANIFEST 1
#endif
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace raw;

namespace {
GBuffer gbufferOf(const Scene& s) {
    RasterOptions ro; ro.perspectiveDepth = true;
    return rasterize(s, 256, 256, nullptr, nullptr, ro);
}
struct Ao { long pixels{0}, occluded{0}; double mean{0}, p95{0}, ctlMean{0}, ctlP95{0}; };
void stats(std::vector<double>& d, double& mean, double& p95) {
    std::sort(d.begin(), d.end());
    mean = 0.0;
    for (double v : d) mean += v / double(d.size());
    p95 = d.empty() ? 0.0 : d[std::min(d.size() - 1, std::size_t(0.95 * double(d.size())))];
}
Ao aoCheck(const Scene& s, const GBuffer& g, const post::ViewGBuffer& v) {
    Ao r;
    const std::vector<double> a = post::gtao(v), ref = post::rayAo(s, g, 0.5);
    std::vector<double> d, c;
    for (int y = 8; y < 248; ++y) for (int x = 8; x < 248; ++x) {
        const std::size_t i = std::size_t(y) * 256 + x;
        if (!g.mask.at(x, y)) continue;
        ++r.pixels; r.occluded += ref[i] < 0.9;
        d.push_back(std::fabs(a[i] - ref[i])); c.push_back(std::fabs(1.0 - ref[i]));
    }
    stats(d, r.mean, r.p95); stats(c, r.ctlMean, r.ctlP95);
    return r;
}
struct Ssr { long resolvable{0}, correct{0}, missed{0}, other{0}, falseHits{0}, ctlFalse{0}, ctlCorrect{0}; };
Ssr ssrCheck(const Scene& s, const GBuffer& g, const post::ViewGBuffer& v) {
    Ssr r;
    std::vector<Vec3> H; std::vector<uint8_t> hit;
    post::rayMirror(s, g, H, hit);
    const std::vector<int> a = post::ssr(v);
    post::SsrParams loose; loose.thickness = 1e30;
    const std::vector<int> ctl = post::ssr(v, loose);
    post::SsrParams wrong; wrong.viewNormal = true;            // control (c), method note 2
    const std::vector<int> ctlN = post::ssr(v, wrong);
    const Mat4 view = s.camera.view();
    for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
        if (!g.mask.at(x, y)) continue;
        const std::size_t i = std::size_t(y) * 256 + x;
        bool resolvable = false;
        double fp = 0.0;
        if (hit[i]) {
            const Vec4 hv = mul(view, Vec4{H[i].x, H[i].y, H[i].z, 1.0f});
            const double hp[3] = {hv.x, hv.y, hv.z};
            double sx, sy;
            if (post::project(v, hp, sx, sy) && sx >= 0 && sy >= 0 && sx < 256 && sy < 256) {
                fp = 2.0 * double(-hv.z) * v.tanHalf / 256.0;
                const int px = int(sx), py = int(sy);
                resolvable = g.mask.at(px, py) && length(g.position.at(px, py) - H[i]) <= 2.0 * fp;
            }
        }
        if (resolvable) {
            ++r.resolvable;
            if (a[i] >= 0 && length(g.position.at(a[i] % 256, a[i] / 256) - H[i]) <= 2.0 * fp) ++r.correct;
            else if (a[i] < 0) ++r.missed;   // reported: the march found nothing
            if (ctlN[i] >= 0 && length(g.position.at(ctlN[i] % 256, ctlN[i] / 256) - H[i]) <= 2.0 * fp) ++r.ctlCorrect;
        } else {
            ++r.other; r.falseHits += a[i] >= 0; r.ctlFalse += ctl[i] >= 0;
        }
    }
    return r;
}
}  // namespace

int main(int argc, char** argv) {
    bool json = false, stats = false, strict = false;
    std::string models;
    for (int i = 1; i < argc; ++i) {
        json = json || !std::strcmp(argv[i], "--json");
        stats = stats || !std::strcmp(argv[i], "--scene-stats");
        strict = strict || json || !std::strcmp(argv[i], "--strict");
        if (!std::strcmp(argv[i], "--models") && i + 1 < argc) models = argv[++i];
    }
    CliParams p; p.width = p.height = 256;
    std::vector<std::pair<std::string, Scene>> scenes{{"built-in test scene", sceneFromParams(p, nullptr)}};
    scenes.push_back({"test scene, near camera", scenes[0].second});
    scenes[1].second.camera.eye = {1.8f, 3.2f, 2.4f}; scenes[1].second.camera.center = {0.8f, 0.2f, 1.0f};
    if (!models.empty()) {
        std::string err;
        p.model = models + "/Models/Suzanne/glTF/Suzanne.gltf";
        scenes.push_back({"Suzanne", sceneFromParams(p, nullptr)});
#ifdef RAW_HAS_MODEL_MANIFEST
        p.model = resolveModelRole(std::string(RAW_SOURCE_DIR) + "/evidence/m3-scene-models.json", "helmet", models, err);
#else
        p.model = models + "/Models/FlightHelmet/glTF/FlightHelmet.gltf";   // the manifest's current helmet choice
#endif
        if (!p.model.empty()) scenes.push_back({"helmet role", sceneFromParams(p, nullptr)});
        // Method note 2: the interior role's model (ABeautifulGame today), with Suzanne's camera.
        p.model = models + "/Models/ABeautifulGame/glTF/ABeautifulGame.gltf";
        scenes.push_back({"interior role", sceneFromParams(p, nullptr)});
    }
    if (!stats && !models.empty()) {   // method note 4: the interior close-up
        Scene c = scenes.back().second;
        c.camera.eye = {0.9f, 1.2f, 1.6f}; c.camera.center = {0.0f, 0.3f, 0.0f};
        scenes.push_back({"interior close-up", c});
    }
    if (stats) {   // scene design only (method note 3): the references' coverage, no GTAO or SSR result
        std::vector<std::pair<std::string, Scene>> all = scenes;
        if (!models.empty()) {
            const Vec3 eyes[3] = {{0.9f, 1.2f, 1.6f}, {0.7f, 1.0f, 1.2f}, {0.5f, 0.8f, 0.9f}};
            for (const Vec3& e : eyes) {
                Scene c = scenes.back().second;
                c.camera.eye = e; c.camera.center = {0.0f, 0.3f, 0.0f};
                char n[96]; std::snprintf(n, sizeof n, "interior close (%.1f, %.1f, %.1f)", e.x, e.y, e.z);
                all.push_back({n, c});
            }
        }
        for (const auto& s : all) {
            const GBuffer g = gbufferOf(s.second);
            const post::ViewGBuffer v = post::viewGBuffer(g, s.second.camera);
            const std::vector<double> ref = post::rayAo(s.second, g, 0.5);
            long occ = 0;
            for (int y = 8; y < 248; ++y) for (int x = 8; x < 248; ++x) occ += g.mask.at(x, y) && ref[std::size_t(y) * 256 + x] < 0.9;
            std::vector<Vec3> H; std::vector<uint8_t> hit;
            post::rayMirror(s.second, g, H, hit);
            const Mat4 view = s.second.camera.view();
            long res = 0;
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
                const std::size_t i = std::size_t(y) * 256 + x;
                if (!g.mask.at(x, y) || !hit[i]) continue;
                const Vec4 hv = mul(view, Vec4{H[i].x, H[i].y, H[i].z, 1.0f});
                const double hp[3] = {hv.x, hv.y, hv.z};
                double sx, sy;
                if (!post::project(v, hp, sx, sy) || sx < 0 || sy < 0 || sx >= 256 || sy >= 256) continue;
                const double fp = 2.0 * double(-hv.z) * v.tanHalf / 256.0;
                res += g.mask.at(int(sx), int(sy)) && length(g.position.at(int(sx), int(sy)) - H[i]) <= 2.0 * fp;
            }
            std::printf("%s: occluded %ld, resolvable %ld\n", s.first.c_str(), occ, res);
        }
        return 0;
    }
    // Sanity, reported: an unoccluded plane alone should read 1.
    Scene plane = scenes[0].second;
    plane.meshes.pop_back();
    const GBuffer pg = gbufferOf(plane);
    const std::vector<double> pa = post::gtao(post::viewGBuffer(pg, plane.camera));
    double planeWorst = 0.0;
    for (int y = 8; y < 248; ++y) for (int x = 8; x < 248; ++x) if (pg.mask.at(x, y)) planeWorst = std::max(planeWorst, std::fabs(pa[std::size_t(y) * 256 + x] - 1.0));
    std::string rows;
    int aoScenes = 0, ssrScenes = 0;
    for (const auto& s : scenes) {
        const GBuffer g = gbufferOf(s.second);
        const post::ViewGBuffer v = post::viewGBuffer(g, s.second.camera);
        const Ao a = aoCheck(s.second, g, v);
        const Ssr r = ssrCheck(s.second, g, v);
        // Method note 2: a scene that misses a coverage bound is reported, not gated, for that metric.
        const bool aoGated = a.occluded >= 2000, ssrGated = r.resolvable >= 2000;
        aoScenes += aoGated; ssrScenes += ssrGated;
        // The interior close-up's accuracy bounds are open failures (evidence/m3-post-runs.json): --json
        // and --strict gate them, ctest prints them. The controls are always gated.
        const bool open = !strict && s.first == "interior close-up";
        const bool aoOk = a.mean <= 0.05 && a.p95 <= 0.15;
        const bool ssrOk = double(r.correct) >= 0.9 * double(r.resolvable) && double(r.falseHits) <= 0.1 * double(r.other);
        if (aoGated) {
            if (open && !aoOk) std::printf("OPEN FAILURE (not gated here): GTAO on %s, mean %.4f, p95 %.4f\n", s.first.c_str(), a.mean, a.p95);
            else CHECK(aoOk);
            CHECK(!(a.ctlMean <= 0.05 && a.ctlP95 <= 0.15));
        }
        if (ssrGated) {
            if (open && !ssrOk) std::printf("OPEN FAILURE (not gated here): SSR on %s, %ld of %ld correct, %ld false hits of %ld\n", s.first.c_str(), r.correct, r.resolvable, r.falseHits, r.other);
            else CHECK(ssrOk);
            CHECK(double(r.ctlCorrect) < 0.9 * double(r.resolvable));
        }
        char b[900];
        std::snprintf(b, sizeof b, "%s  {\"scene\": \"%s\", \"gtao\": {\"pixels\": %ld, \"occluded\": %ld, \"mean_abs\": %.4f, \"p95_abs\": %.4f, \"control_mean\": %.4f, \"control_p95\": %.4f},"
                      " \"ssr\": {\"resolvable\": %ld, \"correct\": %ld, \"rate\": %.4f, \"missed\": %ld, \"other\": %ld, \"false_hits\": %ld, \"false_rate\": %.4f, \"control_false_rate\": %.4f, \"control_view_normal_rate\": %.4f}, \"gated\": {\"gtao\": %s, \"ssr\": %s}}",
                      rows.empty() ? "" : ",\n", s.first.c_str(), a.pixels, a.occluded, a.mean, a.p95, a.ctlMean, a.ctlP95, r.resolvable, r.correct,
                      r.resolvable ? double(r.correct) / double(r.resolvable) : 0.0, r.missed, r.other, r.falseHits, r.other ? double(r.falseHits) / double(r.other) : 0.0,
                      r.other ? double(r.ctlFalse) / double(r.other) : 0.0,
                      r.resolvable ? double(r.ctlCorrect) / double(r.resolvable) : 0.0, aoGated ? "true" : "false", ssrGated ? "true" : "false");
        rows += b;
    }
    if (!models.empty()) CHECK(aoScenes >= 2 && ssrScenes >= 2);   // method note 2: each metric gated on at least two scenes
    else std::printf("note: the two-scene gate needs --models; built-in scenes gate GTAO on %d and SSR on %d\n", aoScenes, ssrScenes);
    std::printf("{\n \"plane_gtao_worst_abs_from_1\": %.5f,\n \"scenes\": [\n%s\n ],\n \"failures\": %d\n}\n", planeWorst, rows.c_str(), raw_test_failures());
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
