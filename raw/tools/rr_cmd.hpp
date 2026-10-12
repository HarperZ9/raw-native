#pragma once
// raw_native_cli rr-render: HW H2 dataset frames from RT's path tracer on an owned scene along a
// camera path (evidence/rr-bounds.json, evidence/rr-bounds-amendment-1.json).
//   rr-render --scene NAME --path SEED --frames N [--first K] [--count C] [--width W] [--height H]
//             [--spp S] [--chunk C] [--out DIR] [--cpu]
// Per frame k, in DIR: fKKK_in.f32 (H x W x 17 float32: 1 spp radiance 3, albedo 3, normal 3,
// depth 1, motion 2, previous position 2, expected previous depth 1, roughness 1, metallic 1),
// fKKK_tri.i32 (H x W), fKKK_ref.f32 (H x W x 7: S spp radiance 3, variance of the mean of
// luminance 1, albedo 3) and fKKK.json (cameras, seed, timings, noise statistics). The input
// uses sample 0 and the reference samples 1 to S of the same counter-based seed.
// Exit 0 done, 2 bad arguments or a write failure, 4 no GPU (without --cpu).
namespace raw {
int rrRenderCommand(int argc, char** argv);
}
