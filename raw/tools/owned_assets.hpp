#pragma once
// Assets raw-native authors and owns outright (ROADMAP M3 criterion 1, amendment 2026-10-10;
// checks in evidence/m3-assets-bounds.json; provenance in assets/PROVENANCE.md). The code is
// the asset: raw-hero (a helmet-class asset carrying every material extension) and raw-hall
// (an interior hall at Sponza scale) are built procedurally with deterministic arithmetic, so
// the exported glTF files are identical byte for byte on every compiler.
#include "raw/renderer/pbr.hpp"
#include "raw/scene/scene.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::owned {

// A glTF material extension, as the exporter names it.
enum Ext : uint32_t {
    kIor = 1, kSpecular = 2, kClearcoat = 4, kSheen = 8, kTransmission = 16, kVolume = 32,
    kAnisotropy = 64, kIridescence = 128, kEmissiveStrength = 256,
};
inline constexpr int kExtCount = 9;
const char* extName(int bit);   // bit index 0..8 to "KHR_materials_..."

struct Part {
    std::string name;
    std::vector<Vec3> positions, normals;
    std::vector<uint32_t> indices;
    pbr::Material material;
    uint32_t extensions{0};      // which extension blocks the export writes
    std::string kind;            // "column", "arch", "drapery", "lantern", ... (hall content checks)
    int instances{1};            // how many separate pieces of `kind` the part holds
};
struct Asset { std::string name; std::vector<Part> parts; };

Asset hero();
Asset hall();
// The scene form: one mesh per part with its base colour as albedo, lit by the default sun.
Scene toScene(const Asset& a);
// The asset in the built-in test scene in place of its box: scaled to a largest extent of 2,
// centred over the origin and resting on the ground, as a loaded model is (model_scene.hpp).
Scene inTestScene(const Asset& a, int w, int h);
// raw-hall from inside: the nave camera at eye height looking down the arcade, lit by the sun
// through the open atrium. near: the gallery camera, closer to columns, arches and drapery.
Scene hallScene(const Asset& hall, bool gallery = false);
// glTF 2.0: the JSON text and its single binary buffer (written as <name>.bin beside it).
std::string exportGltf(const Asset& a, std::vector<uint8_t>& bin);

// Deterministic sine and cosine in double: range reduction and a fixed series, the same
// arithmetic on every compiler (no libm), so generated geometry is reproducible.
double dsin(double x);
double dcos(double x);

}  // namespace raw::owned
namespace raw {
// raw_native_cli export-assets --out DIR: writes raw-hero.gltf/.bin and raw-hall.gltf/.bin.
int exportAssetsCommand(int argc, char** argv);
}
