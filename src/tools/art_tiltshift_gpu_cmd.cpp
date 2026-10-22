// raw_native_cli art tiltshift --gpu/--cost: g1 and g3 (filled in with the WGSL passes).
#include "art_report.hpp"
namespace raw::art_cmd {
std::string tiltGpu(const Options&, bool& pass) { pass = false; Json j; j.str("error", "not built yet"); return j.done(); }
}  // namespace raw::art_cmd
