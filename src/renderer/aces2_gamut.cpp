// ACES 2.0: gamut compression and the forward output transform. Port of OpenColorIO
// v2.6.0 ops/fixedfunction/ACES2/Transform.cpp and FixedFunctionOpCPU.cpp
// (BSD-3-Clause); see aces2.hpp.
#include "aces2.hpp"
#include <algorithm>
#include <cmath>
namespace raw::colour::aces2 {
namespace {
unsigned lookup_hue_interval(float h, const Table1D& hues, const std::array<int, 2>& range){
    unsigned i = kFirstNominal + hue_position_in_uniform_table(h);
    unsigned i_lo = std::max(int(kLowerWrap), int(i) + range[0]);
    unsigned i_hi = std::min(int(kUpperWrap), int(i) + range[1]);
    while (i_lo + 1 < i_hi){
        if (h > hues[i]) i_lo = i; else i_hi = i;
        i = (i_lo + i_hi) / 2;
    }
    return std::max(1U, i_hi);
}

float estimate_line_and_boundary_intersection_M(float J_axis_intersect, float slope, float inv_gamma,
                                                float J_max, float M_max, float J_intersection_reference){
    const float normalised_J = J_axis_intersect / J_intersection_reference;
    const float shifted = J_intersection_reference * std::pow(normalised_J, inv_gamma);
    return shifted * M_max / (J_max - slope * M_max);
}

float smin_scaled(float a, float b, float scale_reference){
    const float s = smooth_cusps * scale_reference;
    const float h = std::max(s - std::abs(a - b), 0.0f) / s;
    return std::min(a, b) - h * h * h * s * (1.f / 6.f);
}

float remap_M_fwd(float M, float gamut_boundary_M, float reach_boundary_M){
    const float proportion = std::max(gamut_boundary_M / reach_boundary_M, compression_threshold);
    const float threshold = proportion * gamut_boundary_M;
    if (M <= threshold || proportion >= 1.0f) return M;
    const float m_offset = M - threshold;
    const float gamut_offset = gamut_boundary_M - threshold;
    const float reach_offset = reach_boundary_M - threshold;
    const float scale = reach_offset / ((reach_offset / gamut_offset) - 1.0f);
    const float nd = m_offset / scale;
    return threshold + scale * nd / (1.0f + nd);
}

HueDependantGamutParams hue_params(float hue, const ResolvedSharedCompressionParameters& sr, const GamutCompressParams& p){
    HueDependantGamutParams h;
    h.gamma_bottom_inv = p.lower_hull_gamma_inv;
    const unsigned i_hi = lookup_hue_interval(hue, p.hue_table, p.hue_linearity_search_range);
    const float t = (hue - p.hue_table[i_hi - 1]) / (p.hue_table[i_hi] - p.hue_table[i_hi - 1]);
    const f3& lo = p.gamut_cusp_table[i_hi - 1];
    const f3& hi = p.gamut_cusp_table[i_hi];
    const f3 cusp = {lerpf(lo[0], hi[0], t), lerpf(lo[1], hi[1], t), lerpf(lo[2], hi[2], t)};
    h.JMcusp = {cusp[0], cusp[1]};
    h.gamma_top_inv = cusp[2];
    h.focusJ = compute_focusJ(h.JMcusp[0], p.mid_J, sr.limit_J_max);
    h.analytical_threshold = lerpf(h.JMcusp[0], sr.limit_J_max, focus_gain_blend);
    return h;
}
}

float get_focus_gain(float J, float analytical_threshold, float limit_J_max, float focus_dist){
    float gain = limit_J_max * focus_dist;
    if (J > analytical_threshold){
        float adj = std::log10((limit_J_max - analytical_threshold) / std::max(0.0001f, limit_J_max - J));
        adj = adj * adj + 1.f;
        gain = gain * adj;
    }
    return gain;
}

float solve_J_intersect(float J, float M, float focusJ, float maxJ, float slope_gain){
    const float M_scaled = M / slope_gain;
    const float a = M_scaled / focusJ;
    if (J < focusJ){
        const float b = 1.f - M_scaled;
        const float c = -J;
        const float root = std::sqrt(b * b - 4.f * a * c);
        return -2.f * c / (b + root);
    }
    const float b = -(1.f + M_scaled + maxJ * a);
    const float c = maxJ * M_scaled + J;
    const float root = std::sqrt(b * b - 4.f * a * c);
    return -2.f * c / (b - root);
}

float compute_compression_vector_slope(float intersectJ, float focusJ, float limitJmax, float slope_gain){
    const float direction_scaler = (intersectJ < focusJ) ? intersectJ : (limitJmax - intersectJ);
    return direction_scaler * (intersectJ - focusJ) / (focusJ * slope_gain);
}

float compute_focusJ(float cusp_J, float mid_J, float limit_J_max){
    return lerpf(cusp_J, mid_J, std::min(1.f, cusp_mid_blend - (cusp_J / limit_J_max)));
}

float find_gamut_boundary_intersection(const f2& JM_cusp, float J_max, float gamma_top_inv, float gamma_bottom_inv,
                                       float J_intersect_source, float slope, float J_intersect_cusp){
    const float lower = estimate_line_and_boundary_intersection_M(J_intersect_source, slope, gamma_bottom_inv, JM_cusp[0], JM_cusp[1], J_intersect_cusp);
    const float upper = estimate_line_and_boundary_intersection_M(J_max - J_intersect_source, -slope, gamma_top_inv,
                                                                  J_max - JM_cusp[0], JM_cusp[1], J_max - J_intersect_cusp);
    return smin_scaled(lower, upper, JM_cusp[1]);
}

f3 gamut_compress_fwd(const f3& JMh, const ResolvedSharedCompressionParameters& sr, const GamutCompressParams& p){
    const float J = JMh[0], M = JMh[1], h = JMh[2];
    if (J <= 0.0f) return {0.0f, 0.f, h};
    if (M <= 0.0f || J > sr.limit_J_max) return {J, 0.f, h};
    const HueDependantGamutParams hdp = hue_params(h, sr, p);
    const float slope_gain = get_focus_gain(J, hdp.analytical_threshold, sr.limit_J_max, p.focus_dist);
    const float Jis = solve_J_intersect(J, M, hdp.focusJ, sr.limit_J_max, slope_gain);
    const float slope = compute_compression_vector_slope(Jis, hdp.focusJ, sr.limit_J_max, slope_gain);
    const float Jic = solve_J_intersect(hdp.JMcusp[0], hdp.JMcusp[1], hdp.focusJ, sr.limit_J_max, slope_gain);
    const float boundary = find_gamut_boundary_intersection(hdp.JMcusp, sr.limit_J_max, hdp.gamma_top_inv, hdp.gamma_bottom_inv, Jis, slope, Jic);
    if (boundary <= 0.0f) return {J, 0.f, h};
    const float reach = estimate_line_and_boundary_intersection_M(Jis, slope, sr.model_gamma_inv, sr.limit_J_max, sr.reachMaxM, sr.limit_J_max);
    const float Mr = remap_M_fwd(M, boundary, reach);
    return {Jis + Mr * slope, Mr, h};
}

Transform::Transform(float peakLuminance, const Primaries& lim)
    : peak(peakLuminance),
      pIn(init_JMhParams(kAP0)),
      pOut(init_JMhParams(lim)),
      t(init_ToneScaleParams(peakLuminance)) {
    const JMhParams reach = init_JMhParams(kAP1);
    s = init_SharedCompressionParams(peakLuminance, pIn, reach);
    c = init_ChromaCompressParams(peakLuminance, t);
    g = init_GamutCompressParams(peakLuminance, pIn, pOut, t, s, reach);
}

f3 Transform::forward(const f3& rgb) const {
    const f3 Aab = RGB_to_Aab(rgb, pIn);
    const f3 JMh = Aab_to_JMh(Aab, pIn);
    const ResolvedSharedCompressionParameters rp = resolve_CompressionParams(JMh[2], s);
    const float h_rad = to_radians(JMh[2]);
    const float cos_hr = std::cos(h_rad), sin_hr = std::sin(h_rad);
    const float Mnorm = chroma_compress_norm(cos_hr, sin_hr, c.chroma_compress_scale);
    const float J_ts = tonescale_A_to_J_fwd(Aab[0], pIn, t);
    const f3 tonemapped = chroma_compress_fwd(JMh, J_ts, Mnorm, rp, c);
    const f3 compressed = gamut_compress_fwd(tonemapped, rp, g);
    return Aab_to_RGB(JMh_to_Aab(compressed, cos_hr, sin_hr, pOut), pOut);
}

}
