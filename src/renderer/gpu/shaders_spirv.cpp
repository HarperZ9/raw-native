// The Vulkan build's shaders (N3, evidence/n3-vulkan-compute-bounds.json): every pass of
// shaders/hlsl/, the HLSL scripts/wgsl_to_hlsl.py generates from the one WGSL source, compiled to
// SPIR-V by glslang's HLSL front end at build time (cmake/vulkan_rhi.cmake) and embedded here.
//
// Control (bounds file, "control"): RAW_NATIVE_SPIRV_CONTROL=swap hands out the ssao pass when
// rtao is asked for, so a check that cannot tell the two apart is caught failing to fail.
#include "shader_library.hpp"
#include "raw_spirv_shaders.hpp"   // generated: kSpirv[], one blob per pass
#include <cstdlib>
#include <cstring>
namespace raw::gpu_shaders {
rhi::ShaderCode find(const char* name, rhi::ShaderFormat format){
    if (format != rhi::ShaderFormat::Spirv) return {};
    const char* control = std::getenv("RAW_NATIVE_SPIRV_CONTROL");
    if (control && std::strcmp(control, "swap") == 0 && std::strcmp(name, "rtao") == 0) name = "ssao";
    for (const SpirvBlob& b : kSpirv)
        if (std::strcmp(b.name, name) == 0) return {rhi::ShaderFormat::Spirv, b.words, b.bytes};
    return {};
}
}  // namespace raw::gpu_shaders
