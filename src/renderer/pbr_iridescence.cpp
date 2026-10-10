// Thin-film iridescence: the Belcour-Barla fast path and its spectral reference.
// See raw/renderer/pbr.hpp. The fast path follows the KHR_materials_iridescence
// specification (CC BY 4.0) term for term; the reference is written from the physics.
#include "raw/renderer/pbr.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
namespace raw::pbr {
namespace {
double iorToF0(double transmitted, double incident) {
    const double q = (transmitted - incident) / (transmitted + incident);
    return q * q;
}
double f0ToIor(double f0) { const double s = std::sqrt(f0); return (1.0 + s) / (1.0 - s); }

// XYZ to linear Rec.709 (D65 primaries; the specification's matrix).
Rgb xyzToRec709(double x, double y, double z) {
    return {3.2404542 * x - 1.5371385 * y - 0.4985314 * z,
            -0.9692660 * x + 1.8760108 * y + 0.0415560 * z,
            0.0556434 * x - 0.2040259 * y + 1.0572252 * z};
}
// The Fourier-domain sensitivity of the specification: XYZ fitted by Gaussians.
Rgb evalSensitivity(double opd, const double shift[3]) {
    const double phase = 2.0 * kPi * opd * 1.0e-9;
    const double val[3] = {5.4856e-13, 4.4201e-13, 5.2481e-13};
    const double pos[3] = {1.6810e+06, 1.7953e+06, 2.2084e+06};
    const double var[3] = {4.3278e+09, 9.3046e+09, 6.6121e+09};
    double xyz[3];
    for (int k = 0; k < 3; ++k)
        xyz[k] = val[k] * std::sqrt(2.0 * kPi * var[k]) * std::cos(pos[k] * phase + shift[k]) * std::exp(-phase * phase * var[k]);
    xyz[0] += 9.7470e-14 * std::sqrt(2.0 * kPi * 4.5282e+09) * std::cos(2.2399e+06 * phase + shift[0]) * std::exp(-4.5282e+09 * phase * phase);
    for (double& v : xyz) v /= 1.0685e-7;
    return xyzToRec709(xyz[0], xyz[1], xyz[2]);
}
double ch(const Rgb& c, int k) { return k == 0 ? c.r : k == 1 ? c.g : c.b; }
void setCh(Rgb& c, int k, double v) { (k == 0 ? c.r : k == 1 ? c.g : c.b) = v; }

// The interface terms both paths share. Returns false on total internal reflection.
struct Film { double r12, t121, phi21, cos2, opd; double r23[3], phi23[3]; };
bool film(double outsideIor, double filmIor, double d, Rgb baseF0, double cos1, Film& f) {
    const double s2 = (outsideIor / filmIor) * (outsideIor / filmIor) * (1.0 - cos1 * cos1), c2sq = 1.0 - s2;
    if (c2sq < 0.0) return false;
    f.cos2 = std::sqrt(c2sq);
    f.r12 = schlick(iorToF0(filmIor, outsideIor), 1.0, cos1);
    f.t121 = 1.0 - f.r12;
    const double phi12 = filmIor < outsideIor ? kPi : 0.0;
    f.phi21 = kPi - phi12;
    for (int k = 0; k < 3; ++k) {
        const double baseIor = f0ToIor(ch(baseF0, k) + 0.0001);
        f.r23[k] = schlick(iorToF0(baseIor, filmIor), 1.0, f.cos2);
        f.phi23[k] = baseIor < filmIor ? kPi : 0.0;
    }
    f.opd = 2.0 * filmIor * d * f.cos2;
    return true;
}

// Wyman, Sloan and Shirley 2013, the multi-lobe fit to CIE 1931 2-degree.
double g(double l, double mu, double s1, double s2) { const double t = (l - mu) / (l < mu ? s1 : s2); return std::exp(-0.5 * t * t); }
void cmf(double l, double& x, double& y, double& z) {
    x = 1.056 * g(l, 599.8, 37.9, 31.0) + 0.362 * g(l, 442.0, 16.0, 26.7) - 0.065 * g(l, 501.1, 20.4, 26.2);
    y = 0.821 * g(l, 568.8, 46.9, 40.5) + 0.286 * g(l, 530.9, 16.3, 31.1);
    z = 1.217 * g(l, 437.0, 11.8, 36.0) + 0.681 * g(l, 459.0, 26.0, 13.8);
}

// The sensitivity tabulated from the same colour matching fit the references integrate:
// T_k(x) = [M sum_lambda cmf(lambda) e^{2 pi i x / lambda}]_k / [M sum_lambda cmf(lambda)]_k for an
// optical path difference x, 380 to 780 nm by 1 nm, every 10 nm of x up to kSensMax (linear in
// between). Re(T_k(x) e^{i shift}) is then exactly the reference's response to cos(2 pi x / lambda +
// shift), so the series matches the spectral integral up to its truncation.
struct SensTable { std::vector<double> re[3], im[3]; };
const SensTable& sensTable() {
    static const SensTable t = [] {
        SensTable s;
        const int n = int(kSensMax / kSensStep) + 1;
        double wx = 0, wy = 0, wz = 0;
        for (int l = 380; l <= 780; ++l) { double x, y, z; cmf(l, x, y, z); wx += x; wy += y; wz += z; }
        const Rgb w = xyzToRec709(wx, wy, wz);
        for (int k = 0; k < 3; ++k) { s.re[k].resize(std::size_t(n)); s.im[k].resize(std::size_t(n)); }
        for (int j = 0; j < n; ++j) {
            const double opd = j * kSensStep;
            double rx = 0, ry = 0, rz = 0, ix = 0, iy = 0, iz = 0;
            for (int l = 380; l <= 780; ++l) {
                double x, y, z; cmf(l, x, y, z);
                const double a = 2.0 * kPi * opd / l, c = std::cos(a), sn = std::sin(a);
                rx += x * c; ry += y * c; rz += z * c; ix += x * sn; iy += y * sn; iz += z * sn;
            }
            const Rgb re = xyzToRec709(rx, ry, rz), im = xyzToRec709(ix, iy, iz);
            s.re[0][std::size_t(j)] = re.r / w.r; s.re[1][std::size_t(j)] = re.g / w.g; s.re[2][std::size_t(j)] = re.b / w.b;
            s.im[0][std::size_t(j)] = im.r / w.r; s.im[1][std::size_t(j)] = im.g / w.g; s.im[2][std::size_t(j)] = im.b / w.b;
        }
        return s;
    }();
    return t;
}
Rgb tabulatedSensitivity(double opd, const double shift[3]) {
    const SensTable& t = sensTable();
    const double u = std::clamp(opd / kSensStep, 0.0, double(t.re[0].size() - 1));
    const std::size_t j = std::min(std::size_t(u), t.re[0].size() - 2);
    const double f = u - double(j);
    Rgb out;
    for (int k = 0; k < 3; ++k) {
        const double re = t.re[k][j] * (1 - f) + t.re[k][j + 1] * f, im = t.im[k][j] * (1 - f) + t.im[k][j + 1] * f;
        setCh(out, k, re * std::cos(shift[k]) - im * std::sin(shift[k]));   // Re((re + i im) e^{i shift})
    }
    return out;
}

// One polarization's interface terms: R12, the film's phase phi21, and per channel R23 and
// phi23, from exact Fresnel amplitudes (complex past total internal reflection at the base).
struct Pol { double r12, t121, phi21; double r23[3], phi23[3]; };
void polarized(double n1, double n2, double cos1, double cos2, const double n3[3], bool p, Pol& o) {
    using C = std::complex<double>;
    const double r12 = p ? (n2 * cos1 - n1 * cos2) / (n2 * cos1 + n1 * cos2) : (n1 * cos1 - n2 * cos2) / (n1 * cos1 + n2 * cos2);
    o.r12 = r12 * r12;
    o.t121 = 1.0 - o.r12;
    o.phi21 = -r12 >= 0.0 ? 0.0 : kPi;                                   // r21 = -r12
    const double sin2sq = 1.0 - cos2 * cos2;
    for (int k = 0; k < 3; ++k) {
        const C c3 = std::sqrt(C(1.0 - (n2 / n3[k]) * (n2 / n3[k]) * sin2sq, 0.0));
        const C r23 = p ? (n3[k] * cos2 - n2 * c3) / (n3[k] * cos2 + n2 * c3) : (n2 * cos2 - n3[k] * c3) / (n2 * cos2 + n3[k] * c3);
        o.r23[k] = std::norm(r23);
        o.phi23[k] = std::arg(r23);
    }
}
// The Belcour-Barla series for one set of interface terms.
Rgb series(double r12, double t121, double phi21, const double r23[3], const double phi23[3], double opd, int harmonics, bool tabulated) {
    Rgb out;
    double phi[3], cm[3], r123[3];
    for (int k = 0; k < 3; ++k) {
        phi[k] = phi21 + phi23[k];
        const double R123 = std::clamp(r12 * r23[k], 1e-5, 0.9999);
        r123[k] = std::sqrt(R123);
        const double rs = t121 * t121 * r23[k] / (1.0 - R123);
        setCh(out, k, r12 + rs);
        cm[k] = rs - t121;
    }
    for (int m = 1; m <= harmonics; ++m) {
        const double shift[3] = {m * phi[0], m * phi[1], m * phi[2]};
        const Rgb sm = tabulated ? tabulatedSensitivity(m * opd, shift) : evalSensitivity(m * opd, shift);
        for (int k = 0; k < 3; ++k) { cm[k] *= r123[k]; setCh(out, k, ch(out, k) + cm[k] * 2.0 * ch(sm, k)); }
    }
    return out;
}
}  // namespace

Rgb iridescentFresnel(double outsideIor, double filmIor, double d, Rgb baseF0, double cos1, int harmonics, bool pol) {
    Film f;
    if (!film(outsideIor, filmIor, d, baseF0, cos1, f)) return {1, 1, 1};
    Rgb out;
    // A real index from F0 describes a dielectric; above F0 0.25 (index 3) the base is treated as
    // a conductor and keeps the Schlick interfaces until a complex-index reference exists
    // (evidence/m3-materials2-bounds.json, not_covered_yet).
    if (pol && std::max({baseF0.r, baseF0.g, baseF0.b}) > 0.25) pol = false;
    if (!pol) out = series(f.r12, f.t121, f.phi21, f.r23, f.phi23, f.opd, harmonics, false);
    else {
        double n3[3];
        for (int k = 0; k < 3; ++k) n3[k] = f0ToIor(ch(baseF0, k) + 0.0001);
        Pol s, p;
        polarized(outsideIor, filmIor, cos1, f.cos2, n3, false, s);
        polarized(outsideIor, filmIor, cos1, f.cos2, n3, true, p);
        const Rgb a = series(s.r12, s.t121, s.phi21, s.r23, s.phi23, f.opd, harmonics, true);
        const Rgb b = series(p.r12, p.t121, p.phi21, p.r23, p.phi23, f.opd, harmonics, true);
        out = {0.5 * (a.r + b.r), 0.5 * (a.g + b.g), 0.5 * (a.b + b.b)};
    }
    return {std::max(out.r, 0.0), std::max(out.g, 0.0), std::max(out.b, 0.0)};
}

// The Airy reflectance |rho12 + T121 rho23 e^{i delta} / (1 - rho21 rho23 e^{i delta})|^2 at each
// wavelength, with the same interface reflectances and phases; its mean over delta is the
// specification's DC term C0, which stays neutral, and the rest goes through the colour
// matching functions normalised by the integral of y-bar, as the fast path does.
Rgb iridescentSpectral(double outsideIor, double filmIor, double d, Rgb baseF0, double cos1) {
    Film f;
    if (!film(outsideIor, filmIor, d, baseF0, cos1, f)) return {1, 1, 1};
    Rgb out;
    for (int k = 0; k < 3; ++k) {
        using C = std::complex<double>;
        const C rho12 = std::polar(std::sqrt(f.r12), kPi - f.phi21), rho21 = std::polar(std::sqrt(f.r12), f.phi21);
        const C rho23 = std::polar(std::sqrt(f.r23[k]), f.phi23[k]);
        const double R123 = f.r12 * f.r23[k], c0 = f.r12 + f.t121 * f.t121 * f.r23[k] / (1.0 - R123);
        double X = 0, Y = 0, Z = 0, Yn = 0;
        for (int l = 380; l <= 780; ++l) {
            const C e = std::polar(1.0, 2.0 * kPi * f.opd / l);
            const C r = rho12 + f.t121 * rho23 * e / (1.0 - rho21 * rho23 * e);
            double x, y, z;
            cmf(l, x, y, z);
            const double ac = std::norm(r) - c0;
            X += ac * x; Y += ac * y; Z += ac * z; Yn += y;
        }
        setCh(out, k, c0 + ch(xyzToRec709(X / Yn, Y / Yn, Z / Yn), k));
    }
    return {std::max(out.r, 0.0), std::max(out.g, 0.0), std::max(out.b, 0.0)};
}

std::vector<double> iridescenceSensitivityGrid() {
    const SensTable& t = sensTable();
    std::vector<double> g;
    g.reserve(t.re[0].size() * 6);
    for (std::size_t j = 0; j < t.re[0].size(); ++j) {
        for (int k = 0; k < 3; ++k) g.push_back(t.re[k][j]);
        for (int k = 0; k < 3; ++k) g.push_back(t.im[k][j]);
    }
    return g;
}

Rgb iridescentExact(double n1, double n2, double d, double n3, double cos1) {
    using C = std::complex<double>;
    const double s1 = 1.0 - cos1 * cos1;
    const C c2 = std::sqrt(C(1.0 - (n1 / n2) * (n1 / n2) * s1, 0.0)), c3 = std::sqrt(C(1.0 - (n1 / n3) * (n1 / n3) * s1, 0.0));
    double X = 0, Y = 0, Z = 0, Wx = 0, Wy = 0, Wz = 0;
    for (int l = 380; l <= 780; ++l) {
        const C e = std::exp(C(0.0, 4.0 * kPi * n2 * d / l) * c2);
        double R = 0.0;
        for (int pol = 0; pol < 2; ++pol) {
            const C r12 = pol ? (n2 * cos1 - n1 * c2) / (n2 * cos1 + n1 * c2) : (n1 * cos1 - n2 * c2) / (n1 * cos1 + n2 * c2);
            const C r23 = pol ? (n3 * c2 - n2 * c3) / (n3 * c2 + n2 * c3) : (n2 * c2 - n3 * c3) / (n2 * c2 + n3 * c3);
            R += 0.5 * std::norm((r12 + r23 * e) / (1.0 + r12 * r23 * e));
        }
        double x, y, z;
        cmf(l, x, y, z);
        X += R * x; Y += R * y; Z += R * z; Wx += x; Wy += y; Wz += z;
    }
    const Rgb a = xyzToRec709(X, Y, Z), w = xyzToRec709(Wx, Wy, Wz);
    return {a.r / w.r, a.g / w.g, a.b / w.b};
}

}  // namespace raw::pbr
