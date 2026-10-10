#pragma once
// raw_native_cli sdf-render: the owned SDF scenes (RT stage R3) from the GPU ray marcher.
//   sdf-render [--out DIR] [--width W] [--height H]
// For garden, menger_tower, bulb and arcade writes <scene>.png (sun with soft shadows, sky
// ambient with AO, fog and god rays; the colours are showcase choices) and the AOVs the shader
// lab reads: <scene>-albedo.pfm, -normal.pfm, -depth.pfm (forward-axis distance, 0 a miss) and
// -material.pfm (material id, -1 a miss). Exit 0 done, 2 bad arguments, 4 no GPU backend.
namespace raw {
int sdfRenderCommand(int argc, char** argv);
}
