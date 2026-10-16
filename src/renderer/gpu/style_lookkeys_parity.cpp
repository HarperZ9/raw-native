// Look keys' CPU link (evidence/cpp-port-style-bounds.json): raw::style::LookKeys against the
// series web/look/keys.mjs gave on the hour grid of tools/port/export_style_fixtures.mjs, and
// the same refusals. The control raises one key by 1e-6 and must break the bound.
#include "raw/renderer/style_parity.hpp"
#include "raw/renderer/look_keys.hpp"
#include "raw/assets/json.hpp"
#include "style_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
namespace raw::gpu_check {
namespace {
using assets::Json;

std::vector<style::Slot> slotsOf(const Json& s) {
    if (s.is(Json::Kind::Number)) {
        const int n = int(s.n);
        if (n == 8) return style::slots8();
        if (n == 6) return style::slots6();
        throw std::invalid_argument("look keys: slots is 8, 6 or a list");
    }
    std::vector<style::Slot> v;
    for (const Json& x : s.a) v.push_back({x.get("name")->s, x.get("hour")->number("hour")});
    return v;
}
std::map<std::string, style::TermSpec> termsOf(const Json& params) {
    std::map<std::string, style::TermSpec> t;
    for (const auto& [name, spec] : params.o) {
        style::TermSpec ts;
        const Json& values = spec.is(Json::Kind::Array) ? spec : *spec.get("values");
        for (const Json& v : values.a) {
            if (v.is(Json::Kind::Array)) { std::vector<double> c; for (const Json& x : v.a) c.push_back(x.n); ts.values.push_back(c); }
            else ts.values.push_back({v.n});
        }
        if (spec.is(Json::Kind::Object)) {
            if (const Json* m = spec.get("min")) ts.min = m->n;
            if (const Json* m = spec.get("max")) ts.max = m->n;
        }
        t[name] = ts;
    }
    return t;
}
// The exported value of a term at hour index k: a number or a vector.
std::vector<double> exported(const Json& v) {
    if (v.is(Json::Kind::Array)) { std::vector<double> c; for (const Json& x : v.a) c.push_back(x.n); return c; }
    return {v.n};
}
}  // namespace

LookKeysResult lookKeysParity(const std::string& path, std::string& err) {
    LookKeysResult r;
    std::vector<std::uint8_t> bytes;
    if (!style_fx::readBytes(path, bytes)) { err = "cannot read " + path; return r; }
    const Json j = assets::parseJson(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    std::vector<double> hours;
    for (const Json& h : j.get("hours")->a) hours.push_back(h.n);
    r.keysExact = true;
    double controlWorst = 0;
    for (const Json& s : j.get("series")->a) {
        const auto terms = termsOf(*s.get("params"));
        const style::LookKeys keys(slotsOf(*s.get("slots")), terms);
        auto bentTerms = terms;
        bentTerms.begin()->second.values[2][0] += 1e-6;   // the control: the third key of the first term
        const style::LookKeys bent(slotsOf(*s.get("slots")), bentTerms);
        r.series++;
        for (const auto& [name, arr] : s.get("values")->o) {
            for (std::size_t k = 0; k < hours.size(); ++k) {
                const std::vector<double> want = exported(arr.a[k]), got = keys.term(name, hours[k]);
                for (std::size_t c = 0; c < want.size(); ++c) r.maxAbs = std::max(r.maxAbs, std::abs(got.at(c) - want[c]));
                if (name == terms.begin()->first) {
                    const std::vector<double> b = bent.term(name, hours[k]);
                    for (std::size_t c = 0; c < want.size(); ++c) controlWorst = std::max(controlWorst, std::abs(b.at(c) - want[c]));
                }
                r.samples++;
            }
        }
        const auto& slots = keys.slots();
        for (const auto& [name, arr] : s.get("atSlots")->o)
            for (std::size_t k = 0; k < slots.size(); ++k)
                if (keys.term(name, slots[k].hour) != exported(arr.a[k])) r.keysExact = false;
    }
    r.refusalsMatch = true;
    for (const Json& q : j.get("refusals")->a) {
        bool refused = false;
        try { style::LookKeys k(slotsOf(*q.get("slots")), termsOf(*q.get("params"))); } catch (const std::invalid_argument&) { refused = true; }
        if (refused != q.get("refused")->b) r.refusalsMatch = false;
    }
    try { style::LookKeys(style::slots8(), {}).term("nope", 3); } catch (const std::invalid_argument&) { r.unknownRefused = j.get("unknownTermRefused")->b; }
    r.controlFails = controlWorst > 1e-12;
    return r;
}

}  // namespace raw::gpu_check
