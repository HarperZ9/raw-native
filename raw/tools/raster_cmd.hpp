#pragma once
// raw_native_cli raster-identity: RHI version 3's checks (raw/renderer/raster_identity.hpp)
// on this build's GPU backend.
//   raster-identity [--size N] [--out FILE]
// Scenes: the built-in test scene, raw-hero in it and raw-hall from its nave (owned assets).
// JSON on stdout (and in FILE); exit 0 pass, 1 fail,
// 2 bad arguments or an unreadable model, 4 without a GPU backend or adapter.
namespace raw {
int rasterIdentityCommand(int argc, char** argv);
}
// raw_native_cli shadow-parity: the cascaded shadow maps' GPU checks
// (raw/renderer/shadow_parity.hpp) on this build's GPU backend.
//   shadow-parity [--size N] [--out FILE]
// Scenes: the built-in test scene from its own camera and from a near one, raw-hero in the test
// scene and raw-hall from its nave (owned assets), each lit by the shadow checks' sun (evidence/m3-shadows-bounds.json, method note). Same
// output and exit codes as raster-identity.
namespace raw {
int shadowParityCommand(int argc, char** argv);
}
// raw_native_cli post-parity: GTAO, SSR and the TAA resolve on this build's GPU backend against
// the CPU (raw/renderer/post_parity.hpp).
//   post-parity [--out FILE]
// Scenes as test_post: the built-in test scene from two cameras, raw-hero in it, and raw-hall
// from its nave and its gallery. Same output and exit codes as raster-identity.
namespace raw {
int postParityCommand(int argc, char** argv);
}
