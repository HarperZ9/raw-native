// The M3 model manifest: see raw/tools/model_manifest.hpp.
#include "raw/tools/model_manifest.hpp"
#include "raw/assets/json.hpp"
#include <fstream>
#include <iterator>
namespace raw {
namespace {
bool load(const std::string& path, assets::Json& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot read the model manifest " + path; return false; }
    const std::string text{std::istreambuf_iterator<char>(f), {}};
    try { out = assets::parseJson(text); }
    catch (const assets::AssetError& e) { err = "model manifest " + path + ": " + e.what(); return false; }
    return true;
}
}  // namespace

std::vector<std::string> modelRoles(const std::string& manifestPath, std::string& err) {
    std::vector<std::string> out;
    assets::Json j;
    if (!load(manifestPath, j, err)) return out;
    const assets::Json* roles = j.get("roles");
    if (!roles || !roles->is(assets::Json::Kind::Array)) { err = "model manifest has no roles"; return out; }
    for (const auto& r : roles->a) if (const assets::Json* n = r.get("role")) out.push_back(n->s);
    return out;
}

std::string resolveModelRole(const std::string& manifestPath, const std::string& role, const std::string& modelsDir, std::string& err) {
    assets::Json j;
    if (!load(manifestPath, j, err)) return {};
    const assets::Json* roles = j.get("roles");
    if (!roles || !roles->is(assets::Json::Kind::Array)) { err = "model manifest has no roles"; return {}; }
    for (const auto& r : roles->a) {
        const assets::Json* name = r.get("role");
        if (!name || name->s != role) continue;
        const assets::Json* use = r.get("use");
        const assets::Json* cands = r.get("candidates");
        if (!use || !cands) break;
        for (const auto& c : cands->a) {
            const assets::Json* cn = c.get("name");
            const assets::Json* main = c.get("main");
            if (cn && main && cn->s == use->s) return modelsDir + "/" + main->s;
        }
        err = "role " + role + " uses " + use->s + ", which is not among its candidates";
        return {};
    }
    if (err.empty()) err = "no role " + role + " in " + manifestPath;
    return {};
}
}  // namespace raw
