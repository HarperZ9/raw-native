#pragma once
#include "raw/render.hpp"
#include <string>
#include <vector>
namespace raw {
// What the GPU adapter says about itself. Strings come from the WebGPU adapter
// info or from DXGI and are recorded verbatim; empty means the API did not say.
// driver is the user-mode driver version DXGI reports (D3D12 only; a browser
// does not expose it).
struct GpuAdapterInfo {
    std::string vendor, architecture, device, description, backend, driver;
};
// One channel compared between the GPU frame and the CPU reference, over the
// pixels both sides cover.
struct GpuChannelCheck {
    std::string name;
    long long values{0};   // compared values (pixels x components)
    double rmse{0}, maxError{0}, bound{0};
    bool pass{false};
};
struct GpuReconcile {
    int width{0}, height{0};
    long long coveredEither{0}, coveredBoth{0}, maskMismatch{0};
    double maskMismatchFraction{0};
    bool maskPass{false};
    std::vector<GpuChannelCheck> channels;
    // The SS-versus-RT verdict each side reached on its own frame.
    bool rt{true};
    float cpuAoRmse{0}, gpuAoRmse{0};
    bool cpuWithin{false}, gpuWithin{false};
    bool reconcilePass{false}, verdictPass{false};
    bool sizeMatch{false};
    std::string reason;     // why the comparison could not be made, when it could not
    bool pass() const;
};
// Compare a GPU-rendered frame with the CPU reference for the same params.
// Bounds come from raw/gpu_tolerance.hpp. With rt == false the ray-traced
// channels are skipped and the verdict comparison is not made.
GpuReconcile reconcileGpuCpu(const FrameResult& gpu, const FrameResult& cpu, bool rt);
// raw-gpu-cert/1: the comparison above as a certificate. `paramsJson` is the
// canonical params, `renderer` the version string, `gpuMs` and `cpuMs` the
// wall times of the two renders (reported, not judged).
std::string gpuCertificateJson(const GpuReconcile& r, const GpuAdapterInfo& adapter,
                               const std::string& renderer, const std::string& paramsJson,
                               double gpuMs, double cpuMs);
}
