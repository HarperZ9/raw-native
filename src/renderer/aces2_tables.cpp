// ACES 2.0: the hue tables (reach, limiting-gamut cusp, upper hull gamma). Port of
// OpenColorIO v2.6.0 ops/fixedfunction/ACES2/Transform.cpp (BSD-3-Clause); see aces2.hpp.
#include "aces2.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace raw::colour::aces2 {
namespace {
using Corners = std::array<f3, totalCornerCount>;

f3 lerp3(const f3& lo, const f3& hi, float t){ return {lerpf(lo[0], hi[0], t), lerpf(lo[1], hi[1], t), lerpf(lo[2], hi[2], t)}; }
float midpoint(float a, float b){ return (a + b) / 2.f; }

f3 unit_cube_cusp_corner(unsigned corner){   // R, Y, G, C, B, M
    return {float(((corner + 1) % cuspCornerCount) < 3), float(((corner + 5) % cuspCornerCount) < 3),
            float(((corner + 3) % cuspCornerCount) < 3)};
}

// Rotate six corners so the lowest hue sits at [1], then close the cycle.
void close_cycle(const std::array<f3, cuspCornerCount>& t, unsigned min_index, Corners& out){
    for (unsigned i = 0; i != cuspCornerCount; ++i) out[i + 1] = t[(i + min_index) % cuspCornerCount];
    out[0] = out[cuspCornerCount];
    out[cuspCornerCount + 1] = out[1];
}

void limiting_cusp_corners(Corners& rgb, Corners& jmh, const JMhParams& params, float peak){
    std::array<f3, cuspCornerCount> tr, tj;
    unsigned min_index = 0;
    for (unsigned i = 0; i != cuspCornerCount; ++i){
        tr[i] = mult_f_f3(peak / reference_luminance, unit_cube_cusp_corner(i));
        tj[i] = RGB_to_JMh(tr[i], params);
        if (tj[i][2] < tj[min_index][2]) min_index = i;
    }
    close_cycle(tr, min_index, rgb);
    close_cycle(tj, min_index, jmh);
    jmh[0][2] = jmh[0][2] - hue_limit;
    jmh[cuspCornerCount + 1][2] = jmh[cuspCornerCount + 1][2] + hue_limit;
}

void reach_corners(Corners& jmh, const JMhParams& params, float limitJ, float maximum_source){
    std::array<f3, cuspCornerCount> tj;
    const float limitA = std::pow(limitJ * (1.0f / J_scale), params.inv_cz);
    unsigned min_index = 0;
    for (unsigned i = 0; i != cuspCornerCount; ++i){
        const f3 v = unit_cube_cusp_corner(i);
        float lower = 0.0f, upper = maximum_source;
        while ((upper - lower) > reach_cusp_tolerance){
            const float test = midpoint(lower, upper);
            const float A = RGB_to_Aab(mult_f_f3(test, v), params)[0];
            if (A < limitA) lower = test; else upper = test;
            if (A == limitA) break;
        }
        tj[i] = RGB_to_JMh(mult_f_f3(upper, v), params);
        if (tj[i][2] < tj[min_index][2]) min_index = i;
    }
    close_cycle(tj, min_index, jmh);
    jmh[0][2] = jmh[0][2] - hue_limit;
    jmh[cuspCornerCount + 1][2] = jmh[cuspCornerCount + 1][2] + hue_limit;
}

unsigned sorted_cube_hues(std::array<float, max_sorted_corners>& out, const Corners& reach, const Corners& display){
    unsigned idx = 0, ri = 1, di = 1;
    while (ri < cuspCornerCount + 1 || di < cuspCornerCount + 1){
        const float rh = reach[ri][2], dh = display[di][2];
        if (rh == dh){ out[idx] = rh; ++ri; ++di; }
        else if (rh < dh){ out[idx] = rh; ++ri; }
        else { out[idx] = dh; ++di; }
        ++idx;
    }
    return idx;
}

void sample_interval(unsigned samples, float lower, float upper, Table1D& t, unsigned base){
    const float delta = (upper - lower) / float(samples);
    for (unsigned i = 0; i != samples; ++i) t[base + i] = lower + float(i) * delta;
}

void build_hue_table(Table1D& t, const std::array<float, max_sorted_corners>& hues, unsigned unique){
    const float ideal_spacing = kNominal / hue_limit;
    std::array<unsigned, 2 * cuspCornerCount + 2> count = {};
    unsigned last_idx = std::numeric_limits<unsigned>::max();
    unsigned min_index = hues[0] == 0.0f ? 0 : 1;
    for (unsigned h = 0; h != unique; ++h){
        unsigned nominal = std::min(std::max(static_cast<unsigned>(std::round(hues[h] * ideal_spacing)), min_index), kNominal - 1);
        if (last_idx == nominal){
            if (h > 1 && count[h - 2] != (count[h - 1] - 1)) count[h - 1] = count[h - 1] - 1;
            else nominal = nominal + 1;
        }
        count[h] = std::min(nominal, kNominal - 1U);
        last_idx = min_index = nominal;
    }
    unsigned total = 0, i = 0;
    sample_interval(count[i], 0.0f, hues[i], t, total + 1);
    total += count[i];
    for (++i; i != unique; ++i){
        const unsigned n = count[i] - count[i - 1];
        sample_interval(n, hues[i - 1], hues[i], t, total + 1);
        total += n;
    }
    sample_interval(kNominal - total, hues[i - 1], hue_limit, t, total + 1);
    t[kLowerWrap] = t[kLastNominal] - hue_limit;
    t[kUpperWrap] = t[kFirstNominal] + hue_limit;
    t[kUpperWrap + 1] = t[kFirstNominal + 1] + hue_limit;
}

f2 display_cusp_for_hue(float hue, const Corners& rgb, const Corners& jmh, const JMhParams& params, f2& previous){
    unsigned upper = 1;
    for (unsigned i = upper; i != totalCornerCount; ++i) if (jmh[i][2] > hue){ upper = i; break; }
    const unsigned lower = upper - 1;
    if (jmh[lower][2] == hue) return {jmh[lower][0], jmh[lower][1]};
    float lower_t = (upper == previous[0]) ? previous[1] : 0.0f, upper_t = 1.0f, t;
    f3 JMh;
    while ((upper_t - lower_t) > display_cusp_tolerance){
        t = midpoint(lower_t, upper_t);
        JMh = RGB_to_JMh(lerp3(rgb[lower], rgb[upper], t), params);
        if (JMh[2] < jmh[lower][2]) upper_t = t;
        else if (JMh[2] >= jmh[upper][2]) lower_t = t;
        else if (JMh[2] > hue) upper_t = t;
        else lower_t = t;
    }
    t = midpoint(lower_t, upper_t);
    JMh = RGB_to_JMh(lerp3(rgb[lower], rgb[upper], t), params);
    previous = {float(upper), t};
    return {JMh[0], JMh[1]};
}

Table3D build_cusp_table(const Table1D& hues, const Corners& rgb, const Corners& jmh, const JMhParams& params){
    f2 previous = {0.0f, 0.0f};
    Table3D out{};
    for (unsigned i = kFirstNominal; i != kUpperWrap; ++i){
        const f2 JM = display_cusp_for_hue(hues[i], rgb, jmh, params, previous);
        out[i] = {JM[0], JM[1] * (1.f + smooth_m * smooth_cusps), hues[i]};
    }
    out[kLowerWrap] = {out[kLastNominal][0], out[kLastNominal][1], hues[kLowerWrap]};
    out[kUpperWrap] = {out[kFirstNominal][0], out[kFirstNominal][1], hues[kUpperWrap]};
    out[kUpperWrap + 1] = {out[kFirstNominal + 1][0], out[kFirstNominal + 1][1], hues[kUpperWrap + 1]};
    return out;
}

bool any_below_zero(const f3& v){ return v[0] < 0. || v[1] < 0. || v[2] < 0.; }

Table1D make_reach_m_table(const JMhParams& params, float limit_J_max){
    Table1D t{};
    for (unsigned i = 0; i < kNominal; i++){
        const float hue = float(i);
        constexpr float search_range = 50.f, search_maximum = 1300.f;
        float low = 0.f, high = low + search_range;
        bool outside = false;
        while (!outside && high < search_maximum){
            outside = any_below_zero(JMh_to_RGB({limit_J_max, high, hue}, params));
            if (!outside){ low = high; high = high + search_range; }
        }
        while (high - low > 1e-2){
            const float sampleM = (high + low) / 2.f;
            if (any_below_zero(JMh_to_RGB({limit_J_max, sampleM, hue}, params))) high = sampleM; else low = sampleM;
        }
        t[i + kBase] = high;
    }
    t[kLowerWrap] = t[kLastNominal];
    t[kUpperWrap] = t[kFirstNominal];
    t[kUpperWrap + 1] = t[kFirstNominal + 1];
    return t;
}

std::array<int, 2> hue_linearity_search_range(const Table3D& cusp){
    std::array<int, 2> r = {0, 1};
    for (unsigned i = kFirstNominal; i != kUpperWrap; ++i){
        const int delta = int(i) - int(kFirstNominal + hue_position_in_uniform_table(cusp[i][2]));
        r[0] = std::min(r[0], delta);
        r[1] = std::max(r[1], delta + 1);
    }
    return r;
}

// The upper hull gamma search (OCIO's make_upper_hull_gamma and its helpers).
struct GammaTest { f3 JMh; float J_intersect_source, slope, J_intersect_cusp; };

std::array<GammaTest, 5> gamma_tests(const f2& JMcusp, float hue, float limit_J_max, float mid_J, float focus_dist){
    const std::array<float, 5> pos = {0.01f, 0.1f, 0.5f, 0.8f, 0.99f};
    const float threshold = lerpf(JMcusp[0], limit_J_max, focus_gain_blend);
    const float focusJ = compute_focusJ(JMcusp[0], mid_J, limit_J_max);
    std::array<GammaTest, 5> out;
    for (unsigned k = 0; k < 5; ++k){
        const float J = lerpf(JMcusp[0], limit_J_max, pos[k]);
        const float gain = get_focus_gain(J, threshold, limit_J_max, focus_dist);
        const float Jis = solve_J_intersect(J, JMcusp[1], focusJ, limit_J_max, gain);
        out[k] = {{J, JMcusp[1], hue}, Jis, compute_compression_vector_slope(Jis, focusJ, limit_J_max, gain),
                  solve_J_intersect(JMcusp[0], JMcusp[1], focusJ, limit_J_max, gain)};
    }
    return out;
}

bool gamma_fits(const f2& JMcusp, const std::array<GammaTest, 5>& data, float top_inv, float peak, float limit_J_max,
                float lower_inv, const JMhParams& limit){
    const float lum_limit = peak / reference_luminance;
    for (const GammaTest& d : data){
        const float M = find_gamut_boundary_intersection(JMcusp, limit_J_max, top_inv, lower_inv, d.J_intersect_source, d.slope, d.J_intersect_cusp);
        const f3 rgb = JMh_to_RGB({d.J_intersect_source + d.slope * M, M, d.JMh[2]}, limit);
        if (!(rgb[0] > lum_limit || rgb[1] > lum_limit || rgb[2] > lum_limit)) return false;
    }
    return true;
}

void make_upper_hull_gamma(const Table1D& hues, Table3D& cusp, float peak, float limit_J_max, float mid_J, float focus_dist,
                           float lower_inv, const JMhParams& limit){
    for (unsigned i = kFirstNominal; i != kUpperWrap; ++i){
        const f2 JMcusp = {cusp[i][0], cusp[i][1]};
        const auto data = gamma_tests(JMcusp, hues[i], limit_J_max, mid_J, focus_dist);
        auto fits = [&](float gamma){ return gamma_fits(JMcusp, data, 1.0f / gamma, peak, limit_J_max, lower_inv, limit); };
        float low = gammaMinimum, high = low + gammaSearchStep;
        bool outside = false;
        while (!outside && high < gammaMaximum){
            if (fits(high)) outside = true; else { low = high; high = high + gammaSearchStep; }
        }
        while ((high - low) > gammaAccuracy){
            const float g = midpoint(high, low);
            if (fits(g)) high = g; else low = g;
        }
        cusp[i][2] = 1.0f / high;
    }
    cusp[kLowerWrap][2] = cusp[kLastNominal][2];
    cusp[kUpperWrap][2] = cusp[kFirstNominal][2];
    cusp[kUpperWrap + 1][2] = cusp[kFirstNominal + 1][2];
}
}

