#pragma once
// Private to src/renderer/gpu: compute dispatch with tracked buffer access for the RT passes
// (rt_gpu.cpp, rt_pt_gpu.cpp). Each use of a buffer records the barrier from its last access.
#include "raw/rhi/rhi.hpp"
#include "shader_library.hpp"
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>
namespace raw::gpu_check::rtdev {
using rhi::Access;
using rhi::BufferHandle;
using rhi::BufferUsage;
inline const BufferUsage kRW = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
inline const BufferUsage kRB = BufferUsage::MapRead | BufferUsage::CopyDst;
inline constexpr std::uint32_t kChunk = 256;

// Buffers with their current access, so each use records exactly the barrier it needs.
struct PassInfo { const char* name; int bindCount; const rhi::Binding* binds; };
template<class Layouts> std::vector<PassInfo> passTable(const Layouts& l) {
    std::vector<PassInfo> v;
    for (const auto& x : l) v.push_back({x.name, x.bindCount, x.binds});
    return v;
}
struct Dev {
    rhi::Device& dev;
    std::string& err;
    std::vector<PassInfo> passes;
    std::map<std::uint32_t, rhi::PipelineHandle> pipes;
    std::vector<BufferHandle> all;
    std::map<std::uint64_t, Access> state;
    rhi::CommandList* cl{nullptr};
    ~Dev() { for (auto b : all) dev.destroyBuffer(b); }
    BufferHandle make(std::uint64_t bytes, BufferUsage u, const char* label) {
        const BufferHandle b = dev.createBuffer({bytes < 16 ? 16 : bytes, u, label}, err);
        if (b.valid()) all.push_back(b);
        return b;
    }
    Access& st(BufferHandle b) { return state[(std::uint64_t(b.gen) << 32) | b.index]; }
    void to(std::initializer_list<BufferHandle> bs, Access a) {
        std::vector<rhi::BufferBarrier> v;
        for (BufferHandle b : bs) { v.push_back({b, st(b), a}); st(b) = a; }
        cl->barrier(v);
    }
    BufferHandle uniform(const std::uint32_t* words, std::size_t n) {
        const BufferHandle b = make(n * 4, BufferUsage::Uniform | BufferUsage::CopyDst, "rt params");
        cl->upload(b, 0, words, n * 4);
        st(b) = Access::CopyDst;
        to({b}, Access::Uniform);
        return b;
    }
    BufferHandle upload(const void* data, std::uint64_t bytes, const char* label) {
        const BufferHandle b = make(bytes, kRW, label);
        cl->upload(b, 0, data, bytes);
        st(b) = Access::CopyDst;
        return b;
    }
    bool pipe(int k, rhi::PipelineHandle& out) {
        const PassInfo& L = passes[std::size_t(k)];
        const rhi::ShaderCode code = gpu_shaders::find(L.name, dev.shaderFormat());
        if (!code.bytes) { err = std::string("this build carries no ") + L.name + " shader"; return false; }
        out = dev.createComputePipeline({L.name, code, std::span<const rhi::Binding>(L.binds, std::size_t(L.bindCount))}, err);
        return out.valid();
    }
    // A dispatch of pass `name` (by layout order) over `binds`; every storage binding goes to StorageWrite.
    void run(const char* name, std::initializer_list<BufferHandle> binds, std::uint32_t x, std::uint32_t y = 1) {
        if (!err.empty()) return;                                   // the first error stands; nothing more is recorded
        int k = 0;
        while (k < int(passes.size()) && std::strcmp(passes[std::size_t(k)].name, name) != 0) ++k;
        if (k == int(passes.size())) { err = std::string("no pass named ") + name; return; }
        if (!pipes.count(std::uint32_t(k)) && !pipe(k, pipes[std::uint32_t(k)])) return;
        std::vector<BufferHandle> v(binds);
        std::vector<rhi::BufferBarrier> bar;
        for (std::size_t i = 1; i < v.size(); ++i) { bar.push_back({v[i], st(v[i]), Access::StorageWrite}); st(v[i]) = Access::StorageWrite; }
        cl->barrier(bar);
        cl->dispatch(pipes[std::uint32_t(k)], v, x, y, 1);
    }
    void params(std::uint32_t* w, std::uint32_t n, std::uint32_t m, std::uint32_t radius, std::uint32_t nextId, std::uint32_t k,
                std::uint32_t j, std::uint32_t count, std::uint32_t mode, float far) {
        const std::uint32_t v[8] = {n, m, radius, nextId, k, j, count, mode};
        std::memcpy(w, v, sizeof v);
        std::memcpy(w + 8, &far, 4);
        w[9] = w[10] = w[11] = 0;
    }
    template<class T> bool read(BufferHandle b, std::size_t n, std::vector<T>& out) {
        const void* p = dev.mapRead(b, std::uint64_t(n * sizeof(T)), err);
        if (!p) return false;
        out.assign(static_cast<const T*>(p), static_cast<const T*>(p) + n);
        dev.unmap(b);
        return true;
    }
    void scan(BufferHandle a, BufferHandle sums, BufferHandle ub, std::uint32_t count) {
        const std::uint32_t chunks = (count + kChunk - 1) / kChunk;
        run("rt_scan_chunk", {ub, a, sums}, (chunks + 63) / 64);
        run("rt_scan_top", {ub, sums}, 1);
        run("rt_scan_add", {ub, a, sums}, (count + 63) / 64);
    }
};
}  // namespace raw::gpu_check::rtdev
