// The M3 model manifest resolves each role to its candidate in use, and one changed field
// swaps the model (raw/tools/model_manifest.hpp, evidence/m3-scene-models.json).
#include "raw/tools/model_manifest.hpp"
#include "raw/tools/cli_params.hpp"
#include "check.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
using namespace raw;

int main() {
    const std::string manifest = std::string(RAW_SOURCE_DIR) + "/evidence/m3-scene-models.json";
    std::string err;
    const auto roles = modelRoles(manifest, err);
    CHECK(err.empty() && roles.size() == 2);
    CHECK(resolveModelRole(manifest, "helmet", "M", err) == "M/raw-hero.gltf");
    CHECK(resolveModelRole(manifest, "interior", "M", err) == "M/raw-hall.gltf");
    // The swap: one changed 'use' field changes what the role resolves to (here to a candidate the
    // role does not list, which must be refused with an error, never resolved silently).
    std::ifstream f(manifest, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(f), {}};
    const std::string from = "\"use\": \"raw-hero\"", to = "\"use\": \"raw-hero-next\"";
    const auto at = text.find(from);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    const std::string swapped = "test_model_manifest_swapped.json";
    std::ofstream(swapped, std::ios::binary) << text;
    std::string e2;
    CHECK(resolveModelRole(swapped, "helmet", "M", e2).empty() && e2.find("raw-hero-next") != std::string::npos);
    std::string e3;
    CHECK(resolveModelRole(manifest, "no-such-role", "M", e3).empty() && !e3.empty());
    std::remove(swapped.c_str());
    // The CLI refuses a role without a models directory.
    const char* argv[] = {"cli", "--model-role", "helmet"};
    std::string e4;
    CHECK(!parseArgs(3, argv, e4) && e4.find("--models") != std::string::npos);
    return raw_test_summary();
}
