// ACES 2.0: CAM, tone scale and chroma compression. Port of OpenColorIO v2.6.0
// ops/fixedfunction/ACES2/Transform.cpp (BSD-3-Clause); see aces2.hpp.
#include "aces2.hpp"
#include <algorithm>
#include <cmath>
namespace raw::colour::aces2 {
namespace {
const Primaries kCAM16 = {{0.8336, 0.1735}, {2.3854, -1.4659}, {0.087, -0.125}, {0.333, 0.333}};
const m33f kIdentity = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};

f3 f3_from_f(float v){ return {v, v, v}; }
m33f mult_f33_f33(const m33f& a, const m33f& b){
    return {a[0]*b[0] + a[1]*b[3] + a[2]*b[6], a[0]*b[1] + a[1]*b[4] + a[2]*b[7], a[0]*b[2] + a[1]*b[5] + a[2]*b[8],
            a[3]*b[0] + a[4]*b[3] + a[5]*b[6], a[3]*b[1] + a[4]*b[4] + a[5]*b[7], a[3]*b[2] + a[4]*b[5] + a[5]*b[8],
            a[6]*b[0] + a[7]*b[3] + a[8]*b[6], a[6]*b[1] + a[7]*b[4] + a[8]*b[7], a[6]*b[2] + a[7]*b[5] + a[8]*b[8]};
}
// OCIO's scale_f33 (it scales the diagonal and transposes the rest, which for
// the identity it is always given is the diagonal matrix of the scale).
m33f scale_f33(const m33f& m, const f3& s){
    return {m[0]*s[0], m[3], m[6], m[1], m[4]*s[1], m[7], m[2], m[5], m[8]*s[2]};
}
m33f invert_f33(const m33f& m){
    Mat3 d; for (int i = 0; i < 9; ++i) d[i] = m[i];
    return to_f33(inverse(d));
}
m33f RGBtoRGB_f33(const Primaries& src, const Primaries& dst){ return mult_f33_f33(XYZtoRGB_f33(dst), RGBtoXYZ_f33(src)); }

inline float cone_fwd(float Rc){ const float F_L_Y = std::pow(Rc, 0.42f); return F_L_Y / (cam_nl_offset + F_L_Y); }
inline float cone_inv(float Ra){
    const float Ra_lim = std::min(Ra, 0.99f);
    const float F_L_Y = (cam_nl_offset * Ra_lim) / (1.0f - Ra_lim);
    return std::pow(F_L_Y, 1.f / 0.42f);
}
float cone_inv_signed(float v){ return std::copysign(cone_inv(std::abs(v)), v); }
inline float Achromatic_n_to_J(float A, float cz){ return J_scale * std::pow(A, cz); }
inline float J_to_Achromatic_n(float J, float inv_cz){ return std::pow(J * (1.0f / J_scale), inv_cz); }
inline float A_to_Y(float A, const JMhParams& p){ return cone_inv(p.A_w_J * A) / p.F_L_n; }
inline float Y_to_J_abs(float abs_Y, const JMhParams& p){
    const float Ra = cone_fwd(abs_Y * p.F_L_n);
    return Achromatic_n_to_J(Ra * p.inv_A_w_J, p.cz);
}
inline float toe_fwd(float x, float limit, float k1_in, float k2_in){
    if (x > limit) return x;
    const float k2 = std::max(k2_in, 0.001f);
    const float k1 = std::sqrt(k1_in * k1_in + k2 * k2);
    const float k3 = (limit + k1) / (limit + k2);
    const float minus_b = k3 * x - k1;
    const float minus_ac = k2 * k3 * x;
    return 0.5f * (minus_b + std::sqrt(minus_b * minus_b + 4.f * minus_ac));
}
inline float aces_tonescale_fwd(float Y_in, const ToneScaleParams& pt){
    const float f = pt.m_2 * std::pow(Y_in / (Y_in + pt.s_2), pt.g);
    return std::max(0.f, f * f / (f + pt.t_1)) * pt.n_r;
}
}

f3 mult_f_f3(float v, const f3& a){ return {v * a[0], v * a[1], v * a[2]}; }
f3 mult_f3_f33(const f3& a, const m33f& m){
    return {a[0]*m[0] + a[1]*m[1] + a[2]*m[2], a[0]*m[3] + a[1]*m[4] + a[2]*m[5], a[0]*m[6] + a[1]*m[7] + a[2]*m[8]};
}
m33f to_f33(const Mat3& m){ m33f r; for (int i = 0; i < 9; ++i) r[i] = (float)m[i]; return r; }
m33f RGBtoXYZ_f33(const Primaries& p){ return to_f33(rgbToXyz(p)); }
m33f XYZtoRGB_f33(const Primaries& p){ return to_f33(inverse(rgbToXyz(p))); }

