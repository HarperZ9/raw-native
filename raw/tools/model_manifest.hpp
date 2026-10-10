#pragma once
// The M3 model manifest (evidence/m3-scene-models.json): models by role, each role naming the
// candidate in use, so one field swaps a model (FlightHelmet or DamagedHelmet, a Sponza-class
// stand-in or Sponza) for every fetch, render and check.
#include <string>
#include <vector>
namespace raw {
// The main .gltf of the candidate in use for `role`, joined to `modelsDir` (a checkout or a
// fetch_gltf.py directory). Empty with `err` set when the manifest or the role is missing.
std::string resolveModelRole(const std::string& manifestPath, const std::string& role, const std::string& modelsDir, std::string& err);
// Every role in the manifest, in order.
std::vector<std::string> modelRoles(const std::string& manifestPath, std::string& err);
}
