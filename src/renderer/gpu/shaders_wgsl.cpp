// The WebGPU build's shaders: the WGSL of shaders/, embedded by cmake/gpu.cmake.
// Each pass is its "//@pass <name>" section of passes.wgsl with common.wgsl in
// front of it.
#include "shader_library.hpp"
#include "raw_gpu_shaders.hpp"   // generated: kCommonWgsl, kPassesWgsl, kThreadsWgsl, kRasterWgsl, kPbrWgsl, kCubeWgsl, kLightWgsl, kShadowWgsl, kPostWgsl, kSwrWgsl, kRtWgsl, kPtWgsl, kSdfWgsl
#include <map>
#include <string>
namespace raw::gpu_shaders {
namespace {
// A pass of a module with its own shared code (threads.wgsl, pbr.wgsl): its section,
// with the code before the module's first marker in front of it.
std::string moduleSource(const char* text, const std::string& name, const char* prelude = ""){
    const std::string all = text, mark = "//@pass " + name + "\n";
    const size_t first = all.find("//@pass "), a = all.find(mark);
    if (a == std::string::npos) return {};
    const size_t b = all.find("//@pass ", a + mark.size());
    return std::string(prelude) + "\n" + all.substr(0, first) + "\n" + all.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
// A raster pass: its section of raster.wgsl alone (one module, entry points vs and
// fs). "<name>.vs" and "<name>.fs" both name it, matching the D3D12 build's DXIL.
std::string rasterSource(std::string name){
    const size_t dot = name.find('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    const std::string all = kRasterWgsl, mark = "//@raster " + name + "\n";
    const size_t a = all.find(mark);
    if (a == std::string::npos) return {};
    const size_t b = all.find("//@raster ", a + mark.size());
    return all.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
std::string passSource(const std::string& name){
    if (name.rfind("threads_", 0) == 0) return moduleSource(kThreadsWgsl, name);
    if (name.rfind("pbr_", 0) == 0) return moduleSource(kPbrWgsl, name, kCubeWgsl);
    if (name.rfind("light_", 0) == 0) return moduleSource(kLightWgsl, name, kCubeWgsl);
    if (name.rfind("swr_", 0) == 0) return moduleSource(kSwrWgsl, name);
    if (name.rfind("rt_", 0) == 0) return moduleSource(kRtWgsl, name);
    if (name.rfind("pt_", 0) == 0) return moduleSource(kPtWgsl, name);
    if (name.rfind("sdf_", 0) == 0) return moduleSource(kSdfWgsl, name);
    if (std::string s = moduleSource(kShadowWgsl, name); !s.empty()) return s;   // shadow_depth is a raster pass
    if (name.rfind("post_", 0) == 0) return moduleSource(kPostWgsl, name);
    if (std::string r = rasterSource(name); !r.empty()) return r;
    const std::string all = kPassesWgsl, mark = "//@pass " + name + "\n";
    size_t a = all.find(mark);
    if (a == std::string::npos) return {};
    size_t b = all.find("//@pass ", a + mark.size());
    return std::string(kCommonWgsl) + "\n" + all.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
}
rhi::ShaderCode find(const char* name, rhi::ShaderFormat format){
    if (format != rhi::ShaderFormat::Wgsl) return {};
    static std::map<std::string, std::string> cache;   // the code must outlive the call
    auto it = cache.find(name);
    if (it == cache.end()){
        std::string src = passSource(name);
        if (src.empty()) return {};
        it = cache.emplace(name, std::move(src)).first;
    }
    return {rhi::ShaderFormat::Wgsl, it->second.data(), it->second.size()};
}
}
