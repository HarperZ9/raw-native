// The scene a run renders: see raw/tools/model_scene.hpp.
#include "raw/tools/model_scene.hpp"
#include "raw/assets/gltf.hpp"
#include "raw/tools/owned_assets.hpp"
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

Scene controlSphereScene() {
    Scene s = buildTestScene(64, 64);
    s.meshes.clear();
    Mesh m; m.material.albedo = {0.6f, 0.6f, 0.6f};
    const int nu = 64, nv = 48;
    for (int j = 0; j <= nv; ++j)
        for (int i = 0; i <= nu; ++i){
            const double th = 3.14159265358979 * j / nv, ph = 6.28318530717959 * i / nu;
            const Vec3 n{float(owned::dsin(th) * owned::dcos(ph)), float(owned::dcos(th)), float(owned::dsin(th) * owned::dsin(ph))};
            m.positions.push_back(n); m.normals.push_back(n);
        }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i){
            const int a = j * (nu + 1) + i, b = a + 1, d = a + nu + 1, e = d + 1;
            for (int k : {a, d, b, b, d, e}) m.indices.push_back(k);
        }
    s.meshes.push_back(std::move(m));
    return s;
}
Scene sceneFromParams(const CliParams& p, Arena* arena) {
    if (p.ownedScene == "raw-hall-nave" || p.ownedScene == "raw-hall-gallery") return owned::hallScene(owned::hall(), p.ownedScene == "raw-hall-gallery");
    if (p.ownedScene == "control-sphere") return controlSphereScene();
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
