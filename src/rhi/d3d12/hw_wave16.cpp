// HW H1.2: full prefix sum (wave or group-shared group scans) and the bilateral filter (fp16 or
// fp32) on D3D12, timed on the hw compute queue (raw/rhi/hw.hpp; evidence/hw-h1-2-bounds.json).
#include "hw_queue.hpp"
#include "raw/rhi/hw.hpp"
#include "raw_hw_dxil.hpp"
#include <cstring>
namespace raw::rhi::hw {
namespace {
using d3d12::ComPtr;
using d3d12::HwPipeline;
using d3d12::HwQueue;

bool pipe(HwQueue& q, const char* name, uint32_t uavs, uint32_t constants, HwPipeline& p, std::string& err){
    for (const HwDxilBlob& b : kHwDxil) if (std::strcmp(b.name, name) == 0) return q.pipeline(b.bytes, b.size, uavs, constants, false, p, err);
    err = std::string("no DXIL named ") + name;
    return false;
}
// (groups x, y) for g groups of a 1D kernel that folds its group index at 32,768.
uint32_t gx(uint32_t g){ return g < 32768 ? g : 32768; }
uint32_t gy(uint32_t g){ return (g + 32767) / 32768; }
// Record and run `record` warmup + repeats + 1 times between two timestamps; the first run's
// result is the one read back, the measured runs are those after the warm-up.
template<class F> bool timed(HwQueue& q, int warmup, int repeats, std::vector<double>& ms, std::string& err, F&& record){
    for (int i = 0; i < 1 + warmup + repeats; ++i){
        if (!q.begin(err)) return false;
        q.timestamp(0);
        record();
        q.timestamp(1);
        if (!q.submitAndWait(2, err)) return false;
        if (i > warmup) ms.push_back(1000.0 * (double)q.ticks(0, 1) / q.ticksPerSecond());
    }
    return true;
}
bool readU32(HwQueue& q, ID3D12Resource* src, std::vector<uint32_t>& out, size_t n, std::string& err){
    ComPtr<ID3D12Resource> rb;
    if (!q.readbackBuffer(4ull * n, rb, err) || !q.begin(err)) return false;
    q.copy(rb.Get(), src, 4ull * n);
    if (!q.submitAndWait(0, err)) return false;
    out.resize(n);
    return q.readBack(rb.Get(), out.data(), 4ull * n, err);
}
}  // namespace

KernelRun scanFull(Device& dev, const std::vector<uint32_t>& in, ScanForm form, int warmup, int repeats){
    KernelRun r;
    if (form != ScanForm::Shared && disabled(kWaveOps)){ r.error = "wave ops forced off"; return r; }
    HwQueue q;
    if (!q.init(static_cast<d3d12::D3d12Device&>(dev).d3d(), D3D12_COMMAND_LIST_TYPE_COMPUTE, 2, r.error)) return r;
    const char* name = form == ScanForm::Shared ? "hw_scan_level_shared" : form == ScanForm::Wave ? "hw_scan_level_wave" : "hw_scan_level_drop";
    HwPipeline lvl, add;
    if (!pipe(q, name, 3, 1, lvl, r.error) || !pipe(q, "hw_scan_add", 2, 1, add, r.error)) return r;
    // Levels: count, then the totals of each level until one group remains.
    std::vector<uint32_t> counts{(uint32_t)in.size()};
    while (counts.back() > 64) counts.push_back((counts.back() + 63) / 64);
    std::vector<ComPtr<ID3D12Resource>> src(counts.size() + 1), dst(counts.size());
    ComPtr<ID3D12Resource> up;
    if (!q.uploadBuffer(in.data(), 4ull * in.size(), up, r.error)) return r;
    for (size_t l = 0; l < counts.size(); ++l)
        if (!q.uavBuffer(4ull * counts[l], src[l], r.error) || !q.uavBuffer(4ull * counts[l], dst[l], r.error)) return r;
    if (!q.uavBuffer(4ull * 64, src[counts.size()], r.error) || !q.begin(r.error)) return r;
    q.copy(src[0].Get(), up.Get(), 4ull * in.size());
    if (!q.submitAndWait(0, r.error)) return r;
    const bool ok = timed(q, warmup, repeats, r.ms, r.error, [&]{
        for (size_t l = 0; l < counts.size(); ++l){   // level l's totals are level l + 1's input
            const uint32_t g = (counts[l] + 63) / 64;
            q.dispatch(lvl, {src[l].Get(), dst[l].Get(), src[l + 1].Get()}, &counts[l], 0, gx(g), gy(g), 1);
            q.uavBarrier();
        }
        for (size_t l = counts.size() - 1; l-- > 0; ){   // add the scanned totals back down
            const uint32_t g = (counts[l] + 63) / 64;
            q.dispatch(add, {dst[l].Get(), dst[l + 1].Get()}, &counts[l], 0, gx(g), gy(g), 1);
            q.uavBarrier();
        }
    });
    if (!ok) return r;
    // Every repetition rewrites the levels from the unchanged input, so the last result equals the first.
    r.ran = readU32(q, dst[0].Get(), r.u, in.size(), r.error);
    return r;
}

KernelRun bilateral(Device& dev, const std::vector<float>& rgb, uint32_t w, uint32_t h, bool fp16, int warmup, int repeats){
    KernelRun r;
    if (fp16 && disabled(kNative16)){ r.error = "native 16-bit forced off"; return r; }
    HwQueue q;
    if (!q.init(static_cast<d3d12::D3d12Device&>(dev).d3d(), D3D12_COMMAND_LIST_TYPE_COMPUTE, 2, r.error)) return r;
    HwPipeline p;
    if (!pipe(q, fp16 ? "hw_bilateral_fp16" : "hw_bilateral_fp32", 2, 2, p, r.error)) return r;
    ComPtr<ID3D12Resource> up, src, dst;
    const uint64_t bytes = 4ull * rgb.size();
    if (!q.uploadBuffer(rgb.data(), bytes, up, r.error) || !q.uavBuffer(bytes, src, r.error) || !q.uavBuffer(bytes, dst, r.error) ||
        !q.begin(r.error)) return r;
    q.copy(src.Get(), up.Get(), bytes);
    if (!q.submitAndWait(0, r.error)) return r;
    const uint32_t wh[2] = {w, h};
    if (!timed(q, warmup, repeats, r.ms, r.error, [&]{ q.dispatch(p, {src.Get(), dst.Get()}, wh, 0, (w + 7) / 8, (h + 7) / 8, 1); })) return r;
    std::vector<uint32_t> bits;
    if (!readU32(q, dst.Get(), bits, rgb.size(), r.error)) return r;
    r.f.resize(bits.size());
    std::memcpy(r.f.data(), bits.data(), bytes);
    r.ran = true;
    return r;
}
}  // namespace raw::rhi::hw
