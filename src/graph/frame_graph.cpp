#include "raw/graph/frame_graph.hpp"
#include <algorithm>
namespace raw::graph {
using rhi::Access;
namespace {
bool pureRead(Access a){ return a == Access::Uniform || a == Access::StorageRead || a == Access::CopySrc; }
}
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
    for (Res& r : resources_) if (r.kind == Kind::Transient && r.buffer.valid()) device_->destroyBuffer(r.buffer);
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
rhi::BufferHandle FrameGraph::buffer(Resource r) const {
    return r.index < resources_.size() ? resources_[r.index].buffer : rhi::BufferHandle{};
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
            if (r.firstUse < 0) r.firstUse = (int)i;
            r.lastUse = (int)i;
            if (r.kind == Kind::Host) continue;
            const Access before = state[u.resource.index];
            if (before != u.access || isWrite(u.access)) s.barriers.push_back({u.resource, before, u.access});
            state[u.resource.index] = u.access;
        }
        plan_.push_back(std::move(s));
    }
}
bool FrameGraph::compile(std::string& err){
    if (!validate(err)) return false;
    cull();
    planBarriers();
    compiled_ = true;
    return true;
}
bool FrameGraph::createBuffers(std::string& err){
    for (Res& r : resources_){
        if (r.kind != Kind::Transient || r.firstUse < 0 || r.buffer.valid()) continue;
        r.buffer = device_->createBuffer(r.desc, err);
        if (!r.buffer.valid()){ err = "frame graph: buffer " + r.name + ": " + err; return false; }
    }
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
        b.clear();
        for (const Barrier& x : s.barriers) b.push_back({buffer(x.resource), x.before, x.after});
        if (!b.empty()) ctx.cmd->barrier(b);
        passes_[s.pass].fn(ctx);
    }
    return device_->submitAndWait(err);
}
std::string FrameGraph::describe() const {
    std::string o;
    std::size_t step = 0;
    for (uint32_t i = 0; i < passes_.size(); ++i){
        const Pass& p = passes_[i];
        o += "pass " + p.name + (p.kept ? " kept" : " culled") + ":";
        for (const Use& u : p.uses) o += " " + resources_[u.resource.index].name + "=" + accessName(u.access);
        if (p.kept && step < plan_.size() && plan_[step].pass == i){
            for (const Barrier& b : plan_[step].barriers)
                o += std::string(" | ") + resources_[b.resource.index].name + " " + accessName(b.before) + "->" + accessName(b.after);
            ++step;
        }
        o += "\n";
    }
    return o;
}
}
