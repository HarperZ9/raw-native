// raw/game/action detmath: the 12 golden bit patterns of Low Water's design/DETMATH.md section 3
// (tests/data/action/detmath-golden.json, copied from combat/tests/detmath-golden.json), and a
// sweep of 1,001 inputs a function hashed by the JS (detmath-sweep.json,
// tools/action/detmath_sweep.mjs). A sine whose first coefficient is one ulp off still passes the
// 12 goldens (measured: they hold three sine calls, none of which moves); the sweep catches it.
#include "check.hpp"
#include "raw/game/action/detmath.hpp"
#include "raw/core/sha256.hpp"
#include "raw/story/story.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace raw::game::action;

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static std::string bits(double d) { uint64_t b; std::memcpy(&b, &d, 8); char o[17]; std::snprintf(o, sizeof o, "%016llx", (unsigned long long)b); return o; }

// The golden calls in the order of the table.
static std::vector<double> calls(const std::function<double(double)>& sin_fn) {
    return {sin_fn(0.5), sin_fn(-2.75), dcos(1.25), dcos(10), datan2(1, 3), datan2(-2, -0.5), dasin(0.3), dacos(-0.7),
            dexp(-0.0333), dexp(4.2), dhypot(1.5, 2.25), sin_fn(1000)};
}

static int matches(const std::vector<double>& got, const raw::story::Json& want) {
    int n = 0;
    for (size_t i = 0; i < got.size() && i < want.a.size(); i++) n += bits(got[i]) == want.a[i].s;
    return n;
}

// The DETMATH sine with S1 moved one ulp toward zero.
static double planted_sin(double x) {
    const double S1 = std::nextafter(-1.66666666666666324348e-1, 0.0), S2 = 8.33333333332248946124e-3, S3 = -1.98412698298579493134e-4;
    const double S4 = 2.75573137070700676789e-6, S5 = -2.50507602534068634195e-8, S6 = 1.58969099521155010221e-10;
    const double k = std::floor(x * 6.36619772367581382433e-1 + 0.5);
    const double r = (x - k * 1.57079632673412561417) - k * 6.07710050650619224932e-11;
    const int q = (int)std::fmod(std::fmod(k, 4.0) + 4.0, 4.0);
    const double z = r * r, s = r + r * z * (S1 + z * (S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)))));
    const double c = 1 - 0.5 * z + z * z * (4.16666666666666019037e-2 + z * (-1.38888888888741095749e-3 + z * (2.48015872894767294178e-5 +
                     z * (-2.75573143513906633035e-7 + z * (2.08757232129817482790e-9 + z * -1.13596475577881948265e-11)))));
    return q == 0 ? s : q == 1 ? c : q == 2 ? -s : -c;
}

// SHA-256 of a function's result bits over the sweep, as the JS writes it.
static std::string sweep(const std::function<double(double, int)>& f) {
    std::string all;
    for (int i = 0; i < 1001; i++) { if (i) all += '\n'; all += bits(f(-20 + i * 0.04, i)); }
    return raw::sha256Hex(all);
}

int main() {
    const auto golden = raw::story::parse_json(slurp(std::string(RAW_SOURCE_DIR) + "/tests/data/action/detmath-golden.json"));
    CHECK(golden.a.size() == 12);
    const int n = matches(calls(dsin), golden);
    std::printf("detmath: %d of 12 golden bit patterns\n", n);
    CHECK(n == 12);
    const int planted = matches(calls(planted_sin), golden);
    std::printf("detmath planted (S1 one ulp off): %d of 12 goldens still match (the goldens cannot see it)\n", planted);
    const auto want = raw::story::parse_json(slurp(std::string(RAW_SOURCE_DIR) + "/tests/data/action/detmath-sweep.json"));
    const std::vector<std::pair<const char*, std::function<double(double, int)>>> fns = {
        {"dsin", [](double x, int) { return dsin(x); }}, {"dcos", [](double x, int) { return dcos(x); }},
        {"datan", [](double x, int) { return datan(x); }}, {"datan2", [](double x, int i) { return datan2(x, 1.5 - i * 0.003); }},
        {"dasin", [](double x, int) { return dasin(x / 20); }}, {"dacos", [](double x, int) { return dacos(x / 20); }},
        {"dexp", [](double x, int) { return dexp(x); }}, {"dhypot", [](double x, int) { return dhypot(x, 0.5 * x + 1); }}};
    int swept = 0;
    for (auto& [name, f] : fns) swept += sweep(f) == want.get("fns")->str_or(name);
    std::printf("detmath sweep: %d of 8 functions match the JS over 1001 inputs\n", swept);
    CHECK(swept == 8);
    const bool caught = sweep([](double x, int) { return planted_sin(x); }) != want.get("fns")->str_or("dsin");
    std::printf("detmath sweep planted (S1 one ulp off): %s\n", caught ? "caught" : "MISSED");
    CHECK(caught);
    // C rules for atan2 at zeros and infinities, and NaN outside asin's domain
    CHECK(std::signbit(datan2(-0.0, 1.0)) && datan2(0.0, -1.0) == DPI && datan2(-0.0, -0.0) == -DPI);
    CHECK(std::isnan(dasin(1.5)) && std::isnan(dsin(INFINITY)) && dexp(1000) == INFINITY && dexp(-1000) == 0);
    return raw_test_summary();
}
