#pragma once
// Perspective-stable pixel art, the C++ core's reference (ADR 0019) of web/shaders/pixel
// (scene.mjs, probe.mjs, splat.mjs, pixel.mjs), after Ebert, "Texel Splatting: Perspective-Stable
// 3D Pixel Art" (arXiv 2603.14587), reimplemented from the paper as the web module is. A
// raymarched diorama is captured into a cubemap probe at the camera position snapped to a world
// grid; each texel is shaded once (posterised OKLab, outlines and creases), splatted into the view
// as a world-space quad (nearest depth key, then lowest texel index), and pixels no texel reaches
// are filled by an eye ray. On a cell change the previous probe is kept and the view crossfades
// with a 4 x 4 Bayer threshold. The JavaScript stores the probe tables and the image as f32;
// so does this. The GPU passes are style_pixel_* in style.wgsl.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace raw::style {

using PV3 = std::array<double, 3>;
// The diorama: distance, material and object at a point; the march (t, material, object; hit false
// on a miss); the normal; camera-independent shading; the sky.
struct SceneHit { bool hit{false}; double t{0}; int mat{-1}, obj{-1}; };
std::array<double, 3> pixelSceneMap(double x, double y, double z);
SceneHit pixelMarch(const PV3& o, const PV3& r, double tMax = 40);
PV3 pixelSceneNormal(const PV3& p);
PV3 pixelShadePoint(const PV3& p, const PV3& n, int mat);
PV3 pixelSky(const PV3& r);
PV3 pixelStylise(const PV3& rgb, double bands, double dL);   // posterised OKLab lightness

struct PixelOptions { int N{128}; double cell{0.5}, bands{6}, outline{0.22}, crease{0.08}, expand{1.15}, fadeFrames{12}, fadeCells{0.15}; double outlineRatio{1.03}; };
struct PixelProbe { PV3 origin{}; int N{0}; std::vector<float> depth, nrm; std::vector<std::int32_t> mat, obj; std::vector<float> col; };
struct PixelCamera { PV3 eye{}, f{}, r{}, u{}; double ty{0}; int w{0}, h{0}; };
struct PixelVis { std::vector<float> zbuf; std::vector<std::uint32_t> idb; };
PixelCamera pixelCamera(const PV3& eye, const PV3& target, double fovy, int w, int h);
PixelProbe pixelCaptureProbe(const PV3& origin, const PixelOptions& o);   // captured and shaded
PixelVis pixelSplat(const PixelProbe& pr, const PixelCamera& cam, double expand);
PV3 pixelFaceDir(int f, double u, double v);
PV3 pixelSnapOrigin(const PV3& eye, double cell);

// The frame loop (createPixel): call frame for each view in order.
class PixelRunner {
public:
    PixelRunner(const PixelOptions& o, int w, int h) : o_(o), w_(w), h_(h) {}
    struct Frame { std::vector<float> img; double blend{1}; const PixelProbe* probe{nullptr}; PixelVis vis; PixelCamera cam; bool hasPrev{false}; const PixelProbe* prev{nullptr}; PixelVis prevVis; };
    Frame frame(const PV3& eye, const PV3& target, double fovy);
private:
    PixelOptions o_;
    int w_, h_;
    std::unique_ptr<PixelProbe> cur_, prev_;
    double fade_{1};
    bool haveLast_{false};
    PV3 lastEye_{};
};
std::uint8_t pixelEncode8(double v);   // encodePixel8: clamp, sRGB, round half up

}  // namespace raw::style