float post_adaptation_cone_response_compression_fwd(float v){ return std::copysign(cone_fwd(std::abs(v)), v); }
float Y_to_J(float Y, const JMhParams& p){ return std::copysign(Y_to_J_abs(std::abs(Y), p), Y); }

f3 RGB_to_Aab(const f3& RGB, const JMhParams& p){
    const f3 m = mult_f3_f33(RGB, p.MATRIX_RGB_to_CAM16_c);
    const f3 a = {post_adaptation_cone_response_compression_fwd(m[0]), post_adaptation_cone_response_compression_fwd(m[1]),
                  post_adaptation_cone_response_compression_fwd(m[2])};
    return mult_f3_f33(a, p.MATRIX_cone_response_to_Aab);
}
f3 Aab_to_JMh(const f3& Aab, const JMhParams& p){
    if (Aab[0] <= 0.f) return {0.f, 0.f, 0.f};
    const float J = Achromatic_n_to_J(Aab[0], p.cz);
    const float M = std::sqrt(Aab[1] * Aab[1] + Aab[2] * Aab[2]);
    return {J, M, from_radians_unwrapped(hue_atan2(Aab[2], Aab[1]))};
}
f3 RGB_to_JMh(const f3& RGB, const JMhParams& p){ return Aab_to_JMh(RGB_to_Aab(RGB, p), p); }
f3 JMh_to_Aab(const f3& JMh, float cos_hr, float sin_hr, const JMhParams& p){
    return {J_to_Achromatic_n(JMh[0], p.inv_cz), JMh[1] * cos_hr, JMh[1] * sin_hr};
}
f3 Aab_to_RGB(const f3& Aab, const JMhParams& p){
    const f3 a = mult_f3_f33(Aab, p.MATRIX_Aab_to_cone_response);
    const f3 m = {cone_inv_signed(a[0]), cone_inv_signed(a[1]), cone_inv_signed(a[2])};
    return mult_f3_f33(m, p.MATRIX_CAM16_c_to_RGB);
}
f3 JMh_to_RGB(const f3& JMh, const JMhParams& p){
    const float h_rad = to_radians(JMh[2]);
    return Aab_to_RGB(JMh_to_Aab(JMh, std::cos(h_rad), std::sin(h_rad), p), p);
}

float chroma_compress_norm(float c1, float s1, float scale){
    const float c2 = 2.0f * c1 * c1 - 1.0f;
    const float s2 = 2.0f * c1 * s1;
    const float c3 = 4.0f * c1 * c1 * c1 - 3.0f * c1;
    const float s3 = 3.0f * s1 - 4.0f * s1 * s1 * s1;
    const float M = 11.34072f * c1 + 16.46899f * c2 + 7.88380f * c3 + 14.66441f * s1 + -6.37224f * s2 + 9.19364f * s3 + 77.12896f;
    return M * scale;
}
float tonescale_A_to_J_fwd(float A, const JMhParams& p, const ToneScaleParams& pt){
    const float Y_out = aces_tonescale_fwd(A_to_Y(A, p), pt);
    return std::copysign(Y_to_J_abs(Y_out, p), A);
}
f3 chroma_compress_fwd(const f3& JMh, float J_ts, float Mnorm, const ResolvedSharedCompressionParameters& pr, const ChromaCompressParams& pc){
    const float J = JMh[0], M = JMh[1];
    float M_cp = M;
    if (M != 0.0f){
        const float nJ = J_ts / pr.limit_J_max;
        const float snJ = std::max(0.f, 1.f - nJ);
        const float limit = std::pow(nJ, pr.model_gamma_inv) * pr.reachMaxM / Mnorm;
        M_cp = M * std::pow(J_ts / J, pr.model_gamma_inv);
        M_cp = M_cp / Mnorm;
        M_cp = limit - toe_fwd(limit - M_cp, limit - 0.001f, snJ * pc.sat, std::sqrt(nJ * nJ + pc.sat_thr));
        M_cp = toe_fwd(M_cp, limit, nJ * pc.compr, snJ);
        M_cp = M_cp * Mnorm;
    }
    return {J_ts, M_cp, JMh[2]};
}
float model_gamma(){ return surround[1] * (1.48f + std::sqrt(Y_b / reference_luminance)); }

