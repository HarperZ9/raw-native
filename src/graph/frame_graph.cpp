#include "raw/graph/frame_graph.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace raw::graph {
using rhi::Access;
namespace {
bool pureRead(Access a){ return a == Access::Uniform || a == Access::StorageRead || a == Access::CopySrc; }
bool envOn(const char* name){ const char* v = std::getenv(name); return v && std::strcmp(v, "1") == 0; }
}
FrameGraph::FrameGraph(rhi::Device* device)
    : device_(device), aliasing_(envOn("RAW_NATIVE_GRAPH_ALIAS")), noFill_(envOn("RAW_NATIVE_GRAPH_ALIAS_NOFILL")) {}
const char* accessName(Access a){
    switch (a){
        case Access::Undefined: return "undefined"; case Access::Uniform: return "uniform";
        case Access::StorageRead: return "storage-read"; case Access::StorageWrite: return "storage-write";
        case Access::CopySrc: return "copy-src"; case Access::CopyDst: return "copy-dst";
    }
    return "?";
}
rhi::BufferHandle PassContext::buffer(Resource r) const { return graph ? graph->buffer(r) : rhi::BufferHandle{}; }

FrameGraph::~FrameGraph(){
    if (!device_) return;
    for (Res& r : resources_) if (r.kind == Kind::Transient && r.slot < 0 && r.buffer.valid()) device_->destroyBuffer(r.buffer);
    for (Slot& sl : slots_) if (sl.buffer.valid()) device_->destroyBuffer(sl.buffer);
}
Resource FrameGraph::createBuffer(std::string name, const rhi::BufferDesc& desc){
    resources_.push_back({std::move(name), Kind::Transient, desc, {}});
    compiled_ = false;
    return {(uint32_t)resources_.size() - 1};
}
Resource FrameGraph::importBuffer(std::string name, rhi::BufferHandle buffer){
    resources_.push_back({std::move(name), Kind::Imported, {}, buffer});
    compiled_ = false;
    return {(uint32_t)resources_.size() - 1};
}
Resource FrameGraph::createHost(std::string name){
    resources_.push_back({std::move(name), Kind::Host, {}, {}});
    compiled_ = false;
    return {(uint32_t)resources_.size() - 1};
}
void FrameGraph::markOutput(Resource r){
    if (r.index < resources_.size()) resources_[r.index].output = true;
    compiled_ = false;
}
void FrameGraph::addPass(std::string name, std::vector<Use> uses, PassFn fn){
    passes_.push_back({std::move(name), std::move(uses), std::move(fn)});
    compiled_ = false;
}
Resource FrameGraph::presentTo(rhi::Swapchain& swapchain, uint32_t syncInterval, std::function<bool(std::string&)> beforePresent){
    swapchain_ = &swapchain; syncInterval_ = syncInterval; beforePresent_ = std::move(beforePresent);
    const Resource r = createHost("swapchain");
    markOutput(r);
    return r;
}
rhi::BufferHandle FrameGraph::buffer(Resource r) const {
    if (r.index >= resources_.size()) return {};
    const Res& x = resources_[r.index];
    return x.slot >= 0 ? slots_[(std::size_t)x.slot].buffer : x.buffer;
}

