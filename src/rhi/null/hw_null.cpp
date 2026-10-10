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
