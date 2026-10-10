// raw_native_cli raster-identity: see raw/tools/raster_cmd.hpp.
#include "raw/tools/raster_cmd.hpp"
#include "raw/tools/model_scene.hpp"
#include "raw/tools/owned_assets.hpp"
#include "raw/renderer/raster_identity.hpp"
#include "raw/assets/json.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
namespace raw {
int rasterIdentityCommand(int argc, char** argv) {
    int size = 256, bits = 0;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--size") && i + 1 < argc) size = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--subpixel-bits") && i + 1 < argc) bits = std::atoi(argv[++i]);   // diagnosis: the reference's snapping
        else { std::printf("usage: raw_native_cli raster-identity [--size N] [--out FILE]\n"); return 2; }
    }
    if (size < 16 || size > 2048) { std::printf("raster-identity: --size 16..2048\n"); return 2; }
    std::string why;
    rhi::Device* dev = rhi::device(why);
    if (!dev) { std::printf("{\n \"error\": \"%s\"\n}\n", why.c_str()); return 4; }
    std::vector<Scene> scenes;
    std::vector<std::string> names{"built-in test scene"};
    CliParams p; p.width = p.height = size;
    scenes.push_back(sceneFromParams(p, nullptr));
    // The owned assets (author's decision, 2026-10-10; evidence/m3-rhi3-bounds.json method note 2).
    scenes.push_back(owned::inTestScene(owned::hero(), size, size));
    names.push_back("raw-hero");
    scenes.push_back(owned::hallScene(owned::hall()));
    names.push_back("raw-hall nave");
    std::vector<std::pair<std::string, const Scene*>> list;
    for (std::size_t k = 0; k < scenes.size(); ++k) list.push_back({names[k], &scenes[k]});
    const gpu_check::RasterIdentity r = gpu_check::rasterIdentity(*dev, list, size, bits);
    const std::string j = r.json();
    std::fputs(j.c_str(), stdout);
    if (!out.empty()) std::ofstream(out, std::ios::binary) << j;
    return r.pass() ? 0 : 1;
}
}  // namespace raw
