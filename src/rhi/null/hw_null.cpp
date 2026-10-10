// HW H1.0 on a backend without hardware features (the null and WebGPU backends): the probe
// names the backend and reports no feature, and nothing runs.
#include "raw/rhi/hw.hpp"
namespace raw::rhi::hw {
Probe probe(Device& dev){
    Probe p;
    p.backend = dev.backendName();
    p.adapter = dev.adapter().description;
    p.driver = dev.adapter().driver;
    p.error = "the " + p.backend + " backend exposes no hardware features beyond the RHI";
    return p;
}
std::vector<FunctionalCheck> functionalChecks(Device&, const Probe&, std::string& err){
    err = "no hardware features on this backend";
    return {};
}
Timing timestampLinearity(Device&, bool, int){
    Timing t;
    t.error = "no timestamp queries on this backend";
    return t;
}
}
namespace raw::rhi::hw {
TraceResult traceRayQuery(Device&, const std::vector<float>&, const std::vector<RayIn>&, const TraceOptions&, std::vector<HitOut>&){
    TraceResult r;
    r.error = "no inline ray query on this backend";
    return r;
}
}
namespace raw::rhi::hw {
AsyncRun asyncSchedule(Device&, Schedule, uint32_t){
    AsyncRun r;
    r.error = "no queues beyond the RHI on this backend";
    return r;
}
}
namespace raw::rhi::hw {
KernelRun scanFull(Device&, const std::vector<uint32_t>&, ScanForm, int, int){
    KernelRun r; r.error = "no hardware kernels on this backend"; return r;
}
KernelRun bilateral(Device&, const std::vector<float>&, uint32_t, uint32_t, bool, int, int){
    KernelRun r; r.error = "no hardware kernels on this backend"; return r;
}
}
namespace raw::rhi::hw {
VisDraw drawVisibility(Device&, const VisInput&, GeomPath, int, int){
    VisDraw r; r.error = "no graphics pipelines beyond the RHI on this backend"; return r;
}
}
