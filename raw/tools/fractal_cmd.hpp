#pragma once
// raw_native_cli fractal-dither (evidence/fractal-dither-bounds.json):
//   fractal-dither orbit [--out FILE] [--frames N] [--px P] [--width W] [--height H]
//       f1, f2, f8: temporal instability of the fractal anchor against the screen anchor on the
//       courtyard orbit, with controls and a paired bootstrap. JSON on stdout (and FILE).
//   fractal-dither scale [--out FILE]      f3: level-change density across a 4x depth range
//   fractal-dither images --out DIR        PNGs of orbit frames 0 and 24 for review
//   fractal-dither gpu [--cost] [--out FILE]
//       f5 (and f6 with --cost): the style_ditherlight pass with anchor 3 on the linked GPU
//       backend against the float64 reference. Exit 0 pass, 1 fail, 4 no backend or adapter.
#include "raw/renderer/fractal_scene.hpp"
#include <string>
#include <vector>
namespace raw {
int fractalCommand(int argc, char** argv);

namespace fractal {
// Parts of the command, each returning the process exit code and its JSON in `json`.
int orbitReport(int frames, int w, int h, double px, std::string& json);
int scaleReport(std::string& json);
int gpuReport(bool cost, std::string& json);
}  // namespace fractal
}  // namespace raw
