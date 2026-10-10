#pragma once
// raw_native_cli bake-room: the Source-style baked lighting showcase (raw/renderer/bake.hpp).
//   bake-room [--out DIR] [--size N] [--spp K] [--paths P]
// Bakes the test room's radiosity normal maps, then draws the room from inside twice: from the
// lightmaps (room-rnm) and by path tracing every pixel (room-reference), each as PNG and PFM,
// with room.json holding the bake time and the luminance difference. Walls carry a procedural
// relief so the normal maps have something to shade. Exit 0 done, 2 bad arguments.
namespace raw {
int bakeCommand(int argc, char** argv);
}
