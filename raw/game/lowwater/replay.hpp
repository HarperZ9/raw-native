// The Low Water game layer over raw/story: replays a recorded run of the JS engine (a golden trace,
// docs/story-trace.md) through the C++ story VM and returns the trace the VM writes. The rules,
// world and combat of Low Water are not ported yet (steps 3 to 8 of the port); their answers come
// from the golden trace's host records, in order, and a call that does not match the next recorded
// one is a divergence.
#pragma once
#include "raw/story/story.hpp"
#include <string>

namespace raw::game::lowwater {

struct ReplayResult {
    std::string trace;                 // the VM's trace, one record per line
    std::string error;                 // non-empty when the VM threw or a host call did not match
    size_t host_calls = 0, vm_records = 0;
};
// golden: the JS trace text. sources: the act's .lwd files. until: "" or an id prefix (the era 1 cut).
ReplayResult replay(const std::string& golden, const std::vector<raw::story::Source>& sources, const std::string& until);
// 0 when equal, else the 1-based number of the first differing line.
size_t first_difference(const std::string& a, const std::string& b);

}  // namespace raw::game::lowwater
