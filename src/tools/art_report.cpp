// raw_native_cli art's shared report pieces: see art_report.hpp.
#include "art_report.hpp"
#include "raw/tools/image_out.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
namespace raw::art_cmd {

CodeStats codeStats(const std::vector<std::uint8_t>& want, const std::vector<std::uint8_t>& got) {
    long hist[256] = {};
    CodeStats s;
    double sum = 0;
    for (std::size_t i = 0; i < want.size() && i < got.size(); ++i) {
        if ((i & 3) == 3) continue;
        const int d = std::abs(int(want[i]) - int(got[i]));
        hist[d]++; sum += d; s.n++; s.max = std::max(s.max, d); s.over2 += d > 2; s.over8 += d > 8; s.differ += d > 0;
    }
    s.mean = s.n ? sum / double(s.n) : 0;
    long acc = 0;
    for (int d = 0; d < 256; ++d) { acc += hist[d]; if (double(acc) >= double(s.n) * 0.999) { s.p999 = d; break; } }
    return s;
}

std::vector<std::uint8_t> unpack(const std::vector<std::uint32_t>& words) {
    std::vector<std::uint8_t> o(words.size() * 4);
    std::memcpy(o.data(), words.data(), o.size());
    return o;
}

bool writeCodesPng(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgba) {
    std::vector<std::uint8_t> rgb(std::size_t(w) * std::size_t(h) * 3);
    for (std::size_t i = 0; i < std::size_t(w) * std::size_t(h); ++i) for (int c = 0; c < 3; ++c) rgb[i * 3 + std::size_t(c)] = rgba[i * 4 + std::size_t(c)];
    return writePng(path, w, h, rgb);
}

std::string hex(std::uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof b, "0x%016llx", static_cast<unsigned long long>(v));
    return b;
}

void Json::key(const char* k) { if (!first_) s_ << ","; first_ = false; s_ << "\n \"" << k << "\": "; }
Json& Json::num(const char* k, double v) {
    key(k);
    if (std::isfinite(v)) { char b[40]; std::snprintf(b, sizeof b, "%.9g", v); s_ << b; } else s_ << "null";
    return *this;
}
Json& Json::integer(const char* k, long long v) { key(k); s_ << v; return *this; }
Json& Json::boolean(const char* k, bool v) { key(k); s_ << (v ? "true" : "false"); return *this; }
Json& Json::str(const char* k, const std::string& v) {
    key(k);
    s_ << '"';
    for (char c : v) { if (c == '"' || c == '\\') s_ << '\\'; s_ << c; }
    s_ << '"';
    return *this;
}
Json& Json::raw(const char* k, const std::string& json) { key(k); s_ << json; return *this; }
Json& Json::nums(const char* k, const std::vector<double>& v) {
    key(k);
    s_ << "[";
    for (std::size_t i = 0; i < v.size(); ++i) { char b[40]; std::snprintf(b, sizeof b, "%s%.7g", i ? ", " : "", v[i]); s_ << b; }
    s_ << "]";
    return *this;
}

std::string pairedJson(const art::Paired& p) {
    Json j;
    j.num("mean_a", p.meanA).num("mean_b", p.meanB).num("diff", p.diff).num("diff_lo", p.diffLo).num("diff_hi", p.diffHi)
     .num("reduction", p.reduction).num("reduction_lo", p.redLo).num("reduction_hi", p.redHi).integer("frames", p.frames).str("seed", hex(p.seed));
    return j.done();
}
std::string statsJson(const CodeStats& s) {
    Json j;
    j.integer("max", s.max).integer("p999", s.p999).num("mean", s.mean).integer("differ", s.differ).integer("over2", s.over2).integer("over8", s.over8).integer("values", s.n);
    return j.done();
}
bool writeText(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
    return bool(f);
}

}  // namespace raw::art_cmd
