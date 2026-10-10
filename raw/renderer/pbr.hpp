#pragma once
// The glTF 2.0 material model and its ratified material extensions, as a float64
// reference (ROADMAP M3, materials). Every GPU shading path is checked against this.
//
// Directions are unit vectors in the shading frame: the normal is +z, the tangent +x
// and the bitangent +y. wo points to the viewer, wi to the light; both leave the
// surface. A wi with z < 0 is a light below the surface (transmission).
//
// The model, lobe by lobe (evidence/m3-materials-bounds.json states it in full):
//   specular  GGX + height-correlated Smith (anisotropic form) + Schlick F(f0, f90),
//             plus Kulla-Conty multiple scattering with coloured Fresnel;
//   metal     the specular lobe with f0 = base colour, f90 = 1;
//   dielectric a specular layer with f0 from KHR_materials_ior and _specular over a
//             Lambertian base weighted by (1 - E_s(mu_o))(1 - E_s(mu_i)) / (1 - E_s_avg);
//   clearcoat an isotropic GGX layer (f0 0.04) over everything, weighting what is below
//             by (1 - c E_c(mu_o))(1 - c E_c(mu_i));
//   sheen     Charlie distribution and visibility, weighting what is below by
//             min(1 - s E_sh(mu_o), 1 - s E_sh(mu_i));
//   transmission thin-walled (mirrored specular lobe) or, with volume, the rough
//             dielectric interface BTDF (Walter et al. 2007) and Beer-Lambert attenuation;
//   iridescence the Belcour-Barla thin-film Fresnel (KHR_materials_iridescence);
//   emission  emissive * emissiveStrength.
// Every term is reciprocal (the volume BTDF in the generalized sense).
#include <vector>
namespace raw::pbr {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kMinRoughness = 0.045;        // perceptual; alpha >= 0.002025
inline constexpr double kMinSheenRoughness = 0.07;

struct D3 { double x{0}, y{0}, z{1}; };
struct Rgb {
    double r{0}, g{0}, b{0};
    double max3() const { return r > g ? (r > b ? r : b) : (g > b ? g : b); }
};

struct Material {
    Rgb baseColor{1, 1, 1};
    double metallic{1}, roughness{1};
    double ior{1.5};                                   // KHR_materials_ior
    double specular{1}; Rgb specularColor{1, 1, 1};    // KHR_materials_specular
    double clearcoat{0}, clearcoatRoughness{0};        // KHR_materials_clearcoat
    D3 coatNormal{0, 0, 1};                            //   in the shading frame
    Rgb sheenColor{0, 0, 0}; double sheenRoughness{0}; // KHR_materials_sheen
    double transmission{0};                            // KHR_materials_transmission
    bool volume{false};                                // KHR_materials_volume present
    double thickness{0}, attenuationDistance{0};       //   0 distance means none
    Rgb attenuationColor{1, 1, 1};
    double anisotropy{0}, anisotropyRotation{0};       // KHR_materials_anisotropy (radians)
    double iridescence{0}, iridescenceIor{1.3};        // KHR_materials_iridescence
    double iridescenceThickness{400};                  //   nanometres
    Rgb emissive{0, 0, 0}; double emissiveStrength{1}; // KHR_materials_emissive_strength
    // The extension texts' forms where raw-native departs from them (pending the author's
    // decision, 2026-10-10): iridescence summed to the second harmonic, and the clearcoat
    // weighted by Fresnel at the view alone, base * (1 - c F(n.v)) + c F(n.v) D V, with no
    // multiple scattering in the coat. Neither form is reciprocal or energy-checked here.
    bool specExact{false};
};

// Tabulated energies, built once by deterministic quadrature and VNDF sampling.
// A, B: split directional albedo of the white GGX lobe, F = f0 A + f90 B for Schlick,
// on a grid of view angle and perceptual roughness (bilinear). Sh: directional albedo
// of the white sheen lobe. The *Avg rows are cosine-weighted means over mu.
class Tables {
public:
    static constexpr int kTheta = 128, kRough = 128, kSamples = 1 << 12;
    static constexpr int kSheenMu = 64, kSheenRough = 64;
    static constexpr double kThetaPad = 0.05, kRoughMax = 1.05;
    Tables();
    double A(double mu, double r) const;
    double B(double mu, double r) const;
    double E(double mu, double r) const { return A(mu, r) + B(mu, r); }
    double Aavg(double r) const;
    double Bavg(double r) const;
    double Eavg(double r) const { return Aavg(r) + Bavg(r); }
    double Sh(double mu, double r) const;
    // Elevation from the normal (radians) of the centroid of the white single-scattering
    // lobe f cos for a view at mu, in the plane of the normal and the view: the direction
    // image lighting reads the prefiltered environment along (VNDF sampled, from the BRDF
    // alone; at roughness 0 it is the mirror direction).
    static constexpr int kDomMu = 64, kDomRough = 64;
    double Dom(double mu, double r) const;
    // Anisotropic split albedo (KHR_materials_anisotropy): view cosine, view azimuth from
    // the anisotropy direction (radians, folded to [0, pi/2]), perceptual roughness r and
    // anisotropy k, read quadrilinearly; the means are over the whole hemisphere.
    static constexpr int kAnMu = 32, kAnPhi = 8, kAnRough = 24, kAnK = 12, kAnSamples = 1 << 10;
    double A4(double mu, double phi, double r, double k) const { return read4(an_a_, mu, phi, r, k); }
    double B4(double mu, double phi, double r, double k) const { return read4(an_b_, mu, phi, r, k); }
    double Aavg2(double r, double k) const { return read2rk(an_aavg_, r, k); }
    double Bavg2(double r, double k) const { return read2rk(an_bavg_, r, k); }
    // The raw grids, for upload to a GPU (row-major: rough rows of theta columns).
    const std::vector<double>& gridA() const { return a_; }
    const std::vector<double>& gridB() const { return b_; }
    const std::vector<double>& gridSh() const { return sh_; }
    const std::vector<double>& gridDom() const { return dom_; }
    const std::vector<double>& rowAavg() const { return aavg_; }
    const std::vector<double>& rowBavg() const { return bavg_; }
    const std::vector<double>& gridA4() const { return an_a_; }
    const std::vector<double>& gridB4() const { return an_b_; }
    const std::vector<double>& gridAavg2() const { return an_aavg_; }
    const std::vector<double>& gridBavg2() const { return an_bavg_; }
private:
    double read4(const std::vector<double>& g, double mu, double phi, double r, double k) const;
    double read2rk(const std::vector<double>& g, double r, double k) const;
    void buildAnisotropic();
    std::vector<double> a_, b_, sh_, aavg_, bavg_;
    std::vector<double> an_a_, an_b_, an_aavg_, an_bavg_;   // [k][r][phi][mu] and [k][r]
    std::vector<double> dom_;
    void buildDominant();
};

// One evaluation split by lobe shape, so a test can integrate each with the quadrature
// that suits it. total() is the BSDF value f (no cosine), per channel.
struct Terms {
    Rgb specular;      // base GGX single scattering (peaked at the mirror direction)
    Rgb coat;          // clearcoat GGX single scattering (peaked)
    Rgb smooth;        // diffuse, sheen, multiple scattering
    Rgb transmission;  // below the surface (peaked about the mirrored or refracted direction)
    Rgb total() const;
};

Terms evalTerms(const Material& m, const Tables& t, D3 wo, D3 wi);

// The material's response to image lighting at one view (the split sum; lighting.hpp pairs
// each weight with one environment lookup). The same energy model as eval: for a uniform
// environment of radiance L the weights sum to the material's directional albedo times L.
struct IblResponse {
    Rgb specular; double roughness{1};      // x prefiltered radiance along the reflection
    Rgb coat; double coatRoughness{1};      // x prefiltered radiance along the coat's reflection
    Rgb irradiance;                         // x irradiance / pi at the normal (diffuse, multiple scattering, sheen)
    Rgb transmission;                       // x prefiltered radiance along the transmitted direction, at roughness
};
IblResponse iblResponse(const Material& m, const Tables& t, D3 wo);
Rgb eval(const Material& m, const Tables& t, D3 wo, D3 wi);
Rgb emission(const Material& m);

// Lobe building blocks, exposed for the tests.
double alphaOf(double roughness);                       // clamp, then r^2
void anisoAlphas(const Material& m, double& ax, double& ay);
double ggxD(D3 h, double ax, double ay);                // h unit, in the frame
double smithV(D3 wo, D3 wi, double ax, double ay);      // G2 / (4 |mu_o| |mu_i|)
double smithG2(D3 wo, D3 wi, double ax, double ay);
double schlick(double f0, double f90, double cosine);
double charlieD(D3 h, double sheenRoughness);
// Charlie visibility (Estevez and Kulla 2017, the fitted lambda KHR_materials_sheen gives):
// 1 / ((1 + lambda(mu_o) + lambda(mu_i)) 4 mu_o mu_i), alpha = sheenRoughness^2.
double charlieV(double muO, double muI, double sheenRoughness);
// Rough dielectric transmission (Walter et al. 2007): light arrives along `in` from the
// side of index etaIn and leaves along `out` on the side of index etaOut; in and out are
// on opposite sides. f0 is the Schlick reflectance taken at the cosine on the side of
// the lower index.
double walterBtdf(D3 in, double etaIn, D3 out, double etaOut, double ax, double ay, double f0);

// Thin-film Fresnel (Belcour and Barla 2017, as KHR_materials_iridescence writes it):
// cosTheta1 outside the film, film ior and thickness (nm), the base's F0 per channel.
// The specification sums the harmonics m = 1 and 2. raw-native sums eight: with a
// reflective base at grazing angles the series converges slowly, and two harmonics left
// an error of 0.32 against the spectral reference (2026-10-10; eight and twenty-four
// agree to 1e-7). Below about cos 0.3 on metals this renders closer to the physics than
// the Khronos sample viewer does, by design.
inline constexpr int kIridescenceHarmonics = 8;
Rgb iridescentFresnel(double outsideIor, double filmIor, double thicknessNm, Rgb baseF0, double cosTheta1,
                      int harmonics = kIridescenceHarmonics);
inline constexpr int kIridescenceHarmonicsSpec = 2;       // the KHR_materials_iridescence text
// The float64 spectral reference for it: the Airy reflectance with the same interface
// terms, integrated over 380 to 780 nm against CIE 1931 (Wyman, Sloan, Shirley 2013),
// in linear Rec.709, scaled so a constant spectrum maps to grey.
Rgb iridescentSpectral(double outsideIor, double filmIor, double thicknessNm, Rgb baseF0, double cosTheta1);

}  // namespace raw::pbr
