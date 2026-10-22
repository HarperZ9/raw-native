#pragma once
// The stylised track's scene: an owned analytic still life ray cast in float64, the shared input of
// every art pass (P5, P16 to P21 in docs/architecture/ROADMAP.md). A ground plane, a back wall and a
// plinth (boxes), a large and a small sphere, and a column (a capped cylinder), each with an object
// id and a material id; a shadowed sun, an optional shadowed spot lamp, a sky gradient and a
// hemispherical ambient. The lit radiance is the mean of 2 x 2 samples a pixel; the G-buffer and the
// light terms are taken at the pixel centre. Stored as f32, like the style passes' buffers.
#include <array>
#include <cstdint>
#include <vector>
namespace raw::art {

using D3 = std::array<double, 3>;

struct Sun { D3 dir{0, 1, 0}; D3 colour{0, 0, 0}; };   // dir points toward the light
struct Lamp { D3 pos{0, 0, 0}, axis{0, -1, 0}; double cosOuter{0.8}, cosInner{0.9}; D3 colour{0, 0, 0}; bool on{false}; };
struct Lighting { Sun sun; Lamp lamp; D3 skyTop, skyHorizon, ground; };
Lighting daylight();
Lighting dusk();
Lighting night();

struct View {
    D3 eye{0, 0, 5}, target{0, 0, 0};
    double fovy{0.75};
    int w{0}, h{0};
    double focalPx() const;   // the focal length in pixels at the frame's height
};
// Frame `frame` of the P5 orbit: target (0, 1, 0), radius 8.5, eye height 4.2, azimuth 20 degrees
// plus `step` degrees a frame.
View orbitView(int frame, int w, int h, double step = 1.0);
inline constexpr double kOrbitRadius = 8.5;

struct Frame {
    int w{0}, h{0};
    std::vector<float> P, N, albedo, dir;   // 3 a pixel: world point, normal, albedo, view ray
    std::vector<float> dist;                // distance from the eye; -1 for sky
    std::vector<float> sun, lamp, amb;      // irradiance terms at the pixel centre (sun and lamp shadowed)
    std::vector<float> lit;                 // RGBA scene-linear radiance, 2 x 2 samples a pixel
    std::vector<std::uint8_t> obj, mat;     // 0 for sky
};
Frame renderFrame(const View& v, const Lighting& L, int threads);

// Motion to the previous view: 2 floats a pixel, current minus previous position in pixels (the
// paint canvas's convention: the previous position of pixel x is x - mv). Surface points are
// reprojected; sky pixels by their direction.
std::vector<float> motionTo(const Frame& cur, const View& prev);
// History valid: the reprojected pixel lies inside the previous frame, shows the same object and
// its distance agrees within 2%.
std::vector<std::uint8_t> historyValid(const Frame& cur, const Frame& prev, const View& prevView, const std::vector<float>& mv);

// The ray caster's pieces, shared by the scene and its tests.
struct Hit { double t{-1}; D3 n{0, 0, 0}; int obj{0}, mat{0}; D3 albedo{0, 0, 0}; };
Hit trace(const D3& o, const D3& d);
bool occluded(const D3& o, const D3& d, double tMax);
D3 rayDir(const View& v, double px, double py);   // px, py in pixels from the top-left corner

}  // namespace raw::art
