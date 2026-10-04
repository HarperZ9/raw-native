// SPDX-License-Identifier: FSL-1.1-MIT
// Run every vector in vectors/ against superstack.hpp.
// Build: g++ -std=c++23 -O1 -I<dir with superstack.hpp> tests/run_vectors.cpp -o run_vectors
// Usage: run_vectors <repo root> [--summary OUT.json]
#include "superstack.hpp"

#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <numbers>
#include <sstream>

using namespace superstack;

namespace {
std::map<std::string, int> counts;
std::vector<std::string> fails;
std::string file;

void check(bool ok, const std::string& what) {
  ++counts[file];
  if (!ok) fails.push_back(file + ": " + what);
}
void raises(const std::function<void()>& fn, const std::string& what) {
  try { fn(); } catch (const std::exception&) { check(true, what); return; }
  check(false, what + " (no error)");
}
std::string read_file(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) throw error("cannot read " + p);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
Bytes hex(const std::string& h) {
  Bytes out;
  for (std::size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return out;
}
std::string to_hex(std::span<const std::uint8_t> b) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (auto x : b) { s += d[x >> 4]; s += d[x & 15]; }
  return s;
}
std::optional<double> opt(const Value& v) { return v.is_null() ? std::nullopt : std::optional<double>(v.num()); }
bool close(std::optional<double> a, std::optional<double> b, double tol) {
  if (!a || !b) return !a && !b;
  return std::abs(*a - *b) <= tol;
}
Bytes i16(const Value& arr) {
  Bytes out;
  for (const auto& v : arr.arr()) {
    auto u = static_cast<std::uint16_t>(static_cast<std::int16_t>(v.num()));
    out.push_back(static_cast<std::uint8_t>(u & 0xFF));
    out.push_back(static_cast<std::uint8_t>(u >> 8));
  }
  return out;
}
Bytes u8(const Value& arr) {
  Bytes out;
  for (const auto& v : arr.arr()) out.push_back(static_cast<std::uint8_t>(v.num()));
  return out;
}
Bytes f32(const Value& arr) {
  Bytes out;
  for (const auto& v : arr.arr()) {
    auto u = std::bit_cast<std::uint32_t>(static_cast<float>(v.num()));
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
  }
  return out;
}
std::vector<std::string> strings(const Value& arr) {
  std::vector<std::string> out;
  for (const auto& v : arr.arr()) out.push_back(v.is_string() ? v.str() : "");
  return out;
}
std::uint32_t u32(const Value& v) { return static_cast<std::uint32_t>(v.num()); }

std::vector<double> synth(const Value& sig) {
  auto rate = static_cast<double>(sig.at("rate").num());
  int ch = static_cast<int>(sig.at("channels").num());
  auto n = static_cast<std::int64_t>(sig.at("frames").num());
  std::string kind = sig.at("kind").str();
  std::vector<double> out;
  for (std::int64_t i = 0; i < n; ++i)
    for (int c = 0; c < ch; ++c) {
      if (kind == "silence") { out.push_back(0.0); continue; }
      double amp = sig.at("amp").arr()[static_cast<std::size_t>(c)].num();
      if (kind == "gated" && i >= n / 2) amp = amp * sig.at("quiet_gain").num();
      out.push_back(amp * std::sin(2.0 * std::numbers::pi * sig.at("freq").num() * static_cast<double>(i) / rate));
    }
  return out;
}

void run_canonical(const Value& v) {
  for (const auto& c : v.at("numbers").arr()) {
    const std::string& in = c.at("in").str();
    std::string got;
    if (c.at("type").str() == "float") {
      double d = 0;
      std::from_chars(in.data(), in.data() + in.size(), d);
      got = canonical_number(d);
    } else {
      got = canonical_integer(std::stoll(in));
    }
    check(got == c.at("out").str(), "number " + in);
  }
  for (const auto& c : v.at("reject_numbers").arr()) {
    const std::string in = c.at("in").str();
    if (c.at("type").str() == "float") {
      double d = in == "NaN" ? NAN : in == "Infinity" ? INFINITY : -INFINITY;
      raises([&] { canonical_number(d); }, "reject " + in);
    } else {
      raises([&] { canonical_integer(std::stoll(in)); }, "reject " + in);
    }
  }
  for (const auto& c : v.at("docs").arr()) {
    std::string out = canonical(parse(c.at("in").str()));
    check(out == c.at("out").str(), "doc " + c.at("in").str());
    check(sha256_hex(std::string_view(out)) == c.at("sha256").str(), "doc sha " + c.at("in").str());
  }
  for (const auto& t : v.at("reject_docs").arr()) raises([&] { canonical(parse(t.str())); }, "reject doc " + t.str());
}

void run_hash(const Value& v) {
  for (const auto& c : v.at("cases").arr()) check(sha256_hex(hex(c.at("hex").str())) == c.at("sha256").str(), "sha256");
}

void run_seed(const Value& v) {
  const Value& s = v.at("substream");
  check(substream(s.at("seed").str(), s.at("tag").str()) == s.at("out").str(), "substream");
  for (const auto& c : v.at("seeds").arr()) {
    const std::string& seed = c.at("seed").str();
    check(xmur3(seed) == u32(c.at("u32")), "xmur3 " + seed);
    check(utf16_units(seed).size() == static_cast<std::size_t>(c.at("utf16_units").num()), "utf16 length " + seed);
    auto g = rng(seed);
    bool ok = true;
    for (const auto& d : c.at("draws_u32").arr()) ok = ok && g.next_u32() == u32(d);
    check(ok, "draws " + seed);
    g = rng(seed);
    ok = true;
    for (const auto& f : c.at("floats").arr()) ok = ok && g.next_float() == f.num();
    check(ok, "floats " + seed);
  }
  for (const auto& c : v.at("mulberry32").arr()) {
    Mulberry32 m(u32(c.at("state")));
    bool ok = true;
    for (const auto& d : c.at("draws_u32").arr()) ok = ok && m.next_u32() == u32(d);
    check(ok, "mulberry32");
  }
  for (const auto& c : v.at("pixel_hash").arr()) {
    auto x = u32(c.at("x")), y = u32(c.at("y")), s2 = u32(c.at("s"));
    check(pixel_hash(x, y, s2) == u32(c.at("u32")), "pixel_hash");
    check(pixel_hash01(x, y, s2) == c.at("unit").num(), "pixel_hash01");
  }
  auto g = rng(v.at("site_make_rng").at("seed").str());
  bool ok = true;
  for (const auto& f : v.at("site_make_rng").at("first_five").arr()) ok = ok && g.next_float() == f.num();
  check(ok, "site makeRng parity");
}

void run_clock(const Value& v) {
  check(FLICKS_PER_SECOND == static_cast<std::int64_t>(v.at("per_second").num()), "per_second");
  bool same = v.at("sample_rates").arr().size() == SAMPLE_RATES.size();
  for (std::size_t i = 0; same && i < SAMPLE_RATES.size(); ++i) same = SAMPLE_RATES[i] == static_cast<std::int64_t>(v.at("sample_rates").arr()[i].num());
  check(same, "sample rate set");
  for (const auto& c : v.at("per_sample").arr())
    check(flicks_per_sample(static_cast<std::int64_t>(c.at("rate").num())) == static_cast<std::int64_t>(c.at("flicks").num()), "per_sample");
  for (const auto& c : v.at("per_frame").arr())
    check(flicks_per_frame(static_cast<std::int64_t>(c.at("num").num()), static_cast<std::int64_t>(c.at("den").num())) ==
              static_cast<std::int64_t>(c.at("flicks").num()), "per_frame");
  for (const auto& r : v.at("reject_rates").arr()) raises([&] { flicks_per_sample(static_cast<std::int64_t>(r.num())); }, "reject rate");
  for (const auto& f : v.at("reject_frames").arr())
    raises([&] { flicks_per_frame(static_cast<std::int64_t>(f.arr()[0].num()), static_cast<std::int64_t>(f.arr()[1].num())); }, "reject frames");
  for (const auto& c : v.at("durations").arr())
    check(static_cast<std::int64_t>(c.at("samples").num()) * flicks_per_sample(static_cast<std::int64_t>(c.at("rate").num())) ==
              static_cast<std::int64_t>(c.at("flicks").num()), "duration");
}

void run_sound(const Value& v) {
  for (const auto& c : v.at("quantize").arr()) {
    double x = c.at("in").num();
    auto b = quantize_s16(std::span<const double>(&x, 1));
    check(s16_at(b, 0) == static_cast<std::int16_t>(c.at("out").num()), "quantize");
  }
  double ktol = v.at("k_tolerance").num();
  for (const auto& m : v.at("k_weighting").obj()) {
    auto got = k_weighting(std::stoll(m.key));
    bool ok = true;
    for (std::size_t i = 0; i < 2; ++i)
      for (std::size_t j = 0; j < 5; ++j) ok = ok && std::abs(got[i][j] - m.value.arr()[i].arr()[j].num()) <= ktol;
    check(ok, "k_weighting " + m.key);
  }
  for (const auto& c : v.at("loudness").arr()) {
    auto x = synth(c);
    auto l = integrated_lufs(x, static_cast<std::int64_t>(c.at("rate").num()), static_cast<int>(c.at("channels").num()));
    check(close(l, opt(c.at("integrated_lufs")), c.at("tolerance_lu").num()), "lufs " + c.at("name").str());
    check(close(peak_dbfs(x), opt(c.at("peak_dbfs")), 1e-9), "peak " + c.at("name").str());
  }
  for (const auto& c : v.at("loudness_check").arr())
    check(loudness_check(c.at("class").str(), opt(c.at("lufs")), opt(c.at("peak"))) == c.at("verdict").str(), "loudness_check " + c.at("class").str());
  const Value& t = v.at("targets");
  bool ok = t.at("speech").at("integrated_lufs").num() == loudness_target("speech")->integrated_lufs &&
            t.at("speech").at("true_peak_dbtp_max").num() == loudness_target("speech")->peak_max &&
            t.at("speech").at("tolerance_lu").num() == loudness_target("speech")->tolerance_lu &&
            t.at("music").at("integrated_lufs").num() == loudness_target("music")->integrated_lufs &&
            t.at("music").at("true_peak_dbtp_max").num() == loudness_target("music")->peak_max &&
            t.at("music").at("tolerance_lu").num() == loudness_target("music")->tolerance_lu &&
            t.at("interactive").at("integrated_lufs_max").num() == loudness_target("interactive")->integrated_lufs &&
            t.at("interactive").at("sample_peak_dbfs_max").num() == loudness_target("interactive")->peak_max &&
            t.obj().size() == 3;
  check(ok, "targets");
}

void run_export(const Value& v) {
  for (const auto& c : v.at("cases").arr()) {
    const std::string& fmt = c.at("format").str();
    Bytes out;
    if (fmt == "wav") {
      out = wav_s16(hex(c.at("pcm_hex").str()), u32(c.at("rate")), u32(c.at("channels")));
      check(to_hex(std::span(out).subspan(0, 44)) == c.at("header_hex").str(), "wav header");
    } else if (fmt == "ppm") {
      out = ppm_rgb8(hex(c.at("body_hex").str()), static_cast<int>(c.at("width").num()), static_cast<int>(c.at("height").num()));
    } else {
      out = pgm_u8(hex(c.at("body_hex").str()), static_cast<int>(c.at("width").num()), static_cast<int>(c.at("height").num()));
    }
    check(sha256_hex(out) == c.at("sha256").str(), "export " + fmt);
  }
}

void run_colour(const Value& v) {
  double tol = v.at("tolerance").num();
  for (const auto& c : v.at("oklab").arr()) {
    auto got = hex_to_oklab(c.at("hex").str());
    bool ok = true;
    for (std::size_t i = 0; i < 3; ++i) ok = ok && std::abs(got[i] - c.at("oklab").arr()[i].num()) <= tol;
    check(ok, "oklab " + c.at("hex").str());
  }
  for (const auto& c : v.at("oklab_inverse").arr()) {
    const auto& l = c.at("oklab").arr();
    auto got = oklab_to_linear_srgb(l[0].num(), l[1].num(), l[2].num());
    bool ok = true;
    for (std::size_t i = 0; i < 3; ++i) ok = ok && std::abs(got[i] - c.at("linear_srgb").arr()[i].num()) <= tol;
    check(ok, "oklab inverse");
  }
  bool lv = v.at("risk_levels").arr().size() == RISK_LEVELS.size();
  for (std::size_t i = 0; lv && i < RISK_LEVELS.size(); ++i) lv = v.at("risk_levels").arr()[i].str() == RISK_LEVELS[i];
  check(lv, "risk levels");
  auto pole_eq = [](const Value& p, const RiskPole& r) {
    return p.obj().size() == 6 && p.at("low").str() == r.low && p.at("moderate").str() == r.moderate &&
           p.at("elevated").str() == r.elevated && p.at("high").str() == r.high && p.at("ink").str() == r.ink &&
           p.at("quiet").str() == r.quiet;
  };
  check(v.at("risk_tokens").obj().size() == 2 && pole_eq(v.at("risk_tokens").at("light"), RISK_LIGHT) &&
            pole_eq(v.at("risk_tokens").at("dark"), RISK_DARK), "risk tokens");
  auto grounds_eq = [](const Value& g, const std::array<std::string_view, 3>& r) {
    if (g.arr().size() != 3) return false;
    for (std::size_t i = 0; i < 3; ++i) if (g.arr()[i].str() != r[i]) return false;
    return true;
  };
  check(grounds_eq(v.at("risk_grounds").at("light"), RISK_GROUNDS_LIGHT) && grounds_eq(v.at("risk_grounds").at("dark"), RISK_GROUNDS_DARK), "risk grounds");
  for (const auto& c : v.at("contrast").arr()) {
    double cr = contrast_ratio(c.at("fg").str(), c.at("bg").str());
    check(std::abs(cr - c.at("ratio").num()) <= tol && (cr >= 4.5) == std::get<bool>(c.at("aa_text").v), "contrast " + c.at("fg").str());
  }
  for (const auto& c : v.at("risk_of").arr())
    check(risk_of(c.at("in").is_string() ? c.at("in").str() : "") == c.at("out").str(), "risk_of");
  for (const auto& c : v.at("hot_mark").arr()) check(hot_mark(strings(c.at("in"))) == static_cast<int>(c.at("out").num()), "hot_mark");
}

void run_reconcile(const Value& v) {
  for (const auto& c : v.at("identity").arr()) check(identity(c.at("ref").str(), c.at("cand").str()) == c.at("out").str(), "identity");
  for (const auto& c : v.at("pcm_s16").arr())
    check(canonical(reconcile_pcm_s16(i16(c.at("ref")), i16(c.at("cand")))) == canonical(c.at("expected")), "pcm " + c.at("name").str());
  for (const auto& c : v.at("rgb8").arr())
    check(canonical(reconcile_rgb8(u8(c.at("ref")), u8(c.at("cand")))) == canonical(c.at("expected")), "rgb8 " + c.at("name").str());
  for (const auto& c : v.at("f32_rmse").arr()) {
    Bytes a = f32(c.at("ref")), b = f32(c.at("cand"));
    std::optional<double> got;
    if (c.at("mask").is_null()) got = f32_rmse(a, b);
    else {
      Bytes m = u8(c.at("mask"));
      got = f32_rmse(a, b, std::span<const std::uint8_t>(m));
    }
    check(close(got, opt(c.at("rmse")), 0.0), "f32_rmse");
  }
  for (const auto& c : v.at("round6").arr()) check(round6(c.at("in").num()) == c.at("out").num(), "round6");
}

void run_receipt(const Value& v) {
  for (const auto& c : v.at("make").arr()) {
    const Value& a = c.at("args");
    Bytes content = hex(a.at("content_hex").str());
    ReceiptArgs ra;
    ra.producer = a.at("producer").str();
    ra.version = a.at("version").str();
    ra.backend = a.at("backend").str();
    ra.scene = a.at("scene");
    ra.media = a.at("media");
    ra.content = content;
    ra.outputs = a.at("outputs");
    ra.reference = a.at("reference");
    ra.reconcile = a.at("reconcile");
    ra.does_not_prove = strings(a.at("does_not_prove"));
    Value got = make_receipt(ra);
    check(canonical(got) == c.at("canonical").str(), "make " + ra.producer);
    check(got.at("receipt_sha256").str() == c.at("receipt_sha256").str(), "make sha " + ra.producer);
  }
  for (const auto& c : v.at("verify").arr()) check(verify_receipt(c.at("receipt")) == strings(c.at("errors")), "verify " + c.at("name").str());
}

void run_scene(const Value& v) {
  for (const auto& c : v.at("hashes").arr()) check(canonical_sha256(c.at("scene")) == c.at("sha256").str(), "scene hash " + c.at("name").str());
  for (const auto& c : v.at("validate").arr()) check(validate_scene(c.at("scene")) == strings(c.at("errors")), "validate " + c.at("name").str());
}
}  // namespace

