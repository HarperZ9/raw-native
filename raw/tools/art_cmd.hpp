#pragma once
// raw_native_cli art <feature>: the stylised track's checks and records (P5, P16 to P21 in
// docs/architecture/ROADMAP.md), each against its bounds file in evidence/.
//   art strokes [--out DIR] [--threads N] [--frames N] [--no-orbit] [--gpu] [--cost] [--repeats R]
// --gpu runs the GPU parity cases on this build's backend; --cost times the passes at 1920 x 1080.
// Writes DIR/<feature>.json and the golden PNGs. Exit 0 when every gated check passes, 1 when one
// fails, 2 on bad arguments, 4 when the GPU was asked for and none is available.
namespace raw {
int artCommand(int argc, char** argv);
}
