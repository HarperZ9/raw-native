#pragma once
// The courtyard of evidence/fractal-dither-bounds.json: an owned analytic scene ray cast in
// float64 into a ditherlight G-buffer, with true motion vectors along a camera orbit. Walls,
// ground, a plinth, two spheres and a column; two radial lamps with shadow rays.
#include "raw/renderer/style_ditherlight.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace raw::fractal {

struct Camera { style::V3 eye, target; double fov{0.75}; };
// Orbit frame t: target (0, 1, 0), radius 8.5, eye height 4.2, azimuth (20 + t) degrees.
Camera orbitCamera(int frame, double radius = 8.5);

struct Frame {
    int w{0}, h{0};
    Camera cam;
    style::DitherGBuffer g;
    std::vector<std::uint8_t> id;   // object id, 0 for sky
};
Frame renderGBuffer(const Camera& cam, int w, int h);
std::vector<style::DitherLight> lights();
// An orbit of `frames` G-buffers at w x h.
std::vector<Frame> orbitFrames(int frames, int w, int h);
// The ditherlight plan of a preset on the courtyard with the given anchor and pixels a cell (cell 0.1).
style::DitherPlan orbitPlan(const std::string& preset, style::Anchor anchor, int h, double px);

struct Motion {
    std::vector<float> mv;               // current minus previous pixel position, 2 floats a pixel
    std::vector<std::uint8_t> valid;     // surface, inside the previous frame, same object, depth within 2%
};
Motion motion(const Frame& cur, const Frame& prev);

}  // namespace raw::fractal
