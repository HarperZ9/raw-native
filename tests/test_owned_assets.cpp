// The owned M3 assets against evidence/m3-assets-bounds.json: determinism, geometry, scale, the
// hero's material coverage, the hall's content and the glTF round trip, with two controls.
//   test_owned_assets [--json] [--write-hashes]
// --write-hashes rewrites evidence/m3-assets.json from this build (run once, on the producing
// compiler; every other compiler then has to match it).
#include "raw/tools/owned_assets.hpp"
#include "raw/assets/gltf.hpp"
#include "raw/core/sha256.hpp"
#include "check.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace raw;

namespace {
struct Stats {
    long triangles{0}, degenerate{0}, badNormals{0}, flipped{0};
    double area{0}, extent[3]{0, 0, 0};
    double extArea[owned::kExtCount]{}, metalArea{0}, dielectricArea{0};
};
Stats stats(const owned::Asset& a) {
    Stats s;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const owned::Part& p : a.parts) {
        for (const Vec3& n : p.normals) s.badNormals += std::fabs(double(length(n)) - 1.0) > 1e-5;
        for (const Vec3& q : p.positions) {
            const float v[3] = {q.x, q.y, q.z};
            for (int k = 0; k < 3; ++k) { lo[k] = std::fmin(lo[k], v[k]); hi[k] = std::fmax(hi[k], v[k]); }
        }
        double partArea = 0.0;
        for (std::size_t i = 0; i + 2 < p.indices.size(); i += 3) {
            const Vec3 a0 = p.positions[p.indices[i]], a1 = p.positions[p.indices[i + 1]], a2 = p.positions[p.indices[i + 2]];
            const Vec3 c = cross(a1 - a0, a2 - a0);
            const double ar = 0.5 * double(length(c));
            ++s.triangles;
            s.degenerate += ar < 1e-10;
            partArea += ar;
            for (int k = 0; k < 3; ++k) s.flipped += dot(c, p.normals[p.indices[i + std::size_t(k)]]) <= 0.0f;
        }
        s.area += partArea;
        for (int b = 0; b < owned::kExtCount; ++b) if (p.extensions & (1u << b)) s.extArea[b] += partArea;
        if (p.extensions == 0) (p.material.metallic >= 1.0 ? s.metalArea : s.dielectricArea) += partArea;
    }
    for (int k = 0; k < 3; ++k) s.extent[k] = double(hi[k]) - lo[k];
    return s;
}
int kindCount(const owned::Asset& a, const std::string& kind, long* tris = nullptr) {
    int n = 0;
    for (const owned::Part& p : a.parts) if (p.kind == kind) { n += p.instances; if (tris) *tris += long(p.indices.size() / 3); }
    return n;
}
std::string hashOf(const std::string& json, const std::vector<uint8_t>& bin) {
    return sha256Hex(json) + ":" + sha256Hex(bin.data(), bin.size());
}
std::string committedHash(const std::string& name) {
    std::ifstream f(std::string(RAW_SOURCE_DIR) + "/evidence/m3-assets.json");
    std::stringstream ss; ss << f.rdbuf();
    const std::string t = ss.str(), key = "\"" + name + "\": \"";
    const std::size_t at = t.find(key);
    if (at == std::string::npos) return {};
    const std::size_t b = at + key.size(), e = t.find('"', b);
    return t.substr(b, e - b);
}
bool roundTrip(const owned::Asset& a, const std::string& json, const std::vector<uint8_t>& bin, const Stats& s, std::string& why) {
    const std::vector<uint8_t> file(json.begin(), json.end());
    const assets::GltfModel m = assets::loadGltf(file, [&](const std::string&, std::vector<uint8_t>& out) { out = bin; return true; });
    long tris = 0;
    for (const auto& mesh : m.meshes) tris += long(mesh.indices.size() / 3);
    const double ext[3] = {double(m.boundsMax.x - m.boundsMin.x), double(m.boundsMax.y - m.boundsMin.y), double(m.boundsMax.z - m.boundsMin.z)};
    bool ok = tris == s.triangles;
    for (int k = 0; k < 3; ++k) ok = ok && std::fabs(ext[k] - s.extent[k]) <= 1e-6 * std::fmax(1.0, s.extent[k]);
    for (int b = 0; b < owned::kExtCount; ++b) {
        bool on = false;
        for (const owned::Part& p : a.parts) on = on || (p.extensions & (1u << b));
        const std::string n = owned::extName(b);
        if (on && (json.find("\"" + n + "\": {") == std::string::npos || json.find("\"extensionsUsed\": [") == std::string::npos)) { ok = false; why += n + " missing; "; }
    }
    if (tris != s.triangles) why += "triangles " + std::to_string(tris) + " vs " + std::to_string(s.triangles) + "; ";
    return ok;
}
bool heroMaterialsOk(const Stats& s) {
    bool ok = s.metalArea >= 0.02 * s.area && s.dielectricArea >= 0.02 * s.area;
    for (double v : s.extArea) ok = ok && v >= 0.02 * s.area;
    return ok;
}
}  // namespace