// Every use names a resource of this graph, once per pass; buffers need a
// device; nothing is read before something writes it (imports excepted).
bool FrameGraph::validate(std::string& err) const {
    std::vector<bool> written(resources_.size(), false);
    for (const Res& r : resources_){
        if (r.kind == Kind::Transient && !device_){ err = "frame graph: buffer " + r.name + " in a graph with no device"; return false; }
    }
    for (std::size_t i = 0; i < resources_.size(); ++i) written[i] = resources_[i].kind == Kind::Imported;
    for (const Pass& p : passes_){
        for (std::size_t a = 0; a < p.uses.size(); ++a){
            const Use& u = p.uses[a];
            if (u.resource.index >= resources_.size()){ err = "frame graph: pass " + p.name + " uses an unknown resource"; return false; }
            for (std::size_t b = 0; b < a; ++b)
                if (p.uses[b].resource.index == u.resource.index){
                    err = "frame graph: pass " + p.name + " uses " + resources_[u.resource.index].name + " twice"; return false; }
            if (pureRead(u.access) && !written[u.resource.index]){
                err = "frame graph: pass " + p.name + " reads " + resources_[u.resource.index].name + " before any pass writes it";
                return false;
            }
        }
        for (const Use& u : p.uses) if (!pureRead(u.access)) written[u.resource.index] = true;
    }
    return true;
}
// Walk back from the outputs: a pass is kept when it writes a resource that an
// output or a later kept pass needs, and then everything it uses is needed.
void FrameGraph::cull(){
    std::vector<bool> needed(resources_.size(), false);
    for (std::size_t i = 0; i < resources_.size(); ++i) needed[i] = resources_[i].output;
    for (auto p = passes_.rbegin(); p != passes_.rend(); ++p){
        p->kept = std::any_of(p->uses.begin(), p->uses.end(),
                              [&](const Use& u){ return !pureRead(u.access) && needed[u.resource.index]; });
        if (p->kept) for (const Use& u : p->uses) needed[u.resource.index] = true;
    }
}
// A barrier whenever a buffer's access changes, and between any two accesses
// of which one writes. Two reads in the same access need none.
void FrameGraph::planBarriers(){
    plan_.clear();
    std::vector<Access> state(resources_.size(), Access::Undefined);
    for (Res& r : resources_){ r.firstUse = -1; r.lastUse = -1; }
    for (uint32_t i = 0; i < passes_.size(); ++i){
        if (!passes_[i].kept) continue;
        Step s{i, {}};
        for (const Use& u : passes_[i].uses){
            Res& r = resources_[u.resource.index];
            if (r.firstUse < 0){ r.firstUse = (int)i; r.firstAccess = u.access; }
            r.lastUse = (int)i; r.lastAccess = u.access;
            if (r.kind == Kind::Host) continue;
            const Access before = state[u.resource.index];
            if (before != u.access || isWrite(u.access)) s.barriers.push_back({u.resource, before, u.access});
            state[u.resource.index] = u.access;
        }
        plan_.push_back(std::move(s));
    }
}
// Slots for transients whose kept lifetimes do not overlap: in first-use order, each takes the
// free slot that fits it best (or the largest free one, which grows), else a new slot. A slot is
// free for a resource when its current occupant's last use comes before the resource's first.
// A resource that takes over a slot another used is zero-filled at its first step.
void FrameGraph::assignSlots(){
    for (Slot& sl : slots_) if (sl.buffer.valid() && device_) device_->destroyBuffer(sl.buffer);
    slots_.clear();
    for (Res& r : resources_) r.slot = -1;
    if (!aliasing_ || !device_) return;
    std::vector<uint32_t> order;
    for (uint32_t i = 0; i < resources_.size(); ++i){
        const Res& r = resources_[i];
        if (r.kind == Kind::Transient && r.firstUse >= 0 && !r.buffer.valid()
            && !rhi::has(r.desc.usage, rhi::BufferUsage::MapRead)) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b){ return resources_[a].firstUse < resources_[b].firstUse; });
    std::vector<uint32_t> occupant;   // the resource now holding each slot
    for (uint32_t i : order){
        Res& r = resources_[i];
        int best = -1;
        for (int k = 0; k < (int)slots_.size(); ++k){
            if (slots_[(std::size_t)k].end >= r.firstUse) continue;
            if (best < 0){ best = k; continue; }
            const uint64_t sk = slots_[(std::size_t)k].desc.size, sb = slots_[(std::size_t)best].desc.size;
            const bool fitsK = sk >= r.desc.size, fitsB = sb >= r.desc.size;
            if ((fitsK && (!fitsB || sk < sb)) || (!fitsK && !fitsB && sk > sb)) best = k;
        }
        if (best < 0){
            slots_.push_back({{r.desc.size, r.desc.usage, "aliased slot"}, {}, r.lastUse});
            occupant.push_back(i);
            r.slot = (int)slots_.size() - 1;
            continue;
        }
        Slot& sl = slots_[(std::size_t)best];
        sl.desc.size = std::max(sl.desc.size, r.desc.size);
        sl.desc.usage = sl.desc.usage | r.desc.usage;
        sl.end = r.lastUse;
        const Access prevLast = resources_[occupant[(std::size_t)best]].lastAccess;
        occupant[(std::size_t)best] = i;
        r.slot = best;
        if (noFill_) continue;   // the false-success control: no zero fill
        for (Step& st : plan_){
            if ((int)st.pass != r.firstUse) continue;
            st.fills.push_back({{i}, prevLast});
            for (Barrier& b : st.barriers) if (b.resource.index == i && b.before == Access::Undefined) b.before = Access::CopyDst;
        }
        sl.desc.usage = sl.desc.usage | rhi::BufferUsage::CopyDst;   // the fill is an upload
    }
}
bool FrameGraph::compile(std::string& err){
    if (!validate(err)) return false;
    cull();
    planBarriers();
    assignSlots();
    compiled_ = true;
    return true;
}
MemoryStats FrameGraph::memoryStats() const {
    MemoryStats m; m.aliasing = aliasing_; m.slots = (uint32_t)slots_.size();
    for (const Res& r : resources_){
        if (r.kind != Kind::Transient || r.firstUse < 0) continue;
        ++m.transients; m.unaliasedBytes += r.desc.size;
        if (r.slot < 0) m.aliasedBytes += r.desc.size;
    }
    for (const Slot& sl : slots_) m.aliasedBytes += sl.desc.size;
    return m;
}
// RAW_NATIVE_GRAPH_STATS=FILE: one JSON line per execute, for the aliasing record.
void FrameGraph::writeStats() const {
    const char* path = std::getenv("RAW_NATIVE_GRAPH_STATS");
    if (!path || !*path) return;
    FILE* f = std::fopen(path, "a");
    if (!f){ std::fprintf(stderr, "frame graph: cannot append to RAW_NATIVE_GRAPH_STATS=%s\n", path); return; }
    const MemoryStats m = memoryStats();
    const std::string first = passes_.empty() ? "" : passes_.front().name, last = passes_.empty() ? "" : passes_.back().name;
    std::fprintf(f, "{\"graph\":\"%s..%s\",\"passes\":%zu,\"aliasing\":%s,\"transients\":%u,\"slots\":%u,"
                    "\"unaliased_bytes\":%llu,\"aliased_bytes\":%llu}\n",
                 first.c_str(), last.c_str(), passes_.size(), m.aliasing ? "true" : "false", m.transients, m.slots,
                 (unsigned long long)m.unaliasedBytes, (unsigned long long)m.aliasedBytes);
    std::fclose(f);
}
bool FrameGraph::createBuffers(std::string& err){
    for (Res& r : resources_){
        if (r.kind != Kind::Transient || r.firstUse < 0 || r.slot >= 0 || r.buffer.valid()) continue;
        r.buffer = device_->createBuffer(r.desc, err);
        if (!r.buffer.valid()){ err = "frame graph: buffer " + r.name + ": " + err; return false; }
    }
    for (Slot& sl : slots_){
        if (sl.buffer.valid()) continue;
        sl.buffer = device_->createBuffer(sl.desc, err);
        if (!sl.buffer.valid()){ err = "frame graph: aliased slot: " + err; return false; }
    }
    writeStats();
    return true;
}
bool FrameGraph::execute(std::string& err){
    if (!compiled_ && !compile(err)) return false;
    PassContext ctx{nullptr, this};
    if (!device_){
        for (const Step& s : plan_) passes_[s.pass].fn(ctx);
        return true;
    }
    if (!createBuffers(err)) return false;
    ctx.cmd = device_->begin(err);
    if (!ctx.cmd) return false;
    std::vector<rhi::BufferBarrier> b;
    for (const Step& s : plan_){
        for (const Fill& f : s.fills){
            const rhi::BufferHandle h = buffer(f.resource);
            const uint64_t n = resources_[f.resource.index].desc.size;
            const rhi::BufferBarrier toCopy{h, f.slotBefore, Access::CopyDst};
            ctx.cmd->barrier({&toCopy, 1});
            if (zeros_.size() < n) zeros_.assign((std::size_t)n, 0);
            ctx.cmd->upload(h, 0, zeros_.data(), n);
        }
        b.clear();
        for (const Barrier& x : s.barriers) b.push_back({buffer(x.resource), x.before, x.after});
        if (!b.empty()) ctx.cmd->barrier(b);
        passes_[s.pass].fn(ctx);
    }
    if (!swapchain_) return device_->submitAndWait(err);
    swapchain_->prepare(*ctx.cmd);
    return finishPresent(err);
}
// Submit, wait, run the before-present step, present. A lost device is reported as such so the
// host can replace the device instead of failing the run.
bool FrameGraph::finishPresent(std::string& err){
    presentStatus_ = rhi::PresentStatus::NotPresented;
    if (!device_->submitAndWait(err)){
        presentStatus_ = device_->lost() ? rhi::PresentStatus::DeviceLost : rhi::PresentStatus::Failed;
        return false;
    }
    if (beforePresent_ && !beforePresent_(err)) return false;
    presentStatus_ = swapchain_->present(syncInterval_, err);
    return presentStatus_ == rhi::PresentStatus::Presented || presentStatus_ == rhi::PresentStatus::Occluded;
}
std::string FrameGraph::describe() const {
    std::string o;
    std::size_t step = 0;
    for (uint32_t i = 0; i < passes_.size(); ++i){
        const Pass& p = passes_[i];
        o += "pass " + p.name + (p.kept ? " kept" : " culled") + ":";
        for (const Use& u : p.uses) o += " " + resources_[u.resource.index].name + "=" + accessName(u.access);
        if (p.kept && step < plan_.size() && plan_[step].pass == i){
            for (const Fill& f : plan_[step].fills)
                o += std::string(" | fill ") + resources_[f.resource.index].name + " slot " + std::to_string(resources_[f.resource.index].slot);
            for (const Barrier& b : plan_[step].barriers)
                o += std::string(" | ") + resources_[b.resource.index].name + " " + accessName(b.before) + "->" + accessName(b.after);
            ++step;
        }
        o += "\n";
    }
    if (swapchain_) o += "present sync " + std::to_string(syncInterval_) + "\n";
    return o;
}
}
