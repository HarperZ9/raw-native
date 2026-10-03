// Builds without RAW_NATIVE_GPU: the GPU entry points exist and say plainly that
// this build has no GPU backend.
#include "raw/gpu.hpp"
namespace raw {
bool gpuCompiled(){ return false; }
bool gpuInit(GpuAdapterInfo&, std::string& err){
    err = "this build has no GPU backend; use the raw-native-gpu WebAssembly build";
    return false;
}
bool renderGpu(const Scene&, int, int, const Mat4&, const RenderOptions&, FrameResult&, std::string& err, bool){
    err = "this build has no GPU backend";
    return false;
}
}
