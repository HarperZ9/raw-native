// The fractal dither's GPU link: see raw/renderer/fractal_gpu.hpp.
#include "raw/renderer/fractal_gpu.hpp"
#include "raw/renderer/fractal_scene.hpp"
#include "style_gpu.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace raw::gpu_check {
namespace {
using style::Anchor;

FractalCase parity(rhi::Device& dev, const char* preset, int frame, std::string& err) {
    FractalCase c;
    c.id = std::string(preset) + "-frame" + std::to_string(frame);
    const fractal::Frame fr = fractal::renderGBuffer(fractal::orbitCamera(frame), 256, 144);
    const style::DitherPlan plan = fractal::orbitPlan(preset, Anchor::Fractal, 144, 2.0);
    const std::vector<style::DitherLight> L = fractal::lights();
    const style::DitherFrame f = style::runDitherlight(plan, L, fr.g);
    std::vector<std::uint32_t> lv, packed;
    if (!style_gpu::runDither(dev, plan, L, fr.g, lv, packed, err)) return c;
    const style::DitherFrame shifted = style::runDitherlight(plan, L, fr.g, 1);
    long over2 = 0, shiftedDiff = 0;
    double sum = 0;
    c.decisions = long(f.level.size());
    for (std::size_t i = 0; i < f.level.size(); ++i) {
        c.mismatch += lv[i] != f.level[i];
        shiftedDiff += lv[i] != shifted.level[i];
        int worst = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            const int d = std::abs(int((packed[i] >> (8 * k)) & 255u) - int(f.rgba[i * 4 + k]));
            worst = std::max(worst, d);
            sum += d;
        }
        over2 += worst > 2;
    }
    c.mismatchFraction = double(c.mismatch) / double(c.decisions);
    c.over2Fraction = double(over2) / double(c.decisions);
    c.meanCode = sum / (3.0 * double(c.decisions));
    c.controlFails = double(shiftedDiff) / double(c.decisions) > kFractalMismatch;
    c.pass = c.mismatchFraction <= kFractalMismatch && c.over2Fraction <= kFractalOver2 && c.meanCode <= kFractalMeanCode && c.controlFails;
    return c;
}

// The pass's GPU time at 1080p for an anchor, interleaved by the caller.
double timeAnchor(rhi::Device& dev, const fractal::Frame& fr, Anchor a, std::string& err) {
    const style::DitherPlan plan = fractal::orbitPlan("ink", a, fr.h, 2.0);
    std::vector<std::uint32_t> lv, packed;
    style_gpu::DitherTiming t;
    if (!style_gpu::runDither(dev, plan, fractal::lights(), fr.g, lv, packed, err, &t)) return -1;
    if (t.ms < 0) err = t.error;
    return t.ms;
}
}  // namespace

bool FractalGpu::parityPass() const {
    if (!error.empty() || cases.empty()) return false;
    for (const FractalCase& c : cases) if (!c.pass) return false;
    return true;
}

FractalGpu fractalGpu(rhi::Device& dev, bool cost) {
    FractalGpu r;
    r.backend = dev.backendName(); r.adapter = dev.adapter().description;
    for (const char* preset : {"ink", "tones4"})
        for (int frame : {0, 24}) {
            r.cases.push_back(parity(dev, preset, frame, r.error));
            if (!r.error.empty()) return r;
        }
    if (!cost) return r;
    r.timed = true;
    const fractal::Frame fr = fractal::renderGBuffer(fractal::orbitCamera(0), 1920, 1080);
    std::vector<double> f, s;
    for (int k = 0; k < 3; ++k) {   // interleaved rounds; the median of the rounds' medians
        f.push_back(timeAnchor(dev, fr, Anchor::Fractal, r.timeError));
        s.push_back(timeAnchor(dev, fr, Anchor::Screen, r.timeError));
    }
    std::sort(f.begin(), f.end()); std::sort(s.begin(), s.end());
    r.msFractal = f[1]; r.msScreen = s[1]; r.msExtra = r.msFractal - r.msScreen;
    return r;
}

std::string FractalGpu::json() const {
    char b[512];
    std::string j = "{\n \"check\": \"fractal dither gpu (f5, f6)\", \"backend\": \"" + backend + "\", \"adapter\": \"" + adapter + "\",\n \"error\": \"" + error + "\",\n \"bound\": {\"mismatch_fraction\": 0.0005, \"over2_fraction\": 0.0005, \"mean_code\": 0.12},\n \"cases\": [";
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const FractalCase& c = cases[i];
        std::snprintf(b, sizeof b, "%s\n  {\"id\": \"%s\", \"decisions\": %ld, \"level_mismatch\": %ld, \"mismatch_fraction\": %.6g, \"over2_fraction\": %.6g, \"mean_code\": %.5f, \"control_fails\": %s, \"pass\": %s}",
                      i ? "," : "", c.id.c_str(), c.decisions, c.mismatch, c.mismatchFraction, c.over2Fraction, c.meanCode, c.controlFails ? "true" : "false", c.pass ? "true" : "false");
        j += b;
    }
    j += "\n ],\n \"parity_pass\": " + std::string(parityPass() ? "true" : "false");
    if (timed) {
        std::snprintf(b, sizeof b, ",\n \"cost_1080p\": {\"fractal_ms\": %.4f, \"screen_ms\": %.4f, \"extra_ms\": %.4f, \"bound_extra_ms\": 0.3, \"timing_error\": \"%s\"}", msFractal, msScreen, msExtra, timeError.c_str());
        j += b;
    }
    j += std::string(",\n \"pass\": ") + (pass() ? "true" : "false") + "\n}\n";
    return j;
}

}  // namespace raw::gpu_check
