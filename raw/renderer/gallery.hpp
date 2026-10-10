#pragma once
// The material gallery (ROADMAP M3 showcase): a grid of spheres, one material family a row
// and one parameter swept along the columns, lit by three lights in physical units and the
// procedural sky. Every pixel is a lighting::Sample, so the float64 reference and the GPU
// shading pass (pbr_shade) draw the same picture from the same inputs.
//
// Rows, top to bottom: gold (roughness), red plastic (roughness), car paint (clearcoat
// roughness), velvet (sheen roughness), brushed steel (anisotropy), thin film (iridescence
// thickness), glass (roughness, thin-walled on the left half, volume on the right).
#include "raw/renderer/lighting.hpp"
#include <string>
#include <vector>
namespace raw::gallery {

using lighting::Rgb;

inline constexpr int kRows = 7, kCols = 7;
inline constexpr double kEv100 = 11.0;

struct Gallery {
    int width{0}, height{0}, spp{1};               // spp: samples per pixel along each axis (spp x spp)
    lighting::ClusterGrid grid;
    std::vector<lighting::Light> lights;
    lighting::Cube env;                             // level 0 filled, the rest left to a prefilter
    std::vector<Rgb> sh;
    std::vector<lighting::Sample> samples;          // the sphere hits
    std::vector<int> pixel;                         // pixel index of each sample
    std::vector<Rgb> background;                    // exposed sky radiance per pixel (misses)
    std::vector<int> hits;                          // samples that landed in each pixel
    double exposure{1};
};
Gallery build(int width, int height, int spp);
// Material of the sphere at row, column (also what the media pages label).
pbr::Material sphereMaterial(int row, int col, std::string* label = nullptr);
// Exposed linear radiance per pixel from per-sample exposed radiance (sphere hits and the
// sky averaged over the pixel's sub-samples).
std::vector<Rgb> resolve(const Gallery& g, const std::vector<Rgb>& perSample);
// The float64 reference: exposed radiance per sample.
std::vector<Rgb> shadeCpu(const Gallery& g, const pbr::Tables& t, const lighting::Cube& prefiltered, int threads);

}  // namespace raw::gallery
