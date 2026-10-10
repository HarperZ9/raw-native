// HW H1.4: serial and async-compute schedules on D3D12, and the wrong-wait control
// (raw/rhi/hw.hpp; bounds in evidence/hw-h1-4-bounds.json).
#include "hw_queue.hpp"
#include "raw/rhi/hw.hpp"
#include "raw_hw_dxil.hpp"
#include <cstring>
namespace raw::rhi::hw {
namespace {
using d3d12::ComPtr;
using d3d12::HwPipeline;
using d3d12::HwQueue;
constexpr uint32_t kThreads = 65536, kGroups = kThreads / 64;

bool pipe(HwQueue& q, const char* name, uint32_t uavs, HwPipeline& p, std::string& err){
    for (const HwDxilBlob& b : kHwDxil) if (std::strcmp(b.name, name) == 0) return q.pipeline(b.bytes, b.size, uavs, 1, false, p, err);
    err = std::string("no DXIL named ") + name;
    return false;
}
double nowMs(){ LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return 1000.0 * (double)t.QuadPart / (double)f.QuadPart; }
struct Work { HwPipeline pp, pw, pc, cp; ComPtr<ID3D12Resource> x, y, z; };
// P on the compute queue, W then C on the direct queue, C behind P's fence. The control holds P
// behind `gate` and lets C wait on value 0, which the fence has already reached.
bool runAsync(HwQueue& direct, HwQueue& compute, Work& w, uint32_t n, bool control, ID3D12Fence* gate, std::string& err){
    const uint32_t one = 1;
    uint64_t dv = 0, cv = 0;
    if (!compute.begin(err)) return false;
    compute.dispatch(w.cp, {w.x.Get()}, &n, 0, kGroups, 1, 1);
    if (!compute.submit(control ? gate : nullptr, 1, cv, err)) return false;
    if (!direct.begin(err)) return false;
    direct.dispatch(w.pw, {w.y.Get()}, &n, 0, kGroups, 1, 1);
    if (!direct.submit(nullptr, 0, dv, err)) return false;
    if (!direct.begin(err)) return false;
    direct.dispatch(w.pc, {w.x.Get(), w.y.Get(), w.z.Get()}, &one, 0, kGroups, 1, 1);
    if (!direct.submit(compute.fence(), control ? 0 : cv, dv, err) || !direct.waitCpu(dv, err)) return false;
    if (control) gate->Signal(1);   // release P only after the consumer has finished
    return compute.waitCpu(cv, err);
}
}  // namespace

AsyncRun asyncSchedule(Device& dev, Schedule s, uint32_t n){
    AsyncRun r;
    if (s != Schedule::Serial && disabled(kAsyncCompute)){ r.error = "async compute forced off"; return r; }
    ID3D12Device* d = static_cast<d3d12::D3d12Device&>(dev).d3d();
    HwQueue direct, compute;
    if (!direct.init(d, D3D12_COMMAND_LIST_TYPE_DIRECT, 0, r.error) || !compute.init(d, D3D12_COMMAND_LIST_TYPE_COMPUTE, 0, r.error)) return r;
    Work w;   // cp: P's pipeline on the compute queue. Committed DEFAULT buffers start zeroed: X reads 0 before P runs.
    ComPtr<ID3D12Resource> rb;
    if (!pipe(direct, "hw_async_p", 1, w.pp, r.error) || !pipe(direct, "hw_async_w", 1, w.pw, r.error) ||
        !pipe(direct, "hw_async_c", 3, w.pc, r.error) || !pipe(compute, "hw_async_p", 1, w.cp, r.error)) return r;
    if (!direct.uavBuffer(4ull * kThreads, w.x, r.error) || !direct.uavBuffer(4ull * kThreads, w.y, r.error) ||
        !direct.uavBuffer(4ull * kThreads, w.z, r.error) || !direct.readbackBuffer(4ull * kThreads, rb, r.error)) return r;
    ComPtr<ID3D12Fence> gate;   // the control's CPU-signalled fence
    if (FAILED(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)))){ r.error = "CreateFence failed"; return r; }
    const uint32_t one = 1;
    const double t0 = nowMs();
    if (s == Schedule::Serial){
        uint64_t dv = 0;
        if (!direct.begin(r.error)) return r;
        direct.dispatch(w.pp, {w.x.Get()}, &n, 0, kGroups, 1, 1); direct.uavBarrier();
        direct.dispatch(w.pw, {w.y.Get()}, &n, 0, kGroups, 1, 1); direct.uavBarrier();
        direct.dispatch(w.pc, {w.x.Get(), w.y.Get(), w.z.Get()}, &one, 0, kGroups, 1, 1);
        if (!direct.submit(nullptr, 0, dv, r.error) || !direct.waitCpu(dv, r.error)) return r;
    } else if (!runAsync(direct, compute, w, n, s == Schedule::WrongWaitControl, gate.Get(), r.error)) return r;
    r.wallMs = nowMs() - t0;
    if (!direct.begin(r.error)) return r;
    direct.copy(rb.Get(), w.z.Get(), 4ull * kThreads);
    if (!direct.submitAndWait(0, r.error)) return r;
    r.z.resize(kThreads);
    if (!direct.readBack(rb.Get(), r.z.data(), 4ull * kThreads, r.error)) return r;
    r.ran = true;
    return r;
}
}  // namespace raw::rhi::hw
