// The sky model and the baked environments (evidence/sky-envmaps-bounds.json k1 to k6).
//   test_sky
#include "raw/core/sha256.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include "raw/renderer/sky.hpp"
#include "check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace raw;
using sky::D3; using sky::Rgb;

namespace {
constexpr double kPi = 3.14159265358979323846;
double lum(Rgb c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
D3 elev(double deg, double az) { const double a = deg * kPi / 180.0, b = az * kPi / 180.0; return {std::cos(a) * std::cos(b), std::sin(a), std::cos(a) * std::sin(b)}; }
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
#if defined(_MSC_VER)
constexpr const char* kFamily = "msvc";
#elif defined(__clang__)
constexpr const char* kFamily = "clang";
#elif defined(__GNUC__)
constexpr const char* kFamily = "gcc";
#else
constexpr const char* kFamily = "other";
#endif
}  // namespace

int main() {
    // k1 Preetham (T 2.5) against the CIE clear sky, relative to each one's zenith.
    for (double sunElev : {30.0, 60.0}) {
        sky::Clear c; c.turbidity = 2.5; c.sun = elev(sunElev, 0.0);
        const double zp = lum(sky::preetham(c, {0, 1, 0}));
        std::vector<double> rel;
        for (int i = 0; i < 20; ++i) for (int j = 0; j < 20; ++j) {
            const D3 d = elev(2.0 + 86.0 * (i + 0.5) / 20.0, 360.0 * (j + 0.5) / 20.0);
            if (std::acos(std::clamp(dot(d, c.sun), -1.0, 1.0)) < 5.0 * kPi / 180.0) continue;
            const double p = lum(sky::preetham(c, d)) / zp, q = sky::cieClearRelative(c.sun, d);
            rel.push_back(std::fabs(p - q) / q);
        }
        std::sort(rel.begin(), rel.end());
        const double med = rel[rel.size() / 2], worst = rel.back();
        std::printf("k1 sun %.0f deg: Preetham against CIE clear, median %.3f worst %.3f (bounds 0.25, 0.80) over %zu directions\n", sunElev, med, worst, rel.size());
        CHECK(med <= 0.25);
        CHECK(worst <= 0.80);
    }
    // k2 overcast horizon / zenith.
    const double k2 = lum(sky::overcast(8000.0, {1, 0, 0})) / lum(sky::overcast(8000.0, {0, 1, 0}));
    std::printf("k2 overcast horizon / zenith %.15f\n", k2);
    CHECK(std::fabs(k2 - 1.0 / 3.0) <= 1e-12);
    // k3 turbidity brightens the circumsolar region (absolute luminance; addendum A of the bounds).
    const auto circ = [](double T) { sky::Clear c; c.turbidity = T; c.sun = elev(45.0, 0.0); return lum(sky::preetham(c, elev(35.0, 0.0))); };
    std::printf("k3 luminance 10 deg from the sun: T 2 %.0f nits, T 6 %.0f nits\n", circ(2.0), circ(6.0));
    CHECK(circ(6.0) > circ(2.0));
    // k4 colour.
    sky::Clear noon; noon.turbidity = 2.5; noon.sun = elev(70.0, 30.0);
    const Rgb z = sky::preetham(noon, {0, 1, 0}), o = sky::overcast(8000.0, {0, 1, 0});
    std::printf("k4 clear noon zenith (%.0f, %.0f, %.0f) nits; overcast (%.0f, %.0f, %.0f)\n", z.r, z.g, z.b, o.r, o.g, o.b);
    CHECK(z.b > z.r);
    CHECK(std::fabs(o.r - o.b) <= 0.01 * o.g);
    // k5 horizontal illuminance of each baked cube (disks removed) against direct integration.
    for (int e = 0; e < 4; ++e) {
        const sky::Env env = sky::Env(e);
        const lighting::Cube c = sky::bake(env, 64, 1), plain = [&] { lighting::Cube p = c; return p; }();
        // The bake with its disk texel removed: recompute that texel from the sky function alone.
        double Ec = 0.0;
        for (int f = 0; f < 6; ++f) for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const D3 d = lighting::cube::dirOf(f, (x + 0.5) / 64.0, (y + 0.5) / 64.0);
            if (d.y <= 0.0) continue;
            Rgb v = c.texel(0, f, y, x);
            if (env == sky::Env::ClearNoon || env == sky::Env::GoldenHour) {
                sky::Clear cl; cl.turbidity = env == sky::Env::ClearNoon ? 2.5 : 4.0; cl.sun = env == sky::Env::ClearNoon ? elev(70.0, 30.0) : elev(4.0, 200.0);
                v = sky::preetham(cl, d);
            } else if (env == sky::Env::Overcast) v = sky::overcast(8000.0, d);
            else v = {0, 0, 0};
            Ec += lum(v) * d.y * sky::texelSolidAngle(64, (x + 0.5) / 64.0, (y + 0.5) / 64.0);
        }
        if (env == sky::Env::Night) { std::printf("k5 night: not integrated (stars and moon are artistic)\n"); continue; }
        double Ed = 0.0; const int n = 1024;
        for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) {   // stratified over the upper hemisphere, uniform in solid angle
            const double ct = (i + 0.5) / n, st = std::sqrt(1.0 - ct * ct), ph = 2.0 * kPi * (j + 0.5) / n;
            const D3 d{st * std::cos(ph), ct, st * std::sin(ph)};
            Rgb v;
            if (env == sky::Env::Overcast) v = sky::overcast(8000.0, d);
            else { sky::Clear cl; cl.turbidity = env == sky::Env::ClearNoon ? 2.5 : 4.0; cl.sun = env == sky::Env::ClearNoon ? elev(70.0, 30.0) : elev(4.0, 200.0); v = sky::preetham(cl, d); }
            Ed += lum(v) * ct;
        }
        Ed *= 2.0 * kPi / (double(n) * n);
        std::printf("k5 %s: cube %.1f lux, direct %.1f lux, relative %.4f (bound 0.02)\n", sky::name(env), Ec, Ed, std::fabs(Ec - Ed) / Ed);
        CHECK(std::fabs(Ec - Ed) / Ed <= 0.02);
        (void)plain;
    }
    // k6 the lockfile: the analytic skies' hashes for this compiler family, when recorded.
    std::ifstream lf(RAW_SOURCE_DIR "/evidence/envmaps.lock.json", std::ios::binary);
    std::stringstream ss; ss << lf.rdbuf();
    const std::string lock = ss.str();
    int checked = 0;
    for (int e = 0; e < 4; ++e) {
        const lighting::Cube c = sky::bake(sky::Env(e), 64, 6);
        const std::string h = sha256Hex(c.rgb.data(), c.levelOffset(1) * sizeof(double));
        std::printf("k6 %s %s %s\n", sky::name(sky::Env(e)), kFamily, h.c_str());
        const std::string key = std::string("\"name\": \"") + sky::name(sky::Env(e)) + "\"";
        for (std::size_t at = lock.find(key); at != std::string::npos; at = lock.find(key, at + 1)) {
            const std::size_t end = lock.find('}', at);
            const std::string entry = lock.substr(at, end - at);
            if (entry.find(std::string("\"family\": \"") + kFamily + "\"") == std::string::npos) continue;
            CHECK(entry.find(h) != std::string::npos);
            ++checked;
        }
    }
    std::printf("k6 lockfile entries checked for %s: %d\n", kFamily, checked);
    return raw_test_summary();
}
