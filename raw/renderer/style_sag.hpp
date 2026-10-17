#pragma once
// Scan-causal EHT sag and the salience caricature warp, the C++ core's references (ADR 0019) of
// web/shaders/sag/sag.mjs and web/shaders/caricature/caricature.mjs, whose headers carry the
// models. Sag: per-line beam current, an anode-voltage recurrence over the lines (twice with
// settle, the second pass starting from the first's end decayed through blanking), then each
// output line resampled from the two drawn lines around it with a gaussian spot. Caricature: a
// smoothed salience field whose gradient displaces the read position, lambda clamped so the map
// cannot fold. The JavaScript computes in float64 and stores each frame as f32; so does this. The
// GPU passes are style_sag_* and style_cari_* in style.wgsl (the line current and the lambda
// reduction use workgroup memory).
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace raw::style {

struct SagParams { double S{0.02}, tauFrame{0.1}, focus{4}, sigma0{0.45}, bright{1}, gamma{2.4}, blankFrac{0.08}; bool settle{true}; double initial{0}; };
SagParams sagPreset(const std::string& name);   // consumer, thriller
struct SagPlan { SagParams p; int w{0}, h{0}, blank{0}, R{0}; double tauLines{0}, alpha{0}; };
SagPlan resolveSag(const SagParams& p, int w, int h);
struct SagScan { std::vector<double> I, v, k, Y, sig; };
// src: R'G'B'A f32 rows. Returns the resampled R'G'B'A signal (f32 rows); scan receives the lines.
std::vector<float> runSag(const SagPlan& plan, const std::vector<float>& src, SagScan& scan);
std::uint8_t sagSignal8(double v);   // signal8: clamp, round half up

struct CariParams { std::array<double, 10> weights{}; double sigma{0.035}, lambda{1e9}, gain{1}; int steps{4}; };
CariParams cariPreset(const std::string& name);   // grotesque, subtle
struct CariPlan { CariParams p; int w{0}, h{0}, R{0}; std::vector<double> taps; };
CariPlan resolveCaricature(const CariParams& p, int w, int h);
struct CariFields { std::vector<double> S, g; double lambda{0}; };
// src: scene-linear RGBA f32 rows; mat: material id per pixel (-1 for none). Returns the warped
// frame (RGBA f32 rows); fields receives the smoothed salience, the gradient and lambda.
std::vector<float> runCaricature(const CariPlan& plan, const std::vector<float>& src, const std::vector<std::int32_t>& mat, CariFields& fields);

}  // namespace raw::style
