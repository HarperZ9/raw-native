// The ACES 2.0 parameters and tables of a pipeline as JSON, for the WGSL
// implementation in web/colour/ (it does not rebuild the tables itself).
#include "raw/renderer/colour.hpp"
#include "aces2.hpp"
#include <cmath>
#include <cstdio>
#include <string>
namespace raw::colour {
namespace {
std::string num(float v){
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", (double)v);
    return b;
}
template <class It> std::string list(It first, It last){
    std::string s = "[";
    for (It i = first; i != last; ++i) s += (i == first ? "" : ",") + num(*i);
    return s + "]";
}
std::string list(const aces2::m33f& m){ return list(m.begin(), m.end()); }
std::string jmh(const aces2::JMhParams& p){
    return "{\"rgb_to_cam\":" + list(p.MATRIX_RGB_to_CAM16_c) + ",\"cam_to_rgb\":" + list(p.MATRIX_CAM16_c_to_RGB) +
           ",\"cone_to_aab\":" + list(p.MATRIX_cone_response_to_Aab) + ",\"aab_to_cone\":" + list(p.MATRIX_Aab_to_cone_response) +
           ",\"F_L_n\":" + num(p.F_L_n) + ",\"cz\":" + num(p.cz) + ",\"inv_cz\":" + num(p.inv_cz) + ",\"A_w_J\":" + num(p.A_w_J) +
           ",\"inv_A_w_J\":" + num(p.inv_A_w_J) + "}";
}
}

std::string Transform::aces2Json() const {
    if (!aces_) return "null";
    const aces2::Transform& a = *aces_;
    const aces2::ToneScaleParams& t = a.t;
    std::vector<float> cusp;
    for (const aces2::f3& c : a.g.gamut_cusp_table) cusp.insert(cusp.end(), c.begin(), c.end());
    const aces2::m33f toAp0 = aces2::to_f33(conversion(kRec709, kAP0, true));
    const aces2::m33f toAp1 = aces2::to_f33(conversion(kAP0, kAP1, false));
    const aces2::m33f fromAp1 = aces2::to_f33(inverse(conversion(kAP0, kAP1, false)));
    const float upper = 8.f * (128.f + 768.f * (std::log(p_.peakNits / 100.f) / std::log(10000.f / 100.f)));
    std::vector<float> out;
    for (double v : outMatrix_) out.push_back((float)v);
    return std::string("{\"pipeline\":\"") + p_.name + "\",\"peak\":" + num(a.peak) +
           ",\"rec709_to_ap0\":" + list(toAp0) + ",\"ap0_to_ap1\":" + list(toAp1) + ",\"ap1_to_ap0\":" + list(fromAp1) +
           ",\"ap1_upper\":" + num(upper) + ",\"limit_to_output\":" + list(out.begin(), out.end()) +
           ",\"in\":" + jmh(a.pIn) + ",\"out\":" + jmh(a.pOut) +
           ",\"tonescale\":{\"n_r\":" + num(t.n_r) + ",\"g\":" + num(t.g) + ",\"t_1\":" + num(t.t_1) + ",\"s_2\":" + num(t.s_2) +
           ",\"m_2\":" + num(t.m_2) + "}" +
           ",\"limit_J_max\":" + num(a.s.limit_J_max) + ",\"model_gamma_inv\":" + num(a.s.model_gamma_inv) +
           ",\"reach_m\":" + list(a.s.reach_m_table.begin(), a.s.reach_m_table.end()) +
           ",\"chroma\":{\"sat\":" + num(a.c.sat) + ",\"sat_thr\":" + num(a.c.sat_thr) + ",\"compr\":" + num(a.c.compr) +
           ",\"scale\":" + num(a.c.chroma_compress_scale) + "}" +
           ",\"gamut\":{\"mid_J\":" + num(a.g.mid_J) + ",\"focus_dist\":" + num(a.g.focus_dist) +
           ",\"lower_hull_gamma_inv\":" + num(a.g.lower_hull_gamma_inv) + ",\"search\":[" +
           std::to_string(a.g.hue_linearity_search_range[0]) + "," + std::to_string(a.g.hue_linearity_search_range[1]) + "]" +
           ",\"hue\":" + list(a.g.hue_table.begin(), a.g.hue_table.end()) + ",\"cusp\":" + list(cusp.begin(), cusp.end()) + "}}";
}

}
