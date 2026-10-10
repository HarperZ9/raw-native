#pragma once
// The base model's specular lobe (ROADMAP M2, gap 5): GGX microfacets with
// height-correlated Smith masking-shadowing, plus a multiple-scattering term so
// a white surface reflects all the light it receives at any roughness.
//
// Roughness r is perceptual; alpha = r^2. Cosines are against the normal:
// mu_o for the view, mu_i for the light, mu_h for the half vector.
//
// Single scattering:   f_ss = D(mu_h) * V(mu_o, mu_i) * F
// Multiple scattering (Kulla and Conty 2017, "Revisiting physically based shading at Imageworks"):
//                      f_ms = (1 - E(mu_o)) (1 - E(mu_i)) / (pi (1 - E_avg))
// where E is the directional albedo of f_ss with F = 1 and E_avg its cosine-weighted
// mean. E is tabulated once (EnergyTable) by VNDF importance sampling (Heitz 2018,
// "Sampling the GGX distribution of visible normals"). The white furnace in
// tests/test_brdf_furnace.cpp integrates the lobe by a different route and checks
// that the total is 1 (evidence/m2-white-furnace-bounds.json).
#include <vector>
namespace raw::brdf {

inline constexpr double kPi = 3.14159265358979323846;

double alphaOf(double roughness);                          // r^2
double ggxD(double muH, double alpha);                     // normal distribution, 1/sr
double smithV(double muO, double muI, double alpha);       // G2 / (4 mu_o mu_i), height-correlated
double smithG1(double mu, double alpha);
double smithG2(double muO, double muI, double alpha);
double schlickF(double f0, double voh);

// Directional albedo E(mu, r) of the white single-scattering lobe on a grid over the
// view angle theta and the roughness, read with bilinear interpolation. The grid runs a
// little past the ends of the valid range (theta from -kThetaPad, r up to kRoughMax) so
// that no tested point sits on a node and mu = 1 is interior; E depends only on cos(theta),
// so a negative theta is valid.
class EnergyTable {
public:
    static constexpr int kTheta = 128, kRough = 128, kSamples = 1 << 12;
    static constexpr double kThetaPad = 0.05, kRoughMax = 1.05;
    EnergyTable();                                         // builds the table (about 67 million samples, a few seconds)
    double E(double mu, double roughness) const;           // interpolated directional albedo
    double Eavg(double roughness) const;                   // 2 * integral of E(mu) mu dmu, from the table
private:
    std::vector<double> e_;                                // kRough rows of kTheta values
};

// Directional albedo of one direction by VNDF sampling on a Hammersley set of n points:
// the estimator G2 / G1 has F = 1 built in.
double albedoVndf(double mu, double alpha, int n);

// The multiple-scattering lobe for a white surface, from a table.
double multiScatter(const EnergyTable& t, double muO, double muI, double roughness);

// Gauss-Legendre nodes and weights on [0, 1].
void gaussLegendre01(int n, std::vector<double>& x, std::vector<double>& w);

}  // namespace raw::brdf
