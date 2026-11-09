// The path tracer on hardware ray queries from the host (raw/renderer/rt_pt_hw.hpp).
#include "raw/renderer/rt_pt_hw.hpp"
#include "rt_pt_pack.hpp"
namespace raw::gpu_check {

HwPtResult pathTraceHw(rhi::Device& dev, const rt::PtScene& s, const rt::PathTraceDesc& d, const HwPtOptions& opt) {
    HwPtResult R;
    rt::PathTraceOutput& O = R.out;
    O.width = d.width; O.height = d.height; O.spp = d.spp;
    if (rt::usesV3(s)) { O.error = "the hardware path tracer takes interface v2 scenes, as pathTraceGpu"; return R; }
    const ptpack::Packed p = ptpack::pack(s);
    const std::uint64_t npx = std::uint64_t(d.width) * std::uint64_t(d.height);
    std::uint32_t slice = std::max<std::uint32_t>(1, opt.slice);
    constexpr std::uint64_t kSliceBudgetPx = 1280ull * 720ull;   // as pathTraceGpu: the OS GPU watchdog
    if (npx > kSliceBudgetPx) slice = std::max<std::uint32_t>(1, std::uint32_t(std::uint64_t(slice) * kSliceBudgetPx / npx));
    rhi::hw::PtPassInput in;
    in.width = std::uint32_t(d.width); in.height = std::uint32_t(d.height);
    in.tris = p.tris; in.attrs = p.attrs; in.mats = p.mats; in.tex = p.tex;
    for (int k = 0; k < 3; ++k) in.offset[k] = opt.offset[k];
    for (std::uint32_t b = 0; b < d.spp;) {
        const std::uint32_t n = std::min(slice, d.spp - b);
        in.sliceParams.push_back(ptpack::params(s, d, d.sppBegin + b, n, b == 0, p.emitterOffset));
        b += n;
    }
    in.finishParams = ptpack::params(s, d, d.sppBegin, d.spp, false, p.emitterOffset);
    const rhi::hw::PtPassResult r = rhi::hw::pathTracePasses(dev, in);
    R.timing = r.timing;
    if (!r.ran) { O.error = r.error.empty() ? "the hardware path tracer did not run" : r.error; return R; }
    ptpack::decode(r.out, npx, O);
    return R;
}

}  // namespace raw::gpu_check
