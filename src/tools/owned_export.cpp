// The owned assets as scenes and as glTF 2.0: see raw/tools/owned_assets.hpp.
#include "raw/tools/owned_assets.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace raw::owned {
namespace {
std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}
std::string rgb(const pbr::Rgb& c) { return "[" + num(c.r) + ", " + num(c.g) + ", " + num(c.b) + "]"; }
void append(std::vector<uint8_t>& bin, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const uint8_t*>(data);
    bin.insert(bin.end(), p, p + bytes);
}
std::string extensionsOf(const Part& p) {
    const pbr::Material& m = p.material;
    std::vector<std::string> e;
    if (p.extensions & kIor) e.push_back("\"KHR_materials_ior\": {\"ior\": " + num(m.ior) + "}");
    if (p.extensions & kSpecular) e.push_back("\"KHR_materials_specular\": {\"specularFactor\": " + num(m.specular) + ", \"specularColorFactor\": " + rgb(m.specularColor) + "}");
    if (p.extensions & kClearcoat) e.push_back("\"KHR_materials_clearcoat\": {\"clearcoatFactor\": " + num(m.clearcoat) + ", \"clearcoatRoughnessFactor\": " + num(m.clearcoatRoughness) + "}");
    if (p.extensions & kSheen) e.push_back("\"KHR_materials_sheen\": {\"sheenColorFactor\": " + rgb(m.sheenColor) + ", \"sheenRoughnessFactor\": " + num(m.sheenRoughness) + "}");
    if (p.extensions & kTransmission) e.push_back("\"KHR_materials_transmission\": {\"transmissionFactor\": " + num(m.transmission) + "}");
    if (p.extensions & kVolume)
        e.push_back("\"KHR_materials_volume\": {\"thicknessFactor\": " + num(m.thickness) + ", \"attenuationDistance\": " + num(m.attenuationDistance) +
                    ", \"attenuationColor\": " + rgb(m.attenuationColor) + "}");
    if (p.extensions & kAnisotropy) e.push_back("\"KHR_materials_anisotropy\": {\"anisotropyStrength\": " + num(m.anisotropy) + ", \"anisotropyRotation\": " + num(m.anisotropyRotation) + "}");
    if (p.extensions & kIridescence)
        e.push_back("\"KHR_materials_iridescence\": {\"iridescenceFactor\": " + num(m.iridescence) + ", \"iridescenceIor\": " + num(m.iridescenceIor) +
                    ", \"iridescenceThicknessMinimum\": " + num(m.iridescenceThickness) + ", \"iridescenceThicknessMaximum\": " + num(m.iridescenceThickness) + "}");
    if (p.extensions & kEmissiveStrength) e.push_back("\"KHR_materials_emissive_strength\": {\"emissiveStrength\": " + num(m.emissiveStrength) + "}");
    std::string s;
    for (std::size_t i = 0; i < e.size(); ++i) s += (i ? ", " : "") + e[i];
    return s;
}
}  // namespace

Scene toScene(const Asset& a) {
    Scene s;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const Part& p : a.parts) {
        Mesh m;
        m.positions.assign(p.positions.begin(), p.positions.end());
        m.normals.assign(p.normals.begin(), p.normals.end());
        for (uint32_t i : p.indices) m.indices.push_back(int(i));
        m.material.albedo = {float(p.material.baseColor.r), float(p.material.baseColor.g), float(p.material.baseColor.b)};
        for (const Vec3& q : p.positions) {
            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
        }
        s.meshes.push_back(std::move(m));
    }
    s.lights.push_back(Light{normalize(Vec3{0.55f, -0.45f, 0.35f}), 1.0f});
    const Vec3 c = (lo + hi) * 0.5f, e = hi - lo;
    const float r = std::max({e.x, e.y, e.z});
    s.camera.center = c;
    s.camera.eye = c + Vec3{0.55f * r, 0.35f * r, 0.95f * r};
    s.camera.fovy = 0.9f; s.camera.aspect = 1.0f;
    return s;
}

