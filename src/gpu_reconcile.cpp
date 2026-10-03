#include "raw/gpu_reconcile.hpp"
#include "raw/gpu_tolerance.hpp"
#include "raw/certificate.hpp"
#include <cmath>
#include <cstdio>
namespace raw {
namespace tol = gpu_tolerance;
namespace {
struct Acc {
    double sumSq{0}, max{0}; long long n{0};
    void add(double d){ d = std::fabs(d); sumSq += d*d; if (d > max) max = d; ++n; }
    GpuChannelCheck done(const std::string& name, double bound) const {
        GpuChannelCheck c; c.name = name; c.values = n; c.bound = bound;
        c.rmse = n ? std::sqrt(sumSq / (double)n) : 0.0; c.maxError = max;
        c.pass = n > 0 && std::isfinite(c.rmse) && c.rmse <= bound;
        return c;
    }
};
std::string jstr(const std::string& s){
    std::string o = "\"";
    for (char ch : s){
        if (ch == '"' || ch == '\\'){ o += '\\'; o += ch; }
        else if ((unsigned char)ch < 0x20){ char b[8]; std::snprintf(b, sizeof b, "\\u%04x", (unsigned char)ch); o += b; }
        else o += ch;
    }
    return o + "\"";
}
std::string num(double v){
    if (!std::isfinite(v)) return "null";
    char b[40]; std::snprintf(b, sizeof b, "%.9g", v); return b;
}
}
bool GpuReconcile::pass() const {
    if (!sizeMatch || !maskPass) return false;
    for (const auto& c : channels) if (!c.pass) return false;
    if (rt && (!reconcilePass || !verdictPass)) return false;
    return true;
}
GpuReconcile reconcileGpuCpu(const FrameResult& gpu, const FrameResult& cpu, bool rt){
    GpuReconcile r;
    r.rt = rt;
    r.width = cpu.g.w; r.height = cpu.g.h;
    r.sizeMatch = gpu.g.w == cpu.g.w && gpu.g.h == cpu.g.h && gpu.frame.w == cpu.frame.w && gpu.frame.h == cpu.frame.h
                  && (!rt || (gpu.aoRT.w == cpu.aoRT.w && gpu.aoRT.h == cpu.aoRT.h));
    if (!r.sizeMatch) return r;
    Acc depth, pos, nrm, mot, ss, rta, frame;
    for (int y = 0; y < r.height; ++y) for (int x = 0; x < r.width; ++x){
        bool a = gpu.g.mask.at(x,y) != 0, b = cpu.g.mask.at(x,y) != 0;
        if (a || b) ++r.coveredEither;
        if (a != b){ ++r.maskMismatch; continue; }
        if (!a) continue;
        ++r.coveredBoth;
        float dc = cpu.g.depth.at(x,y);
        depth.add((gpu.g.depth.at(x,y) - dc) / (std::fabs(dc) > 1e-6f ? dc : 1.0f));
        Vec3 gp = gpu.g.position.at(x,y), cp = cpu.g.position.at(x,y);
        pos.add(gp.x-cp.x); pos.add(gp.y-cp.y); pos.add(gp.z-cp.z);
        Vec3 gn = gpu.g.normal.at(x,y), cn = cpu.g.normal.at(x,y);
        nrm.add(gn.x-cn.x); nrm.add(gn.y-cn.y); nrm.add(gn.z-cn.z);
        Vec2 gm = gpu.g.motion.at(x,y), cm = cpu.g.motion.at(x,y);
        mot.add(gm.x-cm.x); mot.add(gm.y-cm.y);
        ss.add(gpu.aoSS.at(x,y) - cpu.aoSS.at(x,y));
        if (rt) rta.add(gpu.aoRT.at(x,y) - cpu.aoRT.at(x,y));
        Vec3 gf = gpu.frame.at(x,y), cf = cpu.frame.at(x,y);
        frame.add(gf.x-cf.x); frame.add(gf.y-cf.y); frame.add(gf.z-cf.z);
    }
    r.maskMismatchFraction = r.coveredEither ? (double)r.maskMismatch / (double)r.coveredEither : 0.0;
    r.maskPass = r.coveredEither > 0 && r.maskMismatchFraction <= tol::kMaskMismatchFraction;
    r.channels.push_back(depth.done("depth_rel", tol::kDepthRelRmse));
    r.channels.push_back(pos.done("position", tol::kPositionRmse));
    r.channels.push_back(nrm.done("normal", tol::kNormalRmse));
    r.channels.push_back(mot.done("motion", tol::kMotionRmse));
    r.channels.push_back(ss.done("ao_ss", tol::kAoSsRmse));
    if (rt) r.channels.push_back(rta.done("ao_rt", tol::kAoRtRmse));
    r.channels.push_back(frame.done("frame", tol::kFrameRmse));
    r.cpuAoRmse = cpu.rec.rmse; r.gpuAoRmse = gpu.rec.rmse;
    r.cpuWithin = cpu.rec.withinTolerance; r.gpuWithin = gpu.rec.withinTolerance;
    r.reconcilePass = std::fabs((double)r.gpuAoRmse - (double)r.cpuAoRmse) <= tol::kReconcileRmseDelta;
    r.verdictPass = !tol::kVerdictMustMatch || r.cpuWithin == r.gpuWithin;
    return r;
}
std::string gpuCertificateJson(const GpuReconcile& r, const GpuAdapterInfo& a,
                               const std::string& renderer, const std::string& paramsJson,
                               double gpuMs, double cpuMs){
    const char* verdict = !r.sizeMatch ? "unverifiable" : (r.pass() ? "verified" : "refuted");
    std::string o = "{\"schema\":\"raw-gpu-cert/1\"";
    o += ",\"claim\":\"the GPU frame matches the CPU reference within the committed tolerance\"";
    o += std::string(",\"verdict\":\"") + verdict + "\"";
    o += ",\"oracle\":\"raw-gpu-cpu-v1\"";
    if (!r.reason.empty()) o += ",\"reason\":" + jstr(r.reason);
    o += ",\"renderer\":" + jstr(renderer);
    o += ",\"params\":" + paramsJson;
    o += ",\"adapter\":{\"vendor\":" + jstr(a.vendor) + ",\"architecture\":" + jstr(a.architecture)
       + ",\"device\":" + jstr(a.device) + ",\"description\":" + jstr(a.description)
       + ",\"backend\":" + jstr(a.backend) + "}";
    o += ",\"tolerance_source\":\"raw/gpu_tolerance.hpp, committed before the first GPU run\"";
    o += ",\"coverage\":{\"covered_either\":" + std::to_string(r.coveredEither)
       + ",\"covered_both\":" + std::to_string(r.coveredBoth)
       + ",\"mismatch\":" + std::to_string(r.maskMismatch)
       + ",\"mismatch_fraction\":" + num(r.maskMismatchFraction)
       + ",\"bound\":" + num(gpu_tolerance::kMaskMismatchFraction)
       + ",\"pass\":" + (r.maskPass ? "true" : "false") + "}";
    o += ",\"channels\":[";
    for (size_t i = 0; i < r.channels.size(); ++i){
        const auto& c = r.channels[i];
        o += std::string(i ? "," : "") + "{\"name\":" + jstr(c.name) + ",\"values\":" + std::to_string(c.values)
           + ",\"rmse\":" + num(c.rmse) + ",\"max_error\":" + num(c.maxError)
           + ",\"bound\":" + num(c.bound) + ",\"pass\":" + (c.pass ? "true" : "false") + "}";
    }
    o += "]";
    if (r.rt && r.sizeMatch){
        o += ",\"ao_verdict\":{\"cpu_rmse\":" + exactFloat(r.cpuAoRmse) + ",\"gpu_rmse\":" + exactFloat(r.gpuAoRmse)
           + ",\"rmse_delta_bound\":" + num(gpu_tolerance::kReconcileRmseDelta)
           + ",\"cpu\":\"" + (r.cpuWithin ? "verified" : "refuted") + "\",\"gpu\":\"" + (r.gpuWithin ? "verified" : "refuted")
           + "\",\"pass\":" + ((r.reconcilePass && r.verdictPass) ? "true" : "false") + "}";
    } else {
        o += ",\"ao_verdict\":null";
    }
    o += ",\"timing_ms\":{\"gpu\":" + num(gpuMs) + ",\"cpu\":" + num(cpuMs) + ",\"judged\":false}";
    o += ",\"does_not_prove\":["
         "\"Agreement with the CPU path is not agreement with ground truth: the CPU ray-traced AO is itself a 64-sample estimate.\","
         "\"Checked on the adapter and driver named here only. Another GPU, driver or browser may differ.\","
         "\"An RMSE bound limits the average difference, not the worst pixel. Maximum errors are reported, not bounded.\","
         "\"One built-in scene. No claim about other geometry, lights or materials.\","
         "\"The timings are one run each and are not judged.\"]";
    return o + "}";
}
}
