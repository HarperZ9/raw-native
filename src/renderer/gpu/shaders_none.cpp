// Builds without a GPU backend carry no shader code.
#include "shader_library.hpp"
namespace raw::gpu_shaders {
rhi::ShaderCode find(const char*, rhi::ShaderFormat){ return {}; }
}
