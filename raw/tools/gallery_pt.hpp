#pragma once
// The material gallery's error against a path-traced reference (ROADMAP M3 criterion 1; check
// c8 of evidence/rt-v3-bounds.json). The gallery's spheres, lights, sky cube and materials are
// rebuilt as a path tracer v3 scene (each sphere tessellated nu x nv), traced on the CPU at the
// gallery's size and camera, and compared with the float64 raster frame in luminance over the
// pixels the raster covers fully and the tracer hits at the centre. envScale multiplies the
// tracer's environment: pi is the false control.
#include "raw/renderer/gallery.hpp"
#include <string>
#include <vector>
namespace raw::gallery {

struct PtErrorDesc { int spp{64}, bounces{8}, nu{128}, nv{64}, threads{0}, splitStride{32}, splitN{1024}; double envScale{1.0}; };
// cpuPerSample: shadeCpu's exposed radiance per sample. Returns the JSON record; ptImage gets the
// tracer's exposed frame. A diagnostic beside it, per row: the split sum's own error (shadeIbl
// against shadeIblReference on every splitStride-th sample, no occlusion, no interreflection),
// which separates the raster's image-lighting model from the light transport it leaves out.
std::string ptError(const Gallery& g, const std::vector<Rgb>& cpuPerSample, const pbr::Tables& t, const lighting::Cube& prefiltered,
                    const PtErrorDesc& d, std::vector<Rgb>& ptImage);

}  // namespace raw::gallery
