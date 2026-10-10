#pragma once
// raw_native_cli material-gallery: the M3 showcase (raw/renderer/gallery.hpp).
//   material-gallery [--out DIR] [--size N] [--spp K] [--cpu-only]
// Writes gallery-cpu.png and .pfm from the float64 reference and, when the build has a GPU
// backend and an adapter, gallery-gpu.png and .pfm from the pbr_shade pass, plus
// gallery.json with the timings and the GPU's difference from the reference.
// Exit 0 done, 2 bad arguments or an unwritable directory.
namespace raw {
int galleryCommand(int argc, char** argv);
}