JMhParams init_JMhParams(const Primaries& prims){
    const m33f base = {2.0f, 1.0f, 1.0f / 20.0f, 1.0f, -12.0f / 11.0f, 1.0f / 11.0f, 1.0f / 9.0f, 1.0f / 9.0f, -2.0f / 9.0f};
    const m33f MATRIX_16 = XYZtoRGB_f33(kCAM16);
    const f3 XYZ_w = mult_f3_f33(f3_from_f(reference_luminance), RGBtoXYZ_f33(prims));
    const float Y_W = XYZ_w[1];
    const f3 RGB_w = mult_f3_f33(XYZ_w, MATRIX_16);
    constexpr float K = 1.f / (5.f * L_A + 1.f);
    constexpr float K4 = K * K * K * K;
    const float F_L = 0.2f * K4 * (5.f * L_A) + 0.1f * std::pow((1.f - K4), 2.f) * std::pow(5.f * L_A, 1.f / 3.f);
    const float F_L_n = F_L / reference_luminance;
    const float cz = model_gamma();
    const f3 D_RGB = {F_L_n * Y_W / RGB_w[0], F_L_n * Y_W / RGB_w[1], F_L_n * Y_W / RGB_w[2]};
    const f3 RGB_WC = {D_RGB[0] * RGB_w[0], D_RGB[1] * RGB_w[1], D_RGB[2] * RGB_w[2]};
    const f3 RGB_AW = {post_adaptation_cone_response_compression_fwd(RGB_WC[0]), post_adaptation_cone_response_compression_fwd(RGB_WC[1]),
                       post_adaptation_cone_response_compression_fwd(RGB_WC[2])};
    const m33f c2A = mult_f33_f33(scale_f33(kIdentity, f3_from_f(cam_nl_scale)), base);
    const float A_w = c2A[0] * RGB_AW[0] + c2A[1] * RGB_AW[1] + c2A[2] * RGB_AW[2];
    const float A_w_J = cone_fwd(F_L);
    const m33f toCam = mult_f33_f33(RGBtoRGB_f33(prims, kCAM16), scale_f33(kIdentity, f3_from_f(reference_luminance)));
    const m33f toCam_c = mult_f33_f33(scale_f33(kIdentity, D_RGB), toCam);
    const float s = 43.f * surround[2];
    const m33f cone2Aab = {c2A[0] / A_w, c2A[1] / A_w, c2A[2] / A_w, c2A[3] * s, c2A[4] * s, c2A[5] * s, c2A[6] * s, c2A[7] * s, c2A[8] * s};
    return {toCam_c, invert_f33(toCam_c), cone2Aab, invert_f33(cone2Aab), F_L_n, cz, 1.0f / cz, A_w_J, 1.0f / A_w_J};
}

ToneScaleParams init_ToneScaleParams(float n){
    const float n_r = 100.0f, g = 1.15f, c = 0.18f, c_d = 10.013f, w_g = 0.14f, t_1 = 0.04f;
    const float r_hit_min = 128.f, r_hit_max = 896.f;
    const float r_hit = r_hit_min + (r_hit_max - r_hit_min) * (std::log(n / n_r) / std::log(10000.f / 100.f));
    const float m_0 = n / n_r;
    const float m_1 = 0.5f * (m_0 + std::sqrt(m_0 * (m_0 + 4.f * t_1)));
    const float u = std::pow((r_hit / m_1) / ((r_hit / m_1) + 1.f), g);
    const float m = m_1 / u;
    const float w_i = std::log(n / 100.f) / std::log(2.f);
    const float c_t = c_d / n_r * (1.f + w_i * w_g);
    const float g_ip = 0.5f * (c_t + std::sqrt(c_t * (c_t + 4.f * t_1)));
    const float g_ipp2 = -(m_1 * std::pow((g_ip / m), (1.f / g))) / (std::pow(g_ip / m, 1.f / g) - 1.f);
    const float w_2 = c / g_ipp2;
    const float s_2 = w_2 * m_1 * reference_luminance;
    const float u_2 = std::pow((r_hit / m_1) / ((r_hit / m_1) + w_2), g);
    const float m_2 = m_1 / u_2;
    return {n, n_r, g, t_1, c_t, s_2, u_2, m_2, 8.0f * r_hit, n / (u_2 * n_r), std::log10(n / n_r)};
}

ChromaCompressParams init_ChromaCompressParams(float peak, const ToneScaleParams& ts){
    const float compr = chroma_compress + (chroma_compress * chroma_compress_fact) * ts.log_peak;
    const float sat = std::max(0.2f, chroma_expand - (chroma_expand * chroma_expand_fact) * ts.log_peak);
    return {sat, chroma_expand_thr / ts.n, compr, std::pow(0.03379f * peak, 0.30596f) - 0.45135f};
}

}
