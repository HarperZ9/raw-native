// The RHI backend of a build without a GPU: no device, and a reason that says
// how to get one. The renderer then reports --gpu as unverifiable.
#include "raw/rhi/rhi.hpp"
namespace raw::rhi {
const char* linkedBackend(){ return "none"; }
Device* device(std::string& err){
    err = "this build has no GPU backend; build with RAW_NATIVE_GPU_D3D12=ON on Windows "
          "or use the raw-native-gpu WebAssembly build";
    return nullptr;
}
}
