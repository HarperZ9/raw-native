// raw_native_present: a window and a D3D12 swapchain as renderer infrastructure (N2).
// No game loop or gameplay input; those are G1 at M3. See raw/tools/present_cmd.hpp.
#include "raw/tools/present_cmd.hpp"
int main(int argc, char** argv){ return raw::presentCommand(argc, argv); }
