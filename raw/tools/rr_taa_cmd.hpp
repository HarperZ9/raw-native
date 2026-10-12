#pragma once
// raw_native_cli rr-taa: M3's TAA resolve (raw/renderer/taa.hpp, called unchanged) over a sequence of
// filtered frames, the "SVGF plus TAA" baseline of HW H2 (evidence/rr-bounds-amendment-1.json).
//   rr-taa --seq DIR --in DIR --out DIR
// --seq: rr-render frames (depth and previous positions from fKKK_in.f32, size from fKKK.json);
// --in: fKKK.f32 colour frames (H x W x 3 float32, linear); --out: the resolved frames, same form.
// Exit 0 done, 2 bad arguments or I/O failure.
namespace raw {
int rrTaaCommand(int argc, char** argv);
}
