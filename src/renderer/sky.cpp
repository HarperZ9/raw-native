// Analytic skies and baked environments: see raw/renderer/sky.hpp.
#include "raw/renderer/sky.hpp"
#include "raw/renderer/lighting_cube.hpp"
#include "raw/renderer/noise.hpp"
#include <algorithm>
#include <cmath>
namespace raw::sky {
namespace {
constexpr double kPi = 3.14159265358979323846;
double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 unit(D3 a) { const double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }
struct Perez { double A, B, C, D, E; };
double perez(const Perez& p, double cosTheta, double gamma) {
    const double ct = std::max(cosTheta, 0.01), cg = std::cos(gamma);
    return (1.0 + p.A * std::exp(p.B / ct)) * (1.0 + p.C * std::exp(p.D * gamma) + p.E * cg * cg);
}
Perez perezY(double T) { return {0.1787 * T - 1.4630, -0.3554 * T + 0.4275, -0.0227 * T + 5.3251, 0.1206 * T - 2.5771, -0.0670 * T + 0.3703}; }
Perez perezx(double T) { return {-0.0193 * T - 0.2592, -0.0665 * T + 0.0008, -0.0004 * T + 0.2125, -0.0641 * T - 0.8989, -0.0033 * T + 0.0452}; }
Perez perezy(double T) { return {-0.0167 * T - 0.2608, -0.0950 * T + 0.0092, -0.0079 * T + 0.2102, -0.0441 * T - 1.6537, -0.0109 * T + 0.0529}; }
// Zenith chromaticity: [T^2, T, 1] M [ts^3, ts^2, ts, 1].
double zenithChroma(const double M[3][4], double T, double ts) {
    const double t[3] = {T * T, T, 1.0}, s[4] = {ts * ts * ts, ts * ts, ts, 1.0};
    double r = 0.0;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 4; ++j) r += t[i] * M[i][j] * s[j];
    return r;
}
constexpr double kMx[3][4] = {{0.00166, -0.00375, 0.00209, 0.0}, {-0.02903, 0.06377, -0.03202, 0.00394}, {0.11693, -0.21196, 0.06052, 0.25886}};
constexpr double kMy[3][4] = {{0.00275, -0.00610, 0.00317, 0.0}, {-0.04214, 0.08970, -0.04153, 0.00516}, {0.15346, -0.26756, 0.06670, 0.26688}};
Rgb xyYtoRgb(double x, double y, double Y) {
    const double X = x / y * Y, Z = (1.0 - x - y) / y * Y;
    return {3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z, 0.0557 * X - 0.2040 * Y + 1.0570 * Z};
}
double gammaOf(D3 a, D3 b) { return std::acos(std::clamp(dot(a, b), -1.0, 1.0)); }
}  // namespace

double preethamZenithNits(const Clear& c) {
    const double T = c.turbidity, ts = std::acos(std::clamp(c.sun.y, 0.0, 1.0));
    const double chi = (4.0 / 9.0 - T / 120.0) * (kPi - 2.0 * ts);
    return 1000.0 * ((4.0453 * T - 4.9710) * std::tan(chi) - 0.2155 * T + 2.4192);   // kcd/m^2 to nits
}

Rgb preetham(const Clear& c, D3 dir) {
    const D3 d = unit(dir);
    if (d.y < 0.0) {   // the ground: a diffuse plane under the horizon's sky at this azimuth
        D3 h{d.x, 0.0, d.z};
        const double l = std::sqrt(h.x * h.x + h.z * h.z);
        h = l > 1e-9 ? D3{h.x / l, 0.01, h.z / l} : D3{1.0, 0.01, 0.0};
        const Rgb s = preetham(c, h);
        return {s.r * c.groundAlbedo, s.g * c.groundAlbedo, s.b * c.groundAlbedo};
    }
    const double T = c.turbidity, ts = std::acos(std::clamp(c.sun.y, 0.0, 1.0)), g = gammaOf(d, unit(c.sun));
    const Perez pY = perezY(T), px = perezx(T), py = perezy(T);
    const double Yz = preethamZenithNits(c), xz = zenithChroma(kMx, T, ts), yz = zenithChroma(kMy, T, ts);
    const double Y = Yz * perez(pY, d.y, g) / perez(pY, 1.0, ts);
    const double x = xz * perez(px, d.y, g) / perez(px, 1.0, ts), y = yz * perez(py, d.y, g) / perez(py, 1.0, ts);
    const Rgb r = xyYtoRgb(x, y, Y);
    return {std::max(r.r, 0.0), std::max(r.g, 0.0), std::max(r.b, 0.0)};
}

double cieClearRelative(D3 sun, D3 dir) {
    const Perez p{-1.0, -0.32, 10.0, -3.0, 0.45};
    const D3 d = unit(dir), s = unit(sun);
    const double ts = std::acos(std::clamp(s.y, -1.0, 1.0));
    return perez(p, std::max(d.y, 0.0), gammaOf(d, s)) / perez(p, 1.0, ts);
}

