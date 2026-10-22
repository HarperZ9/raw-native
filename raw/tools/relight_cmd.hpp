#pragma once
// raw_native_cli envmaps and relight: the engine's own baked environments and the relighting page
// (src/tools/relight_cmd.cpp; evidence/sky-envmaps-bounds.json).
#include "raw/renderer/lighting.hpp"
#include <string>
#include <vector>
namespace raw {
int envmapsCommand(int argc, char** argv);
int relightCommand(int argc, char** argv);
}
