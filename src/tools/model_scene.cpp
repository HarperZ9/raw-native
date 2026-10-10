// The scene a run renders: see raw/tools/model_scene.hpp.
#include "raw/tools/model_scene.hpp"
#include "raw/assets/gltf.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>
namespace raw {
namespace {
bool readFile(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
}  // namespace

Scene sceneFromParams(const CliParams& p, Arena* arena) {
    Scene s = buildTestScene(p.width, p.height, arena);
    if (p.model.empty()) return s;
    std::vector<std::uint8_t> file;
    if (!readFile(p.model, file)) throw assets::AssetError("cannot read " + p.model);
    const size_t slash = p.model.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? std::string() : p.model.substr(0, slash + 1);
    assets::GltfModel m = assets::loadGltf(file, [&](const std::string& uri, std::vector<std::uint8_t>& out) {
        return uri.find("..") == std::string::npos && readFile(dir + uri, out);   // stay beside the model
    });
    // Largest extent 2 units, centred over the origin, resting on the ground (y = 0).
    const Vec3 lo = m.boundsMin, hi = m.boundsMax;
    const float ext = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-6f}), k = 2.0f / ext;
    const Vec3 c{(lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f};
    for (auto& mesh : m.meshes)
        for (Vec3& v : mesh.positions) v = (v - c) * k;
    s.meshes.pop_back();                                  // the built-in box
    assets::addToScene(m, s);
    return s;
}
}  // namespace raw
