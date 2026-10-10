#pragma once
// ACES 2.0 output transform: a port of OpenColorIO v2.6.0
// (src/OpenColorIO/ops/fixedfunction/ACES2, tag commit 10a43ec1),
// SPDX-License-Identifier: BSD-3-Clause, Copyright Contributors to the
// OpenColorIO Project. The port keeps OCIO's float32 arithmetic and its order of
// operations so the CPU reference tracks OCIO's own CPU path; the licence text is
// in third_party/NOTICE-OpenColorIO.md. Changes: namespaces, std::array types,
// no SIMD branches, and the matrix helpers built on raw::colour. Two changes to the
// arithmetic (2026-10-10, M1 criterion 3): the hue comes from hue_atan2 below, and the
// forward transform takes cos and sin of the hue as a / M and b / M rather than through
// the angle. Both keep the WGSL path within a few ulp of this one; see
// evidence/m1-hdr-f32-diagnosis.json.
#include "raw/renderer/colour.hpp"
#include <array>
#include <cmath>
namespace raw::colour::aces2 {

using f2 = std::array<float, 2>;
using f3 = std::array<float, 3>;
using m33f = std::array<float, 9>;

constexpr float PI = 3.14159265358979f;
constexpr float hue_limit = 360.0f;

inline float lerpf(float a, float b, float z){ return (b - a) * z + a; }
inline float wrap_to_hue_limit(float hue){
    float y = std::fmod(hue, hue_limit);
    return y < 0.f ? y + hue_limit : y;
}
inline constexpr float to_radians(float v){ return PI * v / 180.0f; }
// atan2 from arithmetic alone, so the CPU reference and the WGSL agree on the hue to a few
// ulp. Built-in atan2 may be off by 4096 ulp in WGSL, and the hue tables index by
// truncated degree, so a hue error near a whole degree moved saturated colours past
// M1 criterion 3's bound on SwiftShader (evidence/m1-hdr-signal-runs.json, 2026-10-10).
// The argument is reduced to |t| <= tan(15 deg), where the odd series to t^11 is within
// 3e-9 rad of atan. atan2(0, 0) is 0. web/colour/colour.wgsl hue_atan2 is the same
// sequence of operations; change both together.
inline float hue_atan2(float y, float x){
    const float ax = std::abs(x), ay = std::abs(y);
    if (ax == 0.f && ay == 0.f) return 0.f;
    const bool swap = ay > ax;
    float t = swap ? ax / ay : ay / ax;
    const bool big = t > 0.2679491924f;               // tan(15 deg)
    if (big) t = (t * 1.7320508076f - 1.f) / (1.7320508076f + t);
    const float t2 = t * t;
    float a = t * (1.f + t2 * (-1.f / 3.f + t2 * (1.f / 5.f + t2 * (-1.f / 7.f + t2 * (1.f / 9.f + t2 * (-1.f / 11.f))))));
    if (big) a = a + 0.5235987756f;                   // pi / 6
    if (swap) a = 1.5707963268f - a;
    if (x < 0.f) a = PI - a;
    return y < 0.f ? -a : a;
}
inline float from_radians_unwrapped(float v){       // v already within (-pi, pi]
    float y = 180.0f * v / PI;
    return y < 0.f ? y + hue_limit : y;
}

// Hue tables: 360 nominal entries with one wrap entry below and two above.
constexpr unsigned kLower = 1, kUpper = 2, kNominal = 360, kTotal = kNominal + kLower + kUpper;
constexpr unsigned kBase = kLower, kLowerWrap = 0, kUpperWrap = kBase + kNominal;
constexpr unsigned kFirstNominal = kBase, kLastNominal = kUpperWrap - 1;
using Table1D = std::array<float, kTotal>;
using Table3D = std::array<f3, kTotal>;
inline unsigned hue_position_in_uniform_table(float wrapped_hue){ return static_cast<unsigned>(wrapped_hue); }

struct JMhParams {
    m33f MATRIX_RGB_to_CAM16_c, MATRIX_CAM16_c_to_RGB;
    m33f MATRIX_cone_response_to_Aab, MATRIX_Aab_to_cone_response;
    float F_L_n, cz, inv_cz, A_w_J, inv_A_w_J;
};
struct ToneScaleParams { float n, n_r, g, t_1, c_t, s_2, u_2, m_2, forward_limit, inverse_limit, log_peak; };
struct SharedCompressionParameters { float limit_J_max, model_gamma_inv; Table1D reach_m_table; };
struct ResolvedSharedCompressionParameters { float limit_J_max, model_gamma_inv, reachMaxM; };
struct ChromaCompressParams { float sat, sat_thr, compr, chroma_compress_scale; };
struct HueDependantGamutParams { float gamma_bottom_inv; f2 JMcusp; float gamma_top_inv, focusJ, analytical_threshold; };
struct GamutCompressParams {
    float mid_J, focus_dist, lower_hull_gamma_inv;
    std::array<int, 2> hue_linearity_search_range;
    Table1D hue_table;
    Table3D gamut_cusp_table;
};

// CAM constants
constexpr float reference_luminance = 100.f;
constexpr float L_A = 100.f;
constexpr float Y_b = 20.f;
constexpr f3 surround = {0.9f, 0.59f, 0.9f};
constexpr float J_scale = 100.0f;
constexpr float cam_nl_offset = 0.2713f * 100.0f;
constexpr float cam_nl_scale = 4.0f * 100.0f;
// Chroma compression
constexpr float chroma_compress = 2.4f, chroma_compress_fact = 3.3f;
constexpr float chroma_expand = 1.3f, chroma_expand_fact = 0.69f, chroma_expand_thr = 0.5f;
// Gamut compression
constexpr float smooth_cusps = 0.12f, smooth_m = 0.27f, cusp_mid_blend = 1.3f, focus_gain_blend = 0.3f;
constexpr float focus_distance = 1.35f, focus_distance_scaling = 1.75f, compression_threshold = 0.75f;
// Table generation
constexpr float gammaMinimum = 0.0f, gammaMaximum = 5.0f, gammaSearchStep = 0.4f, gammaAccuracy = 1e-5f;
constexpr int cuspCornerCount = 6, totalCornerCount = cuspCornerCount + 2, max_sorted_corners = 2 * cuspCornerCount;
constexpr float reach_cusp_tolerance = 1e-3f, display_cusp_tolerance = 1e-7f;

// Matrix helpers (OCIO's MatrixLib and ColorLib)
f3 mult_f_f3(float v, const f3& a);
f3 mult_f3_f33(const f3& a, const m33f& m);
m33f to_f33(const Mat3& m);
m33f RGBtoXYZ_f33(const Primaries& p);
m33f XYZtoRGB_f33(const Primaries& p);

// CAM, tone scale and chroma compression (aces2_cam.cpp)
float post_adaptation_cone_response_compression_fwd(float v);
float Y_to_J(float Y, const JMhParams& p);
f3 RGB_to_Aab(const f3& RGB, const JMhParams& p);
f3 Aab_to_JMh(const f3& Aab, const JMhParams& p);
f3 RGB_to_JMh(const f3& RGB, const JMhParams& p);
f3 JMh_to_Aab(const f3& JMh, float cos_hr, float sin_hr, const JMhParams& p);
f3 Aab_to_RGB(const f3& Aab, const JMhParams& p);
f3 JMh_to_RGB(const f3& JMh, const JMhParams& p);
float chroma_compress_norm(float cos_hr, float sin_hr, float chroma_compress_scale);
float tonescale_A_to_J_fwd(float A, const JMhParams& p, const ToneScaleParams& pt);
f3 chroma_compress_fwd(const f3& JMh, float J_ts, float Mnorm, const ResolvedSharedCompressionParameters& ps, const ChromaCompressParams& pc);
float model_gamma();
JMhParams init_JMhParams(const Primaries& P);
ToneScaleParams init_ToneScaleParams(float peakLuminance);
ChromaCompressParams init_ChromaCompressParams(float peakLuminance, const ToneScaleParams& ts);

// Tables (aces2_tables.cpp)
SharedCompressionParameters init_SharedCompressionParams(float peakLuminance, const JMhParams& in, const JMhParams& reach);
ResolvedSharedCompressionParameters resolve_CompressionParams(float hue, const SharedCompressionParameters& p);
GamutCompressParams init_GamutCompressParams(float peakLuminance, const JMhParams& in, const JMhParams& limit,
                                             const ToneScaleParams& ts, const SharedCompressionParameters& sh, const JMhParams& reach);

// Gamut compression (aces2_gamut.cpp)
float get_focus_gain(float J, float analytical_threshold, float limit_J_max, float focus_dist);
float solve_J_intersect(float J, float M, float focusJ, float maxJ, float slope_gain);
float compute_compression_vector_slope(float intersectJ, float focusJ, float limitJmax, float slope_gain);
float compute_focusJ(float cusp_J, float mid_J, float limit_J_max);
float find_gamut_boundary_intersection(const f2& JM_cusp, float J_max, float gamma_top_inv, float gamma_bottom_inv,
                                       float J_intersect_source, float slope, float J_intersect_cusp);
f3 gamut_compress_fwd(const f3& JMh, const ResolvedSharedCompressionParameters& ps, const GamutCompressParams& p);

// The whole forward transform: AP0 in, display-linear in the limiting primaries
// out (1.0 = 100 nits), before OCIO's post-transform clamp.
struct Transform {
    Transform(float peakLuminance, const Primaries& limiting);
    f3 forward(const f3& ap0) const;
    float peak;
    JMhParams pIn, pOut;
    ToneScaleParams t;
    SharedCompressionParameters s;
    ChromaCompressParams c;
    GamutCompressParams g;
};

}
