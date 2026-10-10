// raw_native_cli shadow-parity: see raw/tools/raster_cmd.hpp.
#include "raw/tools/raster_cmd.hpp"
#include "raw/tools/model_scene.hpp"
#include "raw/tools/owned_assets.hpp"
#include "raw/renderer/shadow_parity.hpp"
#include "raw/assets/json.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
int shadowParityCommand(int argc, char** argv) {
    int size = 256;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--size") && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else { std::printf("usage: raw_native_cli shadow-parity [--size N] [--out FILE]\n"); return 2; }
    }
    if (size < 16 || size > 1024) { std::printf("shadow-parity: --size 16..1024\n"); return 2; }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev) { std::printf("{\n \"error\": \"%s\"\n}\n", why.c_str()); return 4; }
    std::vector<Scene> scenes;
    std::vector<std::string> names{"built-in test scene"};
    CliParams p; p.width = p.height = size;
    scenes.push_back(sceneFromParams(p, nullptr));
    scenes.push_back(scenes[0]);                     // method note 5: a near camera, so cascade 0 is in use
    scenes[1].camera.eye = {1.8f, 3.2f, 2.4f}; scenes[1].camera.center = {0.8f, 0.2f, 1.0f};
    names.push_back("test scene, near camera");
    // The owned assets (author's decision, 2026-10-10; evidence/m3-shadows-bounds.json method note 6).
    scenes.push_back(owned::inTestScene(owned::hero(), size, size));
    names.push_back("raw-hero");
    scenes.push_back(owned::hallScene(owned::hall()));
    names.push_back("raw-hall nave");
    std::vector<std::pair<std::string, const Scene*>> list;
    for (std::size_t k = 0; k < scenes.size(); ++k) {
        scenes[k].lights[0].dir = normalize(Vec3{0.55f, -0.45f, 0.35f});   // the shadow checks' sun
        list.push_back({names[k], &scenes[k]});
    }
    const gpu_check::ShadowParity r = gpu_check::shadowParity(*dev, list, size);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << j;
    return r.pass() ? 0 : 1;
}
}  // namespace raw
