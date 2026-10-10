#pragma once
// raw_native_cli swr-render: the software rasterizer's owned scenes as images (RT stage R1).
//   swr-render [--out DIR] [--cpu-only]
// For each owned scene (retro_room, iso_street, clip_stress) writes, from the CPU reference:
//   <scene>-modern.png     640 x 480, perspective-correct, float depth
//   <scene>-retro.png      320 x 240, whole-pixel vertex snap, affine UVs, 12-bit dithered depth
//   <scene>-ids.png        640 x 480, the visibility buffer's triangle ids as colours
// and, with a GPU backend and an adapter, the same frames from the compute rasterizer
// (<scene>-modern-gpu.png, <scene>-retro-gpu.png) with their pixel differences in swr.json, and
//   <scene>-hybrid.png     640 x 480, retro_room and iso_street: the GPU raster with ray-traced
//                          sun shadows and reflections (RT stage R2)
// Exit 0 done, 2 bad arguments or an unwritable directory.
namespace raw {
int swrRenderCommand(int argc, char** argv);
}
