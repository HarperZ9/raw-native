// The D3D12 build's shaders: every pass of shaders/hlsl/ compiled to DXIL by
// the Windows SDK's DXC at build time (cmake/gpu.cmake) and embedded here.
#include "shader_library.hpp"
#include "raw_d3d12_dxil.hpp"   // generated: kDxil[], one blob per pass
#include <cstring>
namespace raw::gpu_shaders {
rhi::ShaderCode find(const char* name, rhi::ShaderFormat format){
    if (format != rhi::ShaderFormat::Dxil) return {};
    for (const DxilBlob& b : kDxil)
        if (std::strcmp(b.name, name) == 0) return {rhi::ShaderFormat::Dxil, b.bytes, b.size};
    return {};
}
}
