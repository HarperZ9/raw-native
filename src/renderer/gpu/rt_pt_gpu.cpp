// The path tracer on the GPU from the host: see raw/renderer/rt_pt_gpu.hpp.
#include "raw/renderer/rt_pt_gpu.hpp"
#include "raw/renderer/rt_scenes.hpp"
#include "shaders/pt_layout.hpp"
#include "rt_dev.hpp"
#include "rt_pt_pack.hpp"
#include "../rt_pt_math.hpp"
#include <cmath>
#include <cstring>
namespace raw::gpu_check {
using namespace rtdev;
using namespace ptpack;

rt::PathTraceOutput pathTraceGpu(rhi::Device& device, const rt::PtScene& s, const rt::PathTraceDesc& d, std::uint32_t slice) {
    rt::PathTraceOutput O;
    O.width = d.width; O.height = d.height; O.spp = d.spp;
    if (rt::usesV3(s)) {   // docs/rt-interface-v3.md: v3 is CPU-only for now
        O.error = "path tracer interface v3 fields (lights, environment, fullMaterials) are CPU-only; pathTraceGpu takes v2 scenes";
        return O;
    }
    Dev D{device, O.error, passTable(pt_passes::kPasses)};
    slice = std::max<std::uint32_t>(1, slice);
    const Packed p = pack(s);
    const std::uint64_t npx = std::uint64_t(d.width) * std::uint64_t(d.height);
    // `slice` samples a submission is sized for 1280 x 720. Above that, keep a submission's sample-pixel
    // count at that budget: at 3840 x 2160 the default 16 spp a slice ran 3.2 s on raw_hall (RTX 4090)
    // and the watchdog removed the device. The sum over slices is unchanged (sample indices are absolute).
    constexpr std::uint64_t kSliceBudgetPx = 1280ull * 720ull;
    if (npx > kSliceBudgetPx) slice = std::max<std::uint32_t>(1, std::uint32_t(std::uint64_t(slice) * kSliceBudgetPx / npx));
    if (!(D.cl = device.begin(O.error))) return O;
    const BufferHandle NI = D.upload(p.ni.data(), p.ni.size() * 4, "pt ni"), NB = D.upload(p.nb.data(), p.nb.size() * 4, "pt nb");
    const BufferHandle T = D.upload(p.tris.data(), p.tris.size() * 4, "pt tris"), A = D.upload(p.attrs.data(), p.attrs.size() * 4, "pt attrs");
    const BufferHandle TX = D.upload(p.tex.data(), p.tex.size() * 4, "pt tex"), M = D.upload(p.mats.data(), p.mats.size() * 4, "pt mats");
    const BufferHandle ACC = D.make(npx * kStride * 4, kRW, "pt acc"), OUT = D.make(npx * kStride * 4, kRW, "pt out"), RB = D.make(npx * kStride * 4, kRB, "pt rb");
    const std::uint32_t gx = std::uint32_t((d.width + 7) / 8), gy = std::uint32_t((d.height + 7) / 8);
    std::uint32_t b = 0;
    // One submission a slice, so no single submission runs long enough for the OS GPU watchdog
    // (Windows TDR, 2 s by default): the first RTX run at 1280 x 720, 1024 spp recorded every
    // slice into one submission and failed (evidence/rt-r2-runs.json, run 15).
    do {
        if (b > 0 && !(D.cl = device.begin(O.error))) return O;
        const std::uint32_t n = std::min(slice, d.spp - b);
        const std::vector<std::uint32_t> w = params(s, d, d.sppBegin + b, n, b == 0, p.emitterOffset);
        D.run("pt_trace", {D.uniform(w.data(), w.size()), NI, NB, T, A, TX, M, ACC}, gx, gy);
        b += n;
        if (!device.submitAndWait(O.error)) return O;
    } while (b < d.spp);
    if (!(D.cl = device.begin(O.error))) return O;
    const std::vector<std::uint32_t> w = params(s, d, d.sppBegin, d.spp, false, p.emitterOffset);
    D.run("pt_finish", {D.uniform(w.data(), w.size()), ACC, OUT}, gx, gy);
    D.to({OUT}, Access::CopySrc);
    D.cl->copyBuffer(OUT, 0, RB, 0, npx * kStride * 4);
    if (!device.submitAndWait(O.error)) return O;
    std::vector<float> o;
    if (!D.read(RB, std::size_t(npx) * kStride, o)) return O;
    decode(o, npx, O);
    return O;
}

}  // namespace raw::gpu_check