int main(int argc, char** argv) {
  std::string root = argc > 1 ? argv[1] : ".";
  std::string summary;
  for (int i = 2; i + 1 < argc; ++i) if (std::string(argv[i]) == "--summary") summary = argv[i + 1];
  const std::map<std::string, std::function<void(const Value&)>> runners = {
      {"canonical", run_canonical}, {"hash", run_hash}, {"seed", run_seed}, {"clock", run_clock},
      {"sound", run_sound}, {"export", run_export}, {"colour", run_colour}, {"reconcile", run_reconcile},
      {"receipt", run_receipt}, {"scene", run_scene}};
  Value manifest = parse(read_file(root + "/vectors/MANIFEST.json"));
  file = "MANIFEST.json";
  check(manifest.at("contract").str() == CONTRACT, "contract id");
  for (const auto& m : manifest.at("files").obj()) {
    file = m.key;
    std::string raw = read_file(root + "/vectors/" + m.key);
    check(sha256_hex(std::string_view(raw)) == m.value.str(), "file hash matches MANIFEST");
    auto it = runners.find(m.key.substr(0, m.key.size() - 5));
    if (it == runners.end()) { check(false, "no runner for this file"); continue; }
    try { it->second(parse(raw)); } catch (const std::exception& e) { check(false, std::string("crashed: ") + e.what()); }
  }
  int total = 0;
  for (const auto& [k, n] : counts) total += n;
  for (std::size_t i = 0; i < fails.size() && i < 40; ++i) std::cout << "FAIL " << fails[i] << "\n";
  std::cout << "c++: " << (total - static_cast<int>(fails.size())) << "/" << total << " checks passed\n";
  if (!summary.empty()) {
    std::ofstream o(summary);
    o << "{";
    bool first = true;
    for (const auto& [k, n] : counts) { o << (first ? "" : ",") << "\"" << k << "\":" << n; first = false; }
    o << "}";
  }
  return fails.empty() ? 0 : 1;
}
