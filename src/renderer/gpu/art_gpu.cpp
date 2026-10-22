// The art checks' pass runner: see art_gpu.hpp.
#include "art_gpu.hpp"
#include "gpu_util.hpp"
#include "shaders/art_layout.hpp"
#include "shaders/style_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
namespace raw::gpu_check::art_gpu {
using rhi::Access;
using rhi::BufferUsage;
namespace {
struct Layout { const char* name{nullptr}; int count{0}; const rhi::Binding* binds{nullptr}; };
Layout layoutOf(const char* name) {
    for (const auto& L : art_passes::kPasses) if (std::strcmp(L.name, name) == 0) return {L.name, L.bindCount, L.binds};
    for (const auto& L : style_passes::kPasses) if (std::strcmp(L.name, name) == 0) return {L.name, L.bindCount, L.binds};
    return {};
}
Access accessOf(rhi::Binding b) {
    return b == rhi::Binding::Uniform ? Access::Uniform : b == rhi::Binding::StorageRead ? Access::StorageRead : Access::StorageWrite;
}
}  // namespace

Runner::~Runner() { for (const Buf& b : bufs_) if (b.h.valid()) dev_.destroyBuffer(b.h); }

int Runner::buffer(std::uint64_t bytes, const char* label) {
    const rhi::BufferHandle h = dev_.createBuffer({std::max<std::uint64_t>(bytes, 16), BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst, label}, err_);
    bufs_.push_back({h, bytes, Access::Undefined, {}});
    return int(bufs_.size() - 1);
}

int Runner::input(const void* data, std::uint64_t bytes, const char* label, bool uniform) {
    const BufferUsage u = uniform ? BufferUsage::Uniform | BufferUsage::CopyDst : BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst;
    const rhi::BufferHandle h = dev_.createBuffer({std::max<std::uint64_t>(bytes, 16), u, label}, err_);
    const auto* p = static_cast<const std::uint8_t*>(data);
    bufs_.push_back({h, bytes, Access::Undefined, std::vector<std::uint8_t>(p, p + bytes)});
    return int(bufs_.size() - 1);
}

bool Runner::pass(const char* name, const std::vector<int>& binds, std::uint32_t count) {
    return passGroups(name, binds, groupsX(count), groupsY(count));
}

bool Runner::passGroups(const char* name, const std::vector<int>& binds, std::uint32_t gx, std::uint32_t gy) {
    if (!err_.empty()) return false;
    const Layout L = layoutOf(name);
    if (!L.name) { err_ = std::string("no art or style pass ") + name; return false; }
    if (int(binds.size()) != L.count) { err_ = std::string("pass ") + name + ": binding count differs from its layout"; return false; }
    auto it = pipes_.find(name);
    if (it == pipes_.end()) {
        const rhi::PipelineHandle p = util::compute(dev_, L.name, L.binds, L.count, err_);
        if (!p.valid()) return false;
        it = pipes_.emplace(name, p).first;
    }
    Step s{it->second, binds, {}, gx, gy};
    for (int k = 0; k < L.count; ++k) s.use.push_back(accessOf(L.binds[k]));
    steps_.push_back(std::move(s));
    return true;
}

void Runner::move(rhi::CommandList& c, int b, Access to) {
    Buf& B = bufs_[std::size_t(b)];
    if (B.state == to && !rhi::isWrite(to)) return;
    const rhi::BufferBarrier bb[1] = {{B.h, B.state, to}};
    c.barrier(bb);
    B.state = to;
}

void Runner::record(rhi::CommandList& c, const Step& s) {
    std::vector<rhi::BufferHandle> h;
    for (std::size_t k = 0; k < s.binds.size(); ++k) { move(c, s.binds[k], s.use[k]); h.push_back(bufs_[std::size_t(s.binds[k])].h); }
    c.dispatch(s.pipe, h, s.gx, s.gy, 1);
}

bool Runner::run(const std::vector<std::pair<int, std::vector<std::uint32_t>*>>& reads) {
    if (!err_.empty()) return false;
    std::vector<rhi::BufferHandle> rb;
    for (const auto& r : reads)
        rb.push_back(dev_.createBuffer({std::max<std::uint64_t>(bufs_[std::size_t(r.first)].bytes, 16), BufferUsage::MapRead | BufferUsage::CopyDst, "art readback"}, err_));
    rhi::CommandList* c = err_.empty() ? dev_.begin(err_) : nullptr;
    bool ok = c != nullptr;
    if (ok && !uploaded_) {
        for (std::size_t b = 0; b < bufs_.size(); ++b) {
            if (bufs_[b].init.empty()) continue;
            move(*c, int(b), Access::CopyDst);
            c->upload(bufs_[b].h, 0, bufs_[b].init.data(), bufs_[b].init.size());
        }
        uploaded_ = true;
    }
    if (ok) {
        for (const Step& s : steps_) record(*c, s);
        for (std::size_t k = 0; k < reads.size(); ++k) {
            move(*c, reads[k].first, Access::CopySrc);
            c->copyBuffer(bufs_[std::size_t(reads[k].first)].h, 0, rb[k], 0, bufs_[std::size_t(reads[k].first)].bytes);
        }
        ok = dev_.submitAndWait(err_);
    }
    for (std::size_t k = 0; ok && k < reads.size(); ++k) {
        const std::uint64_t bytes = bufs_[std::size_t(reads[k].first)].bytes;
        const void* p = dev_.mapRead(rb[k], bytes, err_);
        if (!p) { ok = false; break; }
        reads[k].second->resize(std::size_t(bytes / 4));
        std::memcpy(reads[k].second->data(), p, std::size_t(bytes / 4) * 4);
        dev_.unmap(rb[k]);
    }
    for (rhi::BufferHandle h : rb) if (h.valid()) dev_.destroyBuffer(h);
    return ok;
}

bool Runner::time(int repeats, double& medianMs, std::size_t first, std::size_t last, int inner) {
    last = std::min(last, steps_.size());
    std::vector<double> t;
    for (int r = 0; r < repeats; ++r) {
        rhi::CommandList* c = dev_.begin(err_);
        if (!c) return false;
        for (int k = 0; k < inner; ++k) for (std::size_t s = first; s < last; ++s) record(*c, steps_[s]);
        const auto t0 = std::chrono::steady_clock::now();
        if (!dev_.submitAndWait(err_)) return false;
        t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / std::max(1, inner));
    }
    if (t.empty()) return false;
    std::sort(t.begin(), t.end());
    medianMs = t[t.size() / 2];
    return true;
}

}  // namespace raw::gpu_check::art_gpu