Rgb overcast(double zenithNits, D3 dir, double groundAlbedo) {
    const D3 d = unit(dir);
    const double L = zenithNits * (1.0 + 2.0 * std::max(d.y, 0.0)) / 3.0;
    const double v = d.y >= 0.0 ? L : zenithNits / 3.0 * groundAlbedo;
    return {v, v, v};
}

Rgb sunIlluminance(const Clear& c) {
    const double m = 1.0 / std::max(unit(c.sun).y, 0.05), tau = 0.08 + 0.025 * c.turbidity;
    return {1.0e5 * std::exp(-tau * 0.6 * m), 1.0e5 * std::exp(-tau * m), 1.0e5 * std::exp(-tau * 1.8 * m)};
}

const char* name(Env e) {
    switch (e) { case Env::ClearNoon: return "clear_noon"; case Env::Overcast: return "overcast"; case Env::GoldenHour: return "golden_hour"; default: return "night"; }
}

double texelSolidAngle(int size, double u, double v) {
    const double sc = 2.0 * u - 1.0, tc = 2.0 * v - 1.0, a = 2.0 / size;
    return a * a / std::pow(1.0 + sc * sc + tc * tc, 1.5);
}

lighting::Cube bake(Env e, int size, int levels, D3 (*viewToWorld)(D3)) {
    const auto W = [&](D3 d) { return viewToWorld ? viewToWorld(d) : d; };
    lighting::Cube c; c.size = size; c.levels = levels;
    c.rgb.assign(c.levelOffset(levels), 0.0);
    const auto put = [&](int f, int y, int x, Rgb v, bool add) {
        const std::size_t i = ((std::size_t(f) * size + y) * size + x) * 3;
        if (add) { c.rgb[i] += v.r; c.rgb[i + 1] += v.g; c.rgb[i + 2] += v.b; } else { c.rgb[i] = v.r; c.rgb[i + 1] = v.g; c.rgb[i + 2] = v.b; }
    };
    const auto deposit = [&](D3 dir, Rgb E) {   // a disk's illuminance into the texel holding its direction
        D3 dv = unit(dir);
        if (viewToWorld) {   // the view direction whose world direction is `dir`: the rotation's transpose
            const D3 ex = viewToWorld({1, 0, 0}), ey = viewToWorld({0, 1, 0}), ez = viewToWorld({0, 0, 1});
            dv = {dot(dv, ex), dot(dv, ey), dot(dv, ez)};
        }
        double u, v; const int f = lighting::cube::faceOf(dv, u, v);
        const int x = std::min(int(u * size), size - 1), y = std::min(int(v * size), size - 1);
        const double om = texelSolidAngle(size, (x + 0.5) / size, (y + 0.5) / size);
        put(f, y, x, {E.r / om, E.g / om, E.b / om}, true);
    };
    const auto elev = [](double deg, double az) { const double a = deg * kPi / 180.0, b = az * kPi / 180.0; return D3{std::cos(a) * std::cos(b), std::sin(a), std::cos(a) * std::sin(b)}; };
    Clear cl;
    if (e == Env::ClearNoon) { cl.turbidity = 2.5; cl.sun = elev(70.0, 30.0); }
    if (e == Env::GoldenHour) { cl.turbidity = 4.0; cl.sun = elev(4.0, 200.0); }
    for (int f = 0; f < 6; ++f) for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const D3 d = W(lighting::cube::dirOf(f, (x + 0.5) / size, (y + 0.5) / size));
        Rgb v;
        if (e == Env::Overcast) v = overcast(8000.0, d);
        else if (e == Env::Night) {
            const double up = std::max(d.y, 0.0);
            v = d.y >= 0.0 ? Rgb{0.0008 + 0.0012 * (1.0 - up), 0.0012 + 0.0015 * (1.0 - up), 0.0030 + 0.0010 * (1.0 - up)} : Rgb{0.0002, 0.0002, 0.0002};
            if (d.y > 0.05 && noise::white(f * 4096 + y, x, 0, 0x57a7u) > 0.9965f) {   // stars: artistic, not a catalogue
                const double b = 0.2 + 2.0 * double(noise::white(x, y, f, 0xb1u));
                v = {v.r + b, v.g + b, v.b + b * 1.1};
            }
        } else v = preetham(cl, d);
        put(f, y, x, v, false);
    }
    if (e == Env::ClearNoon || e == Env::GoldenHour) deposit(cl.sun, sunIlluminance(cl));
    if (e == Env::Night) deposit(elev(35.0, 120.0), {0.25, 0.25, 0.27});   // full-moon illuminance, lux
    return c;
}

}  // namespace raw::sky
