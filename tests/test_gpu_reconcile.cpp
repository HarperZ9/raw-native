#include "raw/gpu_reconcile.hpp"
#include "raw/gpu_tolerance.hpp"
#include "raw/scene.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
static FrameResult renderDefault(int w, int h, bool rt){
    Scene s = buildTestScene(w, h);
    Mat4 vp = mul(s.camera.proj(), s.camera.view());
    RenderOptions o; o.rtao = rt;
    return renderWithParams(s, w, h, vp, nullptr, o);
}
static bool has(const std::string& s, const char* sub){ return s.find(sub) != std::string::npos; }
int main(){
    const FrameResult cpu = renderDefault(64, 64, true);
    // A frame compared with itself passes every check with zero error.
    GpuReconcile same = reconcileGpuCpu(cpu, cpu, true);
    CHECK(same.sizeMatch);
    CHECK(same.pass());
    CHECK(same.maskMismatch == 0);
    CHECK(same.coveredBoth > 0);
    for (const auto& c : same.channels) CHECK_NEAR(c.rmse, 0.0, 0.0);
    std::string j = gpuCertificateJson(same, GpuAdapterInfo{"nvidia", "ada", "", "test", "d3d12"}, "raw-native test", "{}", 1.0, 2.0);
    CHECK(has(j, "\"verdict\":\"verified\""));
    CHECK(has(j, "\"schema\":\"raw-gpu-cert/1\""));
    CHECK(has(j, "\"does_not_prove\":["));
    CHECK(has(j, "\"vendor\":\"nvidia\""));

    // One RT AO pixel off by one ray step still passes; the max error is reported.
    FrameResult oneRay = renderDefault(64, 64, true);
    int fx = -1, fy = -1;
    for (int y = 0; y < 64 && fx < 0; ++y) for (int x = 0; x < 64; ++x) if (oneRay.g.mask.at(x,y)){ fx = x; fy = y; break; }
    CHECK(fx >= 0);
    oneRay.aoRT.at(fx, fy) += 1.0f / 64.0f;
    GpuReconcile r1 = reconcileGpuCpu(oneRay, cpu, true);
    CHECK(r1.pass());
    for (const auto& c : r1.channels) if (c.name == "ao_rt") CHECK_NEAR(c.maxError, 1.0 / 64.0, 1e-6);

    // A wrong AO everywhere fails its channel and the certificate says refuted.
    FrameResult wrong = renderDefault(64, 64, true);
    for (auto& v : wrong.aoSS.px) v = v * 0.5f;
    GpuReconcile r2 = reconcileGpuCpu(wrong, cpu, true);
    CHECK(!r2.pass());
    CHECK(has(gpuCertificateJson(r2, {}, "t", "{}", 0, 0), "\"verdict\":\"refuted\""));

    // Coverage that disagrees on more than the bound fails the mask check.
    FrameResult holes = renderDefault(64, 64, true);
    int cleared = 0;
    for (auto& m : holes.g.mask.px) if (m && cleared < 40){ m = 0; ++cleared; }
    GpuReconcile r3 = reconcileGpuCpu(holes, cpu, true);
    CHECK(!r3.maskPass);
    CHECK(!r3.pass());

    // A different frame size cannot be compared: unverifiable, not refuted.
    const FrameResult small = renderDefault(32, 32, true);
    GpuReconcile r4 = reconcileGpuCpu(small, cpu, true);
    CHECK(!r4.sizeMatch);
    CHECK(has(gpuCertificateJson(r4, {}, "t", "{}", 0, 0), "\"verdict\":\"unverifiable\""));

    // Without the ray-traced reference the RT channel and the verdict check are skipped.
    const FrameResult nort = renderDefault(64, 64, false);
    GpuReconcile r5 = reconcileGpuCpu(nort, nort, false);
    CHECK(r5.pass());
    for (const auto& c : r5.channels) CHECK(c.name != "ao_rt");
    CHECK(has(gpuCertificateJson(r5, {}, "t", "{}", 0, 0), "\"ao_verdict\":null"));
    return raw_test_summary();
}
