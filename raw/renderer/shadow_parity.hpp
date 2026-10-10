#pragma once
// GPU parity of the cascaded shadow maps (evidence/m3-shadows-bounds.json): each cascade's
// hardware shadow map (raster pass shadow_depth) against raw::shadows::rasterizeCascade, the
// WGSL lookups (shadow_lookup: hard, PCF, PCSS) against raw::shadows::lookup on the same
// read-back maps and G-buffer, and the contact-shadow march (shadow_contact) against
// raw::shadows::contactShadows. Controls move one shadow caster on the GPU side.
// Backend-neutral: they run on whichever RHI device the build links.
#include "raw/renderer/shadows.hpp"
#include "raw/rhi/rhi.hpp"
#include "raw/scene/scene.hpp"
#include <string>
#include <vector>
namespace raw::gpu_check {

struct ShadowMapCase {
    int cascade{0};
    long both{0};                      // texels both sides cover with the same triangle
    long coverageDiffs{0}, explained{0};   // texels covered differently; of those, within 0.01 texel of an edge
    long depthTies{0};                 // of the differing texels, those at a depth tie (reported; not exempt)
    double worstDepth{0};              // |gpu - cpu| normalised depth where both cover the same triangle
    long depthOver{0};                 // of those texels, how many exceed 1e-5
    bool pass() const { return explained == coverageDiffs && worstDepth <= 1e-5; }   // an empty cascade passes; the scene needs coverage
};
struct ShadowSceneCase {
    std::string scene;
    std::vector<ShadowMapCase> maps;
    long points{0};                    // G-buffer pixels inside the cascades
    long perCascade[4]{0, 0, 0, 0};    // of those, in each cascade
    long outside[3]{0, 0, 0};          // hard, PCF, PCSS: points with |gpu - cpu| > 1e-3
    double worst[3]{0, 0, 0};
    long contactPixels{0}, contactDiffs{0};
    bool pass() const;
};
struct ShadowParity {
    std::string error, backend, adapter;
    int subpixelBits{0};
    std::vector<ShadowSceneCase> scenes;
    bool mapControlFails{false}, lookupControlFails{false}, contactControlFails{false};
    long mapControlUnexplained{0}, lookupControlOff{0}, contactControlDiffs{0};   // what each control changed
    bool pass() const;
    std::string json() const;
};
// Scenes are (name, scene) pairs, lit by their first light; the first scene's last mesh is the
// caster the controls move. size: the G-buffer's width and height.
ShadowParity shadowParity(rhi::Device& dev, const std::vector<std::pair<std::string, const Scene*>>& scenes, int size = 256, int mapSize = 1024);

}  // namespace raw::gpu_check
