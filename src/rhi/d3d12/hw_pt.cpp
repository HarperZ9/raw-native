// HW H2: the path tracer's passes and a persistent ray caster on DXR inline ray queries
// (raw/rhi/hw_rt.hpp; contract docs/hw-rt.md, bounds evidence/hw-h2-pt-bounds.json).
#include "hw_accel.hpp"
#include "raw/rhi/hw_rt.hpp"
#include "raw_hw_dxil.hpp"
#include <chrono>
#include <cstring>
namespace raw::rhi::hw {
namespace {
using d3d12::ComPtr;
using d3d12::HwQueue;
constexpr uint64_t kStride = 36;   // floats a pixel in ACC and O

double since(std::chrono::steady_clock::time_point t0){
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
// Every dispatch's uniform words in one upload buffer, each at a 256-byte aligned offset.
bool constants(HwQueue& q, const std::vector<std::vector<uint32_t>>& sets, ComPtr<ID3D12Resource>& out, std::string& err){
    std::vector<uint8_t> bytes(256 * (sets.size() ? sets.size() : 1), 0);
    for (size_t i = 0; i < sets.size(); ++i){
        if (sets[i].size() * 4 > 256){ err = "a dispatch's uniform words exceed 256 bytes"; return false; }
        std::memcpy(&bytes[256 * i], sets[i].data(), sets[i].size() * 4);
    }
    return q.uploadBuffer(bytes.data(), bytes.size(), out, err);
}
bool timed(HwQueue& q, const d3d12::CbvPipeline& p, D3D12_GPU_VIRTUAL_ADDRESS cbv, const std::vector<ID3D12Resource*>& uavs,
           D3D12_GPU_VIRTUAL_ADDRESS srv, uint32_t gx, uint32_t gy, double& ms, std::string& err){
    if (!q.begin(err)) return false;
    q.timestamp(0);
    d3d12::cbvDispatch(q.list(), p, cbv, uavs, srv, gx, gy, 1);
    q.timestamp(1);
    if (!q.submitAndWait(2, err)) return false;
    ms = 1000.0 * (double)q.ticks(0, 1) / q.ticksPerSecond();
    return true;
}

class D3d12RayCaster final : public RayCaster {
public:
    HwQueue q;
    d3d12::HwAccel accel;
    d3d12::HwPipeline pipe;
    double build{0}, trace{0};
    bool refit(const std::vector<float>& tris, std::string& err) override { return d3d12::refitAccel(q, accel, tris, build, err); }
    bool closest(const std::vector<RayIn>& rays, std::vector<HitOut>& hits, std::string& err) override;
    double buildMs() const override { return build; }
    double traceMs() const override { return trace; }
};
bool D3d12RayCaster::closest(const std::vector<RayIn>& rays, std::vector<HitOut>& hits, std::string& err){
    hits.clear();
    if (rays.empty()) return true;
    const uint64_t rayBytes = sizeof(RayIn) * rays.size(), hitBytes = 16ull * rays.size();
    ComPtr<ID3D12Resource> rayBuf, hitBuf;
    if (!d3d12::uploadUav(q, rays.data(), rayBytes, rayBuf, err) || !q.uavBuffer(hitBytes, hitBuf, err) || !q.begin(err)) return false;
    const uint32_t count = (uint32_t)rays.size();
    q.timestamp(0);
    q.dispatch(pipe, {rayBuf.Get(), hitBuf.Get()}, &count, accel.address(), (count + 63) / 64, 1, 1);
    q.timestamp(1);
    if (!q.submitAndWait(2, err)) return false;
    trace = 1000.0 * (double)q.ticks(0, 1) / q.ticksPerSecond();
    std::vector<float> raw(4 * rays.size());
    if (!d3d12::readUav(q, hitBuf.Get(), raw.data(), hitBytes, err)) return false;
    hits.resize(rays.size());
    for (size_t i = 0; i < rays.size(); ++i){
        int32_t tri; std::memcpy(&tri, &raw[4 * i + 3], 4);
        hits[i] = {raw[4 * i], raw[4 * i + 1], raw[4 * i + 2], tri};
    }
    return true;
}
}  // namespace

std::unique_ptr<RayCaster> RayCaster::create(Device& dev, const std::vector<float>& tris, bool allowUpdate, std::string& err,
                                             const float offset[3]){
    ID3D12Device* d = static_cast<d3d12::D3d12Device&>(dev).d3d();
    if (!d3d12::hwRayQueryOn(d)){ err = disabled(kRayQuery) ? "ray query forced off" : "the device has no DXR tier 1.1"; return nullptr; }
    auto rc = std::make_unique<D3d12RayCaster>();
    const float zero[3] = {0, 0, 0};
    if (!rc->q.init(d, D3D12_COMMAND_LIST_TYPE_COMPUTE, 2, err)) return nullptr;
    const HwDxilBlob* blob = nullptr;
    for (const HwDxilBlob& b : kHwDxil) if (std::strcmp(b.name, "hw_rayquery") == 0) blob = &b;
    if (!blob || !rc->q.pipeline(blob->bytes, blob->size, 2, 1, true, rc->pipe, err)) return nullptr;
    if (!d3d12::buildAccel(rc->q, tris, offset ? offset : zero, allowUpdate, rc->accel, rc->build, err)) return nullptr;
    return rc;
}

PtPassResult pathTracePasses(Device& dev, const PtPassInput& in){
    PtPassResult r;
    const auto t0 = std::chrono::steady_clock::now();
    ID3D12Device* d = static_cast<d3d12::D3d12Device&>(dev).d3d();
    if (!d3d12::hwRayQueryOn(d)){ r.error = disabled(kRayQuery) ? "ray query forced off" : "the device has no DXR tier 1.1"; return r; }
    HwQueue q;
    d3d12::CbvPipeline trace, finish;
    if (!q.init(d, D3D12_COMMAND_LIST_TYPE_COMPUTE, 2, r.error) ||
        !d3d12::cbvPipeline(d, "hw_pt", true, 1, 7, true, trace, r.error) ||
        !d3d12::cbvPipeline(d, "hw_pt_finish", true, 1, 2, false, finish, r.error)) return r;
    d3d12::HwAccel accel;
    if (!d3d12::buildAccel(q, in.tris, in.offset, false, accel, r.timing.buildMs, r.error)) return r;
    const uint64_t npx = uint64_t(in.width) * in.height, accBytes = npx * kStride * 4;
    ComPtr<ID3D12Resource> ni, nb, T, A, TX, M, ACC, O, cb, fcb;
    if (!d3d12::uploadUav(q, nullptr, 0, ni, r.error) || !d3d12::uploadUav(q, nullptr, 0, nb, r.error) ||
        !d3d12::uploadUav(q, in.tris.data(), 4 * in.tris.size(), T, r.error) ||
        !d3d12::uploadUav(q, in.attrs.data(), 4 * in.attrs.size(), A, r.error) ||
        !d3d12::uploadUav(q, in.tex.data(), 4 * in.tex.size(), TX, r.error) ||
        !d3d12::uploadUav(q, in.mats.data(), 4 * in.mats.size(), M, r.error) ||
        !q.uavBuffer(accBytes, ACC, r.error) || !q.uavBuffer(accBytes, O, r.error) ||
        !constants(q, in.sliceParams, cb, r.error) || !constants(q, {in.finishParams}, fcb, r.error)) return r;
    const uint32_t gx = (in.width + 7) / 8, gy = (in.height + 7) / 8;
    // One submission a slice, so no submission runs into the OS GPU watchdog (as pathTraceGpu).
    for (size_t s = 0; s < in.sliceParams.size(); ++s){
        double ms = 0;
        if (!timed(q, trace, cb->GetGPUVirtualAddress() + 256 * s, {ni.Get(), nb.Get(), T.Get(), A.Get(), TX.Get(), M.Get(), ACC.Get()},
                   accel.address(), gx, gy, ms, r.error)) return r;
        r.timing.sliceMs.push_back(ms);
    }
    if (!timed(q, finish, fcb->GetGPUVirtualAddress(), {ACC.Get(), O.Get()}, 0, gx, gy, r.timing.finishMs, r.error)) return r;
    r.out.resize(npx * kStride);
    if (!d3d12::readUav(q, O.Get(), r.out.data(), accBytes, r.error)) return r;
    r.timing.wallMs = since(t0);
    r.ran = true;
    return r;
}

}  // namespace raw::rhi::hw
