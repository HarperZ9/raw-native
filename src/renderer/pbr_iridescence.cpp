// Thin-film iridescence: the Belcour-Barla fast path and its spectral reference.
// See raw/renderer/pbr.hpp. The fast path follows the KHR_materials_iridescence
// specification (CC BY 4.0) term for term; the reference is written from the physics.
#include "raw/renderer/pbr.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
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
}  // namespace

Rgb iridescentFresnel(double outsideIor, double filmIor, double d, Rgb baseF0, double cos1) {
    Film f;
    if (!film(outsideIor, filmIor, d, baseF0, cos1, f)) return {1, 1, 1};
    Rgb out;
    double phi[3];
    for (int k = 0; k < 3; ++k) phi[k] = f.phi21 + f.phi23[k];
    double cm[3], r123[3];
    for (int k = 0; k < 3; ++k) {
        const double R123 = std::clamp(f.r12 * f.r23[k], 1e-5, 0.9999);
        r123[k] = std::sqrt(R123);
        const double rs = f.t121 * f.t121 * f.r23[k] / (1.0 - R123);
        setCh(out, k, f.r12 + rs);
        cm[k] = rs - f.t121;
    }
    for (int m = 1; m <= kIridescenceHarmonics; ++m) {
        const double shift[3] = {m * phi[0], m * phi[1], m * phi[2]};
        const Rgb sm = evalSensitivity(m * f.opd, shift);
        for (int k = 0; k < 3; ++k) { cm[k] *= r123[k]; setCh(out, k, ch(out, k) + cm[k] * 2.0 * ch(sm, k)); }
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

}  // namespace raw::pbr
