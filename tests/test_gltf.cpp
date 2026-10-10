// glTF import (raw/assets/gltf.hpp): embedded and GLB buffers, node transforms,
// generated normals, materials, and refusals of malformed files.
#include "raw/assets/gltf.hpp"
#include "check.hpp"
#include <cstring>
#include <string>
#include <vector>
using namespace raw;
using namespace raw::assets;

static std::string b64(const std::vector<std::uint8_t>& d){
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (std::size_t i = 0; i < d.size(); i += 3){
        const std::uint32_t v = std::uint32_t(d[i]) << 16 | (i + 1 < d.size() ? d[i + 1] << 8 : 0) | (i + 2 < d.size() ? d[i + 2] : 0);
        o += T[v >> 18 & 63]; o += T[v >> 12 & 63]; o += i + 1 < d.size() ? T[v >> 6 & 63] : '='; o += i + 2 < d.size() ? T[v & 63] : '=';
    }
    return o;
}
template <class T> static void put(std::vector<std::uint8_t>& b, T v){ const auto* p = reinterpret_cast<const std::uint8_t*>(&v); b.insert(b.end(), p, p + sizeof v); }

// One triangle (positions only, uint16 indices), in a node translated by (1, 2, 3) under a parent scaled by 2.
static std::vector<std::uint8_t> triangleBuffer(){
    std::vector<std::uint8_t> b;
    for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) put(b, f);
    for (std::uint16_t i : {0, 1, 2}) put(b, i);
    b.push_back(0); b.push_back(0);
    return b;
}
static std::string gltfJson(const std::string& bufferUri){
    return std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)") +
        R"("nodes":[{"scale":[2,2,2],"children":[1]},{"translation":[1,2,3],"mesh":0}],)" +
        R"("meshes":[{"name":"tri","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)" +
        R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,0.125,1]}}],)" +
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)" +
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)" +
        R"("buffers":[{"byteLength":44)" + bufferUri + "}]}";
}
static GltfModel load(const std::string& s){ return loadGltf(std::span(reinterpret_cast<const std::uint8_t*>(s.data()), s.size())); }
static bool refuses(const std::string& s){ try { load(s); return false; } catch (const AssetError&){ return true; } }

int main(){
    const auto buf = triangleBuffer();
    const std::string text = gltfJson(R"(,"uri":"data:application/octet-stream;base64,)" + b64(buf) + "\"");
    GltfModel m = load(text);
    CHECK(m.meshes.size() == 1);
    const GltfMesh& g = m.meshes[0];
    CHECK(g.indices.size() == 3 && g.name == "tri");
    // world = scale 2 * translate (1, 2, 3): vertex (1, 0, 0) lands at (4, 4, 6).
    CHECK_NEAR(g.positions[1].x, 4, 1e-6); CHECK_NEAR(g.positions[1].y, 4, 1e-6); CHECK_NEAR(g.positions[1].z, 6, 1e-6);
    // Generated normals face +z, unit length.
    CHECK_NEAR(g.normals[0].z, 1, 1e-6);
    CHECK_NEAR(g.baseColor.y, 0.25, 1e-6);
    CHECK_NEAR(m.boundsMax.x, 4, 1e-6);

    // The same model as GLB: JSON chunk, then the binary chunk.
    std::string json = gltfJson("");
    while (json.size() % 4) json += ' ';
    std::vector<std::uint8_t> glb;
    put(glb, std::uint32_t(0x46546C67)); put(glb, std::uint32_t(2)); put(glb, std::uint32_t(12 + 8 + json.size() + 8 + buf.size()));
    put(glb, std::uint32_t(json.size())); put(glb, std::uint32_t(0x4E4F534A)); glb.insert(glb.end(), json.begin(), json.end());
    put(glb, std::uint32_t(buf.size())); put(glb, std::uint32_t(0x004E4942)); glb.insert(glb.end(), buf.begin(), buf.end());
    const GltfModel g2 = loadGltf(glb);
    CHECK(g2.meshes.size() == 1 && g2.meshes[0].positions.size() == 3);
    CHECK_NEAR(g2.meshes[0].positions[2].y, 6, 1e-6);

    // Into a scene, with albedo from the material.
    Scene s;
    addToScene(m, s);
    CHECK(s.meshes.size() == 1 && s.meshes[0].indices.size() == 3);
    CHECK_NEAR(s.meshes[0].material.albedo.x, 0.5, 1e-6);

    // Refusals: each throws AssetError, never anything else.
    CHECK(refuses(""));
    CHECK(refuses("{"));
    CHECK(refuses(R"({"asset":{"version":"1.0"}})"));
    std::string bad = text; bad.replace(bad.find("\"count\":3,\"type\":\"VEC3\""), 25, "\"count\":9,\"type\":\"VEC3\"");
    CHECK(refuses(bad));                                        // accessor past its view
    std::string cyc = text; cyc.replace(cyc.find("\"children\":[1]"), 14, "\"children\":[0]");
    CHECK(refuses(cyc));                                        // a node that is its own child
    std::string deep(200, '['); deep += std::string(200, ']');
    CHECK(refuses(deep));                                       // nesting limit
    return raw_test_summary();
}
