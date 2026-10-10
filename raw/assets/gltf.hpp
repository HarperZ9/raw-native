#pragma once
// glTF 2.0 import (ROADMAP M2 criterion 1): .gltf with embedded or external buffers, and
// .glb. Reads the default scene's node tree, each triangle primitive's POSITION, NORMAL
// and indices, and each material's base colour factor; meshes come out in world space.
// Every malformed input throws AssetError (raw/assets/json.hpp): offsets, strides, counts
// and indices are checked before any read, node trees are checked for cycles, and sizes
// are capped, so a hostile file cannot read out of bounds or allocate without limit.
#include "raw/assets/json.hpp"
#include "raw/scene/scene.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>
namespace raw::assets {

struct GltfMesh {
    std::vector<Vec3> positions, normals;        // world space
    std::vector<std::uint32_t> indices;          // triangles
    Vec3 baseColor{0.8f, 0.8f, 0.8f};            // linear, from baseColorFactor
    std::string name;
};

struct GltfModel {
    std::vector<GltfMesh> meshes;
    std::size_t skippedPrimitives = 0;           // primitives that are not triangles
    Vec3 boundsMin{0, 0, 0}, boundsMax{0, 0, 0};
};

// Reads a buffer named by a relative uri (an external .bin); return false when it does
// not exist. Data URIs are decoded without it.
using UriReader = std::function<bool(const std::string& uri, std::vector<std::uint8_t>& out)>;

GltfModel loadGltf(std::span<const std::uint8_t> file, const UriReader& read = {});

// The meshes added to a scene, with their base colours as albedo.
void addToScene(const GltfModel& model, Scene& scene);

}
