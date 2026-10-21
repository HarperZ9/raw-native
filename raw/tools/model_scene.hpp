#pragma once
// The scene a run renders: the built-in test scene, or with --model a glTF model in place
// of its box (ROADMAP M2 criterion 1). The model is scaled so its largest extent is 2
// units, centred over the origin and set on the ground plane, so the default camera frames
// it and the AO check has contact shadows to measure. External buffers are read from the
// model's directory. A malformed file throws AssetError (raw/assets/json.hpp).
#include "raw/scene/scene.hpp"
#include "raw/tools/cli_params.hpp"
namespace raw {
Scene sceneFromParams(const CliParams& p, Arena* arena);
// The control scene of evidence/m3-verified-scenes-bounds.json: one UV sphere (radius 1, 64 x 48
// quads, albedo 0.6) alone under the test scene's light. Convex and alone, so the raster's
// ambient-times-AO and unshadowed sun are exact there.
Scene controlSphereScene();
// raw_native_cli pt-error: the rasterised frame against a path-traced reference (src/tools/pt_error_cmd.cpp).
int ptErrorCommand(int argc, char** argv);
// raw_native_cli restir-di: ReSTIR DI stage 1 on a scene with scattered point lights (src/tools/restir_cmd.cpp).
int restirCommand(int argc, char** argv);
}
