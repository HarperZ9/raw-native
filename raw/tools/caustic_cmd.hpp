#pragma once
// raw_native_cli caustic: effect C1, spectral caustics through measured glass (docs/hw-rt.md,
// bounds in evidence/hw-c1-caustic-bounds.json).
//   caustic render [--width W] [--height H] [--spp N] [--photons N] [--bounces B] [--seed S] [--view V]
//                  [--exposure E] [--lamp-scale K] [--cone DEG] [--bsdf-only] [--no-dispersion] [--cpu] [--out DIR]
//                  XYZ and display images, timing JSON
//   caustic contract [--quick] [--out FILE]                 K1, K2, D1 to D3 and the sweeps
//   caustic time [--width W] [--height H] [--frames N] [--out FILE]   N frames of 1 sample and 1 photon a pixel
// Exit 0 written or within every bound, 1 a bound missed or an error, 2 bad arguments, 4 no GPU backend.
#include <string>
namespace raw::rhi { class Device; }
namespace raw {
struct CausticReport { std::string json; bool pass{false}; };
CausticReport causticContract(rhi::Device& dev, bool quick);
int causticCommand(int argc, char** argv);
}
