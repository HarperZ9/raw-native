#pragma once
// Screen-space ambient occlusion (GTAO) and screen-space reflections (SSR), as the CPU
// references of the GPU passes, and their ray-cast references (ROADMAP M3 dated addition 8;
// bounds in evidence/m3-post-bounds.json). Everything works on a view-space G-buffer: the
// camera looks along -z, and the view depth of a pixel is -z.
#include "raw/renderer/gbuffer.hpp"
#include "raw/scene/scene.hpp"
#include <cstdint>
#include <vector>
namespace raw::post {

struct ViewGBuffer {
    int w{0}, h{0};
    double tanHalf{0.5}, aspect{1.0};     // tan(fovy / 2) and the aspect ratio
    double nearZ{0.1};
    std::vector<float> pos, nrm;          // view-space position and unit normal, 3 floats a pixel
    std::vector<float> depth;             // view depth, negative where nothing was drawn
    bool valid(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && depth[std::size_t(y) * w + x] >= 0.0f; }
};
// The view-space form of a G-buffer drawn with RasterOptions::perspectiveDepth.
ViewGBuffer viewGBuffer(const GBuffer& g, const Camera& cam);
// Screen position (pixels, y down) of a view-space point; false behind the camera.
bool project(const ViewGBuffer& v, const double p[3], double& sx, double& sy);

struct GtaoParams { double radius{0.5}; int slices{16}, steps{16}; };
// GTAO visibility per pixel (1 where nothing was drawn).
std::vector<double> gtao(const ViewGBuffer& v, const GtaoParams& p = {});
// Ray-cast cosine-weighted visibility within `radius`, sqrtRays^2 stratified rays a pixel.
std::vector<double> rayAo(const Scene& s, const GBuffer& g, double radius, int sqrtRays = 32);

struct SsrParams {
    double maxDistance{20.0}, thickness{0.2};
    int steps{64}, refine{8};
    bool viewNormal{false};   // control only: reflect about the view vector instead of the normal
};
// SSR per pixel: the hit pixel's index (y * w + x), or -1 for a miss or an empty pixel.
std::vector<int> ssr(const ViewGBuffer& v, const SsrParams& p = {});
// The mirror ray's first hit in world space per pixel; hit[i] false for a miss.
void rayMirror(const Scene& s, const GBuffer& g, std::vector<Vec3>& at, std::vector<uint8_t>& hit);

}  // namespace raw::post
