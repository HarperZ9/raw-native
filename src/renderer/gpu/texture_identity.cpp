// The sampled-texture identity check: see raw/renderer/texture_identity.hpp.
#include "raw/renderer/texture_identity.hpp"
#include "raw/renderer/sampler.hpp"
#include "shader_library.hpp"
#include "shaders/raster_layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
namespace raw::gpu_check {
namespace {
constexpr uint32_t SW = 37, SH = 23, TW = 64, TH = 48;
constexpr float SCALE[2] = {1.43f, 1.37f}, OFFSET[2] = {-0.21f, -0.17f};   // uv runs past [0, 1] on both axes

std::vector<uint8_t> sourceTexels(bool swapRows) {
    std::vector<uint8_t> t(size_t(SW) * SH * 4);
    for (uint32_t y = 0; y < SH; ++y) for (uint32_t x = 0; x < SW; ++x) for (uint32_t c = 0; c < 4; ++c) {
        uint32_t h = x * 73856093u ^ y * 19349663u ^ (c + 1) * 83492791u;
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        const uint32_t row = swapRows ? SH - 1 - y : y;
        t[(size_t(row) * SW + x) * 4 + c] = uint8_t(c == 3 ? 128 + (h >> 25) : h >> 24);   // alpha kept high so the copy is plain
    }
    return t;
}
// The fragment shader's uv, computed in f32 as the GPU does.
void uvAt(uint32_t x, uint32_t y, float& u, float& v) {
    u = (float(x) + 0.5f) / float(TW) * SCALE[0] + OFFSET[0];
    v = (float(y) + 0.5f) / float(TH) * SCALE[1] + OFFSET[1];
}
TextureCase compare(const char* name, const uint8_t* gpu, uint32_t pitch, const std::vector<uint8_t>& src, sampler::Filter f, sampler::Address a) {
    TextureCase c; c.name = name;
    const sampler::Image im{int(SW), int(SH), src};
    for (uint32_t y = 0; y < TH; ++y) for (uint32_t x = 0; x < TW; ++x) {
        float u, v; uvAt(x, y, u, v);
        if (f == sampler::Filter::Nearest && sampler::edgeDistance(im, u, v) < 1e-3) { ++c.exempt; continue; }
        const auto ref = sampler::sample(im, u, v, f, a);
        const uint8_t* p = gpu + size_t(y) * pitch + size_t(x) * 4;
        for (int k = 0; k < 4; ++k) c.maxCodes = std::max(c.maxCodes, std::abs(int(p[k]) - int(sampler::unorm8(ref[k]))));
        ++c.pixels;
    }
    const int bound = f == sampler::Filter::Nearest ? 0 : 2;
    c.pass = c.maxCodes <= bound && (f == sampler::Filter::Linear || c.exempt * 100 < int(TW * TH));
    return c;
}
}  // namespace

std::string TextureIdentity::json() const {
    std::string s = "{\n \"backend\": \"" + backend + "\",\n \"adapter\": \"" + adapter + "\",\n";
    if (!error.empty()) return s + " \"error\": \"" + error + "\"\n}\n";
    s += " \"cases\": [\n";
    for (size_t i = 0; i < cases.size(); ++i) {
        char b[256];
        std::snprintf(b, sizeof b, "  {\"case\": \"%s\", \"pixels\": %d, \"exempt\": %d, \"max_codes\": %d, \"pass\": %s}%s\n", cases[i].name.c_str(),
                      cases[i].pixels, cases[i].exempt, cases[i].maxCodes, cases[i].pass ? "true" : "false", i + 1 < cases.size() ? "," : "");
        s += b;
    }
    return s + " ],\n \"control_fails\": " + (controlFails ? "true" : "false") + ",\n \"pass\": " + (pass() ? "true" : "false") + "\n}\n";
}

TextureIdentity textureIdentity(rhi::Device& dev) {
    return textureIdentity(dev, gpu_shaders::find("texture_identity.vs", dev.shaderFormat()), gpu_shaders::find("texture_identity.fs", dev.shaderFormat()));
}
TextureIdentity textureIdentity(rhi::Device& dev, const rhi::ShaderCode& vs, const rhi::ShaderCode& fs) {
    TextureIdentity r;
    r.backend = dev.backendName(); r.adapter = dev.adapter().description;
    std::string err;
    if (!vs.bytes || !fs.bytes) { r.error = "no texture_identity shader code"; return r; }
    const auto& L = raster_passes::kRasters[0];
    rhi::RasterPipelineDesc pd{"texture_identity", vs, fs, std::span<const rhi::RasterBinding>(L.binds, L.bindCount)};
    const rhi::RasterPipelineHandle pipe = dev.createRasterPipeline(pd, err);
    const std::vector<uint8_t> src = sourceTexels(false);
    const rhi::TextureHandle tex = dev.createTexture({SW, SH, rhi::TextureFormat::RGBA8Unorm, rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst, "identity source"}, err);
    const rhi::TextureHandle tgt = dev.createTexture({TW, TH, rhi::TextureFormat::RGBA8Unorm, rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc, "identity target"}, err);
    const float map[8] = {float(TW), float(TH), SCALE[0], SCALE[1], OFFSET[0], OFFSET[1], 0, 0};
    const rhi::BufferHandle ubo = dev.createBuffer({sizeof map, rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst, "identity map"}, err);
    const uint32_t pitch = rhi::textureRowPitch(TW);
    const rhi::BufferHandle rb = dev.createBuffer({uint64_t(pitch) * TH, rhi::BufferUsage::MapRead | rhi::BufferUsage::CopyDst, "identity readback"}, err);
    if (!pipe.valid() || !tex.valid() || !tgt.valid() || !ubo.valid() || !rb.valid()) { r.error = err.empty() ? "a resource was not created" : err; return r; }
    struct S { const char* name; rhi::Filter f; rhi::AddressMode a; sampler::Filter rf; sampler::Address ra; };
    const S cases[4] = {{"nearest/clamp", rhi::Filter::Nearest, rhi::AddressMode::ClampToEdge, sampler::Filter::Nearest, sampler::Address::Clamp},
                        {"nearest/repeat", rhi::Filter::Nearest, rhi::AddressMode::Repeat, sampler::Filter::Nearest, sampler::Address::Repeat},
                        {"linear/clamp", rhi::Filter::Linear, rhi::AddressMode::ClampToEdge, sampler::Filter::Linear, sampler::Address::Clamp},
                        {"linear/repeat", rhi::Filter::Linear, rhi::AddressMode::Repeat, sampler::Filter::Linear, sampler::Address::Repeat}};
    for (int k = 0; k < 4; ++k) {
        const rhi::SamplerHandle smp = dev.createSampler({cases[k].f, cases[k].a, cases[k].name}, err);
        rhi::CommandList* cl = smp.valid() ? dev.begin(err) : nullptr;
        if (!cl) { r.error = err; return r; }
        if (k == 0) cl->uploadTexture(tex, src.data(), SW, SH);
        cl->upload(ubo, 0, map, sizeof map);
        rhi::RenderPassDesc pass;
        pass.color[0] = {tgt, {0, 0, 0, 0}};
        cl->beginRenderPass(pass);
        const rhi::RasterBind binds[3] = {{ubo, {}, {}}, {{}, tex, {}}, {{}, {}, smp}};
        cl->draw(pipe, binds, 3);
        cl->endRenderPass();
        cl->copyTextureToBuffer(tgt, rb);
        if (!dev.submitAndWait(err)) { r.error = err; return r; }
        const auto* px = static_cast<const uint8_t*>(dev.mapRead(rb, uint64_t(pitch) * TH, err));
        if (!px) { r.error = err; return r; }
        r.cases.push_back(compare(cases[k].name, px, pitch, src, cases[k].rf, cases[k].ra));
        if (k == 0) r.controlFails = !compare("control", px, pitch, sourceTexels(true), cases[k].rf, cases[k].ra).pass;
        dev.unmap(rb);
    }
    return r;
}
}  // namespace raw::gpu_check
