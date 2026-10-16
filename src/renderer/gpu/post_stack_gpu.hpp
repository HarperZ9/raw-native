#pragma once
// The post stack's device executor (raw/renderer/post_stack.hpp): one stage's schedule as a frame
// graph on an RHI device, each pass the style.wgsl pass of its name (exposure -> style_exposure,
// srgb-decode -> style_srgb_decode), with the PostParams uniform of web/motion/post.mjs. Frames are
// one vec4f a pixel; history frames are buffers this object owns and keeps across run() calls.
#include "raw/renderer/post_stack.hpp"
#include "raw/rhi/rhi.hpp"
#include <string>
#include <vector>
namespace raw::gpu_check::post_gpu {

struct PostParamsGpu { float size[4]; float p[16]; };
static_assert(sizeof(PostParamsGpu) == 80, "PostParams layout");

// The style.wgsl pass of a stack pass, or nullptr when it has none yet.
const char* shaderOf(const std::string& pass);
// The 16 parameter floats of a spec (each pass's params() in the web registry).
std::vector<float> paramsOf(const post_stack::Spec& spec);

class DeviceStack {
public:
    explicit DeviceStack(rhi::Device& dev) : dev_(dev) {}
    ~DeviceStack();
    DeviceStack(const DeviceStack&) = delete;
    DeviceStack& operator=(const DeviceStack&) = delete;
    bool run(const std::vector<post_stack::Spec>& specs, post_stack::Stage stage, const post_stack::HostFrame& input, int frame, double time,
             post_stack::HostFrame& out, std::string& err);
private:
    rhi::Device& dev_;
    std::vector<rhi::BufferHandle> history_;
};

}  // namespace raw::gpu_check::post_gpu