SharedCompressionParameters init_SharedCompressionParams(float peak, const JMhParams& in, const JMhParams& reach){
    const float limit_J_max = Y_to_J(peak, in);
    return {limit_J_max, 1.f / model_gamma(), make_reach_m_table(reach, limit_J_max)};
}

ResolvedSharedCompressionParameters resolve_CompressionParams(float hue, const SharedCompressionParameters& p){
    const unsigned base = hue_position_in_uniform_table(hue);
    const float t = hue - base;
    const unsigned i_lo = base + kFirstNominal;
    return {p.limit_J_max, p.model_gamma_inv, lerpf(p.reach_m_table[i_lo], p.reach_m_table[i_lo + 1], t)};
}

GamutCompressParams init_GamutCompressParams(float peak, const JMhParams& in, const JMhParams& limit,
                                             const ToneScaleParams& ts, const SharedCompressionParameters& sh, const JMhParams& reach){
    GamutCompressParams p;
    p.mid_J = Y_to_J(ts.c_t * reference_luminance, in);
    p.focus_dist = focus_distance + focus_distance * focus_distance_scaling * ts.log_peak;
    p.lower_hull_gamma_inv = 1.0f / (1.14f + 0.07f * ts.log_peak);
    Corners reachJMh, limRGB, limJMh;
    std::array<float, max_sorted_corners> hues;
    reach_corners(reachJMh, reach, sh.limit_J_max, ts.forward_limit);
    limiting_cusp_corners(limRGB, limJMh, limit, peak);
    build_hue_table(p.hue_table, hues, sorted_cube_hues(hues, reachJMh, limJMh));
    p.gamut_cusp_table = build_cusp_table(p.hue_table, limRGB, limJMh, limit);
    p.hue_linearity_search_range = hue_linearity_search_range(p.gamut_cusp_table);
    make_upper_hull_gamma(p.hue_table, p.gamut_cusp_table, peak, sh.limit_J_max, p.mid_J, p.focus_dist, p.lower_hull_gamma_inv, limit);
    return p;
}

}
