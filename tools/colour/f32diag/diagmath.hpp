// Forced include for the float32 diagnosis (M1 criterion 3). Swaps the transcendental
// functions the colour code calls, only while diag::g_mode is set, so table setup stays exact.
//   mode 0: exact (the reference as built)
//   mode 1: pow lowered as exp2(y * log2(x)) in float32, as GPU compilers lower it
//   mode 2: every transcendental result carries a seeded relative error of up to K ulp
//   mode 3: the WGSL precision limits for sin and cos (2^-11 absolute) and atan2 (4096 ulp)
//   mode 4: mode 3 with atan2 exact; mode 5: atan2 at its limit alone
#pragma once
#include <cmath>
#include <cstdint>
#include <type_traits>
namespace diag {
inline int g_mode = 0;
inline float g_ulps = 4.0f;
inline uint64_t g_state = 1;
inline double noise(){
    g_state = g_state * 6364136223846793005ull + 1442695040888963407ull;
    return ((double)(g_state >> 11) / 9007199254740992.0) * 2.0 - 1.0;
}
template <class T> inline T perturb(T v){
    if ((g_mode < 2 || g_mode > 5) || !std::isfinite(v)) return v;
    return (T)(v * (1.0 + g_ulps * 1.1920928955078125e-07 * 0.5 * noise()));
}
template <class A, class B> inline auto pow_(A a, B b){
    using R = decltype(::std::pow(a, b));
    if (g_mode == 1 && std::is_same_v<R, float>){
        if (a == 0) return (R)0;
        if (a < 0) return (R)::std::pow(a, b);
        return (R)::exp2f((float)b * ::log2f((float)a));
    }
    return perturb((R)::std::pow(a, b));
}
}
namespace std {
template <class A, class B> inline auto diag_pow(A a, B b){ return ::diag::pow_(a, b); }
template <class A> inline auto diag_sqrt(A a){ return ::diag::perturb(::std::sqrt(a)); }
// Mode 3: the WGSL precision limits for the hue functions. sin and cos: absolute error
// up to 2^-11 on [-pi, pi]; atan2: up to 4096 ulp. Other functions as mode 2.
template <class A> inline auto spec_abs(A v){
    return (::diag::g_mode == 3 || ::diag::g_mode == 4) ? (A)(v + 0.00048828125 * ::diag::noise()) : ::diag::perturb(v);
}
template <class A> inline auto diag_sin(A a){ return spec_abs(::std::sin(a)); }
template <class A> inline auto diag_cos(A a){ return spec_abs(::std::cos(a)); }
template <class A> inline auto diag_log(A a){ return ::diag::perturb(::std::log(a)); }
template <class A, class B> inline auto diag_atan2(A a, B b){
    auto v = ::std::atan2(a, b);
    if (::diag::g_mode == 4) return v;
    if (::diag::g_mode == 5) return (decltype(v))(v * (1.0 + 4096 * 1.1920928955078125e-07 * 0.5 * ::diag::noise()));
    if (::diag::g_mode == 3) return (decltype(v))(v * (1.0 + 4096 * 1.1920928955078125e-07 * 0.5 * ::diag::noise()));
    return ::diag::perturb(v);
}
}
#define pow diag_pow
#define sqrt diag_sqrt
#define sin diag_sin
#define cos diag_cos
#define log diag_log
#define atan2 diag_atan2
