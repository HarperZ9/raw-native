#pragma once
// raw_native_cli raster-identity: RHI version 3's checks (raw/renderer/raster_identity.hpp)
// on this build's GPU backend.
//   raster-identity [--size N] [--models DIR] [--out FILE]
// Scenes: the built-in test scene, and with --models (a glTF-Sample-Assets checkout) the
// M2 models Suzanne and SciFiHelmet. JSON on stdout (and in FILE); exit 0 pass, 1 fail,
// 2 bad arguments or an unreadable model, 4 without a GPU backend or adapter.
namespace raw {
int rasterIdentityCommand(int argc, char** argv);
}
