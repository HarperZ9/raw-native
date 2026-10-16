// The presentation host's passes and one frame as a frame graph (see present_frames.hpp).
#include "present_frames.hpp"
#include "raw/core/sha256.hpp"
#include "raw/graph/frame_graph.hpp"
#include "raw_present_shaders.hpp"
#include <cstring>
#include <utility>
#include <vector>
namespace raw::present {
namespace {
rhi::ShaderCode blob(const char* name){
    for (const PresentShaderBlob& b : kPresentShaders)
        if (std::strcmp(b.name, name) == 0) return {kPresentShaderFormat, b.bytes, b.size};
    return {};
}
rhi::RasterPipelineHandle pipeline(rhi::Device& dev, const char* label, const char* fs, std::span<const rhi::RasterBinding> layout,
                                   rhi::TextureFormat target, std::string& err){
    rhi::RasterPipelineDesc d;
    d.label = label; d.vertex = blob("present_vs"); d.fragment = blob(fs); d.layout = layout;
    d.targets[0] = target; d.targetCount = 1;
    return dev.createRasterPipeline(d, err);
}
struct SceneUniform { uint32_t width, height, frame, seed; };
struct BlitUniform { int32_t x, y; uint32_t pad[2]; };
}  // namespace

bool createPipelines(rhi::Device& dev, rhi::TextureFormat blitFormat, Pipelines& p, std::string& err){
    if (dev.shaderFormat() != kPresentShaderFormat){ err = "present: this build's shaders are not in the device's format"; return false; }
    using RB = rhi::RasterBinding;
    static constexpr RB uniformOnly[] = {RB::Uniform};
    static constexpr RB uniformTexture[] = {RB::Uniform, RB::Texture};
    static constexpr RB uniformStorage[] = {RB::Uniform, RB::StorageRead};
    p.scene = pipeline(dev, "present_scene", "present_scene", uniformOnly, rhi::TextureFormat::RGBA8Unorm, err);
    if (p.scene.valid()) p.blit = pipeline(dev, "present_blit", "present_blit", uniformTexture, blitFormat, err);
    if (p.blit.valid()) p.pq = pipeline(dev, "present_pq", "present_pq", uniformStorage, rhi::TextureFormat::RGB10A2Unorm, err);
    if (p.pq.valid()) p.pqControl = pipeline(dev, "present_pq_control", "present_pq_control", uniformStorage, rhi::TextureFormat::RGB10A2Unorm, err);
    return p.pqControl.valid();
}

bool makeTargets(rhi::Device& dev, uint32_t w, uint32_t h, Targets& t, std::string& err){
    t = {};
    t.width = w; t.height = h;
    t.offscreen = dev.createTexture({w, h, rhi::TextureFormat::RGBA8Unorm,
        rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc, "present offscreen"}, err);
    if (!t.offscreen.valid()) return false;
    t.readback = dev.createBuffer({uint64_t(rhi::textureRowPitch(w)) * h, rhi::BufferUsage::MapRead | rhi::BufferUsage::CopyDst, "present readback"}, err);
    if (t.readback.valid()) t.sceneUniform = dev.createBuffer({sizeof(SceneUniform), rhi::BufferUsage::Uniform, "present scene uniform"}, err);
    if (t.sceneUniform.valid()) t.blitUniform = dev.createBuffer({sizeof(BlitUniform), rhi::BufferUsage::Uniform, "present blit uniform"}, err);
    return t.blitUniform.valid();
}
void dropTargets(rhi::Device& dev, Targets& t){
    if (t.offscreen.valid()) dev.destroyTexture(t.offscreen);
    for (rhi::BufferHandle b : {t.readback, t.sceneUniform, t.blitUniform}) if (b.valid()) dev.destroyBuffer(b);
    t = {};
}

std::string hashRows(const void* mapped, uint32_t w, uint32_t h, uint32_t pitch, bool bgra){
    std::vector<unsigned char> tight(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(tight.data() + size_t(y) * w * 4, (const unsigned char*)mapped + size_t(y) * pitch, size_t(w) * 4);
    if (bgra) for (size_t i = 0; i < tight.size(); i += 4) std::swap(tight[i], tight[i + 2]);
    return sha256Hex(tight.data(), tight.size());
}

bool runFrame(rhi::Device& dev, const Pipelines& p, Targets& t, rhi::Swapchain* sc, const FrameSpec& spec,
              FrameOut& out, std::string& err){
    using rhi::Access;
    out = {};
    if (sc && (sc->width() != t.width || sc->height() != t.height)){ err = "present: targets and swapchain differ in size"; return false; }
    const uint32_t pitch = rhi::textureRowPitch(t.width);
    auto hashReadback = [&](std::string& e) -> bool {
        const void* m = dev.mapRead(t.readback, uint64_t(pitch) * t.height, e);
        if (!m) return false;
        const bool bgra = sc && sc->format() == rhi::TextureFormat::BGRA8Unorm;   // the read-back is of the backbuffer
        out.hash = hashRows(m, t.width, t.height, pitch, bgra);
        if (spec.keepPixels){
            out.pixels.resize(size_t(t.width) * t.height * 4);
            for (uint32_t y = 0; y < t.height; ++y)
                std::memcpy(out.pixels.data() + size_t(y) * t.width * 4, (const unsigned char*)m + size_t(y) * pitch, size_t(t.width) * 4);
            if (bgra) for (size_t i = 0; i < out.pixels.size(); i += 4) std::swap(out.pixels[i], out.pixels[i + 2]);
        }
        dev.unmap(t.readback);
        return true;
    };
    graph::FrameGraph g(&dev);
    const graph::Resource image = g.createHost("offscreen");
    const graph::Resource rb = g.importBuffer("readback", t.readback);
    g.addPass("scene", {{image, Access::StorageWrite}}, [&](graph::PassContext& c){
        const SceneUniform u{t.width, t.height, spec.frame, spec.seed};
        c.cmd->upload(t.sceneUniform, 0, &u, sizeof u);
        rhi::RenderPassDesc rp; rp.color[0].texture = t.offscreen;
        c.cmd->beginRenderPass(rp);
        const rhi::RasterBind b[] = {{t.sceneUniform, {}, {}}};
        c.cmd->draw(p.scene, b, 3);
        c.cmd->endRenderPass();
    });
    if (!sc){
        g.addPass("readback", {{image, Access::StorageRead}, {rb, Access::CopyDst}}, [&](graph::PassContext& c){
            c.cmd->copyTextureToBuffer(t.offscreen, t.readback);
        });
        g.markOutput(rb);
        if (!g.execute(err)) return false;
        out.graph = g.describe();
        return spec.readback ? hashReadback(err) : true;
    }
    const graph::Resource back = g.presentTo(*sc, spec.syncInterval, [&](std::string& e){ return !spec.readback || hashReadback(e); });
    g.addPass("blit", {{image, Access::StorageRead}, {back, Access::StorageWrite}}, [&](graph::PassContext& c){
        const BlitUniform u{spec.blitOffsetX, 0, {0, 0}};
        c.cmd->upload(t.blitUniform, 0, &u, sizeof u);
        rhi::RenderPassDesc rp; rp.color[0].texture = sc->current();
        c.cmd->beginRenderPass(rp);
        const rhi::RasterBind b[] = {{t.blitUniform, {}, {}}, {{}, t.offscreen, {}}};
        c.cmd->draw(p.blit, b, 3);
        c.cmd->endRenderPass();
    });
    if (spec.readback){
        g.addPass("readback", {{back, Access::StorageRead}, {rb, Access::CopyDst}}, [&](graph::PassContext& c){
            c.cmd->copyTextureToBuffer(sc->current(), t.readback);
        });
        g.markOutput(rb);
    }
    const bool ok = g.execute(err);
    out.status = g.presentStatus();
    out.graph = g.describe();
    return ok;
}
}  // namespace raw::present
