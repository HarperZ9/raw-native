// Check a4 of evidence/gold-highlight-acos-bounds.json: acos_table, the polynomial acos that
// src/renderer/gpu/shaders/pbr.wgsl uses for the (theta, roughness) tables' theta axis, read
// from the WGSL itself (so an edit there is what is tested), evaluated in float64 and in
// float32 against std::acos over [0, 1]. A control with one coefficient perturbed must fail.
//   test_acos_table
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {
// The coefficients in Horner order (highest degree first), from the body of fn acos_table.
std::vector<double> coefficients(const std::string& wgsl) {
    std::vector<double> c;
    const std::size_t at = wgsl.find("fn acos_table(");
    if (at == std::string::npos) return c;
    const std::string body = wgsl.substr(at, wgsl.find("\n}", at) - at);
    const std::regex first(R"(var q: f32 = (-?[0-9.]+);)"), step(R"(q = q \* x ([+-]) ([0-9.]+);)");
    std::smatch m;
    if (std::regex_search(body, m, first)) c.push_back(std::stod(m[1]));
    for (auto it = std::sregex_iterator(body.begin(), body.end(), step); it != std::sregex_iterator(); ++it)
        c.push_back(((*it)[1] == "-" ? -1.0 : 1.0) * std::stod((*it)[2]));
    return c;
}
template <class T> T evalAcos(const std::vector<double>& c, T mu) {
    const T x = std::clamp(mu, T(0), T(1));
    T q = T(c[0]);
    for (std::size_t i = 1; i < c.size(); ++i) q = q * x + T(c[i]);
    return std::sqrt(T(1) - x) * q;
}
// Worst |acos_table - std::acos| over 100,001 points of [0, 1].
template <class T> double worst(const std::vector<double>& c) {
    double w = 0.0;
    for (int i = 0; i <= 100000; ++i) {
        const double mu = i / 100000.0;
        w = std::max(w, std::fabs(double(evalAcos<T>(c, T(mu))) - std::acos(double(T(mu)))));
    }
    return w;
}
}  // namespace

int main() {
    std::ifstream f(RAW_SOURCE_DIR "/src/renderer/gpu/shaders/pbr.wgsl", std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    std::string wgsl = ss.str();
    wgsl.erase(std::remove(wgsl.begin(), wgsl.end(), '\r'), wgsl.end());
    const std::vector<double> c = coefficients(wgsl);
    CHECK(c.size() == 8);
    if (c.size() != 8) return raw_test_summary();
    CHECK(wgsl.find("let th: f32 = acos_table(mu);") != std::string::npos);   // theta_x reads through it
    const double w64 = worst<double>(c), w32 = worst<float>(c);
    std::vector<double> ctl = c;
    ctl[3] += 1e-3;
    const double wc = worst<float>(ctl);
    std::printf("acos_table: float64 worst %.3e (bound 3e-8), float32 worst %.3e (bound 1e-5), control %.3e (must exceed 1e-5)\n", w64, w32, wc);
    CHECK(w64 <= 3e-8);
    CHECK(w32 <= 1e-5);
    CHECK(wc > 1e-5);
    return raw_test_summary();
}
