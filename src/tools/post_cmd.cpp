// raw_native_cli post-parity: see raw/tools/raster_cmd.hpp.
#include "raw/tools/raster_cmd.hpp"
#include "raw/tools/model_scene.hpp"
#include "raw/tools/model_manifest.hpp"
#include "raw/renderer/post_parity.hpp"
#include "raw/assets/json.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
int postParityCommand(int argc, char** argv) {
    std::string models, out, manifest = "evidence/m3-scene-models.json";
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--models") && i + 1 < argc) models = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--model-manifest") && i + 1 < argc) manifest = argv[++i];
        else { std::printf("usage: raw_native_cli post-parity [--models DIR] [--model-manifest PATH] [--out FILE]\n"); return 2; }
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
    if (!models.empty()) {
        // Suzanne, then the helmet and interior roles as the manifest names them.
        std::vector<std::pair<std::string, std::string>> want{{"Suzanne", models + "/Models/Suzanne/glTF/Suzanne.gltf"}};
        for (const char* role : {"helmet", "interior"}) {
            std::string err;
            const std::string path = resolveModelRole(manifest, role, models, err);
            if (path.empty()) { std::printf("post-parity: %s\n", err.c_str()); return 2; }
            want.push_back({std::string(role) + " role", path});
        }
        for (const auto& m : want) {
            p.model = m.second;
            try { scenes.push_back(sceneFromParams(p, nullptr)); }
            catch (const assets::AssetError& e) { std::printf("post-parity: cannot load %s: %s\n", p.model.c_str(), e.what()); return 2; }
            names.push_back(m.first);
        }
        scenes.push_back(scenes.back());
        scenes.back().camera.eye = {0.9f, 1.2f, 1.6f}; scenes.back().camera.center = {0.0f, 0.3f, 0.0f};
        names.push_back("interior close-up");
    }
    std::vector<std::pair<std::string, const Scene*>> list;
    for (std::size_t k = 0; k < scenes.size(); ++k) list.push_back({names[k], &scenes[k]});
    const gpu_check::PostParity r = gpu_check::postParity(*dev, list);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << j;
    return r.pass() ? 0 : 1;
}
}  // namespace raw
