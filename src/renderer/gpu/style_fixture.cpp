// Fixture bundles of the style checks: see style_fixture.hpp.
#include "style_fixture.hpp"
#include <fstream>
#include <iterator>
namespace raw::gpu_check::style_fx {
using assets::Json;

bool readBytes(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
bool loadBundle(const std::string& path, Bundle& b, std::string& err) {
    if (!readBytes(path, b.bytes)) { err = "cannot read " + path; return false; }
    if (b.bytes.size() < 12 || std::memcmp(b.bytes.data(), "RNFX0001", 8) != 0) { err = path + " is not an RNFX0001 bundle"; return false; }
    const std::uint32_t len = std::uint32_t(b.bytes[8]) | (std::uint32_t(b.bytes[9]) << 8) | (std::uint32_t(b.bytes[10]) << 16) | (std::uint32_t(b.bytes[11]) << 24);
    if (12 + std::size_t(len) > b.bytes.size()) { err = path + ": header runs past the file"; return false; }
    b.head = assets::parseJson(std::string_view(reinterpret_cast<const char*>(b.bytes.data()) + 12, len));
    b.base = 12 + std::size_t(len);
    b.base += (8 - b.base % 8) % 8;
    return true;
}

style::V3 v3(const Json& a) { return {a.at(0).number("x"), a.at(1).number("y"), a.at(2).number("z")}; }
static double num(const Json& j, const char* k) {
    const Json* v = j.get(k);
    if (!v) throw assets::AssetError(std::string("fixture: missing ") + k);
    return v->number(k);
}

style::DitherLight light(const Json& j) {
    style::DitherLight L;
    L.pos = v3(*j.get("pos"));
    L.intensity = num(j, "intensity"); L.wrap = num(j, "wrap");
    if (j.get("kind")->s == "radial") { L.kind = style::DitherLight::Kind::Radial; L.radius = num(j, "radius"); L.core = num(j, "core"); return L; }
    L.kind = style::DitherLight::Kind::Beam;
    L.dir = v3(*j.get("dir")); L.tan = v3(*j.get("tan")); L.bit = v3(*j.get("bit"));
    L.outer = num(j, "outer"); L.inner = num(j, "inner"); L.range = num(j, "range");
    L.slats = num(j, "slats"); L.duty = num(j, "duty"); L.phase = num(j, "phase");
    return L;
}

static style::Source source(const std::string& s) {
    if (s == "bayer8") return style::Source::Bayer8;
    if (s == "blue64") return style::Source::Blue64;
    throw assets::AssetError("fixture: unknown threshold source " + s);
}
static style::Anchor anchor(const std::string& s) {
    if (s == "screen") return style::Anchor::Screen;
    if (s == "world-fixed") return style::Anchor::WorldFixed;
    if (s == "world-octave") return style::Anchor::WorldOctave;
    throw assets::AssetError("fixture: unknown anchor " + s);
}
// Applies the plan fields present in j (a whole plan or a case's overrides).
static void apply(style::DitherPlan& p, const Json& j) {
    if (const Json* v = j.get("levels")) p.levels = int(v->integer("levels", 2, 256));
    if (const Json* v = j.get("ink")) p.ink = v3(*v);
    if (const Json* v = j.get("paper")) p.paper = v3(*v);
    if (const Json* v = j.get("source")) p.source = source(v->s);
    if (const Json* v = j.get("anchor")) p.anchor = anchor(v->s);
    const std::pair<const char*, double style::DitherPlan::*> nums[] = {
        {"cell", &style::DitherPlan::cell}, {"px", &style::DitherPlan::px}, {"pa", &style::DitherPlan::pa},
        {"gamma", &style::DitherPlan::gamma}, {"dark", &style::DitherPlan::dark}, {"hemi", &style::DitherPlan::hemi},
        {"fogDist", &style::DitherPlan::fogDist}, {"fogMax", &style::DitherPlan::fogMax}};
    for (const auto& [k, m] : nums) if (const Json* v = j.get(k)) p.*m = v->number(k);
}
style::DitherPlan plan(const Json& j) { style::DitherPlan p; apply(p, j); return p; }
style::DitherPlan planFromPreset(const std::string& preset, const Json& overrides, int height, double fov) {
    style::DitherPlan p = style::ditherPreset(preset, height, fov);
    apply(p, overrides);
    return p;
}
bool samePlan(const style::DitherPlan& a, const style::DitherPlan& b) {
    return a.levels == b.levels && a.ink == b.ink && a.paper == b.paper && a.source == b.source && a.anchor == b.anchor && a.cell == b.cell &&
           a.px == b.px && a.pa == b.pa && a.gamma == b.gamma && a.dark == b.dark && a.hemi == b.hemi && a.fogDist == b.fogDist &&
           a.fogMax == b.fogMax;
}

style::PaintCamera camera(const Json& j) {
    style::PaintCamera c;
    c.ortho = j.get("ortho")->b;
    c.pos = v3(*j.get("pos")); c.fwd = v3(*j.get("fwd")); c.right = v3(*j.get("right")); c.up = v3(*j.get("up"));
    c.scale = num(j, "scale"); c.fovY = num(j, "fovY");
    return c;
}
style::PaintOptions options(const Json& j) {
    style::PaintOptions o;
    o.thetaMaxDeg = num(j, "thetaMaxDeg"); o.facingPow = num(j, "facingPow"); o.fMin = num(j, "fMin"); o.floor = num(j, "floor");
    o.tolBase = num(j, "tolBase"); o.tolGrazing = num(j, "tolGrazing"); o.reach = j.get("reach")->b;
    return o;
}

}  // namespace raw::gpu_check::style_fx
