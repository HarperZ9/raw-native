// Effect C1's passes on DXR inline ray queries (raw/rhi/hw_rt.hpp spectralPasses; shader
// hw_shaders/hw_caustic.hlsl; contract docs/hw-rt.md, bounds evidence/hw-c1-caustic-bounds.json).
#include "hw_accel.hpp"
#include "raw/rhi/hw_rt.hpp"
#include <chrono>
#include <cstring>
namespace raw::rhi::hw {
namespace {
using d3d12::ComPtr;
using d3d12::HwQueue;
bool int64AtomicsOn(ID3D12Device* d){
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
    return SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm)) && sm.HighestShaderModel >= D3D_SHADER_MODEL_6_6;
}
bool upload256(HwQueue& q, const std::vector<std::vector<uint32_t>>& sets, ComPtr<ID3D12Resource>& out, std::string& err){
    std::vector<uint8_t> bytes(256 * (sets.empty() ? 1 : sets.size()), 0);
    for (size_t i = 0; i < sets.size(); ++i){
        if (sets[i].size() * 4 > 256){ err = "a dispatch's uniform words exceed 256 bytes"; return false; }
        std::memcpy(&bytes[256 * i], sets[i].data(), sets[i].size() * 4);
    }
    return q.uploadBuffer(bytes.data(), bytes.size(), out, err);
}
}  // namespace

SpectralPassResult spectralPasses(Device& dev, const SpectralPassInput& in){
    SpectralPassResult r;
    const auto t0 = std::chrono::steady_clock::now();
    ID3D12Device* d = static_cast<d3d12::D3d12Device&>(dev).d3d();
    if (!d3d12::hwRayQueryOn(d)){ r.error = disabled(kRayQuery) ? "ray query forced off" : "the device has no DXR tier 1.1"; return r; }
    if (!int64AtomicsOn(d)){ r.error = "the device has no shader model 6.6 (64-bit atomics for the light pass)"; return r; }
    HwQueue q;
    d3d12::CbvPipeline cam, light;
    if (!q.init(d, D3D12_COMMAND_LIST_TYPE_COMPUTE, 2, r.error) ||
        !d3d12::cbvPipeline(d, "hw_caustic_camera", true, 0, 5, true, cam, r.error) ||
        !d3d12::cbvPipeline(d, "hw_caustic_light", true, 0, 5, true, light, r.error)) return r;
    d3d12::HwAccel accel;
    if (!d3d12::buildAccel(q, in.tris, in.offset, false, accel, r.timing.buildMs, r.error)) return r;
    const uint64_t npx = uint64_t(in.width) * in.height;
    std::vector<uint64_t> zeros(npx * 3, 0);
    ComPtr<ID3D12Resource> T, G, TB, ACC, SPLAT, ccb, lcb;
    if (!d3d12::uploadUav(q, in.tris.data(), 4 * in.tris.size(), T, r.error) ||
        !d3d12::uploadUav(q, in.geo.data(), 4 * in.geo.size(), G, r.error) ||
        !d3d12::uploadUav(q, in.tables.data(), 4 * in.tables.size(), TB, r.error) ||
        !q.uavBuffer(npx * 16, ACC, r.error) || !d3d12::uploadUav(q, zeros.data(), 8 * zeros.size(), SPLAT, r.error) ||
        !upload256(q, in.cameraParams, ccb, r.error) || !upload256(q, in.lightParams, lcb, r.error)) return r;
    const std::vector<ID3D12Resource*> uavs{T.Get(), G.Get(), TB.Get(), ACC.Get(), SPLAT.Get()};
    const uint32_t gx = (in.width + 7) / 8, gy = (in.height + 7) / 8;
    auto run = [&](const d3d12::CbvPipeline& p, D3D12_GPU_VIRTUAL_ADDRESS cb, double& ms){
        if (!q.begin(r.error)) return false;
        q.timestamp(0);
        d3d12::cbvDispatch(q.list(), p, cb, uavs, accel.address(), gx, gy, 1);
        q.timestamp(1);
        if (!q.submitAndWait(2, r.error)) return false;
        ms = 1000.0 * (double)q.ticks(0, 1) / q.ticksPerSecond();
        return true;
    };
    // One submission a dispatch, each sized by the caller to stay under the OS GPU watchdog.
    for (size_t i = 0; i < in.cameraParams.size(); ++i){
        double ms = 0;
        if (!run(cam, ccb->GetGPUVirtualAddress() + 256 * i, ms)) return r;
        r.timing.sliceMs.push_back(ms);
    }
    for (size_t i = 0; i < in.lightParams.size(); ++i){
        double ms = 0;
        if (!run(light, lcb->GetGPUVirtualAddress() + 256 * i, ms)) return r;
        r.lightMs.push_back(ms);
    }
    r.camera.resize(npx * 4);
    r.splat.resize(npx * 3);
    if (!d3d12::readUav(q, ACC.Get(), r.camera.data(), npx * 16, r.error) ||
        !d3d12::readUav(q, SPLAT.Get(), r.splat.data(), npx * 24, r.error)) return r;
    r.timing.wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    r.ran = true;
    return r;
}

}  // namespace raw::rhi::hw
