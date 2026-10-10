// HW H1.0: what the D3D12 device says it offers (raw/rhi/hw.hpp).
#include "hw_queue.hpp"
#include "raw/rhi/hw.hpp"
#include <cstdio>
namespace raw::rhi::hw {
namespace {
const char* smName(D3D_SHADER_MODEL m){
    switch (m){
        case D3D_SHADER_MODEL_6_9: return "6.9"; case D3D_SHADER_MODEL_6_8: return "6.8"; case D3D_SHADER_MODEL_6_7: return "6.7";
        case D3D_SHADER_MODEL_6_6: return "6.6"; case D3D_SHADER_MODEL_6_5: return "6.5"; case D3D_SHADER_MODEL_6_4: return "6.4";
        case D3D_SHADER_MODEL_6_3: return "6.3"; case D3D_SHADER_MODEL_6_2: return "6.2"; case D3D_SHADER_MODEL_6_1: return "6.1";
        case D3D_SHADER_MODEL_6_0: return "6.0"; default: return "below 6.0";
    }
}
// The highest shader model the runtime and driver accept: ask from the top down, since a
// runtime that does not know a value rejects the query with E_INVALIDARG.
D3D_SHADER_MODEL highestShaderModel(ID3D12Device* d){
    const D3D_SHADER_MODEL order[] = {D3D_SHADER_MODEL_6_9, D3D_SHADER_MODEL_6_8, D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6,
        D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0};
    for (D3D_SHADER_MODEL m : order){
        D3D12_FEATURE_DATA_SHADER_MODEL sm{m};
        if (SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm))) return sm.HighestShaderModel;
    }
    return D3D_SHADER_MODEL_5_1;
}
template<class T> bool query(ID3D12Device* d, D3D12_FEATURE f, T& out){ return SUCCEEDED(d->CheckFeatureSupport(f, &out, sizeof out)); }
std::string str(const char* fmt, long a, long b = 0){ char buf[96]; std::snprintf(buf, sizeof buf, fmt, a, b); return buf; }
}  // namespace

Probe probe(Device& base){
    Probe p;
    p.backend = base.backendName();
    p.adapter = base.adapter().description;
    p.driver = base.adapter().driver;
    ID3D12Device* d = static_cast<d3d12::D3d12Device&>(base).d3d();
    const D3D_SHADER_MODEL sm = highestShaderModel(d);
    const std::string smDetail = std::string("highest shader model ") + smName(sm);

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    const bool has5 = query(d, D3D12_FEATURE_D3D12_OPTIONS5, o5);
    const int rt = has5 ? (int)o5.RaytracingTier : 0;
    const char* rtName = rt >= D3D12_RAYTRACING_TIER_1_2 ? "1.2" : rt >= D3D12_RAYTRACING_TIER_1_1 ? "1.1"
                       : rt >= D3D12_RAYTRACING_TIER_1_0 ? "1.0" : "not supported";
    const std::string rtDetail = std::string("raytracing tier ") + rtName;
    p.add(kRayPipeline, rt >= D3D12_RAYTRACING_TIER_1_0, "D3D12_FEATURE_D3D12_OPTIONS5.RaytracingTier >= 1.0", rtDetail);
    p.add(kRayQuery, rt >= D3D12_RAYTRACING_TIER_1_1 && sm >= D3D_SHADER_MODEL_6_5,
          "D3D12_FEATURE_D3D12_OPTIONS5.RaytracingTier >= 1.1 and shader model >= 6.5", rtDetail + ", " + smDetail);
    p.add(kOpacityMicromap, rt >= D3D12_RAYTRACING_TIER_1_2, "D3D12_FEATURE_D3D12_OPTIONS5.RaytracingTier >= 1.2", rtDetail);
    p.add(kReorder, false, "none in the Windows SDK 10.0.26100 headers",
          "shader execution reordering (SM 6.9 MaybeReorderThread) needs an Agility SDK runtime this build does not ship; the Vulkan path carries it");

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
    const bool mesh = query(d, D3D12_FEATURE_D3D12_OPTIONS7, o7) && o7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
    p.add(kMeshShader, mesh, "D3D12_FEATURE_D3D12_OPTIONS7.MeshShaderTier >= 1", mesh ? "mesh shader tier 1" : "not supported");

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{};
    const bool has1 = query(d, D3D12_FEATURE_D3D12_OPTIONS1, o1);
    p.add(kWaveOps, has1 && o1.WaveOps, "D3D12_FEATURE_D3D12_OPTIONS1.WaveOps",
          has1 ? str("wave lanes %ld to %ld", (long)o1.WaveLaneCountMin, (long)o1.WaveLaneCountMax) : "query failed");
    p.add(kInt64, has1 && o1.Int64ShaderOps, "D3D12_FEATURE_D3D12_OPTIONS1.Int64ShaderOps", "");

    D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4{};
    p.add(kNative16, query(d, D3D12_FEATURE_D3D12_OPTIONS4, o4) && o4.Native16BitShaderOpsSupported && sm >= D3D_SHADER_MODEL_6_2,
          "D3D12_FEATURE_D3D12_OPTIONS4.Native16BitShaderOpsSupported and shader model >= 6.2", smDetail);

    p.add(kCoopMatrix, false, "none in the Windows SDK 10.0.26100 headers",
          "D3D12 cooperative vectors are an Agility SDK preview; the Vulkan path carries VK_KHR_cooperative_matrix");

    D3D12_FEATURE_DATA_D3D12_OPTIONS o0{};
    const bool has0 = query(d, D3D12_FEATURE_D3D12_OPTIONS, o0);
    const bool bindless = has0 && o0.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 && sm >= D3D_SHADER_MODEL_6_6;
    p.add(kBindless, bindless, "D3D12_FEATURE_D3D12_OPTIONS.ResourceBindingTier >= 3 and shader model >= 6.6 (ResourceDescriptorHeap)",
          has0 ? str("resource binding tier %ld", (long)o0.ResourceBindingTier) + ", " + smDetail : "query failed");

    D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12{};
    p.add(kEnhancedBarriers, query(d, D3D12_FEATURE_D3D12_OPTIONS12, o12) && o12.EnhancedBarriersSupported,
          "D3D12_FEATURE_D3D12_OPTIONS12.EnhancedBarriersSupported", "");
    D3D12_FEATURE_DATA_D3D12_OPTIONS21 o21{};
    const bool wg = query(d, D3D12_FEATURE_D3D12_OPTIONS21, o21) && o21.WorkGraphsTier != D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED;
    p.add(kWorkGraphs, wg, "D3D12_FEATURE_D3D12_OPTIONS21.WorkGraphsTier", wg ? str("work graphs tier 0x%lx", (long)o21.WorkGraphsTier) : "not supported");

    // A compute queue is part of the API; what H1.4 measures is whether it overlaps.
    d3d12::HwQueue q;
    std::string qerr;
    const bool compute = q.init(d, D3D12_COMMAND_LIST_TYPE_COMPUTE, 2, qerr);
    p.add(kAsyncCompute, compute, "ID3D12Device::CreateCommandQueue(COMPUTE)", compute ? "a compute queue was created" : qerr);
    p.add(kTimestamps, compute && q.ticksPerSecond() > 0, "ID3D12CommandQueue::GetTimestampFrequency (compute queue)",
          compute ? str("%ld ticks a second", (long)q.ticksPerSecond()) : qerr);
    return p;
}
}  // namespace raw::rhi::hw
