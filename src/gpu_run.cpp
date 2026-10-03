#include "raw/gpu_run.hpp"
#include "raw/gpu.hpp"
#include "raw/run.hpp"
#include "raw/channels_json.hpp"
#include "raw/version.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
namespace raw {
namespace {
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0){ return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
// An error as a one-line JSON object; WebGPU messages carry quotes and newlines.
std::string errorJson(const std::string& e){
    std::string o = "{\"error\":\"";
    for (char c : e){ if (c == '"' || c == '\\') o += '\\'; o += (c == '\n' || c == '\r') ? ' ' : c; }
    return o + "\"}";
}
struct GpuJob { Scene scene; Mat4 prevVP; RenderOptions opts; };
GpuJob jobFor(const CliParams& p){
    GpuJob j{buildTestScene(p.width, p.height), {}, {}};
    j.scene.camera = cameraFromParams(p);
    j.prevVP = mul(j.scene.camera.proj(), j.scene.camera.view());
    if (p.hasPrevCamera()){ Camera pc = prevCameraFromParams(p); j.prevVP = mul(pc.proj(), pc.view()); }
    j.opts.tolerance = p.tolerance; j.opts.rtao = p.rtao; j.opts.threads = p.threads;
    return j;
}
bool writeAll(const FrameResult& o, const CliParams& p){
    FileDigests outputs = writeFrameFiles(o, p);
    for (const auto& kv : outputs) if (kv.second.empty()) return false;
    std::ofstream(p.out + "/certificate.json") << to_json(aoCertificate(o, p, outputs));
    std::ofstream(p.out + "/channels.json") << channelsJson(o, p);
    return true;
}
int noGpu(const CliParams& p, const GpuAdapterInfo& info, const std::string& why){
    GpuReconcile r; r.rt = p.rtao; r.reason = why;
    std::string cert = gpuCertificateJson(r, info, version(), canonicalParamsJson(p), 0, 0);
    std::ofstream(p.out + "/gpu_certificate.json") << cert;
    std::printf("gpu: %s\ngpu-certificate: %s\n", why.c_str(), cert.c_str());
    return 4;
}
}
int runGpu(const CliParams& p){
    CliParams pc = p; pc.gpu = false; pc.out = p.out + "/cpu";
    std::error_code ec;
    std::filesystem::create_directories(pc.out, ec);
    if (ec || !std::filesystem::is_directory(pc.out)){ std::printf("cannot create output directory: %s\n", pc.out.c_str()); return 2; }
    GpuAdapterInfo info; std::string err;
    if (!gpuInit(info, err)) return noGpu(p, info, err);
    GpuJob job = jobFor(p);
    FrameResult g;
    auto t0 = Clock::now();
    if (!renderGpu(job.scene, p.width, p.height, job.prevVP, job.opts, g, err)) return noGpu(p, info, err);
    const double gpuMs = msSince(t0);
    t0 = Clock::now();
    const FrameResult c = renderFromParams(pc, nullptr);
    const double cpuMs = msSince(t0);
    if (!writeAll(g, p) || !writeAll(c, pc)){ std::printf("could not write the output files\n"); return 2; }
    GpuReconcile r = reconcileGpuCpu(g, c, p.rtao);
    std::string cert = gpuCertificateJson(r, info, version(), canonicalParamsJson(p), gpuMs, cpuMs);
    std::ofstream(p.out + "/gpu_certificate.json") << cert;
    std::printf("gpu: %s %s (%s), %.1f ms; cpu reference %.1f ms\n", info.vendor.c_str(), info.architecture.c_str(),
                info.backend.c_str(), gpuMs, cpuMs);
    std::printf("gpu-certificate: %s\n", cert.c_str());
    return 0;
}
namespace {
// Time p.bench GPU renders; frameOnly reads back only the frame.
bool timeRuns(const CliParams& p, const GpuJob& job, bool frameOnly, std::vector<double>& ms, std::string& err){
    FrameResult o;
    for (int i = 0; i < p.bench; ++i){
        auto t0 = Clock::now();
        if (!renderGpu(job.scene, p.width, p.height, job.prevVP, job.opts, o, err, frameOnly)) return false;
        ms.push_back(msSince(t0));
    }
    return true;
}
std::string runsJson(const char* key, const std::vector<double>& ms){
    std::vector<double> s = ms; std::sort(s.begin(), s.end());
    const size_t n = s.size();
    const double median = (n % 2) ? s[n/2] : 0.5 * (s[n/2 - 1] + s[n/2]);
    char b[64];
    std::string j = std::string(",\"") + key + "runs_ms\":[";
    for (size_t i = 0; i < n; ++i){ std::snprintf(b, sizeof b, "%s%.3f", i ? "," : "", ms[i]); j += b; }
    std::snprintf(b, sizeof b, "],\"%smedian_ms\":%.3f", key, median);
    return j + b;
}
}  // namespace
std::string benchGpuJson(const CliParams& p){
    GpuAdapterInfo info; std::string err;
    if (!gpuInit(info, err)) return errorJson(err);
    GpuJob job = jobFor(p);
    FrameResult warm;
    if (!renderGpu(job.scene, p.width, p.height, job.prevVP, job.opts, warm, err)) return errorJson(err);
    std::vector<double> all, frame;
    if (!timeRuns(p, job, false, all, err) || !timeRuns(p, job, true, frame, err)) return errorJson(err);
    std::string j = "{\"renderer\":\"" + std::string(version()) + "\",\"backend\":\"webgpu\"";
    j += ",\"adapter\":{\"vendor\":\"" + info.vendor + "\",\"architecture\":\"" + info.architecture + "\"}";
    j += ",\"width\":" + std::to_string(p.width) + ",\"height\":" + std::to_string(p.height);
    j += std::string(",\"rt\":") + (p.rtao ? "true" : "false") + ",\"warmup_runs\":1";
    j += runsJson("", all) + runsJson("frame_only_", frame);
    return j + "}";
}
}
