#pragma once
// raw_native_cli rt-pathtrace: path-traced frames and AOVs of the owned scenes, interface v1
// of raw/renderer/rt_pathtrace.hpp, for the hardware session's references and training data
// and the shader lab's buffers.
//   rt-pathtrace [--scene retro_room|iso_street|dense_block] [--width W] [--height H]
//                [--spp N] [--seed S] [--bounces B] [--out DIR] [--cpu]
//                [--prev-eye x,y,z] [--prev-target x,y,z]
// Writes radiance.pfm and radiance.png (AgX to sRGB), albedo.pfm, normal.pfm, depth.pfm,
// motion.pfm (x, y, 0), variance.pfm, triangle.u32 (little-endian, -1 a miss as 0xffffffff)
// and pt.json (interface version, parameters, backend, time). On the GPU backend when the
// build has one and an adapter, else (or with --cpu) on the CPU reference.
// Exit 0 done, 2 bad arguments or an unwritable directory.
namespace raw {
int ptCommand(int argc, char** argv);
}
