#pragma once
// Private to the style checks (src/renderer/gpu/style_*.cpp): the reader of the fixture bundles
// tools/port/export_style_fixtures.mjs writes, and their JSON records as the C++ references'
// types. An .rnfx file is "RNFX0001", a little-endian u32 header length, the JSON header, zero
// padding to 8 bytes, then the arrays the header lists by type, count and offset.
#include "raw/assets/json.hpp"
#include "raw/renderer/style_ditherlight.hpp"
#include "raw/renderer/style_paintover.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace raw::gpu_check::style_fx {

struct Bundle {
    assets::Json head;
    std::vector<std::uint8_t> bytes;
    std::size_t base{0};
    // The array `name` as T; throws assets::AssetError when it is missing or of another type.
    template<class T> std::vector<T> arr(const std::string& name, const char* type) const {
        const assets::Json* a = head.get("arrays") ? head.get("arrays")->get(name) : nullptr;
        if (!a || !a->get("type") || a->get("type")->s != type) throw assets::AssetError("fixture: no " + std::string(type) + " array " + name);
        const std::size_t n = std::size_t(a->get("count")->number("count")), off = std::size_t(a->get("offset")->number("offset"));
        if (base + off + n * sizeof(T) > bytes.size()) throw assets::AssetError("fixture: array " + name + " runs past the file");
        std::vector<T> v(n);
        if (n) std::memcpy(v.data(), bytes.data() + base + off, n * sizeof(T));
        return v;
    }
};
bool readBytes(const std::string& path, std::vector<std::uint8_t>& out);
// Throws assets::AssetError on a malformed bundle; false when the file cannot be read.
bool loadBundle(const std::string& path, Bundle& b, std::string& err);

style::V3 v3(const assets::Json& a);
style::DitherLight light(const assets::Json& j);
style::DitherPlan plan(const assets::Json& j);                       // an exported (resolved) plan
// The C++ resolution of a preset with the case's overrides: compared with the exported plan.
style::DitherPlan planFromPreset(const std::string& preset, const assets::Json& overrides, int height, double fov);
bool samePlan(const style::DitherPlan& a, const style::DitherPlan& b);
style::PaintCamera camera(const assets::Json& j);
style::PaintOptions options(const assets::Json& j);

}  // namespace raw::gpu_check::style_fx
