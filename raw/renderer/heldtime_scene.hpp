#pragma once
// The held-time demonstration scene (PROPOSALS P1 and P2; evidence/heldtime-loop-bounds.json): a
// capsule figure on a 10-joint skeleton (raw/scene/skin.hpp) dances a 4 s looping clip
// (raw/scene/clip.hpp) beside the still life's torus and box, drawn by the curvature hatching pass
// (style_hatch) with boil. Three layers share one held-time clock:
//   character  on ones, twos or threes (or continuous, the unheld control); its pose buffer is the
//              joint positions plus skinned vertex positions, and its boil seed hashes that buffer
//   camera     continuous (on ones at any render rate), a periodic arc
//   effects    a named 12 Hz candle flicker on the light; its pose is its tick within the loop
// Every term reads the raw frame index and reduces by its own period, so the loop's frame N against
// frame 0 check measures closure instead of assuming it.
#include "raw/scene/clip.hpp"
#include "raw/scene/held_time.hpp"
#include "raw/scene/loop.hpp"
#include "raw/scene/skin.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::heldtime {

anim::Clip demoClip();                 // 4 s, 6 channels, loops
const anim::Skeleton& demoSkeleton();
const anim::SkinMesh& demoMesh();      // points along the bones, two influences each

struct Figure { std::vector<std::array<double, 3>> joints; std::vector<float> poseBuffer; };
// The figure at a clip time: world joint positions and the pose buffer (joints as float32, then
// the skinned vertex positions).
Figure figureAt(double clipTime);

struct DemoSpec {
    anim::Rate character{anim::onN(2)};
    anim::LoopSpec loop{96, {24, 1}};
    std::int64_t cameraPeriod{96};    // frames; the open-camera control sets 97
    double boil{0.35};                // line periods; 0 turns boil off
    std::string preset{"ink"};
    int w{480}, h{270};
};
struct DemoFrame {
    std::vector<std::uint8_t> rgb;    // w * h * 3, sRGB
    std::string poseDigest;           // SHA-256 of the character's pose buffer
    std::uint32_t boilSeed{0}, fxSeed{0};
    std::int64_t tick{0};
};
// The character's held pose at a render frame (no rendering): the pose digest, boil seed and tick.
DemoFrame characterPose(const DemoSpec& s, std::int64_t frame);
std::array<double, 3> cameraEye(const DemoSpec& s, std::int64_t frame);
anim::Layer characterLayer(const DemoSpec& s);
anim::Layer cameraLayer();
anim::Layer effectsLayer();
DemoFrame renderDemo(const DemoSpec& s, std::int64_t frame);
// The scene's animated terms for checkLoop: character, camera, effects.
std::vector<anim::LoopTerm> demoTerms(const DemoSpec& s);

}  // namespace raw::heldtime
