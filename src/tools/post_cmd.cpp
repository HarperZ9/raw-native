// raw_native_cli post-parity: see raw/tools/raster_cmd.hpp.
#include "raw/tools/raster_cmd.hpp"
#include "raw/tools/model_scene.hpp"
#include "raw/tools/owned_assets.hpp"
#include "raw/renderer/post_parity.hpp"
#include "raw/assets/json.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
int postParityCommand(int argc, char** argv) {
    std::string out;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else { std::printf("usage: raw_native_cli post-parity [--out FILE]\n"); return 2; }
    }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev) { std::printf("{\n \"error\": \"%s\"\n}\n", why.c_str()); return 4; }
    std::vector<Scene> scenes;
    std::vector<std::string> names{"built-in test scene", "test scene, near camera"};
    CliParams p; p.width = p.height = 256;
    scenes.push_back(sceneFromParams(p, nullptr));
    scenes.push_back(scenes[0]);
    scenes[1].camera.eye = {1.8f, 3.2f, 2.4f}; scenes[1].camera.center = {0.8f, 0.2f, 1.0f};
    // The owned assets (author's decision, 2026-10-10; evidence/m3-post-bounds.json method note 9).
    const owned::Asset hall = owned::hall();
    scenes.push_back(owned::inTestScene(owned::hero(), 256, 256));
    names.push_back("raw-hero");
    scenes.push_back(owned::hallScene(hall));
    names.push_back("raw-hall nave");
    scenes.push_back(owned::hallScene(hall, true));
    names.push_back("raw-hall gallery");
    std::vector<std::pair<std::string, const Scene*>> list;
    for (std::size_t k = 0; k < scenes.size(); ++k) list.push_back({names[k], &scenes[k]});
    const gpu_check::PostParity r = gpu_check::postParity(*dev, list);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << j;
    return r.pass() ? 0 : 1;
}
}  // namespace raw
