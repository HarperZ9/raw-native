// The WebGPU build's shaders: the WGSL of shaders/, embedded by cmake/gpu.cmake.
// Each pass is its "//@pass <name>" section of passes.wgsl with common.wgsl in
// front of it.
#include "shader_library.hpp"
#include "raw_gpu_shaders.hpp"   // generated: kCommonWgsl, kPassesWgsl
#include <map>
#include <string>
namespace raw::gpu_shaders {
namespace {
// A Threads pass: its section of threads.wgsl, with the code before the first
// marker in front of it.
std::string threadsSource(const std::string& name){
    const std::string all = kThreadsWgsl, mark = "//@pass " + name + "\n";
    const size_t first = all.find("//@pass "), a = all.find(mark);
    if (a == std::string::npos) return {};
    const size_t b = all.find("//@pass ", a + mark.size());
    return all.substr(0, first) + "\n" + all.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
std::string passSource(const std::string& name){
    if (name.rfind("threads_", 0) == 0) return threadsSource(name);
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
