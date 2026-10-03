#include "raw/run.hpp"
#include "raw/composite.hpp"
#include "raw/image.hpp"
#include "raw/sha256.hpp"
#include "raw/version.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
namespace raw {
FrameResult renderFromParams(const CliParams& p, Arena* arena){
    Scene s = buildTestScene(p.width, p.height, arena);
    s.camera = cameraFromParams(p);
    Mat4 curVP  = mul(s.camera.proj(), s.camera.view());
    Mat4 prevVP = curVP;                       // static frame -> honest zero motion
    if (p.hasPrevCamera()){
        Camera pc = prevCameraFromParams(p);
        prevVP = mul(pc.proj(), pc.view());
    }
    RenderOptions opts;
    opts.tolerance = p.tolerance;
    opts.rtao = p.rtao;
    opts.threads = p.threads;
    return renderWithParams(s, p.width, p.height, prevVP, arena, opts);
}
FileDigests writeFrameFiles(const FrameResult& o, const CliParams& p){
    const std::string out = p.out + "/";
    std::vector<std::string> names;
    auto put = [&](const std::string& name){ names.push_back(name); return out + name; };
    writePPM(o.frame, put("frame.ppm"));             // human view: clamped 8-bit
    writePFM(o.hdr,   put("frame_hdr.pfm"));          // model view: exact linear radiance
    writePGM(o.aoSS,  put("ao_ss.pgm"));              // 8-bit previews
    writePFM1(o.aoSS, put("ao_ss.pfm"));              // full precision for re-derivation
    writeMaskPGM(o.g.mask, put("mask.pgm"));          // which pixels the reconcile counts
    if (p.rtao){
        writePGM(o.aoRT,  put("ao_rt.pgm"));
        writePFM1(o.aoRT, put("ao_rt.pfm"));
        writePGM(o.rec.errorMap, put("ao_error.pgm"));
    }
    std::sort(names.begin(), names.end());
    FileDigests d;
    for (const auto& n : names) d.emplace_back(n, sha256File(out + n));
    return d;
}
Certificate aoCertificate(const FrameResult& o, const CliParams& p, const FileDigests& outputs){
    std::optional<double> motionCoherence =
        o.motionTotal > 0 ? std::optional<double>((double)o.motionValid / o.motionTotal)
                          : std::nullopt;
    std::optional<double> hdrHeadroom = std::optional<double>(maxRadiance(o.hdr));
    Certificate c = certificate_with_channels(o.rec, p.tolerance, motionCoherence, hdrHeadroom);
    Provenance pv;
    pv.renderer   = version();
    pv.paramsJson = canonicalParamsJson(p);
    pv.rtSamples  = p.rtao ? kRtSamples : 0;
    pv.ssSamples  = kSsSamples;
    pv.pixels     = o.rec.pixels;
    pv.rmse       = o.rec.rmse;
    pv.maxError   = o.rec.maxError;
    pv.tolerance  = p.tolerance;
    pv.outputs    = outputs;
    c.provenance  = pv;
    return c;
}
std::string benchJson(const CliParams& p){
    std::vector<double> ms;
    for (int i = 0; i < p.bench; ++i){
        auto t0 = std::chrono::steady_clock::now();
        FrameResult o = renderFromParams(p, nullptr);
        auto t1 = std::chrono::steady_clock::now();
        if (o.g.w != p.width) return "{\"error\":\"render size mismatch\"}";
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::vector<double> sorted = ms;
    std::sort(sorted.begin(), sorted.end());
    size_t n = sorted.size();
    double median = (n % 2) ? sorted[n/2] : 0.5 * (sorted[n/2 - 1] + sorted[n/2]);
    char b[64];
    std::string o = "{\"renderer\":\"" + std::string(version()) + "\"";
    o += ",\"width\":" + std::to_string(p.width) + ",\"height\":" + std::to_string(p.height);
    o += std::string(",\"rt\":") + (p.rtao ? "true" : "false");
    o += ",\"threads\":" + std::to_string(p.threads) + ",\"runs_ms\":[";
    for (size_t i = 0; i < n; ++i){
        std::snprintf(b, sizeof b, "%s%.3f", i ? "," : "", ms[i]); o += b; }
    std::snprintf(b, sizeof b, "],\"median_ms\":%.3f}", median);
    return o + b;
}
}