int main(int argc, char** argv) {
    bool json = false, write = false;
    for (int i = 1; i < argc; ++i) { json = json || !std::strcmp(argv[i], "--json"); write = write || !std::strcmp(argv[i], "--write-hashes"); }
    const owned::Asset assets[2] = {owned::hero(), owned::hall()};
    std::string rows, hashes;
    for (const owned::Asset& a : assets) {
        std::vector<uint8_t> bin;
        const std::string js = owned::exportGltf(a, bin);
        const std::string h = hashOf(js, bin), want = committedHash(a.name);
        const Stats s = stats(a);
        std::string why;
        const bool trip = roundTrip(a, js, bin, s, why);
        hashes += std::string(hashes.empty() ? "" : ",\n") + "  \"" + a.name + "\": \"" + h + "\"";
        if (!write) CHECK(h == want);
        CHECK(s.degenerate == 0 && s.badNormals == 0 && s.flipped == 0);
        CHECK(trip);
        char b[900];
        if (a.name == "raw-hero") {
            CHECK(s.triangles >= 40000);
            CHECK(heroMaterialsOk(s));
            std::string ext;
            for (int k = 0; k < owned::kExtCount; ++k) ext += (k ? ", " : "") + std::string("\"") + owned::extName(k) + "\": " + std::to_string(s.extArea[k] / s.area);
            std::snprintf(b, sizeof b, "  {\"asset\": \"%s\", \"triangles\": %ld, \"degenerate\": %ld, \"bad_normals\": %ld, \"flipped\": %ld, \"hash_matches\": %s, \"round_trip\": %s,"
                          " \"area_fraction\": {%s, \"plain_metal\": %.4f, \"plain_dielectric\": %.4f}}",
                          a.name.c_str(), s.triangles, s.degenerate, s.badNormals, s.flipped, h == want ? "true" : "false", trip ? "true" : "false", ext.c_str(),
                          s.metalArea / s.area, s.dielectricArea / s.area);
        } else {
            long drapeTris = 0;
            const int cols = kindCount(a, "column"), arches = kindCount(a, "arch"), drapes = kindCount(a, "drapery", &drapeTris), lanterns = kindCount(a, "lantern");
            CHECK(s.triangles >= 250000);
            CHECK(s.extent[0] >= 30.0 && s.extent[1] >= 14.0 && s.extent[2] >= 12.0);
            CHECK(cols >= 24 && arches >= 16 && drapes >= 6 && drapeTris >= 10000 && lanterns >= 8);
            std::snprintf(b, sizeof b, "  {\"asset\": \"%s\", \"triangles\": %ld, \"degenerate\": %ld, \"bad_normals\": %ld, \"flipped\": %ld, \"hash_matches\": %s, \"round_trip\": %s,"
                          " \"extent\": [%.3f, %.3f, %.3f], \"columns\": %d, \"arches\": %d, \"drapery_panels\": %d, \"drapery_triangles\": %ld, \"lanterns\": %d}",
                          a.name.c_str(), s.triangles, s.degenerate, s.badNormals, s.flipped, h == want ? "true" : "false", trip ? "true" : "false",
                          s.extent[0], s.extent[1], s.extent[2], cols, arches, drapes, drapeTris, lanterns);
        }
        rows += std::string(rows.empty() ? "" : ",\n") + b;
        // Control: one byte changed in the export.
        if (a.name == "raw-hero") {
            std::vector<uint8_t> mutated = bin;
            mutated[mutated.size() / 2] ^= 1;
            CHECK(hashOf(js, mutated) != want);
        }
    }
    // Control: the hero without its visor (the only transmission part).
    owned::Asset noVisor = owned::hero();
    for (std::size_t i = 0; i < noVisor.parts.size(); ++i) if (noVisor.parts[i].name == "visor") noVisor.parts.erase(noVisor.parts.begin() + std::ptrdiff_t(i));
    const bool ctl = heroMaterialsOk(stats(noVisor));
    CHECK(!ctl);
    if (write) {
        std::ofstream f(std::string(RAW_SOURCE_DIR) + "/evidence/m3-assets.json", std::ios::binary);
        f << "{\n \"schema\": \"raw-native.evidence/1\",\n \"what\": \"SHA-256 of each owned asset's glTF JSON and binary buffer (json:bin), as test_owned_assets --write-hashes wrote them\",\n \"hashes\": {\n" << hashes << "\n }\n}\n";
    }
    std::printf("{\n \"assets\": [\n%s\n ],\n \"control_no_visor_passes\": %s,\n \"failures\": %d\n}\n", rows.c_str(), ctl ? "true" : "false", raw_test_failures());
    return json ? (raw_test_failures() ? 1 : 0) : raw_test_summary();
}
