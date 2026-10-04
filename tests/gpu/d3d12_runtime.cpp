// Runtime test for the D3D12 backend: render on the adapter, render the CPU
// reference, and require the GPU certificate to pass. Built only with
// RAW_NATIVE_GPU_D3D12=ON. With no hardware adapter it prints the reason and
// exits 77, which CTest reports as skipped; it never reports a pass without
// having rendered on a GPU.
#include "raw/gpu.hpp"
#include "raw/gpu_reconcile.hpp"
#include "raw/scene.hpp"
#include "check.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace raw;
int main(){
    GpuAdapterInfo info; std::string err;
    if (!gpuInit(info, err)){
        std::printf("SKIP: no D3D12 adapter to render on: %s\n", err.c_str());
        return 77;
    }
    std::printf("adapter: %s %s (%s), driver %s\n", info.vendor.c_str(), info.description.c_str(),
                info.device.c_str(), info.driver.c_str());
    CHECK(std::strcmp(gpuBackendName(), "d3d12") == 0);
    // A software adapter is never a GPU result, whatever DXGI's flags say.
    if (info.vendor == "microsoft" && !std::getenv("RAW_NATIVE_D3D12_WARP")){
        std::printf("FAIL: a software adapter was selected as hardware\n"); return 1; }
    const int w = 96, h = 80;
    for (bool rt : {true, false}){
        Scene s = buildTestScene(w, h);
        Camera prev = s.camera; prev.eye = prev.eye + Vec3{0.3f, 0.0f, -0.3f};
        const Mat4 prevVP = mul(prev.proj(), prev.view());
        RenderOptions o; o.rtao = rt;
        FrameResult g;
        if (!renderGpu(s, w, h, prevVP, o, g, err)){ std::printf("FAIL: render: %s\n", err.c_str()); return 1; }
        const FrameResult c = renderWithParams(s, w, h, prevVP, nullptr, o);
        const GpuReconcile r = reconcileGpuCpu(g, c, rt);
        std::printf("rt=%d covered=%lld mismatch=%lld", (int)rt, r.coveredEither, r.maskMismatch);
        for (const auto& ch : r.channels) std::printf(" %s=%.3g", ch.name.c_str(), ch.rmse);
        std::printf(" -> %s\n", r.pass() ? "verified" : "refuted");
        CHECK(r.sizeMatch);
        CHECK(r.pass());
        CHECK(g.motionValid > 0);
        // Frame-only mode computes the same frame and reads back nothing else.
        FrameResult f;
        CHECK(renderGpu(s, w, h, prevVP, o, f, err, true));
        CHECK(f.frame.px.size() == g.frame.px.size() && f.g.mask.px.empty());
        bool same = f.frame.px.size() == g.frame.px.size();
        for (size_t i = 0; same && i < f.frame.px.size(); ++i)
            same = f.frame.px[i].x == g.frame.px[i].x && f.frame.px[i].y == g.frame.px[i].y && f.frame.px[i].z == g.frame.px[i].z;
        CHECK(same);
    }
    return raw_test_summary();
}
