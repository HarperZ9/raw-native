#pragma once
// The RHI validation layer (ROADMAP M3 criterion 3; rules and checks in
// evidence/m3-aliasing-validation-bounds.json). It wraps any Device, its CommandList and its
// swapchains, keeps what it needs to judge each call (every object it created, each buffer's
// access since the last barrier in the open list, whether a list and a render pass are open), and
// checks thirteen rules, V01 to V13. A call that breaks a rule is recorded and not forwarded to
// the backend; submitAndWait then fails with the reports, so a run under the layer cannot pass
// while the layer has seen an error.
//
// Buffer state follows the backends: at the start of a list every buffer is Undefined (D3D12
// buffers decay to the common state between submissions, and the earlier submission has been
// waited for), and Undefined satisfies any first use. Inside a list, a use in another access than
// the buffer's current one, or any use after a write, needs a barrier first; copies into the same
// buffer may follow each other without one.
//
// RAW_NATIVE_RHI_VALIDATE=1 wraps the process device (rhi::device). Off by default.
#include "raw/rhi/rhi.hpp"
#include <memory>
#include <string>
#include <vector>
namespace raw::rhi {

struct ValidationReport {
    std::string rule;      // "V01" .. "V13"
    std::string message;
};
struct ValidationLog {
    std::vector<ValidationReport> reports;
    bool echo{false};      // also print each report to stderr as it happens
    bool has(const char* rule) const;
};

// A validating device over `inner`, which must outlive it. Reports go to `log`.
std::unique_ptr<Device> makeValidatingDevice(Device& inner, std::shared_ptr<ValidationLog> log);
// For the backends' rhi::device(): `inner` wrapped once when RAW_NATIVE_RHI_VALIDATE=1 (the
// wrapper lives for the process and echoes its reports), otherwise `inner` unchanged.
Device* validatedIfRequested(Device* inner);

}  // namespace raw::rhi
