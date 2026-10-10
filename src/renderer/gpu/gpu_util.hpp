#pragma once
// Private to the GPU checks (src/renderer/gpu/): buffers and textures owned for the length of
// one check, uploads recorded at the start of a submission, and read-back of buffers and
// textures after it.
#include "raw/rhi/rhi.hpp"
#include "shader_library.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
namespace raw::gpu_check::util {

using Record = std::function<void(rhi::CommandList&)>;

struct Gpu {
    rhi::Device& dev;
    std::string err;
    std::vector<rhi::BufferHandle> buffers;
    std::vector<rhi::TextureHandle> textures;
    std::vector<Record> pre;                         // uploads, recorded first
    explicit Gpu(rhi::Device& d) : dev(d) {}
    ~Gpu() { for (auto b : buffers) dev.destroyBuffer(b); for (auto t : textures) dev.destroyTexture(t); }
    rhi::BufferHandle make(uint64_t bytes, rhi::BufferUsage u, const char* label) {
        const rhi::BufferHandle b = dev.createBuffer({bytes < 16 ? 16 : bytes, u, label}, err);
        if (b.valid()) buffers.push_back(b);
        return b;
    }
    rhi::BufferHandle upload(const void* data, uint64_t bytes, rhi::BufferUsage u, const char* label) {
        const rhi::BufferHandle b = make(bytes, u | rhi::BufferUsage::CopyDst, label);
        std::vector<uint8_t> copy(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + bytes);
        pre.push_back([b, copy](rhi::CommandList& c) { c.upload(b, 0, copy.data(), copy.size()); });
        return b;
    }
    rhi::TextureHandle texture(uint32_t w, uint32_t h, rhi::TextureFormat f, rhi::TextureUsage u, const char* label) {
        const rhi::TextureHandle t = dev.createTexture({w, h, f, u, label}, err);
        if (t.valid()) textures.push_back(t);
        return t;
    }
    // Runs `body` after the uploads in one submission, then copies each texture into a
    // read-back buffer and returns its rows (textureRowPitch apart) in `out`.
    bool run(const Record& body, const std::vector<std::pair<rhi::TextureHandle, rhi::TextureFormat>>& reads,
             std::vector<std::vector<uint8_t>>& out, uint32_t w, uint32_t h) {
        if (!err.empty()) return false;
        std::vector<rhi::BufferHandle> rb;
        for (auto& r : reads) rb.push_back(make(uint64_t(rhi::textureRowPitch(w, r.second)) * h, rhi::BufferUsage::MapRead | rhi::BufferUsage::CopyDst, "readback"));
        rhi::CommandList* c = err.empty() ? dev.begin(err) : nullptr;
        if (!c) return false;
        for (auto& f : pre) f(*c);
        pre.clear();
        body(*c);
        for (std::size_t k = 0; k < reads.size(); ++k) c->copyTextureToBuffer(reads[k].first, rb[k]);
        if (!dev.submitAndWait(err)) return false;
        out.clear();
        for (std::size_t k = 0; k < reads.size(); ++k) {
            const uint64_t bytes = uint64_t(rhi::textureRowPitch(w, reads[k].second)) * h;
            const void* p = dev.mapRead(rb[k], bytes, err);
            if (!p) return false;
            out.emplace_back(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + bytes);
            dev.unmap(rb[k]);
        }
        return true;
    }
};
// A raster pipeline from raster_layout.hpp's entry `name`.
template<class Layouts>
rhi::RasterPipelineHandle raster(rhi::Device& dev, const Layouts& layouts, const char* name, rhi::RasterPipelineDesc desc, std::string& err) {
    for (const auto& l : layouts) {
        if (std::strcmp(l.name, name) != 0) continue;
        const std::string n = name;
        desc.label = name;
        desc.vertex = gpu_shaders::find((n + ".vs").c_str(), dev.shaderFormat());
        desc.fragment = gpu_shaders::find((n + ".fs").c_str(), dev.shaderFormat());
        if (!desc.vertex.bytes || !desc.fragment.bytes) { err = "this build carries no " + n + " raster pass"; return {}; }
        desc.layout = std::span<const rhi::RasterBinding>(l.binds, std::size_t(l.bindCount));
        return dev.createRasterPipeline(desc, err);
    }
    err = std::string("no raster layout named ") + name;
    return {};
}
// IEEE half to float.
inline float halfToFloat(uint16_t h) {
    const uint32_t s = uint32_t(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023;
    uint32_t bits;
    if (e == 0) { if (!m) bits = s; else { float f = std::ldexp(float(m), -24); std::memcpy(&bits, &f, 4); bits |= s; } }
    else if (e == 31) bits = s | 0x7F800000u | (m << 13);
    else bits = s | ((e + 112) << 23) | (m << 13);
    float f; std::memcpy(&f, &bits, 4);
    return f;
}

}  // namespace raw::gpu_check::util