std::string exportGltf(const Asset& a, std::vector<uint8_t>& bin) {
    bin.clear();
    std::string meshes, nodes, accessors, views, materials, nodeList;
    uint32_t used = 0;
    int acc = 0;
    for (std::size_t k = 0; k < a.parts.size(); ++k) {
        const Part& p = a.parts[k];
        used |= p.extensions;
        Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (const Vec3& q : p.positions) {
            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
        }
        const std::size_t nv = p.positions.size(), ni = p.indices.size();
        const std::size_t offP = bin.size();
        append(bin, p.positions.data(), nv * 12);
        const std::size_t offN = bin.size();
        append(bin, p.normals.data(), nv * 12);
        const std::size_t offI = bin.size();
        append(bin, p.indices.data(), ni * 4);
        const std::string sep = k ? ",\n" : "";
        views += sep + "  {\"buffer\": 0, \"byteOffset\": " + std::to_string(offP) + ", \"byteLength\": " + std::to_string(nv * 12) + ", \"target\": 34962},\n"
                 "  {\"buffer\": 0, \"byteOffset\": " + std::to_string(offN) + ", \"byteLength\": " + std::to_string(nv * 12) + ", \"target\": 34962},\n"
                 "  {\"buffer\": 0, \"byteOffset\": " + std::to_string(offI) + ", \"byteLength\": " + std::to_string(ni * 4) + ", \"target\": 34963}";
        accessors += sep + "  {\"bufferView\": " + std::to_string(3 * k) + ", \"componentType\": 5126, \"count\": " + std::to_string(nv) +
                     ", \"type\": \"VEC3\", \"min\": [" + num(lo.x) + ", " + num(lo.y) + ", " + num(lo.z) + "], \"max\": [" + num(hi.x) + ", " + num(hi.y) + ", " + num(hi.z) + "]},\n"
                     "  {\"bufferView\": " + std::to_string(3 * k + 1) + ", \"componentType\": 5126, \"count\": " + std::to_string(nv) + ", \"type\": \"VEC3\"},\n"
                     "  {\"bufferView\": " + std::to_string(3 * k + 2) + ", \"componentType\": 5125, \"count\": " + std::to_string(ni) + ", \"type\": \"SCALAR\"}";
        meshes += sep + "  {\"name\": \"" + p.name + "\", \"primitives\": [{\"attributes\": {\"POSITION\": " + std::to_string(acc) + ", \"NORMAL\": " + std::to_string(acc + 1) +
                  "}, \"indices\": " + std::to_string(acc + 2) + ", \"material\": " + std::to_string(k) + "}]}";
        acc += 3;
        nodes += sep + "  {\"name\": \"" + p.name + "\", \"mesh\": " + std::to_string(k) + "}";
        nodeList += (k ? ", " : "") + std::to_string(k);
        const pbr::Material& m = p.material;
        const std::string ext = extensionsOf(p);
        materials += sep + "  {\"name\": \"" + p.name + "\", \"pbrMetallicRoughness\": {\"baseColorFactor\": [" + num(m.baseColor.r) + ", " + num(m.baseColor.g) + ", " +
                     num(m.baseColor.b) + ", 1], \"metallicFactor\": " + num(m.metallic) + ", \"roughnessFactor\": " + num(m.roughness) + "}" +
                     (p.extensions & kEmissiveStrength ? ", \"emissiveFactor\": " + rgb(m.emissive) : std::string()) +
                     (ext.empty() ? std::string() : ", \"extensions\": {" + ext + "}") + "}";
    }
    std::string usedList;
    for (int b = 0; b < kExtCount; ++b) if (used & (1u << b)) usedList += (usedList.empty() ? "\"" : ", \"") + std::string(extName(b)) + "\"";
    return "{\n \"asset\": {\"version\": \"2.0\", \"generator\": \"raw-native owned assets (src/tools/owned_*.cpp)\", \"copyright\": \"raw-native, FSL-1.1-MIT\"},\n"
           " \"extensionsUsed\": [" + usedList + "],\n \"scene\": 0,\n \"scenes\": [{\"name\": \"" + a.name + "\", \"nodes\": [" + nodeList + "]}],\n"
           " \"nodes\": [\n" + nodes + "\n ],\n \"meshes\": [\n" + meshes + "\n ],\n \"materials\": [\n" + materials + "\n ],\n"
           " \"accessors\": [\n" + accessors + "\n ],\n \"bufferViews\": [\n" + views + "\n ],\n"
           " \"buffers\": [{\"uri\": \"" + a.name + ".bin\", \"byteLength\": " + std::to_string(bin.size()) + "}]\n}\n";
}
}  // namespace raw::owned
