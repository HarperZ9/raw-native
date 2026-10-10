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
}
